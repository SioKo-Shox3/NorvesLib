# 荒野（Valley of the Gods）の地形の元データ、3DEP の 1 m DEM タイル 1 枚を落として確かめる。
# ファイルは固定（USGS_1M_12_x60y413_UT_WestEast_B22.tif）。URL は The National Map の API で引く。
# 3DEP の製品はパブリックドメイン。
#
# 使い方:
#   python Scripts/Terrain/DemInput.py --fetch    DEM を落として manifest.json（サイズ・SHA-256・URL・取得日）を書く
#   python Scripts/Terrain/DemInput.py --fetch-neighbors   遠景に使う隣の 1 m タイル 3 枚を落として manifest の neighbors に書く
#   python Scripts/Terrain/DemInput.py --fetch-regional    遠景の外の段に使う 1/3 秒角の 2 枚を落として manifest の regional に書く
#   python Scripts/Terrain/DemInput.py --verify   DEM・manifest・Python のパッケージ・.gitignore を確かめ、
#                                                 座標系・画素の大きさ・行の向き・NoData・高さの範囲を表示する
# DEM は Assets/Terrain/Source/ に置く（git 管理外）。パッケージは Scripts/Terrain/requirements.txt。
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request

from ReferencePhotos import CROP, REPO_ROOT, TILE, sha256_of, ssl_context

SOURCE_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "Source")
MANIFEST_PATH = os.path.join(SOURCE_DIR, "manifest.json")
REQUIREMENTS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "requirements.txt")
TNM_API = "https://tnmaccess.nationalmap.gov/api/v1/products"
USER_AGENT = "NorvesLibDemInput/1.0"

# 使うタイル。TNM の API の title と配布元のファイル名で固定する
DEM_TITLE = "USGS 1 Meter 12 x60y413 UT_WestEast_B22"
DEM_FILE = "USGS_1M_12_x60y413_UT_WestEast_B22.tif"
DEM_HOST = "prd-tnm.s3.amazonaws.com"
DEM_PATH = os.path.join(SOURCE_DIR, DEM_FILE)
# API が返した大きさ（2024-10-30 公開の版）。版が替わったら落とし直す前に気付けるよう固定する
DEM_SIZE = 268270390
DEM_QUERY_BBOX = "-109.84,37.28,-109.82,37.29"

# 遠景（SCN-WL-HORIZON）に使う隣の 1 m タイル。同じ作成計画・同じ公開日の 3 枚で、手元のタイルと合わせて
# E 590000〜610000・N 4120000〜4140000 の 20 km 四方になる。大きさは API が返した値（2024-10-30 公開の版）
NEIGHBOR_TILES = [
    {"title": "USGS 1 Meter 12 x60y414 UT_WestEast_B22", "file": "USGS_1M_12_x60y414_UT_WestEast_B22.tif",
     "size": 310736898, "bbox": "-109.84,37.32,-109.82,37.33",
     "east_min": 600000, "east_max": 610000, "north_min": 4130000, "north_max": 4140000,
     "role": "Castle Butte・北の Cedar Mesa の縁"},
    {"title": "USGS 1 Meter 12 x59y414 UT_WestEast_B22", "file": "USGS_1M_12_x59y414_UT_WestEast_B22.tif",
     "size": 305207474, "bbox": "-109.95,37.32,-109.93,37.33",
     "east_min": 590000, "east_max": 600000, "north_min": 4130000, "north_max": 4140000,
     "role": "北西の Cedar Mesa の縁"},
    {"title": "USGS 1 Meter 12 x59y413 UT_WestEast_B22", "file": "USGS_1M_12_x59y413_UT_WestEast_B22.tif",
     "size": 295344808, "bbox": "-109.95,37.25,-109.93,37.26",
     "east_min": 590000, "east_max": 600000, "north_min": 4120000, "north_max": 4130000,
     "role": "西の地平線（Lady in the Bathtub など）"},
]

# 遠景の外の段（SCN-WL-HORIZON の outer）に使う 1/3 秒角（約 10 m 間隔）の 2 枚。1 m タイル 4 枚の外の東・南の地平線
# （ridge・outpost の視点から見える 20 km 以上先）を覆う。座標系は NAD83 の経緯度（EPSG:4269）、高さは NAVD88。
# 1 m タイルと違い TNM の API の現行の版ではなく historical の固定の版を使うので、URL と大きさをここで固定する
REGIONAL_TILES = [
    {"file": "USGS_13_n38w110_20260630.tif", "size": 421008125,
     "url": "https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/historical/n38w110/USGS_13_n38w110_20260630.tif",
     "lat_min": 37.0, "lat_max": 38.0, "lon_min": -110.0, "lon_max": -109.0,
     "role": "東の地平線（4 枚の東の外、北緯 37〜38°）"},
    {"file": "USGS_13_n37w110_20260514.tif", "size": 403904756,
     "url": "https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/historical/n37w110/USGS_13_n37w110_20260514.tif",
     "lat_min": 36.0, "lat_max": 37.0, "lon_min": -110.0, "lon_max": -109.0,
     "role": "南の地平線（北緯 37° より南、Monument Valley の方角）"},
]

# 切り出しの余白。手本の写真 cliff_01 の推定の撮影位置が範囲の西の外 100〜450 m なので西は 600 m、
# cliff_02 の撮影位置が北の外 305 m なので北は 500 m 取る
CROP_MARGIN = {"west": 600, "east": 100, "south": 100, "north": 500}


def tnm_product(context, title=DEM_TITLE, bbox=DEM_QUERY_BBOX):
    query = urllib.parse.urlencode({
        "datasets": "Digital Elevation Model (DEM) 1 meter",
        "bbox": bbox,
        "prodFormats": "GeoTIFF",
        "max": "50",
        "outputFormat": "JSON",
    })
    request = urllib.request.Request(TNM_API + "?" + query, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, context=context, timeout=120) as response:
        items = json.load(response).get("items", [])
    for item in items:
        if item.get("title") == title:
            return item
    raise RuntimeError("TNM の API に %s が無い（%d 件）" % (title, len(items)))


def fetch_tile(context, title, file_name, size, bbox):
    """TNM の API で URL を引き、大きさが固定値と合うことを確かめてから落とし、manifest の 1 項目を返す"""
    item = tnm_product(context, title, bbox)
    url = item["downloadURL"]
    parsed = urllib.parse.urlparse(url)
    if parsed.hostname != DEM_HOST or os.path.basename(parsed.path) != file_name:
        raise RuntimeError("API の URL が想定と違う: %s" % url)
    if int(item["sizeInBytes"]) != size:
        raise RuntimeError("API の大きさ %s が固定値 %d と違う（版が替わった）" % (item["sizeInBytes"], size))
    path = os.path.join(SOURCE_DIR, file_name)
    if os.path.isfile(path) and os.path.getsize(path) == size:
        print("落とし済み: %s" % path)
    else:
        print("取得: %s (%.1f MB)" % (url, size / 1e6), flush=True)
        download(url, path, context)
    return {
        "file": file_name,
        "title": item["title"],
        "url": url,
        "size": os.path.getsize(path),
        "sha256": sha256_of(path),
        "retrieved": datetime.date.today().isoformat(),
        "publication_date": item.get("publicationDate"),
        "last_updated": item.get("lastUpdated"),
        "bounding_box_wgs84": item.get("boundingBox"),
        "license": "Public domain (All 3DEP products are public domain.)",
        "source": TNM_API,
    }


def fetch_neighbors():
    """遠景に使う隣の 3 枚を落とし、manifest.json の neighbors に書く（手元のタイルの項目はそのまま）"""
    context = ssl_context()
    with open(MANIFEST_PATH, encoding="utf-8") as stream:
        manifest = json.load(stream)
    entries = []
    for tile in NEIGHBOR_TILES:
        entry = fetch_tile(context, tile["title"], tile["file"], tile["size"], tile["bbox"])
        entry["role"] = tile["role"]
        entries.append(entry)
    manifest["neighbors"] = entries
    with open(MANIFEST_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("manifest: %s（neighbors %d 枚）" % (MANIFEST_PATH, len(entries)))


def fetch_regional():
    """遠景の外の段に使う 1/3 秒角の 2 枚を固定の URL から落とし、manifest.json の regional に書く"""
    context = ssl_context()
    with open(MANIFEST_PATH, encoding="utf-8") as stream:
        manifest = json.load(stream)
    entries = []
    for tile in REGIONAL_TILES:
        parsed = urllib.parse.urlparse(tile["url"])
        if parsed.hostname != DEM_HOST or os.path.basename(parsed.path) != tile["file"]:
            raise RuntimeError("URL が想定と違う: %s" % tile["url"])
        request = urllib.request.Request(tile["url"], method="HEAD", headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(request, context=context, timeout=120) as response:
            length = int(response.headers.get("Content-Length", "0"))
            last_modified = response.headers.get("Last-Modified")
        if length != tile["size"]:
            raise RuntimeError("配布元の大きさ %d が固定値 %d と違う（版が替わった）" % (length, tile["size"]))
        path = os.path.join(SOURCE_DIR, tile["file"])
        if os.path.isfile(path) and os.path.getsize(path) == tile["size"]:
            print("落とし済み: %s" % path)
        else:
            print("取得: %s (%.1f MB)" % (tile["url"], tile["size"] / 1e6), flush=True)
            download(tile["url"], path, context)
        entries.append({
            "file": tile["file"],
            "url": tile["url"],
            "size": os.path.getsize(path),
            "sha256": sha256_of(path),
            "retrieved": datetime.date.today().isoformat(),
            "last_modified": last_modified,
            "bounds_nad83": {"lat_min": tile["lat_min"], "lat_max": tile["lat_max"],
                             "lon_min": tile["lon_min"], "lon_max": tile["lon_max"]},
            "license": "Public domain (All 3DEP products are public domain.)",
            "role": tile["role"],
        })
    manifest["regional"] = entries
    with open(MANIFEST_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("manifest: %s（regional %d 枚）" % (MANIFEST_PATH, len(entries)))


def download(url, path, context):
    partial = path + ".part"
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, context=context, timeout=300) as response, open(partial, "wb") as stream:
        total = int(response.headers.get("Content-Length", "0"))
        done = 0
        next_report = 0
        for block in iter(lambda: response.read(1 << 20), b""):
            stream.write(block)
            done += len(block)
            if done >= next_report:
                print("  %.0f / %.0f MB" % (done / 1e6, total / 1e6), flush=True)
                next_report += 50 * 1000 * 1000
    os.replace(partial, path)


def fetch():
    context = ssl_context()
    os.makedirs(SOURCE_DIR, exist_ok=True)
    item = tnm_product(context)
    url = item["downloadURL"]
    parsed = urllib.parse.urlparse(url)
    if parsed.hostname != DEM_HOST or os.path.basename(parsed.path) != DEM_FILE:
        raise RuntimeError("API の URL が想定と違う: %s" % url)
    if int(item["sizeInBytes"]) != DEM_SIZE:
        raise RuntimeError("API の大きさ %s が固定値 %d と違う（版が替わった）" % (item["sizeInBytes"], DEM_SIZE))
    if os.path.isfile(DEM_PATH) and os.path.getsize(DEM_PATH) == DEM_SIZE:
        print("落とし済み: %s" % DEM_PATH)
    else:
        print("取得: %s (%.1f MB)" % (url, DEM_SIZE / 1e6))
        download(url, DEM_PATH, context)
    manifest = {
        "file": DEM_FILE,
        "title": item["title"],
        "url": url,
        "size": os.path.getsize(DEM_PATH),
        "sha256": sha256_of(DEM_PATH),
        "retrieved": datetime.date.today().isoformat(),
        "publication_date": item.get("publicationDate"),
        "last_updated": item.get("lastUpdated"),
        "bounding_box_wgs84": item.get("boundingBox"),
        "license": "Public domain (All 3DEP products are public domain.)",
        "source": TNM_API,
    }
    with open(MANIFEST_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("manifest: %s" % MANIFEST_PATH)


def pinned_requirements():
    pins = {}
    with open(REQUIREMENTS_PATH, encoding="utf-8-sig") as stream:
        for line in stream:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            match = re.match(r"^([A-Za-z0-9_.-]+)==([^;\s]+)", line)
            pins[line.split(";")[0].strip()] = match.groups() if match else None
    return pins


def describe_dem(check):
    import numpy
    import rasterio
    from rasterio.windows import Window, from_bounds

    with rasterio.open(DEM_PATH) as dataset:
        epsg = dataset.crs.to_epsg() if dataset.crs else None
        transform = dataset.transform
        bounds = dataset.bounds
        nodata = dataset.nodata
        print("寸法: %d x %d 画素、バンド %d、型 %s、圧縮 %s、ブロック %s" % (
            dataset.width, dataset.height, dataset.count, dataset.dtypes[0],
            dataset.compression.value if dataset.compression else "なし", dataset.block_shapes[0]))
        print("座標系: EPSG:%s（%s）" % (epsg, dataset.crs.to_string() if dataset.crs else "無し"))
        print("画素の大きさ: x %.6f m、y %.6f m" % (transform.a, transform.e))
        print("範囲（画素の外縁）: E %.3f〜%.3f、N %.3f〜%.3f" % (bounds.left, bounds.right, bounds.bottom, bounds.top))
        print("行の向き: %s" % ("北から南（行 0 が北端）" if transform.e < 0 else "南から北（行 0 が南端）"))
        print("NoData の値: %r" % nodata)
        print("単位: %s、高さの説明: %s" % (dataset.units, dataset.descriptions))
        # 標高の基準はファイルの CRS に入っていない（水平の EPSG:26912 だけ）。3DEP の 1 m DEM は NAVD88
        vertical = dataset.crs.to_wkt() if dataset.crs else ""
        print("垂直の基準: %s" % ("CRS に NAVD88" if "NAVD" in vertical.upper() else "CRS に無し（3DEP の仕様で NAVD88）"))
        print("画素の解釈: %s" % dataset.tags().get("AREA_OR_POINT"))

        check(dataset.count == 1, "バンドが 1 本")
        check(epsg == TILE["epsg"], "座標系が EPSG:%d（NAD83 / UTM 12N）" % TILE["epsg"])
        check(abs(transform.a - 1.0) < 1e-9 and abs(transform.e + 1.0) < 1e-9 and transform.b == 0 and transform.d == 0,
              "画素が 1 m 四方で回転なし")
        check(bounds.left <= TILE["east_min"] and bounds.right >= TILE["east_max"]
              and bounds.bottom <= TILE["north_min"] and bounds.top >= TILE["north_max"],
              "範囲がタイル %s（E %d〜%d、N %d〜%d）を覆う" % (
                  TILE["name"], TILE["east_min"], TILE["east_max"], TILE["north_min"], TILE["north_max"]))

        # 全体を行の帯ごとに読んで NoData と高さの範囲を数える
        nodata_count = 0
        low = numpy.inf
        high = -numpy.inf
        total = 0
        holes = None
        rows = 1024
        for row in range(0, dataset.height, rows):
            window = Window(0, row, dataset.width, min(rows, dataset.height - row))
            block = dataset.read(1, window=window)
            invalid = ~numpy.isfinite(block)
            if nodata is not None:
                invalid |= block == nodata
            nodata_count += int(invalid.sum())
            total += block.size
            invalid_rows = numpy.flatnonzero(invalid.any(axis=1))
            if invalid_rows.size:
                invalid_cols = numpy.flatnonzero(invalid.any(axis=0))
                hole = [row + int(invalid_rows[0]), row + int(invalid_rows[-1]), int(invalid_cols[0]), int(invalid_cols[-1])]
                if holes is None:
                    holes = hole
                else:
                    holes = [min(holes[0], hole[0]), max(holes[1], hole[1]), min(holes[2], hole[2]), max(holes[3], hole[3])]
            valid = block[~invalid]
            if valid.size:
                low = min(low, float(valid.min()))
                high = max(high, float(valid.max()))
        print("NoData の画素数: %d / %d（%.4f %%）" % (nodata_count, total, 100.0 * nodata_count / total))
        if holes:
            # 外接する範囲を画素の外縁の UTM 座標で示す
            print("NoData の外接範囲: 行 %d〜%d・列 %d〜%d（E %.0f〜%.0f、N %.0f〜%.0f）" % (
                holes[0], holes[1], holes[2], holes[3],
                transform.c + holes[2] * transform.a, transform.c + (holes[3] + 1) * transform.a,
                transform.f + (holes[1] + 1) * transform.e, transform.f + holes[0] * transform.e))
        print("高さの範囲: %.3f〜%.3f m（差 %.3f m）" % (low, high, high - low))
        check(1000.0 < low < high < 2500.0, "高さが Valley of the Gods の標高として妥当（1000〜2500 m）")

        # 切り出す範囲と余白が全部有効な画素で覆われているか（隣のタイルが要らないか）
        west = CROP["east_min"] - CROP_MARGIN["west"]
        east = CROP["east_max"] + CROP_MARGIN["east"]
        south = CROP["north_min"] - CROP_MARGIN["south"]
        north = CROP["north_max"] + CROP_MARGIN["north"]
        inside = bounds.left <= west and east <= bounds.right and bounds.bottom <= south and north <= bounds.top
        check(inside, "切り出し E %d〜%d、N %d〜%d（余白込み）がタイルの内側" % (west, east, south, north))
        if inside:
            window = from_bounds(west, south, east, north, transform).round_offsets().round_lengths()
            block = dataset.read(1, window=window)
            invalid = ~numpy.isfinite(block)
            if nodata is not None:
                invalid |= block == nodata
            print("切り出しの窓: 列 %d〜、行 %d〜、%d x %d 画素、高さ %.3f〜%.3f m" % (
                window.col_off, window.row_off, window.width, window.height,
                float(block[~invalid].min()), float(block[~invalid].max())))
            check(int(invalid.sum()) == 0, "切り出し（余白込み）に NoData が無い（%d 画素）" % int(invalid.sum()))


def verify():
    failures = []

    def check(condition, message):
        print(("OK   " if condition else "FAIL ") + message)
        if not condition:
            failures.append(message)

    # パッケージは requirements.txt で版を固定し、入っている版と一致させる
    from importlib import metadata
    check(os.path.isfile(REQUIREMENTS_PATH), "requirements.txt がある")
    if os.path.isfile(REQUIREMENTS_PATH):
        pins = pinned_requirements()
        check(all(pins.values()), "requirements.txt のすべての行が == で版を固定している")
        for name in ("rasterio", "numpy"):
            check(any(pin and pin[0].lower() == name for pin in pins.values()), "requirements.txt に %s がある" % name)
        for pin in pins.values():
            if not pin:
                continue
            try:
                installed = metadata.version(pin[0])
            except metadata.PackageNotFoundError:
                installed = None
            if pin[0].lower() == "colorama" and sys.platform != "win32":
                continue
            check(installed == pin[1], "%s==%s が入っている（入っている版 %s）" % (pin[0], pin[1], installed))

    # manifest とファイル
    check(os.path.isfile(MANIFEST_PATH), "manifest.json がある")
    manifest = {}
    if os.path.isfile(MANIFEST_PATH):
        with open(MANIFEST_PATH, encoding="utf-8") as stream:
            manifest = json.load(stream)
        for key in ("file", "url", "size", "sha256", "retrieved"):
            check(bool(manifest.get(key)), "manifest に %s がある" % key)
        parsed = urllib.parse.urlparse(manifest.get("url", ""))
        check(parsed.scheme == "https" and parsed.hostname == DEM_HOST and os.path.basename(parsed.path) == DEM_FILE,
              "URL が %s の %s" % (DEM_HOST, DEM_FILE))
        check(bool(re.match(r"^\d{4}-\d{2}-\d{2}$", manifest.get("retrieved", ""))), "取得日が YYYY-MM-DD")
        print("URL: %s" % manifest.get("url"))
        print("取得日: %s、公開日: %s" % (manifest.get("retrieved"), manifest.get("publication_date")))
    exists = os.path.isfile(DEM_PATH)
    check(exists, "DEM がある: %s" % os.path.relpath(DEM_PATH, REPO_ROOT))
    if exists:
        size = os.path.getsize(DEM_PATH)
        digest = sha256_of(DEM_PATH)
        print("サイズ: %d バイト、SHA-256: %s" % (size, digest))
        check(size == DEM_SIZE, "サイズが API の値 %d と一致" % DEM_SIZE)
        check(size == manifest.get("size"), "サイズが manifest と一致")
        check(digest == manifest.get("sha256"), "SHA-256 が manifest と一致")

    # git 管理外
    ignored = all(subprocess.run(["git", "check-ignore", "-q", path], cwd=REPO_ROOT).returncode == 0
                  for path in (DEM_PATH, MANIFEST_PATH))
    check(ignored, "Assets/Terrain/Source/ が git の無視対象")

    if exists:
        try:
            describe_dem(check)
        except ImportError as error:
            check(False, "GeoTIFF を読むパッケージが入っている（%s）" % error)

    print("result=%s" % ("pass" if not failures else "fail (%d)" % len(failures)))
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="荒野の地形の元データ（3DEP 1 m DEM）を落として確かめる")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--fetch", action="store_true", help="DEM を落として manifest.json を書く")
    group.add_argument("--verify", action="store_true", help="DEM・manifest・パッケージを確かめて中身を表示する")
    group.add_argument("--fetch-neighbors", action="store_true", help="遠景に使う隣の 1 m タイル 3 枚を落として manifest に足す")
    group.add_argument("--fetch-regional", action="store_true", help="遠景の外の段に使う 1/3 秒角の 2 枚を落として manifest に足す")
    args = parser.parse_args()
    # 出力をファイルへ保存して読むので、端末の文字コードに依らず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.fetch:
        fetch()
        return 0
    if args.fetch_neighbors:
        fetch_neighbors()
        return 0
    if args.fetch_regional:
        fetch_regional()
        return 0
    return verify()


if __name__ == "__main__":
    sys.exit(main())
