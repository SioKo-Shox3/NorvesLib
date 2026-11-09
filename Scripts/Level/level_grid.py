# 高さ格子から斜度、水域、通行領域と経路を計算する。

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import heapq
import math
import os
import operator

import numpy as np
from PIL import Image


_SQRT2 = math.sqrt(2.0)
_NEIGHBORS = (
    (-1, -1), (-1, 0), (-1, 1),
    (0, -1),             (0, 1),
    (1, -1),  (1, 0),   (1, 1),
)


@dataclass
class PathResult:
    """経路の水平距離、費用と始点から終点までの格子点。"""

    length_m: float
    cost_m: float
    cells: list[tuple[int, int]]


def _cell_size(cell_size_m: float) -> float:
    cell = float(cell_size_m)
    if not math.isfinite(cell) or cell <= 0.0:
        raise ValueError("標本間隔は有限の正の値にしてください。")
    return cell


def slope_deg(heights: np.ndarray, cell_size_m: float) -> np.ndarray:
    """各標本の斜度を中心差分で求め、端では片側差分を使う。"""
    cell = _cell_size(cell_size_m)
    values = np.asarray(heights, dtype=np.float64)
    if values.ndim != 2:
        raise ValueError("高さ格子は行と列を持つ二次元配列にしてください。")

    rows, cols = values.shape
    dhdx = np.zeros_like(values)
    dhdz = np.zeros_like(values)
    if cols > 1:
        dhdx[:, 0] = (values[:, 1] - values[:, 0]) / cell
        dhdx[:, -1] = (values[:, -1] - values[:, -2]) / cell
        dhdx[:, 1:-1] = (values[:, 2:] - values[:, :-2]) / (2.0 * cell)
    if rows > 1:
        dhdz[0, :] = (values[1, :] - values[0, :]) / cell
        dhdz[-1, :] = (values[-1, :] - values[-2, :]) / cell
        dhdz[1:-1, :] = (values[2:, :] - values[:-2, :]) / (2.0 * cell)

    return np.degrees(np.arctan(np.hypot(dhdx, dhdz)))


def water_mask(heights: np.ndarray, water_cfg: dict, base_dir: str) -> np.ndarray:
    """水位または画像のしきい値から、水域の真偽値を返す。"""
    mode = water_cfg["mode"]
    if mode == "none":
        return np.zeros(heights.shape, dtype=np.bool_)
    if mode == "below_height":
        return np.asarray(heights) < water_cfg["level_m"]
    if mode != "mask":
        raise ValueError(f"水域の指定方法が不明です: {mode}")

    path = os.path.join(base_dir, water_cfg["mask_path"])
    with Image.open(path) as image:
        if image.size != (heights.shape[1], heights.shape[0]):
            raise ValueError("水域マスク画像の寸法が高さ格子と一致しません。")
        if image.mode.startswith("I;16") or (image.mode == "I" and image.format == "PNG"):
            values = np.asarray(image, dtype=np.float64)
            scale = 65535.0
        elif image.mode in ("1", "L", "P", "LA", "RGB", "RGBA"):
            # 色付き画像は輝度に変換し、透明度は水域の判定に使わない。
            values = np.asarray(image.convert("L"), dtype=np.float64)
            scale = 255.0
        else:
            raise ValueError("水域マスク画像は 8 ビットまたは 16 ビットで指定してください。")
    return values / scale > water_cfg["mask_threshold"]


def default_traversable(
    slope: np.ndarray, water: np.ndarray, max_slope_deg: float
) -> np.ndarray:
    """差し替え可能な fn(slope, water, max_slope_deg) -> bool 配列の既定判定。"""
    return (slope < max_slope_deg) & ~water


def label_components(passable: np.ndarray) -> np.ndarray:
    """角を通り抜けない 8 近傍を行優先で連結成分に分ける。"""
    mask = np.ascontiguousarray(passable, dtype=np.bool_)
    if mask.ndim != 2:
        raise ValueError("通行格子は行と列を持つ二次元配列にしてください。")
    rows, cols = mask.shape
    labels = np.full((rows, cols), -1, dtype=np.int32)
    flags = memoryview(mask.reshape(-1))
    component = memoryview(labels.reshape(-1))
    next_label = 0

    for start in range(rows * cols):
        if not flags[start] or component[start] != -1:
            continue
        component[start] = next_label
        queue = deque([start])
        while queue:
            current = queue.popleft()
            row, col = divmod(current, cols)
            north = row > 0 and flags[current - cols]
            south = row + 1 < rows and flags[current + cols]
            west = col > 0 and flags[current - 1]
            east = col + 1 < cols and flags[current + 1]

            if north and component[current - cols] == -1:
                component[current - cols] = next_label
                queue.append(current - cols)
            if south and component[current + cols] == -1:
                component[current + cols] = next_label
                queue.append(current + cols)
            if west and component[current - 1] == -1:
                component[current - 1] = next_label
                queue.append(current - 1)
            if east and component[current + 1] == -1:
                component[current + 1] = next_label
                queue.append(current + 1)

            # 斜めの移動は両側の直交する標本も通れる場合に限る。
            if north and west and flags[current - cols - 1] and component[current - cols - 1] == -1:
                component[current - cols - 1] = next_label
                queue.append(current - cols - 1)
            if north and east and flags[current - cols + 1] and component[current - cols + 1] == -1:
                component[current - cols + 1] = next_label
                queue.append(current - cols + 1)
            if south and west and flags[current + cols - 1] and component[current + cols - 1] == -1:
                component[current + cols - 1] = next_label
                queue.append(current + cols - 1)
            if south and east and flags[current + cols + 1] and component[current + cols + 1] == -1:
                component[current + cols + 1] = next_label
                queue.append(current + cols + 1)
        next_label += 1
    return labels


def octile_distance_m(
    a_rc: tuple[int, int], b_rc: tuple[int, int], cell_size_m: float
) -> float:
    """障害物のない 8 近傍格子上の水平距離を返す。"""
    cell = _cell_size(cell_size_m)
    dr = abs(a_rc[0] - b_rc[0])
    dc = abs(a_rc[1] - b_rc[1])
    diagonal = min(dr, dc)
    return float((max(dr, dc) - diagonal + _SQRT2 * diagonal) * cell)


def astar(
    heights: np.ndarray,
    passable: np.ndarray,
    cell_size_m: float,
    start_rc: tuple[int, int],
    goal_rc: tuple[int, int],
    slope_cost: float,
) -> PathResult | None:
    """水平距離と高低差の費用を最小にする経路を求める。"""
    cell = _cell_size(cell_size_m)
    height_cost = float(slope_cost)
    if not math.isfinite(height_cost) or height_cost < 0.0:
        raise ValueError("高低差の費用係数は有限の 0 以上の値にしてください。")
    mask = np.ascontiguousarray(passable, dtype=np.bool_)
    values = np.ascontiguousarray(heights, dtype=np.float64)
    if mask.ndim != 2 or values.ndim != 2 or mask.shape != values.shape:
        raise ValueError("高さ格子と通行格子は同じ寸法の二次元配列にしてください。")

    rows, cols = mask.shape
    try:
        start_row, start_col = map(operator.index, start_rc)
        goal_row, goal_col = map(operator.index, goal_rc)
    except (TypeError, ValueError):
        raise ValueError("始点と終点はそれぞれ整数の行と列で指定してください。") from None
    if not (0 <= start_row < rows and 0 <= start_col < cols
            and 0 <= goal_row < rows and 0 <= goal_col < cols):
        return None

    start = start_row * cols + start_col
    goal = goal_row * cols + goal_col
    flags = memoryview(mask.reshape(-1))
    if not flags[start] or not flags[goal]:
        return None
    if start == goal:
        return PathResult(0.0, 0.0, [(start_row, start_col)])

    heights_flat = memoryview(values.reshape(-1))
    costs = np.full(rows * cols, np.inf, dtype=np.float64)
    previous = np.full(rows * cols, -1, dtype=np.int64)
    best_cost = memoryview(costs)
    parent = memoryview(previous)
    best_cost[start] = 0.0
    queue = [(octile_distance_m(start_rc, goal_rc, cell), 0.0, start_row, start_col)]
    diagonal_length = cell * _SQRT2

    while queue:
        _, current_cost, row, col = heapq.heappop(queue)
        current = row * cols + col
        if current_cost != best_cost[current]:
            continue
        if current == goal:
            cells = []
            while current != -1:
                cells.append(divmod(current, cols))
                current = parent[current]
            cells.reverse()
            diagonal_steps = sum(
                a[0] != b[0] and a[1] != b[1] for a, b in zip(cells, cells[1:])
            )
            straight_steps = len(cells) - 1 - diagonal_steps
            length = (straight_steps + _SQRT2 * diagonal_steps) * cell
            return PathResult(float(length), float(current_cost), cells)

        current_height = heights_flat[current]
        for dr, dc in _NEIGHBORS:
            next_row = row + dr
            next_col = col + dc
            if not (0 <= next_row < rows and 0 <= next_col < cols):
                continue
            neighbor = next_row * cols + next_col
            if not flags[neighbor]:
                continue
            if dr and dc and (not flags[next_row * cols + col] or not flags[row * cols + next_col]):
                continue
            step_length = diagonal_length if dr and dc else cell
            next_cost = current_cost + (step_length + height_cost * abs(heights_flat[neighbor] - current_height))
            if next_cost >= best_cost[neighbor]:
                continue
            best_cost[neighbor] = next_cost
            parent[neighbor] = current
            row_distance = abs(goal_row - next_row)
            col_distance = abs(goal_col - next_col)
            diagonal = min(row_distance, col_distance)
            estimate = (max(row_distance, col_distance) - diagonal + _SQRT2 * diagonal) * cell
            heapq.heappush(queue, (next_cost + estimate, next_cost, next_row, next_col))
    return None
