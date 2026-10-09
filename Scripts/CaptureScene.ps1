# 指定のシーンを Game.exe で撮り、視点ごとの PNG と画素の数値（metrics.json）を OutDir へ書き出す。
# Game を --scene=<id> --capture-png --capture-deterministic 付きで起動する。決定的な撮影は、シーンの組み立てが終わった
# 印（SCENE_READY）を待ってから数え始めるので、シーンごとの待ち時間の調整は要らない。
#   mean_luminance      … Rec.709 の重み（0.2126R+0.7152G+0.0722B）で求めた画素値の平均（0〜255）
#   clipped_white_ratio … R・G・B がすべて 255 の画素の割合
#   crushed_black_ratio … R・G・B がすべて 0 の画素の割合
#
# -Views を省くと既定の視点の1枚（default.png）を撮る。与えると視点ごとに --scene-view=<名前> で1回ずつ起動して
# <名前>.png を撮る（視点の名前は Game/Scenes/SceneCatalog.h のシーンの定義）。
# -Width と -Height は両方を与えると --window-size=<幅>x<高さ> で窓とバックバッファの大きさを替える。
# -GameArgs は Game へ足す引数を、先頭の -- を除いてカンマ区切りで与える（例: -GameArgs night,sun-elevation=10）。
#   値にカンマを含む引数（startup-camera=0,5,2.5）は、カンマの次が英字で始まらない限り分けない。
# -RequireAssets を付けると、資産の欠けを失敗にする。付けないときは欠けを metrics.json の missing_assets に数えて警告として出す。
#   欠けの検出は2つ。シーンの定義の必要な資産は Game が --scene-report で書く報告（<視点>.SceneReport.txt）から読むので、
#   ログを書かない Release でも検査できる。Rendering3DTest が読み込み失敗で出す印（COOKED_MODEL_MISSING など）は
#   Game.log からだけ読むので、Debug・RelWithDebInfo で効く。
#
# 失敗にするもの（終了コード 1）: Game の起動失敗・終了コードが0でない・タイムアウト、PNG が無い・大きさが違う、
# Game.log のシェーダーのコンパイル失敗（Slang SDK 未設定の neural_material_decode.slang だけは既知として除外）、
# 未知のシーン・視点の指定、シーンの報告が無い、SCENE_READY が無い、Debug の検証レイヤーのエラー。
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
    [ValidateRange(10, 3600)]
    [int]$TimeoutSeconds = 300
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
$shots = @()
if ($viewNames.Count -eq 0)
{
    $shots += [pscustomobject]@{ Name = 'default'; SceneView = '' }
}
else
{
    foreach ($viewName in $viewNames)
    {
        $shots += [pscustomobject]@{ Name = $viewName; SceneView = $viewName }
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
}
'@

function Stop-OwnedProcessTree([int]$ProcessId)
{
    & taskkill.exe /PID $ProcessId /T /F 2>&1 | Out-Null
}

# 資産の欠けを表す印（シーンの定義の SCENE_ASSET_MISSING と、Rendering3DTest が出す読み込み失敗の印）
$missingAssetPattern = 'SCENE_ASSET_MISSING|COOKED_MODEL_MISSING|COOKED_MODEL_TEXTURES_MISSING|COOKED_BIG_SPHERE_MISSING|SCAN_PROP_MISSING'

if (-not (Test-Path -LiteralPath $gamePath))
{
    Write-Output "CAPTURE_SCENE result=fail reason=game_missing path=$gamePath"
    exit 1
}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$failures = @()
$results = @()
$gameLogPath = Join-Path $repoRoot 'Game.log'
foreach ($shot in $shots)
{
    $pngPath = Join-Path $outRoot "$($shot.Name).png"
    $viewLogPath = Join-Path $outRoot "$($shot.Name).Game.log"
    $viewValidationLogPath = Join-Path $outRoot "$($shot.Name).Validation.log"
    $viewReportPath = Join-Path $outRoot "$($shot.Name).SceneReport.txt"
    foreach ($stale in @($pngPath, $viewLogPath, $viewValidationLogPath, $viewReportPath, $gameLogPath))
    {
        Remove-Item -LiteralPath $stale -Force -ErrorAction SilentlyContinue
    }

    $arguments = @("--scene=$Scene", "--capture-png=`"$pngPath`"", "--scene-report=`"$viewReportPath`"", '--capture-deterministic')
    if ($shot.SceneView -ne '')
    {
        $arguments += "--scene-view=$($shot.SceneView)"
    }
    if ($Width -gt 0)
    {
        $arguments += "--window-size=${Width}x${Height}"
    }
    $arguments += $extraArguments

    # Vulkan の検証レイヤーのメッセージ（VUID）は Game.log に入らないので、検証レイヤー自身のログ出力を視点ごとの
    # ファイルへ向けてエラーを数える（Debug だけ。他は検証レイヤーが無効）。起動した Game だけが受け継ぐよう、起動の直後に戻す。
    $previousValidationSettings = @{}
    foreach ($validationVariable in @('VK_KHRONOS_VALIDATION_DEBUG_ACTION', 'VK_KHRONOS_VALIDATION_LOG_FILENAME', 'VK_KHRONOS_VALIDATION_REPORT_FLAGS'))
    {
        $previousValidationSettings[$validationVariable] = [Environment]::GetEnvironmentVariable($validationVariable)
    }
    if ($Configuration -eq 'Debug')
    {
        $env:VK_KHRONOS_VALIDATION_DEBUG_ACTION = 'VK_DBG_LAYER_ACTION_LOG_MSG'
        $env:VK_KHRONOS_VALIDATION_LOG_FILENAME = $viewValidationLogPath
        $env:VK_KHRONOS_VALIDATION_REPORT_FLAGS = 'error,warn'
    }
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
    $exitCode = $null
    if ($process.WaitForExit($TimeoutSeconds * 1000))
    {
        $process.WaitForExit()
        $exitCode = [int]$process.ExitCode
    }
    else
    {
        Stop-OwnedProcessTree $process.Id
        $failures += "$($shot.Name): timeout（$TimeoutSeconds 秒で終わらなかった）"
    }

    if (Test-Path -LiteralPath $gameLogPath)
    {
        Copy-Item -LiteralPath $gameLogPath -Destination $viewLogPath -Force
    }
    if ($null -ne $exitCode -and $exitCode -ne 0)
    {
        $failures += "$($shot.Name): Game の終了コードが $exitCode"
    }

    # シーンの報告（--scene-report）の検査。ログとは別に Game が書くので、Release でも未知のシーン・視点と必要な資産の欠けを検査できる。
    # 報告は ResolveStartupScene が必ず書くので、Game が正常に終わったのに無いなら失敗にする。
    $missingAssets = @()
    if (Test-Path -LiteralPath $viewReportPath)
    {
        foreach ($reportLine in @(Get-Content -LiteralPath $viewReportPath -Encoding UTF8 | Where-Object { $_ -match '\S' }))
        {
            if ($reportLine -match '^(unknown_scene|unknown_view)=')
            {
                $failures += "$($shot.Name): シーンの報告: $($reportLine.Trim())"
            }
            elseif ($reportLine -match '^missing_asset=')
            {
                $missingAssets += $reportLine.Trim()
            }
        }
    }
    elseif ($null -ne $exitCode -and $exitCode -eq 0)
    {
        $failures += "$($shot.Name): シーンの報告（$($shot.Name).SceneReport.txt）が無い"
    }

    # Game.log の検査。Release はログが無効（NORVES_ENABLE_LOGGING=0）で Game.log を書かないため飛ばす。
    $sceneReady = $null
    $logMissingAssets = @()
    if (Test-Path -LiteralPath $viewLogPath)
    {
        $sceneReady = @(Select-String -LiteralPath $viewLogPath -Pattern "SCENE_READY scene=$Scene\s*$").Count -gt 0
        if (-not $sceneReady)
        {
            $failures += "$($shot.Name): SCENE_READY（シーンの組み立ての完了）がログに無い"
        }
        $shaderFailures = @(Select-String -LiteralPath $viewLogPath -Pattern 'Failed to compile shader' -SimpleMatch)
        foreach ($line in $shaderFailures)
        {
            $failures += "$($shot.Name): $($line.Line.Trim())"
        }
        $unknownLines = @(Select-String -LiteralPath $viewLogPath -Pattern 'SCENE_UNKNOWN|SCENE_VIEW_UNKNOWN|SCENE_CATALOG_INVALID')
        foreach ($line in $unknownLines)
        {
            $failures += "$($shot.Name): $($line.Line.Trim())"
        }
        # シーンの定義の欠け（SCENE_ASSET_MISSING）は報告で数えたので、ログからは Rendering3DTest の読み込み失敗の印だけを拾う。
        $logMissingAssets = @(Select-String -LiteralPath $viewLogPath -Pattern $missingAssetPattern |
            Where-Object { $_.Line -notmatch 'SCENE_ASSET_MISSING' } | ForEach-Object { $_.Line.Trim() })
    }
    elseif ($Configuration -ne 'Release')
    {
        $failures += "$($shot.Name): Game.log が無い"
    }

    $missingAssets = @($missingAssets) + @($logMissingAssets)
    foreach ($line in $missingAssets)
    {
        if ($RequireAssets)
        {
            $failures += "$($shot.Name): 資産の欠け: $line"
        }
        else
        {
            Write-Output "CAPTURE_SCENE warning view=$($shot.Name) missing_asset=$line"
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
            log = "$($shot.Name).Validation.log"
            error_count = $errorLines.Count
            warning_count = $warningLines.Count
        }
        if ($errorLines.Count -gt 0)
        {
            $failures += "$($shot.Name): 検証レイヤーのエラーが $($errorLines.Count) 件ある（$($shot.Name).Validation.log。先頭: $($errorLines[0]))"
        }
    }
    elseif ($Configuration -eq 'Debug')
    {
        $failures += "$($shot.Name): 検証レイヤーのログ（$($shot.Name).Validation.log）が無い（検証レイヤーがログの設定を読んでいない）"
    }

    if (-not (Test-Path -LiteralPath $pngPath))
    {
        $failures += "$($shot.Name): PNG が無い"
        continue
    }
    $measured = [SceneCaptureMetrics]::Measure($pngPath)
    if ($Width -gt 0 -and ([int]$measured[0] -ne $Width -or [int]$measured[1] -ne $Height))
    {
        $failures += "$($shot.Name): PNG の大きさが指定と違う（$([int]$measured[0])x$([int]$measured[1])、指定 ${Width}x${Height}）"
    }
    $result = [ordered]@{
        view = $shot.Name
        scene_view = $shot.SceneView
        png = "$($shot.Name).png"
        scene_report = if (Test-Path -LiteralPath $viewReportPath) { "$($shot.Name).SceneReport.txt" } else { $null }
        game_log = if (Test-Path -LiteralPath $viewLogPath) { "$($shot.Name).Game.log" } else { $null }
        width = [int]$measured[0]
        height = [int]$measured[1]
        mean_luminance = [math]::Round($measured[2], 3)
        clipped_white_ratio = [math]::Round($measured[3], 6)
        crushed_black_ratio = [math]::Round($measured[4], 6)
        scene_ready = $sceneReady
        missing_assets = $missingAssets.Count
        vulkan_validation = $validationInfo
    }
    $results += [pscustomobject]$result
    Write-Output ("CAPTURE_SCENE view={0} size={1}x{2} mean_luminance={3} clipped_white_ratio={4} crushed_black_ratio={5}" -f `
        $result.view, $result.width, $result.height, $result.mean_luminance, $result.clipped_white_ratio, $result.crushed_black_ratio)
}

$metrics = [ordered]@{
    scene = $Scene
    configuration = $Configuration
    window_size = if ($Width -gt 0) { "${Width}x${Height}" } else { $null }
    game_args = $extraArguments
    require_assets = [bool]$RequireAssets
    views = $results
    failures = $failures
}
$metricsPath = Join-Path $outRoot 'metrics.json'
$metricsJson = $metrics | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($metricsPath, $metricsJson, (New-Object System.Text.UTF8Encoding($false)))

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
