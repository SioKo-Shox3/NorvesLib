[CmdletBinding(DefaultParameterSetName = 'None')]
param(
    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTestMeasurementParser,

    [Parameter(ParameterSetName = 'ApprovedCandidateValidationSelfTest', Mandatory = $true)]
    [switch]$SelfTestApprovedCandidateValidation,

    [Parameter(ParameterSetName = 'R1SelfTest', Mandatory = $true)]
    [switch]$SelfTestR1Contract,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [switch]$GenerateCandidate,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [ValidateRange(1, 100)]
    [int]$Iterations,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [switch]$RequireGpu,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9a-f]{40}$')]
    [string]$CodeHead,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$BaselineIndoorSha256,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$BaselineOutdoorSha256,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [string]$DecisionPath,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [string]$DecisionSection,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [switch]$PublishApprovedCandidate,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$CandidateSha256
)

$ErrorActionPreference = 'Stop'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

if ($null -eq ('NorvesLibR1ThresholdStrictJson' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

public static class NorvesLibR1ThresholdStrictJson
{
    public static void ValidateNoDuplicateProperties(string json)
    {
        var reader = new Utf8JsonReader(Encoding.UTF8.GetBytes(json));
        var objects = new Stack<HashSet<string>>();
        while (reader.Read())
        {
            if (reader.TokenType == JsonTokenType.StartObject)
            {
                objects.Push(new HashSet<string>(StringComparer.Ordinal));
            }
            else if (reader.TokenType == JsonTokenType.EndObject)
            {
                objects.Pop();
            }
            else if (reader.TokenType == JsonTokenType.PropertyName)
            {
                if (objects.Count == 0 || !objects.Peek().Add(reader.GetString()))
                {
                    throw new InvalidOperationException("duplicate JSON property");
                }
            }
        }
    }
}
'@
}

function Convert-CanonicalF9ToNanounits {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value -notmatch '\A(0\.[0-9]{9}|1\.000000000)\z') {
        throw "Value is not a canonical F9 decimal: '$Value'."
    }
    if ($Value -eq '1.000000000') {
        return [uint64]1000000000
    }
    [uint64]$nanounits = 0
    $fraction = $Value.Substring(2, 9)
    for ($index = 0; $index -lt $fraction.Length; ++$index) {
        $nanounits = $nanounits * [uint64]10 + [uint64]([int][char]$fraction[$index] - 48)
    }
    return $nanounits
}

function Convert-CanonicalF6ToMillionths {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value -notmatch '\A(0\.[0-9]{6}|1\.000000)\z') {
        throw "Value is not a canonical F6 decimal: '$Value'."
    }
    if ($Value -eq '1.000000') {
        return [uint64]1000000
    }
    [uint64]$millionths = 0
    $fraction = $Value.Substring(2, 6)
    for ($index = 0; $index -lt $fraction.Length; ++$index) {
        $millionths = $millionths * [uint64]10 + [uint64]([int][char]$fraction[$index] - 48)
    }
    return $millionths
}

function Convert-NanounitsToDecimal {
    param([Parameter(Mandatory = $true)][uint64]$Nanounits)
    return [decimal]$Nanounits / [decimal]1000000000
}

function Format-Millionths {
    param([Parameter(Mandatory = $true)][uint64]$Millionths)
    if ($Millionths -gt 1000000) {
        throw "F6 threshold is outside [0,1]: $Millionths."
    }
    if ($Millionths -eq 1000000) {
        return '1.000000'
    }
    return ('0.{0:D6}' -f $Millionths)
}

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

    [uint64]$meanNanounits = Convert-CanonicalF9ToNanounits $fields.mean_flip
    [uint64]$maximumNanounits = Convert-CanonicalF9ToNanounits $fields.max_flip
    [decimal]$mean = Convert-NanounitsToDecimal $meanNanounits
    [decimal]$maximum = Convert-NanounitsToDecimal $maximumNanounits
    [int]$rawMaximum = 0
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
        MeanNanounits = $meanNanounits
        MaxNanounits = $maximumNanounits
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
        Convert-StrictMetricLine @("NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=$nonFinite max_flip=0.200000000 raw_max=3") 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
        }
    }
    Assert-ParserRejects 'measurement_field_missing' {
        Convert-StrictMetricLine @('NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=0.100000000 raw_max=3') 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
    }
    Assert-ParserRejects 'measurement_field_duplicate' {
        Convert-StrictMetricLine @('NORVESLIB_VISUAL_MEASUREMENT scene=indoor mean_flip=0.100000000 mean_flip=0.200000000 max_flip=0.200000000 raw_max=3') 'NORVESLIB_VISUAL_MEASUREMENT' 'indoor' $null $null
    }
    Assert-ParserRejects 'measurement_scene_mismatch' {
        Convert-StrictMetricLine @($valid) 'NORVESLIB_VISUAL_MEASUREMENT' 'outdoor' $null $null
    }

    $artificial = 'NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=0.100000000 max_flip=0.200000000 raw_max=2'
    Convert-StrictMetricLine @($artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2 | Out-Null
    Assert-ParserRejects 'artificial_missing_line' {
        Convert-StrictMetricLine @('noise') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_duplicate_line' {
        Convert-StrictMetricLine @($artificial, $artificial) 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_nonfinite' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=NaN max_flip=0.200000000 raw_max=2') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_field_missing' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=0.100000000 raw_max=2') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
    }
    Assert-ParserRejects 'artificial_field_duplicate' {
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 patch_size=4 channel_delta=2 mean_flip=0.100000000 max_flip=0.200000000 raw_max=2') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
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
        Convert-StrictMetricLine @('NORVESLIB_ARTIFICIAL_METRICS scene=indoor patch_size=4 channel_delta=2 mean_flip=0.100000000 max_flip=0.200000000 raw_max=1') 'NORVESLIB_ARTIFICIAL_METRICS' 'indoor' 4 2
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

function Get-R1NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-R1PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $normalizedPath = Get-R1NormalizedPath $Path
    $normalizedRoot = (Get-R1NormalizedPath $Root).TrimEnd('\', '/')
    if (-not $normalizedPath.StartsWith($normalizedRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside its required root: $normalizedPath"
    }
}

function Assert-R1NoReparseAncestors {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Assert-R1PathWithinRoot $Path $Root $Label
    $normalizedRoot = Get-R1NormalizedPath $Root
    $current = Get-R1NormalizedPath $Path
    while ($current.StartsWith($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        if ([System.IO.Directory]::Exists($current)) {
            $attributes = ([System.IO.DirectoryInfo]$current).Attributes
            if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label has a reparse-point ancestor: $current"
            }
        } elseif ([System.IO.File]::Exists($current)) {
            $attributes = ([System.IO.FileInfo]$current).Attributes
            if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label is a reparse point: $current"
            }
        }
        if ($current -ceq $normalizedRoot) {
            break
        }
        $parent = [System.IO.Directory]::GetParent($current)
        if ($null -eq $parent) {
            break
        }
        $current = $parent.FullName
    }
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

function Format-Nine([decimal]$Value) {
    return $Value.ToString('F9', $invariant)
}

function Select-R1StrictCandidate {
    param(
        [Parameter(Mandatory = $true)][object[]]$Artificial,
        [Parameter(Mandatory = $true)][uint64]$NoiseMaximumNanounits
    )
    foreach ($candidate in $Artificial) {
        if ($candidate.MeanNanounits -le $NoiseMaximumNanounits) {
            continue
        }
        [uint64]$numerator = [uint64]3 * $NoiseMaximumNanounits + $candidate.MeanNanounits
        [decimal]$quotient = [decimal]$numerator / [decimal]4000
        [uint64]$limitMillionths = [uint64][math]::Ceiling($quotient)
        if ($NoiseMaximumNanounits -lt $limitMillionths * [uint64]1000 -and
            $limitMillionths * [uint64]1000 -lt $candidate.MeanNanounits) {
            return [pscustomobject]@{
                Candidate = $candidate
                LimitMillionths = $limitMillionths
            }
        }
    }
    return $null
}

function Invoke-CandidateGeneration {
    if ($Iterations -ne 10) {
        throw 'R1 calibration requires exactly -Iterations 10.'
    }
    if (-not $RequireGpu) {
        throw 'R1 calibration requires -RequireGpu.'
    }
    if ($CodeHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'R1 calibration CodeHead must be 40 lowercase hexadecimal characters.'
    }
    if ($DecisionPath -cne 'Docs/RenderingValidation/R1Acceptance.md' -or
        $DecisionSection -cne 'R1 visual approval') {
        throw 'R1 calibration decision path or section is not canonical.'
    }
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
    if ($LASTEXITCODE -ne 0 -or $headCommit -notmatch '\A[0-9a-f]{40}\z') {
        throw 'Unable to resolve calibration HEAD.'
    }
    if ($headCommit -cne $CodeHead) {
        throw "Calibration HEAD does not match CodeHead: expected=$CodeHead actual=$headCommit."
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
        $expectedBaselineHash = if ($scene -eq 'indoor') { $BaselineIndoorSha256 } else { $BaselineOutdoorSha256 }
        if ($baselineHash -cne $expectedBaselineHash.ToUpperInvariant()) {
            throw "Baseline hash does not match the supplied identity for $scene."
        }
        $noise = @()
        for ($run = 1; $run -le $Iterations; ++$run) {
            $result = Invoke-FixedExecutable $goldenExecutable @("--scene=$scene", '--capture-source=back-buffer', '--measure-visual') $runsDirectory
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
            $measurementLines.Add("noise`t$scene`t$run`t`t`t$($metric.MeanText)`t$($metric.MaxText)`t$($metric.RawMax)`t$baselineHash`t$headCommit")
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
                MeanNanounits = $metric.MeanNanounits
                MaxNanounits = $metric.MaxNanounits
                RawMax = $metric.RawMax
            }
            $measurementLines.Add("artificial`t$scene`t$artificialRun`t$($specification.PatchSize)`t$($specification.ChannelDelta)`t$($metric.MeanText)`t$($metric.MaxText)`t$($metric.RawMax)`t$baselineHash`t$headCommit")
        }

        [uint64]$noiseMaximumNanounits = ($noise | Measure-Object -Property MeanNanounits -Maximum).Maximum
        $noiseMaximum = Convert-NanounitsToDecimal $noiseMaximumNanounits
        $selection = Select-R1StrictCandidate $artificial $noiseMaximumNanounits
        if ($null -eq $selection) {
            throw "No detectable artificial candidate exists for $scene."
        }
        $selected = $selection.Candidate
        [uint64]$selectedLimitMillionths = $selection.LimitMillionths

        $limitText = Format-Millionths $selectedLimitMillionths
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
    $thresholdManifestPath = Join-Path $calibrationDirectory 'VisualThresholdCandidateManifest.json'
    $thresholdManifest = [ordered]@{
        Schema = 'NorvesLib.RenderingVisualThresholdCandidate.R1.v1'
        CodeHead = $CodeHead
        BaselineManifestRelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Manifest.json'
        Baselines = [ordered]@{
            Indoor = [ordered]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'
                Sha256 = $BaselineIndoorSha256.ToUpperInvariant()
            }
            Outdoor = [ordered]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'
                Sha256 = $BaselineOutdoorSha256.ToUpperInvariant()
            }
        }
        Decision = [ordered]@{
            RelativePath = $DecisionPath
            Section = $DecisionSection
        }
        Candidate = [ordered]@{
            RelativePath = 'build/RenderingValidation/Calibration/VisualThresholds.candidate.tsv'
            Sha256 = $candidateHash.ToUpperInvariant()
        }
        Measurements = [ordered]@{
            RelativePath = 'build/RenderingValidation/Calibration/VisualThresholdMeasurements.tsv'
            Sha256 = (Get-FileHash -LiteralPath $measurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
            Rows = 60
            NoiseRows = 20
            ArtificialRows = 40
        }
        Policy = [ordered]@{
            Iterations = 10
            RequireGpu = $true
            CaptureSource = 'back-buffer'
            FlipCommit = 'b475eb4bf394ab877c42166c9eb0a84a02cc5b14'
            PixelsPerDegree = '67.0'
            RawMaximumChannelDelta = 8
        }
    }
    Write-AtomicUtf8Lines $thresholdManifestPath @((($thresholdManifest | ConvertTo-Json -Depth 8) -split "`r?`n"))
    Assert-R1ThresholdCandidateManifest (Read-R1StrictJsonFile $thresholdManifestPath)
    Write-Output "candidate_sha256=$candidateHash"
    Write-Output "candidate_path=$candidatePath"
    Write-Output "measurement_path=$measurementPath"
    Write-Output "manifest_path=$thresholdManifestPath"
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

function Convert-ValidatedMeasurementNanounits {
    param([string]$Value, [string]$Field, [string]$Scene, [string]$Run)
    try {
        return Convert-CanonicalF9ToNanounits $Value
    } catch {
        throw "Measurement $Field is not canonical F9 in [0,1] for $Scene run $Run."
    }
}

function Convert-ValidatedMeasurementDecimal {
    param([string]$Value, [string]$Field, [string]$Scene, [string]$Run)
    return Convert-NanounitsToDecimal (Convert-ValidatedMeasurementNanounits $Value $Field $Scene $Run)
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
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string]$CandidatePath,
        [string]$MeasurementPath,
        [string]$ApprovedHash,
        [Parameter(Mandatory = $true)][string]$ExpectedCurrentHead
    )
    if ($ExpectedCurrentHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'Expected current HEAD must be 40 lowercase hexadecimal characters.'
    }
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

    $measurementHeads = @($measurementRows.head_commit | Sort-Object -Unique)
    if ($measurementHeads.Count -ne 1 -or $measurementHeads[0] -ne $ExpectedCurrentHead) {
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
        [uint64]$limitMillionths = 0
        if (-not [int]::TryParse($candidate.patch_size, [ref]$patchSize) -or
            $patchSize -notin @(1, 2, 4, 8, 16) -or
            -not [int]::TryParse($candidate.channel_delta, [ref]$channelDelta) -or
            $channelDelta -notin @(1, 2, 4, 8) -or
            $candidate.mean_flip_limit -notmatch '^(0\.[0-9]{6}|1\.000000)$') {
            throw "Candidate numeric fields are invalid for $scene."
        }
        try {
            $limitMillionths = Convert-CanonicalF6ToMillionths $candidate.mean_flip_limit
        } catch {
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
            if ($row.head_commit -ne $ExpectedCurrentHead -or $row.baseline_sha256 -ne $baselineHashes[$scene]) {
                throw "Measurement identity is invalid for $scene run $($row.run)."
            }
            $rowRun = Convert-ValidatedMeasurementInteger $row.run 'run' $scene $row.run 20
            $meanNanounits = Convert-ValidatedMeasurementNanounits $row.mean_flip 'mean_flip' $scene $row.run
            $maximumNanounits = Convert-ValidatedMeasurementNanounits $row.max_flip 'max_flip' $scene $row.run
            $mean = Convert-NanounitsToDecimal $meanNanounits
            $maximum = Convert-NanounitsToDecimal $maximumNanounits
            $rawMaximum = Convert-ValidatedMeasurementInteger $row.raw_max 'raw_max' $scene $row.run 255
            $validatedRows += [pscustomobject]@{
                Source = $row
                Run = $rowRun
                Mean = $mean
                Maximum = $maximum
                MeanNanounits = $meanNanounits
                MaximumNanounits = $maximumNanounits
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

        [uint64]$noiseMaximumNanounits = (@($validatedRows | Where-Object { $_.Source.kind -eq 'noise' }).MeanNanounits |
            Measure-Object -Maximum).Maximum
        $orderedArtificial = @()
        for ($index = 0; $index -lt $specifications.Count; ++$index) {
            $specification = $specifications[$index]
            $key = "$($specification.PatchSize)/$($specification.ChannelDelta)"
            $artificial = $artificialByKey[$key]
            if ($null -eq $artificial -or $artificial.Row.Run -ne $index + 1) {
                throw "Artificial measurement cost order is invalid for $scene."
            }
            $orderedArtificial += [pscustomobject]@{
                PatchSize = $artificial.PatchSize
                ChannelDelta = $artificial.ChannelDelta
                MeanNanounits = $artificial.Row.MeanNanounits
            }
        }
        $selection = Select-R1StrictCandidate $orderedArtificial $noiseMaximumNanounits
        if ($null -eq $selection -or $limitMillionths -ne $selection.LimitMillionths -or
            $patchSize -ne $selection.Candidate.PatchSize -or $channelDelta -ne $selection.Candidate.ChannelDelta) {
            throw "Candidate selection, formula, or separation is invalid for $scene."
        }
        $expectedLimit = $selection.LimitMillionths
        $derivedSummaries += [pscustomobject]@{
            Scene = $scene
            NoiseMaximum = (Format-Nine (Convert-NanounitsToDecimal $noiseMaximumNanounits))
            Limit = (Format-Millionths $expectedLimit)
            NegativeMean = (Format-Nine (Convert-NanounitsToDecimal $selection.Candidate.MeanNanounits))
            PatchSize = $selection.Candidate.PatchSize
            ChannelDelta = $selection.Candidate.ChannelDelta
        }
    }
    return [pscustomobject]@{ Lines = $candidateLines; Hash = $actualHash }
}

function Assert-R1ThresholdApproval {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$DecisionPath,
        [Parameter(Mandatory = $true)][string]$DecisionSection,
        [Parameter(Mandatory = $true)][string]$CandidateHash,
        [Parameter(Mandatory = $true)][string]$ExpectedCodeHead
    )
    if ($DecisionPath -cne 'Docs/RenderingValidation/R1Acceptance.md' -or
        $DecisionSection -cne 'R1 visual approval') {
        throw 'R1 threshold decision path or section is not canonical.'
    }
    $absolutePath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot ($DecisionPath -replace '/', '\')))
    Assert-R1PathWithinRoot $absolutePath $RepoRoot 'R1 threshold decision path'
    Assert-R1NoReparseAncestors $absolutePath $RepoRoot 'R1 threshold decision path'
    if (-not [System.IO.File]::Exists($absolutePath)) {
        throw "R1 threshold decision document is missing: $absolutePath"
    }
    $document = [System.IO.File]::ReadAllText($absolutePath, [System.Text.UTF8Encoding]::new($false))
    $heading = '## R1 visual approval'
    $headingPattern = '(?m)^' + [regex]::Escape($heading) + '\s*$'
    $headingMatches = @([regex]::Matches($document, $headingPattern))
    if ($headingMatches.Count -ne 1) {
        throw 'R1 visual approval heading must occur exactly once.'
    }
    $start = $headingMatches[0].Index + $headingMatches[0].Length
    $nextHeading = [regex]::Match($document.Substring($start), '(?m)^#{1,2}\s+.*$')
    $section = if ($nextHeading.Success) {
        $document.Substring($start, $nextHeading.Index)
    } else {
        $document.Substring($start)
    }
    $approvalLine = "R1 visual threshold candidate SHA256=$CandidateHash CodeHead=$ExpectedCodeHead を承認する。"
    $documentLines = @($document -split "`r?`n" | Where-Object { $_ -ceq $approvalLine })
    $sectionLines = @($section -split "`r?`n" | Where-Object { $_ -ceq $approvalLine })
    if ($documentLines.Count -ne 1 -or $sectionLines.Count -ne 1) {
        throw 'R1 threshold approval line must occur exactly once inside its section.'
    }
}

function Invoke-ThresholdPublishTransaction {
    param(
        [Parameter(Mandatory = $true)][string]$CandidatePath,
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$TemporaryPath,
        [Parameter(Mandatory = $true)][string]$BackupPath,
        [Parameter(Mandatory = $true)][string]$RollbackDiscardPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSourceHash,
        [string]$FailureMode
    )
    if ($FailureMode -and $FailureMode -cnotin @('before-publish', 'after-publish')) {
        throw "Unsupported threshold transaction failure mode: $FailureMode"
    }
    $published = $false
    $rollbackFailed = $false
    try {
        if ($FailureMode -ceq 'before-publish') {
            throw 'Injected threshold transaction failure before publish.'
        }
        [System.IO.File]::WriteAllBytes($TemporaryPath, [System.IO.File]::ReadAllBytes($CandidatePath))
        if ((Get-FileHash -LiteralPath $TemporaryPath -Algorithm SHA256).Hash.ToUpperInvariant() -cne
            (Get-FileHash -LiteralPath $CandidatePath -Algorithm SHA256).Hash.ToUpperInvariant()) {
            throw 'Threshold transaction temporary hash mismatch.'
        }
        [System.IO.File]::Replace($TemporaryPath, $SourcePath, $BackupPath, $true)
        $published = $true
        if ($FailureMode -ceq 'after-publish') {
            throw 'Injected threshold transaction failure after publish.'
        }
    } catch {
        $publishError = $_
        if ($published) {
            try {
                [System.IO.File]::Replace($BackupPath, $SourcePath, $RollbackDiscardPath, $true)
                if ((Get-FileHash -LiteralPath $SourcePath -Algorithm SHA256).Hash.ToUpperInvariant() -cne
                    $ExpectedSourceHash.ToUpperInvariant()) {
                    throw 'Threshold rollback hash mismatch.'
                }
            } catch {
                $rollbackFailed = $true
                throw "Threshold publish failed and rollback also failed: publish=$publishError rollback=$($_.Exception.Message)"
            }
        }
        throw $publishError
    } finally {
        foreach ($path in @($TemporaryPath, $RollbackDiscardPath)) {
            if ([System.IO.File]::Exists($path)) {
                Remove-Item -LiteralPath $path -Force
            }
        }
        if (-not $rollbackFailed -and [System.IO.File]::Exists($BackupPath)) {
            Remove-Item -LiteralPath $BackupPath -Force
        }
    }
}

function Invoke-R1ThresholdPublish {
    if ($DecisionPath -cne 'Docs/RenderingValidation/R1Acceptance.md' -or
        $DecisionSection -cne 'R1 visual approval') {
        throw 'R1 threshold publish decision arguments are not canonical.'
    }
    if ($CodeHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'R1 threshold publish CodeHead must be lowercase hexadecimal.'
    }
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $buildRoot = Join-Path $repoRoot 'build'
    $calibrationDirectory = Join-Path $buildRoot 'RenderingValidation\Calibration'
    $candidatePath = Join-Path $calibrationDirectory 'VisualThresholds.candidate.tsv'
    $measurementPath = Join-Path $calibrationDirectory 'VisualThresholdMeasurements.tsv'
    $thresholdManifestPath = Join-Path $calibrationDirectory 'VisualThresholdCandidateManifest.json'
    $baselineManifestPath = Join-Path $buildRoot 'RenderingValidation\R1\BaselineCandidate\Manifest.json'
    $sourceRoot = Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation'
    $sourcePath = Join-Path $sourceRoot 'VisualThresholds.tsv'
    $currentHead = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $currentHead -cne $CodeHead) {
        throw "Current HEAD does not match CodeHead: expected=$CodeHead actual=$currentHead."
    }
    $validated = Get-R1ThresholdPublishInputs $repoRoot $buildRoot $calibrationDirectory $sourceRoot `
        $baselineManifestPath $thresholdManifestPath $candidatePath $measurementPath $sourcePath `
        $currentHead $BaselineIndoorSha256 $BaselineOutdoorSha256 $CandidateSha256 $DecisionPath $DecisionSection
    $candidateHash = $validated.CandidateHash

    $temporaryPath = Join-Path $calibrationDirectory 'VisualThresholds.tsv.tmp'
    $backupPath = Join-Path $calibrationDirectory 'VisualThresholds.tsv.bak'
    $rollbackDiscardPath = Join-Path $calibrationDirectory 'VisualThresholds.tsv.rollback-discard'
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $failureMode = [Environment]::GetEnvironmentVariable('NORVESLIB_VISUAL_THRESHOLD_TRANSACTION_TEST_FAILURE')
    Invoke-ThresholdPublishTransaction $candidatePath $sourcePath $temporaryPath $backupPath `
        $rollbackDiscardPath $sourceHash $failureMode
    Write-Output "published_candidate_sha256=$candidateHash"
    Write-Output "source_threshold_path=$sourcePath"
}

function Invoke-ApprovedCandidateValidationSelfTest {
    Invoke-R1ContractSelfTest
}

function Assert-R1ExactPropertySet {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string[]]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($null -eq $Object) {
        throw "$Label is missing."
    }
    $actual = @($Object.psobject.Properties | ForEach-Object { [string]$_.Name })
    if ($actual.Count -ne $Expected.Count -or
        @($actual | Where-Object { $_ -cnotin $Expected }).Count -ne 0 -or
        @($Expected | Where-Object { $_ -cnotin $actual }).Count -ne 0) {
        throw "$Label property set is invalid."
    }
}

function Test-R1JsonInteger {
    param($Value)
    return ($Value -is [byte] -or $Value -is [sbyte] -or $Value -is [int16] -or
        $Value -is [uint16] -or $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64])
}

function Read-R1StrictJsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $text = [System.IO.File]::ReadAllText($Path, [System.Text.UTF8Encoding]::new($false))
    [NorvesLibR1ThresholdStrictJson]::ValidateNoDuplicateProperties($text)
    return ($text | ConvertFrom-Json)
}

function Assert-R1BaselineBridgeManifest {
    param([Parameter(Mandatory = $true)]$Manifest)
    Assert-R1ExactPropertySet $Manifest @('Schema', 'CodeHead', 'CaptureSource', 'Candidates', 'SourceStart') 'baseline bridge manifest'
    if ($Manifest.Schema -isnot [string] -or
        $Manifest.Schema -cne 'NorvesLib.RenderingBaselineCandidate.R1.v1' -or
        $Manifest.CodeHead -isnot [string] -or $Manifest.CodeHead -cnotmatch '\A[0-9a-f]{40}\z' -or
        $Manifest.CaptureSource -isnot [string] -or $Manifest.CaptureSource -cne 'back-buffer') {
        throw 'Baseline bridge manifest identity is invalid.'
    }
    Assert-R1ExactPropertySet $Manifest.Candidates @('Indoor', 'Outdoor') 'baseline bridge Candidates'
    Assert-R1ExactPropertySet $Manifest.SourceStart @('Indoor', 'Outdoor') 'baseline bridge SourceStart'
    foreach ($scene in @('Indoor', 'Outdoor')) {
        $candidate = $Manifest.Candidates.$scene
        $source = $Manifest.SourceStart.$scene
        Assert-R1ExactPropertySet $candidate @('RelativePath', 'Sha256') "baseline bridge Candidates.$scene"
        Assert-R1ExactPropertySet $source @('RelativePath', 'Sha256') "baseline bridge SourceStart.$scene"
        if ($candidate.RelativePath -isnot [string] -or
            $candidate.RelativePath -cne "build/RenderingValidation/R1/BaselineCandidate/$scene.png" -or
            $source.RelativePath -isnot [string] -or
            $source.RelativePath -cne "Test/Core/Rendering/Baselines/RenderingValidation/$scene.png" -or
            $candidate.Sha256 -isnot [string] -or $candidate.Sha256 -cnotmatch '\A[0-9A-F]{64}\z' -or
            $source.Sha256 -isnot [string] -or $source.Sha256 -cnotmatch '\A[0-9A-F]{64}\z') {
            throw "Baseline bridge manifest path or hash is invalid for $scene."
        }
    }
}

function Assert-R1ThresholdCandidateManifest {
    param([Parameter(Mandatory = $true)]$Manifest)

    Assert-R1ExactPropertySet $Manifest @(
        'Schema', 'CodeHead', 'BaselineManifestRelativePath', 'Baselines',
        'Decision', 'Candidate', 'Measurements', 'Policy') 'threshold manifest'
    if ($Manifest.Schema -isnot [string] -or
        $Manifest.Schema -cne 'NorvesLib.RenderingVisualThresholdCandidate.R1.v1' -or
        $Manifest.CodeHead -isnot [string] -or
        $Manifest.CodeHead -cnotmatch '\A[0-9a-f]{40}\z' -or
        $Manifest.BaselineManifestRelativePath -isnot [string] -or
        $Manifest.BaselineManifestRelativePath -cne 'build/RenderingValidation/R1/BaselineCandidate/Manifest.json') {
        throw 'Threshold manifest identity is invalid.'
    }

    Assert-R1ExactPropertySet $Manifest.Baselines @('Indoor', 'Outdoor') 'threshold manifest Baselines'
    foreach ($scene in @('Indoor', 'Outdoor')) {
        $baseline = $Manifest.Baselines.$scene
        Assert-R1ExactPropertySet $baseline @('RelativePath', 'Sha256') "threshold manifest Baselines.$scene"
        $expectedPath = "Test/Core/Rendering/Baselines/RenderingValidation/$scene.png"
        if ($baseline.RelativePath -isnot [string] -or $baseline.RelativePath -cne $expectedPath -or
            $baseline.Sha256 -isnot [string] -or $baseline.Sha256 -cnotmatch '\A[0-9A-F]{64}\z') {
            throw "Threshold manifest baseline identity is invalid for $scene."
        }
    }

    Assert-R1ExactPropertySet $Manifest.Decision @('RelativePath', 'Section') 'threshold manifest Decision'
    if ($Manifest.Decision.RelativePath -isnot [string] -or
        $Manifest.Decision.RelativePath -cne 'Docs/RenderingValidation/R1Acceptance.md' -or
        $Manifest.Decision.Section -isnot [string] -or
        $Manifest.Decision.Section -cne 'R1 visual approval') {
        throw 'Threshold manifest decision identity is invalid.'
    }

    Assert-R1ExactPropertySet $Manifest.Candidate @('RelativePath', 'Sha256') 'threshold manifest Candidate'
    if ($Manifest.Candidate.RelativePath -isnot [string] -or
        $Manifest.Candidate.RelativePath -cne 'build/RenderingValidation/Calibration/VisualThresholds.candidate.tsv' -or
        $Manifest.Candidate.Sha256 -isnot [string] -or
        $Manifest.Candidate.Sha256 -cnotmatch '\A[0-9A-F]{64}\z') {
        throw 'Threshold manifest candidate identity is invalid.'
    }

    Assert-R1ExactPropertySet $Manifest.Measurements @(
        'RelativePath', 'Sha256', 'Rows', 'NoiseRows', 'ArtificialRows') 'threshold manifest Measurements'
    if ($Manifest.Measurements.RelativePath -isnot [string] -or
        $Manifest.Measurements.RelativePath -cne 'build/RenderingValidation/Calibration/VisualThresholdMeasurements.tsv' -or
        $Manifest.Measurements.Sha256 -isnot [string] -or
        $Manifest.Measurements.Sha256 -cnotmatch '\A[0-9A-F]{64}\z' -or
        -not (Test-R1JsonInteger $Manifest.Measurements.Rows) -or $Manifest.Measurements.Rows -ne 60 -or
        -not (Test-R1JsonInteger $Manifest.Measurements.NoiseRows) -or $Manifest.Measurements.NoiseRows -ne 20 -or
        -not (Test-R1JsonInteger $Manifest.Measurements.ArtificialRows) -or $Manifest.Measurements.ArtificialRows -ne 40) {
        throw 'Threshold manifest measurement identity is invalid.'
    }

    Assert-R1ExactPropertySet $Manifest.Policy @(
        'Iterations', 'RequireGpu', 'CaptureSource', 'FlipCommit',
        'PixelsPerDegree', 'RawMaximumChannelDelta') 'threshold manifest Policy'
    if (-not (Test-R1JsonInteger $Manifest.Policy.Iterations) -or $Manifest.Policy.Iterations -ne 10 -or
        $Manifest.Policy.RequireGpu -isnot [bool] -or
        -not $Manifest.Policy.RequireGpu -or $Manifest.Policy.CaptureSource -isnot [string] -or
        $Manifest.Policy.CaptureSource -cne 'back-buffer' -or
        $Manifest.Policy.FlipCommit -isnot [string] -or
        $Manifest.Policy.FlipCommit -cne 'b475eb4bf394ab877c42166c9eb0a84a02cc5b14' -or
        $Manifest.Policy.PixelsPerDegree -isnot [string] -or
        $Manifest.Policy.PixelsPerDegree -cne '67.0' -or
        -not (Test-R1JsonInteger $Manifest.Policy.RawMaximumChannelDelta) -or
        $Manifest.Policy.RawMaximumChannelDelta -ne 8) {
        throw 'Threshold manifest policy is invalid.'
    }
}

function Assert-R1ThresholdFileHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedHash,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if (-not [System.IO.File]::Exists($Path)) {
        throw "$Label is missing: $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualHash -cne $ExpectedHash.ToUpperInvariant()) {
        throw "$Label hash mismatch: expected=$($ExpectedHash.ToUpperInvariant()) actual=$actualHash"
    }
    return $actualHash
}

function Get-R1ThresholdPublishInputs {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$CalibrationDirectory,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$BaselineManifestPath,
        [Parameter(Mandatory = $true)][string]$ThresholdManifestPath,
        [Parameter(Mandatory = $true)][string]$CandidatePath,
        [Parameter(Mandatory = $true)][string]$MeasurementPath,
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$ExpectedCurrentHead,
        [Parameter(Mandatory = $true)][string]$BaselineIndoorHash,
        [Parameter(Mandatory = $true)][string]$BaselineOutdoorHash,
        [Parameter(Mandatory = $true)][string]$CandidateHash,
        [Parameter(Mandatory = $true)][string]$DecisionPath,
        [Parameter(Mandatory = $true)][string]$DecisionSection
    )
    if ($ExpectedCurrentHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'Expected current HEAD must be 40 lowercase hexadecimal characters.'
    }
    Assert-R1PathWithinRoot $BuildRoot $RepoRoot 'Build root'
    Assert-R1PathWithinRoot $CalibrationDirectory $BuildRoot 'Calibration directory'
    Assert-R1PathWithinRoot $SourceRoot $RepoRoot 'Threshold source root'
    foreach ($path in @($BaselineManifestPath, $ThresholdManifestPath, $CandidatePath, $MeasurementPath)) {
        Assert-R1PathWithinRoot $path $BuildRoot 'Threshold build input'
    }
    Assert-R1PathWithinRoot $SourcePath $SourceRoot 'Threshold source'
    Assert-R1NoReparseAncestors $BuildRoot $RepoRoot 'Build root'
    Assert-R1NoReparseAncestors $CalibrationDirectory $BuildRoot 'Calibration directory'
    Assert-R1NoReparseAncestors $SourceRoot $RepoRoot 'Threshold source root'
    foreach ($path in @($BaselineManifestPath, $ThresholdManifestPath, $CandidatePath, $MeasurementPath)) {
        Assert-R1NoReparseAncestors $path $BuildRoot 'Threshold build input'
    }
    Assert-R1NoReparseAncestors $SourcePath $SourceRoot 'Threshold source'
    foreach ($path in @($BaselineManifestPath, $ThresholdManifestPath, $CandidatePath, $MeasurementPath, $SourcePath)) {
        if (-not [System.IO.File]::Exists($path)) {
            throw "Required threshold publish input is missing: $path"
        }
    }

    $baselineManifest = Read-R1StrictJsonFile $BaselineManifestPath
    Assert-R1BaselineBridgeManifest $baselineManifest
    if ($baselineManifest.CodeHead -cne $ExpectedCurrentHead) {
        throw 'Baseline bridge manifest CodeHead does not match threshold publish CodeHead.'
    }
    $indoorSourcePath = Join-Path $SourceRoot 'Indoor.png'
    $outdoorSourcePath = Join-Path $SourceRoot 'Outdoor.png'
    Assert-R1PathWithinRoot $indoorSourcePath $SourceRoot 'Indoor baseline source'
    Assert-R1PathWithinRoot $outdoorSourcePath $SourceRoot 'Outdoor baseline source'
    Assert-R1NoReparseAncestors $indoorSourcePath $SourceRoot 'Indoor baseline source'
    Assert-R1NoReparseAncestors $outdoorSourcePath $SourceRoot 'Outdoor baseline source'
    $indoorBaselineHash = Assert-R1ThresholdFileHash $indoorSourcePath $BaselineIndoorHash 'Indoor baseline source'
    $outdoorBaselineHash = Assert-R1ThresholdFileHash $outdoorSourcePath $BaselineOutdoorHash 'Outdoor baseline source'
    if ($baselineManifest.Candidates.Indoor.Sha256 -cne $indoorBaselineHash -or
        $baselineManifest.Candidates.Outdoor.Sha256 -cne $outdoorBaselineHash) {
        throw 'Baseline bridge candidate hashes do not match the published baselines.'
    }

    $thresholdManifest = Read-R1StrictJsonFile $ThresholdManifestPath
    Assert-R1ThresholdCandidateManifest $thresholdManifest
    if ($thresholdManifest.CodeHead -cne $ExpectedCurrentHead -or
        $thresholdManifest.Baselines.Indoor.Sha256 -cne $indoorBaselineHash -or
        $thresholdManifest.Baselines.Outdoor.Sha256 -cne $outdoorBaselineHash) {
        throw 'Threshold manifest code or baseline identity does not match publish inputs.'
    }
    $actualCandidateHash = Assert-R1ThresholdFileHash $CandidatePath $CandidateHash 'Threshold candidate'
    $actualMeasurementHash = (Get-FileHash -LiteralPath $MeasurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($thresholdManifest.Candidate.Sha256 -cne $actualCandidateHash -or
        $thresholdManifest.Measurements.Sha256 -cne $actualMeasurementHash) {
        throw 'Threshold candidate or measurement hash does not match its manifest.'
    }
    Assert-R1ThresholdApproval $RepoRoot $DecisionPath $DecisionSection $actualCandidateHash $ExpectedCurrentHead
    $validated = Get-ValidatedApprovedCandidate $RepoRoot $CandidatePath $MeasurementPath `
        $actualCandidateHash $ExpectedCurrentHead
    return [pscustomobject]@{
        BaselineManifest = $baselineManifest
        ThresholdManifest = $thresholdManifest
        CandidateHash = $actualCandidateHash
        MeasurementHash = $actualMeasurementHash
        Validated = $validated
    }
}

function Assert-R1SelfTestRejects {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )
    $rejected = $false
    try {
        & $Action | Out-Null
    } catch {
        $rejected = $true
    }
    if (-not $rejected) {
        throw "R1 self-test negative '$Name' was accepted."
    }
}

function New-R1ThresholdSyntheticFixture {
    param(
        [Parameter(Mandatory = $true)][string]$SelfTestRoot,
        [Parameter(Mandatory = $true)][string]$CodeHead
    )
    $repoRoot = Join-Path $SelfTestRoot 'threshold-repo'
    $buildRoot = Join-Path $repoRoot 'build'
    $calibrationDirectory = Join-Path $buildRoot 'RenderingValidation\Calibration'
    $baselineCandidateDirectory = Join-Path $buildRoot 'RenderingValidation\R1\BaselineCandidate'
    $sourceRoot = Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation'
    $decisionPath = Join-Path $repoRoot 'Docs\RenderingValidation\R1Acceptance.md'
    $baselineManifestPath = Join-Path $baselineCandidateDirectory 'Manifest.json'
    $thresholdManifestPath = Join-Path $calibrationDirectory 'VisualThresholdCandidateManifest.json'
    $candidatePath = Join-Path $calibrationDirectory 'VisualThresholds.candidate.tsv'
    $measurementPath = Join-Path $calibrationDirectory 'VisualThresholdMeasurements.tsv'
    $sourcePath = Join-Path $sourceRoot 'VisualThresholds.tsv'
    foreach ($directory in @($calibrationDirectory, $baselineCandidateDirectory, $sourceRoot,
                             (Split-Path -Parent $decisionPath))) {
        [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    }

    $indoorBaselinePath = Join-Path $sourceRoot 'Indoor.png'
    $outdoorBaselinePath = Join-Path $sourceRoot 'Outdoor.png'
    [System.IO.File]::WriteAllBytes($indoorBaselinePath, [System.Text.Encoding]::UTF8.GetBytes('synthetic-indoor-baseline'))
    [System.IO.File]::WriteAllBytes($outdoorBaselinePath, [System.Text.Encoding]::UTF8.GetBytes('synthetic-outdoor-baseline'))
    [System.IO.File]::Copy($indoorBaselinePath, (Join-Path $baselineCandidateDirectory 'Indoor.png'), $true)
    [System.IO.File]::Copy($outdoorBaselinePath, (Join-Path $baselineCandidateDirectory 'Outdoor.png'), $true)
    $indoorHash = (Get-FileHash -LiteralPath $indoorBaselinePath -Algorithm SHA256).Hash.ToUpperInvariant()
    $outdoorHash = (Get-FileHash -LiteralPath $outdoorBaselinePath -Algorithm SHA256).Hash.ToUpperInvariant()

    $measurementHeader = "kind`tscene`trun`tpatch_size`tchannel_delta`tmean_flip`tmax_flip`traw_max`tbaseline_sha256`thead_commit"
    $measurementLines = [System.Collections.Generic.List[string]]::new()
    $measurementLines.Add($measurementHeader)
    $specifications = foreach ($patchSize in @(1, 2, 4, 8, 16)) {
        foreach ($channelDelta in @(1, 2, 4, 8)) {
            [pscustomobject]@{
                PatchSize = $patchSize
                ChannelDelta = $channelDelta
                ChangeAmount = $patchSize * $patchSize * $channelDelta
            }
        }
    }
    $specifications = @($specifications | Sort-Object ChangeAmount, PatchSize, ChannelDelta)
    foreach ($scene in @('indoor', 'outdoor')) {
        $baselineHash = if ($scene -ceq 'indoor') { $indoorHash } else { $outdoorHash }
        for ($run = 1; $run -le 10; ++$run) {
            $measurementLines.Add("noise`t$scene`t$run`t`t`t0.000001000`t0.000001000`t0`t$baselineHash`t$CodeHead")
        }
        $run = 0
        foreach ($specification in $specifications) {
            ++$run
            $measurementLines.Add("artificial`t$scene`t$run`t$($specification.PatchSize)`t$($specification.ChannelDelta)`t0.100000000`t0.100000000`t$($specification.ChannelDelta)`t$baselineHash`t$CodeHead")
        }
    }
    $candidateHeader = "scene`tmean_flip_limit`tmax_channel_delta`tpixels_per_degree`tpatch_size`tchannel_delta`tflip_commit"
    $candidateLines = @(
        $candidateHeader,
        "indoor`t0.025001`t8`t67.0`t1`t1`tb475eb4bf394ab877c42166c9eb0a84a02cc5b14",
        "outdoor`t0.025001`t8`t67.0`t1`t1`tb475eb4bf394ab877c42166c9eb0a84a02cc5b14")
    Write-AtomicUtf8Lines $measurementPath $measurementLines.ToArray()
    Write-AtomicUtf8Lines $candidatePath $candidateLines
    [System.IO.File]::WriteAllText($sourcePath, "synthetic-original-threshold`n", [System.Text.UTF8Encoding]::new($false))
    $candidateHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash.ToUpperInvariant()
    $measurementHash = (Get-FileHash -LiteralPath $measurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $baselineManifest = [ordered]@{
        Schema = 'NorvesLib.RenderingBaselineCandidate.R1.v1'
        CodeHead = $CodeHead
        CaptureSource = 'back-buffer'
        Candidates = [ordered]@{
            Indoor = [ordered]@{ RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Indoor.png'; Sha256 = $indoorHash }
            Outdoor = [ordered]@{ RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Outdoor.png'; Sha256 = $outdoorHash }
        }
        SourceStart = [ordered]@{
            Indoor = [ordered]@{ RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'; Sha256 = $indoorHash }
            Outdoor = [ordered]@{ RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'; Sha256 = $outdoorHash }
        }
    }
    $baselineManifestText = (($baselineManifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
    Write-AtomicUtf8Lines $baselineManifestPath @($baselineManifestText -split "`r?`n")
    $thresholdManifest = [ordered]@{
        Schema = 'NorvesLib.RenderingVisualThresholdCandidate.R1.v1'
        CodeHead = $CodeHead
        BaselineManifestRelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Manifest.json'
        Baselines = [ordered]@{
            Indoor = [ordered]@{ RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'; Sha256 = $indoorHash }
            Outdoor = [ordered]@{ RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'; Sha256 = $outdoorHash }
        }
        Decision = [ordered]@{ RelativePath = 'Docs/RenderingValidation/R1Acceptance.md'; Section = 'R1 visual approval' }
        Candidate = [ordered]@{ RelativePath = 'build/RenderingValidation/Calibration/VisualThresholds.candidate.tsv'; Sha256 = $candidateHash }
        Measurements = [ordered]@{
            RelativePath = 'build/RenderingValidation/Calibration/VisualThresholdMeasurements.tsv'
            Sha256 = $measurementHash
            Rows = 60
            NoiseRows = 20
            ArtificialRows = 40
        }
        Policy = [ordered]@{
            Iterations = 10
            RequireGpu = $true
            CaptureSource = 'back-buffer'
            FlipCommit = 'b475eb4bf394ab877c42166c9eb0a84a02cc5b14'
            PixelsPerDegree = '67.0'
            RawMaximumChannelDelta = 8
        }
    }
    $thresholdManifestText = (($thresholdManifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
    Write-AtomicUtf8Lines $thresholdManifestPath @($thresholdManifestText -split "`r?`n")
    $thresholdApprovalLine = "R1 visual threshold candidate SHA256=$candidateHash CodeHead=$CodeHead を承認する。"
    $decisionText = "# Synthetic R1`n`n## R1 visual approval`n$thresholdApprovalLine`n`n## Next`n"
    Write-AtomicUtf8Lines $decisionPath @($decisionText -split "`r?`n")
    return [pscustomobject]@{
        RepoRoot = $repoRoot
        BuildRoot = $buildRoot
        CalibrationDirectory = $calibrationDirectory
        SourceRoot = $sourceRoot
        BaselineManifestPath = $baselineManifestPath
        ThresholdManifestPath = $thresholdManifestPath
        CandidatePath = $candidatePath
        MeasurementPath = $measurementPath
        SourcePath = $sourcePath
        DecisionPath = 'Docs/RenderingValidation/R1Acceptance.md'
        DecisionSection = 'R1 visual approval'
        CodeHead = $CodeHead
        IndoorHash = $indoorHash
        OutdoorHash = $outdoorHash
        CandidateHash = $candidateHash
        MeasurementHash = $measurementHash
        CandidateLines = $candidateLines
        MeasurementLines = $measurementLines.ToArray()
        BaselineManifestText = $baselineManifestText
        ThresholdManifestText = $thresholdManifestText
        DecisionText = $decisionText
    }
}

function New-R1SyntheticManifest {
    return [pscustomobject]@{
        Schema = 'NorvesLib.RenderingVisualThresholdCandidate.R1.v1'
        CodeHead = ('a' * 40)
        BaselineManifestRelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Manifest.json'
        Baselines = [pscustomobject]@{
            Indoor = [pscustomobject]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'
                Sha256 = ('A' * 64)
            }
            Outdoor = [pscustomobject]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'
                Sha256 = ('B' * 64)
            }
        }
        Decision = [pscustomobject]@{
            RelativePath = 'Docs/RenderingValidation/R1Acceptance.md'
            Section = 'R1 visual approval'
        }
        Candidate = [pscustomobject]@{
            RelativePath = 'build/RenderingValidation/Calibration/VisualThresholds.candidate.tsv'
            Sha256 = ('C' * 64)
        }
        Measurements = [pscustomobject]@{
            RelativePath = 'build/RenderingValidation/Calibration/VisualThresholdMeasurements.tsv'
            Sha256 = ('D' * 64)
            Rows = 60
            NoiseRows = 20
            ArtificialRows = 40
        }
        Policy = [pscustomobject]@{
            Iterations = 10
            RequireGpu = $true
            CaptureSource = 'back-buffer'
            FlipCommit = 'b475eb4bf394ab877c42166c9eb0a84a02cc5b14'
            PixelsPerDegree = '67.0'
            RawMaximumChannelDelta = 8
        }
    }
}

function Invoke-R1ContractSelfTest {
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
    $selfTestRoot = Join-Path $repoRoot 'build\RenderingValidation\R1\SelfTest\Threshold'
    if (-not $selfTestRoot.StartsWith($buildRoot.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Threshold self-test root is outside the build root.'
    }
    if ([System.IO.Directory]::Exists($selfTestRoot)) {
        $rootAttributes = ([System.IO.DirectoryInfo]$selfTestRoot).Attributes
        if (($rootAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Threshold self-test root is a reparse point.'
        }
    }
    if ([System.IO.Directory]::Exists($selfTestRoot)) {
        Remove-Item -LiteralPath $selfTestRoot -Recurse -Force
    }
    [System.IO.Directory]::CreateDirectory($selfTestRoot) | Out-Null
    try {
        Invoke-ParserSelfTest | Out-Null
        foreach ($value in @('0.000000000', '0.123456789', '1.000000000')) {
            [void](Convert-CanonicalF9ToNanounits $value)
        }
        foreach ($value in @(
            ' 0.000000000', '0.000000000 ', '+0.000000000', '1e-9',
            'NaN', 'Infinity', '0.00000000', '0.0000000000', '1.00000000')) {
            Assert-R1SelfTestRejects "canonical_f9_$value" { Convert-CanonicalF9ToNanounits $value }
        }

        $header = "kind`tscene`trun`tpatch_size`tchannel_delta`tmean_flip`tmax_flip`traw_max`tbaseline_sha256`thead_commit"
        $row = "noise`tindoor`t1`t`t`t0.000000000`t0.000000000`t0`t$('A' * 64)`t$('a' * 40)"
        foreach ($count in @(59, 60, 61)) {
            $path = Join-Path $selfTestRoot "measurements-$count.tsv"
            $lines = @($header) + @($row) * $count
            [System.IO.File]::WriteAllLines($path, $lines, [System.Text.UTF8Encoding]::new($false))
            if ($count -eq 60) {
                if (@(Get-StrictTsvLines $path $header 60).Count -ne 61) {
                    throw 'The 60-row measurement shape was not accepted.'
                }
            } else {
                Assert-R1SelfTestRejects "rows_$count" { Get-StrictTsvLines $path $header 60 }
            }
        }

        $validManifest = New-R1SyntheticManifest
        Assert-R1ThresholdCandidateManifest $validManifest
        $p5Manifest = [pscustomobject]@{
            Schema = 'NorvesLib.RenderingVisualThresholdCandidate.P5.v1'
            CodeHead = ('a' * 40)
        }
        Assert-R1SelfTestRejects 'p5_manifest' { Assert-R1ThresholdCandidateManifest $p5Manifest }
        $numberAsStringManifest = New-R1SyntheticManifest
        $numberAsStringManifest.Measurements.Rows = '60'
        $numberAsStringManifest.Measurements.NoiseRows = '20'
        $numberAsStringManifest.Measurements.ArtificialRows = '40'
        $numberAsStringManifest.Policy.Iterations = '10'
        $numberAsStringManifest.Policy.RawMaximumChannelDelta = '8'
        Assert-R1SelfTestRejects 'manifest_number_as_string' {
            Assert-R1ThresholdCandidateManifest $numberAsStringManifest
        }

        $r0Paths = @(
            (Join-Path $repoRoot 'Docs\RenderingValidation\R0Acceptance.md'),
            (Join-Path $repoRoot 'Docs\RenderingValidation\PerceptualDiffSelection.md'))
        $r0Hashes = @{}
        foreach ($path in $r0Paths) {
            if (-not [System.IO.File]::Exists($path)) {
                throw "R0 immutable document is missing: $path"
            }
            $r0Hashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        }
        $thresholdFixture = New-R1ThresholdSyntheticFixture $selfTestRoot ('a' * 40)
        $validateThresholdFixture = {
            Get-R1ThresholdPublishInputs $thresholdFixture.RepoRoot $thresholdFixture.BuildRoot `
                $thresholdFixture.CalibrationDirectory $thresholdFixture.SourceRoot `
                $thresholdFixture.BaselineManifestPath $thresholdFixture.ThresholdManifestPath `
                $thresholdFixture.CandidatePath $thresholdFixture.MeasurementPath $thresholdFixture.SourcePath `
                $thresholdFixture.CodeHead $thresholdFixture.IndoorHash $thresholdFixture.OutdoorHash `
                $thresholdFixture.CandidateHash $thresholdFixture.DecisionPath $thresholdFixture.DecisionSection | Out-Null
        }
        & $validateThresholdFixture
        $writeThresholdManifest = {
            param($manifest)
            $text = (($manifest | ConvertTo-Json -Depth 12) + [Environment]::NewLine)
            [System.IO.File]::WriteAllText($thresholdFixture.ThresholdManifestPath, $text, [System.Text.UTF8Encoding]::new($false))
        }
        $restoreThresholdManifest = {
            [System.IO.File]::WriteAllText($thresholdFixture.ThresholdManifestPath,
                $thresholdFixture.ThresholdManifestText, [System.Text.UTF8Encoding]::new($false))
        }
        $restoreThresholdDecision = {
            [System.IO.File]::WriteAllText((Join-Path $thresholdFixture.RepoRoot 'Docs\RenderingValidation\R1Acceptance.md'),
                $thresholdFixture.DecisionText, [System.Text.UTF8Encoding]::new($false))
        }
        $variantMeasurementLines = @($thresholdFixture.MeasurementLines)
        $variantMeasurementLines[11] = $variantMeasurementLines[11] -replace '^artificial', 'noise'
        [System.IO.File]::WriteAllLines($thresholdFixture.MeasurementPath, $variantMeasurementLines,
            [System.Text.UTF8Encoding]::new($false))
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Measurements.Sha256 = (Get-FileHash -LiteralPath $thresholdFixture.MeasurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'noise_10_plus_10_violation' $validateThresholdFixture
        & $restoreThresholdManifest

        $variantMeasurementLines = @($thresholdFixture.MeasurementLines)
        $variantMeasurementLines[1] = $variantMeasurementLines[1] -replace '^noise', 'artificial'
        [System.IO.File]::WriteAllLines($thresholdFixture.MeasurementPath, $variantMeasurementLines,
            [System.Text.UTF8Encoding]::new($false))
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Measurements.Sha256 = (Get-FileHash -LiteralPath $thresholdFixture.MeasurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'artificial_40_row_violation' $validateThresholdFixture
        & $restoreThresholdManifest

        $variantMeasurementLines = @($thresholdFixture.MeasurementLines)
        $firstArtificial = $variantMeasurementLines[11].Split("`t")
        $secondArtificial = $variantMeasurementLines[12].Split("`t")
        $temporaryField = $firstArtificial[3]
        $firstArtificial[3] = $secondArtificial[3]
        $secondArtificial[3] = $temporaryField
        $temporaryField = $firstArtificial[4]
        $firstArtificial[4] = $secondArtificial[4]
        $secondArtificial[4] = $temporaryField
        $temporaryField = $firstArtificial[7]
        $firstArtificial[7] = $secondArtificial[7]
        $secondArtificial[7] = $temporaryField
        $variantMeasurementLines[11] = $firstArtificial -join "`t"
        $variantMeasurementLines[12] = $secondArtificial -join "`t"
        [System.IO.File]::WriteAllLines($thresholdFixture.MeasurementPath, $variantMeasurementLines,
            [System.Text.UTF8Encoding]::new($false))
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Measurements.Sha256 = (Get-FileHash -LiteralPath $thresholdFixture.MeasurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'artificial_fixed_order_violation' $validateThresholdFixture
        & $restoreThresholdManifest

        $variantCandidateLines = @($thresholdFixture.CandidateLines)
        $variantCandidateLines[1] = $variantCandidateLines[1] -replace '0\.025001', '0.025002'
        $variantCandidateLines[2] = $variantCandidateLines[2] -replace '0\.025001', '0.025002'
        [System.IO.File]::WriteAllLines($thresholdFixture.CandidatePath, $variantCandidateLines,
            [System.Text.UTF8Encoding]::new($false))
        $variantCandidateHash = (Get-FileHash -LiteralPath $thresholdFixture.CandidatePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Candidate.Sha256 = $variantCandidateHash
        & $writeThresholdManifest $variantManifest
        $variantDecisionText = $thresholdFixture.DecisionText -replace [regex]::Escape($thresholdFixture.CandidateHash), $variantCandidateHash
        [System.IO.File]::WriteAllText((Join-Path $thresholdFixture.RepoRoot 'Docs\RenderingValidation\R1Acceptance.md'),
            $variantDecisionText, [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'f6_threshold_mismatch' {
            Get-R1ThresholdPublishInputs $thresholdFixture.RepoRoot $thresholdFixture.BuildRoot `
                $thresholdFixture.CalibrationDirectory $thresholdFixture.SourceRoot `
                $thresholdFixture.BaselineManifestPath $thresholdFixture.ThresholdManifestPath `
                $thresholdFixture.CandidatePath $thresholdFixture.MeasurementPath $thresholdFixture.SourcePath `
                $thresholdFixture.CodeHead $thresholdFixture.IndoorHash $thresholdFixture.OutdoorHash `
                $variantCandidateHash $thresholdFixture.DecisionPath $thresholdFixture.DecisionSection | Out-Null
        }
        [System.IO.File]::WriteAllLines($thresholdFixture.CandidatePath, $thresholdFixture.CandidateLines,
            [System.Text.UTF8Encoding]::new($false))
        & $restoreThresholdManifest
        & $restoreThresholdDecision

        $variantMeasurementLines = @($thresholdFixture.MeasurementLines)
        $variantFields = $variantMeasurementLines[11].Split("`t")
        $variantFields[7] = '2'
        $variantMeasurementLines[11] = $variantFields -join "`t"
        [System.IO.File]::WriteAllLines($thresholdFixture.MeasurementPath, $variantMeasurementLines,
            [System.Text.UTF8Encoding]::new($false))
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Measurements.Sha256 = (Get-FileHash -LiteralPath $thresholdFixture.MeasurementPath -Algorithm SHA256).Hash.ToUpperInvariant()
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'raw_max_mismatch' $validateThresholdFixture
        & $restoreThresholdManifest

        Assert-R1SelfTestRejects 'threshold_current_head_mismatch' {
            Get-R1ThresholdPublishInputs $thresholdFixture.RepoRoot $thresholdFixture.BuildRoot `
                $thresholdFixture.CalibrationDirectory $thresholdFixture.SourceRoot `
                $thresholdFixture.BaselineManifestPath $thresholdFixture.ThresholdManifestPath `
                $thresholdFixture.CandidatePath $thresholdFixture.MeasurementPath $thresholdFixture.SourcePath `
                ('b' * 40) $thresholdFixture.IndoorHash $thresholdFixture.OutdoorHash `
                $thresholdFixture.CandidateHash $thresholdFixture.DecisionPath $thresholdFixture.DecisionSection | Out-Null
        }
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.CodeHead = 'b' * 40
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'threshold_code_head_mismatch' $validateThresholdFixture
        & $restoreThresholdManifest
        Assert-R1SelfTestRejects 'wrong_baseline_hash' {
            Get-R1ThresholdPublishInputs $thresholdFixture.RepoRoot $thresholdFixture.BuildRoot `
                $thresholdFixture.CalibrationDirectory $thresholdFixture.SourceRoot `
                $thresholdFixture.BaselineManifestPath $thresholdFixture.ThresholdManifestPath `
                $thresholdFixture.CandidatePath $thresholdFixture.MeasurementPath $thresholdFixture.SourcePath `
                $thresholdFixture.CodeHead ('0' * 64) $thresholdFixture.OutdoorHash `
                $thresholdFixture.CandidateHash $thresholdFixture.DecisionPath $thresholdFixture.DecisionSection | Out-Null
        }
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Schema = 'NorvesLib.RenderingVisualThresholdCandidate.P5.v1'
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'p5_era_manifest' $validateThresholdFixture
        & $restoreThresholdManifest
        Assert-R1SelfTestRejects 'candidate_hash_argument_mismatch' {
            Get-R1ThresholdPublishInputs $thresholdFixture.RepoRoot $thresholdFixture.BuildRoot `
                $thresholdFixture.CalibrationDirectory $thresholdFixture.SourceRoot `
                $thresholdFixture.BaselineManifestPath $thresholdFixture.ThresholdManifestPath `
                $thresholdFixture.CandidatePath $thresholdFixture.MeasurementPath $thresholdFixture.SourcePath `
                $thresholdFixture.CodeHead $thresholdFixture.IndoorHash $thresholdFixture.OutdoorHash `
                ('0' * 64) $thresholdFixture.DecisionPath $thresholdFixture.DecisionSection | Out-Null
        }
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Measurements.Sha256 = '0' * 64
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'measurement_hash_manifest_mismatch' $validateThresholdFixture
        & $restoreThresholdManifest
        $duplicateThresholdJson = '{"Schema":"a","Schema":"b"}'
        [System.IO.File]::WriteAllText($thresholdFixture.ThresholdManifestPath, $duplicateThresholdJson,
            [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'threshold_duplicate_json_key' $validateThresholdFixture
        & $restoreThresholdManifest
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        Add-Member -InputObject $variantManifest -NotePropertyName Unknown -NotePropertyValue 1
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'threshold_unknown_manifest_property' $validateThresholdFixture
        & $restoreThresholdManifest
        $wrongCaseJson = $thresholdFixture.ThresholdManifestText.Replace('"Schema"', '"schema"')
        [System.IO.File]::WriteAllText($thresholdFixture.ThresholdManifestPath, $wrongCaseJson,
            [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'threshold_wrong_property_case' $validateThresholdFixture
        & $restoreThresholdManifest
        $variantManifest = Read-R1StrictJsonFile $thresholdFixture.ThresholdManifestPath
        $variantManifest.Measurements.Rows = '60'
        & $writeThresholdManifest $variantManifest
        Assert-R1SelfTestRejects 'threshold_wrong_json_type' $validateThresholdFixture
        & $restoreThresholdManifest
        Assert-R1SelfTestRejects 'wrong_decision_section' {
            Get-R1ThresholdPublishInputs $thresholdFixture.RepoRoot $thresholdFixture.BuildRoot `
                $thresholdFixture.CalibrationDirectory $thresholdFixture.SourceRoot `
                $thresholdFixture.BaselineManifestPath $thresholdFixture.ThresholdManifestPath `
                $thresholdFixture.CandidatePath $thresholdFixture.MeasurementPath $thresholdFixture.SourcePath `
                $thresholdFixture.CodeHead $thresholdFixture.IndoorHash $thresholdFixture.OutdoorHash `
                $thresholdFixture.CandidateHash $thresholdFixture.DecisionPath 'wrong section' | Out-Null
        }
        [System.IO.File]::WriteAllText((Join-Path $thresholdFixture.RepoRoot 'Docs\RenderingValidation\R1Acceptance.md'),
            "# Synthetic R1`n`n## R1 visual approval`n", [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'threshold_approval_zero' $validateThresholdFixture
        $thresholdApprovalLine = "R1 visual threshold candidate SHA256=$($thresholdFixture.CandidateHash) CodeHead=$($thresholdFixture.CodeHead) を承認する。"
        [System.IO.File]::WriteAllText((Join-Path $thresholdFixture.RepoRoot 'Docs\RenderingValidation\R1Acceptance.md'),
            "# Synthetic R1`n`n## R1 visual approval`n$thresholdApprovalLine`n$thresholdApprovalLine`n", [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'threshold_approval_two' $validateThresholdFixture
        [System.IO.File]::WriteAllText((Join-Path $thresholdFixture.RepoRoot 'Docs\RenderingValidation\R1Acceptance.md'),
            "$thresholdApprovalLine`n`n## R1 visual approval`n", [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'threshold_approval_outside_section' $validateThresholdFixture
        & $restoreThresholdDecision
        $baselineVariant = Read-R1StrictJsonFile $thresholdFixture.BaselineManifestPath
        $baselineVariant.Candidates.Indoor.Sha256 = '0' * 64
        [System.IO.File]::WriteAllText($thresholdFixture.BaselineManifestPath,
            (($baselineVariant | ConvertTo-Json -Depth 12) + [Environment]::NewLine), [System.Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'wrong_baseline_bridge_hash' $validateThresholdFixture
        [System.IO.File]::WriteAllText($thresholdFixture.BaselineManifestPath,
            $thresholdFixture.BaselineManifestText, [System.Text.UTF8Encoding]::new($false))
        foreach ($path in $r0Paths) {
            $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
            if ($actualHash -cne $r0Hashes[$path]) {
                throw "R0 immutable document changed during threshold self-test: $path"
            }
        }

        $strictSelection = Select-R1StrictCandidate @(
            [pscustomobject]@{ PatchSize = 1; ChannelDelta = 1; MeanNanounits = [uint64]999500000 }
            [pscustomobject]@{ PatchSize = 1; ChannelDelta = 2; MeanNanounits = [uint64]1000000000 }
        ) ([uint64]999000000)
        if ($null -eq $strictSelection -or $strictSelection.Candidate.ChannelDelta -ne 1 -or
            $strictSelection.LimitMillionths -ne 999125) {
            throw 'Strict-separated first-candidate selection failed.'
        }
        $boundarySelection = Select-R1StrictCandidate @(
            [pscustomobject]@{ PatchSize = 1; ChannelDelta = 1; MeanNanounits = [uint64]1000000000 }
        ) ([uint64]999999000)
        if ($null -ne $boundarySelection) {
            throw 'Strict separation accepted an unseparated boundary candidate.'
        }

        $sourcePath = Join-Path $selfTestRoot 'source.tsv'
        $candidatePath = Join-Path $selfTestRoot 'candidate.tsv'
        $temporaryPath = Join-Path $selfTestRoot 'source.tsv.tmp'
        $backupPath = Join-Path $selfTestRoot 'source.tsv.bak'
        $rollbackDiscardPath = Join-Path $selfTestRoot 'source.tsv.rollback-discard'
        $originalBytes = [System.Text.Encoding]::UTF8.GetBytes("original`n")
        $publishedBytes = [System.Text.Encoding]::UTF8.GetBytes("published`n")
        [System.IO.File]::WriteAllBytes($sourcePath, $originalBytes)
        [System.IO.File]::WriteAllBytes($candidatePath, $publishedBytes)
        $originalHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        $beforePublishRejected = $false
        try {
            Invoke-ThresholdPublishTransaction $candidatePath $sourcePath $temporaryPath $backupPath `
                $rollbackDiscardPath $originalHash 'before-publish'
        } catch {
            $beforePublishRejected = $true
        }
        if (-not $beforePublishRejected) {
            throw 'Threshold transaction did not reject the injected before-publish failure.'
        }
        if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -cne $originalHash) {
            throw 'Threshold source changed during before-publish self-test.'
        }
        $afterPublishRejected = $false
        try {
            Invoke-ThresholdPublishTransaction $candidatePath $sourcePath $temporaryPath $backupPath `
                $rollbackDiscardPath $originalHash 'after-publish'
        } catch {
            $afterPublishRejected = $true
        }
        if (-not $afterPublishRejected) {
            throw 'Threshold transaction did not reject the injected after-publish failure.'
        }
        if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -cne $originalHash) {
            throw 'Threshold rollback did not restore the source bytes.'
        }
        foreach ($path in @($temporaryPath, $backupPath, $rollbackDiscardPath)) {
            if ([System.IO.File]::Exists($path)) {
                throw "Threshold transaction residue remains: $path"
            }
        }
        Invoke-ThresholdPublishTransaction $candidatePath $sourcePath $temporaryPath $backupPath `
            $rollbackDiscardPath $originalHash
        $publishedHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $candidateHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($publishedHash -cne $candidateHash) {
            throw 'Threshold success publish hash mismatch.'
        }
        foreach ($path in @($temporaryPath, $backupPath, $rollbackDiscardPath)) {
            if ([System.IO.File]::Exists($path)) {
                throw "Threshold successful transaction residue remains: $path"
            }
        }
        Write-Output 'measurement_parser_self_test=PASS'
        Write-Output 'approved_candidate_validation_self_test=PASS'
        Write-Output 'THRESHOLD_R1_SELF_TEST=PASS rows_59=rejected rows_60=accepted rows_61=rejected strict_separation=PASS p5_manifest=rejected rollback=restored'
        Write-Output 'THRESHOLD_R1_SELF_TEST_SUCCESS=PASS source_matches_candidate=1 residue=0'
    } finally {
        if ([System.IO.Directory]::Exists($selfTestRoot)) {
            $rootAttributes = ([System.IO.DirectoryInfo]$selfTestRoot).Attributes
            if (($rootAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Threshold self-test cleanup root became a reparse point.'
            }
            Remove-Item -LiteralPath $selfTestRoot -Recurse -Force
        }
    }
}

if ($SelfTestMeasurementParser) {
    Invoke-ParserSelfTest
    exit 0
}
if ($SelfTestApprovedCandidateValidation) {
    Invoke-ApprovedCandidateValidationSelfTest
    exit 0
}
if ($SelfTestR1Contract) {
    Invoke-R1ContractSelfTest
    exit 0
}
if ($GenerateCandidate) {
    Invoke-CandidateGeneration
    exit 0
}
if ($PublishApprovedCandidate) {
    Invoke-R1ThresholdPublish
    exit 0
}
throw 'Specify -SelfTestMeasurementParser, -SelfTestApprovedCandidateValidation, -GenerateCandidate, or -PublishApprovedCandidate.'
