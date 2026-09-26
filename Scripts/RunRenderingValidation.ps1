[CmdletBinding()]
param(
    [ValidateRange(1, 100)]
    [int]$Iterations = 10,
    [switch]$RequireGpu,
    [switch]$SelfTestVisualMetricsParser
)

$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$floatStyle = [Globalization.NumberStyles]::Float

function Convert-VisualMetricsLine {
    param(
        [string[]]$OutputLines,
        [string]$ExpectedScene,
        [double]$MeanLimit,
        [int]$RawLimit
    )
    $prefix = 'NORVESLIB_VISUAL_METRICS'
    $matching = @($OutputLines | Where-Object { $_ -match '^NORVESLIB_VISUAL_METRICS(?:\s|$)' })
    if ($matching.Count -ne 1) {
        throw "Expected exactly one $prefix line; found $($matching.Count)."
    }
    $tokens = @($matching[0] -split ' ' | Where-Object Length -gt 0)
    if ($tokens.Count -ne 5 -or $tokens[0] -ne $prefix) {
        throw "Malformed $prefix line."
    }
    $fields = @{}
    foreach ($token in $tokens[1..4]) {
        $parts = @($token -split '=', 2)
        if ($parts.Count -ne 2 -or $parts[0].Length -eq 0 -or $parts[1].Length -eq 0) {
            throw "Malformed field in $prefix line."
        }
        if ($fields.ContainsKey($parts[0])) {
            throw "Duplicate field '$($parts[0])' in $prefix line."
        }
        $fields[$parts[0]] = $parts[1]
    }
    foreach ($name in @('scene', 'mean_flip', 'max_flip', 'raw_max')) {
        if (-not $fields.ContainsKey($name)) {
            throw "Missing field '$name' in $prefix line."
        }
    }
    if ($fields.scene -ne $ExpectedScene) {
        throw "Scene mismatch: expected '$ExpectedScene', got '$($fields.scene)'."
    }
    [double]$mean = 0.0
    [double]$maximum = 0.0
    [int]$rawMaximum = 0
    if (-not [double]::TryParse($fields.mean_flip, $floatStyle, $invariant, [ref]$mean) -or
        -not [double]::IsFinite($mean) -or $mean -lt 0.0 -or $mean -gt 1.0) {
        throw 'mean_flip must be a finite invariant decimal in [0,1].'
    }
    if (-not [double]::TryParse($fields.max_flip, $floatStyle, $invariant, [ref]$maximum) -or
        -not [double]::IsFinite($maximum) -or $maximum -lt 0.0 -or $maximum -gt 1.0) {
        throw 'max_flip must be a finite invariant decimal in [0,1].'
    }
    if (-not [int]::TryParse($fields.raw_max, [Globalization.NumberStyles]::None,
                             $invariant, [ref]$rawMaximum) -or
        $rawMaximum -lt 0 -or $rawMaximum -gt 255) {
        throw 'raw_max must be an invariant integer in [0,255].'
    }
    if ($mean -gt $MeanLimit) {
        throw "mean_flip threshold exceeded: value=$mean limit=$MeanLimit."
    }
    if ($rawMaximum -gt $RawLimit) {
        throw "raw_max threshold exceeded: value=$rawMaximum limit=$RawLimit."
    }
    [pscustomobject]@{ Scene = $ExpectedScene; MeanFlip = $mean; MaxFlip = $maximum; RawMax = $rawMaximum }
}

function Assert-ParserRejects {
    param([string]$Name, [scriptblock]$Action)
    $rejected = $false
    try {
        & $Action | Out-Null
    } catch {
        $rejected = $true
        Write-Output "continuous_parser_negative=$Name status=rejected reason=$($_.Exception.Message)"
    }
    if (-not $rejected) {
        throw "Continuous parser negative '$Name' was accepted."
    }
}

function Invoke-VisualMetricsParserSelfTest {
    $valid = 'NORVESLIB_VISUAL_METRICS scene=indoor mean_flip=0.000001000 max_flip=0.200000000 raw_max=8'
    Convert-VisualMetricsLine @($valid) 'indoor' 0.000001 8 | Out-Null
    Assert-ParserRejects 'missing_line' {
        Convert-VisualMetricsLine @('noise') 'indoor' 0.000001 8
    }
    Assert-ParserRejects 'duplicate_line' {
        Convert-VisualMetricsLine @($valid, $valid) 'indoor' 0.000001 8
    }
    foreach ($value in @('NaN', '+Inf', '-Inf')) {
        Assert-ParserRejects "nonfinite_$value" {
            Convert-VisualMetricsLine @("NORVESLIB_VISUAL_METRICS scene=indoor mean_flip=$value max_flip=0.2 raw_max=0") 'indoor' 0.000001 8
        }
    }
    Assert-ParserRejects 'field_missing' {
        Convert-VisualMetricsLine @('NORVESLIB_VISUAL_METRICS scene=indoor mean_flip=0 max_flip=0') 'indoor' 0.000001 8
    }
    Assert-ParserRejects 'field_duplicate' {
        Convert-VisualMetricsLine @('NORVESLIB_VISUAL_METRICS scene=indoor mean_flip=0 mean_flip=0 max_flip=0 raw_max=0') 'indoor' 0.000001 8
    }
    Assert-ParserRejects 'scene_mismatch' {
        Convert-VisualMetricsLine @($valid) 'outdoor' 0.000001 8
    }
    Assert-ParserRejects 'mean_threshold' {
        Convert-VisualMetricsLine @('NORVESLIB_VISUAL_METRICS scene=indoor mean_flip=0.000002 max_flip=0.2 raw_max=0') 'indoor' 0.000001 8
    }
    Assert-ParserRejects 'raw_threshold' {
        Convert-VisualMetricsLine @('NORVESLIB_VISUAL_METRICS scene=indoor mean_flip=0 max_flip=0.2 raw_max=9') 'indoor' 0.000001 8
    }
    Write-Output 'continuous_visual_metrics_parser_self_test=PASS'
}

if ($SelfTestVisualMetricsParser) {
    Invoke-VisualMetricsParserSelfTest
    exit 0
}

function Get-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

function Assert-PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $normalizedPath = Get-NormalizedPath $Path
    $normalizedRoot = (Get-NormalizedPath $Root).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $prefix = $normalizedRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $normalizedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside its required root: $normalizedPath"
    }
}

function Get-VisualThresholds {
    param([string]$Path)
    $header = "scene`tmean_flip_limit`tmax_channel_delta`tpixels_per_degree`tpatch_size`tchannel_delta`tflip_commit"
    $lines = @([IO.File]::ReadAllLines($Path, [Text.UTF8Encoding]::new($false)))
    if ($lines.Count -ne 3 -or $lines[0] -ne $header) {
        throw 'Visual threshold TSV shape is invalid.'
    }
    $rows = @($lines | ConvertFrom-Csv -Delimiter "`t")
    if (($rows.scene -join ',') -ne 'indoor,outdoor') {
        throw 'Visual threshold TSV must contain exact indoor/outdoor rows.'
    }
    $result = @{}
    foreach ($row in $rows) {
        [double]$meanLimit = 0.0
        [int]$rawLimit = 0
        if (-not [double]::TryParse($row.mean_flip_limit, $floatStyle, $invariant, [ref]$meanLimit) -or
            -not [double]::IsFinite($meanLimit) -or $meanLimit -lt 0.0 -or $meanLimit -gt 1.0 -or
            -not [int]::TryParse($row.max_channel_delta, [ref]$rawLimit) -or
            $rawLimit -lt 0 -or $rawLimit -gt 8 -or $row.pixels_per_degree -ne '67.0' -or
            $row.flip_commit -ne 'b475eb4bf394ab877c42166c9eb0a84a02cc5b14') {
            throw "Visual threshold row is invalid: $($row.scene)."
        }
        $result[$row.scene] = [pscustomobject]@{ MeanLimit = $meanLimit; RawLimit = $rawLimit }
    }
    return $result
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Get-NormalizedPath (Join-Path $repoRoot 'build')
$runsRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\Runs')
$exe = Get-NormalizedPath (Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe')
$thresholdPath = Get-NormalizedPath (Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\VisualThresholds.tsv')

Assert-PathWithinRoot $buildRoot $repoRoot 'Build root'
Assert-PathWithinRoot $runsRoot $buildRoot 'Rendering validation runs root'
Assert-PathWithinRoot $exe $buildRoot 'Golden executable'
Assert-PathWithinRoot $thresholdPath $repoRoot 'Visual threshold TSV'

if (-not [IO.File]::Exists($exe)) {
    throw 'Golden executable is missing. Run: cmake --build build --config Debug --target RenderingGoldenImageTest'
}
$thresholds = Get-VisualThresholds $thresholdPath
[IO.Directory]::CreateDirectory($runsRoot) | Out-Null

function Invoke-GoldenComparison {
    param([Parameter(Mandatory = $true)][string]$Scene)
    Push-Location $runsRoot
    try {
        $lines = @(& $exe "--scene=$Scene" 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    foreach ($line in $lines) {
        Write-Host $line
    }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

$metrics = @{ indoor = @(); outdoor = @() }
$skipCounts = @{ indoor = 0; outdoor = 0 }
for ($run = 1; $run -le $Iterations; ++$run) {
    foreach ($scene in @('indoor', 'outdoor')) {
        $result = Invoke-GoldenComparison $scene
        if ($result.ExitCode -eq 125) {
            if ($RequireGpu) {
                throw "GPU was required: $scene run=$run"
            }
            ++$skipCounts[$scene]
            Write-Host "GPU skip: $scene run=$run"
            continue
        }
        if ($result.ExitCode -ne 0) {
            throw "Golden run failed: $scene run=$run exit=$($result.ExitCode)"
        }
        $threshold = $thresholds[$scene]
        $metric = Convert-VisualMetricsLine $result.Lines $scene $threshold.MeanLimit $threshold.RawLimit
        $metrics[$scene] += $metric
        Write-Host "Golden pass: $scene run=$run mean_flip=$($metric.MeanFlip) raw_max=$($metric.RawMax)"
    }
}

foreach ($scene in @('indoor', 'outdoor')) {
    $sceneMetrics = @($metrics[$scene])
    $successfulRuns = $sceneMetrics.Count
    $maxMean = if ($successfulRuns -gt 0) {
        ($sceneMetrics | Measure-Object -Property MeanFlip -Maximum).Maximum
    } else { 0.0 }
    $maxRaw = if ($successfulRuns -gt 0) {
        ($sceneMetrics | Measure-Object -Property RawMax -Maximum).Maximum
    } else { 0 }
    Write-Output ("Visual summary: scene={0} successful_runs={1} skipped_runs={2} max_mean_flip={3} max_raw={4}" -f
        $scene, $successfulRuns, $skipCounts[$scene],
        ([double]$maxMean).ToString('F9', $invariant), $maxRaw)
}
