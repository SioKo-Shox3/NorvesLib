# -*- coding: utf-8 -*-
"""手元の ComfyUI で、シーン用の 3D モデル(GLB)を生成・検証するスクリプト。標準ライブラリだけで動く。

定義: Assets/AssetSets/GeneratedModels.json(id・英語のプロンプト・seed・目標面数・テクスチャの大きさ・実寸の高さ・用途)
ワークフロー: Tools/ComfyWorkflows/(API 形式。文字→画像と画像→3D)
出力: Assets/Models/Generated/<id>/(<id>.glb・reference.png・manifest.json。git 管理外)

使い方:
  python Scripts/GenerateSceneModels.py --ids banana            生成(ComfyUI が動いていなければ起動し、終わったら止める)
  python Scripts/GenerateSceneModels.py --ids banana --refit    既存の GLB を定義の実寸(height_m)へ合わせ直す(GPU 不使用)
  python Scripts/GenerateSceneModels.py --ids banana --preview  Blender のヘッドレスで GLB を 3 方向から描く
  python Scripts/GenerateSceneModels.py --verify [--require-cooked] [--ids banana]
                                                                GPU を使わず、定義・manifest・実ファイルの MD5 と GLB の実寸の一致を確かめる

生成の時間: 1 体あたりの上限(--model-time-limit、既定 900 秒)を超えたら ComfyUI の /interrupt で止め、
upsample_resolution を 1024 にして 1 回だけやり直す(やり直しも超えたら失敗として次の id へ)。
1 回の実行の合計(--run-budget、既定 1800 秒)は開始時に決めた絶対時刻の締切 1 つで守る。ComfyUI の起動・参照画像・
3D・GLB の取得・復旧(/queue・再起動)・後処理はすべてこの締切までに打ち切り、終了処理(/interrupt・解放・停止)の分は
先に取っておく。収まらない生成は見送り、次の実行へ回す。
manifest が定義と食い違わない id は飛ばす(--force で作り直す)。

生成は GPU を占有する。Game を撮る前に ComfyUI が止まっていること(このスクリプトは終了時に降ろして止める)。
"""
import argparse
import datetime
import hashlib
import json
import os
import struct
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFINITION_PATH = os.path.join(REPO_ROOT, "Assets", "AssetSets", "GeneratedModels.json")
WORKFLOW_DIR = os.path.join(REPO_ROOT, "Tools", "ComfyWorkflows")
TEXT_TO_IMAGE_WORKFLOW = os.path.join(WORKFLOW_DIR, "text_to_image_zimage_turbo_api.json")
IMAGE_TO_3D_WORKFLOW = os.path.join(WORKFLOW_DIR, "image_to_3d_trellis2_api.json")
OUTPUT_ROOT = os.path.join(REPO_ROOT, "Assets", "Models", "Generated")
COOKED_MANIFEST = os.path.join(REPO_ROOT, "build", "CookedAssets", "manifest.json")
BLENDER_PREVIEW_SCRIPT = os.path.join(os.path.dirname(__file__), "BlenderPreviewGlb.py")

DEFAULT_COMFY_ROOT = r"Y:\ComfyUI\ComfyUI_windows_portable"
DEFAULT_BLENDER = r"Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe"
DEFAULT_PORT = 8188

# 画像→3D の質感の段(Trellis2UpsampleStage)の解像度。大きい建物では形の段が数千万面になり終わらないので、定義ごとに下げられる
DEFAULT_UPSAMPLE_RESOLUTION = 1536
FALLBACK_UPSAMPLE_RESOLUTION = 1024
DEFAULT_MODEL_TIME_LIMIT = 900
REFERENCE_TIME_LIMIT = 900
DEFAULT_RUN_BUDGET = 1800
# 残りがこれ未満なら新しい試行を始めない(起動・読み込みだけで尽きるため)
MIN_ATTEMPT_SECONDS = 180
STARTUP_TIMEOUT_SECONDS = 300
RECOVER_WAIT_SECONDS = 90
# 1 体の後処理(実寸合わせ・manifest)の分。3D の生成と GLB の取得はこの分を残して打ち切る
POSTPROCESS_SECONDS = 60
# 終了処理の各段の上限。合計(SHUTDOWN_RESERVE_SECONDS)を実行の締切から先に取っておく
INTERRUPT_SECONDS = 10
RELEASE_SECONDS = 20
STOP_SECONDS = 30
SHUTDOWN_RESERVE_SECONDS = INTERRUPT_SECONDS + RELEASE_SECONDS + STOP_SECONDS

# 参照画像の被写体の置き方。背景除去が効きやすいよう、単色の背景・影なしを求める。
PROMPT_SUFFIX = ", centered, isolated on a plain light grey background, studio lighting, no shadow, sharp focus, photorealistic"

REQUIRED_FIELDS = ("id", "prompt", "seed", "target_faces", "texture_size", "height_m", "usage")


class PipelineError(RuntimeError):
    pass


class WorkflowTimeout(PipelineError):
    """締切(1 体の上限か実行の締切)までに終わらなかった。"""


class RunClock:
    """1 回の実行の締切(絶対時刻)。

    final_deadline は開始時刻 + 予算で、終了処理を含むすべてがここまでに終わる。
    work_deadline は終了処理の分を先に引いた締切で、起動・生成・取得・復旧・後処理はここまでに打ち切る。
    """

    def __init__(self, budget, shutdown_reserve=SHUTDOWN_RESERVE_SECONDS):
        self.started = time.time()
        self.final_deadline = self.started + budget
        self.work_deadline = self.final_deadline - shutdown_reserve

    def elapsed(self):
        return time.time() - self.started

    def remaining(self, final=False):
        return (self.final_deadline if final else self.work_deadline) - time.time()

    def allowance(self, cap, deadline=None, final=False):
        """cap 秒・deadline・実行の締切(final なら最終締切)のうち最も早いものまでの秒数を返す(負もあり得る)。"""
        limit = self.final_deadline if final else self.work_deadline
        if deadline is not None:
            limit = min(limit, deadline)
        return min(cap, limit - time.time())


def call_within(function, seconds):
    """function を別スレッドで走らせ、seconds 秒で打ち切る。

    urllib のタイムアウトはソケット操作 1 回ごとなので、それだけでは呼び出し全体の時間を締切で縛れない。
    打ち切ったスレッドは捨てる(デーモンなので終了を妨げない)。終わらなければ TimeoutError。
    """
    result = {}

    def target():
        try:
            result["value"] = function()
        except BaseException as error:
            result["error"] = error

    worker = threading.Thread(target=target, daemon=True)
    worker.start()
    worker.join(max(seconds, 0.0))
    if worker.is_alive():
        raise TimeoutError("%.0f 秒で終わりませんでした" % seconds)
    if "error" in result:
        raise result["error"]
    return result["value"]


def log(message):
    print(message, flush=True)


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_definitions():
    with open(DEFINITION_PATH, "r", encoding="utf-8-sig") as stream:
        data = json.load(stream)
    models = data.get("models")
    if not isinstance(models, list) or not models:
        raise PipelineError("%s の models は空でない配列にしてください" % DEFINITION_PATH)
    seen = set()
    for model in models:
        for field in REQUIRED_FIELDS:
            if field not in model:
                raise PipelineError("%s: 定義 %r に %s がありません" % (DEFINITION_PATH, model.get("id"), field))
        if model["id"] in seen:
            raise PipelineError("%s: id が重複しています: %s" % (DEFINITION_PATH, model["id"]))
        seen.add(model["id"])
    return models


def select_models(models, ids):
    if not ids:
        return models
    by_id = {m["id"]: m for m in models}
    unknown = [i for i in ids if i not in by_id]
    if unknown:
        raise PipelineError("定義に無い id: %s(定義: %s)" % (", ".join(unknown), ", ".join(by_id)))
    return [by_id[i] for i in ids]


def model_dir(model_id):
    return os.path.join(OUTPUT_ROOT, model_id)


def read_glb(path):
    """GLB の JSON 部を返す。"""
    with open(path, "rb") as stream:
        header = stream.read(12)
        if len(header) < 12 or header[:4] != b"glTF":
            raise PipelineError("GLB ではありません: %s" % path)
        chunk_length, chunk_type = struct.unpack("<II", stream.read(8))
        if chunk_type != 0x4E4F534A:
            raise PipelineError("GLB の先頭チャンクが JSON ではありません: %s" % path)
        return json.loads(stream.read(chunk_length).decode("utf-8"))


def glb_statistics(path):
    """三角形数・材質テクスチャの種類・位置の範囲(メートル相当の単位のまま)を返す。"""
    gltf = read_glb(path)
    accessors = gltf.get("accessors", [])
    triangles = 0
    bounds_min = None
    bounds_max = None
    for mesh in gltf.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            if "indices" in primitive:
                triangles += accessors[primitive["indices"]]["count"] // 3
            else:
                triangles += accessors[primitive["attributes"]["POSITION"]]["count"] // 3
            position = accessors[primitive["attributes"]["POSITION"]]
            if "min" in position and "max" in position:
                if bounds_min is None:
                    bounds_min, bounds_max = list(position["min"]), list(position["max"])
                else:
                    bounds_min = [min(a, b) for a, b in zip(bounds_min, position["min"])]
                    bounds_max = [max(a, b) for a, b in zip(bounds_max, position["max"])]
    textures = set()
    for material in gltf.get("materials", []):
        pbr = material.get("pbrMetallicRoughness", {})
        if "baseColorTexture" in pbr:
            textures.add("base_color")
        if "metallicRoughnessTexture" in pbr:
            textures.add("metallic_roughness")
        if "normalTexture" in material:
            textures.add("normal")
        if "occlusionTexture" in material:
            textures.add("occlusion")
    return {"triangles": triangles, "textures": sorted(textures), "bounds_min": bounds_min, "bounds_max": bounds_max}


def glb_height(path):
    """GLB の位置の範囲から、高さ(Y 方向の長さ)を返す。範囲が無ければ None。"""
    statistics = glb_statistics(path)
    if statistics["bounds_min"] is None:
        return None
    return statistics["bounds_max"][1] - statistics["bounds_min"][1]


def fit_glb_height(path, height_m):
    """GLB の頂点位置を一様に拡縮して、Y 方向の高さを height_m に合わせる。倍率を返す(合っていれば 1.0)。

    生成物の大きさは元の単位のまま(高さ約 1)なので、定義の実寸へここで合わせる。
    原点まわりの拡縮なので位置の中心は動かない。法線・接線・UV は一様拡縮で変わらない。
    """
    current = glb_height(path)
    if current is None or current <= 0.0:
        raise PipelineError("GLB に位置の範囲がなく、実寸へ合わせられません: %s" % path)
    scale = float(height_m) / current
    if abs(scale - 1.0) < 1e-4:
        return 1.0
    with open(path, "rb") as stream:
        data = bytearray(stream.read())
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise PipelineError("GLB の先頭チャンクが JSON ではありません: %s" % path)
    json_start = 20
    bin_header = json_start + json_length
    bin_length, bin_type = struct.unpack_from("<II", data, bin_header)
    if bin_type != 0x004E4942:
        raise PipelineError("GLB の 2 番目のチャンクが BIN ではありません: %s" % path)
    bin_start = bin_header + 8
    gltf = json.loads(bytes(data[json_start:bin_header]).decode("utf-8"))
    for node in gltf.get("nodes", []):
        if any(key in node for key in ("matrix", "translation", "rotation", "scale")):
            raise PipelineError("ノードに変換があり、頂点の拡縮では合わせられません: %s" % path)
    if gltf.get("skins") or gltf.get("animations"):
        raise PipelineError("スキンかアニメーションがあり、頂点の拡縮では合わせられません: %s" % path)
    accessors = gltf["accessors"]
    views = gltf["bufferViews"]
    position_ids = sorted({p["attributes"]["POSITION"] for m in gltf.get("meshes", []) for p in m.get("primitives", [])})
    for accessor_id in position_ids:
        accessor = accessors[accessor_id]
        if accessor["componentType"] != 5126 or accessor["type"] != "VEC3" or "sparse" in accessor:
            raise PipelineError("POSITION が float の VEC3 ではありません: %s" % path)
        view = views[accessor["bufferView"]]
        stride = view.get("byteStride", 12)
        base = bin_start + view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        for index in range(accessor["count"]):
            offset = base + index * stride
            x, y, z = struct.unpack_from("<3f", data, offset)
            struct.pack_into("<3f", data, offset, x * scale, y * scale, z * scale)
        accessor["min"] = [v * scale for v in accessor["min"]]
        accessor["max"] = [v * scale for v in accessor["max"]]
    # JSON チャンクの長さを変えない(末尾の空白で埋める)。BIN の位置がずれないようにする
    encoded = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    if len(encoded) > json_length:
        raise PipelineError("JSON チャンクが元より長くなり、書き戻せません: %s" % path)
    data[json_start:bin_header] = encoded + b" " * (json_length - len(encoded))
    temporary = path + ".tmp"
    with open(temporary, "wb") as stream:
        stream.write(data)
    os.replace(temporary, path)
    return scale


# ---------------------------------------------------------------------------------------------
# ComfyUI
# ---------------------------------------------------------------------------------------------

class ComfyServer:
    """ComfyUI の起動・停止・API 呼び出し。自分で起動したものだけ止める。

    API 呼び出しはすべて clock の締切で打ち切る(終了処理だけ final=True で最終締切まで使える)。
    """

    def __init__(self, comfy_root, port, clock):
        self.comfy_root = comfy_root
        self.port = port
        self.clock = clock
        self.base = "http://127.0.0.1:%d" % port
        self.process = None
        self.external = False
        self.client_id = uuid.uuid4().hex

    def _request(self, path, data=None, timeout=60, raw=False, headers=None, deadline=None, final=False):
        """1 回の上限 timeout 秒と締切(deadline と実行の締切)の早いほうで打ち切る。
        締切で打ち切ったら WorkflowTimeout、timeout で打ち切ったら TimeoutError。締切を過ぎていれば呼ばない。"""
        seconds = self.clock.allowance(timeout, deadline, final)
        if seconds <= 0:
            raise WorkflowTimeout("締切を過ぎたため %s を呼びません" % path.split("?")[0])
        request = urllib.request.Request(self.base + path, data=data, headers=headers or {})

        def call():
            with urllib.request.urlopen(request, timeout=seconds) as response:
                return response.read()

        try:
            body = call_within(call, seconds)
        except (TimeoutError, urllib.error.URLError) as error:
            timed_out = isinstance(error, TimeoutError) or isinstance(getattr(error, "reason", None), TimeoutError)
            if timed_out and seconds < timeout:
                raise WorkflowTimeout("%s が締切までに終わりませんでした" % path.split("?")[0]) from error
            raise
        return body if raw else json.loads(body.decode("utf-8"))

    def is_alive(self, deadline=None):
        try:
            self._request("/system_stats", timeout=3, deadline=deadline)
            return True
        except (urllib.error.URLError, OSError, ValueError, WorkflowTimeout):
            return False

    def ensure_running(self, deadline):
        """deadline(絶対時刻。起動の上限 STARTUP_TIMEOUT_SECONDS と実行の締切でさらに縛る)までに起動する。
        実行の締切で間に合わなければ WorkflowTimeout(見送り)、起動の上限で間に合わなければ PipelineError。"""
        startup_deadline = time.time() + STARTUP_TIMEOUT_SECONDS
        limit = min(startup_deadline, deadline, self.clock.work_deadline)
        if self.is_alive(limit):
            self.external = self.process is None
            log("COMFY already_running port=%d(止めずに使う)" % self.port)
            return
        if limit - time.time() <= 0:
            raise WorkflowTimeout("ComfyUI を起動する時間が残っていません")
        python = os.path.join(self.comfy_root, "python_embeded", "python.exe")
        main = os.path.join(self.comfy_root, "ComfyUI", "main.py")
        if not (os.path.isfile(python) and os.path.isfile(main)):
            raise PipelineError("ComfyUI が見つかりません: %s" % self.comfy_root)
        log("COMFY starting port=%d" % self.port)
        log_path = os.path.join(REPO_ROOT, "build", "ComfyUI.log")
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        log_file = open(log_path, "wb")
        self.process = subprocess.Popen(
            [python, "-s", main, "--windows-standalone-build", "--disable-auto-launch", "--port", str(self.port)],
            cwd=self.comfy_root, stdout=log_file, stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
        while time.time() < limit:
            if self.process.poll() is not None:
                raise PipelineError("ComfyUI が起動中に終了しました(ログ: %s)" % log_path)
            if self.is_alive(limit):
                log("COMFY started")
                return
            time.sleep(max(min(2, limit - time.time()), 0))
        if limit < startup_deadline:
            raise WorkflowTimeout("ComfyUI の起動が締切までに終わりませんでした(ログ: %s)" % log_path)
        raise PipelineError("ComfyUI の起動が %d 秒で終わりませんでした(ログ: %s)" % (STARTUP_TIMEOUT_SECONDS, log_path))

    def release_models(self):
        """モデルと確保メモリを降ろす(VRAM を空ける)。終了処理の分の時間で行い、停止の分は残す。"""
        deadline = self.clock.final_deadline - STOP_SECONDS
        try:
            body = json.dumps({"unload_models": True, "free_memory": True}).encode("utf-8")
            self._request("/free", data=body, timeout=RELEASE_SECONDS, raw=True, deadline=deadline, final=True)
            log("COMFY models_released")
        except (urllib.error.URLError, OSError, ValueError, WorkflowTimeout) as error:
            log("COMFY release_failed %s" % error)

    def interrupt(self):
        """実行中のワークフローを止める。終了処理の分の時間で行い、解放と停止の分は残す。"""
        deadline = self.clock.final_deadline - RELEASE_SECONDS - STOP_SECONDS
        try:
            self._request("/interrupt", data=b"{}", timeout=INTERRUPT_SECONDS, raw=True, deadline=deadline, final=True)
            log("COMFY interrupt_sent")
        except (urllib.error.URLError, OSError, ValueError, WorkflowTimeout) as error:
            log("COMFY interrupt_failed %s" % error)

    def is_idle(self, deadline):
        try:
            queue = self._request("/queue", timeout=10, deadline=deadline)
        except (urllib.error.URLError, OSError, ValueError, WorkflowTimeout):
            return False
        return not queue.get("queue_running") and not queue.get("queue_pending")

    def recover_after_timeout(self, wait_seconds):
        """/interrupt はノードの切れ目でしか効かない。wait_seconds(実行の締切でさらに縛る)だけ待ち、
        実行中のノードが終わらなければ、自分で起動した ComfyUI は止める。止めたら True(続けるなら呼び出し側が
        ensure_running で起動し直す)、アイドルに戻っていれば False を返す。時間が残っていなければ /queue は呼ばない。"""
        deadline = min(time.time() + max(wait_seconds, 0), self.clock.work_deadline)
        while time.time() < deadline:
            if self.is_idle(deadline):
                log("COMFY idle_after_interrupt")
                return False
            time.sleep(max(min(3, deadline - time.time()), 0))
        if self.process is None:
            if time.time() >= self.clock.work_deadline:
                # 実行の締切で待てなかっただけ。呼び出し側は残りが無いので見送る
                log("COMFY not_idle_at_deadline(自分で起動したものではないので止めません)")
                return False
            raise PipelineError("ComfyUI が /interrupt の後も止まりません(自分で起動したものではないので止めません)")
        log("COMFY stop_after_interrupt")
        self.stop()
        return True

    def stop(self):
        """自分で起動した ComfyUI を止める。最終締切までの残り(上限 STOP_SECONDS)で行う。
        プロセスを残すと GPU を占有し続けるので、残りが尽きていても taskkill だけは最短 1 秒で試す。"""
        if self.process is None:
            return
        try:
            subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                           timeout=max(self.clock.allowance(STOP_SECONDS, final=True), 1))
        except subprocess.TimeoutExpired:
            log("COMFY taskkill_timeout")
        try:
            self.process.wait(timeout=max(self.clock.allowance(STOP_SECONDS, final=True), 0))
        except subprocess.TimeoutExpired:
            pass
        self.process = None
        log("COMFY stopped")

    def upload_image(self, path, deadline):
        boundary = uuid.uuid4().hex
        name = os.path.basename(path)
        with open(path, "rb") as stream:
            content = stream.read()
        parts = [
            ("--%s\r\nContent-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\n"
             "Content-Type: image/png\r\n\r\n" % (boundary, name)).encode("utf-8"),
            content,
            ("\r\n--%s\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--%s--\r\n"
             % (boundary, boundary)).encode("utf-8"),
        ]
        headers = {"Content-Type": "multipart/form-data; boundary=" + boundary}
        return self._request("/upload/image", data=b"".join(parts), timeout=120, headers=headers, deadline=deadline)["name"]

    def run_workflow(self, workflow, deadline, label):
        """ワークフローを投入して deadline(絶対時刻。実行の締切でさらに縛る)まで完了を待ち、history の outputs を返す。
        間に合わなければ /interrupt を送って WorkflowTimeout。"""
        end = min(deadline, self.clock.work_deadline)
        body = json.dumps({"prompt": workflow, "client_id": self.client_id}).encode("utf-8")
        try:
            prompt_id = self._request("/prompt", data=body, timeout=60, headers={"Content-Type": "application/json"},
                                      deadline=end)["prompt_id"]
        except urllib.error.HTTPError as error:
            raise PipelineError("ワークフローを受け付けませんでした(%s): %s" % (label, error.read().decode("utf-8", "replace")[:2000]))
        log("COMFY queued %s prompt_id=%s" % (label, prompt_id))
        started = time.time()
        next_report = started + 60
        while time.time() < end:
            try:
                history = self._request("/history/" + prompt_id, deadline=end)
            except WorkflowTimeout:
                break
            if prompt_id in history:
                entry = history[prompt_id]
                status = entry.get("status", {})
                if status.get("status_str") == "error":
                    messages = [m for m in status.get("messages", []) if m and m[0] == "execution_error"]
                    raise PipelineError("ワークフローが失敗しました(%s): %s" % (label, json.dumps(messages, ensure_ascii=False)[:3000]))
                if entry.get("outputs") is not None and status.get("completed", True):
                    log("COMFY done %s %.0fs" % (label, time.time() - started))
                    return entry["outputs"]
            if time.time() >= next_report:
                log("COMFY waiting %s %.0fs" % (label, time.time() - started))
                next_report += 60
            time.sleep(max(min(3, end - time.time()), 0))
        self.interrupt()
        raise WorkflowTimeout("ワークフローが締切までに終わりませんでした(%s、%.0f 秒待った)" % (label, time.time() - started))

    def fetch_output(self, outputs, node_id, destination, deadline):
        """出力ノードのファイルを destination へ保存する。取得は deadline(実行の締切でさらに縛る)で打ち切る。"""
        files = []
        for value in outputs.get(node_id, {}).values():
            if isinstance(value, list):
                files += [v for v in value if isinstance(v, dict) and "filename" in v]
        if not files:
            raise PipelineError("出力ノード %s にファイルがありません: %s" % (node_id, json.dumps(outputs, ensure_ascii=False)[:1500]))
        item = files[-1]
        query = urllib.parse.urlencode({"filename": item["filename"], "subfolder": item.get("subfolder", ""), "type": item.get("type", "output")})
        data = self._request("/view?" + query, timeout=300, raw=True, deadline=deadline)
        with open(destination, "wb") as stream:
            stream.write(data)
        return item["filename"]


def load_workflow(path):
    with open(path, "r", encoding="utf-8") as stream:
        return json.load(stream)


def generate_reference(server, model, destination, deadline):
    workflow = load_workflow(TEXT_TO_IMAGE_WORKFLOW)
    workflow["4"]["inputs"]["text"] = model["prompt"] + PROMPT_SUFFIX
    workflow["8"]["inputs"]["seed"] = int(model["seed"])
    workflow["10"]["inputs"]["filename_prefix"] = "generated/%s_reference" % model["id"]
    outputs = server.run_workflow(workflow, deadline=deadline, label="%s:image" % model["id"])
    server.fetch_output(outputs, "10", destination, deadline=server.clock.work_deadline)


def generate_glb(server, model, reference_path, destination, upsample_resolution, deadline):
    """参照画像から GLB を作る。投入と生成の待ちは deadline(1 体の上限)まで、取得は後処理の分を残した実行の締切まで。"""
    uploaded = server.upload_image(reference_path, deadline)
    workflow = load_workflow(IMAGE_TO_3D_WORKFLOW)
    size = int(model["texture_size"])
    workflow["94"]["inputs"]["target_resolution"] = str(int(upsample_resolution))
    workflow["122"]["inputs"]["image"] = uploaded
    workflow["186"]["inputs"]["target_face_count"] = int(model["target_faces"])
    workflow["288"]["inputs"]["value"] = size
    workflow["224"]["inputs"]["resolution"] = min(size, 2048)
    workflow["233"]["inputs"]["resolution"] = min(size, 2048)
    for node in ("3", "12", "18", "23"):
        workflow[node]["inputs"]["seed"] = int(model["seed"])
    workflow["322"]["inputs"]["filename_prefix"] = "3d/generated_%s" % model["id"]
    outputs = server.run_workflow(workflow, deadline=deadline, label="%s:3d@%d" % (model["id"], int(upsample_resolution)))
    server.fetch_output(outputs, "322", destination, deadline=server.clock.work_deadline - POSTPROCESS_SECONDS)


def write_manifest(model, directory, upsample_resolution):
    glb_path = os.path.join(directory, model["id"] + ".glb")
    reference_path = os.path.join(directory, "reference.png")
    statistics = glb_statistics(glb_path)
    manifest = {
        "version": 1,
        "id": model["id"],
        "prompt": model["prompt"],
        "seed": int(model["seed"]),
        "target_faces": int(model["target_faces"]),
        "texture_size": int(model["texture_size"]),
        "height_m": float(model["height_m"]),
        "usage": model["usage"],
        "upsample_resolution": int(upsample_resolution),
        "face_count": statistics["triangles"],
        "textures": statistics["textures"],
        "bounds_min": statistics["bounds_min"],
        "bounds_max": statistics["bounds_max"],
        "glb": {"file": os.path.basename(glb_path), "md5": md5_of(glb_path)},
        "reference": {"file": os.path.basename(reference_path), "md5": md5_of(reference_path)},
        "workflows": {
            "text_to_image": md5_of(TEXT_TO_IMAGE_WORKFLOW),
            "image_to_3d": md5_of(IMAGE_TO_3D_WORKFLOW),
        },
        "generated_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    }
    with open(os.path.join(directory, "manifest.json"), "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    return manifest


def generate_model(server, model, directory, args):
    """1 体を作る。server.clock の締切の範囲で、1 体の上限を超えたら質感の解像度を下げて 1 回だけやり直す。
    作れたら True、実行の締切に収まらず見送ったら None、失敗は PipelineError。"""
    clock = server.clock
    glb_path = os.path.join(directory, model["id"] + ".glb")
    reference_path = os.path.join(directory, "reference.png")
    os.makedirs(directory, exist_ok=True)
    if args.reuse_reference and os.path.isfile(reference_path):
        log("GEN reuse_reference %s" % model["id"])
    else:
        reference_deadline = time.time() + REFERENCE_TIME_LIMIT
        try:
            generate_reference(server, model, reference_path, deadline=reference_deadline)
        except WorkflowTimeout as error:
            if clock.work_deadline < reference_deadline:
                log("GEN deferred id=%s(参照画像が実行の締切までに終わらない: %s)" % (model["id"], error))
                return None
            raise
    if args.reference_only:
        log("GEN reference_only %s -> %s" % (model["id"], reference_path))
        return True
    upsample = int(model.get("upsample_resolution", DEFAULT_UPSAMPLE_RESOLUTION))
    attempts = [upsample] + ([FALLBACK_UPSAMPLE_RESOLUTION] if upsample > FALLBACK_UPSAMPLE_RESOLUTION else [])
    for index, resolution in enumerate(attempts):
        # 後処理の分を残した残り
        left = clock.remaining() - POSTPROCESS_SECONDS
        if left < MIN_ATTEMPT_SECONDS:
            log("GEN deferred id=%s(1 回の実行の残りが %d 秒)" % (model["id"], left))
            return None
        limit = min(args.model_time_limit, left)
        # 1 体の上限より実行の締切が先に来るなら、打ち切りは見送り(次の実行へ)として扱う
        budget_bound = limit < args.model_time_limit
        try:
            generate_glb(server, model, reference_path, glb_path, resolution, time.time() + limit)
        except WorkflowTimeout as error:
            log("GEN timeout id=%s upsample_resolution=%d limit=%ds" % (model["id"], resolution, limit))
            stopped = server.recover_after_timeout(RECOVER_WAIT_SECONDS)
            if budget_bound or clock.remaining() - POSTPROCESS_SECONDS <= 0:
                log("GEN deferred id=%s(実行の締切までに終わらない: %s)" % (model["id"], error))
                return None
            if index + 1 == len(attempts):
                raise PipelineError("%s: 上限時間内に終わりませんでした(%s)" % (model["id"], error))
            if stopped:
                try:
                    server.ensure_running(clock.work_deadline - POSTPROCESS_SECONDS - MIN_ATTEMPT_SECONDS)
                except WorkflowTimeout as restart_error:
                    log("GEN deferred id=%s(再起動の時間が残っていません: %s)" % (model["id"], restart_error))
                    return None
            log("GEN retry id=%s upsample_resolution=%d" % (model["id"], attempts[index + 1]))
            continue
        scale = fit_glb_height(glb_path, model["height_m"])
        log("GEN fit_height id=%s height_m=%s scale=%.6f" % (model["id"], model["height_m"], scale))
        manifest = write_manifest(model, directory, resolution)
        log("GEN done id=%s faces=%d textures=%s upsample_resolution=%d glb_md5=%s"
            % (model["id"], manifest["face_count"], ",".join(manifest["textures"]), resolution, manifest["glb"]["md5"]))
        return True
    return None


def command_generate(args, models):
    # 締切は開始時に絶対時刻で 1 つだけ決める(定義の確認の時間も含める)
    clock = RunClock(args.run_budget)
    pending = []
    for model in models:
        if not args.force and not args.reference_only and not verify_model(model, False, None):
            log("GEN skip id=%s(manifest が定義と一致。作り直すなら --force)" % model["id"])
            continue
        pending.append(model)
    if not pending:
        log("GEN nothing_to_do")
        return 0
    failed = []
    deferred = []
    server = ComfyServer(args.comfy_root, args.port, clock)
    try:
        try:
            server.ensure_running(clock.work_deadline)
        except WorkflowTimeout as error:
            log("GEN deferred_all(%s)" % error)
            deferred = [model["id"] for model in pending]
            pending = []
        for model in pending:
            if clock.remaining() < MIN_ATTEMPT_SECONDS:
                deferred.append(model["id"])
                continue
            try:
                result = generate_model(server, model, model_dir(model["id"]), args)
            except (PipelineError, OSError) as error:
                # 通信の失敗(1 回の上限での打ち切りを含む)もその id の失敗にして次へ進む
                log("GEN failed id=%s %s" % (model["id"], error))
                failed.append(model["id"])
                continue
            if result is None:
                deferred.append(model["id"])
    finally:
        # 終了処理は先に取っておいた分(最終締切まで)で行う。自分で起動したものは止めれば VRAM も空くので降ろさない
        if server.process is None and server.external:
            server.release_models()
        server.stop()
    log("GEN summary elapsed=%ds budget=%ds failed=%s deferred=%s"
        % (clock.elapsed(), args.run_budget, ",".join(failed) or "-", ",".join(deferred) or "-"))
    return 1 if failed else 0


# ---------------------------------------------------------------------------------------------
# プレビュー・検証
# ---------------------------------------------------------------------------------------------

def command_refit(args, models):
    """既存の GLB を定義の実寸へ合わせ直し、manifest を書き直す(GPU・ComfyUI 不使用)。"""
    for model in models:
        directory = model_dir(model["id"])
        glb_path = os.path.join(directory, model["id"] + ".glb")
        if not os.path.isfile(glb_path) or not os.path.isfile(os.path.join(directory, "reference.png")):
            raise PipelineError("GLB か参照画像がありません(先に生成してください): %s" % directory)
        scale = fit_glb_height(glb_path, model["height_m"])
        manifest_path = os.path.join(directory, "manifest.json")
        recorded = None
        if os.path.isfile(manifest_path):
            with open(manifest_path, "r", encoding="utf-8") as stream:
                recorded = json.load(stream).get("upsample_resolution")
        upsample = int(recorded if recorded is not None else model.get("upsample_resolution", DEFAULT_UPSAMPLE_RESOLUTION))
        manifest = write_manifest(model, directory, upsample)
        log("REFIT id=%s height_m=%s scale=%.6f glb_md5=%s" % (model["id"], model["height_m"], scale, manifest["glb"]["md5"]))
    return 0


def command_preview(args, models):
    if not os.path.isfile(args.blender):
        raise PipelineError("Blender が見つかりません: %s" % args.blender)
    for model in models:
        directory = model_dir(model["id"])
        glb_path = os.path.join(directory, model["id"] + ".glb")
        if not os.path.isfile(glb_path):
            raise PipelineError("GLB がありません(先に生成してください): %s" % glb_path)
        prefix = os.path.join(directory, "preview")
        command = [args.blender, "--background", "--factory-startup", "--python", BLENDER_PREVIEW_SCRIPT,
                   "--", glb_path, prefix]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        if result.returncode != 0:
            raise PipelineError("Blender が失敗しました(%d):\n%s" % (result.returncode, result.stdout[-3000:]))
        views = [prefix + "_" + name + ".png" for name in ("front", "side", "top")]
        missing = [v for v in views if not os.path.isfile(v)]
        if missing:
            raise PipelineError("プレビューが出ませんでした: %s\n%s" % (missing, result.stdout[-3000:]))
        log("PREVIEW id=%s %s" % (model["id"], " ".join(views)))
    return 0


def verify_model(model, require_cooked, cooked_paths):
    """問題の一覧を返す(空なら合格)。GPU も ComfyUI も使わない。"""
    problems = []
    directory = model_dir(model["id"])
    manifest_path = os.path.join(directory, "manifest.json")
    if not os.path.isfile(manifest_path):
        return ["manifest.json がありません: %s" % manifest_path]
    with open(manifest_path, "r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    for key, expected in (("id", model["id"]), ("usage", model["usage"]), ("prompt", model["prompt"]), ("seed", int(model["seed"])),
                          ("target_faces", int(model["target_faces"])), ("texture_size", int(model["texture_size"])),
                          ("height_m", float(model["height_m"]))):
        if manifest.get(key) != expected:
            problems.append("定義と manifest が食い違い: %s(定義 %r / manifest %r)" % (key, expected, manifest.get(key)))
    for key, filename in (("glb", model["id"] + ".glb"), ("reference", "reference.png")):
        entry = manifest.get(key, {})
        path = os.path.join(directory, filename)
        if not os.path.isfile(path):
            problems.append("%s がありません: %s" % (key, path))
        elif md5_of(path) != entry.get("md5"):
            problems.append("%s の MD5 が manifest と一致しません: %s" % (key, path))
    glb_path = os.path.join(directory, model["id"] + ".glb")
    if os.path.isfile(glb_path):
        height = glb_height(glb_path)
        expected_height = float(model["height_m"])
        if height is None or abs(height - expected_height) > expected_height * 0.01:
            problems.append("GLB の高さが実寸と合いません(定義 %s m / GLB %s。--refit で合わせ直せます)" % (expected_height, height))
    for key in ("face_count", "generated_at"):
        if key not in manifest:
            problems.append("manifest に %s がありません" % key)
    if "face_count" in manifest and int(manifest["face_count"]) <= 0:
        problems.append("面数が 0 です")
    if require_cooked:
        problems += verify_cooked(model, cooked_paths)
    return problems


def cooked_logical_path(model):
    return "Models/Generated/%s/%s.glb" % (model["id"], model["id"])


def verify_cooked(model, cooked_paths):
    """クック済みのマニフェストに、この id のモデルの項目と実ファイルがあることを確かめる。"""
    if not os.path.isfile(COOKED_MANIFEST):
        return ["クックのマニフェストがありません: %s(Scripts/CookAssets.ps1 を実行してください)" % COOKED_MANIFEST]
    with open(COOKED_MANIFEST, "r", encoding="utf-8-sig") as stream:
        assets = json.load(stream).get("assets", [])
    logical = cooked_logical_path(model)
    entries = [a for a in assets if a.get("logical_path") == logical and a.get("kind") == "model"]
    if not entries:
        return ["クック済みの項目がありません: %s" % logical]
    problems = []
    # GLB 内蔵のテクスチャは <論理パス>.img<n>.png の項目として一緒に焼かれる。モデルの項目と同様に実ファイルを確かめる
    embedded = [a for a in assets if a.get("kind") == "texture" and a.get("logical_path", "").startswith(logical + ".")]
    if not embedded:
        problems.append("クック済みの内蔵テクスチャがありません: %s.img<n>.png" % logical)
    for entry in entries + embedded:
        package = os.path.join(os.path.dirname(COOKED_MANIFEST), entry.get("cooked_package", "").replace("/", os.sep))
        if not os.path.isfile(package):
            problems.append("クック済みのパッケージがありません: %s" % package)
    return problems


def command_verify(args, models):
    failed = 0
    for model in models:
        problems = verify_model(model, args.require_cooked, None)
        if problems:
            failed += 1
            for problem in problems:
                log("VERIFY_FAIL id=%s %s" % (model["id"], problem))
        else:
            log("VERIFY_OK id=%s%s" % (model["id"], " cooked" if args.require_cooked else ""))
    log("VERIFY %s" % ("failed=%d" % failed if failed else "pass"))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description="ComfyUI でシーン用の 3D モデルを生成・検証する")
    parser.add_argument("--ids", nargs="+", default=[], help="対象の id(空白かカンマ区切り。省略時は定義の全部)")
    parser.add_argument("--verify", action="store_true", help="定義・manifest・実ファイルの一致を確かめる(GPU 不使用)")
    parser.add_argument("--require-cooked", action="store_true", help="--verify でクック済みの出力も要求する")
    parser.add_argument("--preview", action="store_true", help="Blender のヘッドレスで GLB を 3 方向から描く(生成はしない)")
    parser.add_argument("--refit", action="store_true", help="既存の GLB を定義の実寸へ合わせ直し、manifest を書き直す(GPU 不使用)")
    parser.add_argument("--reference-only", action="store_true", help="参照画像だけ作る(seed の選び直し用)")
    parser.add_argument("--reuse-reference", action="store_true", help="参照画像があればそれを使い、3D だけ作り直す")
    parser.add_argument("--force", action="store_true", help="manifest が定義と一致していても作り直す")
    parser.add_argument("--model-time-limit", type=int, default=DEFAULT_MODEL_TIME_LIMIT,
                        help="1 体の画像→3D の上限秒数(超えたら質感の解像度を下げて 1 回やり直す)")
    parser.add_argument("--run-budget", type=int, default=DEFAULT_RUN_BUDGET,
                        help="1 回の実行の合計秒数(ComfyUI の起動から終了処理まで。収まらない分は次の実行へ)")
    parser.add_argument("--comfy-root", default=DEFAULT_COMFY_ROOT)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--blender", default=DEFAULT_BLENDER)
    args = parser.parse_args()
    ids = [i for chunk in args.ids for i in chunk.split(",") if i]
    try:
        models = select_models(load_definitions(), ids)
        if args.verify:
            return command_verify(args, models)
        if args.refit:
            return command_refit(args, models)
        if args.preview:
            return command_preview(args, models)
        return command_generate(args, models)
    except PipelineError as error:
        log("ERROR %s" % error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
