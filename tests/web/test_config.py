"""Run with a Python environment containing Playwright and Chromium."""
import functools
import http.server
import json
from pathlib import Path
import threading
import unittest
from playwright.sync_api import sync_playwright

WEB = Path(__file__).resolve().parents[2] / "firmware/main/web"
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
  ssid: Studio WiFi
  password: password123
  hostname: curio-studio
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


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


class ConfigBrowserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(WEB)))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.playwright = sync_playwright().start()
        cls.browser = cls.playwright.chromium.launch(headless=True)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.playwright.stop()
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        self.context = self.browser.new_context(accept_downloads=True)
        self.page = self.context.new_page()
        self.source, self.revision = FIXTURE, 1
        self.admin = "admin-token-123456789"
        self.state = {"hosts": []}
        self.requests, self.errors = [], []
        self.conflict = False
        self.regenerate_tls = False
        self.page.on("pageerror", lambda error: self.errors.append(str(error)))
        self.page.on("dialog", lambda dialog: dialog.accept())
        self.page.route("**/api/v2/**", self.api)
        self.page.goto(f"http://127.0.0.1:{self.server.server_port}/")
        self.page.locator("#token").fill(self.admin)
        self.page.locator("#login button").click()
        self.page.locator("main").wait_for(state="visible")

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.context.close()

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

    def config(self):
        return self.page.evaluate("jsyaml.load(yamlText)")

    def test_forms_yaml_credentials_and_conflict(self):
        self.tab("device")
        self.page.locator("#brightness").fill("61")
        self.page.locator("#brightness").dispatch_event("change")
        self.page.locator("#save-device").click()
        self.page.wait_for_function("!deviceDirty")
        document = self.config()
        self.assertEqual(document["device"]["brightness"], 61)
        self.assertEqual(document["network"]["ssid"], "Studio WiFi")
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
        self.assertEqual(self.page.locator("#network-ssid").input_value(), "Studio WiFi")
        self.assertEqual(self.page.locator("#network-hostname").input_value(), "curio-studio")
        with self.page.expect_download() as info:
            self.page.locator("#download-certificate").click()
        self.assertEqual(info.value.suggested_filename, "curio-studio.crt")
        self.assertIn("initial-cert", Path(info.value.path()).read_text())
        self.page.locator("#network-ssid").fill("New Studio")
        self.page.locator("#network-ssid").dispatch_event("change")
        self.page.locator("#network-hostname").fill("curio-renamed")
        self.page.locator("#network-hostname").dispatch_event("change")
        self.assertTrue(self.page.locator("#download-certificate").is_disabled())
        self.regenerate_tls = True
        self.page.locator("#save-device").click()
        self.page.wait_for_function("!deviceDirty")
        submitted = [body for path, method, _, body in self.requests if path == "config" and method == "PUT"][-1]
        self.assertNotIn("tls:", submitted)
        self.assertEqual(self.config()["network"]["hostname"], "curio-renamed")
        self.assertIn("replacement-cert", self.config()["tls"]["certificate"])
        self.assertFalse(any(path == "restart" for path, *_ in self.requests))
        with self.page.expect_download() as info:
            self.page.locator("#download-certificate").click()
        self.assertEqual(info.value.suggested_filename, "curio-renamed.crt")
        self.assertIn("replacement-cert", Path(info.value.path()).read_text())
        self.page.locator("#restart-device").click()
        self.page.wait_for_function("document.querySelector('#status').textContent.includes('设备正在重启')")
        restarts = [(method, headers) for path, method, headers, _ in self.requests if path == "restart"]
        self.assertEqual(len(restarts), 1)
        self.assertEqual(restarts[0][0], "POST")
        self.assertEqual(restarts[0][1]["authorization"], "Bearer " + self.admin)

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
        self.page.locator("#save").click()
        self.page.wait_for_function("!displayDirty")
        binding = self.config()["screen"]["pages"][0]["widgets"][1]
        self.assertEqual((binding["host"], binding["source"], binding["key"]),
                         ("workstation", "system.cpu", "usage_pct"))
        self.assertEqual(self.config()["network"]["hostname"], "curio-studio")
        self.assertIn("initial-cert", self.config()["tls"]["certificate"])
        self.assertFalse(any(path == "catalog" for path, *_ in self.requests))

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
        self.page.locator("#brightness").fill("24")
        self.page.locator("#brightness").dispatch_event("change")
        self.tab("alerts")
        self.page.locator('[data-alert-field="volume"]').fill("42")
        self.page.locator('[data-alert-field="volume"]').dispatch_event("change")
        self.tab("config")
        self.assertEqual(self.config()["device"]["brightness"], 24)
        self.assertEqual(self.config()["alerts"]["volume"], 42)
        self.page.locator("#save-config").click()
        self.page.wait_for_function("!yamlDirty && !deviceDirty && !alertsDirty")
        self.tab("device")
        self.assertEqual(self.page.locator("#brightness").input_value(), "24")

    def test_vibration_controls_yaml_and_type_changes(self):
        self.tab("alerts")
        strength = self.page.locator('[data-alert-field="vibration_strength"]')
        self.assertEqual(strength.input_value(), "60")
        self.assertFalse(self.page.locator('[data-alert-field="vibrate"]').first.is_checked())
        strength.fill("0")
        strength.dispatch_event("change")
        self.page.locator('[data-alert-field="vibrate"]').first.check()
        self.assertFalse(self.page.locator('[data-alert-field="sound"]').first.is_checked())
        self.page.locator('.alert-rule details').first.locator('summary').click()
        self.page.locator('[data-alert-field="type"]').first.select_option("host_offline")
        self.assertTrue(self.page.locator('[data-alert-field="vibrate"]').first.is_checked())
        self.page.locator('[data-alert-field="type"]').first.select_option("agent_done")
        self.assertTrue(self.page.locator('[data-alert-field="vibrate"]').first.is_checked())
        self.page.locator("#save-alerts").click()
        self.page.wait_for_function("!alertsDirty")
        document = self.config()
        self.assertEqual(document["alerts"]["vibration_strength"], 0)
        self.assertTrue(document["alerts"]["rules"][0]["vibrate"])
        self.assertFalse(document["alerts"]["rules"][0]["sound"])
        self.assertNotIn("vibrate", document["alerts"]["rules"][1])
        self.tab("config")
        source = self.page.locator("#yaml-editor").input_value().replace("vibration_strength: 0", "vibration_strength: 83")
        self.page.locator("#yaml-editor").fill(source)
        self.tab("alerts")
        self.assertEqual(self.page.locator('[data-alert-field="vibration_strength"]').input_value(), "83")
        self.assertTrue(self.page.locator('[data-alert-field="vibrate"]').first.is_checked())
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)

    def test_flash_controls_and_yaml_types(self):
        self.tab("alerts")
        self.assertFalse(self.page.locator('[data-alert-field="flash_enabled"]').is_checked())
        self.assertFalse(self.page.locator('[data-alert-field="flash"]').first.is_checked())
        self.page.locator('[data-alert-field="flash_enabled"]').check()
        self.page.locator('[data-alert-field="flash"]').first.check()
        self.page.locator('.alert-rule details').first.locator('summary').click()
        for kind in ("host_offline", "agent_done"):
            self.page.locator('[data-alert-field="type"]').first.select_option(kind)
            self.assertTrue(self.page.locator('[data-alert-field="flash"]').first.is_checked())
        self.page.locator("#save-alerts").click()
        self.page.wait_for_function("!alertsDirty")
        document = self.config()
        self.assertTrue(document["alerts"]["flash_enabled"])
        self.assertTrue(document["alerts"]["rules"][0]["flash"])
        self.assertNotIn("flash", document["alerts"]["rules"][1])
        self.page.set_viewport_size({"width": 375, "height": 812})
        self.assertLessEqual(self.page.evaluate("document.documentElement.scrollWidth"), 375)
        self.tab("config")
        for field in ("flash_enabled", "flash"):
            for value in ("1", "'true'", "null"):
                source = FIXTURE.replace("volume: 35", "volume: 35\n  flash_enabled: " + value) if field == "flash_enabled" else FIXTURE.replace("- type: agent_done", "- type: agent_done\n      flash: " + value)
                self.page.locator("#yaml-editor").fill(source)
                self.tab("alerts")
                self.assertIn(field, self.page.locator("#status").inner_text())

    def test_invalid_scalar_types_preserve_raw_draft(self):
        self.tab("config")
        cases = [
            ("auto_rotate: true", "auto_rotate: 'false'", "auto_rotate"),
            ("rotation: 90", "rotation: 45", "rotation"),
            ("- type: agent_done", "- type: agent_done\n      sound: 'false'", "sound"),
            ("- type: agent_done", "- type: agent_done\n      include_idle: null", "include_idle"),
            ("version: 1", "version: 2", "version"),
        ]
        for old, new, field in cases:
            draft = FIXTURE.replace(old, new)
            self.page.locator("#yaml-editor").fill(draft)
            self.tab("device")
            self.assertTrue(self.page.locator("#yaml-editor").is_visible())
            self.assertIn(field, self.page.locator("#status").inner_text())
            self.assertEqual(self.page.locator("#yaml-editor").input_value(), draft)

    def test_vibration_yaml_types(self):
        self.tab("config")
        for value in ("101", "-1", "1.5", "'60'", "true"):
            self.page.locator("#yaml-editor").fill(FIXTURE.replace("volume: 35", "volume: 35\n  vibration_strength: " + value))
            self.tab("alerts")
            self.assertIn("alerts.vibration_strength", self.page.locator("#status").inner_text())
        invalid = FIXTURE.replace("- type: agent_done", "- type: agent_done\n      vibrate: 'true'")
        self.page.locator("#yaml-editor").fill(invalid)
        self.tab("alerts")
        self.assertIn("vibrate", self.page.locator("#status").inner_text())

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
        self.page.locator("#save-config").click()
        self.page.wait_for_function("!yamlDirty")
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
        self.assertTrue(self.page.evaluate("displayDirty"))
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets(), before)
        self.assertFalse(self.page.evaluate("displayDirty"))
        self.begin_drag("cpu", 116, 0)
        self.page.mouse.up()
        self.page.locator("#save").click()
        self.page.wait_for_function("!displayDirty")
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
        binding = {"host_id": "h", "source_id": "s", "key": "x"}
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
        self.page.locator("#save").click()
        self.page.wait_for_function("!displayDirty")
        widget = self.config()["screen"]["pages"][0]["widgets"][0]
        self.assertEqual((widget["type"], widget["host"], widget["source"]), ("status", "workstation", "service.http"))
        self.tab("config")
        with self.page.expect_download() as download:
            self.page.locator("#export-yaml").click()
        exported = Path(download.value.path()).read_text()
        self.page.locator("#import-yaml").set_input_files({
            "name": "status.yaml", "mimeType": "application/yaml", "buffer": exported.encode(),
        })
        self.page.wait_for_function("yamlDirty")
        self.tab("screen")
        self.assertEqual(self.widgets()[0]["source_id"], "service.http")
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
        self.page.evaluate("agentPreview(page().widgets[1]).due = 0")
        self.page.wait_for_function("agentPreview(page().widgets[1]).page === 1")
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
        self.page.locator("#add-widget").click()
        self.page.locator('[data-field="binding.host_id"]').fill("not-reported")
        self.page.locator('[data-field="binding.host_id"]').dispatch_event("change")
        self.page.locator("#save").click()
        self.page.wait_for_function("!displayDirty")
        self.assertEqual(self.widgets()[0]["binding"]["host_id"], "not-reported")
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
        self.page.locator('[data-field="title"]').fill("Renamed")
        self.page.locator('[data-field="title"]').dispatch_event("change")
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets()[0]["title"], "cpu")
        self.assertEqual(self.widgets()[0]["at"], [1, 0])
        self.page.locator("#undo-layout").click()
        self.assertEqual(self.widgets()[0]["at"], [0, 0])
        self.assertFalse(self.page.evaluate("dirty()"))
        self.page.locator('[data-field="title"]').fill("Another")
        self.page.locator('[data-field="title"]').dispatch_event("change")
        self.assertFalse(self.page.locator("#undo-layout").is_disabled())
        self.tab("config")
        self.page.locator("#import-yaml").set_input_files({
            "name": "import.yml", "mimeType": "application/yaml", "buffer": FIXTURE.encode(),
        })
        self.page.wait_for_function("yamlDirty && display.pages[0].widgets.length === 2")
        self.tab("screen")
        self.assertTrue(self.page.locator("#undo-layout").is_disabled())

    def test_clock_add_edit_export_reload_and_layout(self):
        self.load_widgets([])
        self.page.locator("#new-type").select_option("clock")
        self.page.locator("#add-widget").click()
        clock = self.widgets()[0]
        self.assertEqual((clock["title"], clock["size"]), ("", [1, 1]))
        self.assertEqual(self.page.locator('[data-field="title"]').get_attribute("placeholder"), "自动")
        self.assertEqual(self.page.locator('[data-field="host_id"], [data-metric-select]').count(), 0)
        self.page.locator("#save").click()
        self.page.wait_for_function("!displayDirty")
        saved = self.config()["screen"]["pages"][0]["widgets"][0]
        for key in ("host", "source", "utc_offset", "seconds", "title"):
            self.assertNotIn(key, saved)
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
        self.page.locator("#ntp-server").fill("time.example.com")
        self.page.locator("#ntp-server").dispatch_event("change")
        self.page.locator("#save-device").click()
        self.page.wait_for_function("!deviceDirty")
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

    def test_clock_fixed_offset_preview_and_validation(self):
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
            self.assertEqual(self.page.evaluate("offset => clockTitle({title:'', utc_offset:offset}, 224)", offset), expected)
        self.assertEqual(self.page.evaluate("clockParts({utc_offset:'-12:00',seconds:false}, Date.parse('2026-01-01T01:00:00Z'))"),
                         {"time": "13:00", "date": "2025-12-31"})
        self.tab("config")
        original = self.page.locator("#yaml-editor").input_value()
        for value in ("+14:01", "-12:01", "+5:45", "+00:60", "UTC"):
            draft = original.replace("+05:45", value)
            self.page.locator("#yaml-editor").fill(draft)
            self.tab("screen")
            self.assertTrue(self.page.locator("#yaml-editor").is_visible())
            self.assertIn("UTC 偏移", self.page.locator("#status").inner_text())
        for value in ("https://time.example.com", "time.example.com/path", ""):
            draft = original.replace("brightness: 75", "brightness: 75\n  ntp_server: '" + value + "'")
            self.page.locator("#yaml-editor").fill(draft)
            self.tab("device")
            self.assertIn("NTP", self.page.locator("#status").inner_text())

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

    def test_invalid_layout_ids_preserve_raw_draft(self):
        self.tab("config")
        cases = [
            ("c.screen.pages[0].id = ''", "id"),
            ("c.screen.pages[0].id = 'bad id'", "id"),
            ("c.screen.pages[0].id = 'x'.repeat(33)", "id"),
            ("c.screen.pages.push(structuredClone(c.screen.pages[0]))", "重复"),
            ("c.screen.default_page = 'missing'", "default_page"),
            ("c.screen.pages[0].widgets[0].id = 'metric-2'", "重复"),
            ("c.screen.pages[0].widgets[0].id = 1", "id"),
        ]
        for mutation, expected in cases:
            with self.subTest(mutation=mutation):
                draft = self.page.evaluate("""args => {
                    const c = jsyaml.load(args.source);
                    eval(args.mutation);
                    return jsyaml.dump(c);
                }""", {"source": FIXTURE, "mutation": mutation})
                self.page.locator("#yaml-editor").fill(draft)
                self.tab("screen")
                self.assertTrue(self.page.locator("#yaml-editor").is_visible())
                self.assertIn(expected, self.page.locator("#status").inner_text())
                self.assertEqual(self.page.locator("#yaml-editor").input_value(), draft)

    def test_undo_after_yaml_projection_and_invalid_geometry(self):
        self.load_widgets([self.metric("cpu", [0, 0])])
        self.begin_drag("cpu", 116, 0)
        self.page.mouse.up()
        self.tab("config")
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["at"], [1, 0])
        self.tab("screen")
        self.page.locator("#undo-layout").click()
        self.tab("config")
        self.assertEqual(self.config()["screen"]["pages"][0]["widgets"][0]["at"], [0, 0])
        original = self.page.locator("#yaml-editor").input_value()
        for field in ("slot", "layout", "missing_size"):
            source = self.page.evaluate("""args => {
                const c = jsyaml.load(args.source), p = c.screen.pages[0];
                if (args.field === 'layout') p.layout = 'focus';
                else p.widgets[0][args.field] = 0;
                return jsyaml.dump(c);
            }""", {"source": original, "field": field})
            self.page.locator("#yaml-editor").fill(source)
            self.tab("screen")
            self.assertTrue(self.page.locator("#yaml-editor").is_visible())
            self.assertIn("未知字段", self.page.locator("#status").inner_text())
        cases = [
            [self.metric("outside", [4, 0])],
            [self.agents("small", [0, 0], [2, 3])],
            [self.metric("a", [0, 0]), self.metric("b", [0, 0])],
            [self.metric(str(i), [i % 4, i // 4]) for i in range(9)],
        ]
        for widgets in cases:
            source = self.page.evaluate("""args => { const c = jsyaml.load(args.source); c.screen.pages[0].widgets = args.widgets; return jsyaml.dump(c); }""", {"source": original, "widgets": widgets})
            self.page.locator("#yaml-editor").fill(source)
            self.tab("screen")
            self.assertTrue(self.page.locator("#yaml-editor").is_visible())
            self.assertTrue(self.page.locator("#status").inner_text())


if __name__ == "__main__":
    unittest.main()
