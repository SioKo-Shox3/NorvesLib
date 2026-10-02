# 起動画面（Rendering3DTest）を既定・近接・低角度の3視点で撮影し、各PNGと画素の数値を OutDir へ書き出す。
# Game.exe を --capture-png と --startup-camera 付きで起動し、アセットの読み込みが落ち着いた後の最終出力
# （ImGui・画面空間のボードを含まない）を保存させる。数値は 8bit の表示値から求める:
#   mean_luminance      … Rec.709 の重み（0.2126R+0.7152G+0.0722B）で求めた画素値の平均（0〜255）
#   clipped_white_ratio … R・G・B がすべて 255 の画素の割合
#   crushed_black_ratio … R・G・B がすべて 0 の画素の割合
# Game の終了コードが0でない、PNGが無い、Game.log にシェーダーのコンパイル失敗がある場合は終了コード1を返す。
# Slang SDK 未設定の neural_material_decode.slang のコンパイル失敗だけは既知として除外する。
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
    [double]$NoiseRatioLimit = 2.0
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

if (-not (Test-Path -LiteralPath $gamePath))
{
    Write-Output "CAPTURE_STARTUP_SCENE result=fail reason=game_missing path=$gamePath"
    exit 1
}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$failures = @()
$results = @()
$temporalNoise = @()
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
    if (Test-Path -LiteralPath $viewLogPath)
    {
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
    else
    {
        $failures += "$($view.Name): Game.log が無い"
    }

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
            game_log = "$($view.Name).Game.log"
            width = [int]$measured[0]
            height = [int]$measured[1]
            mean_luminance = [math]::Round($measured[2], 3)
            clipped_white_ratio = [math]::Round($measured[3], 6)
            crushed_black_ratio = [math]::Round($measured[4], 6)
            indirect_lighting = $indirectLighting
        }
        $results += [pscustomobject]$result
        Write-Output ("CAPTURE_STARTUP_SCENE view={0} size={1}x{2} mean_luminance={3} clipped_white_ratio={4} crushed_black_ratio={5} indirect_lighting={6}" -f `
            $result.view, $result.width, $result.height, $result.mean_luminance, $result.clipped_white_ratio, $result.crushed_black_ratio, $result.indirect_lighting)
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

$metricsPath = Join-Path $outRoot 'metrics.json'
$metrics = [ordered]@{
    configuration = $Configuration
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
    failures = $failures
}
[IO.File]::WriteAllText($metricsPath, ($metrics | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding($false)))

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
