"""地図の位置、継ぎ目、決定性、平面の陰影を検証する。"""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compose_map import compose, compute_hillshade


class ComposeMapTests(unittest.TestCase):
    width, height = 401, 193
    buffer_width, buffer_height, margin = 160, 120, 8
    ground_color = (95, 115, 75)

    def _fixture(self, root, rectangles=()):
        """C++ の配置規則で、縁付きの 3 列 × 2 行の入力を作る。"""
        root.mkdir(parents=True, exist_ok=True)
        image = np.full((self.height, self.width, 3), self.ground_color, dtype=np.uint8)
        # 試験側の式は既定方向を明記し、合成側の座標変換関数を使わない。
        world_x = self.width - (np.arange(self.width)[None, :] + 0.5)
        world_z = self.height - (np.arange(self.height)[:, None] + 0.5)
        for rect, color in rectangles:
            min_x, min_z, max_x, max_z = rect
            mask = (world_x >= min_x) & (world_x < max_x) & (world_z >= min_z) & (world_z < max_z)
            image[mask] = color
        transform = {"format": "norves.map_transform", "version": 1,
                     "world_rect": {"min_x": 0.0, "min_z": 0.0, "max_x": float(self.width), "max_z": float(self.height)},
                     "pixel_size_m": 1.0, "image_width": self.width, "image_height": self.height,
                     "up_axis": "+z", "right_axis": "-x"}
        inner_width = self.buffer_width - 2 * self.margin
        inner_height = self.buffer_height - 2 * self.margin
        columns, rows = (self.width - 1) // inner_width + 1, (self.height - 1) // inner_height + 1
        self.assertEqual((columns, rows), (3, 2))
        tiles = []
        for row in range(rows):
            for column in range(columns):
                x, y = column * inner_width, row * inner_height
                width, height = min(inner_width, self.width - x), min(inner_height, self.height - y)
                tile = np.full((self.buffer_height, self.buffer_width, 3), (255, 0, 255), dtype=np.uint8)
                tile[self.margin:self.margin + height, self.margin:self.margin + width] = image[y:y + height, x:x + width]
                name = f"tile_c{column:02d}_r{row:02d}.png"
                self.assertTrue(cv2.imwrite(str(root / name), tile[..., ::-1], [cv2.IMWRITE_PNG_COMPRESSION, 9]))
                tiles.append({"column": column, "row": row, "file": name,
                              "map_pixel": {"x": x, "y": y},
                              "crop": {"x": self.margin, "y": self.margin, "width": width, "height": height},
                              "world_rect": {"min_x": self.width - x - width, "min_z": self.height - y - height,
                                             "max_x": self.width - x, "max_z": self.height - y},
                              "camera_position": [self.width - x - inner_width * 0.5, 100.0, self.height - y - inner_height * 0.5]})
        manifest = {"format": "norves.map_capture_manifest", "version": 1,
                    "settings": {"pixel_size_m": 1.0, "tile_margin_px": self.margin,
                                 "back_buffer_width": self.buffer_width, "back_buffer_height": self.buffer_height,
                                 "projection": "orthographic", "forward": [0, -1, 0], "up": [0, 0, 1]},
                    "map_transform": transform,
                    "grid": {"columns": columns, "rows": rows, "map_width_px": self.width, "map_height_px": self.height},
                    "tiles": tiles}
        manifest_path = root / "capture_manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        terrain = {"origin_x": 0.0, "origin_y": 3.0, "origin_z": 0.0, "cell_size_m": 1.0,
                   "heights_m": np.full((self.height + 1, self.width + 1), 17.0, dtype=np.float64), "layers": {}}
        return manifest_path, terrain, transform

    def test_world_centroids(self):
        """四つの非対称な四角の重心が world で一画素以内に戻る。"""
        rectangles = [((244, 84, 280, 110), (230, 35, 40)),
                      ((330, 140, 355, 158), (30, 220, 65)),
                      ((42, 22, 56, 60), (35, 60, 235)),
                      ((126, 126, 169, 139), (235, 205, 25))]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, terrain, _ = self._fixture(root / "input", rectangles)
            options = {"hillshade_strength": 0, "elevation_opacity": 0, "contour_opacity": 0, "major_contour_opacity": 0}
            compose(manifest, terrain, root / "out", options)
            image = cv2.imread(str(root / "out/map_full.png"), cv2.IMREAD_UNCHANGED)
            self.assertEqual(image.shape, (self.height, self.width, 4))
            self.assertTrue(np.all(image[..., 3] == 255))
            rgb = image[..., :3][..., ::-1]
            transform = json.loads((root / "out/map_transform.json").read_text(encoding="utf-8"))
            rect = transform["world_rect"]
            for bounds, color in rectangles:
                with self.subTest(color=color):
                    py, px = np.nonzero(np.all(rgb == color, axis=-1))
                    self.assertGreater(len(px), 0)
                    u = (px.mean() + 0.5) / transform["image_width"]
                    v = (py.mean() + 0.5) / transform["image_height"]
                    x = rect["max_x"] - u * (rect["max_x"] - rect["min_x"])
                    z = rect["max_z"] - v * (rect["max_z"] - rect["min_z"])
                    expected = ((bounds[0] + bounds[2]) * 0.5, (bounds[1] + bounds[3]) * 0.5)
                    self.assertLessEqual(np.hypot(x - expected[0], z - expected[1]), transform["pixel_size_m"])
            self.assertFalse(np.any(np.all(rgb == (255, 0, 255), axis=-1)))

    def test_tile_seams(self):
        """平面の全てのタイル境界で、両側と外側の差が一階調以内になる。"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, terrain, _ = self._fixture(root / "input")
            compose(manifest, terrain, root / "out")
            image = cv2.imread(str(root / "out/map_full.png"), cv2.IMREAD_UNCHANGED).astype(np.float64)
            for x in (144, 288):
                boundary = image[:, [x - 1, x], :3]
                outside = image[:, [x - 2, x + 1], :3]
                self.assertLessEqual(float(np.abs(boundary - outside).mean()), 1.0)
            y = 104
            boundary = image[[y - 1, y], :, :3]
            outside = image[[y - 2, y + 1], :, :3]
            self.assertLessEqual(float(np.abs(boundary - outside).mean()), 1.0)

    def test_deterministic_files(self):
        """同じ入力から出力した全七ファイルのバイトと報告のハッシュが一致する。"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, terrain, _ = self._fixture(root / "input")
            first = compose(manifest, terrain, root / "first")
            second = compose(manifest, terrain, root / "second")
            expected = {"map_full.png", "map_2048.png", "height_r8.png", "water_r8.png", "road_r8.png",
                        "map_transform.json", "compose_report.json"}
            self.assertEqual({path.name for path in (root / "first").iterdir()}, expected)
            self.assertEqual({path.name for path in (root / "second").iterdir()}, expected)
            self.assertEqual(set(first["files"]), expected)
            self.assertEqual(first["files"], second["files"])
            for name in expected:
                left = hashlib.sha256((root / "first" / name).read_bytes()).hexdigest()
                right = hashlib.sha256((root / "second" / name).read_bytes()).hexdigest()
                self.assertEqual(left, right)
                self.assertEqual(first["files"][name], left)
            report = json.loads((root / "first/compose_report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["outputs_sha256"], {key: value for key, value in first["files"].items() if key != "compose_report.json"})

    def test_flat_hillshade(self):
        """端を含め高さが一定の格子では、陰影の係数が全画素で一定になる。"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, terrain, transform = self._fixture(root / "input")
            compose(manifest, terrain, root / "out")
            shade = compute_hillshade(terrain, transform)
            self.assertEqual(shade.shape, (self.height, self.width))
            self.assertTrue(np.isfinite(shade).all())
            self.assertLessEqual(float(shade.max() - shade.min()), 1e-6)


if __name__ == "__main__":
    unittest.main()
