# 指定のシーンを Game.exe で撮り、視点ごとの PNG と画素の数値（metrics.json）を OutDir へ書き出す。
# Game を --scene=<id> --capture-png --capture-deterministic 付きで起動する。決定的な撮影は、シーンの組み立てが終わった
# 印（SCENE_READY）を待ってから数え始めるので、シーンごとの待ち時間の調整は要らない。
#   mean_luminance      … Rec.709 の重み（0.2126R+0.7152G+0.0722B）で求めた画素値の平均（0〜255）
#   clipped_white_ratio … R・G・B がすべて 255 の画素の割合
#   crushed_black_ratio … R・G・B がすべて 0 の画素の割合
#
# -Views を省くと、そのシーンの全視点を撮る。既定の視点を default.png に、残りの視点を <名前>.png に撮る（既定の視点の実際の
# 名前は metrics.json の scene_view に入る）。-Views を与えると、その視点だけを撮る（視点の名前は Game/Scenes/SceneCatalog.h のシーンの定義）。
# 決定的な撮影で -Views を省いたとき・2 つ以上の視点を与えたときは、Game を 1 回だけ起動し、--capture-views でシーンの組み立てを
# 1 回で済ませて視点を順に撮る（視点ごとにカメラを置き直し、ストリーミングが追いつくのを待ってから、決定的な撮影のエポックを
# 始め直して撮る。Game/Debug/MultiViewCapture.h）。その起動のログ・検証レイヤーのログは views.Game.log・views.Validation.log で、
# シーンの報告は視点ごとの <視点>.SceneReport.txt へ写す。実行中に視点を置き直せないシーン（Game が報告に capture_views=unsupported を
# 書く）と、視点が 1 つのとき・-GpuTimingFrames の計測は、視点ごとに起動する（--scene-view=<名前>。-Views を省いたときは、既定の視点の
# 起動の報告（views=）から残りの視点の起動を足す。ログは <視点>.Game.log・<視点>.Validation.log）。
# -LaunchPerView を付けると、2 つ以上の視点でも視点ごとに起動する（前の視点のジオメトリのページの常駐を持ち越さない。
# 荒野の --wasteland-finest-lod のように、視点を続けて撮ると常駐が置き場の上限に当たって追い出しと読み直しが続く撮影に使う）。
# どちらで撮ったかは metrics.json の capture_mode（multi_view・per_view）と、視点ごとの capture_run（起動の名前）に入る。
# -Width と -Height は両方を与えると --window-size=<幅>x<高さ> で窓とバックバッファの大きさを替える。
# -GameArgs は Game へ足す引数を、先頭の -- を除いてカンマ区切りで与える（例: -GameArgs night,sun-elevation=10）。
#   値にカンマを含む引数（startup-camera=0,5,2.5）は、カンマの次が英字で始まらない限り分けない。
# -RequireAssets を付けると、資産の欠けを失敗にする。付けないときは欠けを metrics.json の missing_assets に数えて警告として出す。
#   欠けの検出は2つ。シーンの定義の必要な資産は Game が --scene-report で書く報告（<視点>.SceneReport.txt）から読むので、
#   ログを書かない Release でも検査できる。Rendering3DTest が読み込み失敗で出す印（COOKED_MODEL_MISSING など）は
#   Game.log からだけ読むので、Debug・RelWithDebInfo で効く。
# -GpuTimingFrames を与えると、決定的な撮影ではなく実時間の計測にする。Game を --trace-file 付きで起動し、有効な標本を
# -GpuTimingFrames 件だけ得る。計測の窓は、シーンの初期配置と公開の完了（SceneReady。ログの SCENE_READY_FRAME のフレーム番号）
# より後の描画フレームだけで、先頭のウォームアップ（-TimingWarmupFrames 件。既定 60。件数で数える）と、末尾の撮影の直前2件を
# それぞれ除く。SceneReady の記録（SCENE_READY・SCENE_READY_FRAME）がログに無ければ、計測せず失敗にする。
# Game は (-GpuTimingFrames + ウォームアップ + 末尾の2 + -TimingReadyMarginFrames) の描画フレームの後に撮って終わる
# （--exit-after-rendered-frames）。余白 -TimingReadyMarginFrames は SceneReady までに要するフレーム数以上にする
# （足りないと窓が SceneReady より前に入るので、標本が足りないとして失敗にし、要るフレーム数を示す。重いシーンは大きくする）。
# トレースの FrameGPU（GPU のタイムスタンプで測ったフレームの区間）の窓の中央値・平均・95 パーセンタイル・最大と、パスごとの
# 中央値を gpu_timing として metrics.json へ書く。視点ごとに、シーンが出した配置の数（placed_count。MEGA_STRESS_PLACED・
# MEGA_BANANAS_PLACED の行）、SceneReady のフレーム（scene_ready_frame）、計測の開始のフレーム（timing_start_frame。
# ウォームアップを除いた最初の有効な描画フレーム）も書く。
# 置いた Entity の数（placed_entities。MEGA_STRESS_PLACED の entities）とバナナの本数（placed_bananas。MEGA_STRESS_PLACED の
# bananas か MEGA_BANANAS_PLACED の count）も分けて書き、-RequireAssets のときは -GameArgs の stress-entities・banana-count と
# 違えば失敗にする（引数どおりに置けたことの確認。mega-stress のバナナは banana-count を指定したときだけ置く）。
# 同じ撮影のログから、描いたクラスタ数（MEGA_OCCLUSION の pass1 + pass2_drawn の最後の行）・MegaGeometry のバッファの大きさ
# （MEGA_CAPACITY）・ジオメトリのプールの使用量（VRAM_POOLS の geometry_used_mb）を mega_stats として書く。
# 統計が無効な Release は GPU のタイムスタンプを取れないので併用しない（RelWithDebInfo で測る）。
# gpu_timing には CPU 側も入る。ゲームスレッド・描画スレッドのフレーム時間（game_thread_ms_* / render_thread_ms_*。median と p95）、
# パスごとの median・p95・max（pass_ms。同じ内容を gpu_pass_ms にも書く）、CPU のスコープ（トレースの Type=Scope の行）は全部の区間を
# 名前ごとに集計して cpu_scope_ms へ（その区間が出たフレームだけで数えたフレームあたりの合計（全スレッド）の median・p95・max、
# 区間が出たスレッドの数 threads とその名前 thread（GameThread・RenderThread・Worker））、
# 名前の先頭 2 つの部分ごとの合計（Ecs.Stage.*・Ecs.Sys.*・RG.Record.* など）を cpu_scope_group_ms へ書く。
# CPU 側も GPU と同じ手順（SceneReady の後の描画フレームからウォームアップと末尾を除いた最後の -GpuTimingFrames 件）で数える。
# 描画スレッドの時間だけは、その窓の最初〜最後のフレーム番号の範囲で RenderThreadMs > 0 の Frame 行から独立に集める。
#
# -RequireChecks <名前,...> を与えると、Game を --engine-checks=<名前,...> 付きで起動し（開発ビルドの検査。Debug/EngineChecks.h）、
# Game.log の ENGINE_CHECK の行で各名前の pass が 1 回以上あり fail が 0 であることを見る（満たさなければ失敗）。
# 名前を与えていなくても、ENGINE_CHECK の fail の行が 1 件でもあれば失敗にする。Release はログが無いので併用できない。
# -Build を付けると、撮る構成（-Configuration）の Game を先にビルドし、失敗したら撮らずに失敗にする（古い exe で撮らないため）。
# -Hdr を付けると、Game を --capture-hdr 付きで起動し、視点ごとに PNG と同じフレームのブルームの前のシーンの色を
# 放射輝度の HDR として <視点>.hdr.pfm（線形の Rec.709・D65、cd/m²。エンジンのプリエクスポージャを外した RGB の 32 bit 浮動小数）と
# <視点>.hdr.json（大きさ・プリエクスポージャ・表示変換の差し替え）に書く（Core の Rendering/FrameCaptureHdrWriter.h）。
# 撮り終えたら Scripts/Terrain/ReferenceCheck.py --record-inputs で、撮影の入力（描画のコード・シェーダー・荒野の組み立て・
# git のコミット）と撮影に使った Game.exe の中身のハッシュを inputs.json へ書く（ReferenceCheck.py --hdr --verify が今のファイルと
# 照らして古い撮影を拒否する。--blur-static は組ごとの Game.exe のハッシュが揃うことも求める）。
# 開発ビルドの機能なので Release と、決定的でない -GpuTimingFrames とは併用できない。HDR が無い・大きさが PNG と違えば失敗にする。
# -Aov を付けると、Game を --capture-aov 付きで起動し、視点ごとに PNG と同じフレームの GBuffer の生の値を浮動小数の画像に書く
# （<視点>.aov.albedo.pfm〔線形のアルベド〕・.aov.normal.pfm〔ワールドの法線、+Y が上〕・.aov.roughness.pfm・.aov.depth.pfm
# 〔カメラの前方向への距離 m、空は +∞〕と、カメラを書いた <視点>.aov.json。Core の Rendering/FrameCaptureHdrWriter.h）。
# 値の検査は ReferenceCheck.py --aov。-Hdr と同じく開発ビルドの機能で、撮り終えたら入力のハッシュ（inputs.json）を書く。
# -GameArgs に atmosphere-dump を含めると、Game は視点ごとに PNG と同じフレームの空と霞の値（空の LUT・前計算の表・決めた視線の
# 空気遠近と高さフォグ・地表の照度）を <視点>.atmos.json と <視点>.atmos.*.f32 に書く（Core の Rendering/FrameCaptureHdrWriter.h）。
# Game の --capture-views の撮影だけが書くので、視点が 1 つでも 1 回の起動で撮る形（--capture-views）にする。値の検査は
# ReferenceCheck.py --atmos-dump。-Hdr と同じく開発ビルドの機能で、撮り終えたら入力のハッシュ（inputs.json）を書く。
#
# -CompareWith <dir> を与えると、その撮影（同じ視点名の PNG）と視点ごとに比べる。平均輝度の差が -CompareMeanLuminanceLimit 以下、
# PSNR が -ComparePsnrLimit 以上なら一致（式は CaptureStartupScene.ps1 の -CompareDeterministicWith と同じ）。
# 比べる側にも gpu_timing があれば、metrics.json の comparison に GPU・ゲームスレッド・描画スレッドの median の比も書く。
#
# 失敗にするもの（終了コード 1）: Game の起動失敗・終了コードが0でない・タイムアウト（1 回の起動で複数の視点を撮るときは
# -TimeoutSeconds の 2 倍）、PNG が無い（その起動より前の古い PNG を含む）・大きさが違う、視点の切り替えの失敗（CAPTURE_VIEWS failed）、
# Game.log のシェーダーのコンパイル失敗（Slang SDK 未設定の neural_material_decode.slang だけは既知として除外）、
# 未知のシーン・視点の指定、カメラの台本の読み込み・撮影の段取りの失敗（CAMERA_SCRIPT_INVALID・CAMERA_SCRIPT_CAPTURE failed・
# unavailable）、シーンの報告が無い、SCENE_READY が無い、検証レイヤーのエラー（Debug と、-GameArgs に validation-layer を含む
# RelWithDebInfo。検証レイヤーのログが無い・Game.log に VALIDATION_LAYER enabled が無いときも失敗）。
# -Configuration Release はログが無効で Game.log を書かないため、ログの検査（シェーダーのコンパイル失敗・SCENE_READY・
# Rendering3DTest の読み込み失敗の印）を飛ばす。未知のシーン・視点と必要な資産の欠けは報告から検査するので Release でも効く。
# 成功すると CAPTURE_SCENE result=pass を出す。
param(
    [Parameter(Mandatory = $true)]
    [string]$Scene,
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    # 視点の名前（カンマ区切りで1つの文字列でも、配列でもよい）
    [string[]]$Views = @(),
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [ValidateRange(0, 7680)]
    [int]$Width = 0,
    [ValidateRange(0, 4320)]
    [int]$Height = 0,
    [string[]]$GameArgs = @(),
    [switch]$RequireAssets,
    # 開発ビルドの検査の名前（カンマ区切りで1つの文字列でも、配列でもよい）。pass が 1 回以上・fail が 0 でなければ失敗にする。
    [string[]]$RequireChecks = @(),
    # 撮る前に、-Configuration の Game をビルドする（失敗したら撮らずに失敗）。
    [switch]$Build,
    # 視点ごとに、ブルームの前の HDR（<視点>.hdr.pfm・<視点>.hdr.json）も PNG と同じフレームから書く（開発ビルドだけ）。
    [switch]$Hdr,
    # 視点ごとに、GBuffer の生の値（<視点>.aov.*.pfm・<視点>.aov.json）も PNG と同じフレームから書く（開発ビルドだけ）。
    [switch]$Aov,
    # 2 つ以上の視点でも、視点ごとに Game を起動して撮る（1 回の起動にまとめない）。
    [switch]$LaunchPerView,
    # 1 回の起動の時間切れ[秒]。荒野は Debug で地形の区画と崖錐の岩を置き終えて SceneReady になるまで約 300 秒かかるので、
    # 1 視点の撮影でも収まるよう既定を 600 秒にする
    [ValidateRange(10, 3600)]
    [int]$TimeoutSeconds = 600,
    # GPU のフレーム時間を測るときの、落ち着いてから撮るまでの描画フレーム数（0 で測らず、決定的な撮影にする）。
    [ValidateRange(0, 100000)]
    [int]$GpuTimingFrames = 0,
    # 計測の窓の前に捨てる、SceneReady の後のフレーム数。
    [ValidateRange(0, 100000)]
    [int]$TimingWarmupFrames = 60,
    # SceneReady までに要するフレーム数の見込み（Game が終わるまでのフレーム数に足す余白）。
    [ValidateRange(0, 1000000)]
    [int]$TimingReadyMarginFrames = 240,
    # GPU のフレーム時間の予算（ms）。超えても失敗にはせず、metrics.json に within_budget として書く。
    [ValidateRange(0.1, 1000.0)]
    [double]$GpuFrameBudgetMs = 16.6,
    # 比べる別の撮影の出力先（metrics.json と <視点>.png がある）。空なら比べない。
    [string]$CompareWith = '',
    # -CompareWith の判定: 平均輝度の差の上限と PSNR（dB）の下限。
    [ValidateRange(0.0, 255.0)]
    [double]$CompareMeanLuminanceLimit = 0.1,
    [ValidateRange(0.0, 100.0)]
    [double]$ComparePsnrLimit = 45.0
)

$ErrorActionPreference = 'Stop'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

if ($Scene -notmatch '^[a-z0-9][a-z0-9-]*$')
{
    Write-Output "CAPTURE_SCENE result=fail reason=invalid_scene value=$Scene"
    exit 1
}
if (($Width -gt 0) -ne ($Height -gt 0))
{
    Write-Output 'CAPTURE_SCENE result=fail reason=width_height_must_be_given_together'
    exit 1
}
if ($Width -gt 0 -and ($Width -lt 64 -or $Height -lt 64))
{
    Write-Output 'CAPTURE_SCENE result=fail reason=window_size_too_small min=64'
    exit 1
}

if ($GpuTimingFrames -gt 0 -and $GpuTimingFrames -lt 100)
{
    Write-Output 'CAPTURE_SCENE result=fail reason=gpu_timing_frames_too_few（-GpuTimingFrames は100以上）'
    exit 1
}
if ($GpuTimingFrames -gt 0 -and $Configuration -eq 'Release')
{
    Write-Output 'CAPTURE_SCENE result=fail reason=gpu_timing_without_stats（Release は統計が無効で GPU のタイムスタンプを取れない。-Configuration RelWithDebInfo で測る）'
    exit 1
}

if ($Hdr -and $Configuration -eq 'Release')
{
    Write-Output 'CAPTURE_SCENE result=fail reason=hdr_in_release（-Hdr は開発ビルドの機能で、Release の Game は HDR を書かない。RelWithDebInfo か Debug で撮る）'
    exit 1
}
if ($Hdr -and $GpuTimingFrames -gt 0)
{
    Write-Output 'CAPTURE_SCENE result=fail reason=hdr_with_gpu_timing（-Hdr は決定的な撮影と併せて使う）'
    exit 1
}
if ($Aov -and $Configuration -eq 'Release')
{
    Write-Output 'CAPTURE_SCENE result=fail reason=aov_in_release（-Aov は開発ビルドの機能で、Release の Game は AOV を書かない。RelWithDebInfo か Debug で撮る）'
    exit 1
}
if ($Aov -and $GpuTimingFrames -gt 0)
{
    Write-Output 'CAPTURE_SCENE result=fail reason=aov_with_gpu_timing（-Aov は決定的な撮影と併せて使う）'
    exit 1
}
# AOV の画像の名前（<視点> の後ろ）
$aovSuffixes = @('.aov.albedo.pfm', '.aov.normal.pfm', '.aov.roughness.pfm', '.aov.depth.pfm', '.aov.json')
# 大気の書き出しのファイルの名前（<視点> の後ろ）
$atmosphereSuffixes = @('.atmos.json', '.atmos.probe.f32', '.atmos.transmittance_lut.f32', '.atmos.radiance_lut.f32',
    '.atmos.transmittance_table.f32', '.atmos.multiple_scattering.f32', '.atmos.sky_view.f32')

# 検査の名前: 「A,B」の1文字列でも配列でも受ける。
$requiredChecks = @($RequireChecks | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' } | Select-Object -Unique)
foreach ($checkName in $requiredChecks)
{
    if ($checkName -notmatch '^[A-Z][A-Z0-9_]*$')
    {
        Write-Output "CAPTURE_SCENE result=fail reason=invalid_check_name value=$checkName"
        exit 1
    }
}
if ($requiredChecks.Count -gt 0 -and $Configuration -eq 'Release')
{
    Write-Output 'CAPTURE_SCENE result=fail reason=checks_without_log（Release はログが無く、検査の結果を照合できない。Debug か RelWithDebInfo を使う）'
    exit 1
}

# 視点: 「a,b」の1文字列でも配列でも受ける。空なら既定の視点の1枚。
$viewNames = @($Views | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
foreach ($viewName in $viewNames)
{
    if ($viewName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$')
    {
        Write-Output "CAPTURE_SCENE result=fail reason=invalid_view value=$viewName"
        exit 1
    }
}
if (@($viewNames | Select-Object -Unique).Count -ne $viewNames.Count)
{
    Write-Output 'CAPTURE_SCENE result=fail reason=duplicate_view'
    exit 1
}
$shots = New-Object System.Collections.ArrayList
# -Views を省いたときは、撮った後の報告（views=・capture_views=）で残りの視点を足す
$expandViews = ($viewNames.Count -eq 0)
# 決定的な撮影で、-Views を省いたとき・2 つ以上の視点を与えたときは、1 回の起動で撮る（--capture-views）
$useMultiView = ($GpuTimingFrames -eq 0) -and ($expandViews -or $viewNames.Count -ge 2) -and -not $LaunchPerView
if ($expandViews)
{
    [void]$shots.Add([pscustomobject]@{ Name = 'default'; SceneView = ''; DefaultViewName = '' })
}
else
{
    foreach ($viewName in $viewNames)
    {
        [void]$shots.Add([pscustomobject]@{ Name = $viewName; SceneView = $viewName; DefaultViewName = '' })
    }
}

# Game へ足す引数: カンマの次が「英字で始まり、= かカンマか末尾が続く」ところだけで分け、先頭の -- を付ける。
$extraArguments = @()
foreach ($gameArgument in $GameArgs)
{
    foreach ($part in ($gameArgument -split ',(?=[A-Za-z][A-Za-z0-9-]*(?:=|,|$))'))
    {
        $trimmed = $part.Trim().TrimStart('-')
        if ($trimmed -ne '')
        {
            $extraArguments += "--$trimmed"
        }
    }
}
# 検証レイヤー（--validation-layer）は開発ビルドの Game だけが受け付ける。Debug は既定で入る
$validationLayerArgument = ($extraArguments -contains '--validation-layer')
if ($validationLayerArgument -and $Configuration -eq 'Release')
{
    Write-Output 'CAPTURE_SCENE result=fail reason=validation_layer_in_release（validation-layer は開発ビルドの機能で、Release の Game は受け付けない。RelWithDebInfo か Debug で撮る）'
    exit 1
}
$validationLayerExpected = ($Configuration -eq 'Debug' -or $validationLayerArgument)
# 大気の書き出し（--atmosphere-dump）は開発ビルドの Game の --capture-views の撮影だけが書く
$atmosphereDump = ($extraArguments -contains '--atmosphere-dump')
if ($atmosphereDump -and $Configuration -eq 'Release')
{
    Write-Output 'CAPTURE_SCENE result=fail reason=atmosphere_dump_in_release（atmosphere-dump は開発ビルドの機能で、Release の Game は書かない。RelWithDebInfo か Debug で撮る）'
    exit 1
}
if ($atmosphereDump -and $GpuTimingFrames -gt 0)
{
    Write-Output 'CAPTURE_SCENE result=fail reason=atmosphere_dump_with_gpu_timing（atmosphere-dump は決定的な撮影と併せて使う）'
    exit 1
}
if ($atmosphereDump)
{
    $useMultiView = $true
}
# カメラの台本の撮影の段取り（home・epoch・capture・wait-ready。Game/Input/CameraScript.h）も、Game の --capture-views の撮影だけが
# 受けるので、視点が 1 つでも 1 回の起動で撮る形にする
foreach ($argument in $extraArguments)
{
    if ($argument -match '^--camera-script=(.+)$')
    {
        $scriptPath = $Matches[1].Trim('"')
        if (-not [System.IO.Path]::IsPathRooted($scriptPath)) { $scriptPath = Join-Path (Split-Path -Parent $PSScriptRoot) $scriptPath }
        if ((Test-Path -LiteralPath $scriptPath) -and
            (Select-String -LiteralPath $scriptPath -Pattern '^\s*\d+\s[^#]*\b(capture=|epoch\b|wait-ready\b|home\b)' -Quiet))
        {
            $useMultiView = $true
        }
    }
}

# 置く数の指定（-RequireAssets のとき、置いた数と突き合わせる）。指定が無ければ $null
$requestedEntities = $null
$requestedBananas = $null
foreach ($argument in $extraArguments)
{
    if ($argument -match '^--stress-entities=(\d+)$') { $requestedEntities = [long]$Matches[1] }
    if ($argument -match '^--banana-count=(\d+)$') { $requestedBananas = [long]$Matches[1] }
}

if ($requiredChecks.Count -gt 0)
{
    $extraArguments += "--engine-checks=$($requiredChecks -join ',')"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$gamePath = Join-Path $repoRoot "build\Game\$Configuration\Game.exe"
$outRoot = if ([IO.Path]::IsPathRooted($OutDir)) { [IO.Path]::GetFullPath($OutDir) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir)) }

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class SceneCaptureMetrics
{
    public static double[] Measure(string path)
    {
        using (var bitmap = new Bitmap(path))
        {
            var rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            BitmapData data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int stride = data.Stride;
                byte[] bytes = new byte[stride * bitmap.Height];
                Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
                double luminanceSum = 0.0;
                long white = 0;
                long black = 0;
                for (int y = 0; y < bitmap.Height; ++y)
                {
                    int row = y * stride;
                    for (int x = 0; x < bitmap.Width; ++x)
                    {
                        int i = row + x * 4;
                        byte b = bytes[i];
                        byte g = bytes[i + 1];
                        byte r = bytes[i + 2];
                        luminanceSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
                        if (r == 255 && g == 255 && b == 255) { ++white; }
                        if (r == 0 && g == 0 && b == 0) { ++black; }
                    }
                }
                double count = (double)bitmap.Width * bitmap.Height;
                return new double[] { bitmap.Width, bitmap.Height, luminanceSum / count, white / count, black / count };
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
        }
    }

    // 同じ大きさの2枚の画像の違い。{ 平均輝度A, 平均輝度B, PSNR（dB。R・G・B の 8bit。同一なら 100）,
    // 一致しない画素の割合, 1チャンネルの最大の絶対差 }（CaptureStartupScene.ps1 の StartupCaptureMetrics.Compare と同じ式）。
    public static double[] Compare(string pathA, string pathB)
    {
        using (var bitmapA = new Bitmap(pathA))
        using (var bitmapB = new Bitmap(pathB))
        {
            if (bitmapA.Width != bitmapB.Width || bitmapA.Height != bitmapB.Height)
            {
                throw new ArgumentException("画像の寸法が揃っていない: " + pathA + " / " + pathB);
            }
            int width = bitmapA.Width;
            int height = bitmapA.Height;
            var rect = new Rectangle(0, 0, width, height);
            BitmapData dataA = bitmapA.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            BitmapData dataB = bitmapB.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                byte[] bytesA = new byte[dataA.Stride * height];
                byte[] bytesB = new byte[dataB.Stride * height];
                Marshal.Copy(dataA.Scan0, bytesA, 0, bytesA.Length);
                Marshal.Copy(dataB.Scan0, bytesB, 0, bytesB.Length);
                double sumA = 0.0;
                double sumB = 0.0;
                double squaredError = 0.0;
                long mismatched = 0;
                int maxDifference = 0;
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        int ia = y * dataA.Stride + x * 4;
                        int ib = y * dataB.Stride + x * 4;
                        bool differs = false;
                        for (int c = 0; c < 3; ++c)
                        {
                            int d = bytesA[ia + c] - bytesB[ib + c];
                            if (d != 0)
                            {
                                differs = true;
                                squaredError += (double)d * d;
                                int magnitude = d < 0 ? -d : d;
                                if (magnitude > maxDifference) { maxDifference = magnitude; }
                            }
                        }
                        if (differs) { ++mismatched; }
                        sumA += 0.2126 * bytesA[ia + 2] + 0.7152 * bytesA[ia + 1] + 0.0722 * bytesA[ia];
                        sumB += 0.2126 * bytesB[ib + 2] + 0.7152 * bytesB[ib + 1] + 0.0722 * bytesB[ib];
                    }
                }
                double count = (double)width * height;
                double meanSquaredError = squaredError / (count * 3.0);
                double psnr = meanSquaredError <= 0.0 ? 100.0 : Math.Min(100.0, 10.0 * Math.Log10(255.0 * 255.0 / meanSquaredError));
                return new double[] { sumA / count, sumB / count, psnr, mismatched / count, maxDifference };
            }
            finally
            {
                bitmapA.UnlockBits(dataA);
                bitmapB.UnlockBits(dataB);
            }
        }
    }
}
'@

function Stop-OwnedProcessTree([int]$ProcessId)
{
    & taskkill.exe /PID $ProcessId /T /F 2>&1 | Out-Null
}

# 外から止められた（ランナーの再検証の時間切れ・taskkill）powershell が Game を残さないよう、Game を起動する前に
# この powershell を Job Object（JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE）へ入れる。子は生成の時点から同じ Job に入るので、
# 起動から登録までの隙間が無い。ハンドルは継承させずこのプロセスだけが持つので、powershell が終わるとカーネルが
# ハンドルを閉じ、中の Game も終わる。自前の時間切れは Stop-OwnedProcessTree のまま。
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

public static class CaptureProcessJob
{
    [StructLayout(LayoutKind.Sequential)]
    struct JOBOBJECT_BASIC_LIMIT_INFORMATION
    {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct IO_COUNTERS
    {
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    const int JobObjectExtendedLimitInformation = 9;
    const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateJobObject(IntPtr attributes, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, uint length);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);

    // この powershell を入れた後は、powershell が終わるまで閉じない（閉じた時点で中のプロセスが終わるため）
    static IntPtr job = IntPtr.Zero;

    // この powershell を Job へ入れる（2 回目以降は何もしない）。入れられなければ例外を投げる
    public static void EnterCurrentProcess()
    {
        if (job != IntPtr.Zero) { return; }
        IntPtr created = CreateJobObject(IntPtr.Zero, null);
        if (created == IntPtr.Zero) { throw new Win32Exception(); }
        var info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, ref info, (uint)Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION))))
        {
            throw new Win32Exception();
        }
        if (!AssignProcessToJobObject(created, Process.GetCurrentProcess().Handle)) { throw new Win32Exception(); }
        job = created;
    }

    // 起動した子がこの Job に入っているか（確かめられないときも false）
    public static bool Contains(IntPtr processHandle)
    {
        bool inJob;
        if (job == IntPtr.Zero || !IsProcessInJob(processHandle, job, out inJob)) { return false; }
        return inJob;
    }
}
'@

# 昇順に並べた標本の百分位（0〜1）。標本の添字は floor((n-1)*p)（GPU のフレーム時間の既存の求め方と同じ）。
function Get-Percentile([double[]]$Sorted, [double]$Fraction)
{
    if ($Sorted.Count -eq 0) { return $null }
    return $Sorted[[int][math]::Floor(($Sorted.Count - 1) * $Fraction)]
}

# トレース（--trace-file の CSV）から GPU・CPU のフレーム時間を集計する。Type=GPU の行は描画したフレームごとの FrameGPU と
# パスごとの区間（Frame 列は区間を記録したフレームの番号）。窓は、SceneReady のフレーム（ReadyFrame）以降の描画した
# 有効なフレームから、先頭の WarmupFrames 個（ウォームアップ）を件数で除き、さらに撮影のフレームの直前 TailFrames 個を
# 除いた残りの最後の Samples 件（ウォームアップと末尾の除外は別々に取る）。
# CPU 側（ゲームスレッド・スコープ）は、描画したフレームの行（RenderFrameMs > 0）で同じ手順の窓を取る。
# 描画スレッドの時間（RenderThreadMs）は RenderFrameMs と同じ行に載るとは限らない（ゲームスレッドが描画 1 フレームの間に
# 何百回も回ると別の行に出る）ので、その窓の最初〜最後のフレーム番号の範囲の Frame 行のうち RenderThreadMs > 0 の行から集める。
# 有効な標本（FrameGPU > 0 のフレーム）が除外の後に Samples 件に満たなければ失敗（Error）にする。
function Get-GpuTimingFromTrace([string]$TracePath, [int]$Samples, [double]$BudgetMs, [long]$ReadyFrame, [int]$WarmupFrames, [int]$TailFrames)
{
    $header = @((Get-Content -LiteralPath $TracePath -TotalCount 1) -split ',')
    $nameColumn = [array]::IndexOf($header, 'Name')
    $durationColumn = [array]::IndexOf($header, 'DurationMs')
    $renderFrameColumn = [array]::IndexOf($header, 'RenderFrameMs')
    $cpuFrameColumn = [array]::IndexOf($header, 'CPUFrameMs')
    $gameThreadColumn = [array]::IndexOf($header, 'GameThreadMs')
    $renderThreadColumn = [array]::IndexOf($header, 'RenderThreadMs')
    $threadColumn = [array]::IndexOf($header, 'ThreadId')
    $frameRowThreads = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    $gpuRowThreads = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    $cpuRows = New-Object System.Collections.Generic.List[object]
    $renderThreadRows = New-Object System.Collections.Generic.List[object]
    $gpuScopesByFrame = New-Object 'System.Collections.Generic.SortedDictionary[long,object]'
    foreach ($line in [IO.File]::ReadLines($TracePath))
    {
        if ($line.StartsWith('Frame,'))
        {
            $fields = $line -split ','
            $frameRowThreads[$fields[$threadColumn]] = $frameRowThreads[$fields[$threadColumn]] + 1
            $renderThreadMs = [double]::Parse($fields[$renderThreadColumn], $invariant)
            if ($renderThreadMs -gt 0.0) { $renderThreadRows.Add([pscustomobject]@{ Frame = [long]$fields[1]; RenderThread = $renderThreadMs }) }
            if ([double]::Parse($fields[$renderFrameColumn], $invariant) -le 0.0) { continue }
            $cpuRows.Add([pscustomobject]@{
                Frame = [long]$fields[1]
                Cpu = [double]::Parse($fields[$cpuFrameColumn], $invariant)
                GameThread = [double]::Parse($fields[$gameThreadColumn], $invariant)
            })
        }
        elseif ($line.StartsWith('GPU,'))
        {
            $fields = $line -split ','
            $gpuRowThreads[$fields[$threadColumn]] = $gpuRowThreads[$fields[$threadColumn]] + 1
            $frameNumber = [long]$fields[1]
            if (-not $gpuScopesByFrame.ContainsKey($frameNumber))
            {
                $gpuScopesByFrame[$frameNumber] = New-Object 'System.Collections.Generic.List[object]'
            }
            $gpuScopesByFrame[$frameNumber].Add([pscustomobject]@{
                Name = $fields[$nameColumn].Trim('"')
                Ms = [double]::Parse($fields[$durationColumn], $invariant)
            })
        }
    }
    $windowCount = $Samples
    $cpuReady = @($cpuRows.ToArray() | Where-Object { $_.Frame -ge $ReadyFrame })
    $cpuArray = if ($cpuReady.Count -gt $WarmupFrames) { @($cpuReady[$WarmupFrames..($cpuReady.Count - 1)]) } else { @() }
    $cpuUsable = if ($cpuArray.Count -gt $TailFrames) { @($cpuArray[0..($cpuArray.Count - $TailFrames - 1)]) } else { @() }
    $cpuWindow = if ($cpuUsable.Count -gt $windowCount) { @($cpuUsable[($cpuUsable.Count - $windowCount)..($cpuUsable.Count - 1)]) } else { $cpuUsable }
    $cpuSamples = [double[]]@($cpuWindow | ForEach-Object { $_.Cpu } | Where-Object { $_ -gt 0.0 } | Sort-Object)
    $gameThreadSamples = [double[]]@($cpuWindow | ForEach-Object { $_.GameThread } | Where-Object { $_ -gt 0.0 } | Sort-Object)
    $renderThreadFirst = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[0].Frame } else { [long]::MaxValue }
    $renderThreadLast = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[$cpuWindow.Count - 1].Frame } else { [long]::MinValue }
    $renderThreadSamples = [double[]]@($renderThreadRows.ToArray() | Where-Object { $_.Frame -ge $renderThreadFirst -and $_.Frame -le $renderThreadLast } | ForEach-Object { $_.RenderThread } | Sort-Object)
    $gpuReady = @($gpuScopesByFrame.Keys | Where-Object { $_ -ge $ReadyFrame -and @($gpuScopesByFrame[$_] | Where-Object { $_.Name -eq 'FrameGPU' -and $_.Ms -gt 0.0 }).Count -gt 0 })
    $gpuFrames = if ($gpuReady.Count -gt $WarmupFrames) { @($gpuReady[$WarmupFrames..($gpuReady.Count - 1)]) } else { @() }
    $gpuUsable = if ($gpuFrames.Count -gt $TailFrames) { @($gpuFrames[0..($gpuFrames.Count - $TailFrames - 1)]) } else { @() }
    $gpuWindow = if ($gpuUsable.Count -gt $windowCount) { @($gpuUsable[($gpuUsable.Count - $windowCount)..($gpuUsable.Count - 1)]) } else { $gpuUsable }
    $gpuFrameMs = @{}
    $passSamples = @{}
    foreach ($frameNumber in $gpuWindow)
    {
        foreach ($scope in $gpuScopesByFrame[$frameNumber])
        {
            if ($scope.Name -eq 'FrameGPU') { $gpuFrameMs[$frameNumber] = $scope.Ms; continue }
            if (-not $passSamples.ContainsKey($scope.Name)) { $passSamples[$scope.Name] = New-Object System.Collections.Generic.List[double] }
            $passSamples[$scope.Name].Add($scope.Ms)
        }
    }
    $gpuSamples = [double[]]@($gpuWindow | ForEach-Object { $gpuFrameMs[$_] } | Sort-Object)
    if ($gpuSamples.Count -lt $Samples)
    {
        return [pscustomobject]@{ Error = "GPU のフレーム時間の有効な標本が足りない（SceneReady（フレーム $ReadyFrame）の後の有効な描画フレーム $($gpuReady.Count) 件からウォームアップ $WarmupFrames 件と末尾 $TailFrames 件を除き、$($gpuSamples.Count) 件 / 指定 $Samples 件。統計が無効な構成か、GPU のタイムスタンプが使えないか、SceneReady までに要したフレームが -TimingReadyMarginFrames を超えた。後者なら大きくして撮り直す）"; Timing = $null }
    }
    $maximum = $gpuSamples[$gpuSamples.Count - 1]
    $passList = @($passSamples.Keys | ForEach-Object {
        $sortedPass = [double[]]@($passSamples[$_] | Sort-Object)
        [pscustomobject][ordered]@{
            pass = $_
            median_ms = [math]::Round((Get-Percentile $sortedPass 0.5), 3)
            p95_ms = [math]::Round((Get-Percentile $sortedPass 0.95), 3)
            max_ms = [math]::Round($sortedPass[$sortedPass.Count - 1], 3)
        }
    } | Sort-Object -Property median_ms -Descending)
    $passMedianList = @($passList | ForEach-Object { [pscustomobject][ordered]@{ pass = $_.pass; median_ms = $_.median_ms } })

    # CPU のスコープ（Type=Scope の行）: 窓の最初〜最後のフレーム番号の範囲に記録された全部の区間を、名前ごとに集計する。
    # Scope 行の Frame は記録時点のゲームフレーム番号で、描画を完了したフレームの行（窓）の番号とは一致しないため、
    # 窓の行の番号に所属するかではなく連続した範囲で選ぶ。
    # フレームあたりの値 = そのフレームに記録された同じ名前の区間の継続時間の合計（全スレッド・並列の仕事は足し合わせる）。
    # median・p95・max はその名前が出たフレーム（frames_present）だけで求める。ゲームスレッドが描画 1 フレームの間に何回も回ると
    # 描画スレッドの区間は範囲の一部のフレームにしか出ないので、出なかったフレームを 0 として数えると median が 0 になるため。
    # threads = その名前の区間が出たスレッド（ThreadId）の数、thread = その名前（GameThread・RenderThread・Worker を , で連ねる）、
    # frames_present = 出たフレームの数、samples = 区間の記録の件数。
    # スレッドの名前は、Frame の行を書くスレッドを GameThread、GPU の行を書くスレッドを RenderThread とし（それぞれ最も多い ThreadId）、
    # それ以外を Worker とする。
    # 名前の先頭 2 つの部分（A.B）が同じで 3 つ以上の部分を持つ名前は、A.B.* としてフレームごとに合計した値も書く。
    $scopeFrameFirst = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[0].Frame } else { [long]::MaxValue }
    $scopeFrameLast = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[$cpuWindow.Count - 1].Frame } else { [long]::MinValue }
    $scopeFrameCount = if ($cpuWindow.Count -gt 0) { [int]($scopeFrameLast - $scopeFrameFirst + 1) } else { 0 }
    $gameThreadId = if ($frameRowThreads.Count -gt 0) { @($frameRowThreads.Keys | Sort-Object { $frameRowThreads[$_] } -Descending)[0] } else { $null }
    $renderThreadId = if ($gpuRowThreads.Count -gt 0) { @($gpuRowThreads.Keys | Sort-Object { $gpuRowThreads[$_] } -Descending)[0] } else { $null }
    $scopePerFrame =New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.Dictionary[long,double]]'
    $scopeThreads = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.HashSet[string]]'
    $scopeRecords = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    $groupPerFrame = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.Dictionary[long,double]]'
    $groupThreads = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.HashSet[string]]'
    $groupRecords = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    foreach ($line in [IO.File]::ReadLines($TracePath))
    {
        if (-not $line.StartsWith('Scope,')) { continue }
        $frameEnd = $line.IndexOf(',', 6)
        $scopeFrame = [long]$line.Substring(6, $frameEnd - 6)
        if ($scopeFrame -lt $scopeFrameFirst -or $scopeFrame -gt $scopeFrameLast) { continue }
        $fields = $line.Split(',', 8)
        $scopeName = $fields[$nameColumn].Trim('"')
        $scopeMs = [double]::Parse($fields[$durationColumn], $invariant)
        $threadId = $fields[$threadColumn]
        $targets = @(, @($scopeName, $scopePerFrame, $scopeThreads, $scopeRecords))
        $nameParts = $scopeName.Split('.')
        if ($nameParts.Count -ge 3) { $targets += , @(($nameParts[0] + '.' + $nameParts[1] + '.*'), $groupPerFrame, $groupThreads, $groupRecords) }
        foreach ($target in $targets)
        {
            $key = $target[0]
            $perFrame = $target[1]
            if (-not $perFrame.ContainsKey($key))
            {
                $perFrame[$key] = New-Object 'System.Collections.Generic.Dictionary[long,double]'
                $target[2][$key] = New-Object 'System.Collections.Generic.HashSet[string]'
                $target[3][$key] = 0
            }
            $frameTotals = $perFrame[$key]
            $previous = 0.0
            $null = $frameTotals.TryGetValue($scopeFrame, [ref]$previous)
            $frameTotals[$scopeFrame] = $previous + $scopeMs
            $null = $target[2][$key].Add($threadId)
            $target[3][$key] = $target[3][$key] + 1
        }
    }
    function ConvertTo-ScopeStatList($PerFrame, $Threads, $Records, $GameThreadId, $RenderThreadId)
    {
        return @($PerFrame.Keys | ForEach-Object {
            $scopeName = $_
            $frameTotals = $PerFrame[$scopeName]
            $sortedScope = [double[]]@($frameTotals.Values | Sort-Object)
            $threadNames = @($Threads[$scopeName] | ForEach-Object {
                if ($_ -eq $GameThreadId) { 'GameThread' } elseif ($_ -eq $RenderThreadId) { 'RenderThread' } else { 'Worker' }
            } | Sort-Object { @('GameThread', 'RenderThread', 'Worker').IndexOf($_) } | Select-Object -Unique)
            [pscustomobject][ordered]@{
                scope = $scopeName
                median_ms = [math]::Round((Get-Percentile $sortedScope 0.5), 3)
                p95_ms = [math]::Round((Get-Percentile $sortedScope 0.95), 3)
                max_ms = [math]::Round($sortedScope[$sortedScope.Count - 1], 3)
                threads = $Threads[$scopeName].Count
                thread = $threadNames -join ','
                frames_present = $frameTotals.Count
                samples = $Records[$scopeName]
            }
        } | Sort-Object -Property @{ Expression = 'median_ms'; Descending = $true }, @{ Expression = 'max_ms'; Descending = $true }, @{ Expression = 'scope'; Descending = $false })
    }
    # 関数の戻りは 1 件だと配列でなくなり JSON がオブジェクトになるので、受け側で配列に包む（0 件・1 件でも配列を保つ）
    $cpuScopeList = @(ConvertTo-ScopeStatList $scopePerFrame $scopeThreads $scopeRecords $gameThreadId $renderThreadId)
    $cpuScopeGroupList = @(ConvertTo-ScopeStatList $groupPerFrame $groupThreads $groupRecords $gameThreadId $renderThreadId)

    $timing = [ordered]@{
        trace = [IO.Path]::GetFileName($TracePath)
        frames = $gpuSamples.Count
        valid_samples = $gpuSamples.Count
        requested_samples = $Samples
        scene_ready_frame = $ReadyFrame
        warmup_frames_removed = $WarmupFrames
        start_frame = [long]$gpuFrames[0]
        window_first_frame = [long]$gpuWindow[0]
        window_last_frame = [long]$gpuWindow[$gpuWindow.Count - 1]
        gpu_frame_ms_median = [math]::Round((Get-Percentile $gpuSamples 0.5), 3)
        gpu_frame_ms_mean = [math]::Round(($gpuSamples | Measure-Object -Average).Average, 3)
        gpu_frame_ms_p95 = [math]::Round((Get-Percentile $gpuSamples 0.95), 3)
        gpu_frame_ms_max = [math]::Round($maximum, 3)
        cpu_frame_ms_median = if ($cpuSamples.Count -gt 0) { [math]::Round((Get-Percentile $cpuSamples 0.5), 3) } else { $null }
        cpu_frame_ms_p95 = if ($cpuSamples.Count -gt 0) { [math]::Round((Get-Percentile $cpuSamples 0.95), 3) } else { $null }
        game_thread_ms_median = if ($gameThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $gameThreadSamples 0.5), 3) } else { $null }
        game_thread_ms_p95 = if ($gameThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $gameThreadSamples 0.95), 3) } else { $null }
        render_thread_ms_median = if ($renderThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $renderThreadSamples 0.5), 3) } else { $null }
        render_thread_ms_p95 = if ($renderThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $renderThreadSamples 0.95), 3) } else { $null }
        cpu_frames = $cpuSamples.Count
        budget_ms = $BudgetMs
        within_budget = ($maximum -le $BudgetMs)
        pass_median_ms = $passMedianList
        pass_ms = $passList
        gpu_pass_ms = $passList
        cpu_scope_frames = $scopeFrameCount
        cpu_scope_ms = $cpuScopeList
        cpu_scope_group_ms = $cpuScopeGroupList
    }
    return [pscustomobject]@{ Error = $null; Timing = [pscustomobject]$timing }
}

# ログの MegaGeometry の統計（描いたクラスタ数・バッファの大きさ・ジオメトリのプールの使用量）の最後の値を読む。
# 値が無い項目は $null。
function Get-MegaStatsFromLog([string]$LogPath)
{
    $stats = [ordered]@{
        clusters_drawn_last = $null
        clusters_drawn_max = $null
        draw_dropped_last = $null
        draw_dropped_max = $null
        visibility_bytes = $null
        indirect_bytes = $null
        draw_info_bytes = $null
        bvh_queue_bytes = $null
        sw_raster_bytes = $null
        geometry_used_mb = $null
        geometry_used_mb_max = $null
        vt_used_mb_max = $null
        shadow_map_pool_mb = $null
        geometry_pool_capacity_mb = $null
        sw_raster_list_mb = $null
        visbuffer_records_mb = $null
        visbuffer64_mb = $null
        vsm_pool_mb = $null
    }
    $number = { param($line, $key) if ($line -match "(?:^|\s)$key=(\d+(?:\.\d+)?)") { [double]$Matches[1] } else { $null } }
    foreach ($line in [IO.File]::ReadLines($LogPath))
    {
        if ($line.Contains('MEGA_OCCLUSION frame='))
        {
            $drawn = (& $number $line 'pass1') + (& $number $line 'pass2_drawn')
            $stats.clusters_drawn_last = $drawn
            if ($null -eq $stats.clusters_drawn_max -or $drawn -gt $stats.clusters_drawn_max) { $stats.clusters_drawn_max = $drawn }
        }
        elseif ($line.Contains('MEGA_CAPACITY '))
        {
            foreach ($key in @('visibility_bytes', 'indirect_bytes', 'draw_info_bytes', 'bvh_queue_bytes', 'sw_raster_bytes'))
            {
                $stats[$key] = & $number $line $key
            }
            $dropped = & $number $line 'draw_dropped'
            $stats.draw_dropped_last = $dropped
            if ($null -eq $stats.draw_dropped_max -or $dropped -gt $stats.draw_dropped_max) { $stats.draw_dropped_max = $dropped }
        }
        elseif ($line.Contains('VRAM_LEDGER geometry_pool '))
        {
            $stats.geometry_pool_capacity_mb = & $number $line 'capacity_mb'
        }
        elseif ($line.Contains('VRAM_LEDGER sw_raster_list '))
        {
            $stats.sw_raster_list_mb = & $number $line 'mb'
        }
        elseif ($line.Contains('VRAM_LEDGER visbuffer_records '))
        {
            $stats.visbuffer_records_mb = & $number $line 'mb'
        }
        elseif ($line.Contains('VRAM_LEDGER visbuffer64 '))
        {
            $stats.visbuffer64_mb = & $number $line 'mb'
        }
        elseif ($line.Contains('VRAM_LEDGER vsm_pool '))
        {
            $stats.vsm_pool_mb = & $number $line 'mb'
        }
        elseif ($line.Contains('VRAM_POOLS '))
        {
            $geometry = & $number $line 'geometry_used_mb'
            $stats.geometry_used_mb = $geometry
            if ($null -eq $stats.geometry_used_mb_max -or $geometry -gt $stats.geometry_used_mb_max) { $stats.geometry_used_mb_max = $geometry }
            $vt = & $number $line 'vt_used_mb'
            if ($null -eq $stats.vt_used_mb_max -or $vt -gt $stats.vt_used_mb_max) { $stats.vt_used_mb_max = $vt }
            $stats.shadow_map_pool_mb = & $number $line 'shadow_map_pool_mb'
        }
    }
    return [pscustomobject]$stats
}

# Game.log の ENGINE_CHECK の行を名前ごとに数える。pass/fail の数と、fail の行の本文を返す。
function Get-EngineCheckCounts([string]$LogPath)
{
    $counts = @{}
    $failLines = @()
    foreach ($line in [IO.File]::ReadLines($LogPath))
    {
        if ($line -notmatch 'ENGINE_CHECK name=([A-Z][A-Z0-9_]*) result=(pass|fail)')
        {
            continue
        }
        if (-not $counts.ContainsKey($Matches[1])) { $counts[$Matches[1]] = [pscustomobject]@{ pass = 0; fail = 0 } }
        if ($Matches[2] -eq 'pass') { $counts[$Matches[1]].pass += 1 } else { $counts[$Matches[1]].fail += 1; $failLines += $line.Trim() }
    }
    return [pscustomobject]@{ Counts = $counts; FailLines = $failLines }
}

# 撮る構成の Game をビルドする。成功なら $null、失敗なら理由を返す（出力は LogPath へ）。
function Invoke-GameBuild([string]$RepoRoot, [string]$Config, [string]$LogPath)
{
    $buildDirectory = Join-Path $RepoRoot 'build'
    if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt')))
    {
        return "build が構成されていない（$buildDirectory に CMakeCache.txt が無い。cmake -B build -S . を先に実行する）"
    }
    $errorLogPath = "$LogPath.err"
    Remove-Item -LiteralPath $LogPath, $errorLogPath -Force -ErrorAction SilentlyContinue
    $buildProcess = Start-Process -FilePath 'cmake.exe' -ArgumentList @('--build', "`"$buildDirectory`"", '--config', $Config, '--target', 'Game', '--', '/m:1') `
        -WorkingDirectory $RepoRoot -NoNewWindow -PassThru -RedirectStandardOutput $LogPath -RedirectStandardError $errorLogPath
    # -Wait は子孫まで待ち、ビルド後に残る vctip.exe で止まるので cmake 本体の終了だけを待つ。
    # Handle を先に取らないと終了後に ExitCode が空になる。
    $null = $buildProcess.Handle
    $buildProcess.WaitForExit()
    if ($buildProcess.ExitCode -ne 0)
    {
        $tail = @(Get-Content -LiteralPath $LogPath -Tail 8 -ErrorAction SilentlyContinue) -join ' | '
        return "Game のビルド（$Config）が失敗（終了コード $($buildProcess.ExitCode)。ログ: $LogPath。末尾: $tail）"
    }
    return $null
}

# 資産の欠けを表す印（シーンの定義の SCENE_ASSET_MISSING と、Rendering3DTest が出す読み込み失敗の印）
$missingAssetPattern = 'SCENE_ASSET_MISSING|COOKED_MODEL_MISSING|COOKED_MODEL_TEXTURES_MISSING|COOKED_BIG_SPHERE_MISSING|SCAN_PROP_MISSING'

New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
if ($Build)
{
    $buildFailure = Invoke-GameBuild $repoRoot $Configuration (Join-Path $outRoot 'Build.log')
    if ($null -ne $buildFailure)
    {
        Write-Output "CAPTURE_SCENE result=fail reason=build_failed detail=$buildFailure"
        exit 1
    }
    Write-Output "CAPTURE_SCENE build result=pass configuration=$Configuration"
}
if (-not (Test-Path -LiteralPath $gamePath))
{
    Write-Output "CAPTURE_SCENE result=fail reason=game_missing path=$gamePath"
    exit 1
}

$failures = @()
$results = @()
$gameLogPath = Join-Path $repoRoot 'Game.log'
# 計測の窓から外す、末尾の撮影のフレームの直前のフレーム数
$timingTailFrames = 2

# Game の起動の列。1 回の起動（run）が 1 つ以上の視点（Shots）を撮る。
#   視点ごとの起動: Label は視点の名前で、Shots は 1 つ。-Views を省いたときは、既定の視点の起動の報告（views=）で残りの起動を足す。
#   1 回の起動で複数の視点（--capture-views）: Label は views。-Views を省いたときは Game が報告に書く capture_views= で Shots を決める。
$runs = New-Object System.Collections.ArrayList
function Add-PerViewRuns
{
    foreach ($perViewShot in $shots)
    {
        [void]$runs.Add([pscustomobject]@{ Label = $perViewShot.Name; Shots = (New-Object System.Collections.ArrayList (, @($perViewShot))); CaptureViews = '' })
    }
}
$captureMode = if ($useMultiView) { 'multi_view' } else { 'per_view' }
# 視点ごとの起動で、既定の視点の報告から残りの起動を足したか
$perViewExpanded = $false
if ($useMultiView)
{
    $multiShots = New-Object System.Collections.ArrayList
    if (-not $expandViews)
    {
        foreach ($multiShot in $shots) { [void]$multiShots.Add($multiShot) }
    }
    [void]$runs.Add([pscustomobject]@{
        Label = 'views'
        Shots = $multiShots
        CaptureViews = if ($expandViews) { 'all' } else { $viewNames -join ',' }
    })
}
else
{
    Add-PerViewRuns
}

# Game を起動する前にこの powershell を Job Object へ入れる（入れられなければ Game を起動せずに失敗にする）
try
{
    [CaptureProcessJob]::EnterCurrentProcess()
}
catch
{
    Write-Output "CAPTURE_SCENE result=fail reason=job_object_failed detail=$($_.Exception.Message)"
    exit 1
}
for ($runIndex = 0; $runIndex -lt $runs.Count; ++$runIndex)
{
    $run = $runs[$runIndex]
    $bMultiRun = ($run.CaptureViews -ne '')
    $failuresBeforeRun = $failures.Count
    # 1 回の起動で撮るときの --capture-png は先頭の視点の PNG（Game はそのフォルダへ全視点を <視点>.png で書く）
    $firstShotName = if ($bMultiRun) { if ($expandViews) { 'default' } else { $run.Shots[0].Name } } else { $run.Shots[0].Name }
    $pngPath = Join-Path $outRoot "$firstShotName.png"
    $viewLogPath = Join-Path $outRoot "$($run.Label).Game.log"
    $viewValidationLogPath = Join-Path $outRoot "$($run.Label).Validation.log"
    $viewReportPath = Join-Path $outRoot "$($run.Label).SceneReport.txt"
    foreach ($stale in @($pngPath, $viewLogPath, $viewValidationLogPath, $viewReportPath, $gameLogPath))
    {
        Remove-Item -LiteralPath $stale -Force -ErrorAction SilentlyContinue
    }
    foreach ($staleShot in $run.Shots)
    {
        foreach ($staleSuffix in (@('.png', '.hdr.pfm', '.hdr.json') + $aovSuffixes))
        {
            Remove-Item -LiteralPath (Join-Path $outRoot "$($staleShot.Name)$staleSuffix") -Force -ErrorAction SilentlyContinue
        }
    }

    $tracePath = Join-Path $outRoot "$($run.Label).trace.csv"
    $arguments = @("--scene=$Scene", "--capture-png=`"$pngPath`"", "--scene-report=`"$viewReportPath`"")
    # 検証の起動は窓を前面に出さない（使っている人の作業を遮らない）
    $arguments += '--window-background'
    if ($GpuTimingFrames -gt 0)
    {
        # 実時間の計測: 決定的な撮影にせず、トレースを取って決まった描画フレーム数の後に撮る
        Remove-Item -LiteralPath $tracePath -Force -ErrorAction SilentlyContinue
        $arguments += "--trace-file=`"$tracePath`""
        $arguments += "--exit-after-rendered-frames=$($GpuTimingFrames + $TimingWarmupFrames + $timingTailFrames + $TimingReadyMarginFrames)"
    }
    else
    {
        $arguments += '--capture-deterministic'
    }
    if ($Hdr)
    {
        $arguments += '--capture-hdr'
    }
    if ($Aov)
    {
        $arguments += '--capture-aov'
    }
    if ($bMultiRun)
    {
        $arguments += "--capture-views=$($run.CaptureViews)"
    }
    elseif ($run.Shots[0].SceneView -ne '')
    {
        $arguments += "--scene-view=$($run.Shots[0].SceneView)"
    }
    if ($Width -gt 0)
    {
        $arguments += "--window-size=${Width}x${Height}"
    }
    $arguments += $extraArguments

    # Vulkan の検証レイヤーのメッセージ（VUID）は Game.log に入らないので、検証レイヤー自身のログ出力を起動ごとの
    # ファイルへ向けてエラーを数える（Debug と、-GameArgs に validation-layer を含む RelWithDebInfo だけ。他は検証レイヤーが無効）。
    # 起動した Game だけが受け継ぐよう、起動の直後に戻す。
    $previousValidationSettings = @{}
    foreach ($validationVariable in @('VK_KHRONOS_VALIDATION_DEBUG_ACTION', 'VK_KHRONOS_VALIDATION_LOG_FILENAME', 'VK_KHRONOS_VALIDATION_REPORT_FLAGS'))
    {
        $previousValidationSettings[$validationVariable] = [Environment]::GetEnvironmentVariable($validationVariable)
    }
    if ($validationLayerExpected)
    {
        $env:VK_KHRONOS_VALIDATION_DEBUG_ACTION = 'VK_DBG_LAYER_ACTION_LOG_MSG'
        $env:VK_KHRONOS_VALIDATION_LOG_FILENAME = $viewValidationLogPath
        $env:VK_KHRONOS_VALIDATION_REPORT_FLAGS = 'error,warn'
    }
    # 1 回の起動で複数の視点を撮るときは、時間切れの上限を -TimeoutSeconds の 2 倍にする（読み込みは 1 回で、視点ごとの待ちが足される）
    $runTimeoutSeconds = if ($bMultiRun) { $TimeoutSeconds * 2 } else { $TimeoutSeconds }
    $runStopwatch = [Diagnostics.Stopwatch]::StartNew()
    # この起動より前に書かれた PNG（残っていた古い撮影）を見分けるための起動の時刻（ファイルの時刻の粒度の分だけ前へ寄せる）
    $runStartUtc = [DateTime]::UtcNow.AddSeconds(-2)
    # アセットは作業ディレクトリからの相対パスで読むため、リポジトリのルートで起動する。
    try
    {
        $process = Start-Process -FilePath $gamePath -ArgumentList $arguments -WorkingDirectory $repoRoot -PassThru
    }
    finally
    {
        foreach ($validationVariable in $previousValidationSettings.Keys)
        {
            [Environment]::SetEnvironmentVariable($validationVariable, $previousValidationSettings[$validationVariable])
        }
    }
    [void]$process.Handle
    if (-not [CaptureProcessJob]::Contains($process.Handle))
    {
        # Job の外の Game は powershell が外から止められると残るので、撮影に使わず止めて失敗にする
        Stop-OwnedProcessTree $process.Id
        Write-Output "CAPTURE_SCENE result=fail reason=job_object_failed detail=起動した Game が Job Object に入っていない"
        exit 1
    }
    $exitCode = $null
    if ($process.WaitForExit($runTimeoutSeconds * 1000))
    {
        $process.WaitForExit()
        $exitCode = [int]$process.ExitCode
    }
    else
    {
        Stop-OwnedProcessTree $process.Id
        $failures += "$($run.Label): timeout（$runTimeoutSeconds 秒で終わらなかった）"
    }
    $runStopwatch.Stop()
    Write-Output ("CAPTURE_SCENE run label={0} mode={1} seconds={2:0.0} exit_code={3}" -f $run.Label, $(if ($bMultiRun) { 'multi_view' } else { 'per_view' }), $runStopwatch.Elapsed.TotalSeconds, $exitCode)

    if (Test-Path -LiteralPath $gameLogPath)
    {
        Copy-Item -LiteralPath $gameLogPath -Destination $viewLogPath -Force
    }

    # シーンの報告（--scene-report）の検査。ログとは別に Game が書くので、Release でも未知のシーン・視点と必要な資産の欠けを検査できる。
    # 報告は ResolveStartupScene が必ず書くので、Game が正常に終わったのに無いなら失敗にする。
    $missingAssets = @()
    $reportLines = @()
    if (Test-Path -LiteralPath $viewReportPath)
    {
        $reportLines = @(Get-Content -LiteralPath $viewReportPath -Encoding UTF8 | Where-Object { $_ -match '\S' })
    }
    # 実行中に視点を置き直せないシーン: 1 回の起動での撮影をやめ、視点ごとの起動に切り替える（この起動の失敗は数えない）
    if ($bMultiRun -and @($reportLines | Where-Object { $_ -match '^capture_views=unsupported\s*$' }).Count -gt 0)
    {
        Write-Output "CAPTURE_SCENE capture_views unsupported scene=$Scene（実行中に視点を置き直せないシーン。視点ごとに起動して撮る）"
        if ($failures.Count -gt $failuresBeforeRun)
        {
            $failures = @($failures | Select-Object -First $failuresBeforeRun)
        }
        $captureMode = 'per_view'
        if ($expandViews)
        {
            $shots.Clear()
            [void]$shots.Add([pscustomobject]@{ Name = 'default'; SceneView = ''; DefaultViewName = '' })
        }
        Add-PerViewRuns
        continue
    }
    foreach ($reportLine in $reportLines)
    {
        if ($reportLine -match '^(unknown_scene|unknown_view)=')
        {
            $failures += "$($run.Label): シーンの報告: $($reportLine.Trim())"
        }
        elseif ($reportLine -match '^missing_asset=')
        {
            $missingAssets += $reportLine.Trim()
        }
        elseif ($reportLine -match '^views=(.+)$' -and $expandViews)
        {
            $sceneViews = @($Matches[1].Trim() -split ',' | Where-Object { $_ -ne '' })
            if (-not $bMultiRun -and -not $perViewExpanded)
            {
                $perViewExpanded = $true
                # 先頭が既定の視点（今撮っているもの）。残りの視点を撮る起動を足す
                $run.Shots[0].DefaultViewName = $sceneViews[0]
                foreach ($otherView in @($sceneViews | Select-Object -Skip 1))
                {
                    $otherShot = [pscustomobject]@{ Name = $otherView; SceneView = $otherView; DefaultViewName = '' }
                    [void]$shots.Add($otherShot)
                    [void]$runs.Add([pscustomobject]@{ Label = $otherView; Shots = (New-Object System.Collections.ArrayList (, @($otherShot))); CaptureViews = '' })
                }
            }
            elseif ($bMultiRun)
            {
                $run | Add-Member -NotePropertyName DefaultViewName -NotePropertyValue $sceneViews[0] -Force
            }
        }
        elseif ($reportLine -match '^capture_views=(.+)$' -and $bMultiRun -and $expandViews -and $run.Shots.Count -eq 0)
        {
            # Game が決めた撮る順の視点（先頭は default）
            foreach ($capturedView in @($Matches[1].Trim() -split ',' | Where-Object { $_ -ne '' }))
            {
                $sceneView = if ($capturedView -eq 'default') { '' } else { $capturedView }
                [void]$run.Shots.Add([pscustomobject]@{ Name = $capturedView; SceneView = $sceneView; DefaultViewName = '' })
            }
        }
    }
    if ($bMultiRun -and $expandViews)
    {
        foreach ($expandedShot in $run.Shots)
        {
            if ($expandedShot.Name -eq 'default' -and $null -ne $run.PSObject.Properties['DefaultViewName'])
            {
                $expandedShot.DefaultViewName = $run.DefaultViewName
            }
        }
        if ($run.Shots.Count -eq 0 -and $null -ne $exitCode -and $exitCode -eq 0)
        {
            $failures += "$($run.Label): シーンの報告に capture_views=（撮った視点の列）が無い"
        }
    }
    if ($reportLines.Count -eq 0 -and $null -ne $exitCode -and $exitCode -eq 0)
    {
        $failures += "$($run.Label): シーンの報告（$($run.Label).SceneReport.txt）が無い"
    }
    if ($null -ne $exitCode -and $exitCode -ne 0)
    {
        $failures += "$($run.Label): Game の終了コードが $exitCode"
    }

    # Game.log の検査。Release はログが無効（NORVES_ENABLE_LOGGING=0）で Game.log を書かないため飛ばす。
    $sceneReady = $null
    $sceneReadyFrame = $null
    $placedCount = $null
    $placedEntities = $null
    $placedBananas = $null
    $engineChecks = @()
    $logMissingAssets = @()
    if (Test-Path -LiteralPath $viewLogPath)
    {
        # SceneReady のフレーム（SCENE_READY_FRAME）と、シーンが出した配置の数（最後の行）
        $readyFrameLines = @(Select-String -LiteralPath $viewLogPath -Pattern "SCENE_READY_FRAME scene=$Scene frame=(\d+)")
        if ($readyFrameLines.Count -gt 0)
        {
            $sceneReadyFrame = [long]$readyFrameLines[-1].Matches[0].Groups[1].Value
        }
        $placedLines = @(Select-String -LiteralPath $viewLogPath -Pattern '(?:MEGA_STRESS_PLACED entities|MEGA_BANANAS_PLACED count)=(\d+)')
        if ($placedLines.Count -gt 0)
        {
            $placedCount = [long]$placedLines[-1].Matches[0].Groups[1].Value
        }
        # 置いた Entity の数とバナナの本数（最後の行）
        $entityLines = @(Select-String -LiteralPath $viewLogPath -Pattern 'MEGA_STRESS_PLACED entities=(\d+)')
        if ($entityLines.Count -gt 0)
        {
            $placedEntities = [long]$entityLines[-1].Matches[0].Groups[1].Value
        }
        $bananaLines = @(Select-String -LiteralPath $viewLogPath -Pattern '(?:MEGA_STRESS_PLACED .*\bbananas|MEGA_BANANAS_PLACED count)=(\d+)')
        if ($bananaLines.Count -gt 0)
        {
            $placedBananas = [long]$bananaLines[-1].Matches[0].Groups[1].Value
        }
        if ($RequireAssets)
        {
            if ($null -ne $requestedEntities -and $null -ne $placedEntities -and $placedEntities -ne $requestedEntities)
            {
                $failures += "$($run.Label): 置いた Entity の数 $placedEntities が指定（stress-entities=$requestedEntities）と違う"
            }
            if ($null -ne $requestedBananas -and $null -ne $placedBananas -and $placedBananas -ne $requestedBananas)
            {
                $failures += "$($run.Label): 置いたバナナの本数 $placedBananas が指定（banana-count=$requestedBananas）と違う"
            }
        }
        # 開発ビルドの検査（ENGINE_CHECK）。fail の行は、名前を指定していなくても失敗にする。
        $checkResult = Get-EngineCheckCounts $viewLogPath
        foreach ($failLine in $checkResult.FailLines)
        {
            $failures += "$($run.Label): 検査が fail: $failLine"
        }
        foreach ($checkName in $requiredChecks)
        {
            $count = $checkResult.Counts[$checkName]
            $passCount = if ($null -ne $count) { $count.pass } else { 0 }
            $failCount = if ($null -ne $count) { $count.fail } else { 0 }
            $engineChecks += [pscustomobject][ordered]@{ name = $checkName; pass = $passCount; fail = $failCount }
            Write-Output "CAPTURE_SCENE engine_check view=$($run.Label) name=$checkName pass=$passCount fail=$failCount"
            if ($passCount -lt 1)
            {
                $failures += "$($run.Label): 検査 $checkName の pass がログに無い（ENGINE_CHECK name=$checkName result=pass が 0 件。開発ビルドか、--engine-checks が効いているかを確かめる）"
            }
        }
        $sceneReady = @(Select-String -LiteralPath $viewLogPath -Pattern "SCENE_READY scene=$Scene\s*$").Count -gt 0
        if (-not $sceneReady)
        {
            $failures += "$($run.Label): SCENE_READY（シーンの組み立ての完了）がログに無い"
        }
        if ($GpuTimingFrames -gt 0 -and $null -eq $sceneReadyFrame)
        {
            $failures += "$($run.Label): SCENE_READY_FRAME（SceneReady のフレーム番号）がログに無く、計測の窓を決められない"
        }
        $shaderFailures = @(Select-String -LiteralPath $viewLogPath -Pattern 'Failed to compile shader' -SimpleMatch)
        foreach ($line in $shaderFailures)
        {
            $failures += "$($run.Label): $($line.Line.Trim())"
        }
        $unknownLines = @(Select-String -LiteralPath $viewLogPath -Pattern 'SCENE_UNKNOWN|SCENE_VIEW_UNKNOWN|SCENE_CATALOG_INVALID|CAMERA_SCRIPT_INVALID|CAMERA_SCRIPT_CAPTURE (failed|unavailable)|CAPTURE_VIEWS failed')
        foreach ($line in $unknownLines)
        {
            $failures += "$($run.Label): $($line.Line.Trim())"
        }
        # シーンの定義の欠け（SCENE_ASSET_MISSING）は報告で数えたので、ログからは Rendering3DTest の読み込み失敗の印だけを拾う。
        $logMissingAssets = @(Select-String -LiteralPath $viewLogPath -Pattern $missingAssetPattern |
            Where-Object { $_.Line -notmatch 'SCENE_ASSET_MISSING' } | ForEach-Object { $_.Line.Trim() })
    }
    elseif ($Configuration -ne 'Release')
    {
        $failures += "$($run.Label): Game.log が無い"
    }

    $missingAssets = @($missingAssets) + @($logMissingAssets)
    foreach ($line in $missingAssets)
    {
        if ($RequireAssets)
        {
            $failures += "$($run.Label): 資産の欠け: $line"
        }
        else
        {
            Write-Output "CAPTURE_SCENE warning view=$($run.Label) missing_asset=$line"
        }
    }

    # 検証レイヤーのログ。"Validation Error:" の見出しの行を数え、1 件でもあれば失敗にする。
    # Debug でログのファイルが無いときは、検証レイヤーがログの設定を読んでいない（落ちようのない合格になる）ので失敗にする。
    $validationInfo = $null
    if (Test-Path -LiteralPath $viewValidationLogPath)
    {
        $validationLines = @(Get-Content -LiteralPath $viewValidationLogPath -Encoding Default | Where-Object { $_ -match '\S' })
        $errorLines = @($validationLines | Where-Object { $_ -match 'Validation Error:' })
        $warningLines = @($validationLines | Where-Object { $_ -match 'Validation Warning:' })
        $validationInfo = [ordered]@{
            log = "$($run.Label).Validation.log"
            error_count = $errorLines.Count
            warning_count = $warningLines.Count
        }
        if ($errorLines.Count -gt 0)
        {
            $failures += "$($run.Label): 検証レイヤーのエラーが $($errorLines.Count) 件ある（$($run.Label).Validation.log。先頭: $($errorLines[0]))"
        }
    }
    elseif ($validationLayerExpected)
    {
        $failures += "$($run.Label): 検証レイヤーのログ（$($run.Label).Validation.log）が無い（検証レイヤーがログの設定を読んでいない）"
    }
    # 検証レイヤーを入れる撮影では、Game が入れたこと（VALIDATION_LAYER enabled）をログに書いたかも見る
    if ($validationLayerExpected -and (Test-Path -LiteralPath $viewLogPath))
    {
        $layerEnabled = @(Select-String -LiteralPath $viewLogPath -Pattern 'VALIDATION_LAYER enabled' -SimpleMatch).Count -gt 0
        if ($null -ne $validationInfo) { $validationInfo['layer_enabled_logged'] = $layerEnabled }
        if (-not $layerEnabled)
        {
            $failures += "$($run.Label): Game.log に VALIDATION_LAYER enabled が無い（検証レイヤーが入っていない）"
        }
    }

    $gpuTiming = $null
    $megaStats = $null
    $timingStartFrame = $null
    if ($GpuTimingFrames -gt 0)
    {
        if (-not (Test-Path -LiteralPath $tracePath))
        {
            $failures += "$($run.Label): トレースが無い（$tracePath）"
        }
        else
        {
            # 計測の窓は SceneReady のフレームの後だけで取る。SceneReady の記録が無ければ、窓を決められないので計測しない。
            if ($null -ne $sceneReadyFrame)
            {
                $timingResult = Get-GpuTimingFromTrace $tracePath $GpuTimingFrames $GpuFrameBudgetMs $sceneReadyFrame $TimingWarmupFrames $timingTailFrames
                if ($null -ne $timingResult.Error)
                {
                    $failures += "$($run.Label): $($timingResult.Error)"
                }
                else
                {
                    $gpuTiming = $timingResult.Timing
                    # 計測の開始のフレーム: ウォームアップを除いた最初の有効な描画フレーム
                    $timingStartFrame = $gpuTiming.start_frame
                }
            }
        }
        if (Test-Path -LiteralPath $viewLogPath)
        {
            $megaStats = Get-MegaStatsFromLog $viewLogPath
        }
    }

    foreach ($shot in $run.Shots)
    {
        $shotPngPath = Join-Path $outRoot "$($shot.Name).png"
        # 視点ごとのシーンの報告: 1 回の起動で撮った視点には、その起動の報告を写す（出力の形を視点ごとの起動と揃える）
        $shotReportPath = Join-Path $outRoot "$($shot.Name).SceneReport.txt"
        if ($bMultiRun -and (Test-Path -LiteralPath $viewReportPath))
        {
            Copy-Item -LiteralPath $viewReportPath -Destination $shotReportPath -Force
        }
        # この起動より前の PNG（残っていた古い撮影）は撮れていないものとして扱う
        if (-not (Test-Path -LiteralPath $shotPngPath) -or (Get-Item -LiteralPath $shotPngPath).LastWriteTimeUtc -lt $runStartUtc)
        {
            $failures += "$($shot.Name): PNG が無い"
            continue
        }
        $measured = [SceneCaptureMetrics]::Measure($shotPngPath)
        if ($Width -gt 0 -and ([int]$measured[0] -ne $Width -or [int]$measured[1] -ne $Height))
        {
            $failures += "$($shot.Name): PNG の大きさが指定と違う（$([int]$measured[0])x$([int]$measured[1])、指定 ${Width}x${Height}）"
        }
        # HDR: PNG と同じ起動で書かれ、大きさが PNG と同じこと（値の検査は ReferenceCheck.py --hdr）
        $hdrInfo = $null
        if ($Hdr)
        {
            $hdrPfmPath = Join-Path $outRoot "$($shot.Name).hdr.pfm"
            $hdrJsonPath = Join-Path $outRoot "$($shot.Name).hdr.json"
            $bHdrFresh = $true
            foreach ($hdrPath in @($hdrPfmPath, $hdrJsonPath))
            {
                if (-not (Test-Path -LiteralPath $hdrPath) -or (Get-Item -LiteralPath $hdrPath).LastWriteTimeUtc -lt $runStartUtc)
                {
                    $failures += "$($shot.Name): HDR が無い（$([IO.Path]::GetFileName($hdrPath))）"
                    $bHdrFresh = $false
                }
            }
            if ($bHdrFresh)
            {
                $hdrMeta = Get-Content -LiteralPath $hdrJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
                if ([int]$hdrMeta.width -ne [int]$measured[0] -or [int]$hdrMeta.height -ne [int]$measured[1])
                {
                    $failures += "$($shot.Name): HDR の大きさ $($hdrMeta.width)x$($hdrMeta.height) が PNG $([int]$measured[0])x$([int]$measured[1]) と違う"
                }
                $hdrInfo = [ordered]@{
                    file = "$($shot.Name).hdr.pfm"
                    meta = "$($shot.Name).hdr.json"
                    width = [int]$hdrMeta.width
                    height = [int]$hdrMeta.height
                    pre_exposure = [double]$hdrMeta.pre_exposure
                    frame_number = [long]$hdrMeta.frame_number
                }
                Write-Output ("CAPTURE_SCENE hdr view={0} size={1}x{2} pre_exposure={3} frame={4}" -f `
                    $shot.Name, $hdrInfo.width, $hdrInfo.height, $hdrInfo.pre_exposure, $hdrInfo.frame_number)
            }
        }
        # AOV: PNG と同じ起動で書かれ、大きさが PNG と同じこと（値の検査は ReferenceCheck.py --aov）
        $aovInfo = $null
        if ($Aov)
        {
            $bAovFresh = $true
            foreach ($aovSuffix in $aovSuffixes)
            {
                $aovPath = Join-Path $outRoot "$($shot.Name)$aovSuffix"
                if (-not (Test-Path -LiteralPath $aovPath) -or (Get-Item -LiteralPath $aovPath).LastWriteTimeUtc -lt $runStartUtc)
                {
                    $failures += "$($shot.Name): AOV が無い（$([IO.Path]::GetFileName($aovPath))）"
                    $bAovFresh = $false
                }
            }
            if ($bAovFresh)
            {
                $aovMeta = Get-Content -LiteralPath (Join-Path $outRoot "$($shot.Name).aov.json") -Raw -Encoding UTF8 | ConvertFrom-Json
                if ([int]$aovMeta.width -ne [int]$measured[0] -or [int]$aovMeta.height -ne [int]$measured[1])
                {
                    $failures += "$($shot.Name): AOV の大きさ $($aovMeta.width)x$($aovMeta.height) が PNG $([int]$measured[0])x$([int]$measured[1]) と違う"
                }
                $aovInfo = [ordered]@{
                    meta = "$($shot.Name).aov.json"
                    width = [int]$aovMeta.width
                    height = [int]$aovMeta.height
                    frame_number = [long]$aovMeta.frame_number
                }
                Write-Output ("CAPTURE_SCENE aov view={0} size={1}x{2} frame={3}" -f `
                    $shot.Name, $aovInfo.width, $aovInfo.height, $aovInfo.frame_number)
            }
        }
        # 大気の書き出し: PNG と同じ起動で書かれ、同じフレームであること（値の検査は ReferenceCheck.py --atmos-dump）
        $atmosphereInfo = $null
        if ($atmosphereDump)
        {
            $bAtmosphereFresh = $true
            foreach ($atmosphereSuffix in $atmosphereSuffixes)
            {
                $atmospherePath = Join-Path $outRoot "$($shot.Name)$atmosphereSuffix"
                if (-not (Test-Path -LiteralPath $atmospherePath) -or (Get-Item -LiteralPath $atmospherePath).LastWriteTimeUtc -lt $runStartUtc)
                {
                    $failures += "$($shot.Name): 大気の書き出しが無い（$([IO.Path]::GetFileName($atmospherePath))）"
                    $bAtmosphereFresh = $false
                }
            }
            if ($bAtmosphereFresh)
            {
                $atmosphereMeta = Get-Content -LiteralPath (Join-Path $outRoot "$($shot.Name).atmos.json") -Raw -Encoding UTF8 | ConvertFrom-Json
                $atmosphereInfo = [ordered]@{
                    meta = "$($shot.Name).atmos.json"
                    rays = @($atmosphereMeta.probe.rays).Count
                    frame_number = [long]$atmosphereMeta.frame_number
                }
                Write-Output ("CAPTURE_SCENE atmosphere view={0} rays={1} frame={2}" -f `
                    $shot.Name, $atmosphereInfo.rays, $atmosphereInfo.frame_number)
            }
        }
        $result = [ordered]@{
            view = $shot.Name
            scene_view = if ($shot.SceneView -ne '') { $shot.SceneView } else { $shot.DefaultViewName }
            png = "$($shot.Name).png"
            scene_report = if (Test-Path -LiteralPath $shotReportPath) { "$($shot.Name).SceneReport.txt" } else { $null }
            game_log = if (Test-Path -LiteralPath $viewLogPath) { "$($run.Label).Game.log" } else { $null }
            capture_run = $run.Label
            width = [int]$measured[0]
            height = [int]$measured[1]
            mean_luminance = [math]::Round($measured[2], 3)
            clipped_white_ratio = [math]::Round($measured[3], 6)
            crushed_black_ratio = [math]::Round($measured[4], 6)
            scene_ready = $sceneReady
            scene_ready_frame = $sceneReadyFrame
            placed_count = $placedCount
            placed_entities = $placedEntities
            placed_bananas = $placedBananas
            timing_start_frame = $timingStartFrame
            engine_checks = $engineChecks
            missing_assets = $missingAssets.Count
            vulkan_validation = $validationInfo
            gpu_timing = $gpuTiming
            mega_stats = $megaStats
            hdr = $hdrInfo
            aov = $aovInfo
            atmosphere = $atmosphereInfo
        }
        $results += [pscustomobject]$result
        Write-Output ("CAPTURE_SCENE view={0} size={1}x{2} mean_luminance={3} clipped_white_ratio={4} crushed_black_ratio={5}" -f `
            $result.view, $result.width, $result.height, $result.mean_luminance, $result.clipped_white_ratio, $result.crushed_black_ratio)
        Write-Output ("CAPTURE_SCENE placed view={0} entities={1} bananas={2} requested_entities={3} requested_bananas={4}" -f `
            $result.view, $placedEntities, $placedBananas, $requestedEntities, $requestedBananas)
        if ($null -ne $gpuTiming)
        {
            Write-Output ("CAPTURE_SCENE timing_window view={0} scene_ready_frame={1} timing_start_frame={2} window={3}..{4} valid_samples={5} placed_count={6}" -f `
                $result.view, $sceneReadyFrame, $timingStartFrame, $gpuTiming.window_first_frame, $gpuTiming.window_last_frame, $gpuTiming.valid_samples, $placedCount)
            Write-Output ("CAPTURE_SCENE gpu_timing view={0} frames={1} median_ms={2} mean_ms={3} p95_ms={4} max_ms={5} cpu_median_ms={6} gt_median_ms={7} gt_p95_ms={8} rt_median_ms={9} rt_p95_ms={10} budget_ms={11} within_budget={12}" -f `
                $result.view, $gpuTiming.frames, $gpuTiming.gpu_frame_ms_median, $gpuTiming.gpu_frame_ms_mean, $gpuTiming.gpu_frame_ms_p95,
                $gpuTiming.gpu_frame_ms_max, $gpuTiming.cpu_frame_ms_median, $gpuTiming.game_thread_ms_median, $gpuTiming.game_thread_ms_p95,
                $gpuTiming.render_thread_ms_median, $gpuTiming.render_thread_ms_p95, $gpuTiming.budget_ms, $gpuTiming.within_budget)
            Write-Output ("CAPTURE_SCENE gpu_pass_median view={0} {1}" -f $result.view,
                (($gpuTiming.pass_median_ms | Select-Object -First 10 | ForEach-Object { "$($_.pass)=$($_.median_ms)" }) -join ' '))
            Write-Output ("CAPTURE_SCENE gpu_pass_p95 view={0} {1}" -f $result.view,
                (($gpuTiming.pass_ms | Select-Object -First 10 | ForEach-Object { "$($_.pass)=$($_.p95_ms)/max=$($_.max_ms)" }) -join ' '))
            Write-Output ("CAPTURE_SCENE cpu_scope_median view={0} scopes={1} {2}" -f $result.view, @($gpuTiming.cpu_scope_ms).Count,
                (($gpuTiming.cpu_scope_ms | Select-Object -First 15 | ForEach-Object { "$($_.scope)=$($_.median_ms)/max=$($_.max_ms)/t=$($_.thread)" }) -join ' '))
            Write-Output ("CAPTURE_SCENE cpu_scope_group view={0} {1}" -f $result.view,
                (($gpuTiming.cpu_scope_group_ms | ForEach-Object { "$($_.scope)=$($_.median_ms)/p95=$($_.p95_ms)/max=$($_.max_ms)" }) -join ' '))
        }
        if ($null -ne $megaStats)
        {
            Write-Output ("CAPTURE_SCENE mega_stats view={0} clusters_drawn_last={1} clusters_drawn_max={2} draw_dropped_last={3} draw_dropped_max={4} geometry_used_mb={5} geometry_pool_capacity_mb={6} vt_used_mb_max={7} shadow_map_pool_mb={8} visibility_bytes={9} indirect_bytes={10} draw_info_bytes={11} bvh_queue_bytes={12} sw_raster_bytes={13} sw_raster_list_mb={14} visbuffer_records_mb={15} visbuffer64_mb={16} vsm_pool_mb={17}" -f `
                $result.view, $megaStats.clusters_drawn_last, $megaStats.clusters_drawn_max, $megaStats.draw_dropped_last, $megaStats.draw_dropped_max, $megaStats.geometry_used_mb,
                $megaStats.geometry_pool_capacity_mb, $megaStats.vt_used_mb_max, $megaStats.shadow_map_pool_mb, $megaStats.visibility_bytes, $megaStats.indirect_bytes,
                $megaStats.draw_info_bytes, $megaStats.bvh_queue_bytes, $megaStats.sw_raster_bytes, $megaStats.sw_raster_list_mb, $megaStats.visbuffer_records_mb,
                $megaStats.visbuffer64_mb, $megaStats.vsm_pool_mb)
        }
    }
}

# 別の撮影（-CompareWith）と、同じ視点名の PNG を比べる。平均輝度の差と PSNR の式は CaptureStartupScene.ps1 の
# -CompareDeterministicWith と同じ。
$comparison = @()
if ($CompareWith -ne '')
{
    $compareRoot = if ([IO.Path]::IsPathRooted($CompareWith)) { [IO.Path]::GetFullPath($CompareWith) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $CompareWith)) }
    $compareMetricsPath = Join-Path $compareRoot 'metrics.json'
    if (-not (Test-Path -LiteralPath $compareMetricsPath))
    {
        $failures += "比べる撮影の metrics.json が無い: $compareRoot"
    }
    else
    {
        $compareMetrics = Get-Content -LiteralPath $compareMetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($entry in $results)
        {
            $thisPng = Join-Path $outRoot $entry.png
            $otherPng = Join-Path $compareRoot $entry.png
            if (-not (Test-Path -LiteralPath $thisPng) -or -not (Test-Path -LiteralPath $otherPng))
            {
                $failures += "$($entry.view): 比べる画像が揃っていない（$thisPng / $otherPng）"
                continue
            }
            try
            {
                $difference = [SceneCaptureMetrics]::Compare($thisPng, $otherPng)
            }
            catch
            {
                $failures += "$($entry.view): 画像を比べられない: $($_.Exception.Message)"
                continue
            }
            $meanDifference = [math]::Abs($difference[0] - $difference[1])
            $withinLimits = ($meanDifference -le $CompareMeanLuminanceLimit) -and ($difference[2] -ge $ComparePsnrLimit)
            $item = [ordered]@{
                view = $entry.view
                png = $entry.png
                mean_luminance = [math]::Round($difference[0], 4)
                compare_mean_luminance = [math]::Round($difference[1], 4)
                mean_luminance_difference = [math]::Round($meanDifference, 4)
                psnr_db = [math]::Round($difference[2], 3)
                mismatched_pixel_ratio = [math]::Round($difference[3], 6)
                max_channel_difference = [int]$difference[4]
                within_limits = $withinLimits
            }
            # 両方に gpu_timing があれば、median の比（この撮影 / 比べる撮影）も書く
            $otherView = @($compareMetrics.views | Where-Object { $_.view -eq $entry.view }) | Select-Object -First 1
            if ($null -ne $entry.gpu_timing -and $null -ne $otherView -and $null -ne $otherView.gpu_timing)
            {
                foreach ($key in @('gpu_frame_ms_median', 'game_thread_ms_median', 'render_thread_ms_median'))
                {
                    $otherValue = $otherView.gpu_timing.$key
                    $item["${key}_ratio"] = if ($null -ne $otherValue -and [double]$otherValue -gt 0.0 -and $null -ne $entry.gpu_timing.$key) { [math]::Round([double]$entry.gpu_timing.$key / [double]$otherValue, 3) } else { $null }
                }
            }
            $comparison += [pscustomobject]$item
            Write-Output ("CAPTURE_SCENE comparison view={0} mean_luminance={1} compare_mean_luminance={2} mean_luminance_difference={3} (limit {4}) psnr_db={5} (limit {6}) mismatched_pixel_ratio={7} max_channel_difference={8} within_limits={9}" -f `
                $item.view, $item.mean_luminance, $item.compare_mean_luminance, $item.mean_luminance_difference, $CompareMeanLuminanceLimit,
                $item.psnr_db, $ComparePsnrLimit, $item.mismatched_pixel_ratio, $item.max_channel_difference, $item.within_limits)
            if (-not $withinLimits)
            {
                $failures += "$($entry.view): 比べる撮影と一致しない（平均輝度の差 $($item.mean_luminance_difference) / 上限 $CompareMeanLuminanceLimit、PSNR $($item.psnr_db) dB / 下限 $ComparePsnrLimit dB）"
            }
        }
    }
}

$metrics = [ordered]@{
    scene = $Scene
    configuration = $Configuration
    capture_mode = $captureMode
    window_size = if ($Width -gt 0) { "${Width}x${Height}" } else { $null }
    game_args = $extraArguments
    require_assets = [bool]$RequireAssets
    gpu_timing_frames = $GpuTimingFrames
    hdr = [bool]$Hdr
    aov = [bool]$Aov
    atmosphere_dump = [bool]$atmosphereDump
    timing_warmup_frames = $TimingWarmupFrames
    timing_ready_margin_frames = $TimingReadyMarginFrames
    require_checks = $requiredChecks
    compare_with = $CompareWith
    compare_mean_luminance_limit = $CompareMeanLuminanceLimit
    compare_psnr_limit = $ComparePsnrLimit
    comparison = $comparison
    views = $results
    failures = $failures
}
$metricsPath = Join-Path $outRoot 'metrics.json'
$metricsJson = $metrics | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($metricsPath, $metricsJson, (New-Object System.Text.UTF8Encoding($false)))

# HDR・AOV・大気の書き出しの撮影は、入力のハッシュ（inputs.json）を横に書く。撮影が失敗したときは古い入力を残さない。
# 署名は metrics.json の視点・起動引数から出力の画像とカメラ台本を辿るので、metrics.json を書いた後に記録する
if ($Hdr -or $Aov -or $atmosphereDump)
{
    $inputsPath = Join-Path $outRoot 'inputs.json'
    Remove-Item -LiteralPath $inputsPath -Force -ErrorAction SilentlyContinue
    if ($failures.Count -eq 0)
    {
        $recordOutput = & python (Join-Path $repoRoot 'Scripts\Terrain\ReferenceCheck.py') --record-inputs --kind engine --capture $outRoot --game-exe $gamePath 2>&1
        $recordExitCode = $LASTEXITCODE
        foreach ($line in @($recordOutput)) { Write-Output "$line" }
        if ($recordExitCode -ne 0 -or -not (Test-Path -LiteralPath $inputsPath))
        {
            $failures += "入力のハッシュ（inputs.json）を書けなかった（ReferenceCheck.py --record-inputs の終了コード $recordExitCode）"
            # 入力の記録の失敗を metrics.json の failures にも入れる
            $metrics.failures = $failures
            $metricsJson = $metrics | ConvertTo-Json -Depth 8
            [IO.File]::WriteAllText($metricsPath, $metricsJson, (New-Object System.Text.UTF8Encoding($false)))
        }
    }
}

if ($failures.Count -gt 0)
{
    foreach ($failure in $failures)
    {
        Write-Output "CAPTURE_SCENE failure: $failure"
    }
    Write-Output "CAPTURE_SCENE result=fail failures=$($failures.Count) metrics=$metricsPath"
    exit 1
}
Write-Output "CAPTURE_SCENE result=pass scene=$Scene metrics=$metricsPath"
exit 0
