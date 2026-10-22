# シーンの比較の手本の写真を Wikimedia Commons から落とし、
# manifest.json（URL・作者・ライセンス・撮影の条件の推定・写っている物）と縮小一覧の PNG を書く。
# 手本の組は 2 つ:
#   ValleyOfTheGods  荒野（ユタ州）。写真・範囲・主役の地形・前哨地の候補の選んだ理由は
#                    Docs/RenderingValidation/WastelandReference.md
#   InteriorLight    室内の光。写真と、写真から決めた部屋の大きさ・窓の向き・太陽の高さ・家具の並びは
#                    Docs/RenderingValidation/InteriorLightReference.md
#
# 使い方（--set を省くと ValleyOfTheGods）:
#   python Scripts/Terrain/ReferencePhotos.py --fetch  [--set 組]  写真を落として manifest.json と contact_sheet.png を書く
#   python Scripts/Terrain/ReferencePhotos.py --verify [--set 組]  落とした写真・manifest・.gitignore・文書が揃っているかを見る
#   python Scripts/Terrain/ReferencePhotos.py --sheet  [--set 組]  落とし済みの写真から一覧の PNG だけ作り直す
# 写真は Assets/Reference/<組>/ に置く（git 管理外）。
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
import argparse
import hashlib
import html
import json
import math
import os
import re
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GITIGNORE_PATH = os.path.join(REPO_ROOT, ".gitignore")
COMMONS_API = "https://commons.wikimedia.org/w/api.php"
USER_AGENT = "NorvesLibReferencePhotos/1.0 (https://commons.wikimedia.org/wiki/Category:Valley_of_the_Gods)"

# 使ってよいライセンス（パブリックドメイン・CC0・CC BY・CC BY-SA）。NC・ND を含むものは使わない
ALLOWED_LICENSE = re.compile(r"^(Public domain|PD.*|CC0.*|CC BY(-SA)? \d\.\d.*)$", re.IGNORECASE)
FORBIDDEN_LICENSE = re.compile(r"\b(NC|ND)\b|FAL|GFDL", re.IGNORECASE)

# 役割ごとの枚数の範囲（遠景・主役の崖・足元）
ROLE_COUNTS = {"far": (2, 3), "cliff": (1, 2), "ground": (2, 3)}

# 3DEP の 1 m タイル x60y413（NAD83 / UTM 12N、EPSG:26912）。TNM の API の boundingBox と一致を確かめ済み
TILE = {"name": "USGS_1M_12_x60y413", "epsg": 26912, "east_min": 600000, "east_max": 610000,
        "north_min": 4120000, "north_max": 4130000}

# 切り出す範囲（UTM 12N の m）。主役の Battleship Rock と Franklin・Rooster・Setting Hen の各ビュートと、
# 前哨地の候補の平らな所を含む 4 km 四方
CROP = {"east_min": 602500, "east_max": 606500, "north_min": 4125000, "north_max": 4129000}

# 名前の付いた地形（GNIS 由来の Wikidata・Commons の座標を UTM 12N へ換算した値）
FEATURES = {
    "Battleship Rock": {"lat": 37.29250, "lon": -109.83667, "role": "主役"},
    "Franklin Butte": {"lat": 37.28667, "lon": -109.82694, "role": "準主役"},
    "Rooster Butte": {"lat": 37.27639, "lon": -109.81083, "role": "中景"},
    "Setting Hen Butte": {"lat": 37.27139, "lon": -109.80667, "role": "中景"},
}

# 前哨地を置く自然に平らな場所の候補（UTM 12N の m）。半径 150 m の傾斜と平面からのずれは 3DEP の 50 m 間隔の標本で見た値
OUTPOST_CANDIDATES = [
    {"id": "A", "east": 605700, "north": 4126850, "tilt_deg": 1.1, "rms_m": 0.5, "rank": 1},
    {"id": "B", "east": 605550, "north": 4127450, "tilt_deg": 1.0, "rms_m": 0.4, "rank": 2},
    {"id": "C", "east": 604100, "north": 4126850, "tilt_deg": 1.3, "rms_m": 0.7, "rank": 3},
]

# 手本の写真。camera は EXIF の GPS があればその値（gps）、無ければ写っている地形からの推定（estimate）
PHOTOS = [
    {
        "id": "far_01", "role": "far",
        "title": "File:Valley of the Gods - panoramio (1).jpg",
        "camera": {"lat": 37.2978, "lon": -109.8318, "source": "gps"},
        "heading": "北西（方位 300〜320°の推定）。Battleship Rock の北の谷床から Castle Butte の方へ",
        "contents": "手前に板状に割れた砂岩の露頭と赤い砂、低木（ブラックブラッシュ類）が 1〜3 m 間隔で一面に点在。"
                    "中景に平らな谷床、奥に城のような大きなビュートと二本の尖塔、背後に段の付いた崖（Cedar Mesa の縁）。晴天に積雲",
        "compare": "谷床の低木の被覆の密度と大きさ、谷床の平らさと赤の彩度、奥の崖までの霞の量、ビュートの高さと間隔の比",
    },
    {
        "id": "far_02", "role": "far",
        "title": "File:Grand Staircase 201812 0239 (43703896942).jpg",
        "camera": {"lat": 37.27479, "lon": -109.80596, "source": "gps"},
        "heading": "西〜南西（推定。GPS の位置はビュートに近すぎるので数百 m の誤差がありうる）",
        "contents": "Rooster Butte と Setting Hen Butte の並び（左に尖塔の頭の小さなビュート、右に横縞の地層の大きなビュート）。"
                    "手前は礫の混じる赤い緩斜面と株立ちの低木（2〜5 m 間隔）、ビュートの裾に崖錐。薄曇り",
        "compare": "ビュートの裾の崖錐の広がりと傾き、地層の横縞の間隔、手前の礫の大きさと低木の間隔、薄曇りの空と地面の明るさの比",
    },
    {
        "id": "cliff_01", "role": "cliff",
        "title": "File:Battleship Rock, Valley Of The Gods.jpg",
        "camera": {"lat": None, "lon": None, "source": "estimate",
                   "note": "Battleship Rock の北西の谷床、1〜1.5 km 離れた所（推定。E 約 602050〜602400・N 約 4128650〜4129000）。"
                           "根拠は Commons の説明 \"Northwest aspect of Battleship Rock\"（北西面を写す）と、"
                           "低い西日が手前の面を照らしていること（EXIF の時刻 01:51 を UTC とみると現地 18:51 の日没前）。"
                           "距離は崖の見かけの大きさからの目安で、数百 m の誤差がありうる"},
        "heading": "南東（方位 120〜150°の推定）。北西面を正面から見る",
        "contents": "主役の崖の北西面。上部の垂直な崖と城壁のような頭部、中段の段々の地層と棚、下部の崖錐の斜面と落ちた岩塊、手前の谷床。"
                    "晴天で低い西日が右後ろから当たり、崖の影の面がはっきり出る",
        "compare": "崖の高さと崖錐の斜面の高さの比、地層の段の数と厚さ、崖の上端の凹凸、落石の岩塊の大きさと散らばり、日向と影の明るさの比",
    },
    {
        "id": "cliff_02", "role": "cliff",
        "title": "File:Franklin Butte, Valley of the Gods.jpg",
        "camera": {"lat": 37.30475, "lon": -109.84583, "source": "gps",
                   "note": "切り出し元のパノラマ File:Utah Valley of the Gods 17 Mile Loop (32365160328).jpg の GPS"},
        "heading": "南東（方位 約 140°。撮影位置から Franklin Butte への向き。右手前 1.6 km に Battleship Rock がある）",
        "contents": "Franklin Butte の北西面。ピラミッド状の地層の斜面と頂部の柱状の岩、奥に地平線の山並み。"
                    "色は HDR の処理で彩度と紫みが強い",
        "compare": "形と地層の間隔だけを見る（色・明るさは比較に使わない）。斜面の傾きと頂部の柱の太さの比",
    },
    {
        "id": "ground_01", "role": "ground",
        "title": "File:Valley of the Gods - HCP - March 17, 2025 - 006.jpg",
        "camera": {"lat": None, "lon": None, "source": "estimate",
                   "note": "Valley of the Gods の道沿いの谷床（同じ日の連作の 1 枚。位置の記録なし）"},
        "heading": "不明（奥に平らなメサ 2 つ）",
        "contents": "足元。礫（数 cm〜20 cm）が一面に散る赤い地表、低木の株（高さ 0.3〜1 m）、手前の高さ数十 cm の小さな侵食の段（沢の肩）。曇天",
        "compare": "礫の大きさと被覆、低木の株の大きさと間隔、地表の赤の色味（曇天なので素材の色の比較に向く）、侵食の段の形",
    },
    {
        "id": "ground_02", "role": "ground",
        "title": "File:Annular Eclipse in Valley of the Gods - 53318131401.jpg",
        "camera": {"lat": 37.300958, "lon": -109.835461, "source": "gps",
                   "note": "Battleship Rock の北 940 m の沢の中（範囲の内側）"},
        "heading": "南東（方位 約 145°の推定）。右のピラミッド形のビュートは Franklin Butte（方位 154°、1.8 km。cliff_02 と同じ形）、"
                   "左奥の小さなビュートは Rooster Butte（方位 141°、3.5 km）と見る。EXIF の焦点距離は 35 mm 換算 110 mm（水平の画角 約 19°）で、"
                   "Franklin Butte の見かけの高さ（約 3.7°）が高さ 116 m・距離 1.8 km と合う",
        "contents": "足元（沢底）。乾いた沢の流路（幅 5〜10 m）に砂と礫（数 cm〜50 cm の角ばった砂岩）、両岸に高さ 0.5〜1 m の"
                    "切り立った岸（板状の砂岩の段）、岸沿いにハコヤナギ（cottonwood）と黄色い花のラビットブラシ、低木。晴天の午後",
        "compare": "沢の幅と岸の高さ、流路の砂と礫の大きさの分布、岸の段の割れ方、沢沿いだけに木が立つ植生の帯（谷床の低木との違い）",
    },
]

# 室内の光の手本。役割ごとの枚数の範囲（低い日が窓から差す部屋・色移り・床の艶・光の筋）と全体の枚数
INTERIOR_ROLE_COUNTS = {"window": (1, 2), "bleed": (1, 2), "gloss": (1, 2), "shafts": (1, 2)}
INTERIOR_TOTAL = (4, 6)

# light は窓の向きと太陽の高さの推定と根拠。太陽の高さは EXIF の時刻と位置からの計算（位置が無いものは写真からの目安）
INTERIOR_PHOTOS = [
    {
        "id": "window_01", "role": "window",
        "title": "File:Luxurious Drawing Room in a Historic European Palace (55246911504).jpg",
        "light": "EXIF の GPS（41.1420, -8.5746。ポルトの Palácio do Freixo）と時刻 2026-04-23 10:20（WEST = UTC+1）から"
                 "太陽の高さ 39.5°・方位 110°（東南東）。左の壁の背の高い縦長の窓から入った直射が、窓の桟の格子の形のまま床に斜めに伸びる",
        "contents": "宮殿の客間（天井高 約 6 m の推定。扉の高さ 約 3 m の 2 倍）。明るい木と濃い木の菱形の寄木の床に窓の形の日だまりが"
                    "斜めに伸び、日の当たる所の木が橙に光る。中央に淡い色の絨毯、木の机、青い肘掛け椅子とソファの組。"
                    "壁は淡い緑の漆喰に金の縁取りと絵と鏡。35 mm 換算 14 mm の広角",
        "compare": "日だまりと日陰の床の明るさの比（直射と回り込む光の比）、日だまりの縁のぼけ（太陽の円盤による半影）、"
                   "日向の木の床の色の温かさ、日陰の床と壁に残る照り返しの明るさ、家具の脚の長い影",
    },
    {
        "id": "window_02", "role": "window",
        "title": "File:Sunlight in an old workshop.jpg",
        "light": "EXIF の位置（58.3569, 11.5159。スウェーデン Lysekil の Gåseberg）と時刻 2017-02-07 12:04（CET = UTC+1）から"
                 "太陽の高さ 16.3°・方位 174°（冬の正午のほぼ真南の低い太陽）。左の壁の高い窓の列から入った光が床を横切って奥の壁の下部まで届く",
        "contents": "古い機械の作業場（奥行き 15 m 以上・天井高 約 5 m の木の柱と波板の建屋）。低い太陽の光が窓の形のまま床に長く伸び、"
                    "日向の照り返しが天井の梁と壁を暖色に照らす。床はコンクリートに土埃。左の壁に配電盤。3 枚の露出からのトーンマップ",
        "compare": "低い太陽の日だまりの長さと向き（窓の高さと太陽の高さで決まる）、奥の壁に這い上がる光の帯、照り返しで天井の梁が見える明るさ、"
                   "窓に対する室内の暗部の明るさ（トーンマップの処理があるので比は目安）",
    },
    {
        "id": "gloss_01", "role": "gloss",
        "title": "File:Empty apartment room with corner windows.jpg",
        "light": "EXIF の時刻 2018-08-08 15:08（EDT）と分類（マンハッタン）から太陽の高さ 約 53°・方位 約 237°（南西）の推定"
                 "（建物の位置は記録なし。街の中の数 km の違いは高さに効かない）。右の窓から入った直射が窓の脇の壁と床に小さな日だまりを作る",
        "contents": "空き部屋の角（家具なし）。濃い色の堅木の床（艶のある仕上げ）、白い漆喰の壁と幅木、角を挟む 2 つの上げ下げ窓。"
                    "床に窓と明るい壁の像がぼやけて縦に伸びて映る。窓の外は隣のビル。スマートフォン（Nexus 5）の写真",
        "compare": "床の艶の映り込み（窓と明るい壁の像の伸び方とぼけ方 = 粗さ、映り込みの明るさ = 斜めの角度のフレネル）、"
                   "板の継ぎ目での映り込みの途切れ、日だまりから白い壁へ回る照り返しの広がり、窓の脇の壁の明るさの勾配",
    },
    {
        "id": "bleed_01", "role": "bleed",
        "title": "File:Sarasota FL Selby Gardens Payne Mansion inside02.jpg",
        "light": "EXIF の時刻 2011-06-19 16:15（EDT と見る。カメラの時計の時差は確かめられない）と Selby 植物園の位置（約 27.33, -82.54）"
                 "から太陽の高さ 約 53°・方位 273°（西）。右の壁のアーチ付きのフランス窓から入る直射が床に格子の日だまりを 2 つ作る",
        "contents": "邸宅（Christy Payne House）の玄関広間（天井高 約 5 m の推定。フランス窓の高さ 約 3 m から）。黄色の漆喰の壁、"
                    "白い階段と手すり、白い扉枠と天井。濃い色の艶のある床に窓の格子の日だまり。木の飾り棚、大時計、壁の絵",
        "compare": "黄色の壁からの色移り（白い天井・階段の側面・扉枠が黄に染まる量）、日だまりの照り返しで明るくなる周りの床と壁の下部、"
                   "艶のある床の窓の格子の映り込み、日だまりの縁の鋭さ",
    },
    {
        "id": "shafts_01", "role": "shafts",
        "title": "File:Dust dancing in the sunlight P9060755c.jpg",
        "light": "EXIF の時刻 2005-09-06 09:10。位置の記録は無い（作者の説明は「古い馬場」）。光の筋の傾きから太陽の高さは"
                 "25〜35° の目安（画面の奥行きの向きが分からないので幅を持たせる）。上部の窓から入る朝の光",
        "contents": "古い馬場の室内（ヴォールトの天井、レンガと剥げた漆喰の壁、土の床、工事の足場と一輪車）。上部の窓から斜めに差す"
                    "3 本の光の筋が塵の舞う空気の中に白く見え、床に細長い日だまりを作る",
        "compare": "光の筋の明るさと背景の暗部の比（筋が見えるのは暗い背景の前）、筋の縁の鋭さと筋の中の明るさのむら、"
                   "床の日だまりと筋のつながり、レンガと漆喰の壁の照り返しの暖かさ",
    },
]

# 室内の光の文書に、写真から決めた部屋の条件として書いてあるべき見出しの語
INTERIOR_DOC_TERMS = ("部屋の大きさ", "窓の向き", "太陽の高さ", "家具の並び")

# 手本の組。terrain は荒野の範囲・地形・前哨地の候補の確かめを行う組
SETS = {
    "ValleyOfTheGods": {
        "photos": PHOTOS, "role_counts": ROLE_COUNTS, "total": None,
        "doc": os.path.join("Docs", "RenderingValidation", "WastelandReference.md"),
        "fields": ("page_url", "original_url", "author", "license_url", "heading", "contents", "compare"),
        "header": {"source": "Wikimedia Commons", "region": "Valley of the Gods, San Juan County, Utah (BLM)"},
        "terrain": True, "doc_terms": (),
    },
    "InteriorLight": {
        "photos": INTERIOR_PHOTOS, "role_counts": INTERIOR_ROLE_COUNTS, "total": INTERIOR_TOTAL,
        "doc": os.path.join("Docs", "RenderingValidation", "InteriorLightReference.md"),
        "fields": ("page_url", "original_url", "author", "license_url", "light", "contents", "compare"),
        "header": {"source": "Wikimedia Commons", "subject": "窓から低い日が差す部屋（室内の光の手本）"},
        "terrain": False, "doc_terms": INTERIOR_DOC_TERMS,
    },
}


def set_paths(name):
    out_dir = os.path.join(REPO_ROOT, "Assets", "Reference", name)
    return out_dir, os.path.join(out_dir, "manifest.json"), os.path.join(out_dir, "contact_sheet.png")


def utm_from_latlon(lat, lon, zone=12):
    # GRS80 の横メルカトル（Krüger の級数）。NAD83 と WGS84 の差（約 1 m）はここでは無視する
    a, f, k0 = 6378137.0, 1 / 298.257222101, 0.9996
    n = f / (2 - f)
    big_a = a / (1 + n) * (1 + n * n / 4 + n ** 4 / 64)
    alpha = [n / 2 - 2 * n * n / 3 + 5 * n ** 3 / 16, 13 * n * n / 48 - 3 * n ** 3 / 5, 61 * n ** 3 / 240]
    lam = math.radians(lon) - math.radians(-183 + 6 * zone)
    phi = math.radians(lat)
    e = 2 * math.sqrt(n) / (1 + n)
    t = math.sinh(math.atanh(math.sin(phi)) - e * math.atanh(e * math.sin(phi)))
    xi = math.atan2(t, math.cos(lam))
    eta = math.atanh(math.sin(lam) / math.sqrt(1 + t * t))
    east = eta + sum(alpha[j] * math.cos(2 * (j + 1) * xi) * math.sinh(2 * (j + 1) * eta) for j in range(3))
    north = xi + sum(alpha[j] * math.sin(2 * (j + 1) * xi) * math.cosh(2 * (j + 1) * eta) for j in range(3))
    return 500000 + k0 * big_a * east, k0 * big_a * north


def ssl_context():
    # Windows の証明書ストアで Wikimedia の証明書の連鎖が期限切れと判定されることがあるので、
    # certifi があればその CA の束を使う
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def http_get(url, context):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, context=context, timeout=120) as response:
        return response.read()


def strip_html(text):
    return re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", "", text or ""))).strip()


def local_name(photo):
    extension = os.path.splitext(photo["title"])[1].lower()
    return photo["id"] + (".jpg" if extension in (".jpg", ".jpeg") else extension)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def query_commons(titles, context):
    params = {"action": "query", "titles": "|".join(titles), "prop": "imageinfo",
              "iiprop": "url|size|sha1|extmetadata", "format": "json",
              "iiextmetadatafilter": "Artist|Credit|LicenseShortName|LicenseUrl|ImageDescription|DateTimeOriginal"}
    data = json.loads(http_get(COMMONS_API + "?" + urllib.parse.urlencode(params), context))
    normalized = {entry["to"]: entry["from"] for entry in data["query"].get("normalized", [])}
    result = {}
    for page in data["query"]["pages"].values():
        if "imageinfo" not in page:
            raise RuntimeError("Commons にファイルが無い: %s" % page.get("title"))
        result[normalized.get(page["title"], page["title"])] = page["imageinfo"][0]
    return result


def crop_record():
    width = CROP["east_max"] - CROP["east_min"]
    height = CROP["north_max"] - CROP["north_min"]
    features = {}
    for name, feature in FEATURES.items():
        east, north = utm_from_latlon(feature["lat"], feature["lon"])
        features[name] = dict(feature, east=round(east), north=round(north))
    return {"tile": TILE, "crop": dict(CROP, width_m=width, height_m=height), "features": features,
            "outpost_candidates": OUTPOST_CANDIDATES}


def fetch(name):
    spec = SETS[name]
    out_dir, manifest_path, _ = set_paths(name)
    os.makedirs(out_dir, exist_ok=True)
    context = ssl_context()
    infos = query_commons([photo["title"] for photo in spec["photos"]], context)
    entries = []
    for photo in spec["photos"]:
        info = infos[photo["title"]]
        meta = {key: value.get("value", "") for key, value in info.get("extmetadata", {}).items()}
        license_name = strip_html(meta.get("LicenseShortName"))
        if FORBIDDEN_LICENSE.search(license_name) or not ALLOWED_LICENSE.match(license_name):
            raise RuntimeError("使えないライセンス: %s (%s)" % (photo["title"], license_name))
        path = os.path.join(out_dir, local_name(photo))
        if not (os.path.exists(path) and os.path.getsize(path) == info["size"]):
            print("取得: %s (%.1f MB)" % (photo["title"], info["size"] / 1e6))
            payload = http_get(info["url"], context)
            if hashlib.sha1(payload).hexdigest() != info["sha1"]:
                raise RuntimeError("Commons の SHA-1 と一致しない: %s" % photo["title"])
            with open(path, "wb") as stream:
                stream.write(payload)
        entry = {
            "id": photo["id"], "role": photo["role"], "file": local_name(photo),
            "commons_title": photo["title"], "page_url": info["descriptionurl"], "original_url": info["url"],
            "author": strip_html(meta.get("Artist")), "credit": strip_html(meta.get("Credit")),
            "license": license_name, "license_url": meta.get("LicenseUrl", ""),
            "date_taken": strip_html(meta.get("DateTimeOriginal")),
            "width": info["width"], "height": info["height"], "size_bytes": info["size"],
            "sha1": info["sha1"], "sha256": sha256_of(path),
        }
        if "camera" in photo:
            camera = dict(photo["camera"])
            if camera.get("lat") is not None:
                east, north = utm_from_latlon(camera["lat"], camera["lon"])
                camera.update(east=round(east), north=round(north))
            entry.update(camera=camera, heading=photo["heading"])
        else:
            entry["light"] = photo["light"]
        entry.update(contents=photo["contents"], compare=photo["compare"])
        entries.append(entry)
    manifest = dict(spec["header"], photos=entries)
    if spec["terrain"]:
        manifest.update(crop_record())
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("manifest: %s" % manifest_path)
    build_sheet(name, manifest)


def label_font():
    # 日本語の文字を描けるフォントを Windows の標準フォントから探す。無ければ既定のフォント
    from PIL import ImageFont
    for name in ("meiryo.ttc", "YuGothM.ttc", "msgothic.ttc"):
        try:
            return ImageFont.truetype(os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", name), 14)
        except OSError:
            continue
    return ImageFont.load_default()


def build_sheet(name, manifest):
    from PIL import Image, ImageDraw
    out_dir, _, sheet_path = set_paths(name)
    font = label_font()
    cell_w, cell_h, label_h, columns = 560, 380, 44, 3
    photos = manifest["photos"]
    rows = (len(photos) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell_w, rows * (cell_h + label_h)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for index, entry in enumerate(photos):
        with Image.open(os.path.join(out_dir, entry["file"])) as image:
            image = image.convert("RGB")
            image.thumbnail((cell_w - 10, cell_h - 10))
            x = (index % columns) * cell_w
            y = (index // columns) * (cell_h + label_h)
            sheet.paste(image, (x + (cell_w - image.width) // 2, y + (cell_h - image.height) // 2))
        label = "%s  %s  [%s]" % (entry["id"], entry["commons_title"][5:60], entry["license"])
        for line, (text, color) in enumerate(((label, (255, 230, 120)), ("作者: " + entry["author"], (200, 200, 200)))):
            # 隣のセルへはみ出さないよう、セルの幅に収まるまで末尾を削る
            while len(text) > 1 and draw.textlength(text, font=font) > cell_w - 12:
                text = text[:-2] + "…"
            draw.text((x + 6, y + cell_h + 4 + 18 * line), text, fill=color, font=font)
    sheet.save(sheet_path)
    print("一覧: %s (%dx%d)" % (sheet_path, sheet.width, sheet.height))


def verify_terrain(manifest, check):
    # 荒野の組だけ: 切り出しの範囲・名前の付いた地形・前哨地の候補が DEM のタイルの内側にあるか
    crop = manifest.get("crop", {})
    check(crop == dict(CROP, width_m=CROP["east_max"] - CROP["east_min"], height_m=CROP["north_max"] - CROP["north_min"]),
          "manifest の範囲がスクリプトの範囲と一致する")
    width = CROP["east_max"] - CROP["east_min"]
    height = CROP["north_max"] - CROP["north_min"]
    check(2000 <= width <= 4000 and 2000 <= height <= 4000, "範囲が 2〜4 km 四方（%d x %d m）" % (width, height))
    margin = 100
    check(CROP["east_min"] - margin >= TILE["east_min"] and CROP["east_max"] + margin <= TILE["east_max"]
          and CROP["north_min"] - margin >= TILE["north_min"] and CROP["north_max"] + margin <= TILE["north_max"],
          "範囲と周りの余白 %d m がタイル %s の内側" % (margin, TILE["name"]))
    for name, feature in manifest.get("features", {}).items():
        inside = CROP["east_min"] <= feature["east"] <= CROP["east_max"] and CROP["north_min"] <= feature["north"] <= CROP["north_max"]
        check(inside, "%s（%s, E %d N %d）が範囲の内側" % (name, feature["role"], feature["east"], feature["north"]))
    for candidate in OUTPOST_CANDIDATES:
        inside = CROP["east_min"] <= candidate["east"] <= CROP["east_max"] and CROP["north_min"] <= candidate["north"] <= CROP["north_max"]
        check(inside, "前哨地の候補 %s が範囲の内側" % candidate["id"])


def verify(name):
    spec = SETS[name]
    out_dir, manifest_path, sheet_path = set_paths(name)
    doc_path = os.path.join(REPO_ROOT, spec["doc"])
    failures = []

    def check(condition, message):
        print(("OK   " if condition else "FAIL ") + message)
        if not condition:
            failures.append(message)

    print("set=%s" % name)
    check(os.path.exists(manifest_path), "manifest.json がある")
    if not os.path.exists(manifest_path):
        return 1
    with open(manifest_path, encoding="utf-8") as stream:
        manifest = json.load(stream)
    photos = manifest.get("photos", [])
    check([entry["id"] for entry in photos] == [photo["id"] for photo in spec["photos"]], "manifest の写真がスクリプトの一覧と一致する")
    for role, (low, high) in spec["role_counts"].items():
        count = sum(1 for entry in photos if entry["role"] == role)
        check(low <= count <= high, "%s の写真が %d 枚（%d〜%d 枚）" % (role, count, low, high))
    if spec["total"]:
        low, high = spec["total"]
        check(low <= len(photos) <= high, "写真が全部で %d 枚（%d〜%d 枚）" % (len(photos), low, high))
    for entry in photos:
        path = os.path.join(out_dir, entry["file"])
        exists = os.path.exists(path)
        check(exists and os.path.getsize(path) == entry["size_bytes"] and sha256_of(path) == entry["sha256"],
              "%s: ファイルがあり大きさと SHA-256 が manifest と一致する" % entry["file"])
        license_name = entry.get("license", "")
        check(bool(ALLOWED_LICENSE.match(license_name)) and not FORBIDDEN_LICENSE.search(license_name),
              "%s: ライセンス %s は PD・CC0・CC BY・CC BY-SA のどれか（NC・ND なし）" % (entry["id"], license_name))
        missing = [key for key in spec["fields"] if not entry.get(key)]
        if spec["terrain"]:
            camera = entry.get("camera", {})
            if camera.get("source") == "gps":
                if camera.get("east") is None:
                    missing.append("camera.east")
            elif not camera.get("note"):
                missing.append("camera.note")
        check(not missing, "%s: 記録の欄が揃っている%s" % (entry["id"], "" if not missing else "（欠け: %s）" % ", ".join(missing)))
    if spec["terrain"]:
        verify_terrain(manifest, check)
    if os.path.exists(sheet_path):
        from PIL import Image
        with Image.open(sheet_path) as sheet:
            sheet_size = sheet.size
        newest = max(os.path.getmtime(os.path.join(out_dir, entry["file"])) for entry in photos
                     if os.path.exists(os.path.join(out_dir, entry["file"])))
        check(os.path.getmtime(sheet_path) >= newest, "一覧の PNG がある（%dx%d）・写真より新しい" % sheet_size)
    else:
        check(False, "一覧の PNG がある")
    with open(GITIGNORE_PATH, encoding="utf-8-sig") as stream:
        ignore_lines = {line.strip() for line in stream}
    for pattern in ("/Assets/Reference/", "/Assets/Terrain/"):
        check(pattern in ignore_lines, ".gitignore に %s がある" % pattern)
    if os.path.exists(doc_path):
        with open(doc_path, encoding="utf-8-sig") as stream:
            doc = stream.read()
        if spec["terrain"]:
            coordinates = ["%d" % CROP[key] for key in ("east_min", "east_max", "north_min", "north_max")]
            check(all(value in doc for value in coordinates), "文書に範囲の座標が書いてある")
        check(all(entry["id"] in doc for entry in photos), "文書に写真ごとの節がある")
        for term in spec["doc_terms"]:
            check(term in doc, "文書に「%s」が書いてある" % term)
    else:
        check(False, "文書 %s がある" % spec["doc"])
    print("result=%s" % ("pass" if not failures else "fail (%d)" % len(failures)))
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="シーンの手本の写真を落とし、manifest と一覧を書いて確かめる")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--fetch", action="store_true", help="写真を落として manifest.json と一覧の PNG を書く")
    group.add_argument("--sheet", action="store_true", help="一覧の PNG だけ作り直す")
    group.add_argument("--verify", action="store_true", help="写真・manifest・.gitignore・文書を確かめる")
    parser.add_argument("--set", choices=sorted(SETS), default="ValleyOfTheGods", help="手本の組（既定は荒野）")
    args = parser.parse_args()
    # 出力をファイルへ保存して読むので、端末の文字コードに依らず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.fetch:
        fetch(args.set)
        return 0
    if args.sheet:
        with open(set_paths(args.set)[1], encoding="utf-8") as stream:
            build_sheet(args.set, json.load(stream))
        return 0
    return verify(args.set)


if __name__ == "__main__":
    sys.exit(main())
