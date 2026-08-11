[CmdletBinding(DefaultParameterSetName = 'None')]
param(
    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTestMeasurementParser,

    [Parameter(ParameterSetName = 'ApprovedCandidateValidationSelfTest', Mandatory = $true)]
    [switch]$SelfTestApprovedCandidateValidation,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [switch]$GenerateCandidate,

    [Parameter(ParameterSetName = 'Generate')]
    [ValidateRange(1, 100)]
    [int]$Iterations = 10,

    [Parameter(ParameterSetName = 'Generate')]
    [switch]$RequireGpu,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [switch]$PublishApprovedCandidate,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$CandidateSha256
)

$ErrorActionPreference = 'Stop'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$floatStyle = [System.Globalization.NumberStyles]::Float

function Convert-StrictMetricLine {
    param(
        [string[]]$OutputLines,
        [string]$Prefix,
        [string]$ExpectedScene,
        [Nullable[int]]$ExpectedPatchSize,
        [Nullable[int]]$ExpectedChannelDelta
    )

    $matching = @($OutputLines | Where-Object { $_ -match ('^' + [regex]::Escape($Prefix) + '(?:\s|$)') })
    if ($matching.Count -ne 1) {
        throw "Expected exactly one $Prefix line; found $($matching.Count)."
    }

    $tokens = @($matching[0] -split ' ' | Where-Object { $_.Length -gt 0 })
    if ($tokens.Count -lt 2 -or $tokens[0] -ne $Prefix) {
        throw "Malformed $Prefix line."
    }
    $fields = @{}
    foreach ($token in $tokens[1..($tokens.Count - 1)]) {
        $parts = @($token -split '=', 2)
        if ($parts.Count -ne 2 -or $parts[0].Length -eq 0 -or $parts[1].Length -eq 0) {
            throw "Malformed field in $Prefix line."
        }
        if ($fields.ContainsKey($parts[0])) {
            throw "Duplicate field '$($parts[0])' in $Prefix line."
        }
        $fields[$parts[0]] = $parts[1]
    }

    $required = if ($Prefix -eq 'NORVESLIB_VISUAL_MEASUREMENT') {
        @('scene', 'mean_flip', 'max_flip', 'raw_max')
    } else {
        @('scene', 'patch_size', 'channel_delta', 'mean_flip', 'max_flip', 'raw_max')
    }
    if ($fields.Count -ne $required.Count) {
        throw "Unexpected field count in $Prefix line."
    }
    foreach ($name in $required) {
        if (-not $fields.ContainsKey($name)) {
            throw "Missing field '$name' in $Prefix line."
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
    if (-not [int]::TryParse($fields.raw_max, [System.Globalization.NumberStyles]::None,
                             $invariant, [ref]$rawMaximum) -or
        $rawMaximum -lt 0 -or $rawMaximum -gt 255) {
        throw 'raw_max must be an invariant integer in [0,255].'
    }

    [int]$patchSize = 0
    [int]$channelDelta = 0
    if ($Prefix -eq 'NORVESLIB_ARTIFICIAL_METRICS') {
        if (-not [int]::TryParse($fields.patch_size, [ref]$patchSize) -or
            $patchSize -notin @(1, 2, 4, 8, 16)) {
            throw 'patch_size is invalid.'
        }
        if (-not [int]::TryParse($fields.channel_delta, [ref]$channelDelta) -or
            $channelDelta -notin @(1, 2, 4, 8)) {
            throw 'channel_delta is invalid.'
        }
        $expectedPatch = [int]$ExpectedPatchSize
        $expectedDelta = [int]$ExpectedChannelDelta
        if ($patchSize -ne $expectedPatch) {
            throw "Patch mismatch: expected $expectedPatch, got $patchSize."
        }
        if ($channelDelta -ne $expectedDelta) {
            throw "Delta mismatch: expected $expectedDelta, got $channelDelta."
        }
        if ($rawMaximum -ne $channelDelta -or $rawMaximum -gt 8) {
            throw 'Artificial raw_max must equal channel_delta and remain <=8.'
        }
    }

    [pscustomobject]@{
        Scene = $fields.scene
        PatchSize = $patchSize
        ChannelDelta = $channelDelta
        MeanFlip = $mean
        MaxFlip = $maximum
        RawMax = $rawMaximum
        MeanText = $fields.mean_flip
        MaxText = $fields.max_flip
    }
}

function Assert-ParserRejects {
    param(
        [string]$Name,
        [scriptblock]$Action
    )
    $rejected = $false
    try {
        & $Action | Out-Null
    } catch {
        $rejected = $true
        Write-Output "parser_negative=$Name status=rejected reason=$($_.Exception.Message)"
    }
    if (-not $rejected) {
        throw "Parser negative '$Name' was accepted."
    }
}

function Invoke-ParserSelfTest {
    $valid = 'NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=0.000001000 max_flip=0.200000000 raw_max=3'
    Convert-StrictMetricLine @($valid) 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null | Out-Null

    Assert-ParserRejects 'measurement_missing_line' {
        Convert-StrictMetricLine @('noise') 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
    }
    Assert-ParserRejects 'measurement_duplicate_line' {
        Convert-StrictMetricLine @($valid, $valid) 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
    }
    foreach ($nonFinite in @('NaN', '+Inf', '-Inf')) {
        Assert-ParserRejects "measurement_nonfinite_$nonFinite" {
            Convert-StrictMetricLine @("NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=$nonFinite max_flip=0.2 raw_max=3") 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
        }
    }
    Assert-ParserRejects 'measurement_field_missing' {
        Convert-StrictMetricLine @('NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=0.1 raw_max=3') 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
    }
    Assert-ParserRejects 'measurement_field_duplicate' {
        Convert-StrictMetricLine @('NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=0.1 mean_flip=0.2 max_flip=0.2 raw_max=3') 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
    }
    Assert-ParserRejects 'measurement_scene_mismatch' {
        Convert-StrictMetricLine @($valid) 'NORVESLIB_VISUAL_MEASUREMENT' 'outdoor' $null $null
    }

    $artificial = 'NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=0.1 max_flip=0.2 raw_max=2'
    Convert-StrictMetricLine @($artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2 | Out-Null
    Assert-ParserRejects 'artificial_missing_line' {
        Convert-StrictMetricLine @('noise') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_duplicate_line' {
        Convert-StrictMetricLine @($artificial, $artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_nonfinite' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=NaN max_flip=0.2 raw_max=2') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_field_missing' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=0.1 raw_max=2') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_field_duplicate' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 patch_size=4 channel_delta=2 mean_flip=0.1 max_flip=0.2 raw_max=2') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_scene_mismatch' {
        Convert-StrictMetricLine @($artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'outdoor' 4 2
    }
    Assert-ParserRejects 'artificial_patch_mismatch' {
        Convert-StrictMetricLine @($artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 8 2
    }
    Assert-ParserRejects 'artificial_delta_mismatch' {
        Convert-StrictMetricLine @($artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 4
    }
    Assert-ParserRejects 'artificial_raw_mismatch' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=0.1 max_flip=0.2 raw_max=1') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Write-Output 'measurement_parser_self_test=PASS'
}

function Invoke-FixedExecutable {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$RunsDirectory
    )
    Push-Location -LiteralPath $RunsDirectory
    try {
        $lines = @(& $Executable @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

function Write-AtomicUtf8Lines {
    param([string]$Path, [string[]]$Lines)
    $directory = Split-Path -Parent $Path
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    $temporary = "$Path.tmp"
    $backup = "$Path.bak"
    [System.IO.File]::WriteAllLines($temporary, $Lines, [System.Text.UTF8Encoding]::new($false))
    if ([System.IO.File]::Exists($Path)) {
        [System.IO.File]::Replace($temporary, $Path, $backup, $true)
        [System.IO.File]::Delete($backup)
    } else {
        [System.IO.File]::Move($temporary, $Path)
    }
}

function Format-Nine([double]$Value) {
    return $Value.ToString('F9', $invariant)
}

function Invoke-CandidateGeneration {
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $buildRoot = Join-Path $repoRoot 'build'
    $runsDirectory = Join-Path $buildRoot 'RenderingValidation\Runs'
    $goldenExecutable = Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe'
    $perceptualExecutable = Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingPerceptualDiffTest.exe'
    foreach ($path in @($runsDirectory, $goldenExecutable, $perceptualExecutable)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Required calibration input is missing: $path"
        }
    }

    $headCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $headCommit -notmatch '^[0-9a-f]{40}$') {
        throw 'Unable to resolve calibration HEAD.'
    }

    $measurementLines = [System.Collections.Generic.List[string]]::new()
    $measurementLines.Add("kind`tscene`trun`tpatch_size`tchannel_delta`tmean_flip`tmax_flip`traw_max`tbaseline_sha256`thead_commit")
    $candidateLines = [System.Collections.Generic.List[string]]::new()
    $candidateLines.Add("scene`tmean_flip_limit`tmax_channel_delta`tpixels_per_degree`tpatch_size`tchannel_delta`tflip_commit")
    $summaries = @()

    foreach ($scene in @('indoor', 'outdoor')) {
        $baselineName = if ($scene -eq 'indoor') { 'Indoor.png' } else { 'Outdoor.png' }
        $baselinePath = Join-Path $repoRoot "Test\Core\Rendering\Baselines\RenderingValidation\$baselineName"
        $baselineHash = (Get-FileHash -LiteralPath $baselinePath -Algorithm SHA256).Hash
        $noise = @()
        for ($run = 1; $run -le $Iterations; ++$run) {
            $result = Invoke-FixedExecutable $goldenExecutable @("--scene=$scene", '--measure-visual') $runsDirectory
            if ($result.ExitCode -eq 125) {
                throw "GPU calibration skipped for $scene run $run."
            }
            if ($result.ExitCode -ne 0) {
                throw "GPU calibration failed for $scene run $run (exit $($result.ExitCode))."
            }
            $metric = Convert-StrictMetricLine $result.Lines 'NORVESLIB_VISUAL_MEASUREMENT' $scene $null $null
            if ($metric.RawMax -gt 8) {
                throw "Normal capture raw_max exceeded 8 for $scene run $run."
            }
            $noise += $metric
            $measurementLines.Add("noise`t$scene`t$run`t`t`t$(Format-Nine $metric.MeanFlip)`t$(Format-Nine $metric.MaxFlip)`t$($metric.RawMax)`t$baselineHash`t$headCommit")
        }

        $artificial = @()
        $specifications = foreach ($patchSize in @(1, 2, 4, 8, 16)) {
            foreach ($channelDelta in @(1, 2, 4, 8)) {
                [pscustomobject]@{
                    PatchSize = $patchSize
                    ChannelDelta = $channelDelta
                    ChangeAmount = $patchSize * $patchSize * $channelDelta
                }
            }
        }
        $specifications = $specifications | Sort-Object ChangeAmount, PatchSize, ChannelDelta
        $artificialRun = 0
        foreach ($specification in $specifications) {
            ++$artificialRun
            $arguments = @('--measure-artificial', "--scene=$scene",
                           "--patch-size=$($specification.PatchSize)",
                           "--channel-delta=$($specification.ChannelDelta)")
            $result = Invoke-FixedExecutable $perceptualExecutable $arguments $runsDirectory
            if ($result.ExitCode -ne 0) {
                throw "Artificial measurement failed for $scene patch=$($specification.PatchSize) delta=$($specification.ChannelDelta)."
            }
            $metric = Convert-StrictMetricLine $result.Lines 'NORVESLIB_ARTIFICIAL_METRICS' $scene $specification.PatchSize $specification.ChannelDelta
            $artificial += [pscustomobject]@{
                PatchSize = $specification.PatchSize
                ChannelDelta = $specification.ChannelDelta
                MeanFlip = $metric.MeanFlip
                MaxFlip = $metric.MaxFlip
                RawMax = $metric.RawMax
            }
            $measurementLines.Add("artificial`t$scene`t$artificialRun`t$($specification.PatchSize)`t$($specification.ChannelDelta)`t$(Format-Nine $metric.MeanFlip)`t$(Format-Nine $metric.MaxFlip)`t$($metric.RawMax)`t$baselineHash`t$headCommit")
        }

        $noiseMaximum = ($noise | Measure-Object -Property MeanFlip -Maximum).Maximum
        $selected = $null
        $selectedLimit = [decimal]0
        foreach ($candidate in $artificial) {
            if ($candidate.MeanFlip -le $noiseMaximum) {
                continue
            }
            $noiseDecimal = [decimal]::Parse((Format-Nine $noiseMaximum), $invariant)
            $negativeDecimal = [decimal]::Parse((Format-Nine $candidate.MeanFlip), $invariant)
            $limit = [decimal]::Ceiling(($noiseDecimal + (($negativeDecimal - $noiseDecimal) / [decimal]4)) * [decimal]1000000) / [decimal]1000000
            if ($noiseDecimal -lt $limit -and $limit -lt $negativeDecimal) {
                $selected = $candidate
                $selectedLimit = $limit
                break
            }
        }
        if ($null -eq $selected) {
            throw "No detectable artificial candidate exists for $scene."
        }

        $limitText = $selectedLimit.ToString('F6', $invariant)
        $candidateLines.Add("$scene`t$limitText`t8`t67.0`t$($selected.PatchSize)`t$($selected.ChannelDelta)`tb475eb4bf394ab877c42166c9eb0a84a02cc5b14")
        $summaries += [pscustomobject]@{
            Scene = $scene
            NoiseMaximum = $noiseMaximum
            Limit = $limitText
            NegativeMean = $selected.MeanFlip
            PatchSize = $selected.PatchSize
            ChannelDelta = $selected.ChannelDelta
            Noise = $noise
        }
    }

    $calibrationDirectory = Join-Path $buildRoot 'RenderingValidation\Calibration'
    $candidatePath = Join-Path $calibrationDirectory 'VisualThresholds.candidate.tsv'
    $measurementPath = Join-Path $calibrationDirectory 'VisualThresholdMeasurements.tsv'
    Write-AtomicUtf8Lines $measurementPath $measurementLines.ToArray()
    Write-AtomicUtf8Lines $candidatePath $candidateLines.ToArray()
    $candidateHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash
    Write-Output "candidate_sha256=$candidateHash"
    Write-Output "candidate_path=$candidatePath"
    Write-Output "measurement_path=$measurementPath"
    foreach ($summary in $summaries) {
        Write-Output ("scene={0} noiseMax={1} limit={2} negativeMean={3} patch_size={4} channel_delta={5}" -f
            $summary.Scene, (Format-Nine $summary.NoiseMaximum), $summary.Limit,
            (Format-Nine $summary.NegativeMean), $summary.PatchSize, $summary.ChannelDelta)
    }
}

function Get-StrictTsvLines {
    param(
        [string]$Path,
        [string]$ExpectedHeader,
        [int]$ExpectedDataRows
    )
    $lines = @([System.IO.File]::ReadAllLines($Path, [System.Text.UTF8Encoding]::new($false)))
    if ($lines.Count -ne $ExpectedDataRows + 1 -or $lines[0] -ne $ExpectedHeader) {
        throw "TSV shape is invalid: $Path"
    }
    return $lines
}

function Test-DecisionRecordApproval {
    param(
        [string]$DecisionPath,
        [string]$CandidateHash,
        [string]$CalibrationHead,
        [hashtable]$BaselineHashes,
        [object[]]$DerivedSummaries
    )
    $decision = [System.IO.File]::ReadAllText($DecisionPath)
    $requiredMarkers = @(
        '## R0 threshold 承認記録',
        '承認原文: 「承認します」',
        "承認対象 candidate SHA-256: ``$CandidateHash``",
        "calibration HEAD: ``$CalibrationHead``",
        "Indoor baseline SHA-256: ``$($BaselineHashes.indoor)``",
        "Outdoor baseline SHA-256: ``$($BaselineHashes.outdoor)``"
    )
    foreach ($marker in $requiredMarkers) {
        if (-not $decision.Contains($marker, [System.StringComparison]::Ordinal)) {
            throw "Decision record approval marker is missing: $marker"
        }
    }
    foreach ($summary in $DerivedSummaries) {
        $measurementMarker = "- 【実測】$($summary.Scene): 通常10回の ``noiseMax=$($summary.NoiseMaximum)``、採用 ``mean_flip_limit=$($summary.Limit)``、選定人工差 ``negativeMean=$($summary.NegativeMean)``／``patch_size=$($summary.PatchSize)``／``channel_delta=$($summary.ChannelDelta)``。"
        if (-not $decision.Contains($measurementMarker, [System.StringComparison]::Ordinal)) {
            throw "Decision record approved measurement is inconsistent for $($summary.Scene)."
        }
    }
}

function Convert-ValidatedMeasurementDecimal {
    param([string]$Value, [string]$Field, [string]$Scene, [string]$Run)
    [double]$finiteValue = 0
    [decimal]$decimalValue = 0
    if (-not [double]::TryParse($Value, $floatStyle, $invariant, [ref]$finiteValue) -or
        -not [double]::IsFinite($finiteValue) -or $finiteValue -lt 0.0 -or $finiteValue -gt 1.0 -or
        -not [decimal]::TryParse($Value, $floatStyle, $invariant, [ref]$decimalValue)) {
        throw "Measurement $Field is not finite and within [0,1] for $Scene run $Run."
    }
    return $decimalValue
}

function Convert-ValidatedMeasurementInteger {
    param([string]$Value, [string]$Field, [string]$Scene, [string]$Run, [int]$Maximum)
    [int]$integerValue = 0
    if ($Value -notmatch '^(0|[1-9][0-9]*)$' -or
        -not [int]::TryParse($Value, [System.Globalization.NumberStyles]::None, $invariant, [ref]$integerValue) -or
        $integerValue -lt 0 -or $integerValue -gt $Maximum) {
        throw "Measurement $Field is invalid for $Scene run $Run."
    }
    return $integerValue
}

function Get-ValidatedApprovedCandidate {
    param(
        [string]$RepoRoot,
        [string]$CandidatePath,
        [string]$MeasurementPath,
        [string]$DecisionPath,
        [string]$ApprovedHash
    )
    $actualHash = (Get-FileHash -LiteralPath $CandidatePath -Algorithm SHA256).Hash
    if (-not $actualHash.Equals($ApprovedHash, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Candidate hash mismatch: expected $ApprovedHash, got $actualHash."
    }

    $candidateHeader = "scene`tmean_flip_limit`tmax_channel_delta`tpixels_per_degree`tpatch_size`tchannel_delta`tflip_commit"
    $candidateLines = Get-StrictTsvLines $CandidatePath $candidateHeader 2
    $candidateRows = @($candidateLines | ConvertFrom-Csv -Delimiter "`t")
    if (($candidateRows.scene -join ',') -ne 'indoor,outdoor') {
        throw 'Candidate must contain exact indoor/outdoor rows in order.'
    }

    $measurementHeader = "kind`tscene`trun`tpatch_size`tchannel_delta`tmean_flip`tmax_flip`traw_max`tbaseline_sha256`thead_commit"
    $measurementLines = Get-StrictTsvLines $MeasurementPath $measurementHeader 60
    $measurementRows = @($measurementLines | ConvertFrom-Csv -Delimiter "`t")
    if (@($measurementRows | Where-Object kind -eq 'noise').Count -ne 20 -or
        @($measurementRows | Where-Object kind -eq 'artificial').Count -ne 40) {
        throw 'Measurement table must contain 20 noise and 40 artificial rows.'
    }

    $currentHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $currentHead -notmatch '^[0-9a-f]{40}$') {
        throw 'Unable to resolve current HEAD.'
    }
    $measurementHeads = @($measurementRows.head_commit | Sort-Object -Unique)
    if ($measurementHeads.Count -ne 1 -or $measurementHeads[0] -ne $currentHead) {
        throw 'Measurement calibration HEAD does not match current HEAD.'
    }

    $baselineHashes = @{
        indoor = (Get-FileHash -LiteralPath (Join-Path $RepoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\Indoor.png') -Algorithm SHA256).Hash
        outdoor = (Get-FileHash -LiteralPath (Join-Path $RepoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\Outdoor.png') -Algorithm SHA256).Hash
    }
    $derivedSummaries = @()

    foreach ($candidate in $candidateRows) {
        $scene = $candidate.scene
        if ($candidate.flip_commit -ne 'b475eb4bf394ab877c42166c9eb0a84a02cc5b14' -or
            $candidate.pixels_per_degree -ne '67.0' -or
            $candidate.max_channel_delta -ne '8') {
            throw "Candidate policy fields are invalid for $scene."
        }
        [int]$patchSize = 0
        [int]$channelDelta = 0
        [decimal]$limit = 0
        if (-not [int]::TryParse($candidate.patch_size, [ref]$patchSize) -or
            $patchSize -notin @(1, 2, 4, 8, 16) -or
            -not [int]::TryParse($candidate.channel_delta, [ref]$channelDelta) -or
            $channelDelta -notin @(1, 2, 4, 8) -or
            -not [decimal]::TryParse($candidate.mean_flip_limit, $floatStyle, $invariant, [ref]$limit)) {
            throw "Candidate numeric fields are invalid for $scene."
        }

        $sceneRows = @($measurementRows | Where-Object scene -eq $scene)
        if ($sceneRows.Count -ne 30) {
            throw "Measurement baseline identity is invalid for $scene."
        }
        $noiseRows = @($sceneRows | Where-Object kind -eq 'noise')
        $artificialRows = @($sceneRows | Where-Object kind -eq 'artificial')
        if ($noiseRows.Count -ne 10 -or $artificialRows.Count -ne 20) {
            throw "Measurement row set is invalid for $scene."
        }

        $validatedRows = @()
        foreach ($row in $sceneRows) {
            if ($row.head_commit -ne $currentHead -or $row.baseline_sha256 -ne $baselineHashes[$scene]) {
                throw "Measurement identity is invalid for $scene run $($row.run)."
            }
            $rowRun = Convert-ValidatedMeasurementInteger $row.run 'run' $scene $row.run 20
            $mean = Convert-ValidatedMeasurementDecimal $row.mean_flip 'mean_flip' $scene $row.run
            $maximum = Convert-ValidatedMeasurementDecimal $row.max_flip 'max_flip' $scene $row.run
            $rawMaximum = Convert-ValidatedMeasurementInteger $row.raw_max 'raw_max' $scene $row.run 255
            $validatedRows += [pscustomobject]@{
                Source = $row
                Run = $rowRun
                Mean = $mean
                Maximum = $maximum
                RawMaximum = $rawMaximum
            }
        }

        $noiseRuns = @{}
        foreach ($row in @($validatedRows | Where-Object { $_.Source.kind -eq 'noise' })) {
            if ($row.Run -lt 1 -or $row.Run -gt 10 -or $noiseRuns.ContainsKey($row.Run) -or
                $row.Source.patch_size.Length -ne 0 -or $row.Source.channel_delta.Length -ne 0 -or
                $row.RawMaximum -gt 8) {
                throw "Noise measurement set is invalid for $scene run $($row.Run)."
            }
            $noiseRuns[$row.Run] = $true
        }
        if ($noiseRuns.Count -ne 10) {
            throw "Noise measurement runs are incomplete for $scene."
        }

        $specifications = foreach ($expectedPatch in @(1, 2, 4, 8, 16)) {
            foreach ($expectedDelta in @(1, 2, 4, 8)) {
                [pscustomobject]@{
                    PatchSize = $expectedPatch
                    ChannelDelta = $expectedDelta
                    ChangeAmount = $expectedPatch * $expectedPatch * $expectedDelta
                }
            }
        }
        $specifications = @($specifications | Sort-Object ChangeAmount, PatchSize, ChannelDelta)
        $artificialByKey = @{}
        foreach ($row in @($validatedRows | Where-Object { $_.Source.kind -eq 'artificial' })) {
            $rowPatch = Convert-ValidatedMeasurementInteger $row.Source.patch_size 'patch_size' $scene $row.Run 16
            $rowDelta = Convert-ValidatedMeasurementInteger $row.Source.channel_delta 'channel_delta' $scene $row.Run 8
            $key = "$rowPatch/$rowDelta"
            if ($rowPatch -notin @(1, 2, 4, 8, 16) -or $rowDelta -notin @(1, 2, 4, 8) -or
                $artificialByKey.ContainsKey($key) -or $row.RawMaximum -ne $rowDelta) {
                throw "Artificial measurement set is invalid for $scene run $($row.Run)."
            }
            $artificialByKey[$key] = [pscustomobject]@{
                Row = $row
                PatchSize = $rowPatch
                ChannelDelta = $rowDelta
            }
        }
        if ($artificialByKey.Count -ne 20) {
            throw "Artificial measurement Cartesian product is incomplete for $scene."
        }

        [decimal]$noiseMaximum = (@($validatedRows | Where-Object { $_.Source.kind -eq 'noise' }).Mean |
            Measure-Object -Maximum).Maximum
        $selected = $null
        [decimal]$expectedLimit = 0
        for ($index = 0; $index -lt $specifications.Count; ++$index) {
            $specification = $specifications[$index]
            $key = "$($specification.PatchSize)/$($specification.ChannelDelta)"
            $artificial = $artificialByKey[$key]
            if ($null -eq $artificial -or $artificial.Row.Run -ne $index + 1) {
                throw "Artificial measurement cost order is invalid for $scene."
            }
            if ($null -ne $selected -or $artificial.Row.Mean -le $noiseMaximum) {
                continue
            }
            $proposedLimit = [decimal]::Ceiling(($noiseMaximum + (($artificial.Row.Mean - $noiseMaximum) / [decimal]4)) * [decimal]1000000) / [decimal]1000000
            if ($noiseMaximum -lt $proposedLimit -and $proposedLimit -lt $artificial.Row.Mean) {
                $selected = $artificial
                $expectedLimit = $proposedLimit
            }
        }
        if ($null -eq $selected -or $limit -ne $expectedLimit -or
            $patchSize -ne $selected.PatchSize -or $channelDelta -ne $selected.ChannelDelta) {
            throw "Candidate selection, formula, or separation is invalid for $scene."
        }
        $derivedSummaries += [pscustomobject]@{
            Scene = $scene
            NoiseMaximum = $noiseMaximum.ToString('F9', $invariant)
            Limit = $expectedLimit.ToString('F6', $invariant)
            NegativeMean = $selected.Row.Mean.ToString('F9', $invariant)
            PatchSize = $selected.PatchSize
            ChannelDelta = $selected.ChannelDelta
        }
    }
    Test-DecisionRecordApproval $DecisionPath $actualHash $currentHead $baselineHashes $derivedSummaries
    return [pscustomobject]@{ Lines = $candidateLines; Hash = $actualHash }
}

function Invoke-ApprovedCandidatePublish {
    param([string]$ApprovedHash)
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $calibrationDirectory = Join-Path $repoRoot 'build\RenderingValidation\Calibration'
    $candidatePath = Join-Path $calibrationDirectory 'VisualThresholds.candidate.tsv'
    $measurementPath = Join-Path $calibrationDirectory 'VisualThresholdMeasurements.tsv'
    $decisionPath = Join-Path $repoRoot 'Docs\RenderingValidation\PerceptualDiffSelection.md'
    $sourcePath = Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\VisualThresholds.tsv'
    $temporaryPath = Join-Path $calibrationDirectory 'VisualThresholds.tsv.tmp'
    $backupPath = Join-Path $calibrationDirectory 'VisualThresholds.tsv.bak'
    $rollbackDiscardPath = Join-Path $calibrationDirectory 'VisualThresholds.tsv.rollback-discard'

    $failureMode = [System.Environment]::GetEnvironmentVariable('NORVESLIB_VISUAL_THRESHOLD_TRANSACTION_TEST_FAILURE')
    if ($failureMode -and $failureMode -notin @('before-publish', 'after-publish')) {
        throw "Unsupported threshold transaction test failure mode: $failureMode"
    }

    $validated = Get-ValidatedApprovedCandidate $repoRoot $candidatePath $measurementPath $decisionPath $ApprovedHash
    $sourceExisted = [System.IO.File]::Exists($sourcePath)
    $published = $false
    $rollbackAttempted = $false
    $rollbackSucceeded = $false
    $preserveBackup = $false
    try {
        [System.IO.File]::WriteAllBytes($temporaryPath, [System.IO.File]::ReadAllBytes($candidatePath))
        if ($failureMode -eq 'before-publish') {
            throw 'Injected threshold transaction failure before publish.'
        }
        if ($sourceExisted) {
            [System.IO.File]::Replace($temporaryPath, $sourcePath, $backupPath, $true)
        } else {
            [System.IO.File]::Move($temporaryPath, $sourcePath)
        }
        $published = $true
        if ($failureMode -eq 'after-publish') {
            throw 'Injected threshold transaction failure after publish.'
        }
        Write-Output "published_candidate_sha256=$($validated.Hash)"
        Write-Output "source_threshold_path=$sourcePath"
    } catch {
        $publishError = $_
        if ($published) {
            $rollbackAttempted = $true
            try {
                if ($sourceExisted) {
                    [System.IO.File]::Replace($backupPath, $sourcePath, $rollbackDiscardPath, $true)
                } elseif ([System.IO.File]::Exists($sourcePath)) {
                    [System.IO.File]::Delete($sourcePath)
                }
                $rollbackSucceeded = $true
            } catch {
                $rollbackError = $_
                $preserveBackup = $sourceExisted -and [System.IO.File]::Exists($backupPath)
                $recoveryDiagnostic = if ($preserveBackup) {
                    " Recovery backup preserved at '$backupPath'."
                } else {
                    ' No recovery backup is available.'
                }
                throw [System.InvalidOperationException]::new(
                    "Threshold publish failed: $($publishError.Exception.Message) Rollback also failed: $($rollbackError.Exception.Message)$recoveryDiagnostic",
                    $rollbackError.Exception)
            }
        }
        throw $publishError
    } finally {
        $cleanupPaths = @($temporaryPath, $rollbackDiscardPath)
        if (-not $rollbackAttempted -or $rollbackSucceeded) {
            $cleanupPaths += $backupPath
        }
        foreach ($cleanupPath in $cleanupPaths) {
            if ([System.IO.File]::Exists($cleanupPath)) {
                try {
                    [System.IO.File]::Delete($cleanupPath)
                } catch {
                    Write-Warning "Threshold transaction cleanup deferred for '$cleanupPath': $($_.Exception.Message)"
                }
            }
        }
    }
}

function New-ApprovedCandidateValidationNegative {
    param(
        [string[]]$OriginalLines,
        [string]$Name
    )

    $mutated = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $OriginalLines) {
        $mutated.Add($line)
    }
    $rows = for ($index = 1; $index -lt $mutated.Count; ++$index) {
        $fields = @($mutated[$index].Split("`t"))
        [pscustomobject]@{ Index = $index; Fields = $fields }
    }

    switch ($Name) {
        'missing_noise_run' {
            $target = $rows | Where-Object { $_.Fields[0] -eq 'noise' -and $_.Fields[1] -eq 'indoor' -and $_.Fields[2] -eq '10' } | Select-Object -First 1
            $mutated.RemoveAt($target.Index)
        }
        'duplicate_noise_run' {
            $target = $rows | Where-Object { $_.Fields[0] -eq 'noise' -and $_.Fields[1] -eq 'indoor' -and $_.Fields[2] -eq '2' } | Select-Object -First 1
            $fields = @($target.Fields)
            $fields[2] = '1'
            $mutated[$target.Index] = $fields -join "`t"
        }
        'nonselected_artificial_raw' {
            $target = $rows | Where-Object { $_.Fields[0] -eq 'artificial' -and $_.Fields[1] -eq 'indoor' -and $_.Fields[3] -eq '16' -and $_.Fields[4] -eq '8' } | Select-Object -First 1
            $fields = @($target.Fields)
            $fields[7] = '7'
            $mutated[$target.Index] = $fields -join "`t"
        }
        'nonselected_artificial_nonfinite' {
            $target = $rows | Where-Object { $_.Fields[0] -eq 'artificial' -and $_.Fields[1] -eq 'indoor' -and $_.Fields[3] -eq '16' -and $_.Fields[4] -eq '8' } | Select-Object -First 1
            $fields = @($target.Fields)
            $fields[5] = 'NaN'
            $mutated[$target.Index] = $fields -join "`t"
        }
        'artificial_cost_order' {
            $first = $rows | Where-Object { $_.Fields[0] -eq 'artificial' -and $_.Fields[1] -eq 'indoor' -and $_.Fields[2] -eq '19' } | Select-Object -First 1
            $second = $rows | Where-Object { $_.Fields[0] -eq 'artificial' -and $_.Fields[1] -eq 'indoor' -and $_.Fields[2] -eq '20' } | Select-Object -First 1
            $firstFields = @($first.Fields)
            $secondFields = @($second.Fields)
            $firstFields[2] = '20'
            $secondFields[2] = '19'
            $mutated[$first.Index] = $firstFields -join "`t"
            $mutated[$second.Index] = $secondFields -join "`t"
        }
        default {
            throw "Unknown approved candidate validation negative: $Name"
        }
    }
    return $mutated.ToArray()
}

function Invoke-ApprovedCandidateValidationSelfTest {
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $calibrationDirectory = Join-Path $repoRoot 'build\RenderingValidation\Calibration'
    $candidatePath = Join-Path $calibrationDirectory 'VisualThresholds.candidate.tsv'
    $measurementPath = Join-Path $calibrationDirectory 'VisualThresholdMeasurements.tsv'
    $sourcePath = Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\VisualThresholds.tsv'
    $transactionPaths = @(
        (Join-Path $calibrationDirectory 'VisualThresholds.tsv.tmp'),
        (Join-Path $calibrationDirectory 'VisualThresholds.tsv.bak'),
        (Join-Path $calibrationDirectory 'VisualThresholds.tsv.rollback-discard')
    )
    if (-not [System.IO.File]::Exists($candidatePath) -or
        -not [System.IO.File]::Exists($measurementPath) -or
        -not [System.IO.File]::Exists($sourcePath)) {
        throw 'Approved candidate validation self-test requires the approved candidate, measurement table, and source threshold.'
    }
    foreach ($transactionPath in $transactionPaths) {
        if ([System.IO.File]::Exists($transactionPath) -or [System.IO.Directory]::Exists($transactionPath)) {
            throw "Threshold transaction remainder blocks self-test: $transactionPath"
        }
    }

    $candidateHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $originalMeasurementBytes = [System.IO.File]::ReadAllBytes($measurementPath)
    $originalMeasurementLines = @([System.IO.File]::ReadAllLines($measurementPath, [System.Text.UTF8Encoding]::new($false)))
    $negativeNames = @(
        'missing_noise_run',
        'duplicate_noise_run',
        'nonselected_artificial_raw',
        'nonselected_artificial_nonfinite',
        'artificial_cost_order'
    )

    try {
        foreach ($negativeName in $negativeNames) {
            $mutatedLines = New-ApprovedCandidateValidationNegative $originalMeasurementLines $negativeName
            [System.IO.File]::WriteAllLines($measurementPath, $mutatedLines, [System.Text.UTF8Encoding]::new($false))
            $rejected = $false
            try {
                Invoke-ApprovedCandidatePublish $candidateHash | Out-Null
            } catch {
                $rejected = $true
                $reason = $_.Exception.Message
            } finally {
                [System.IO.File]::WriteAllBytes($measurementPath, $originalMeasurementBytes)
            }
            if (-not $rejected) {
                throw "Approved candidate validation negative '$negativeName' was published."
            }
            $currentSourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
            if ($currentSourceHash -ne $sourceHash) {
                throw "Approved candidate validation negative '$negativeName' changed the source threshold."
            }
            foreach ($transactionPath in $transactionPaths) {
                if ([System.IO.File]::Exists($transactionPath) -or [System.IO.Directory]::Exists($transactionPath)) {
                    throw "Approved candidate validation negative '$negativeName' left transaction state: $transactionPath"
                }
            }
            Write-Output "approved_candidate_negative=$negativeName status=rejected source_hash=$currentSourceHash reason=$reason"
        }
    } finally {
        [System.IO.File]::WriteAllBytes($measurementPath, $originalMeasurementBytes)
    }

    if ((Get-FileHash -LiteralPath $measurementPath -Algorithm SHA256).Hash -ne
        [System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::HashData($originalMeasurementBytes)).Replace('-', '')) {
        throw 'Approved candidate validation self-test did not restore the measurement table exactly.'
    }
    Write-Output 'approved_candidate_validation_self_test=PASS'
}

if ($SelfTestMeasurementParser) {
    Invoke-ParserSelfTest
    exit 0
}
if ($SelfTestApprovedCandidateValidation) {
    Invoke-ApprovedCandidateValidationSelfTest
    exit 0
}
if ($GenerateCandidate) {
    Invoke-CandidateGeneration
    exit 0
}
if ($PublishApprovedCandidate) {
    Invoke-ApprovedCandidatePublish $CandidateSha256
    exit 0
}
throw 'Specify -SelfTestMeasurementParser, -SelfTestApprovedCandidateValidation, -GenerateCandidate, or -PublishApprovedCandidate.'
