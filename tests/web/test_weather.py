"""Weather editor behavior using the shared browser fixture."""
import unittest
import test_config


class WeatherBrowserTest(unittest.TestCase):
    setUpClass = classmethod(test_config.ConfigBrowserTest.setUpClass.__func__)
    tearDownClass = classmethod(test_config.ConfigBrowserTest.tearDownClass.__func__)
    setUp = test_config.ConfigBrowserTest.setUp
    tearDown = test_config.ConfigBrowserTest.tearDown
    api = test_config.ConfigBrowserTest.api
    tab = test_config.ConfigBrowserTest.tab
    config = test_config.ConfigBrowserTest.config
    load_widgets = test_config.ConfigBrowserTest.load_widgets
    widgets = test_config.ConfigBrowserTest.widgets

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
        self.page.locator("#save").click()
        self.page.wait_for_function("!displayDirty")
        saved = self.config()["screen"]["pages"][0]["widgets"][0]
        self.assertEqual(saved["latitude"], -12.5)
        self.assertEqual(len(requests), 1)


if __name__ == "__main__":
    unittest.main()
