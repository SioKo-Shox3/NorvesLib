# 起動画面（Rendering3DTest）を既定・近接・低角度の3視点で撮影し、各PNGと画素の数値を OutDir へ書き出す。
# Game.exe を --capture-png と --startup-camera 付きで起動し、アセットの読み込みが落ち着いた後の最終出力
# （ImGui・画面空間のボードを含まない）を保存させる。数値は 8bit の表示値から求める:
#   mean_luminance      … Rec.709 の重み（0.2126R+0.7152G+0.0722B）で求めた画素値の平均（0〜255）
#   clipped_white_ratio … R・G・B がすべて 255 の画素の割合
#   crushed_black_ratio … R・G・B がすべて 0 の画素の割合
# Game の終了コードが0でない、PNGが無い、Game.log にシェーダーのコンパイル失敗がある場合は終了コード1を返す。
# Slang SDK 未設定の neural_material_decode.slang のコンパイル失敗だけは既知として除外する。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [ValidateRange(10, 3600)]
    [int]$TimeoutSeconds = 300
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
foreach ($view in $views)
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
