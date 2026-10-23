# 雲の形の合否（ReferenceCheck.py --cloud-shape）の独立の見本にする Poly Haven の CC0 の部分曇りの空の HDRI を取得する。
#
# 使い方:
#   python Scripts/Terrain/FetchSkySamples.py
#       Assets/Reference/SkySamples/（git 管理外）へ 2k の .hdr を落とす。取得の前に名前と大きさを出力し、30 MB を超える物は
#       落とさない。取得の後に Poly Haven の API が返す MD5 と照らす。既にあって MD5 が合う物は落とし直さない。
import hashlib
import json
import os
import sys
import urllib.request

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(REPO_ROOT, "Assets", "Reference", "SkySamples")
# 地面の映らない pure skies の、昼の部分曇りの空（積雲の塊と青い切れ間がある）
SAMPLES = ("kloofendal_48d_partly_cloudy_puresky", "kloofendal_38d_partly_cloudy_puresky", "sunflowers_puresky")
RESOLUTION = "2k"
MAX_BYTES = 30 * 1024 * 1024
HEADERS = {"User-Agent": "NorvesLib-reference-fetch"}


def fetch_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS)) as response:
        return json.load(response)


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    failed = 0
    for asset in SAMPLES:
        entry = fetch_json(f"https://api.polyhaven.com/files/{asset}")["hdri"][RESOLUTION]["hdr"]
        info = fetch_json(f"https://api.polyhaven.com/info/{asset}")
        path = os.path.join(OUT_DIR, os.path.basename(entry["url"]))
        print(f"SKY_SAMPLE name={asset} size={entry['size']} md5={entry['md5']} url={entry['url']} "
              f"authors={','.join(info.get('authors', {}))} license=CC0", flush=True)
        if entry["size"] > MAX_BYTES:
            print(f"SKY_SAMPLE skip={asset} reason=too_large")
            failed += 1
            continue
        if os.path.isfile(path) and md5_of(path) == entry["md5"]:
            print(f"SKY_SAMPLE cached={path}")
            continue
        with urllib.request.urlopen(urllib.request.Request(entry["url"], headers=HEADERS)) as response, \
                open(path, "wb") as f:
            f.write(response.read())
        digest = md5_of(path)
        print(f"SKY_SAMPLE saved={path} md5_ok={int(digest == entry['md5'])}")
        failed += int(digest != entry["md5"])
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
