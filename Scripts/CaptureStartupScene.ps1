# 起動画面（Rendering3DTest）を既定・近接・低角度の3視点で撮影し、各PNGと画素の数値を OutDir へ書き出す。
# Game.exe を --capture-png と --startup-camera 付きで起動し、アセットの読み込みが落ち着いた後の最終出力
# （ImGui・画面空間のボードを含まない）を保存させる。数値は 8bit の表示値から求める:
#   mean_luminance      … Rec.709 の重み（0.2126R+0.7152G+0.0722B）で求めた画素値の平均（0〜255）
#   clipped_white_ratio … R・G・B がすべて 255 の画素の割合
#   crushed_black_ratio … R・G・B がすべて 0 の画素の割合
# Game の終了コードが0でない、PNGが無い、Game.log にシェーダーのコンパイル失敗がある場合は終了コード1を返す。
# Slang SDK 未設定の neural_material_decode.slang のコンパイル失敗だけは既知として除外する。
# -Configuration Release はログが無効で Game.log を書かないため、ログの検査（シェーダーの失敗・間接光の出どころ）を飛ばす。
#
# -SunElevations を与えると、各視点を太陽の仰角（度）ごとに撮り、<視点>-sun<仰角>.png として保存する
# （例: -SunElevations 10,45,3 で朝・昼・夕）。方位は -SunAzimuth（省略時は起動画面の既定）。
# 露出は -ExposureEV100s で仰角と同じ順に与える。省略時は仰角から晴天の目安の EV100 を選ぶ
# （自動露出が入るまでの暫定の対応表）。
#
# -OrbitDegreesPerSecond を与えると、起動からカメラを一定の速さ（度/秒）で軸の周りに回し続け、回っている
# 途中の画面を連続して撮る（動くカメラでの TAA の残像の確認用）。各視点を1回の起動で、アセットの読み込みが
# 落ち着いてから -OrbitRenderedFrames の描画フレーム数（既定 60,75,90）の時点ごとに撮り（TAA の履歴は撮影の
# 間つながったまま）、<視点>-orbit-f<フレーム数>.png として保存する。ログは <視点>-orbit.Game.log。-AntiAliasing FXAA で起動画面の既定の TAA の代わりに FXAA で撮る（見比べ用）。-HeightFogDensity・-HeightFogFalloff で高さフォグの密度・減衰を起動画面の既定から替えて撮る。
# -RenderScale で内部解像度の倍率（0.5〜1）を替え、-DebugDrawTestLines で大きな球を囲む箱をデバッグの線で描いて撮る
# （デバッグ描画が内部解像度に依らず最終解像度で描かれることの確認用）。
# -Night で夜（--night: 空と空の太陽を消し、環境光を月明かり程度にする。露出は自動のまま）の3視点を
# <視点>-night.png として撮る。点光源の影の確認用で、-SunElevations とは併用しない。
# -StillRenderedFrames を与えると、カメラを止めたまま1回の起動で各描画フレーム数の時点を撮り
# （<視点>-still-f<フレーム数>.png）、視点ごとに決めた静止した地面の領域で、画素の時間方向の標準偏差の
# 平均を temporal_noise として metrics.json へ書く（表示の 8bit の輝度と、それを sRGB からリニアへ戻した
# 輝度の2つ）。RTGI などの時間方向の雑音の確認用で、-OrbitDegreesPerSecond とは併用しない。
# 領域は視点ごとに複数（手前・中ほど・地平線寄りの地面の帯）で、標準偏差が表示の1/255を超える画素の割合も書く。
# -Rtgi Off で起動画面の RTGI を切り（環境変数 NORVES_STARTUP_RTGI=0）、環境光（IBL）だけで撮る。
# -CompareNoiseWith に別の撮影（例: -Rtgi Off）の出力先を与えると、視点・領域ごとに表示の標準偏差の比
# （今回/比べる側）を求め、どれかが -NoiseRatioLimit（既定2）を超えたら失敗にする。
# -GpuTimingFrames を与えると、Game を --trace-file 付きで起動し、落ち着いてからその描画フレーム数の後に撮る。
# トレースの Type=GPU の行（フレームごとの FrameGPU・AccelerationStructureBuild（加速構造の更新）・
# RenderGraph のパスごとの GPU の時間。Frame 列は区間を記録したフレームの番号）のうち、FrameGPU（GPU の
# タイムスタンプで測ったフレームの区間。加速構造の構築・更新、RenderGraph の全パスと表示への書き出しを含む）の
# 最後の (-GpuTimingFrames − 60) フレーム（撮影のフレームの直前2つを除く）の中央値・95 パーセンタイル・最大と、
# パスごとの中央値を gpu_timing として metrics.json へ書く。予算を超えたフレームはパスごとの時間と
# 中央値からの増分を over_budget_frames に書く。
# タイムスタンプは統計が有効な構成（Debug・RelWithDebInfo）だけで取れるため、Release とは併用しない。
# 予算（-GpuFrameBudgetMs、既定 16.6 ms）を超えても失敗にはせず、窓のどれかのフレームが超えたら
# within_budget=false と書く（95 パーセンタイルの判定は p95_within_budget）。
# -LooseTextures でクック済みのテクスチャ（build/CookedAssets/）を使わず、ばらの元画像を無圧縮で読んで撮る
# （--no-cooked-textures。クック済みとの見た目の比較用）。各撮影のログから VRAM_LEDGER の texture_mb と、
# クック済みが無くばらで読んだ数（TEXTURE_COOKED_MISSING）を metrics.json へ書く。
# -DefaultCamera で既定視点のカメラを替え（例: 変更前の版の既定 0,30,5）、-ViewNames で撮る視点を絞る（例: default）。
#
# -Deterministic で Game を --capture-deterministic 付きで起動し、同じコードを2回撮ると一致する画像を撮る。
# Game は読み込みと大きな球の生成が終わるまで待ってから、経過時間を 1/60 秒の固定刻みにして、TAA の揺らしの列・
# RTGI の乱数の列と履歴・自動露出の順応・大きな球の自転をそこから数え直し、決まった描画フレーム数の後に撮る
# （描画は GameThread で1フレームずつ行う）。metrics.json に deterministic=true を書く。
# 同じコードの2回の撮影は -CompareDeterministicWith で比べる: 後の撮影に前の出力先を与えると、視点ごとの平均輝度の差
# （既定 0.1 以下）と PSNR（既定 45 dB 以上）を deterministic_comparison として metrics.json へ書く。
# 撮り直さず既存の2つの出力先だけを比べるときは -CompareOnly を足す。
# -StillRenderedFrames・-GpuTimingFrames（連続撮影・計測）とは併用しない。
# -OrbitDegreesPerSecond とは併用できる: 旋回の角度は読み込み完了（エポック）からの固定刻みの時間で決まるので、
# -OrbitRenderedFrames の各時点を同じ画像で撮り直せ、-MegaOcclusion Off の撮影と画素で比べられる（-CompareDeterministicWith）。
#
# -OcclusionViews を足すと、遮蔽カリングの確認用の視点 occ-sphere（大きな球が岩を隠す）・occ-cottage（小屋が球と岩を隠す）・
# occ-cottage-edge（小屋の端が球の一部を隠す）と、旋回の出発点 occ-sphere-orbit・occ-cottage-orbit を撮る視点へ加える（-ViewNames にこれらの名前を直接与えてもよい）。
#
# -StressTextures でテクスチャの負荷モード（--stress-textures: 地面の外側へ、負荷用の材質 24 種を貼った板を格子に並べ、
# カメラの軸を格子の中心へ移す）の default・low と、格子を見下ろす top（0,80,70）を撮る。負荷用のテクスチャは
# Scripts/FetchPolyHavenTextures.ps1 -StressSet で落とし、CookAssets で焼く。-VramBudgetMb（--vram-budget-mb）で
# VRAM の上限を人工的に下げ、各撮影のログの VRAM_POOLS（cap_mb・vt_target_mb・vt_used_mb・vt_evicted_tiles）を
# metrics.json へ書く。最後の VT の使用量（vt_used_mb_last）が目標（vt_target_mb）を超えたまま終わったか、負荷用の材質がそろっていなければ失敗にする
# （目標が縮んだ直後の1回の確認の間だけ使用量が超えることがあるので、最大 vt_used_mb_max は失敗にせず書くだけ）。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [ValidateRange(10, 3600)]
    [int]$TimeoutSeconds = 300,
    # 仰角・EV100 の並びは、-File で起動しても1つの文字列で届くため「10,45,3」の形で受けて分ける。
    [string[]]$SunElevations = @(),
    [ValidateRange(-180.0, 180.0)]
    [Nullable[double]]$SunAzimuth = $null,
    [string[]]$ExposureEV100s = @(),
    [ValidateRange(-360.0, 360.0)]
    [double]$OrbitDegreesPerSecond = 0.0,
    # 回している途中を撮る描画フレーム数の並び（「60,75,90」の形）。-OrbitDegreesPerSecond と併せて使う。
    [string[]]$OrbitRenderedFrames = @(),
    [ValidateSet('TAA', 'FXAA')]
    [string]$AntiAliasing = 'TAA',
    # 内部解像度の倍率（0.5〜1）。1未満なら画面解像度×倍率で描いて拡大する（--render-scale）。
    [ValidateRange(0.5, 1.0)]
    [double]$RenderScale = 1.0,
    # 大きな球を囲む箱をデバッグの線で描く（--debug-draw-test-lines。最終解像度で描かれることの確認用）。
    [switch]$DebugDrawTestLines,
    # 高さフォグの地面での密度（1/m）。省略時は起動画面の既定、0 でフォグ無し（撮り比べ用）。
    [ValidateRange(0.0, 1.0)]
    [Nullable[double]]$HeightFogDensity = $null,
    # 高さフォグの高さ方向の減衰（1/m）。省略時は起動画面の既定。
    [ValidateRange(0.0, 1.0)]
    [Nullable[double]]$HeightFogFalloff = $null,
    # 夜の条件で撮る（太陽が無いので -SunElevations・-ExposureEV100s とは併用しない）。
    [switch]$Night,
    # カメラを止めたまま撮る描画フレーム数の並び（「60,66,72」の形）。-OrbitDegreesPerSecond とは併用しない。
    [string[]]$StillRenderedFrames = @(),
    # 起動画面の RTGI（既定は有効）。Off で環境光（IBL）だけで撮る。
    [ValidateSet('On', 'Off')]
    [string]$Rtgi = 'On',
    # 時間方向の雑音を比べる別の撮影の出力先（metrics.json のあるディレクトリ）。
    [string]$CompareNoiseWith = '',
    [ValidateRange(1.0, 100.0)]
    [double]$NoiseRatioLimit = 2.0,
    # GPU のフレーム時間を測るときの、落ち着いてから撮るまでの描画フレーム数（0 で測らない）。
    [ValidateRange(0, 100000)]
    [int]$GpuTimingFrames = 0,
    # GPU のフレーム時間の予算（ms）。超えても失敗にはせず、metrics.json に within_budget として書く。
    [ValidateRange(0.1, 1000.0)]
    [double]$GpuFrameBudgetMs = 16.6,
    # 既定視点のカメラ（「yaw,pitch,arm」）。省略時は起動時の既定のカメラ。変更前の版と同じ視点で撮り比べる用。
    [string]$DefaultCamera = '',
    # 撮る視点の名前（「default,near」の形）。省略時は3視点すべて。
    [string[]]$ViewNames = @(),
    # クック済みのテクスチャを使わず、ばらの元画像を無圧縮で読んで撮る（--no-cooked-textures。比べる側の撮影用）。
    [switch]$LooseTextures,
    # 決定的な撮影（--capture-deterministic）で撮る。同じコードを2回撮ると一致する（見た目の保全を数値で比べる用）。
    [switch]$Deterministic,
    # テクスチャの負荷モード（--stress-textures）で default・low・top の3視点を撮る。-ViewNames で絞れる。
    [switch]$StressTextures,
    # 遮蔽カリングの確認用の視点（occ-sphere・occ-cottage・occ-cottage-edge・旋回の出発点 occ-sphere-orbit・occ-cottage-orbit）を撮る視点へ加える。
    [switch]$OcclusionViews,
    # VRAM の上限（MB。--vram-budget-mb）。0 は渡さない。
    [ValidateRange(0, 1048576)]
    [int]$VramBudgetMb = 0,
    # MegaGeometry（岩・小屋など）の遮蔽カリング（2パス。既定は有効）。Off は遮蔽の判定なしの従来の経路で撮る
    # （--mega-occlusion=off。見た目の比較用）。各撮影のログの MEGA_OCCLUSION を metrics.json の mega_occlusion へ書く。
    [ValidateSet('On', 'Off')]
    [string]$MegaOcclusion = 'On',
    # 同じコードを -Deterministic で撮った別の出力先。各視点の平均輝度の差と PSNR を求めて metrics.json へ書き、
    # 平均輝度の差が -DeterministicMeanLuminanceLimit を超えるか PSNR が -DeterministicPsnrLimit を下回れば失敗にする。
    [string]$CompareDeterministicWith = '',
    [ValidateRange(0.0, 255.0)]
    [double]$DeterministicMeanLuminanceLimit = 0.1,
    [ValidateRange(0.0, 100.0)]
    [double]$DeterministicPsnrLimit = 45.0,
    # 撮影せず、OutDir に撮った既存の画像を -CompareDeterministicWith と比べて metrics.json へ書き足す。
    [switch]$CompareOnly,
    # Game へそのまま渡す引数（空白で区切る。例: --texture-asset-root と --texture-asset-manifest で別のクック済みの出力を使う）。
    [string[]]$ExtraGameArguments = @()
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$gamePath = Join-Path $repoRoot "build\Game\$Configuration\Game.exe"
$outRoot = if ([IO.Path]::IsPathRooted($OutDir)) { [IO.Path]::GetFullPath($OutDir) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir)) }

# 視点: Camera が空なら起動時の既定のカメラのまま撮る。値は --startup-camera=<yaw>,<pitch>,<arm>。
# NoiseRegions は -StillRenderedFrames の時間方向の雑音を測る静止した地面の領域（名前と、画像の幅・高さに
# 対する割合で 左,上,右,下）。物体・光源の球の写る範囲は避ける。
$nearGroundRegion = [pscustomobject]@{ Name = 'near'; Rect = @(0.05, 0.80, 0.95, 0.98) }
$views = @(
    [pscustomobject]@{ Name = 'default'; Camera = ''; NoiseRegions = @(
        $nearGroundRegion,
        [pscustomobject]@{ Name = 'middle'; Rect = @(0.05, 0.65, 0.95, 0.80) },
        # 地平線寄り（小屋と光源の球を避けた左側）
        [pscustomobject]@{ Name = 'far'; Rect = @(0.05, 0.33, 0.35, 0.42) }) },
    # 球（中心 y=0.5・半径1）へ寄り、輪郭が画面の中央の周りに来る視点
    [pscustomobject]@{ Name = 'near'; Camera = '0,5,2.5'; NoiseRegions = @($nearGroundRegion) },
    # カメラの高さ約 -0.84（地面は y=-1）から地面すれすれに見る視点
    [pscustomobject]@{ Name = 'low'; Camera = '20,-8,6'; NoiseRegions = @(
        $nearGroundRegion,
        # 物体の接地の少し下から手前の帯まで
        [pscustomobject]@{ Name = 'middle'; Rect = @(0.05, 0.65, 0.95, 0.80) }) }
)

# 「10,45,3」や配列で渡された数の並びを、範囲を確かめて double の配列にする。
function ConvertTo-NumberList([string[]]$Values, [double]$Minimum, [double]$Maximum, [string]$Name)
{
    $numbers = @()
    foreach ($item in ($Values -join ',').Split(',', [StringSplitOptions]::RemoveEmptyEntries))
    {
        $number = 0.0
        if (-not [double]::TryParse($item.Trim(), [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            $number -lt $Minimum -or $number -gt $Maximum)
        {
            Write-Host "CAPTURE_STARTUP_SCENE result=fail reason=invalid_$Name value=$item（$Minimum〜$Maximum）"
            exit 1
        }
        $numbers += $number
    }
    return ,$numbers
}

if ($DefaultCamera -ne '')
{
    $views[0].Camera = $DefaultCamera
}
# 遮蔽カリングの確認用の視点（カメラの軸は原点の大きな球、岩は (3,-0.93,0)、小屋は (0,-1,-18) を中心とする
# 幅12.4 m・奥行き14.7 m・高さ6.8 m。小屋の外から見るので腕の長さは小屋の奥の面（z=-25.4）より長くする）。
# 既定では撮らず、-OcclusionViews か -ViewNames で名前を与えたときだけ撮る（既存の撮影の視点の並びを変えない）。
$occlusionViewList = @(
    # 球の -X 側の低い位置から +X の向きに見る。岩が球の真後ろに入り、球に隠れる。
    [pscustomobject]@{ Name = 'occ-sphere'; Camera = '-90,4,9'; NoiseRegions = @() },
    # 小屋の奥（-Z 側）から +Z の向きに見る。球と岩が小屋の真後ろに入り、小屋に隠れる。
    [pscustomobject]@{ Name = 'occ-cottage'; Camera = '182,3,29'; NoiseRegions = @() },
    # 球の中心と小屋の角（6.2,-10.65）を結ぶ線の延長から見る。小屋の端が球の一部を隠す（隠れる・見えるの境目）。
    [pscustomobject]@{ Name = 'occ-cottage-edge'; Camera = '150,3,29'; NoiseRegions = @() },
    # 旋回（-OrbitDegreesPerSecond）の出発点。旋回の途中で、岩が球の陰に入る・出る向き（球の -X 側）から始める。
    [pscustomobject]@{ Name = 'occ-sphere-orbit'; Camera = '-100,4,9'; NoiseRegions = @() },
    # 旋回の途中で、球と岩が小屋の端の陰に入る向きから始める。
    [pscustomobject]@{ Name = 'occ-cottage-orbit'; Camera = '146,3,29'; NoiseRegions = @() }
)
if ($OcclusionViews)
{
    $views = @($views) + $occlusionViewList
}
if ($StressTextures)
{
    # 負荷モードはカメラの軸が格子の中心なので、近接（球が無い）は撮らず、格子の全体を見下ろす視点を足す。
    $views = @($views | Where-Object { $_.Name -ne 'near' })
    $views += [pscustomobject]@{ Name = 'top'; Camera = '0,80,70'; NoiseRegions = @() }
}
$viewNameList = @(($ViewNames -join ',').Split(',', [StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object { $_.Trim() })
if ($viewNameList.Count -gt 0)
{
    # 遮蔽カリングの確認用の視点は名前を与えれば撮れる（-OcclusionViews を併せて与えなくてよい）。
    $views = @($views) + @($occlusionViewList | Where-Object { $_.Name -in $viewNameList -and $_.Name -notin @($views | ForEach-Object { $_.Name }) })
    $unknownViews = @($viewNameList | Where-Object { $_ -notin $views.Name })
    if ($unknownViews.Count -gt 0)
    {
        Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=unknown_view value=$($unknownViews -join ',')（default・near・low・top・occ-sphere・occ-cottage・occ-cottage-edge・occ-sphere-orbit・occ-cottage-orbit）"
        exit 1
    }
    $views = @($views | Where-Object { $_.Name -in $viewNameList })
}

$sunElevationList = ConvertTo-NumberList $SunElevations 0.0 90.0 'sun_elevation'
$exposureList = ConvertTo-NumberList $ExposureEV100s -6.0 24.0 'exposure_ev100'
$orbitFrameList = ConvertTo-NumberList $OrbitRenderedFrames 1.0 100000.0 'orbit_rendered_frames'
if ($orbitFrameList.Count -gt 0 -and $OrbitDegreesPerSecond -eq 0.0)
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=orbit_frames_without_orbit（-OrbitRenderedFrames は -OrbitDegreesPerSecond と併せて使う）"
    exit 1
}
if ($OrbitDegreesPerSecond -ne 0.0 -and $orbitFrameList.Count -eq 0)
{
    $orbitFrameList = @(60.0, 75.0, 90.0)
}
$stillFrameList = ConvertTo-NumberList $StillRenderedFrames 1.0 100000.0 'still_rendered_frames'
if ($stillFrameList.Count -gt 0 -and $OrbitDegreesPerSecond -ne 0.0)
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=still_frames_with_orbit（-StillRenderedFrames は -OrbitDegreesPerSecond と併用しない）"
    exit 1
}
if ($GpuTimingFrames -gt 0 -and ($stillFrameList.Count -gt 0 -or $OrbitDegreesPerSecond -ne 0.0))
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=gpu_timing_with_sequence（-GpuTimingFrames は -StillRenderedFrames・-OrbitDegreesPerSecond と併用しない）"
    exit 1
}
if ($GpuTimingFrames -gt 0 -and $GpuTimingFrames -lt 100)
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=gpu_timing_frames_too_few（-GpuTimingFrames は100以上）"
    exit 1
}
if ($GpuTimingFrames -gt 0 -and $Configuration -eq 'Release')
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=gpu_timing_without_stats（Release は統計が無効で GPU のタイムスタンプを取れない。-Configuration RelWithDebInfo で測る）"
    exit 1
}
if ($Deterministic -and ($stillFrameList.Count -gt 0 -or $GpuTimingFrames -gt 0))
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=deterministic_with_sequence（-Deterministic は -StillRenderedFrames・-GpuTimingFrames と併用しない）"
    exit 1
}
if ($CompareOnly -and $CompareDeterministicWith -eq '')
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=compare_only_without_target（-CompareOnly は -CompareDeterministicWith と併せて使う）"
    exit 1
}
if ($CompareDeterministicWith -ne '' -and -not $Deterministic)
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=compare_without_deterministic（-CompareDeterministicWith は -Deterministic と併せて使う）"
    exit 1
}
if ($stillFrameList.Count -eq 1)
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=still_frames_too_few（-StillRenderedFrames は2つ以上）"
    exit 1
}

# 太陽の仰角から、晴天の手動露出（EV100）の目安を選ぶ。表の間は線形に補間する。
function Get-DefaultExposureEV100([double]$Elevation)
{
    $table = @(@(0.0, 11.0), @(3.0, 11.5), @(10.0, 13.0), @(25.0, 14.0), @(45.0, 14.6), @(90.0, 15.0))
    for ($i = 1; $i -lt $table.Count; ++$i)
    {
        if ($Elevation -le $table[$i][0])
        {
            $t = ($Elevation - $table[$i - 1][0]) / ($table[$i][0] - $table[$i - 1][0])
            return $table[$i - 1][1] + ($table[$i][1] - $table[$i - 1][1]) * $t
        }
    }
    return $table[$table.Count - 1][1]
}

if ($Night -and ($sunElevationList.Count -gt 0 -or $exposureList.Count -gt 0))
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=night_with_sun（-Night は -SunElevations・-ExposureEV100s と併用しない）"
    exit 1
}

if ($exposureList.Count -gt 0 -and $exposureList.Count -ne $sunElevationList.Count)
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=exposure_count_mismatch（-ExposureEV100s は -SunElevations と同じ数）"
    exit 1
}

# 撮影の一覧: 視点 × 太陽の仰角。仰角の指定が無ければ視点だけを起動時の太陽で撮る。
$invariant = [Globalization.CultureInfo]::InvariantCulture
$shots = @()
foreach ($view in $views)
{
    if ($sunElevationList.Count -eq 0)
    {
        $shotName = if ($Night) { "$($view.Name)-night" } else { $view.Name }
        $shots += [pscustomobject]@{ Name = $shotName; Camera = $view.Camera; NoiseRegions = $view.NoiseRegions; SunElevation = $null; ExposureEV100 = $null }
        continue
    }
    for ($i = 0; $i -lt $sunElevationList.Count; ++$i)
    {
        $elevation = $sunElevationList[$i]
        $ev = if ($exposureList.Count -gt 0) { $exposureList[$i] } else { Get-DefaultExposureEV100 $elevation }
        $shots += [pscustomobject]@{
            Name = "$($view.Name)-sun$($elevation.ToString($invariant))"
            Camera = $view.Camera
            NoiseRegions = $view.NoiseRegions
            SunElevation = $elevation
            ExposureEV100 = [math]::Round($ev, 2)
        }
    }
}

# カメラを回すときは、各撮影を1回の起動で撮り、回している途中の描画フレーム数ごとに画像を保存する
# （同じ起動の中なので TAA の履歴は撮影の間つながったまま）。最後の1枚は --capture-png、それより前は
# --capture-sequence で撮る。取得の要求は同時に1つだけなので、最後の2つの間は少なくとも8フレーム空ける。
# カメラを止めて撮るときも同じく1回の起動で続けて撮る（名前は <視点>-still）。
$sequenceFrameList = if ($stillFrameList.Count -gt 0) { $stillFrameList } else { $orbitFrameList }
$sequenceSuffix = if ($stillFrameList.Count -gt 0) { 'still' } else { 'orbit' }
if ($sequenceFrameList.Count -gt 0)
{
    $sortedOrbitFrames = @($sequenceFrameList | ForEach-Object { [int]$_ } | Sort-Object -Unique)
    if ($sortedOrbitFrames.Count -gt 1 -and
        $sortedOrbitFrames[$sortedOrbitFrames.Count - 1] - $sortedOrbitFrames[$sortedOrbitFrames.Count - 2] -lt 8)
    {
        Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=sequence_frames_too_close（-OrbitRenderedFrames・-StillRenderedFrames の最後の2つは8フレーム以上空ける）"
        exit 1
    }
    $orbitShots = @()
    foreach ($shot in $shots)
    {
        $orbitShots += [pscustomobject]@{
            Name = "$($shot.Name)-$sequenceSuffix"
            Camera = $shot.Camera
            NoiseRegions = $shot.NoiseRegions
            SunElevation = $shot.SunElevation
            ExposureEV100 = $shot.ExposureEV100
            OrbitFrames = $sortedOrbitFrames
        }
    }
    $shots = $orbitShots
}

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class StartupCaptureMetrics
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
    // 一致しない画素の割合, 1チャンネルの最大の絶対差 }。
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

    // 画像を Rec.709 の重みの輝度へ読む（display=true なら 8bit の表示値 0〜255、false なら sRGB から戻したリニア 0〜1）。
    static double[] ReadLuminance(string path, bool display, out int width, out int height)
    {
        using (var bitmap = new Bitmap(path))
        {
            width = bitmap.Width;
            height = bitmap.Height;
            var rect = new Rectangle(0, 0, width, height);
            BitmapData data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int stride = data.Stride;
                byte[] bytes = new byte[stride * height];
                Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
                double[] result = new double[width * height];
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        int i = y * stride + x * 4;
                        double b = bytes[i];
                        double g = bytes[i + 1];
                        double r = bytes[i + 2];
                        if (!display)
                        {
                            r = SrgbToLinear(r / 255.0);
                            g = SrgbToLinear(g / 255.0);
                            b = SrgbToLinear(b / 255.0);
                        }
                        result[y * width + x] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                    }
                }
                return result;
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
        }
    }

    static double SrgbToLinear(double c)
    {
        return c <= 0.04045 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4);
    }

    // 同じ視点の連続した画像で、領域（画素の範囲 [x0,x1)×[y0,y1)）の各画素の輝度の時間方向の標準偏差
    // （母標準偏差）を求め、領域の平均を返す。{ 表示の8bit, リニア, リニアの平均輝度, x0, y0, x1, y1,
    // 表示の標準偏差が1（8bitの1段）を超える画素の割合 }。
    public static double[] TemporalNoise(string[] paths, double[] regionFractions)
    {
        int width = 0;
        int height = 0;
        double[][] display = new double[paths.Length][];
        double[][] linear = new double[paths.Length][];
        for (int f = 0; f < paths.Length; ++f)
        {
            int w;
            int h;
            display[f] = ReadLuminance(paths[f], true, out w, out h);
            linear[f] = ReadLuminance(paths[f], false, out w, out h);
            if (f == 0) { width = w; height = h; }
            else if (w != width || h != height) { throw new ArgumentException("画像の寸法が揃っていない: " + paths[f]); }
        }
        int x0 = (int)Math.Floor(regionFractions[0] * width);
        int y0 = (int)Math.Floor(regionFractions[1] * height);
        int x1 = (int)Math.Ceiling(regionFractions[2] * width);
        int y1 = (int)Math.Ceiling(regionFractions[3] * height);
        double displaySum = 0.0;
        double linearSum = 0.0;
        double linearMeanSum = 0.0;
        long pixels = 0;
        long flickerPixels = 0;
        int n = paths.Length;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                int i = y * width + x;
                double dMean = 0.0;
                double lMean = 0.0;
                for (int f = 0; f < n; ++f) { dMean += display[f][i]; lMean += linear[f][i]; }
                dMean /= n;
                lMean /= n;
                double dVar = 0.0;
                double lVar = 0.0;
                for (int f = 0; f < n; ++f)
                {
                    dVar += (display[f][i] - dMean) * (display[f][i] - dMean);
                    lVar += (linear[f][i] - lMean) * (linear[f][i] - lMean);
                }
                double dStd = Math.Sqrt(dVar / n);
                displaySum += dStd;
                linearSum += Math.Sqrt(lVar / n);
                linearMeanSum += lMean;
                if (dStd > 1.0) { ++flickerPixels; }
                ++pixels;
            }
        }
        return new double[] { displaySum / pixels, linearSum / pixels, linearMeanSum / pixels, x0, y0, x1, y1,
                              (double)flickerPixels / pixels };
    }
}
'@

# Slang SDK を組み込んでいないビルドで neural_material_decode.slang が読めない場合だけを許す。
# 同じシェーダーでも別の理由の失敗や、ほかのシェーダーの失敗は検出する。
function Test-AllowedShaderFailure([string]$Line)
{
    return $Line -match 'Failed to compile shader \[neural_material_decode\.slang\]: Slang SDK not available\.'
}

function Stop-OwnedProcessTree([int]$ProcessId)
{
    & taskkill.exe /PID $ProcessId /T /F 2>&1 | Out-Null
}

if (-not $CompareOnly -and -not (Test-Path -LiteralPath $gamePath))
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=game_missing path=$gamePath"
    exit 1
}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$failures = @()
$results = @()
if ($CompareOnly)
{
    # 撮り直さず、OutDir の既存の撮影（metrics.json の views）を比べる。
    $existingMetricsPath = Join-Path $outRoot 'metrics.json'
    if (-not (Test-Path -LiteralPath $existingMetricsPath))
    {
        Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=compare_only_metrics_missing path=$existingMetricsPath"
        exit 1
    }
    $existingMetrics = Get-Content -LiteralPath $existingMetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $results = @($existingMetrics.views)
    $failures = @($existingMetrics.failures | Where-Object { $_ })
    $shots = @()
}
$temporalNoise = @()
$gpuTiming = @()
$gameLogPath = Join-Path $repoRoot 'Game.log'
foreach ($view in $shots)
{
    # 保存する画像（名前と、落ち着いてからの描画フレーム数）。カメラを回すときは1回の起動で複数枚を撮る。
    $images = @()
    if ($null -ne $view.PSObject.Properties['OrbitFrames'])
    {
        foreach ($frames in $view.OrbitFrames)
        {
            $images += [pscustomobject]@{ Name = "$($view.Name)-f$frames"; RenderedFrames = [int]$frames }
        }
    }
    else
    {
        $images += [pscustomobject]@{ Name = $view.Name; RenderedFrames = $null }
    }
    $lastImage = $images[$images.Count - 1]
    $pngPath = Join-Path $outRoot "$($lastImage.Name).png"
    $viewLogPath = Join-Path $outRoot "$($view.Name).Game.log"
    foreach ($image in $images)
    {
        Remove-Item -LiteralPath (Join-Path $outRoot "$($image.Name).png") -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $viewLogPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $gameLogPath -Force -ErrorAction SilentlyContinue

    $arguments = @("--capture-png=`"$pngPath`"")
    if ($view.Camera -ne '')
    {
        $arguments += "--startup-camera=$($view.Camera)"
    }
    if ($null -ne $view.SunElevation)
    {
        $arguments += "--sun-elevation=$($view.SunElevation.ToString($invariant))"
        $arguments += "--exposure-ev100=$($view.ExposureEV100.ToString($invariant))"
    }
    if ($null -ne $SunAzimuth)
    {
        # [Nullable[double]] の引数は値が入ると double として渡るため、.Value を経由せず変換する。
        $arguments += "--sun-azimuth=$(([double]$SunAzimuth).ToString($invariant))"
    }
    if ($OrbitDegreesPerSecond -ne 0.0)
    {
        $arguments += "--orbit-degrees-per-second=$($OrbitDegreesPerSecond.ToString($invariant))"
    }
    if ($null -ne $lastImage.RenderedFrames)
    {
        $arguments += "--exit-after-rendered-frames=$($lastImage.RenderedFrames)"
    }
    if ($images.Count -gt 1)
    {
        # 最後より前の時点は、同じ起動の中で Game が <接頭辞><フレーム数>.png として保存する。
        $sequencePrefix = Join-Path $outRoot "$($view.Name)-f"
        $sequenceFrames = @($images[0..($images.Count - 2)] | ForEach-Object { $_.RenderedFrames }) -join ','
        $arguments += "--capture-sequence=`"$sequencePrefix`""
        $arguments += "--capture-sequence-rendered-frames=$sequenceFrames"
    }
    if ($null -ne $HeightFogDensity)
    {
        $arguments += "--height-fog-density=$(([double]$HeightFogDensity).ToString($invariant))"
    }
    if ($null -ne $HeightFogFalloff)
    {
        $arguments += "--height-fog-falloff=$(([double]$HeightFogFalloff).ToString($invariant))"
    }
    if ($LooseTextures)
    {
        $arguments += '--no-cooked-textures'
    }
    if ($Deterministic)
    {
        $arguments += '--capture-deterministic'
    }
    if ($StressTextures)
    {
        $arguments += '--stress-textures'
    }
    if ($VramBudgetMb -gt 0)
    {
        $arguments += "--vram-budget-mb=$VramBudgetMb"
    }
    # 遮蔽カリングは既定で有効なので、Off のときだけ引数を渡す。
    if ($MegaOcclusion -eq 'Off')
    {
        $arguments += '--mega-occlusion=off'
    }
    foreach ($extraArgument in (($ExtraGameArguments -join ' ').Split(@(' ', ','), [StringSplitOptions]::RemoveEmptyEntries)))
    {
        $arguments += $extraArgument
    }
    if ($Night)
    {
        $arguments += '--night'
    }
    # TAA は起動画面の既定なので引数を渡さず、既定のまま撮る。
    if ($AntiAliasing -ne 'TAA')
    {
        $arguments += "--anti-aliasing=$($AntiAliasing.ToLowerInvariant())"
    }
    if ($RenderScale -lt 1.0)
    {
        $arguments += "--render-scale=$($RenderScale.ToString($invariant))"
    }
    if ($DebugDrawTestLines)
    {
        $arguments += '--debug-draw-test-lines'
    }
    $tracePath = Join-Path $outRoot "$($view.Name).trace.csv"
    if ($GpuTimingFrames -gt 0)
    {
        Remove-Item -LiteralPath $tracePath -Force -ErrorAction SilentlyContinue
        $arguments += "--trace-file=`"$tracePath`""
        $arguments += "--exit-after-rendered-frames=$GpuTimingFrames"
    }

    # RTGI を切るときは環境変数で起動画面へ伝える（起動した Game だけが受け継ぐよう、起動の直後に戻す）。
    $previousRtgiSetting = $env:NORVES_STARTUP_RTGI
    if ($Rtgi -eq 'Off')
    {
        $env:NORVES_STARTUP_RTGI = '0'
    }
    else
    {
        Remove-Item Env:NORVES_STARTUP_RTGI -ErrorAction SilentlyContinue
    }
    # アセットは作業ディレクトリからの相対パスで読むため、リポジトリのルートで起動する。
    try
    {
        $process = Start-Process -FilePath $gamePath -ArgumentList $arguments -WorkingDirectory $repoRoot -PassThru
    }
    finally
    {
        if ($null -eq $previousRtgiSetting) { Remove-Item Env:NORVES_STARTUP_RTGI -ErrorAction SilentlyContinue }
        else { $env:NORVES_STARTUP_RTGI = $previousRtgiSetting }
    }
    [void]$process.Handle
    $exitCode = $null
    if ($process.WaitForExit($TimeoutSeconds * 1000))
    {
        $process.WaitForExit()
        $exitCode = [int]$process.ExitCode
    }
    else
    {
        Stop-OwnedProcessTree $process.Id
        $failures += "$($view.Name): timeout（$TimeoutSeconds 秒で終わらなかった）"
    }

    if (Test-Path -LiteralPath $gameLogPath)
    {
        Copy-Item -LiteralPath $gameLogPath -Destination $viewLogPath -Force
    }

    if ($null -ne $exitCode -and $exitCode -ne 0)
    {
        $failures += "$($view.Name): Game の終了コードが $exitCode"
    }

    # 間接光の出どころ（rtgi・ibl など。LightingPass が切り替わりのときだけ記録する）の最後の値。
    $indirectLighting = $null
    $vramLedgerTextureMb = $null
    $cookedMissingCount = $null
    $vramPools = $null
    $occlusionStats = $null
    $stressMaterials = $null
    if (Test-Path -LiteralPath $viewLogPath)
    {
        # VRAM_POOLS（予算の割り振りと VT の使用量）。数値は "none"（上限なし）のこともある。使用量は最大と最後の値を残す。
        $poolLines = @(Select-String -LiteralPath $viewLogPath -Pattern 'VRAM_POOLS cap_mb=(\w+) non_pool_mb=(\d+) vt_target_mb=(\w+) vt_used_mb=(\d+) vt_evicted_tiles=(\d+)')
        if ($poolLines.Count -gt 0)
        {
            $lastPool = $poolLines[$poolLines.Count - 1].Matches[0].Groups
            $maxUsed = ($poolLines | ForEach-Object { [uint64]$_.Matches[0].Groups[4].Value } | Measure-Object -Maximum).Maximum
            $targetText = $lastPool[3].Value
            $vramPools = [ordered]@{
                cap_mb = $lastPool[1].Value
                non_pool_mb = [uint64]$lastPool[2].Value
                vt_target_mb = $targetText
                vt_used_mb_last = [uint64]$lastPool[4].Value
                vt_used_mb_max = [uint64]$maxUsed
                vt_evicted_tiles = [uint64]$lastPool[5].Value
                lines = $poolLines.Count
            }
        }
        # MEGA_OCCLUSION（遮蔽カリングの1フレームの数。1パス目で描いた数・2パス目で判定した数・描いた数・隠れていた数）。最後の値と、
        # 描いた数・隠れていた数の最大を残す。遮蔽カリングを使えない撮影（--mega-occlusion=off など）はログが無い。
        $occlusionLines = @(Select-String -LiteralPath $viewLogPath -Pattern 'MEGA_OCCLUSION pass1=(\d+) pass2_tested=(\d+) pass2_drawn=(\d+) occluded=(\d+)')
        if ($occlusionLines.Count -gt 0)
        {
            $lastOcclusion = $occlusionLines[$occlusionLines.Count - 1].Matches[0].Groups
            $maxOccluded = ($occlusionLines | ForEach-Object { [uint64]$_.Matches[0].Groups[4].Value } | Measure-Object -Maximum).Maximum
            $occlusionStats = [ordered]@{
                pass1 = [uint64]$lastOcclusion[1].Value
                pass2_tested = [uint64]$lastOcclusion[2].Value
                pass2_drawn = [uint64]$lastOcclusion[3].Value
                occluded = [uint64]$lastOcclusion[4].Value
                occluded_max = [uint64]$maxOccluded
                lines = $occlusionLines.Count
            }
        }
        $stressLine = @(Select-String -LiteralPath $viewLogPath -Pattern 'STRESS_TEXTURES materials=(\d+) of (\d+)')
        if ($stressLine.Count -gt 0)
        {
            $stressGroups = $stressLine[$stressLine.Count - 1].Matches[0].Groups
            $stressMaterials = [ordered]@{ present = [int]$stressGroups[1].Value; total = [int]$stressGroups[2].Value }
        }

        # テクスチャの VRAM（最後の VRAM_LEDGER）と、クック済みが無くばらで読んだテクスチャの数。
        $ledgerTextureMb = @(Select-String -LiteralPath $viewLogPath -Pattern 'VRAM_LEDGER textures=\d+ texture_mb=([0-9.]+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value })
        if ($ledgerTextureMb.Count -gt 0)
        {
            $vramLedgerTextureMb = [double]::Parse($ledgerTextureMb[$ledgerTextureMb.Count - 1], $invariant)
        }
        $cookedMissingCount = @(Select-String -LiteralPath $viewLogPath -Pattern 'TEXTURE_COOKED_MISSING path=' -SimpleMatch).Count
        $indirectSources = @(Select-String -LiteralPath $viewLogPath -Pattern 'INDIRECT_LIGHTING source=(\w+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value })
        if ($indirectSources.Count -gt 0)
        {
            $indirectLighting = $indirectSources[$indirectSources.Count - 1]
        }
        $shaderFailures = @(Select-String -LiteralPath $viewLogPath -Pattern 'Failed to compile shader' -SimpleMatch |
            Where-Object { -not (Test-AllowedShaderFailure $_.Line) })
        foreach ($line in $shaderFailures)
        {
            $failures += "$($view.Name): $($line.Line.Trim())"
        }
        if ($StressTextures)
        {
            if ($null -eq $stressMaterials -or $stressMaterials.present -ne $stressMaterials.total)
            {
                $failures += "$($view.Name): 負荷用の材質がそろっていない（STRESS_TEXTURES materials=$(if ($null -eq $stressMaterials) { 'なし' } else { "$($stressMaterials.present) of $($stressMaterials.total)" })。FetchPolyHavenTextures.ps1 -StressSet と CookAssets を実行する）"
            }
            if ($null -ne $vramPools -and $vramPools.vt_target_mb -ne 'none' -and $vramPools.vt_used_mb_last -gt [uint64]$vramPools.vt_target_mb)
            {
                $failures += "$($view.Name): VT の使用量が目標を超えたまま終わった（vt_used_mb_last=$($vramPools.vt_used_mb_last) vt_target_mb=$($vramPools.vt_target_mb)）"
            }
        }
        if ($images.Count -gt 1)
        {
            # 連続撮影の数え始め（アセットが落ち着いた描画フレーム）は --capture-png と同じでなければならない。
            $processorBaseline = @(Select-String -LiteralPath $viewLogPath -Pattern 'asset settle baseline rendered=(\d+)' |
                ForEach-Object { $_.Matches[0].Groups[1].Value })
            $sequenceBaseline = @(Select-String -LiteralPath $viewLogPath -Pattern 'SEQUENCE_CAPTURE baseline rendered=(\d+)' |
                ForEach-Object { $_.Matches[0].Groups[1].Value })
            if ($processorBaseline.Count -eq 0 -or $sequenceBaseline.Count -eq 0 -or
                $processorBaseline[$processorBaseline.Count - 1] -ne $sequenceBaseline[$sequenceBaseline.Count - 1])
            {
                $failures += "$($view.Name): 連続撮影の数え始めが --capture-png と食い違う（capture_png=$($processorBaseline -join '/') sequence=$($sequenceBaseline -join '/')）"
            }
        }
    }
    elseif ($Configuration -ne 'Release')
    {
        $failures += "$($view.Name): Game.log が無い"
    }
    # Release はログが無効（NORVES_ENABLE_LOGGING=0）で Game.log を書かないため、ログの検査を飛ばす。

    foreach ($image in $images)
    {
        $imagePath = Join-Path $outRoot "$($image.Name).png"
        if (-not (Test-Path -LiteralPath $imagePath))
        {
            $failures += "$($image.Name): PNG が無い"
            continue
        }

        $measured = [StartupCaptureMetrics]::Measure($imagePath)
        $result = [ordered]@{
            view = $image.Name
            camera = $view.Camera
            sun_elevation = $view.SunElevation
            exposure_ev100 = $view.ExposureEV100
            rendered_frames = $image.RenderedFrames
            png = "$($image.Name).png"
            game_log = if (Test-Path -LiteralPath $viewLogPath) { "$($view.Name).Game.log" } else { $null }
            width = [int]$measured[0]
            height = [int]$measured[1]
            mean_luminance = [math]::Round($measured[2], 3)
            clipped_white_ratio = [math]::Round($measured[3], 6)
            crushed_black_ratio = [math]::Round($measured[4], 6)
            indirect_lighting = $indirectLighting
            vram_ledger_texture_mb = $vramLedgerTextureMb
            cooked_missing_count = $cookedMissingCount
            vram_pools = $vramPools
            mega_occlusion = $occlusionStats
            stress_materials = $stressMaterials
        }
        $results += [pscustomobject]$result
        Write-Output ("CAPTURE_STARTUP_SCENE view={0} size={1}x{2} mean_luminance={3} clipped_white_ratio={4} crushed_black_ratio={5} indirect_lighting={6}" -f `
            $result.view, $result.width, $result.height, $result.mean_luminance, $result.clipped_white_ratio, $result.crushed_black_ratio, $result.indirect_lighting)
    }

    # トレースの最後のフレームから GPU のフレーム時間を集計する（撮影のフレームの直前2つは除く）。
    if ($GpuTimingFrames -gt 0)
    {
        if (-not (Test-Path -LiteralPath $tracePath))
        {
            $failures += "$($view.Name): トレースが無い（$tracePath）"
        }
        else
        {
            # GPU の時間は Type=GPU の行（描画したフレームごとに FrameGPU・AccelerationStructureBuild・
            # パスごとの区間。Frame 列は区間を記録したフレームの番号）から取る。CPU の時間は Frame 行のうち
            # 描画したフレームの行（RenderFrameMs > 0）から取る。行数が多いため Import-Csv を使わず1行ずつ読む。
            $header = @((Get-Content -LiteralPath $tracePath -TotalCount 1) -split ',')
            $renderFrameColumn = [array]::IndexOf($header, 'RenderFrameMs')
            $cpuFrameColumn = [array]::IndexOf($header, 'CPUFrameMs')
            $nameColumn = [array]::IndexOf($header, 'Name')
            $durationColumn = [array]::IndexOf($header, 'DurationMs')
            $cpuRows = New-Object System.Collections.Generic.List[double]
            $gpuScopesByFrame = New-Object 'System.Collections.Generic.SortedDictionary[long,object]'
            foreach ($line in [IO.File]::ReadLines($tracePath))
            {
                if ($line.StartsWith('Frame,'))
                {
                    $fields = $line -split ','
                    if ([double]::Parse($fields[$renderFrameColumn], $invariant) -le 0.0) { continue }
                    $cpuRows.Add([double]::Parse($fields[$cpuFrameColumn], $invariant))
                }
                elseif ($line.StartsWith('GPU,'))
                {
                    $fields = $line -split ','
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
            $windowCount = $GpuTimingFrames - 60
            $cpuArray = $cpuRows.ToArray()
            $cpuUsable = if ($cpuArray.Count -gt 2) { @($cpuArray[0..($cpuArray.Count - 3)]) } else { @() }
            $cpuWindow = if ($cpuUsable.Count -gt $windowCount) { @($cpuUsable[($cpuUsable.Count - $windowCount)..($cpuUsable.Count - 1)]) } else { $cpuUsable }
            $cpuSamples = @($cpuWindow | Where-Object { $_ -gt 0.0 } | Sort-Object)
            # FrameGPU を持つフレームを番号の順に並べ、撮影のフレームの直前2つを除いた最後の窓を使う。
            $gpuFrames = @($gpuScopesByFrame.Keys | Where-Object { @($gpuScopesByFrame[$_] | Where-Object { $_.Name -eq 'FrameGPU' -and $_.Ms -gt 0.0 }).Count -gt 0 })
            $gpuUsable = if ($gpuFrames.Count -gt 2) { @($gpuFrames[0..($gpuFrames.Count - 3)]) } else { @() }
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
            $gpuSamples = @($gpuWindow | ForEach-Object { $gpuFrameMs[$_] } | Sort-Object)
            if ($gpuSamples.Count -lt [math]::Min(50, $windowCount))
            {
                $failures += "$($view.Name): GPU のフレーム時間の標本が足りない（$($gpuSamples.Count) 件。統計が無効な構成か、GPU のタイムスタンプが使えない）"
            }
            else
            {
                $median = $gpuSamples[[int][math]::Floor(($gpuSamples.Count - 1) * 0.5)]
                $p95 = $gpuSamples[[int][math]::Floor(($gpuSamples.Count - 1) * 0.95)]
                $maximum = $gpuSamples[$gpuSamples.Count - 1]
                $mean = ($gpuSamples | Measure-Object -Average).Average
                $cpuMedian = if ($cpuSamples.Count -gt 0) { $cpuSamples[[int][math]::Floor(($cpuSamples.Count - 1) * 0.5)] } else { $null }
                # パスごとの中央値（窓の全フレーム）。予算を超えたフレームの内訳と比べる基準にする。
                $passMedian = @{}
                foreach ($name in $passSamples.Keys)
                {
                    $sortedPass = @($passSamples[$name] | Sort-Object)
                    $passMedian[$name] = $sortedPass[[int][math]::Floor(($sortedPass.Count - 1) * 0.5)]
                }
                $passMedianList = @($passMedian.GetEnumerator() | Sort-Object -Property Value -Descending | ForEach-Object {
                    [pscustomobject][ordered]@{ pass = $_.Key; median_ms = [math]::Round($_.Value, 3) }
                })
                # 予算を超えたフレームごとに、パスの時間・中央値からの増分・どの区間にも入らない残りを書く。
                $overBudgetFrames = @()
                foreach ($frameNumber in $gpuWindow)
                {
                    $frameMs = $gpuFrameMs[$frameNumber]
                    if ($frameMs -le $GpuFrameBudgetMs) { continue }
                    $scopes = @($gpuScopesByFrame[$frameNumber] | Where-Object { $_.Name -ne 'FrameGPU' })
                    $scopeSum = ($scopes | Measure-Object -Property Ms -Sum).Sum
                    $passes = @($scopes | Sort-Object -Property Ms -Descending | ForEach-Object {
                        $baseline = if ($passMedian.ContainsKey($_.Name)) { $passMedian[$_.Name] } else { 0.0 }
                        [pscustomobject][ordered]@{
                            pass = $_.Name
                            ms = [math]::Round($_.Ms, 3)
                            median_ms = [math]::Round($baseline, 3)
                            over_median_ms = [math]::Round($_.Ms - $baseline, 3)
                        }
                    })
                    $overBudgetFrames += [pscustomobject][ordered]@{
                        frame = $frameNumber
                        frame_gpu_ms = [math]::Round($frameMs, 3)
                        over_budget_ms = [math]::Round($frameMs - $GpuFrameBudgetMs, 3)
                        unattributed_ms = [math]::Round($frameMs - $scopeSum, 3)
                        passes = $passes
                    }
                }
                $timing = [ordered]@{
                    view = $view.Name
                    trace = "$($view.Name).trace.csv"
                    frames = $gpuSamples.Count
                    gpu_frame_ms_median = [math]::Round($median, 3)
                    gpu_frame_ms_mean = [math]::Round($mean, 3)
                    gpu_frame_ms_p95 = [math]::Round($p95, 3)
                    gpu_frame_ms_max = [math]::Round($maximum, 3)
                    cpu_frame_ms_median = if ($null -ne $cpuMedian) { [math]::Round($cpuMedian, 3) } else { $null }
                    budget_ms = $GpuFrameBudgetMs
                    # 窓のすべてのフレームが予算以内か（95 パーセンタイルだけで判定しない）。
                    within_budget = ($maximum -le $GpuFrameBudgetMs)
                    p95_within_budget = ($p95 -le $GpuFrameBudgetMs)
                    over_budget_count = $overBudgetFrames.Count
                    pass_median_ms = $passMedianList
                    over_budget_frames = $overBudgetFrames
                }
                $gpuTiming += [pscustomobject]$timing
                Write-Output ("CAPTURE_STARTUP_SCENE gpu_timing view={0} frames={1} median_ms={2} mean_ms={3} p95_ms={4} max_ms={5} cpu_median_ms={6} budget_ms={7} within_budget={8} over_budget_count={9}" -f `
                    $timing.view, $timing.frames, $timing.gpu_frame_ms_median, $timing.gpu_frame_ms_mean, $timing.gpu_frame_ms_p95,
                    $timing.gpu_frame_ms_max, $timing.cpu_frame_ms_median, $timing.budget_ms, $timing.within_budget, $timing.over_budget_count)
                Write-Output ("CAPTURE_STARTUP_SCENE gpu_pass_median view={0} {1}" -f $timing.view,
                    (($passMedianList | Select-Object -First 8 | ForEach-Object { "$($_.pass)=$($_.median_ms)" }) -join ' '))
                foreach ($over in $overBudgetFrames)
                {
                    Write-Output ("CAPTURE_STARTUP_SCENE gpu_over_budget view={0} frame={1} frame_gpu_ms={2} over_budget_ms={3} unattributed_ms={4} passes={5}" -f `
                        $timing.view, $over.frame, $over.frame_gpu_ms, $over.over_budget_ms, $over.unattributed_ms,
                        (($over.passes | Select-Object -First 6 | ForEach-Object { "{0}:{1}({2})" -f $_.pass, $_.ms, $_.over_median_ms.ToString('+0.###;-0.###;0', $invariant) }) -join ' '))
                }
            }
        }
    }

    # カメラを止めて続けて撮ったときは、静止した地面の領域で画素の時間方向の標準偏差を求める。
    if ($stillFrameList.Count -gt 0)
    {
        $stillPaths = @($images | ForEach-Object { Join-Path $outRoot "$($_.Name).png" } | Where-Object { Test-Path -LiteralPath $_ })
        if ($stillPaths.Count -ne $images.Count)
        {
            $failures += "$($view.Name): 時間方向の雑音を測る画像が揃っていない（$($stillPaths.Count)/$($images.Count)）"
        }
        else
        {
            foreach ($region in $view.NoiseRegions)
            {
                $noise = [StartupCaptureMetrics]::TemporalNoise([string[]]$stillPaths, [double[]]$region.Rect)
                $noiseResult = [ordered]@{
                    view = $view.Name
                    region = $region.Name
                    frames = $images.Count
                    region_pixels = @([int]$noise[3], [int]$noise[4], [int]$noise[5], [int]$noise[6])
                    temporal_std_display = [math]::Round($noise[0], 4)
                    temporal_std_linear = [math]::Round($noise[1], 6)
                    mean_linear = [math]::Round($noise[2], 6)
                    flicker_ratio = [math]::Round($noise[7], 4)
                }
                $temporalNoise += [pscustomobject]$noiseResult
                Write-Output ("CAPTURE_STARTUP_SCENE temporal_noise view={0} region={1} frames={2} pixels={3} std_display={4} std_linear={5} mean_linear={6} flicker_ratio={7}" -f `
                    $noiseResult.view, $noiseResult.region, $noiseResult.frames, ($noiseResult.region_pixels -join ','),
                    $noiseResult.temporal_std_display, $noiseResult.temporal_std_linear, $noiseResult.mean_linear, $noiseResult.flicker_ratio)
            }
        }
    }
}

# 別の撮影と、視点・領域ごとに時間方向の雑音（表示の標準偏差）の比を求める。
$noiseComparison = @()
if ($CompareNoiseWith -ne '')
{
    $compareRoot = if ([IO.Path]::IsPathRooted($CompareNoiseWith)) { $CompareNoiseWith } else { Join-Path $repoRoot $CompareNoiseWith }
    $compareMetricsPath = Join-Path $compareRoot 'metrics.json'
    if (-not (Test-Path -LiteralPath $compareMetricsPath))
    {
        $failures += "比べる撮影の metrics.json が無い: $compareMetricsPath"
    }
    else
    {
        $compareNoise = @((Get-Content -LiteralPath $compareMetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json).temporal_noise)
        foreach ($entry in $temporalNoise)
        {
            $other = $compareNoise | Where-Object { $_.view -eq $entry.view -and $_.region -eq $entry.region } | Select-Object -First 1
            if ($null -eq $other)
            {
                $failures += "$($entry.view)/$($entry.region): 比べる撮影に同じ領域の測定が無い"
                continue
            }
            $ratio = if ([double]$other.temporal_std_display -gt 0.0) { [double]$entry.temporal_std_display / [double]$other.temporal_std_display } else { [double]::PositiveInfinity }
            $comparison = [ordered]@{
                view = $entry.view
                region = $entry.region
                std_display = $entry.temporal_std_display
                compare_std_display = [double]$other.temporal_std_display
                ratio_display = [math]::Round($ratio, 3)
                flicker_ratio = $entry.flicker_ratio
                compare_flicker_ratio = $other.flicker_ratio
            }
            $noiseComparison += [pscustomobject]$comparison
            Write-Output ("CAPTURE_STARTUP_SCENE noise_ratio view={0} region={1} std_display={2} compare_std_display={3} ratio={4} limit={5} flicker_ratio={6} compare_flicker_ratio={7}" -f `
                $comparison.view, $comparison.region, $comparison.std_display, $comparison.compare_std_display, $comparison.ratio_display,
                $NoiseRatioLimit, $comparison.flicker_ratio, $comparison.compare_flicker_ratio)
            if (-not ($ratio -le $NoiseRatioLimit))
            {
                $failures += "$($entry.view)/$($entry.region): 時間方向の雑音が比べる撮影の $([math]::Round($ratio, 3)) 倍（上限 $NoiseRatioLimit）"
            }
        }
    }
}

# 同じコードを -Deterministic で撮った別の撮影と、視点ごとに平均輝度の差と PSNR を求める。
$deterministicComparison = @()
if ($CompareDeterministicWith -ne '')
{
    $compareImageRoot = if ([IO.Path]::IsPathRooted($CompareDeterministicWith)) { $CompareDeterministicWith } else { Join-Path $repoRoot $CompareDeterministicWith }
    if (-not (Test-Path -LiteralPath (Join-Path $compareImageRoot 'metrics.json')))
    {
        $failures += "比べる撮影の metrics.json が無い: $compareImageRoot"
    }
    else
    {
        foreach ($entry in $results)
        {
            $thisPng = Join-Path $outRoot $entry.png
            $otherPng = Join-Path $compareImageRoot $entry.png
            if (-not (Test-Path -LiteralPath $thisPng) -or -not (Test-Path -LiteralPath $otherPng))
            {
                $failures += "$($entry.view): 比べる画像が揃っていない（$thisPng / $otherPng）"
                continue
            }
            $difference = [StartupCaptureMetrics]::Compare($thisPng, $otherPng)
            $meanDifference = [math]::Abs($difference[0] - $difference[1])
            $withinLimits = ($meanDifference -le $DeterministicMeanLuminanceLimit) -and ($difference[2] -ge $DeterministicPsnrLimit)
            $comparison = [ordered]@{
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
            $deterministicComparison += [pscustomobject]$comparison
            Write-Output ("CAPTURE_STARTUP_SCENE deterministic_comparison view={0} mean_luminance={1} compare_mean_luminance={2} mean_luminance_difference={3} (limit {4}) psnr_db={5} (limit {6}) mismatched_pixel_ratio={7} max_channel_difference={8} within_limits={9}" -f `
                $comparison.view, $comparison.mean_luminance, $comparison.compare_mean_luminance, $comparison.mean_luminance_difference,
                $DeterministicMeanLuminanceLimit, $comparison.psnr_db, $DeterministicPsnrLimit, $comparison.mismatched_pixel_ratio,
                $comparison.max_channel_difference, $comparison.within_limits)
            if (-not $withinLimits)
            {
                $failures += "$($entry.view): 同じコードの2回の撮影が一致しない（平均輝度の差 $($comparison.mean_luminance_difference) / 上限 $DeterministicMeanLuminanceLimit、PSNR $($comparison.psnr_db) dB / 下限 $DeterministicPsnrLimit dB）"
            }
        }
    }
}

$metricsPath = Join-Path $outRoot 'metrics.json'
$metrics = [ordered]@{
    configuration = $Configuration
    deterministic = [bool]$Deterministic
    compare_deterministic_with = $CompareDeterministicWith
    deterministic_mean_luminance_limit = $DeterministicMeanLuminanceLimit
    deterministic_psnr_limit = $DeterministicPsnrLimit
    deterministic_comparison = $deterministicComparison
    orbit_degrees_per_second = $OrbitDegreesPerSecond
    orbit_rendered_frames = $orbitFrameList
    anti_aliasing = $AntiAliasing
    render_scale = $RenderScale
    debug_draw_test_lines = [bool]$DebugDrawTestLines
    night = [bool]$Night
    rtgi = $Rtgi
    still_rendered_frames = $stillFrameList
    views = $results
    temporal_noise = $temporalNoise
    compare_noise_with = $CompareNoiseWith
    noise_ratio_limit = $NoiseRatioLimit
    noise_comparison = $noiseComparison
    gpu_timing_frames = $GpuTimingFrames
    gpu_timing = $gpuTiming
    failures = $failures
}
[IO.File]::WriteAllText($metricsPath, ($metrics | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))

if ($failures.Count -gt 0)
{
    foreach ($failure in $failures)
    {
        Write-Output "CAPTURE_STARTUP_SCENE failure: $failure"
    }
    Write-Output "CAPTURE_STARTUP_SCENE result=fail metrics=$metricsPath"
    exit 1
}
Write-Output "CAPTURE_STARTUP_SCENE result=pass metrics=$metricsPath"
exit 0
