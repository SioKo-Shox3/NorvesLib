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
# 途中の画面を連続して撮る（動くカメラでの TAA の残像の確認用）。各視点を、アセットの読み込みが落ち着いてから
# -OrbitRenderedFrames の描画フレーム数（既定 60,75,90）の時点で1枚ずつ撮り、<視点>-orbit-f<フレーム数>.png
# として保存する。-AntiAliasing FXAA で起動画面の既定の TAA の代わりに FXAA で撮る（見比べ用）。-HeightFogDensity・-HeightFogFalloff で高さフォグの密度・減衰を起動画面の既定から替えて撮る。
# -Night で夜（--night: 空と空の太陽を消し、環境光を月明かり程度にする。露出は自動のまま）の3視点を
# <視点>-night.png として撮る。点光源の影の確認用で、-SunElevations とは併用しない。
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
    # 高さフォグの地面での密度（1/m）。省略時は起動画面の既定、0 でフォグ無し（撮り比べ用）。
    [ValidateRange(0.0, 1.0)]
    [Nullable[double]]$HeightFogDensity = $null,
    # 高さフォグの高さ方向の減衰（1/m）。省略時は起動画面の既定。
    [ValidateRange(0.0, 1.0)]
    [Nullable[double]]$HeightFogFalloff = $null,
    # 夜の条件で撮る（太陽が無いので -SunElevations・-ExposureEV100s とは併用しない）。
    [switch]$Night
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$gamePath = Join-Path $repoRoot "build\Game\$Configuration\Game.exe"
$outRoot = if ([IO.Path]::IsPathRooted($OutDir)) { [IO.Path]::GetFullPath($OutDir) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir)) }

# 視点: Camera が空なら起動時の既定のカメラのまま撮る。値は --startup-camera=<yaw>,<pitch>,<arm>。
$views = @(
    [pscustomobject]@{ Name = 'default'; Camera = '' },
    # 球（中心 y=0.5・半径1）へ寄り、輪郭が画面の中央の周りに来る視点
    [pscustomobject]@{ Name = 'near'; Camera = '0,5,2.5' },
    # カメラの高さ約 -0.84（地面は y=-1）から地面すれすれに見る視点
    [pscustomobject]@{ Name = 'low'; Camera = '20,-8,6' }
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
        $shots += [pscustomobject]@{ Name = $shotName; Camera = $view.Camera; SunElevation = $null; ExposureEV100 = $null }
        continue
    }
    for ($i = 0; $i -lt $sunElevationList.Count; ++$i)
    {
        $elevation = $sunElevationList[$i]
        $ev = if ($exposureList.Count -gt 0) { $exposureList[$i] } else { Get-DefaultExposureEV100 $elevation }
        $shots += [pscustomobject]@{
            Name = "$($view.Name)-sun$($elevation.ToString($invariant))"
            Camera = $view.Camera
            SunElevation = $elevation
            ExposureEV100 = [math]::Round($ev, 2)
        }
    }
}

# カメラを回すときは、各撮影を回している途中の描画フレーム数ごとに分ける（同じ起動条件で撮る時点だけが違う）。
if ($orbitFrameList.Count -gt 0)
{
    $orbitShots = @()
    foreach ($shot in $shots)
    {
        foreach ($frames in $orbitFrameList)
        {
            $orbitShots += [pscustomobject]@{
                Name = "$($shot.Name)-orbit-f$([int]$frames)"
                Camera = $shot.Camera
                SunElevation = $shot.SunElevation
                ExposureEV100 = $shot.ExposureEV100
                RenderedFrames = [int]$frames
            }
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
$gameLogPath = Join-Path $repoRoot 'Game.log'
foreach ($view in $shots)
{
    $pngPath = Join-Path $outRoot "$($view.Name).png"
    $viewLogPath = Join-Path $outRoot "$($view.Name).Game.log"
    Remove-Item -LiteralPath $pngPath, $viewLogPath -Force -ErrorAction SilentlyContinue
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
    if ($null -ne $view.PSObject.Properties['RenderedFrames'])
    {
        $arguments += "--exit-after-rendered-frames=$($view.RenderedFrames)"
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

    # アセットは作業ディレクトリからの相対パスで読むため、リポジトリのルートで起動する。
    $process = Start-Process -FilePath $gamePath -ArgumentList $arguments -WorkingDirectory $repoRoot -PassThru
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

    if (Test-Path -LiteralPath $viewLogPath)
    {
        $shaderFailures = @(Select-String -LiteralPath $viewLogPath -Pattern 'Failed to compile shader' -SimpleMatch |
            Where-Object { -not (Test-AllowedShaderFailure $_.Line) })
        foreach ($line in $shaderFailures)
        {
            $failures += "$($view.Name): $($line.Line.Trim())"
        }
    }
    else
    {
        $failures += "$($view.Name): Game.log が無い"
    }

    if (-not (Test-Path -LiteralPath $pngPath))
    {
        $failures += "$($view.Name): PNG が無い"
        continue
    }

    $measured = [StartupCaptureMetrics]::Measure($pngPath)
    $result = [ordered]@{
        view = $view.Name
        camera = $view.Camera
        sun_elevation = $view.SunElevation
        exposure_ev100 = $view.ExposureEV100
        rendered_frames = if ($null -ne $view.PSObject.Properties['RenderedFrames']) { $view.RenderedFrames } else { $null }
        png = "$($view.Name).png"
        width = [int]$measured[0]
        height = [int]$measured[1]
        mean_luminance = [math]::Round($measured[2], 3)
        clipped_white_ratio = [math]::Round($measured[3], 6)
        crushed_black_ratio = [math]::Round($measured[4], 6)
    }
    $results += [pscustomobject]$result
    Write-Output ("CAPTURE_STARTUP_SCENE view={0} size={1}x{2} mean_luminance={3} clipped_white_ratio={4} crushed_black_ratio={5}" -f `
        $result.view, $result.width, $result.height, $result.mean_luminance, $result.clipped_white_ratio, $result.crushed_black_ratio)
}

$metricsPath = Join-Path $outRoot 'metrics.json'
$metrics = [ordered]@{
    configuration = $Configuration
    orbit_degrees_per_second = $OrbitDegreesPerSecond
    orbit_rendered_frames = $orbitFrameList
    anti_aliasing = $AntiAliasing
    night = [bool]$Night
    views = $results
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
