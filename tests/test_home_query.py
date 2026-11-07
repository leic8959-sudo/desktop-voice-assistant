import asyncio
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from home_control import HomeController


def device(device_id, name, area, entity_id, enabled=True, actions=None):
    return {
        "device_id": device_id,
        "name": name,
        "alias": None,
        "area": area,
        "enabled": enabled,
        "state": {"entity_id": entity_id},
        "actions": actions or [],
    }


class HomeQueryTests(unittest.TestCase):
    def setUp(self):
        self.runtime = tempfile.TemporaryDirectory()
        self.addCleanup(self.runtime.cleanup)
        self.m40 = device("m40-id", "M40", "卧室", "vacuum.m40")
        self.l10 = device("l10-id", "L10", "客厅", "vacuum.l10", enabled=False)
        self.light = device("light-id", "卧室灯", "卧室", "light.bedroom")

    def test_vacuum_synonyms_match_actual_vacuum(self):
        for query in ("扫地机器人", "扫拖机器人", "扫托机器人", "机器人", "vacuum", "RoBoT"):
            with self.subTest(query=query):
                self.assertTrue(HomeController._matches_device_query(self.m40, query))

    def test_case_insensitive_model_query_matches_m40(self):
        self.assertTrue(HomeController._matches_device_query(self.m40, "m40"))
        self.assertTrue(HomeController._matches_device_query(self.m40, "M40"))

    def test_room_or_model_qualifier_is_preserved(self):
        self.assertTrue(HomeController._matches_device_query(self.m40, "卧室扫地机器人"))
        self.assertTrue(HomeController._matches_device_query(self.m40, "m40扫拖机器人"))
        self.assertFalse(HomeController._matches_device_query(self.l10, "卧室扫地机器人"))
        self.assertFalse(HomeController._matches_device_query(self.m40, "L10扫托机器人"))

    def test_non_vacuum_does_not_match_vacuum_category(self):
        for query in ("扫地机器人", "扫拖机器人", "扫托机器人", "机器人"):
            with self.subTest(query=query):
                self.assertFalse(HomeController._matches_device_query(self.light, query))

    def test_multiple_matches_are_returned_without_auto_selection(self):
        catalog = {"devices": [self.m40, {**self.l10, "enabled": True}], "refreshed_at": 0}
        controller = HomeController({"runtime_dir": self.runtime.name}, None)

        async def discover(*, force=False):
            return catalog

        controller._discover = discover
        result = asyncio.run(controller.list_home_devices(query="机器人"))
        self.assertEqual({item["device_id"] for item in result["devices"]}, {"m40-id", "l10-id"})

    def test_authorization_filter_remains_in_list_path(self):
        catalog = {"devices": [self.m40, self.l10, self.light], "refreshed_at": 0}
        controller = HomeController({"runtime_dir": self.runtime.name}, None)

        async def discover(*, force=False):
            return catalog

        controller._discover = discover
        enabled = asyncio.run(controller.list_home_devices(query="机器人"))
        self.assertEqual([item["device_id"] for item in enabled["devices"]], ["m40-id"])
        all_devices = asyncio.run(controller.list_home_devices(query="机器人", include_disabled=True))
        self.assertEqual({item["device_id"] for item in all_devices["devices"]}, {"m40-id", "l10-id"})


if __name__ == "__main__":
    unittest.main()
