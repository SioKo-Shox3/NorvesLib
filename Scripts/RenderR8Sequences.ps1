# R8の屋内・屋外の連番（既定: 240フレーム・24 fps・1280×720・1024 spp）を build/R8Sequences/<scene>/ へ書き出し、
# EXR連番の検証exe（欠番・寸法・非有限の画素・ポッピング）で検査する。書き出し済みのフレームは飛ばし、
# 欠けている範囲だけをChunkFrames枚ずつ描くので、中断しても同じコマンドで続きから再開できる。
# 描画のプロセスはCPUの優先度を下げて動かす。所要時間・容量・検査の結果を .harness/runs/<日付>-r8-sequences/ へ残す。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('indoor', 'outdoor')]
    [string]$Scene,
    [ValidateRange(1, 100000)]
    [int]$Frames = 240,
    [ValidateRange(16, 7680)]
    [int]$Width = 1280,
    [ValidateRange(16, 4320)]
    [int]$Height = 720,
    [ValidateRange(8, 8192)]
    [int]$Spp = 1024,
    [ValidateRange(0, 255)]
    [int]$Seed = 0,
    [ValidateRange(1, 1000)]
    [int]$ChunkFrames = 24,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
if ($Spp % 8 -ne 0) {
    throw "Spp must be a multiple of 8 (got $Spp)."
}

$repository = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repository 'build'
$binaryRoot = Join-Path $buildRoot "Test/Core/Rendering/$Configuration"
$renderer = Join-Path $binaryRoot 'R8SequenceRenderer.exe'
$validator = Join-Path $binaryRoot 'R8ExrSequenceValidator.exe'
foreach ($executable in @($renderer, $validator)) {
    if (-not (Test-Path -LiteralPath $executable)) {
        throw "Missing $executable. Build first: cmake --build build --config $Configuration --target R8SequenceRenderer R8ExrSequenceValidator"
    }
}

$outputDirectory = Join-Path $buildRoot "R8Sequences/$Scene"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$runDirectory = Join-Path $repository (".harness/runs/{0}-r8-sequences" -f (Get-Date -Format 'yyyyMMdd'))
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

function Get-FrameFileName([int]$Frame) {
    return ('{0}_seed{1:x8}_spp{2:d6}_frame{3:d6}.exr' -f $Scene, $Seed, $Spp, $Frame)
}

# 同じscene・seed・sppで別の寸法の連番が残っていれば、完成したフレームとして使わず止める。
foreach ($manifest in @(Get-ChildItem -LiteralPath $outputDirectory -Filter "$Scene`_manifest_frames*.txt" -File)) {
    $fields = @{}
    foreach ($line in Get-Content -LiteralPath $manifest.FullName) {
        $parts = @($line -split '=', 2)
        if ($parts.Count -eq 2) {
            $fields[$parts[0]] = $parts[1]
        }
    }
    if ($fields['spp'] -eq "$Spp" -and $fields['seed'] -eq "$Seed" -and
        ($fields['width'] -ne "$Width" -or $fields['height'] -ne "$Height")) {
        throw ("{0} has a {1}x{2} sequence with the same scene, seed and spp. Remove {3} before rendering {4}x{5}." -f `
                $manifest.Name, $fields['width'], $fields['height'], $outputDirectory, $Width, $Height)
    }
}

# 中断された書き出しの一時ファイル（完成前のEXR）を消す。
Get-ChildItem -LiteralPath $outputDirectory -Filter 'pt*.tmp' -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

$missing = [Collections.Generic.List[int]]::new()
for ($frame = 0; $frame -lt $Frames; ++$frame) {
    if (-not (Test-Path -LiteralPath (Join-Path $outputDirectory (Get-FrameFileName $frame)))) {
        $missing.Add($frame)
    }
}
Write-Host ("r8_sequences scene={0} frames={1} missing={2} size={3}x{4} spp={5}" -f `
        $Scene, $Frames, $missing.Count, $Width, $Height, $Spp)

# 欠けている番号を連続する範囲にまとめ、ChunkFrames枚ずつ描く。
$chunks = [Collections.Generic.List[object]]::new()
$index = 0
while ($index -lt $missing.Count) {
    $start = $missing[$index]
    $count = 1
    while ($index + $count -lt $missing.Count -and $missing[$index + $count] -eq $start + $count -and
        $count -lt $ChunkFrames) {
        ++$count
    }
    $chunks.Add([pscustomobject]@{ First = $start; Count = $count })
    $index += $count
}

$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$renderedFrames = 0
foreach ($chunk in $chunks) {
    $tag = '{0}-frames{1:d6}-{2:d6}' -f $Scene, $chunk.First, ($chunk.First + $chunk.Count - 1)
    $standardOutput = Join-Path $runDirectory "$tag.out.txt"
    $standardError = Join-Path $runDirectory "$tag.err.txt"
    $arguments = @(
        "--r8-seq-scene=$Scene",
        "--r8-seq-out=$($outputDirectory -replace '\\', '/')",
        "--r8-seq-first=$($chunk.First)",
        "--r8-seq-count=$($chunk.Count)",
        "--r8-seq-width=$Width",
        "--r8-seq-height=$Height",
        "--r8-seq-spp=$Spp",
        "--r8-seq-seed=$Seed")
    Write-Host "render $tag"
    $process = Start-Process -FilePath $renderer -ArgumentList $arguments -WorkingDirectory $buildRoot `
        -RedirectStandardOutput $standardOutput -RedirectStandardError $standardError -NoNewWindow -PassThru
    $null = $process.Handle
    try {
        $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal
    }
    catch {
        Write-Warning "描画のプロセスの優先度を下げられませんでした: $($_.Exception.Message)"
    }
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    if ($exitCode -eq 125) {
        Write-Host "R8SequenceRenderer skipped (GPU unavailable)"
        exit 125
    }
    if ($exitCode -ne 0) {
        Get-Content -LiteralPath $standardOutput -Tail 20 -ErrorAction SilentlyContinue | Write-Host
        throw "R8SequenceRenderer failed for $tag (exit $exitCode). Log: $standardOutput"
    }
    $renderedFrames += $chunk.Count
}
$stopwatch.Stop()

$validationOutput = Join-Path $runDirectory "$Scene-validation.txt"
$validationArguments = @(
    "--dir=$($outputDirectory -replace '\\', '/')",
    "--scene=$Scene",
    "--frames=$Frames",
    '--first=0',
    "--width=$Width",
    "--height=$Height")
& $validator @validationArguments 2>&1 | Tee-Object -FilePath $validationOutput | Write-Host
$validationExit = $LASTEXITCODE

$files = @(Get-ChildItem -LiteralPath $outputDirectory -Filter "$Scene`_seed*_frame*.exr" -File)
$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
if ($null -eq $totalBytes) {
    $totalBytes = 0
}
$summary = @(
    "time=$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')",
    "scene=$Scene",
    "frames=$Frames",
    "size=${Width}x${Height}",
    "spp=$Spp",
    "seed=$Seed",
    "configuration=$Configuration",
    "rendered_this_run=$renderedFrames",
    ("render_seconds_this_run={0:F1}" -f $stopwatch.Elapsed.TotalSeconds),
    "exr_files=$($files.Count)",
    "exr_bytes=$totalBytes",
    "validation_exit=$validationExit",
    "validation_log=$validationOutput",
    '')
Add-Content -LiteralPath (Join-Path $runDirectory "$Scene-summary.txt") -Value $summary -Encoding UTF8
$summary | Write-Host
exit $validationExit
