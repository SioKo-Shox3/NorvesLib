# 指定のシーンを Game.exe で撮り、視点ごとの PNG と画素の数値（metrics.json）を OutDir へ書き出す。
# Game を --scene=<id> --capture-png --capture-deterministic 付きで起動する。決定的な撮影は、シーンの組み立てが終わった
# 印（SCENE_READY）を待ってから数え始めるので、シーンごとの待ち時間の調整は要らない。
#   mean_luminance      … Rec.709 の重み（0.2126R+0.7152G+0.0722B）で求めた画素値の平均（0〜255）
#   clipped_white_ratio … R・G・B がすべて 255 の画素の割合
#   crushed_black_ratio … R・G・B がすべて 0 の画素の割合
#
# -Views を省くと、そのシーンの全視点を撮る。まず既定の視点を default.png に撮り、Game が --scene-report に書く視点の一覧
# （views=）から残りの視点を <名前>.png に撮る（既定の視点の実際の名前は metrics.json の scene_view に入る）。
# -Views を与えると、その視点だけを --scene-view=<名前> で1回ずつ起動して撮る（視点の名前は Game/Scenes/SceneCatalog.h のシーンの定義）。
# -Width と -Height は両方を与えると --window-size=<幅>x<高さ> で窓とバックバッファの大きさを替える。
# -GameArgs は Game へ足す引数を、先頭の -- を除いてカンマ区切りで与える（例: -GameArgs night,sun-elevation=10）。
#   値にカンマを含む引数（startup-camera=0,5,2.5）は、カンマの次が英字で始まらない限り分けない。
# -RequireAssets を付けると、資産の欠けを失敗にする。付けないときは欠けを metrics.json の missing_assets に数えて警告として出す。
#   欠けの検出は2つ。シーンの定義の必要な資産は Game が --scene-report で書く報告（<視点>.SceneReport.txt）から読むので、
#   ログを書かない Release でも検査できる。Rendering3DTest が読み込み失敗で出す印（COOKED_MODEL_MISSING など）は
#   Game.log からだけ読むので、Debug・RelWithDebInfo で効く。
# -GpuTimingFrames を与えると、決定的な撮影ではなく実時間の計測にする。Game を --trace-file 付きで起動し、落ち着いてから
# その描画フレーム数の後に撮る（--exit-after-rendered-frames）。トレースの FrameGPU（GPU のタイムスタンプで測ったフレームの
# 区間）の最後の (-GpuTimingFrames − 60) フレーム（撮影のフレームの直前2つを除く）の中央値・平均・95 パーセンタイル・最大と、
# パスごとの中央値を gpu_timing として metrics.json へ書く（CaptureStartupScene.ps1 の -GpuTimingFrames と同じ測り方）。
# 決定的な撮影ではないので、SCENE_READY の照合はしない（シーンの組み立ての完了は待たず、描画フレーム数で落ち着かせる）。
# 同じ撮影のログから、描いたクラスタ数（MEGA_OCCLUSION の pass1 + pass2_drawn の最後の行）・MegaGeometry のバッファの大きさ
# （MEGA_CAPACITY）・ジオメトリのプールの使用量（VRAM_POOLS の geometry_used_mb）を mega_stats として書く。
# 統計が無効な Release は GPU のタイムスタンプを取れないので併用しない（RelWithDebInfo で測る）。
# gpu_timing には CPU 側も入る。ゲームスレッド・描画スレッドのフレーム時間（game_thread_ms_* / render_thread_ms_*。median と p95）、
# パスごとの median・p95・max（pass_ms）、CPU のスコープ（トレースの Type=Scope の行）の median 上位 15（cpu_scope_median_ms）。
# CPU 側も GPU と同じ窓（最後の (-GpuTimingFrames − 60) フレームから撮影の直前2つを除く）で数える。
#
# -CompareWith <dir> を与えると、その撮影（同じ視点名の PNG）と視点ごとに比べる。平均輝度の差が -CompareMeanLuminanceLimit 以下、
# PSNR が -ComparePsnrLimit 以上なら一致（式は CaptureStartupScene.ps1 の -CompareDeterministicWith と同じ）。
# 比べる側にも gpu_timing があれば、metrics.json の comparison に GPU・ゲームスレッド・描画スレッドの median の比も書く。
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
    [int]$TimeoutSeconds = 300,
    # GPU のフレーム時間を測るときの、落ち着いてから撮るまでの描画フレーム数（0 で測らず、決定的な撮影にする）。
    [ValidateRange(0, 100000)]
    [int]$GpuTimingFrames = 0,
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
# -Views を省いたときは、既定の視点を撮った後の報告（views=）で残りの視点を足す
$expandViews = ($viewNames.Count -eq 0)
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

# 昇順に並べた標本の百分位（0〜1）。標本の添字は floor((n-1)*p)（GPU のフレーム時間の既存の求め方と同じ）。
function Get-Percentile([double[]]$Sorted, [double]$Fraction)
{
    if ($Sorted.Count -eq 0) { return $null }
    return $Sorted[[int][math]::Floor(($Sorted.Count - 1) * $Fraction)]
}

# トレース（--trace-file の CSV）から GPU・CPU のフレーム時間を集計する。Type=GPU の行は描画したフレームごとの FrameGPU と
# パスごとの区間（Frame 列は区間を記録したフレームの番号）。撮影のフレームの直前2つを除いた最後の窓を使う。
# CPU 側（ゲームスレッド・描画スレッド・スコープ）は、描画したフレームの行（RenderFrameMs > 0）の窓で数える。
function Get-GpuTimingFromTrace([string]$TracePath, [int]$Frames, [double]$BudgetMs)
{
    $header = @((Get-Content -LiteralPath $TracePath -TotalCount 1) -split ',')
    $nameColumn = [array]::IndexOf($header, 'Name')
    $durationColumn = [array]::IndexOf($header, 'DurationMs')
    $renderFrameColumn = [array]::IndexOf($header, 'RenderFrameMs')
    $cpuFrameColumn = [array]::IndexOf($header, 'CPUFrameMs')
    $gameThreadColumn = [array]::IndexOf($header, 'GameThreadMs')
    $renderThreadColumn = [array]::IndexOf($header, 'RenderThreadMs')
    $cpuRows = New-Object System.Collections.Generic.List[object]
    $gpuScopesByFrame = New-Object 'System.Collections.Generic.SortedDictionary[long,object]'
    foreach ($line in [IO.File]::ReadLines($TracePath))
    {
        if ($line.StartsWith('Frame,'))
        {
            $fields = $line -split ','
            if ([double]::Parse($fields[$renderFrameColumn], $invariant) -le 0.0) { continue }
            $cpuRows.Add([pscustomobject]@{
                Frame = [long]$fields[1]
                Cpu = [double]::Parse($fields[$cpuFrameColumn], $invariant)
                GameThread = [double]::Parse($fields[$gameThreadColumn], $invariant)
                RenderThread = [double]::Parse($fields[$renderThreadColumn], $invariant)
            })
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
    $windowCount = $Frames - 60
    $cpuArray = $cpuRows.ToArray()
    $cpuUsable = if ($cpuArray.Count -gt 2) { @($cpuArray[0..($cpuArray.Count - 3)]) } else { @() }
    $cpuWindow = if ($cpuUsable.Count -gt $windowCount) { @($cpuUsable[($cpuUsable.Count - $windowCount)..($cpuUsable.Count - 1)]) } else { $cpuUsable }
    $cpuSamples = [double[]]@($cpuWindow | ForEach-Object { $_.Cpu } | Where-Object { $_ -gt 0.0 } | Sort-Object)
    $gameThreadSamples = [double[]]@($cpuWindow | ForEach-Object { $_.GameThread } | Where-Object { $_ -gt 0.0 } | Sort-Object)
    $renderThreadSamples = [double[]]@($cpuWindow | ForEach-Object { $_.RenderThread } | Where-Object { $_ -gt 0.0 } | Sort-Object)
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
    $gpuSamples = [double[]]@($gpuWindow | ForEach-Object { $gpuFrameMs[$_] } | Sort-Object)
    if ($gpuSamples.Count -lt [math]::Min(50, $windowCount))
    {
        return [pscustomobject]@{ Error = "GPU のフレーム時間の標本が足りない（$($gpuSamples.Count) 件。統計が無効な構成か、GPU のタイムスタンプが使えない）"; Timing = $null }
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

    # CPU のスコープ（Type=Scope の行）: 窓の最初〜最後のフレーム番号の範囲に記録された区間の継続時間の median の上位 15。
    # Scope 行の Frame は記録時点のゲームフレーム番号で、描画を完了したフレームの行（窓）の番号とは一致しないため、
    # 窓の行の番号に所属するかではなく連続した範囲で選ぶ。
    $scopeFrameFirst = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[0].Frame } else { [long]::MaxValue }
    $scopeFrameLast = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[$cpuWindow.Count - 1].Frame } else { [long]::MinValue }
    $scopeSamples = @{}
    foreach ($line in [IO.File]::ReadLines($TracePath))
    {
        if (-not $line.StartsWith('Scope,')) { continue }
        $frameEnd = $line.IndexOf(',', 6)
        $scopeFrame = [long]$line.Substring(6, $frameEnd - 6)
        if ($scopeFrame -lt $scopeFrameFirst -or $scopeFrame -gt $scopeFrameLast) { continue }
        $fields = $line.Split(',', 8)
        $scopeName = $fields[$nameColumn].Trim('"')
        if (-not $scopeSamples.ContainsKey($scopeName)) { $scopeSamples[$scopeName] = New-Object System.Collections.Generic.List[double] }
        $scopeSamples[$scopeName].Add([double]::Parse($fields[$durationColumn], $invariant))
    }
    $cpuScopeList = @($scopeSamples.Keys | ForEach-Object {
        $sortedScope = [double[]]@($scopeSamples[$_] | Sort-Object)
        [pscustomobject][ordered]@{
            scope = $_
            median_ms = [math]::Round((Get-Percentile $sortedScope 0.5), 3)
            p95_ms = [math]::Round((Get-Percentile $sortedScope 0.95), 3)
            samples = $sortedScope.Count
        }
    } | Sort-Object -Property median_ms -Descending | Select-Object -First 15)

    $timing = [ordered]@{
        trace = [IO.Path]::GetFileName($TracePath)
        frames = $gpuSamples.Count
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
        cpu_scope_median_ms = $cpuScopeList
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
for ($shotIndex = 0; $shotIndex -lt $shots.Count; ++$shotIndex)
{
    $shot = $shots[$shotIndex]
    $pngPath = Join-Path $outRoot "$($shot.Name).png"
    $viewLogPath = Join-Path $outRoot "$($shot.Name).Game.log"
    $viewValidationLogPath = Join-Path $outRoot "$($shot.Name).Validation.log"
    $viewReportPath = Join-Path $outRoot "$($shot.Name).SceneReport.txt"
    foreach ($stale in @($pngPath, $viewLogPath, $viewValidationLogPath, $viewReportPath, $gameLogPath))
    {
        Remove-Item -LiteralPath $stale -Force -ErrorAction SilentlyContinue
    }

    $tracePath = Join-Path $outRoot "$($shot.Name).trace.csv"
    $arguments = @("--scene=$Scene", "--capture-png=`"$pngPath`"", "--scene-report=`"$viewReportPath`"")
    if ($GpuTimingFrames -gt 0)
    {
        # 実時間の計測: 決定的な撮影にせず、トレースを取って決まった描画フレーム数の後に撮る
        Remove-Item -LiteralPath $tracePath -Force -ErrorAction SilentlyContinue
        $arguments += "--trace-file=`"$tracePath`""
        $arguments += "--exit-after-rendered-frames=$GpuTimingFrames"
    }
    else
    {
        $arguments += '--capture-deterministic'
    }
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
            elseif ($reportLine -match '^views=(.+)$' -and $expandViews -and $shotIndex -eq 0)
            {
                # 先頭が既定の視点（今撮っているもの）。残りの視点を撮る列へ足す
                $sceneViews = @($Matches[1].Trim() -split ',' | Where-Object { $_ -ne '' })
                $shot.DefaultViewName = $sceneViews[0]
                foreach ($otherView in @($sceneViews | Select-Object -Skip 1))
                {
                    [void]$shots.Add([pscustomobject]@{ Name = $otherView; SceneView = $otherView; DefaultViewName = '' })
                }
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
        # SCENE_READY は決定的な撮影のときだけ出る（実時間の計測では出ないので照合しない）
        if (-not $sceneReady -and $GpuTimingFrames -le 0)
        {
            $failures += "$($shot.Name): SCENE_READY（シーンの組み立ての完了）がログに無い"
        }
        $shaderFailures = @(Select-String -LiteralPath $viewLogPath -Pattern 'Failed to compile shader' -SimpleMatch)
        foreach ($line in $shaderFailures)
        {
            $failures += "$($shot.Name): $($line.Line.Trim())"
        }
        $unknownLines = @(Select-String -LiteralPath $viewLogPath -Pattern 'SCENE_UNKNOWN|SCENE_VIEW_UNKNOWN|SCENE_CATALOG_INVALID|CAMERA_SCRIPT_INVALID')
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

    $gpuTiming = $null
    $megaStats = $null
    if ($GpuTimingFrames -gt 0)
    {
        if (-not (Test-Path -LiteralPath $tracePath))
        {
            $failures += "$($shot.Name): トレースが無い（$tracePath）"
        }
        else
        {
            $timingResult = Get-GpuTimingFromTrace $tracePath $GpuTimingFrames $GpuFrameBudgetMs
            if ($null -ne $timingResult.Error)
            {
                $failures += "$($shot.Name): $($timingResult.Error)"
            }
            else
            {
                $gpuTiming = $timingResult.Timing
            }
        }
        if (Test-Path -LiteralPath $viewLogPath)
        {
            $megaStats = Get-MegaStatsFromLog $viewLogPath
        }
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
        scene_view = if ($shot.SceneView -ne '') { $shot.SceneView } else { $shot.DefaultViewName }
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
        gpu_timing = $gpuTiming
        mega_stats = $megaStats
    }
    $results += [pscustomobject]$result
    Write-Output ("CAPTURE_SCENE view={0} size={1}x{2} mean_luminance={3} clipped_white_ratio={4} crushed_black_ratio={5}" -f `
        $result.view, $result.width, $result.height, $result.mean_luminance, $result.clipped_white_ratio, $result.crushed_black_ratio)
    if ($null -ne $gpuTiming)
    {
        Write-Output ("CAPTURE_SCENE gpu_timing view={0} frames={1} median_ms={2} mean_ms={3} p95_ms={4} max_ms={5} cpu_median_ms={6} gt_median_ms={7} gt_p95_ms={8} rt_median_ms={9} rt_p95_ms={10} budget_ms={11} within_budget={12}" -f `
            $result.view, $gpuTiming.frames, $gpuTiming.gpu_frame_ms_median, $gpuTiming.gpu_frame_ms_mean, $gpuTiming.gpu_frame_ms_p95,
            $gpuTiming.gpu_frame_ms_max, $gpuTiming.cpu_frame_ms_median, $gpuTiming.game_thread_ms_median, $gpuTiming.game_thread_ms_p95,
            $gpuTiming.render_thread_ms_median, $gpuTiming.render_thread_ms_p95, $gpuTiming.budget_ms, $gpuTiming.within_budget)
        Write-Output ("CAPTURE_SCENE gpu_pass_median view={0} {1}" -f $result.view,
            (($gpuTiming.pass_median_ms | Select-Object -First 10 | ForEach-Object { "$($_.pass)=$($_.median_ms)" }) -join ' '))
        Write-Output ("CAPTURE_SCENE gpu_pass_p95 view={0} {1}" -f $result.view,
            (($gpuTiming.pass_ms | Select-Object -First 10 | ForEach-Object { "$($_.pass)=$($_.p95_ms)/max=$($_.max_ms)" }) -join ' '))
        Write-Output ("CAPTURE_SCENE cpu_scope_median view={0} {1}" -f $result.view,
            (($gpuTiming.cpu_scope_median_ms | Select-Object -First 15 | ForEach-Object { "$($_.scope)=$($_.median_ms)" }) -join ' '))
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
    window_size = if ($Width -gt 0) { "${Width}x${Height}" } else { $null }
    game_args = $extraArguments
    require_assets = [bool]$RequireAssets
    gpu_timing_frames = $GpuTimingFrames
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
