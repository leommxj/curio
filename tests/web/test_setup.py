"""Run with a Python environment containing Playwright and Chromium."""
import copy
from pathlib import Path
import unittest

from browser import BrowserTest

FIXTURE = """version: 1
access:
  admin_token: admin-token-123456789
  collector_token: collector-token-123456789
device:
  brightness: 75
screen:
  pages:
    - id: main
      widgets: []
alerts:
  volume: 35
"""
CERT = "-----BEGIN CERTIFICATE-----\nexample-certificate\n-----END CERTIFICATE-----\n"
KEY = "-----BEGIN PRIVATE KEY-----\nexample-key\n-----END PRIVATE KEY-----\n"


class SetupBrowserTest(BrowserTest):
    def setUp(self):
        super().setUp()
        self.page.add_init_script("Object.defineProperty(window, 'structuredClone', {value: undefined})")
        self.source = FIXTURE
        self.revision = 1
        self.saved = []
        self.restarts = 0
        self.resets = 0
        self.fail_reset = False
        self.fail_next_save = False
        self.remote_wifi_on_conflict = False
        self.fail_get = False
        self.page.route("**/api/v2/setup**", self.api)

    def api(self, route):
        request = route.request
        path = request.url.split("/api/v2/setup", 1)[1]
        if path == "/reset":
            self.assertEqual(request.method, "POST")
            self.resets += 1
            if self.fail_reset:
                route.fulfill(status=503, json={"error": "reset failed"})
            else:
                route.fulfill(status=200, json={"ok": True})
            return
        if path == "/restart":
            self.assertEqual(request.method, "POST")
            self.restarts += 1
            route.fulfill(status=200, body="")
            return
        self.assertEqual(path, "")
        if request.method == "PUT":
            self.assertEqual(request.headers["content-type"], "application/yaml")
            self.assertEqual(request.headers["if-match"], f'"{self.revision}"')
            if self.fail_next_save:
                self.fail_next_save = False
                self.source = self.source.replace("brightness: 75", "brightness: 60")
                if self.remote_wifi_on_conflict:
                    changed = self.page.evaluate("source => jsyaml.load(source)", self.source)
                    changed["network"]["wifi"].append({"ssid": "Remote", "password": "remote-pass"})
                    self.source = self.page.evaluate("value => jsyaml.dump(value)", changed)
                self.revision += 1
                route.fulfill(status=409, json={"error": "配置已变更"})
                return
            config = self.page.evaluate("source => jsyaml.load(source)", request.post_data)
            self.saved.append(copy.deepcopy(config))
            if config["network"]["wifi"] and "tls" not in config:
                config["tls"] = {"certificate": CERT, "private_key": KEY}
            self.source = self.page.evaluate("config => jsyaml.dump(config)", config)
            self.revision += 1
        else:
            self.assertEqual(request.method, "GET")
            if self.fail_get:
                route.fulfill(status=503, json={"error": "unavailable"})
                return
        route.fulfill(body=self.source, content_type="application/yaml",
                      headers={"ETag": f'"{self.revision}"'})

    def open(self):
        self.page.goto(f"http://127.0.0.1:{self.server.server_port}/setup.html")
        self.page.locator("#setup-form").wait_for(state="visible")
        self.assertEqual(self.page.evaluate("typeof structuredClone"), "undefined")

    def test_save_download_restart_and_reload(self):
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.open()
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)
        self.page.locator("#ssid").fill(" Office Wi-Fi ")
        self.page.locator("#password").fill("password123")
        self.page.locator("#hostname").fill("curio-desk")
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertEqual(self.saved[0]["network"], {
            "wifi": [{"ssid": " Office Wi-Fi ", "password": "password123"}], "hostname": "curio-desk"})
        self.assertEqual(self.saved[0]["device"]["brightness"], 75)
        self.assertEqual(self.saved[0]["alerts"]["volume"], 35)
        self.assertNotIn("tls", self.saved[0])
        self.assertEqual(self.page.locator("#admin-token").inner_text(), "admin-token-123456789")
        self.assertEqual(self.page.locator("#collector-token").inner_text(), "collector-token-123456789")
        self.assertEqual(self.page.locator("#device-link").get_attribute("href"), "https://curio-desk.local")
        with self.page.expect_download() as info:
            self.page.locator("#download-cert").click()
        self.assertEqual(info.value.suggested_filename, "server-cert.pem")
        self.assertEqual(Path(info.value.path()).read_text(), CERT)
        with self.page.expect_download() as info:
            self.page.locator("#download-config").click()
        self.assertEqual(info.value.suggested_filename, "curio.yaml")
        exported = self.page.evaluate("source => jsyaml.load(source)", Path(info.value.path()).read_text())
        self.assertEqual(exported["tls"]["private_key"], KEY)
        self.assertEqual(self.restarts, 0)
        self.page.locator("#restart").click()
        self.page.wait_for_function("document.querySelector('#setup-status').textContent.includes('重启')")
        self.assertEqual(self.restarts, 1)
        with self.page.expect_download() as info:
            self.page.locator("#download-config").click()
        exported = self.page.evaluate("source => jsyaml.load(source)", Path(info.value.path()).read_text())
        self.assertEqual(exported["access"]["admin_token"], "admin-token-123456789")
        self.fail_get = True
        self.page.reload()
        self.page.locator("#reload-setup").wait_for(state="visible")
        self.assertTrue(self.page.locator("#setup-saved").is_hidden())
        self.assertIn("unavailable", self.page.locator("#setup-status").inner_text())
        self.fail_get = False
        self.source = FIXTURE
        self.revision = 1
        self.page.locator("#reload-setup").click()
        self.page.locator("#setup-form").wait_for(state="visible")
        self.assertEqual(self.page.locator("#ssid").input_value(), "")

    def test_existing_config_preserved_and_failed_save_retried(self):
        self.source = FIXTURE + "network: {wifi: [{ssid: Old Wi-Fi, password: old-password}, {ssid: Backup, password: backup-pass}], hostname: curio}\n" + \
            "tls: {certificate: 'certificate', private_key: 'key'}\n"
        self.open()
        self.assertEqual(self.page.locator("#ssid").input_value(), "Old Wi-Fi")
        self.assertEqual(self.page.locator("#password").input_value(), "old-password")
        self.page.locator("#hostname").fill("new-curio")
        self.page.locator("[data-ipv4-mode]").select_option("static")
        ipv4 = dict(address="192.168.1.50", netmask="255.255.255.0", gateway="192.168.1.1", dns="8.8.8.8")
        for field, value in ipv4.items():
            self.page.locator(f'[data-ipv4-field="{field}"]').fill(value)
        self.fail_next_save = True
        self.remote_wifi_on_conflict = True
        self.page.locator("#save-setup").click()
        self.page.wait_for_function("document.querySelector('#setup-status').textContent.includes('配置已变更')")
        self.assertTrue(self.page.locator("#setup-saved").is_hidden())
        self.assertEqual(self.page.locator("#hostname").input_value(), "new-curio")
        self.assertTrue(self.page.locator("#reload-setup").is_visible())
        self.page.locator("#reload-setup").click()
        self.page.wait_for_function("etag === '\"2\"'")
        self.assertEqual(self.page.locator("#hostname").input_value(), "new-curio")
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertEqual(self.saved[0]["network"]["wifi"][0]["password"], "old-password")
        self.assertEqual(self.saved[0]["network"]["wifi"][0]["ipv4"], ipv4)
        self.assertEqual([wifi["ssid"] for wifi in self.saved[0]["network"]["wifi"]], ["Old Wi-Fi", "Backup", "Remote"])
        self.assertNotIn("tls", self.saved[0])
        self.assertEqual(self.saved[0]["device"]["brightness"], 60)
        self.assertEqual(self.saved[0]["access"]["admin_token"], "admin-token-123456789")
        self.page.locator("#restart").click()
        self.page.wait_for_function("document.querySelector('#restart').disabled")
        self.page.locator("#edit-setup").click()
        self.page.locator("#password").fill("")
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertFalse(self.page.locator("#restart").is_disabled())
        self.assertEqual(self.saved[1]["network"]["wifi"][0]["password"], "")
        self.assertEqual(self.saved[1]["tls"]["certificate"], CERT)

    def test_conflict_reload_after_all_networks_removed(self):
        self.source = FIXTURE + "network: {wifi: [{ssid: Office, password: password123}]}\n"
        self.open()
        self.page.locator("#hostname").fill("curio-desk")
        self.fail_next_save = True
        self.page.locator("#save-setup").click()
        self.page.wait_for_function("document.querySelector('#setup-status').textContent.includes('配置已变更')")
        self.source = FIXTURE
        self.page.locator("#reload-setup").click()
        self.page.wait_for_function("etag === '\"2\"'")
        self.assertEqual(self.page.locator("#wifi-select option").count(), 1)
        self.assertEqual(self.page.locator("#ssid").input_value(), "")
        self.assertEqual(self.page.locator("#hostname").input_value(), "curio-desk")
        self.page.locator("#ssid").fill("New Wi-Fi")
        self.page.locator("#password").fill("new-password")
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertEqual(self.saved[0]["network"], {
            "wifi": [{"ssid": "New Wi-Fi", "password": "new-password"}], "hostname": "curio-desk"})

    def test_static_ipv4_validation_reload_and_dhcp(self):
        self.source = FIXTURE + "network: {wifi: [{ssid: Office, password: password123}]}\n" + \
            "tls: {certificate: old-certificate, private_key: old-key}\n"
        self.open()
        self.page.locator("[data-ipv4-mode]").select_option("static")
        ipv4 = dict(address="192.168.1.50", netmask="255.255.255.0", gateway="192.168.1.1", dns="8.8.8.8")
        for field, value in ipv4.items():
            self.page.locator(f'[data-ipv4-field="{field}"]').fill(value)
        self.page.locator('[data-ipv4-field="gateway"]').fill("192.168.2.1")
        self.page.locator("#save-setup").click()
        self.assertIn("ipv4", self.page.locator("#setup-status").inner_text())
        self.assertEqual(self.page.locator('[data-ipv4-field="gateway"]').input_value(), "192.168.2.1")
        self.page.locator('[data-ipv4-field="gateway"]').fill(ipv4["gateway"])
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertEqual(self.saved[-1]["network"]["wifi"][0]["ipv4"], ipv4)
        self.assertEqual(self.saved[-1]["tls"]["certificate"], "old-certificate")
        self.page.reload()
        self.page.locator("#setup-form").wait_for(state="visible")
        self.assertEqual(self.page.locator('[data-ipv4-field="address"]').input_value(), ipv4["address"])
        self.page.locator("[data-ipv4-mode]").select_option("dhcp")
        self.assertTrue(self.page.locator("[data-ipv4-fields]").is_hidden())
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertNotIn("ipv4", self.saved[-1]["network"]["wifi"][0])

    def test_default_hostname_keeps_existing_certificate(self):
        self.source = FIXTURE + "network: {wifi: [{ssid: Office, password: password123}]}\n" + \
            "tls: {certificate: old-certificate, private_key: old-key}\n"
        self.open()
        self.assertEqual(self.page.locator("#hostname").input_value(), "curio")
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertEqual(self.saved[0]["tls"],
                         {"certificate": "old-certificate", "private_key": "old-key"})

    def test_edit_or_add_wifi_preserves_other_networks(self):
        self.source = FIXTURE + "network:\n  hostname: curio\n  wifi:\n    - {ssid: First, password: first-pass}\n    - {ssid: Second, password: second-pass}\n"
        self.open()
        self.page.locator("#wifi-select").select_option("1")
        self.assertEqual(self.page.locator("#ssid").input_value(), "Second")
        self.page.locator("#password").fill("changed-pass")
        self.page.locator("#add-setup-wifi").click()
        self.page.locator("#ssid").fill("Third")
        self.page.locator("#password").fill("a" * 64)
        self.page.locator("#wifi-select").select_option("0")
        self.assertEqual(self.page.locator("#ssid").input_value(), "First")
        self.page.locator("#save-setup").click()
        self.page.locator("#setup-saved").wait_for(state="visible")
        self.assertEqual(self.saved[0]["network"]["wifi"], [
            {"ssid": "First", "password": "first-pass"},
            {"ssid": "Second", "password": "changed-pass"},
            {"ssid": "Third", "password": "a" * 64},
        ])

    def test_wifi_validation(self):
        self.open()
        self.page.locator("#ssid").fill("Office")
        self.page.locator("#password").fill("short")
        self.page.locator("#save-setup").click()
        self.assertIn("密码", self.page.locator("#setup-status").inner_text())
        self.assertEqual(self.saved, [])
        self.page.locator("#password").fill("password123")
        self.page.locator("#add-setup-wifi").click()
        self.page.locator("#ssid").fill("Office")
        self.page.locator("#save-setup").click()
        self.assertIn("重复", self.page.locator("#setup-status").inner_text())
        self.assertEqual(self.saved, [])

    def test_reset_confirmation_failure_and_retry(self):
        self.open()
        self.page.locator("#ssid").fill("Office")
        self.page.once("dialog", lambda dialog: dialog.dismiss())
        self.page.locator("#reset-device").click()
        self.assertEqual(self.resets, 0)
        self.assertEqual(self.page.locator("#ssid").input_value(), "Office")
        self.fail_reset = True
        self.page.once("dialog", lambda dialog: dialog.accept())
        self.page.locator("#reset-device").click()
        self.page.wait_for_function("document.querySelector('#setup-status').textContent.includes('reset failed')")
        self.assertFalse(self.page.locator("#reset-device").is_disabled())
        self.assertFalse(self.page.locator("#ssid").is_disabled())
        self.assertEqual(self.page.locator("#ssid").input_value(), "Office")
        self.fail_reset = False
        self.page.once("dialog", lambda dialog: dialog.accept())
        self.page.locator("#reset-device").click()
        self.page.wait_for_function("document.querySelector('#setup-status').textContent.includes('重新连接热点')")
        self.assertEqual(self.resets, 2)
        self.assertTrue(self.page.locator("#setup-form").is_hidden())
        self.assertTrue(self.page.locator("#setup-saved").is_hidden())
        self.assertTrue(self.page.locator("#reset-device").is_disabled())
        self.assertEqual(self.restarts, 0)


if __name__ == "__main__":
    unittest.main()
