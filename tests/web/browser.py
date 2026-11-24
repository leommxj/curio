import functools
import http.server
from pathlib import Path
import subprocess
import sys
import threading
import unittest

from playwright.sync_api import sync_playwright

WEB = Path(__file__).resolve().parents[2] / "firmware/main/web"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/app.js", "/style.css"):
            asset = WEB.parents[2] / "build/modules" / self.path[1:]
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript" if self.path.endswith(".js") else "text/css")
            self.end_headers()
            self.wfile.write(asset.read_bytes())
            return
        super().do_GET()

    def log_message(self, *_):
        pass


class BrowserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, "scripts/modules.py"], cwd=WEB.parents[2], check=True)
        cls.server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(WEB)))
        cls.addClassCleanup(cls.server.server_close)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.addClassCleanup(cls.server.shutdown)
        cls.playwright = sync_playwright().start()
        cls.addClassCleanup(cls.playwright.stop)
        cls.browser = cls.playwright.chromium.launch(headless=True)
        cls.addClassCleanup(cls.browser.close)

    def setUp(self):
        self.context = self.browser.new_context(accept_downloads=True)
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on("pageerror", lambda error: self.errors.append(str(error)))

    def tearDown(self):
        self.assertEqual(self.errors, [])
