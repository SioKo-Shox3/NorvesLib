# 合成地形と六つのマーカーで配置検証と報告の契約を確かめる。
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import validate_level


def _write_json(path, document):
    path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")


def _object(class_name, properties):
    return {
        "class": class_name, "classId": "0", "path": "",
        "properties": [
            {"name": name, "propertyId": "0", "type": type_name,
             "typeId": "0", "value": value}
            for name, (type_name, value) in properties.items()
        ],
    }


def _entity(position, components, alias="1", parent_alias="0", children=None):
    return {
        "alias": alias, "parentAlias": parent_alias,
        "object": _object("Entity", {
            "Position": ("Vector3", f"Vector3({position[0]},{position[1]},{position[2]})"),
        }),
        "components": [
            {"alias": str(100 + index), "ownerAlias": alias,
             "object": _object(class_name, properties)}
            for index, (class_name, properties) in enumerate(components.items())
        ],
        "children": children if children is not None else [],
    }


def _marker(key, position, components=None, tag=""):
    return _entity(position, {
        "LevelMarkerComponent": {"Key": ("String", key), "Tag": ("String", tag)},
        **(components or {}),
    })


def _base_config():
    return {
        "owner_tag": "shisha",
        "traversal": {"max_slope_deg": 30},
        "water": {"mode": "none"},
        "territory": {"ground_tolerance_m": 0.5, "max_slope_deg": 30,
                      "max_water_fraction": 0},
        "reachability": {"start_xz": [10, 10]},
        "travel": {"cruise_speed_mps": 7, "slope_cost": 2},
        "poi_travel_time_band_s": {"min": 0, "max": 100},
        "trail": {"sample_step_m": 0.5},
        "provisional": [],
    }


def _base_scene(config):
    nodes = [
        _marker("owner.one", (50, 10, 40), tag=config["owner_tag"]),
        _marker("territory.one", (60, 10, 40), {
            "TerritoryMarkerComponent": {"OwnerKey": ("String", "owner.one")},
            "EncounterZoneComponent": {"Shape": ("EEncounterZoneShape", "0"),
                                       "Radius": ("float", "8")},
        }),
        _marker("rest.one", (20, 10, 20), {
            "RestPointComponent": {"bFastTravel": ("bool", "1")},
        }),
        _marker("poi.a", (30, 10, 100), {"PoiMarkerComponent": {}}),
        _marker("poi.b", (73, 10, 117), {"PoiMarkerComponent": {}}),
        _marker("trail.one", (35, 10, 40), {
            "ScentTrailComponent": {"TargetKey": ("String", "territory.one")},
        }),
    ]
    # 点は親の位置からの相対座標で保存し、末尾を持ち場の中心に重ねる。
    nodes[-1]["children"] = [
        _entity((offset, 0, 0), {"ScentTrailPointComponent": {}},
                alias=str(index + 2), parent_alias="1")
        for index, offset in enumerate((0, 12, 25))
    ]
    return {
        "formatVersion": 3, "module": "NorvesLib", "assets": [],
        "roots": [{"formatVersion": 3, "rootAlias": "1", "rootPath": "", "root": node}
                  for node in nodes],
    }


def _node(scene, index):
    return scene["roots"][index]["root"]


def _property(node, class_name, name):
    record = node["object"] if class_name == "Entity" else next(
        component["object"] for component in node["components"]
        if component["object"]["class"] == class_name
    )
    return next(prop for prop in record["properties"] if prop["name"] == name)


class ValidateLevelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.terrain_directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.terrain_directory.cleanup)
        directory = Path(cls.terrain_directory.name)
        z, x = np.indices((257, 257), dtype=np.float64)
        # 半径25 m・高さ25 mの円錐は、裾と頂点以外の斜度が約45度になる。
        heights = 10 + np.maximum(0, 25 - np.hypot(x - 200, z - 60))
        outer = (180 <= x) & (x <= 240) & (180 <= z) & (z <= 240)
        inner = (183 <= x) & (x <= 237) & (183 <= z) & (z <= 237)
        # 高さ40 m・幅3標本の壁で、高さ10 mの平坦な区画を完全に囲む。
        heights[outer & ~inner] = 40
        samples = np.round(heights * 65535 / 64).astype("<u2")
        (directory / "height.raw").write_bytes(samples.tobytes())
        cls.heights = samples.astype(np.float64) * (64 / 65535)
        cls.terrain_path = directory / "terrain.json"
        _write_json(cls.terrain_path, {
            "height_source": {"path": "height.raw", "format": "raw16",
                              "width": 257, "height": 257, "endian": "little"},
            "height_mapping": {"min_m": 0, "max_m": 64},
            "cell_size_m": 1.0, "tile_cells": 128,
        })
        cls.base_config = _base_config()
        cls.base_scene = _base_scene(cls.base_config)

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.scene_path = self.directory / "scene.json"
        self.config_path = self.directory / "config.json"
        self.scene = deepcopy(self.base_scene)
        self.config = deepcopy(self.base_config)

    def _validate(self):
        _write_json(self.scene_path, self.scene)
        _write_json(self.config_path, self.config)
        return validate_level.validate(self.scene_path, self.terrain_path, self.config_path)

    def _main(self):
        return validate_level.main([
            "--scene", str(self.scene_path), "--terrain", str(self.terrain_path),
            "--config", str(self.config_path), "--out", str(self.directory / "cli"),
        ])

    def _find_issue(self, report, code, key, field="issues"):
        matches = [item for item in report[field] if item["code"] == code and item["key"] == key]
        self.assertTrue(matches, f"{field} に {code} / {key} がありません: {report[field]}")
        return matches[0]

    def _poi_pair(self, report):
        pair = next(item for item in report["poi_travel"]
                    if item["from"] == "poi.a" and item["to"] == "poi.b")
        self.assertTrue(pair["reachable"])
        return pair

    def test_complete_scene_passes(self):
        report = self._validate()
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["issues"], [])
        self.assertEqual(self._main(), 0)

    def test_detects_owner_without_territory(self):
        territory = _node(self.scene, 1)
        component = next(item for item in territory["components"]
                         if item["object"]["class"] == "TerritoryMarkerComponent")
        component["object"]["properties"] = []
        self._find_issue(self._validate(), "owner_without_territory", "owner.one")

    def test_detects_orphan_territory(self):
        _property(_node(self.scene, 1), "TerritoryMarkerComponent", "OwnerKey")["value"] = "owner.missing"
        self._find_issue(self._validate(), "territory_orphan", "territory.one")
        self.assertEqual(self._main(), 1)

    def test_detects_duplicate_key(self):
        _property(_node(self.scene, 4), "LevelMarkerComponent", "Key")["value"] = "poi.a"
        issue = self._find_issue(self._validate(), "key_duplicate", "poi.a")
        self.assertEqual(issue["details"]["count"], 2)

    def test_detects_unreachable_rest_point(self):
        _property(_node(self.scene, 2), "Entity", "Position")["value"] = "Vector3(210,10,210)"
        self._find_issue(self._validate(), "rest_point_unreachable", "rest.one")

    def test_detects_territory_slope_exceeded(self):
        height = self.heights[60, 210]
        _property(_node(self.scene, 1), "Entity", "Position")["value"] = f"Vector3(210,{height},60)"
        self._find_issue(self._validate(), "territory_slope_exceeded", "territory.one")

    def test_detects_poi_interval_out_of_band(self):
        time = self._poi_pair(self._validate())["time_s"]
        self.config["poi_travel_time_band_s"] = {"min": time * 2, "max": time * 3}
        report = self._validate()
        self._find_issue(report, "poi_interval_out_of_band", "poi.a", field="warnings")
        self.assertEqual(report["status"], "pass")

    def test_flat_travel_time_equals_length_over_speed(self):
        pair = self._poi_pair(self._validate())
        # 距離と時間の両方に入る小数点以下6桁の丸めを許容する。
        self.assertAlmostEqual(pair["time_s"],
                               pair["path_length_m"] / self.config["travel"]["cruise_speed_mps"],
                               delta=1e-6)
        self.assertEqual(pair["cost_m"], pair["path_length_m"])

    def test_report_json_is_deterministic(self):
        self.config["provisional"] = ["travel.cruise_speed_mps"]
        first_json, first_md = validate_level.write_reports(self._validate(), self.directory / "first")
        second_json, second_md = validate_level.write_reports(self._validate(), self.directory / "second")
        self.assertEqual(first_json.name, "level_report.json")
        self.assertEqual(first_json.read_bytes(), second_json.read_bytes())
        for markdown in (first_md, second_md):
            self.assertEqual(markdown.name, "level_report.md")
            self.assertIn("未採用の仮置き", markdown.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
