"""Run with a Python environment containing Playwright and Chromium."""
import json
import importlib.util
from pathlib import Path
import unittest

from browser import BrowserTest, WEB
FIXTURE = """# Curio config
version: 1
access:
  admin_token: admin-token-123456789
  collector_token: collector-token-123456789
device:
  brightness: 75
  rotation: 90
  auto_rotate: true
network:
  hostname: curio-studio
  wifi:
    - ssid: Studio WiFi
      password: password123
tls:
  certificate: |
    -----BEGIN CERTIFICATE-----
    initial-cert
    -----END CERTIFICATE-----
  private_key: |
    -----BEGIN PRIVATE KEY-----
    initial-key
    -----END PRIVATE KEY-----
screen:
  pages:
    - id: main
      title: Main
      widgets:
        - type: agents
          at: [0, 1]
          size: [4, 5]
        - type: metric
          at: [0, 0]
          size: [1, 1]
          host: workstation
          source: system.cpu
          key: usage_pct
          unit: percent
alerts:
  volume: 35
  rules:
    - type: agent_done
    - type: metric_high
      host: workstation
      source: system.memory
      key: usage_pct
      unit: percent
      threshold: 90
      recover: 85
    - type: host_offline
"""


class ConfigBrowserTest(BrowserTest):
    def setUp(self):
        super().setUp()
        self.source, self.revision = FIXTURE, 1
        self.admin = "admin-token-123456789"
        self.state = {"hosts": []}
        self.requests = []
        self.conflict = False
        self.regenerate_tls = False
        self.page.on("dialog", lambda dialog: dialog.accept())
        self.page.route("**/api/v2/**", self.api)
        self.page.goto(f"http://127.0.0.1:{self.server.server_port}/")
        self.page.locator("#token").fill(self.admin)
        self.page.locator("#login button").click()
        self.page.locator("main").wait_for(state="visible")

    def api(self, route):
        request = route.request
        path = request.url.split("/api/v2/")[1]
        self.requests.append((path, request.method, request.headers, request.post_data))
        if request.headers.get("authorization") != "Bearer " + self.admin:
            route.fulfill(status=401, json={"error": "Unauthorized"})
            return
        if path == "state":
            route.fulfill(json=self.state)
            return
        if request.method != "GET":
            if self.conflict:
                route.fulfill(status=409, json={"error": "Config changed"})
                return
            if path == "config":
                assert request.headers["content-type"] == "application/yaml"
                assert request.headers["if-match"] == f'"{self.revision}"'
                self.source = request.post_data
                if self.regenerate_tls and "\ntls:" not in self.source:
                    self.source += "\ntls:\n  certificate: |\n    replacement-cert\n  private_key: |\n    replacement-key\n"
            elif path == "restart":
                assert request.method == "POST"
                route.fulfill(status=202, json={"status": "restarting"})
                return
            elif path == "tokens/admin":
                new_token = json.loads(request.post_data)["token"]
                self.source = self.source.replace(self.admin, new_token)
            elif path == "tokens/collector":
                old = self.page.evaluate("jsyaml.load(yamlText).access.collector_token")
                self.source = self.source.replace(old, "new-collector-token-123456789")
            else:
                raise AssertionError(path)
            self.revision += 1
            self.admin = self.page.evaluate("source => jsyaml.load(source).access.admin_token", self.source)
        route.fulfill(body=self.source, content_type="application/yaml", headers={"ETag": f'"{self.revision}"'})

    def tab(self, name):
        self.page.locator(f'[data-tab="{name}"]').click()

    def change(self, selector, value):
        field = self.page.locator(selector)
        field.fill(value)
        field.dispatch_event("change")

    def save(self, selector):
        self.page.locator(selector).click()
        self.page.wait_for_function("!dirty()")

    def config(self):
        return self.page.evaluate("jsyaml.load(yamlText)")

    def test_forms_yaml_credentials_and_conflict(self):
        self.tab("alerts")
        volume = self.page.locator('[data-alert-field="volume"]')
        volume.fill("")
        volume.dispatch_event("change")
        writes = sum(method == "PUT" for _, method, *_ in self.requests)
        self.page.locator("#save-alerts").click()
        self.page.wait_for_function("/音量|volume/.test(document.querySelector('#status').textContent)")
        self.assertEqual(volume.input_value(), "")
        self.assertEqual(sum(method == "PUT" for _, method, *_ in self.requests), writes)
        volume.fill("35")
        volume.dispatch_event("change")
        self.tab("device")
        self.change("#brightness", "61")
        self.save("#save-device")
        document = self.config()
        self.assertEqual(document["device"]["brightness"], 61)
        self.assertEqual(document["network"]["wifi"][0]["ssid"], "Studio WiFi")
        self.assertIn("initial-cert", document["tls"]["certificate"])
        self.assertEqual(document["access"]["collector_token"], "collector-token-123456789")
        self.assertEqual(document["screen"]["pages"][0]["widgets"][1]["host"], "workstation")
        self.assertEqual(document["alerts"]["rules"][1]["hold"], 60)
        self.assertEqual(document["alerts"]["rules"][0]["duration"], 10)
        self.assertNotIn("host_id", document["alerts"]["rules"][1])
        self.tab("config")
        self.page.locator("#new-admin-token").fill("new-admin-token-123456789")
        self.page.locator("#save-admin-token").click()
        self.page.wait_for_function("sessionStorage.getItem('curio-token') === 'new-admin-token-123456789'")
        self.page.locator("#generate-collector-token").click()
        self.page.wait_for_function("configDocument.access.collector_token === 'new-collector-token-123456789'")
        with self.page.expect_download() as info:
            self.page.locator("#export-yaml").click()
        exported = Path(info.value.path()).read_text()
        self.assertIn("new-admin-token-123456789", exported)
        self.assertIn("new-collector-token-123456789", exported)
        self.assertEqual(info.value.suggested_filename, "curio.yaml")
        self.page.locator("#yaml-editor").fill(exported + "\n# pending edit\n")
        self.assertTrue(self.page.locator("#generate-collector-token").is_disabled())
        self.assertTrue(self.page.locator("#save-admin-token").is_disabled())
        self.conflict = True
        self.page.locator("#save-config").click()
        self.page.wait_for_function("document.querySelector('#status').textContent.includes('配置已变更')")
        self.assertTrue(self.page.locator("#yaml-editor").input_value().endswith("# pending edit\n"))
        self.assertTrue(all(path in ("config", "state", "tokens/admin", "tokens/collector") for path, *_ in self.requests))

    def test_network_edit_certificate_and_restart(self):
        self.tab("device")
        self.assertEqual(self.page.locator('[data-wifi-index="0"] [data-wifi-field="ssid"]').input_value(), "Studio WiFi")
        self.assertEqual(self.page.locator("#network-hostname").input_value(), "curio-studio")
        with self.page.expect_download() as info:
            self.page.locator("#download-certificate").click()
        self.assertEqual(info.value.suggested_filename, "curio-studio.crt")
        self.assertIn("initial-cert", Path(info.value.path()).read_text())
        self.change('[data-wifi-index="0"] [data-wifi-field="ssid"]', "New Studio")
        self.change("#network-hostname", "curio-renamed")
        self.assertTrue(self.page.locator("#download-certificate").is_disabled())
        self.regenerate_tls = True
        self.save("#save-device")
        submitted = [body for path, method, _, body in self.requests if path == "config" and method == "PUT"][-1]
        self.assertNotIn("tls:", submitted)
        self.assertEqual(self.config()["network"]["hostname"], "curio-renamed")
        self.assertIn("replacement-cert", self.config()["tls"]["certificate"])
        self.assertFalse(any(path == "restart" for path, *_ in self.requests))
        with self.page.expect_download() as info:
            self.page.locator("#download-certificate").click()
        self.assertEqual(info.value.suggested_filename, "curio-renamed.crt")
        self.assertIn("replacement-cert", Path(info.value.path()).read_text())
        self.regenerate_tls = False
        self.tab("config")
        imported = self.config()
        imported["network"]["hostname"] = "curio-imported"
        imported["tls"] = {"certificate": "imported-cert", "private_key": "imported-key"}
        self.page.locator("#yaml-editor").fill(json.dumps(imported))
        self.tab("device")
        self.change("#brightness", "59")
        self.save("#save-device")
        self.assertEqual(self.config()["tls"], {"certificate": "imported-cert", "private_key": "imported-key"})
        self.assertEqual(self.config()["network"]["hostname"], "curio-imported")
        self.page.locator("#restart-device").click()
        self.page.wait_for_function("document.querySelector('#status').textContent.includes('设备正在重启')")
        restarts = [(method, headers) for path, method, headers, _ in self.requests if path == "restart"]
        self.assertEqual(len(restarts), 1)
        self.assertEqual(restarts[0][0], "POST")
        self.assertEqual(restarts[0][1]["authorization"], "Bearer " + self.admin)

    def test_static_ipv4_yaml_reload_and_dhcp(self):
        self.tab("device")
        self.page.locator("[data-ipv4-mode]").select_option("static")
        ipv4 = dict(address="192.168.1.50", netmask="255.255.255.0", gateway="192.168.1.1", dns="8.8.8.8")
        for field, value in ipv4.items():
            self.change(f'[data-ipv4-field="{field}"]', value)
        self.save("#save-device")
        self.assertEqual(self.config()["network"]["wifi"][0]["ipv4"], ipv4)
        self.assertIn("initial-cert", self.config()["tls"]["certificate"])
        with self.page.expect_download() as info:
            self.page.locator("#download-certificate").click()
        self.assertIn("initial-cert", Path(info.value.path()).read_text())
        invalid = [dict(ipv4, address="192.168.01.50"), dict(ipv4, address="192.168.1.0"),
                   dict(ipv4, netmask="255.0.255.0"), dict(ipv4, netmask="255.255.255.254"),
                   dict(ipv4, gateway="192.168.2.1"), dict(ipv4, dns="127.0.0.1"), dict(ipv4, toString="extra"),
                   {key: value for key, value in ipv4.items() if key != "dns"}]
        for value in invalid:
            self.assertTrue(self.page.evaluate("ipv4 => { try { validateWifi([{ssid: 'Office', ipv4}]); return false; } catch (_) { return true; } }", value))
        self.tab("config")
        self.assertIn("192.168.1.50", self.page.locator("#yaml-editor").input_value())
        self.tab("device")
        self.page.locator("#reload").click()
        self.page.wait_for_function("document.querySelector('[data-ipv4-field=address]').value === '192.168.1.50'")
        self.page.locator("[data-ipv4-mode]").select_option("dhcp")
        self.assertTrue(self.page.locator("[data-ipv4-fields]").is_hidden())
        self.save("#save-device")
        self.assertNotIn("ipv4", self.config()["network"]["wifi"][0])

    def test_wifi_list_order_and_removal(self):
        self.tab("device")
        self.page.locator("#add-wifi").click()
        second = self.page.locator('[data-wifi-index="1"]')
        second.locator('[data-wifi-field="ssid"]').fill("Guest")
        second.locator('[data-wifi-field="ssid"]').dispatch_event("change")
        second.locator('[data-wifi-field="password"]').fill("a" * 64)
        second.locator('[data-wifi-field="password"]').dispatch_event("change")
        self.page.locator('[data-wifi-index="1"] [data-wifi-move="up"]').click()
        self.save("#save-device")
        self.assertEqual(self.config()["network"]["wifi"], [
            {"ssid": "Guest", "password": "a" * 64},
            {"ssid": "Studio WiFi", "password": "password123"},
        ])
        self.page.locator('[data-wifi-index="1"] [data-wifi-remove]').click()
        self.save("#save-device")
        self.assertEqual([wifi["ssid"] for wifi in self.config()["network"]["wifi"]], ["Guest"])
        self.tab("config")
        original = self.page.locator("#yaml-editor").input_value()
        empty = self.page.evaluate("source => { const c = jsyaml.load(source); c.network.wifi = []; return jsyaml.dump(c); }", original)
        self.page.locator("#yaml-editor").fill(empty)
        self.tab("device")
        self.assertEqual(self.page.locator(".wifi-row").count(), 0)

    def test_reported_data_refresh_and_metric_binding(self):
        self.state = {"hosts": [{
            "host_id": "workstation", "host_name": "Office", "health": "fresh",
            "sources": {
                "system.cpu": {"kind": "metrics", "status": "ok", "health": "fresh",
                               "metrics": {"usage_pct": {"unit": "percent", "value": 42, "age_ms": 0}}},
                "herdr": {"kind": "agents", "status": "ok", "health": "fresh",
                          "agents": [{"id": "agent-1", "name": "Build", "brand": "codex", "project": "curio", "state": "working"}]},
            },
        }]}
        self.tab("data")
        self.page.locator("#refresh-data").click()
        self.page.locator(".catalog-agent strong", has_text="Build").wait_for()
        self.tab("screen")
        self.page.locator("#widget-select").select_option("metric-2")
        metric = self.page.locator('.widget[data-index="1"]')
        metric.locator("[data-metric-select]").select_option("0")
        self.assertEqual(self.page.locator(".metric-value").inner_text(), "42%")
        self.assertEqual(self.page.locator(".agent-name strong").inner_text(), "Build")
        self.save("#save")
        binding = self.config()["screen"]["pages"][0]["widgets"][1]
        self.assertEqual((binding["host"], binding["source"], binding["key"]),
                         ("workstation", "system.cpu", "usage_pct"))
        self.assertEqual(self.config()["network"]["hostname"], "curio-studio")
        self.assertIn("initial-cert", self.config()["tls"]["certificate"])
        self.assertFalse(any(path == "catalog" for path, *_ in self.requests))
        self.tab("alerts")
        rule = self.page.locator(".alert-rule").nth(1)
        rule.locator("summary").first.click()
        rule.locator("[data-alert-metric]").select_option("0")
        self.assertTrue(self.page.evaluate("dirtySections.has('alerts')"))
        self.save("#save-alerts")
        binding = self.config()["alerts"]["rules"][1]
        self.assertEqual((binding["source"], binding["key"]), ("system.cpu", "usage_pct"))

    def test_yaml_admin_change_and_mobile_layout(self):
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.tab("config")
        self.page.locator("#yaml-editor").fill(FIXTURE.replace(self.admin, "raw-admin-token-123456789"))
        self.page.locator("#save-config").click()
        self.page.wait_for_function("sessionStorage.getItem('curio-token') === 'raw-admin-token-123456789'")
        self.page.locator("#reload-config").click()
        self.page.wait_for_function("document.querySelector('main').inert === false")
        self.assertEqual(self.config()["access"]["admin_token"], "raw-admin-token-123456789")
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)
        self.page.evaluate("Object.defineProperty(navigator, 'clipboard', {value: {writeText: async value => window.copied = value}, configurable: true})")
        self.page.locator("#copy-collector-token").click()
        self.assertEqual(self.page.evaluate("window.copied"), "collector-token-123456789")

    def test_import_parse_errors_and_shared_draft(self):
        self.tab("config")
        self.page.locator("#yaml-editor").fill("screen: [\n")
        self.tab("device")
        self.assertTrue(self.page.locator("#yaml-editor").is_visible())
        self.assertIn("(2:1)", self.page.locator("#status").inner_text())
        self.assertEqual(self.page.locator("#yaml-editor").input_value(), "screen: [\n")
        unsupported = FIXTURE.replace("brightness: 75", "brightness: 75\n  unsupported: true")
        self.page.locator("#yaml-editor").fill(unsupported)
        self.tab("device")
        self.assertIn("device.unsupported", self.page.locator("#status").inner_text())
        self.assertEqual(self.page.locator("#yaml-editor").input_value(), unsupported)
        self.page.locator("#import-yaml").set_input_files({
            "name": "import.yml", "mimeType": "application/yaml",
            "buffer": FIXTURE.replace("brightness: 75", "brightness: 23").encode(),
        })
        self.page.wait_for_function("device.brightness === 23")
        self.assertIn("# Curio config", self.page.locator("#yaml-editor").input_value())
        self.tab("device")
        self.assertEqual(self.page.locator("#brightness").input_value(), "23")
        self.change("#brightness", "24")
        self.tab("alerts")
        self.change('[data-alert-field="volume"]', "42")
        self.tab("config")
        self.assertEqual(self.config()["device"]["brightness"], 24)
        self.assertEqual(self.config()["alerts"]["volume"], 42)
        self.save("#save-config")
        self.tab("device")
        self.assertEqual(self.page.locator("#brightness").input_value(), "24")

    def test_example_and_module_cases_match_native_config(self):
        root = WEB.parents[2]
        spec = importlib.util.spec_from_file_location("curio_dev_hub", root / "scripts/dev/hub.py")
        hub = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(hub)
        core = hub.Core()
        registry = json.loads((root / "firmware/modules/registry.json").read_text())
        try:
            example = (root / "curio.example.yaml").read_text()
            self.page.evaluate("parseConfig", example)
            canonical = core.config_yaml(example.encode()).decode()
            self.page.evaluate("parseConfig", canonical)
            for category, names in registry.items():
                for name in names:
                    path = root / "firmware/modules" / category / name / "cases.json"
                    cases = json.loads(path.read_text())
                    self.assertTrue(cases, str(path))
                    for case in cases:
                        with self.subTest(module=f"{category}/{name}", case=case["name"]):
                            source = self.page.evaluate("""([fixture, category, name, options]) => {
                                const c = jsyaml.load(fixture);
                                c.screen.pages[0].widgets = [];
                                c.alerts.rules = [];
                                if (category === "widgets") {
                                    c.screen.pages[0].widgets = [{type: name, at: [0, 0],
                                        size: widgetTypes[name].minimum, ...options}];
                                } else c.alerts.rules = [{type: name, ...options}];
                                return jsyaml.dump(c);
                            }""", [FIXTURE, category, name, case["options"]])
                            browser = self.page.evaluate("""([source, category, name]) => {
                                try {
                                    const c = parseConfig(source), module = (category === "widgets" ? widgetTypes : alertTypes)[name];
                                    const item = category === "widgets" ? c.screen.pages[0].widgets[0] : c.alerts.rules[0];
                                    const normalized = {...module.defaults(), ...item};
                                    return {valid: true, options: Object.fromEntries(Object.keys(module.types)
                                        .filter(key => normalized[key] !== undefined).map(key => [key, normalized[key]]))};
                                } catch (error) {return {valid: false, error: error.message};}
                            }""", [source, category, name])
                            try:
                                canonical = core.config_yaml(source.encode()).decode()
                                native_valid = True
                            except ValueError:
                                native_valid = False
                            self.assertEqual(browser["valid"], case["valid"], browser)
                            self.assertEqual(native_valid, case["valid"])
                            if not native_valid:
                                continue
                            native = self.page.evaluate("""([source, category, name]) => {
                                const c = parseConfig(source), module = (category === "widgets" ? widgetTypes : alertTypes)[name];
                                const item = category === "widgets" ? c.screen.pages[0].widgets[0] : c.alerts.rules[0];
                                const normalized = {...module.defaults(), ...item};
                                return Object.fromEntries(Object.keys(module.types).filter(key => normalized[key] !== undefined)
                                    .map(key => [key, normalized[key]]));
                            }""", [canonical, category, name])
                            self.assertEqual(browser["options"], native)
                            for key, expected in case.get("expected", {}).items():
                                self.assertEqual(browser["options"].get(key), expected)
        finally:
            core.close()

    def test_metric_omitted_unit_preserves_reported_unit(self):
        widget = self.metric("value", [0, 0])
        del widget["unit"]
        state = {"hosts": [{"host_id": "workstation", "sources": {
            "system.cpu": {"kind": "metrics", "status": "ok", "metrics": {
                "usage_pct": {"value": 42, "unit": "count", "age_ms": 0}}}}}]}
        self.load_widgets([widget], state)
        self.assertEqual(self.widgets()[0]["unit"], "")
        self.assertEqual(self.page.locator(".metric-value").inner_text(), "42")
        self.change('[data-field="title"]', "Count")
        self.save("#save")
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["unit"], "")

    def test_module_scalar_schemas_and_hostless_alert_are_independent(self):
        self.page.evaluate("""() => {
            widgetTypes.typed = {
                label: "Typed", minimum: [1, 1], fields: ["source"], types: {source: "number"},
                defaults: () => ({title: "Typed"}), validate() {}, editor: () => "",
                classes: () => "typed-card", preview: w => String(w.source),
            };
            alertTypes.hostless = {
                label: "Hostless", fields: ["source"], types: {source: "boolean"},
                defaults: () => ({}), validate() {}, editor: () => "", summary: r => String(r.source),
            };
        }""")
        draft = self.page.evaluate("""source => {
            const c = jsyaml.load(source);
            c.screen.pages[0].widgets = [{type: "typed", at: [0, 0], size: [1, 1], source: 7},
                {...c.screen.pages[0].widgets[1], at: [1, 0]}];
            c.alerts.rules = [{type: "hostless", source: true}];
            return jsyaml.dump(c);
        }""", FIXTURE)
        self.tab("config")
        self.page.locator("#yaml-editor").fill(draft)
        self.tab("screen")
        self.assertEqual(self.page.locator(".typed-card").inner_text(), "7")
        self.save("#save")
        self.tab("alerts")
        self.save("#save-alerts")
        self.assertNotIn("host", self.config()["alerts"]["rules"][0])
        self.tab("config")
        invalid = draft.replace("source: 7", "source: invalid")
        self.page.locator("#yaml-editor").fill(invalid)
        self.tab("screen")
        self.assertTrue(self.page.locator("#yaml-editor").is_visible())
        self.assertIn("source", self.page.locator("#status").inner_text())
        self.assertEqual(self.page.locator("#yaml-editor").input_value(), invalid)

    def test_registered_widget_uses_common_editor_and_yaml_flow(self):
        self.load_widgets([])
        self.page.evaluate("""() => {
            widgetTypes.note = {
                label: "Note", minimum: [1, 1], fields: ["text"], types: {text: "string"},
                defaults: () => ({title: "Note", text: "hello"}),
                validate(w, check) {check(w.text, "Text", 48);},
                editor(w) {return field("Text", "text", w.text);},
                classes: () => "note-card", preview: w => esc(w.text),
            };
            render();
        }""")
        self.page.locator("#new-type").select_option("note")
        self.page.locator("#add-widget").click()
        self.change('[data-field="text"]', "module text")
        self.assertEqual(self.page.locator(".note-card").inner_text(), "module text")
        self.save("#save")
        saved = self.config()["screen"]["pages"][0]["widgets"][0]
        self.assertEqual((saved["type"], saved["text"]), ("note", "module text"))
        self.page.locator("#reload").click()
        self.page.wait_for_function("!document.querySelector('main').inert")
        self.assertEqual(self.page.locator(".note-card").inner_text(), "module text")

    def test_agent_done_custom_source_roundtrip(self):
        self.tab("alerts")
        self.page.locator(".alert-rule details").first.locator("summary").click()
        source = self.page.locator('[data-alert-field="source"]').first
        source.fill("team.agents")
        source.dispatch_event("change")
        self.save("#save-alerts")
        self.assertEqual(self.config()["alerts"]["rules"][0]["source"], "team.agents")
        self.assertIn("team.agents", self.page.locator(".alert-rule").first.inner_text())

    def test_alert_feedback_controls_and_yaml(self):
        self.tab("alerts")
        strength = self.page.locator('[data-alert-field="vibration_strength"]')
        self.assertEqual(strength.input_value(), "60")
        strength.fill("0")
        strength.dispatch_event("change")
        for field in ("vibrate", "flash", "flash_enabled"):
            control = self.page.locator(f'[data-alert-field="{field}"]').first
            self.assertFalse(control.is_checked())
            control.check()
        self.page.locator('.alert-rule details').first.locator('summary').click()
        for kind in ("host_offline", "agent_done"):
            self.page.locator('[data-alert-field="type"]').first.select_option(kind)
            for field in ("vibrate", "flash"):
                self.assertTrue(self.page.locator(f'[data-alert-field="{field}"]').first.is_checked())
        self.save("#save-alerts")
        alerts = self.config()["alerts"]
        self.assertEqual(alerts["vibration_strength"], 0)
        self.assertTrue(alerts["flash_enabled"])
        self.assertFalse(alerts["rules"][0]["sound"])
        for field in ("vibrate", "flash"):
            self.assertTrue(alerts["rules"][0][field])
            self.assertFalse(alerts["rules"][1][field])
        self.tab("config")
        source = self.page.locator("#yaml-editor").input_value().replace("vibration_strength: 0", "vibration_strength: 83")
        self.page.locator("#yaml-editor").fill(source)
        self.tab("alerts")
        self.assertEqual(self.page.locator('[data-alert-field="vibration_strength"]').input_value(), "83")
        for field in ("vibrate", "flash", "flash_enabled"):
            self.assertTrue(self.page.locator(f'[data-alert-field="{field}"]').first.is_checked())
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)

    def test_invalid_drafts_preserve_input(self):
        fields = [
            ("c.device.auto_rotate", ["false"], "auto_rotate"),
            ("c.device.rotation", [45], "rotation"),
            ("c.device.brightness", [-1, 101, 1.5], "亮度"),
            ("c.alerts.volume", [-1, 101, 1.5], "音量"),
            ("c.alerts.rules[0].duration", [-1, 301, 1.5], "显示时间"),
            ("c.alerts.rules[0].id", ["bad id", "x" * 49], "规则 ID"),
            ("c.device.ntp_server", ["https://time.example.com", "time.example.com/path", ""], "NTP"),
            ("c.alerts.rules[0].sound", ["false"], "sound"),
            ("c.alerts.rules[0].include_idle", [None], "include_idle"),
            ("c.version", [2], "version"),
            ("c.alerts.vibration_strength", [101, -1, 1.5, "60", True], "vibration_strength"),
            ("c.alerts.rules[0].vibrate", ["true"], "vibrate"),
            ("c.alerts.flash_enabled", [1, "true", None], "flash_enabled"),
            ("c.alerts.rules[0].flash", [1, "true", None], "flash"),
            ("c.network.wifi[0].ssid", ["", "😀" * 9], "1–32"),
            ("c.network.wifi[0].password", ["short", "z" * 64], "password"),
            ("c.screen.pages[0].id", ["", "bad id", "x" * 33], "id"),
            ("c.screen.default_page", ["missing"], "default_page"),
            ("c.screen.pages[0].widgets[0].id", [1], "id"),
        ]
        cases = [(f"{field} = {json.dumps(value)}", error)
                 for field, values, error in fields for value in values]
        cases += [
            ("c.network.wifi.push({...c.network.wifi[0]})", "重复"),
            ("c.alerts.rules = Array(9).fill({type:'agent_done'})", "最多 8"),
            ("c.alerts.rules[1].id = c.alerts.rules[0].id = 'same'", "重复"),
            ("c.network.wifi = Array(5).fill({ssid:'x'})", "最多 4"),
            ("c.network.ssid = 'legacy'", "未知字段"),
            ("c.screen.pages.push(structuredClone(c.screen.pages[0]))", "重复"),
            ("c.screen.pages[0].widgets[0].id = 'metric-2'", "重复"),
            ("c.screen.pages[0].layout = 'focus'", "未知字段"),
            ("c.screen.pages[0].widgets[0].slot = 0", "未知字段"),
            ("delete c.screen.pages[0].widgets[0].size", "size"),
            ("c.screen.pages[0].widgets[1].at = [4, 0]", "边界"),
            ("c.screen.pages[0].widgets[0].size = [2, 3]", "尺寸"),
            ("c.screen.pages[0].widgets[1].at = [0, 1]", "重叠"),
        ]
        widgets = [self.metric(str(i), [i % 4, i // 4]) for i in range(9)]
        cases.append((f"c.screen.pages[0].widgets = {json.dumps(widgets)}", "最多 8"))
        cases += [
            ("delete c.screen.pages[0].widgets[1].host", "机器 ID"),
            ("delete c.screen.pages[0].widgets[1].source", "来源 ID"),
            ("delete c.screen.pages[0].widgets[1].key", "指标键"),
            ("delete c.alerts.rules[1].source", "来源 ID"),
            ("delete c.alerts.rules[1].threshold", "有限数字"),
            ("delete c.alerts.rules[1].recover", "有限数字"),
        ]
        self.tab("config")
        for mutation, error in cases:
            with self.subTest(mutation=mutation):
                draft = self.page.evaluate("""([source, mutation]) => {
                    const c = jsyaml.load(source); eval(mutation); return jsyaml.dump(c);
                }""", [FIXTURE, mutation])
                self.page.locator("#yaml-editor").fill(draft)
                self.tab("screen")
                self.assertTrue(self.page.locator("#yaml-editor").is_visible())
                self.assertEqual(self.page.locator("#yaml-editor").input_value(), draft)
                self.assertIn(error, self.page.locator("#status").inner_text())

    def load_widgets(self, widgets, state=None):
        if state is not None:
            self.state = state
        source = self.page.evaluate("""widgets => {
            const config = jsyaml.load(yamlText);
            config.screen.pages[0].widgets = widgets;
            return jsyaml.dump(config);
        }""", widgets)
        self.tab("config")
        self.page.locator("#yaml-editor").fill(source)
        self.save("#save-config")
        self.tab("screen")
        if state is not None:
            self.tab("data")
            self.page.locator("#refresh-data").click()
            self.tab("screen")

    @staticmethod
    def metric(id, at, size=(1, 1)):
        return {"id": id, "type": "metric", "title": id, "at": at, "size": list(size),
                "host": "workstation", "source": "system.cpu", "key": "usage_pct", "unit": "percent"}

    @staticmethod
    def agents(id, at, size=(3, 3)):
        return {"id": id, "type": "agents", "title": id, "at": at, "size": list(size)}

    def widgets(self):
        return self.page.evaluate("display.pages[selected].widgets")

    def begin_drag(self, id, dx, dy, resize=False):
        card = self.page.locator(f'.card[data-widget-id="{id}"]')
        if resize:
            self.page.locator("#widget-select").select_option(id)
            rect = card.locator(".resize-handle").bounding_box()
        else:
            rect = card.bounding_box()
        scale = self.page.locator("#screen").bounding_box()["width"] / 480
        x, y = rect["x"] + rect["width"] / 2, rect["y"] + rect["height"] / 2
        self.page.mouse.move(x, y)
        self.page.mouse.down()
        self.page.mouse.move(x + dx * scale, y + dy * scale, steps=4)

    def test_drag_swap_tentative_undo_and_yaml_save(self):
        self.load_widgets([self.metric("cpu", [0, 0]), self.metric("ram", [1, 0]), self.metric("other", [3, 5])])
        before = self.widgets()
        writes = sum(method != "GET" for _, method, *_ in self.requests)
        self.begin_drag("cpu", 116, 0)
        self.assertEqual(self.widgets(), before)
        self.assertEqual(self.page.locator('.card[data-widget-id="cpu"]').evaluate("e => e.style.left"), "128px")
        self.assertEqual(self.page.locator('.card[data-widget-id="ram"]').evaluate("e => e.style.left"), "12px")
        self.page.mouse.up()
        moved = self.widgets()
        self.assertEqual([w["at"] for w in moved], [[1, 0], [0, 0], [3, 5]])
        self.assertEqual(sum(method != "GET" for _, method, *_ in self.requests), writes)
        self.assertTrue(self.page.evaluate("dirtySections.has('screen')"))
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets(), before)
        self.assertFalse(self.page.evaluate("dirtySections.has('screen')"))
        self.begin_drag("cpu", 116, 0)
        self.page.mouse.up()
        self.save("#save")
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["at"], [1, 0])
        self.assertTrue(all("at" in w and "size" in w and "slot" not in w for w in self.config()["screen"]["pages"][0]["widgets"]))
        self.assertNotIn("layout", self.config()["screen"]["pages"][0])
        self.page.locator("#reload").click()
        self.page.wait_for_function("!document.querySelector('main').inert")
        self.assertEqual(self.widgets(), moved)

    def test_resize_reflow_bounds_and_undo(self):
        self.load_widgets([self.metric("grow", [0, 0]), self.metric("right", [1, 0]), self.metric("below", [0, 1]), self.metric("corner", [1, 1]), self.agents("agents", [0, 3], [4, 3])])
        before = self.widgets()
        self.begin_drag("grow", 116, 70, resize=True)
        self.assertEqual(self.widgets(), before)
        self.assertEqual(self.page.locator('.card[data-widget-id="grow"]').evaluate("e => e.style.width"), "224px")
        self.page.mouse.up()
        after = self.widgets()
        self.assertEqual(after[0]["size"], [2, 2])
        self.assertEqual(len(after), 5)
        self.assertEqual(after[4], before[4])
        self.assertEqual(sum(a["at"] != b["at"] for a, b in zip(before, after)), 3)
        self.page.evaluate("validateLayout(page())")
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets(), before)
        self.begin_drag("grow", -116, 0)
        self.assertTrue(self.page.locator(".invalid-placement").is_visible())
        self.page.mouse.up()
        self.assertEqual(self.widgets(), before)
        self.assertIn("无法放置", self.page.locator("#status").inner_text())
        self.page.locator('[data-field="rect.col"]').select_option("4")
        self.page.locator('[data-field="rect.width"]').select_option("2")
        self.assertEqual(self.widgets()[0]["size"], [1, 1])
        self.assertEqual(self.page.locator('[data-field="rect.width"]').input_value(), "1")

    def test_general_metric_formatting_and_source_freshness(self):
        cases = [[0, "bytes", "0 B"], [1536, "bytes", "1.5 KiB"],
                 [1048576, "bytes_per_second", "1 MiB/s"], [1250, "watts", "1.3 kW"],
                 [2400000000, "hertz", "2.4 GHz"], [0.25, "seconds", "250 ms"],
                 [90000, "milliseconds", "1.5 min"], [86400, "seconds", "1 d"],
                 [23.5, "celsius", "23.5 C"], [42, "count", "42"],
                 [-0.01, "number", "0"], [-1.25, "number", "-1.3"], [-1.5, "count", "-2"], [7.25, "custom", "7.3 custom"]]
        for value, unit, expected in cases:
            self.assertEqual(self.page.evaluate("([v,u]) => formatMetric(v,u)", [value, unit]), expected)
        self.assertEqual(self.page.evaluate("formatMetric(Infinity, 'number')"), "--")
        self.page.evaluate("""() => { state = {hosts:[{host_id:'h',sources:{s:{kind:'metrics',status:'ok',max_age_ms:60000,metrics:{x:{value:5,unit:'count',age_ms:30000}}}}}]}; }""")
        binding = {"host": "h", "source": "s", "key": "x"}
        self.assertEqual(self.page.evaluate("b => resolve(b).text", binding), "5")
        self.page.evaluate("state.hosts[0].sources.s.max_age_ms = 20000")
        self.assertEqual(self.page.evaluate("b => resolve(b).text", binding), "Stale")

    def test_status_widget_binding_health_and_yaml_roundtrip(self):
        source = {"kind": "status", "status": "ok", "health": "fresh", "sample_age_ms": 0,
                  "max_age_ms": 60000, "state": {"text": "Online", "level": "ok", "detail": "HTTP 200"}}
        state = {"hosts": [{"host_id": "workstation", "sources": {"service.http": source}}]}
        self.load_widgets([], state)
        self.page.locator("#new-type").select_option("status")
        self.page.locator("#add-widget").click()
        self.page.locator("[data-status-select]").select_option("0")
        self.assertEqual(self.page.locator(".card .metric-status").inner_text(), "Online")
        self.page.locator('[data-field="rect.height"]').select_option("2")
        self.assertEqual(self.page.locator(".status-detail").inner_text(), "HTTP 200")
        self.save("#save")
        widget = self.config()["screen"]["pages"][0]["widgets"][0]
        self.assertEqual((widget["type"], widget["host"], widget["source"]), ("status", "workstation", "service.http"))
        self.tab("config")
        with self.page.expect_download() as download:
            self.page.locator("#export-yaml").click()
        exported = Path(download.value.path()).read_text()
        self.page.locator("#import-yaml").set_input_files({
            "name": "status.yaml", "mimeType": "application/yaml", "buffer": exported.encode(),
        })
        self.page.wait_for_function("dirtySections.has('config')")
        self.tab("screen")
        self.assertEqual(self.widgets()[0]["source"], "service.http")
        for mutation, expected in [("s.status='error'", "Collection error"),
                                   ("s.status='ok';s.sample_age_ms=60001", "Stale"),
                                   ("delete state.hosts[0].sources['service.http']", "Not reported")]:
            self.page.evaluate("mutation => { const s=state.hosts[0].sources['service.http']; eval(mutation); renderPreview(); }", mutation)
            self.assertEqual(self.page.locator(".card .metric-status").inner_text(), expected)

    def test_compact_metric_values_and_host_visibility(self):
        state = {"hosts": [{"host_id": "workstation", "host_name": "Office", "health": "fresh", "sources": {
            "system.cpu": {"kind": "metrics", "status": "ok", "health": "fresh", "metrics": {
                "usage_pct": {"unit": "percent", "value": 42.4, "age_ms": 0}}}}}]}
        self.load_widgets([self.metric("compact", [0, 0]), self.metric("wide", [1, 0], [2, 1])], state)
        compact = self.page.locator('.card[data-widget-id="compact"]')
        wide = self.page.locator('.card[data-widget-id="wide"]')
        self.assertEqual(compact.locator(".metric-value").inner_text(), "42.4%")
        self.assertEqual(compact.locator(".metric-host").count(), 0)
        self.assertEqual(wide.locator(".metric-host").inner_text(), "Office")
        title, value = compact.locator(".metric-title").bounding_box(), compact.locator(".metric-value").bounding_box()
        self.assertGreaterEqual(value["y"], title["y"] + title["height"])
        for card in (compact, wide):
            card_box = card.bounding_box()
            value_box = card.locator(".metric-value").bounding_box()
            self.assertLessEqual(value_box["y"] + value_box["height"], card_box["y"] + card_box["height"])

    def test_multiple_agent_previews_are_independent_and_transient(self):
        agents = [{"id": f"agent-{i}", "name": f"Build {i}", "brand": "codex", "project": "curio", "state": "blocked" if i < 3 else "working"} for i in range(10)]
        state = {"hosts": [{"host_id": "workstation", "host_name": "Office", "health": "fresh", "sources": {
            "herdr": {"kind": "agents", "status": "ok", "health": "fresh", "agents": agents}}},
            {"host_id": "unreported", "host_name": "A machine with a very long name", "health": "fresh", "sources": {}}]}
        self.load_widgets([self.agents("top", [0, 0]), self.agents("bottom", [0, 3])], state)
        top = self.page.locator('.card[data-widget-id="top"]')
        bottom = self.page.locator('.card[data-widget-id="bottom"]')
        self.assertEqual(top.locator(".agent-row").count(), 2)
        self.assertEqual(bottom.locator(".agent-row").count(), 2)
        summary = top.locator(".agent-summary")
        self.assertIn("Not reported", summary.inner_text())
        self.assertAlmostEqual(summary.bounding_box()["width"], top.bounding_box()["width"] - 20, delta=1)
        top.locator('[data-agent-turn="1"]').click()
        self.assertIn("2 / 5", top.locator(".agent-footer").inner_text())
        self.assertIn("1 / 5", bottom.locator(".agent-footer").inner_text())
        top.locator('[data-preview-filter="attention"]').click()
        self.assertIn("3 agents", top.locator(".agent-footer").inner_text())
        self.assertIn("1 / 2", top.locator(".agent-footer").inner_text())
        self.assertIn("10 agents", bottom.locator(".agent-footer").inner_text())
        self.assertFalse(self.page.evaluate("dirty()"))
        self.assertEqual([w["filter"] for w in self.widgets()], ["all", "all"])
        self.page.evaluate("widgetTypes.agents.view(page().widgets[1]).due = 0")
        self.page.wait_for_function("widgetTypes.agents.view(page().widgets[1]).page === 1")
        self.assertIn("1 / 2", top.locator(".agent-footer").inner_text())
        self.assertIn("2 / 5", bottom.locator(".agent-footer").inner_text())
        for card in (top, bottom):
            card_box, footer = card.bounding_box(), card.locator(".agent-footer").bounding_box()
            self.assertGreaterEqual(footer["y"], card_box["y"])
            self.assertLessEqual(footer["y"] + footer["height"], card_box["y"] + card_box["height"])
        self.assertFalse(self.page.evaluate("dirty()"))
        self.page.locator("#save").click()
        self.page.wait_for_function("!document.querySelector('main').inert")
        self.assertEqual([w.get("filter", "all") for w in self.config()["screen"]["pages"][0]["widgets"]], ["all", "all"])

    def test_full_page_add_rejection_limits_and_manual_binding(self):
        self.load_widgets([self.agents("full", [0, 0], [4, 6])])
        before = self.widgets()
        self.page.locator("#new-type").select_option("metric")
        self.page.locator("#add-widget").click()
        self.assertEqual(self.widgets(), before)
        self.assertIn("没有可用位置", self.page.locator("#status").inner_text())
        self.assertFalse(self.page.evaluate("dirty()"))
        self.page.locator("[data-remove]").click()
        self.page.locator("#new-type").select_option("metric")
        self.page.locator("#add-widget").click()
        self.change('[data-field="host"]', "not-reported")
        self.save("#save")
        self.assertEqual(self.widgets()[0]["host"], "not-reported")
        for _ in range(3):
            self.page.locator("#add-page").click()
        self.assertTrue(self.page.locator("#add-page").is_disabled())
        self.assertEqual(self.page.evaluate("display.pages.length"), 4)
        self.assertEqual(self.page.locator('[data-field="page.layout"]').count(), 0)

    def test_eight_widgets_reflow_has_bounded_runtime_and_keeps_all(self):
        self.load_widgets([self.metric(str(i), [i % 4, i // 4]) for i in range(8)])
        self.assertTrue(self.page.locator("#add-widget").is_disabled())
        before = self.widgets()
        self.begin_drag("0", 348, 0)
        self.page.mouse.up()
        after = self.widgets()
        self.assertEqual(after[0]["at"], [3, 0])
        self.assertEqual(after[3]["at"], [3, 2])
        self.assertEqual(sum(a["at"] != b["at"] for a, b in zip(before, after)), 2)
        self.assertEqual(len(after), 8)
        self.page.evaluate("validateLayout(page())")
        elapsed = self.page.evaluate("""() => {
            const start = performance.now();
            for (let y = 0; y < 6; y++) for (let x = 0; x < 4; x++)
                layoutProposal({...page().widgets[0], at: [x,y], size: [2,2]});
            return performance.now() - start;
        }""")
        self.assertLess(elapsed, 1500)

    def test_compact_values_and_error_labels_stay_inside_cards(self):
        widgets = [self.metric(name, [i % 4, i // 4]) for i, name in enumerate(("full", "error", "missing", "waiting", "warming"))]
        for widget in widgets:
            widget["source"] = widget["id"]
        widgets[3]["host"] = "unknown"
        state = {"hosts": [{"host_id": "workstation", "host_name": "Office", "health": "fresh", "sources": {
            "full": {"kind": "metrics", "status": "ok", "health": "fresh", "metrics": {"usage_pct": {"unit": "percent", "value": 100, "age_ms": 0}}},
            "error": {"kind": "metrics", "status": "error", "health": "fresh"},
            "warming": {"kind": "metrics", "status": "warming_up", "health": "fresh"},
        }}]}
        self.load_widgets(widgets, state)
        expected = ("100%", "Collection error", "Not reported", "Waiting", "Warming up")
        for widget, text in zip(widgets, expected):
            card = self.page.locator(f'.card[data-widget-id="{widget["id"]}"]')
            label = card.locator(".metric-value, .metric-status")
            self.assertEqual(label.inner_text(), text)
            self.assertTrue(label.evaluate("e => e.scrollHeight <= e.clientHeight && e.scrollWidth <= e.clientWidth"))
            box, label_box = card.bounding_box(), label.bounding_box()
            self.assertLessEqual(label_box["x"] + label_box["width"], box["x"] + box["width"])
            self.assertLessEqual(label_box["y"] + label_box["height"], box["y"] + box["height"])
        self.assertEqual(self.page.locator(".screen-head").inner_text().count("100%"), 0)

    def test_screen_properties_undo_and_raw_import_history(self):
        self.load_widgets([self.metric("cpu", [0, 0])])
        self.begin_drag("cpu", 116, 0)
        self.page.mouse.up()
        self.change('[data-field="title"]', "Renamed")
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets()[0]["title"], "cpu")
        self.assertEqual(self.widgets()[0]["at"], [1, 0])
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets()[0]["at"], [0, 0])
        self.assertFalse(self.page.evaluate("dirty()"))
        for tab, selector, value in [("device", "#brightness", "62"),
                                     ("alerts", '[data-alert-field="volume"]', "43")]:
            self.tab(tab)
            self.change(selector, value)
        self.tab("screen")
        self.begin_drag("cpu", 116, 0)
        self.page.mouse.up()
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.page.evaluate("[...dirtySections].sort()"), ["alerts", "device"])
        self.tab("config")
        self.assertEqual(self.config()["device"]["brightness"], 62)
        self.assertEqual(self.config()["alerts"]["volume"], 43)
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["at"], [0, 0])
        self.tab("screen")
        self.change('[data-field="title"]', "Another")
        self.assertFalse(self.page.locator("#undo-layout").is_disabled())
        self.tab("config")
        self.page.locator("#import-yaml").set_input_files({
            "name": "import.yml", "mimeType": "application/yaml", "buffer": FIXTURE.encode(),
        })
        self.page.wait_for_function("dirtySections.has('config') && display.pages[0].widgets.length === 2")
        self.tab("screen")
        self.assertTrue(self.page.locator("#undo-layout").is_disabled())

    def test_clock_add_edit_export_reload_and_layout(self):
        self.load_widgets([])
        self.page.locator("#new-type").select_option("clock")
        self.page.locator("#add-widget").click()
        clock = self.widgets()[0]
        self.assertEqual((clock["title"], clock["size"]), ("", [1, 1]))
        self.assertEqual(self.page.locator('[data-field="title"]').get_attribute("placeholder"), "自动")
        self.assertEqual(self.page.locator('[data-field="host"], [data-metric-select]').count(), 0)
        self.save("#save")
        saved = self.config()["screen"]["pages"][0]["widgets"][0]
        for key in ("host", "source"):
            self.assertNotIn(key, saved)
        self.assertEqual((saved["utc_offset"], saved["seconds"], saved["title"]), ("+00:00", False, ""))
        offset = self.page.locator('[data-field="utc_offset"]')
        offset.fill("+05:45")
        offset.dispatch_event("change")
        self.page.locator('[data-field="seconds"]').check()
        self.begin_drag(clock["id"], 116, 70, resize=True)
        self.page.mouse.up()
        self.begin_drag(clock["id"], 116, 70)
        self.page.mouse.up()
        self.assertEqual(self.widgets()[0]["size"], [2, 2])
        self.assertEqual(self.widgets()[0]["at"], [1, 1])
        self.assertEqual(self.page.locator(".clock-title").inner_text(), "Time (UTC+5:45)")
        title = self.page.locator('[data-field="title"]')
        title.fill("Office")
        title.dispatch_event("change")
        self.assertEqual(self.page.locator(".clock-title").inner_text(), "Office")
        title.fill("")
        title.dispatch_event("change")
        self.assertEqual(self.page.locator(".clock-title").inner_text(), "Time (UTC+5:45)")
        self.assertRegex(self.page.locator(".clock-time").inner_text(), r"^\d{2}:\d{2}:\d{2}$")
        self.assertRegex(self.page.locator(".clock-date").inner_text(), r"^\d{4}-\d{2}-\d{2}$")
        self.tab("device")
        self.change("#ntp-server", "time.example.com")
        self.save("#save-device")
        self.tab("config")
        with self.page.expect_download() as download:
            self.page.locator("#export-yaml").click()
        exported = Path(download.value.path()).read_text()
        self.assertIn("+05:45", exported)
        self.assertIn("seconds: true", exported)
        self.assertIn("ntp_server: time.example.com", exported)
        self.page.locator("#reload-config").click()
        self.page.wait_for_function("!document.querySelector('main').inert")
        self.tab("screen")
        self.assertEqual(self.widgets()[0]["utc_offset"], "+05:45")
        self.assertTrue(self.widgets()[0]["seconds"])
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)

    def test_clock_text_fits_every_grid_size(self):
        failures = self.page.evaluate("""() => {
            const failures = [];
            for (let width = 1; width <= 4; width++)
              for (let height = 1; height <= 6; height++)
                for (const seconds of [false, true])
                  for (const title of ["Clock", ""]) {
                    page().widgets = [{id:"clock", type:"clock", title,
                        at:[0,0], size:[width,height], utc_offset:"+00:00", seconds}];
                    renderPreview();
                    const card = document.querySelector(".clock-card"), box = card.getBoundingClientRect();
                    for (const label of card.querySelectorAll("span")) {
                        const rect = label.getBoundingClientRect();
                        if (rect.x < box.x || rect.y < box.y ||
                            rect.right > box.right + 0.1 || rect.bottom > box.bottom + 0.1 ||
                            label.scrollWidth > label.clientWidth || label.scrollHeight > label.clientHeight)
                            failures.push({width,height,seconds,title,label:label.className});
                    }
                    const time = card.querySelector(".clock-time");
                    if (!(seconds ? /^\\d{2}:\\d{2}:\\d{2}$/ : /^\\d{2}:\\d{2}$/).test(time.textContent))
                        failures.push({width,height,seconds,invalidTime:time.textContent});
                    if (height > 1 && !/^\\d{4}-\\d{2}-\\d{2}$/.test(card.querySelector(".clock-date").textContent))
                        failures.push({width,height,invalidDate:true});
                  }
            return failures;
        }""")
        self.assertEqual(failures, [])

    def test_clock_fixed_offset_preview(self):
        self.context.new_cdp_session(self.page).send("Emulation.setTimezoneOverride", {"timezoneId": "Pacific/Honolulu"})
        self.page.evaluate("() => { Date.now = () => Date.parse('2026-01-01T22:30:45Z'); }")
        self.load_widgets([{"id": "clock", "type": "clock", "at": [0, 0], "size": [1, 2],
                            "utc_offset": "+05:45", "seconds": True}])
        self.assertEqual(self.page.locator(".clock-time").inner_text(), "04:15:45")
        self.assertEqual(self.page.locator(".clock-date").inner_text(), "2026-01-02")
        self.assertTrue(self.page.locator(".clock-date").evaluate("e => e.scrollWidth <= e.clientWidth"))
        self.assertEqual(self.page.locator(".clock-title").inner_text(), "Time")
        for offset, expected in (("+00:00", "Time (UTC)"), ("+08:00", "Time (UTC+8)"),
                                 ("-00:30", "Time (UTC-0:30)"), ("-03:30", "Time (UTC-3:30)")):
            self.assertEqual(self.page.evaluate("offset => widgetTypes.clock.title({title:'', utc_offset:offset}, 224)", offset), expected)
        self.assertEqual(self.page.evaluate("widgetTypes.clock.parts({utc_offset:'-12:00',seconds:false}, Date.parse('2026-01-01T01:00:00Z'))"),
                         {"time": "13:00", "date": "2025-12-31"})

    def test_machine_minimum_card_fits_both_values(self):
        machine = {"id": "machine", "type": "machine", "title": "Workstation", "host": "workstation", "at": [0, 0], "size": [2, 2]}
        state = {"hosts": [{"host_id": "workstation", "health": "fresh", "sources": {
            source: {"kind": "metrics", "status": "ok", "health": "fresh", "metrics": {
                "usage_pct": {"unit": "percent", "value": value, "age_ms": 0}}}
            for source, value in (("system.cpu", 25), ("system.memory", 61))}}]}
        self.load_widgets([machine], state)
        card = self.page.locator(".machine-card")
        self.assertEqual(card.locator(".metric-value").all_inner_texts(), ["25%", "61%"])
        box = card.bounding_box()
        value = card.locator(".metric-value").last.bounding_box()
        self.assertLessEqual(value["y"] + value["height"], box["y"] + box["height"])
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)

    def test_undo_after_yaml_projection(self):
        self.load_widgets([self.metric("cpu", [0, 0])])
        self.begin_drag("cpu", 116, 0)
        self.page.mouse.up()
        self.tab("config")
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["at"], [1, 0])
        self.tab("screen")
        self.page.locator("#undo-layout").click()
        self.tab("config")
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["at"], [0, 0])

    def test_city_selection_manual_coordinates_and_undo(self):
        self.load_widgets([{"type": "weather", "city": "北京", "latitude": 39.9,
                            "longitude": 116.4, "at": [0, 0], "size": [1, 1]}])
        requests = []

        def geocode(route):
            requests.append(route.request.url)
            route.fulfill(json={"results": [
                {"name": "Paris", "admin1": "Île-de-France", "country": "France",
                 "latitude": 48.85, "longitude": 2.35},
                {"name": "Paris", "admin1": "Texas", "country": "United States",
                 "latitude": 33.66, "longitude": -95.55},
            ]})

        self.page.route("https://geocoding-api.open-meteo.com/**", geocode)
        self.assertEqual(self.page.locator(".weather-title").inner_text(), "北京")
        self.page.locator("[data-weather-query]").fill("Paris")
        self.assertEqual(self.widgets()[0]["city"], "北京")
        self.page.locator("[data-weather-search]").click()
        second = self.page.locator('[data-weather-result="1"]')
        second.wait_for()
        self.assertIn("Texas", second.inner_text())
        self.assertEqual(self.widgets()[0]["latitude"], 39.9)
        second.click()
        selected = self.widgets()[0]
        self.assertEqual([selected[k] for k in ("city", "latitude", "longitude")],
                         ["Paris", 33.66, -95.55])
        self.assertEqual(len(requests), 1)
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets()[0]["city"], "北京")
        self.page.locator(".manual-fields summary").click()
        coordinate = self.page.locator('[data-weather-coordinate="latitude"]')
        coordinate.fill("-12.5")
        coordinate.dispatch_event("change")
        self.assertEqual(self.widgets()[0]["latitude"], -12.5)
        self.save("#save")
        saved = self.config()["screen"]["pages"][0]["widgets"][0]
        self.assertEqual(saved["latitude"], -12.5)
        self.assertEqual(len(requests), 1)


if __name__ == "__main__":
    unittest.main()
