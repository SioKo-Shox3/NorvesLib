[CmdletBinding(DefaultParameterSetName = 'None')]
param(
    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [switch]$GenerateCandidate,

    [Parameter(ParameterSetName = 'Generate', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9a-f]{40}$')]
    [string]$CodeHead,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [switch]$PublishApprovedCandidate,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ApprovedIndoorSha256,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ApprovedOutdoorSha256,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [string]$DecisionPath,

    [Parameter(ParameterSetName = 'Publish', Mandatory = $true)]
    [string]$DecisionSection,

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTestR1Contract,

    [Parameter(ParameterSetName = 'R2SelfTest', Mandatory = $true)]
    [switch]$SelfTestR2Contract
)

$ErrorActionPreference = 'Stop'

if ($null -eq ('NorvesLibR1StrictJson' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

public static class NorvesLibR1StrictJson
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

function Assert-NoReparseAncestors {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Assert-PathWithinRoot $Path $Root $Label
    $normalizedRoot = Get-NormalizedPath $Root
    $current = Get-NormalizedPath $Path
    while ($current.StartsWith($normalizedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        if ([IO.Directory]::Exists($current)) {
            $attributes = ([IO.DirectoryInfo]$current).Attributes
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label has a reparse-point ancestor: $current"
            }
        } elseif ([IO.File]::Exists($current)) {
            $attributes = ([IO.FileInfo]$current).Attributes
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label is a reparse point: $current"
            }
        }
        if ($current -ceq $normalizedRoot) {
            break
        }
        $parent = [IO.Directory]::GetParent($current)
        if ($null -eq $parent) {
            break
        }
        $current = $parent.FullName
    }
}

function Remove-FixedPaths {
    param([Parameter(Mandatory = $true)][string[]]$Paths)
    foreach ($path in $Paths) {
        if ([IO.File]::Exists($path)) {
            Remove-Item -LiteralPath $path -Force
        } elseif ([IO.Directory]::Exists($path)) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
}

function Write-AtomicUtf8Text {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )
    $directory = Split-Path -Parent $Path
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $temporary = "$Path.tmp"
    $backup = "$Path.bak"
    [IO.File]::WriteAllText($temporary, $Text, [Text.UTF8Encoding]::new($false))
    if ([IO.File]::Exists($Path)) {
        [IO.File]::Replace($temporary, $Path, $backup, $true)
        if ([IO.File]::Exists($backup)) {
            [IO.File]::Delete($backup)
        }
    } else {
        [IO.File]::Move($temporary, $Path)
    }
}

function Read-StrictJsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $text = [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false))
    [NorvesLibR1StrictJson]::ValidateNoDuplicateProperties($text)
    return ($text | ConvertFrom-Json)
}

function Assert-ExactPropertySet {
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

function Assert-BaselineManifest {
    param([Parameter(Mandatory = $true)]$Manifest)
    Assert-ExactPropertySet $Manifest @('Schema', 'CodeHead', 'CaptureSource', 'Candidates', 'SourceStart') 'baseline manifest'
    if ($Manifest.Schema -isnot [string] -or
        $Manifest.Schema -cne 'NorvesLib.RenderingBaselineCandidate.R1.v1' -or
        $Manifest.CodeHead -isnot [string] -or $Manifest.CodeHead -cnotmatch '\A[0-9a-f]{40}\z' -or
        $Manifest.CaptureSource -isnot [string] -or $Manifest.CaptureSource -cne 'back-buffer') {
        throw 'Baseline manifest identity is invalid.'
    }
    Assert-ExactPropertySet $Manifest.Candidates @('Indoor', 'Outdoor') 'baseline manifest Candidates'
    Assert-ExactPropertySet $Manifest.SourceStart @('Indoor', 'Outdoor') 'baseline manifest SourceStart'
    foreach ($scene in @('Indoor', 'Outdoor')) {
        $candidate = $Manifest.Candidates.$scene
        $source = $Manifest.SourceStart.$scene
        Assert-ExactPropertySet $candidate @('RelativePath', 'Sha256') "baseline manifest Candidates.$scene"
        Assert-ExactPropertySet $source @('RelativePath', 'Sha256') "baseline manifest SourceStart.$scene"
        $candidatePath = "build/RenderingValidation/R1/BaselineCandidate/$scene.png"
        $sourcePath = "Test/Core/Rendering/Baselines/RenderingValidation/$scene.png"
        if ($candidate.RelativePath -isnot [string] -or $candidate.RelativePath -cne $candidatePath -or
            $source.RelativePath -isnot [string] -or $source.RelativePath -cne $sourcePath -or
            $candidate.Sha256 -isnot [string] -or $candidate.Sha256 -cnotmatch '\A[0-9A-F]{64}\z' -or
            $source.Sha256 -isnot [string] -or $source.Sha256 -cnotmatch '\A[0-9A-F]{64}\z') {
            throw "Baseline manifest path or hash is invalid for $scene."
        }
    }
}

function Assert-R1CurrentHead {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ExpectedHead
    )
    if ($ExpectedHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'Expected current HEAD must be 40 lowercase hexadecimal characters.'
    }
    $currentHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $currentHead -cne $ExpectedHead) {
        throw "Current HEAD does not match expected value: expected=$ExpectedHead actual=$currentHead."
    }
    return $currentHead
}

function Assert-R1FileHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedHash,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if (-not [IO.File]::Exists($Path)) {
        throw "$Label is missing: $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualHash -cne $ExpectedHash.ToUpperInvariant()) {
        throw "$Label hash mismatch: expected=$($ExpectedHash.ToUpperInvariant()) actual=$actualHash"
    }
    return $actualHash
}

function Get-R1BaselinePublishInputs {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$CandidateRoot,
        [Parameter(Mandatory = $true)][string]$StagingRoot,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$ExpectedCurrentHead,
        [Parameter(Mandatory = $true)][string]$ApprovedIndoorHash,
        [Parameter(Mandatory = $true)][string]$ApprovedOutdoorHash,
        [Parameter(Mandatory = $true)][string]$DecisionPath,
        [Parameter(Mandatory = $true)][string]$DecisionSection
    )
    if ($ExpectedCurrentHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'Expected current HEAD must be 40 lowercase hexadecimal characters.'
    }
    Assert-PathWithinRoot $BuildRoot $RepoRoot 'Build root'
    Assert-PathWithinRoot $CandidateRoot $BuildRoot 'Candidate root'
    Assert-PathWithinRoot $StagingRoot $BuildRoot 'Staging root'
    Assert-PathWithinRoot $SourceRoot $RepoRoot 'Source root'
    Assert-PathWithinRoot $ManifestPath $CandidateRoot 'Baseline manifest'
    Assert-NoReparseAncestors $BuildRoot $RepoRoot 'Build root'
    Assert-NoReparseAncestors $CandidateRoot $BuildRoot 'Candidate root'
    Assert-NoReparseAncestors $StagingRoot $BuildRoot 'Staging root'
    Assert-NoReparseAncestors $SourceRoot $RepoRoot 'Source root'
    Assert-NoReparseAncestors $ManifestPath $CandidateRoot 'Baseline manifest'
    if (-not [IO.File]::Exists($ManifestPath)) {
        throw 'A valid baseline Manifest.json is required before publish.'
    }
    $manifest = Read-StrictJsonFile $ManifestPath
    Assert-BaselineManifest $manifest
    if ($manifest.CodeHead -cne $ExpectedCurrentHead) {
        throw 'Baseline manifest CodeHead does not match the expected current HEAD.'
    }
    Assert-R1BaselineApproval $RepoRoot $DecisionPath $DecisionSection `
        $ApprovedIndoorHash.ToUpperInvariant() $ApprovedOutdoorHash.ToUpperInvariant() $ExpectedCurrentHead
    $entries = Get-BaselineEntries $RepoRoot $BuildRoot $CandidateRoot $StagingRoot
    $sourceHashes = @{}
    foreach ($entry in $entries) {
        Assert-PathWithinRoot $entry.Source $SourceRoot "$($entry.Scene) source"
        Assert-NoReparseAncestors $entry.Source $SourceRoot "$($entry.Scene) source"
        Assert-PathWithinRoot $entry.Staging $StagingRoot "$($entry.Scene) staging"
        Assert-NoReparseAncestors $entry.Staging $StagingRoot "$($entry.Scene) staging"
        Assert-PathWithinRoot $entry.Candidate $CandidateRoot "$($entry.Scene) candidate"
        Assert-NoReparseAncestors $entry.Candidate $CandidateRoot "$($entry.Scene) candidate"
        if (-not [IO.File]::Exists($entry.Source) -or -not [IO.File]::Exists($entry.Candidate)) {
            throw "Baseline source or candidate is missing: $($entry.Scene)"
        }
        $sourceHashes[$entry.Scene] = Assert-R1FileHash $entry.Source `
            $manifest.SourceStart.($entry.Scene).Sha256 "$($entry.Scene) source-start"
        $candidateHash = Assert-R1FileHash $entry.Candidate `
            $manifest.Candidates.($entry.Scene).Sha256 "$($entry.Scene) candidate"
        $approvedHash = if ($entry.Scene -ceq 'Indoor') { $ApprovedIndoorHash } else { $ApprovedOutdoorHash }
        if ($candidateHash -cne $approvedHash.ToUpperInvariant()) {
            throw "Candidate hash validation failed: $($entry.Scene)"
        }
    }
    return [pscustomobject]@{
        Manifest = $manifest
        Entries = $entries
        SourceHashes = $sourceHashes
    }
}

function Get-BaselineEntries {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$CandidateRoot,
        [Parameter(Mandatory = $true)][string]$StagingRoot
    )
    return @(
        [pscustomobject]@{
            Scene = 'Indoor'
            Source = Get-NormalizedPath (Join-Path $RepoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\Indoor.png')
            Staging = Get-NormalizedPath (Join-Path $StagingRoot 'Indoor.png.tmp')
            Candidate = Get-NormalizedPath (Join-Path $CandidateRoot 'Indoor.png')
            CandidateTemporary = Get-NormalizedPath (Join-Path $CandidateRoot 'Indoor.png.tmp')
            SourceTemporary = Get-NormalizedPath (Join-Path $CandidateRoot 'Indoor.png.source.tmp')
            Backup = Get-NormalizedPath (Join-Path $CandidateRoot 'Indoor.png.source.bak')
            RollbackDiscard = Get-NormalizedPath (Join-Path $CandidateRoot 'Indoor.png.rollback-discard')
        },
        [pscustomobject]@{
            Scene = 'Outdoor'
            Source = Get-NormalizedPath (Join-Path $RepoRoot 'Test\Core\Rendering\Baselines\RenderingValidation\Outdoor.png')
            Staging = Get-NormalizedPath (Join-Path $StagingRoot 'Outdoor.png.tmp')
            Candidate = Get-NormalizedPath (Join-Path $CandidateRoot 'Outdoor.png')
            CandidateTemporary = Get-NormalizedPath (Join-Path $CandidateRoot 'Outdoor.png.tmp')
            SourceTemporary = Get-NormalizedPath (Join-Path $CandidateRoot 'Outdoor.png.source.tmp')
            Backup = Get-NormalizedPath (Join-Path $CandidateRoot 'Outdoor.png.source.bak')
            RollbackDiscard = Get-NormalizedPath (Join-Path $CandidateRoot 'Outdoor.png.rollback-discard')
        }
    )
}

function Assert-R1ProductionArguments {
    param([Parameter(Mandatory = $true)][string]$ExpectedCodeHead)
    if ($ExpectedCodeHead -notmatch '\A[0-9a-f]{40}\z') {
        throw 'R1 CodeHead must be 40 lowercase hexadecimal characters.'
    }
}

function Invoke-GoldenCapture {
    param(
        [Parameter(Mandatory = $true)]$Entry,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$RunsRoot
    )
    Push-Location -LiteralPath $RunsRoot
    try {
        & $Executable "--scene=$($Entry.Scene.ToLowerInvariant())" '--capture-source=back-buffer' '--write-baseline-staging' | Out-Host
        return $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

function Assert-R1CaptureExitCode {
    param(
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][string]$Scene
    )
    if ($ExitCode -eq 125 -or $ExitCode -ne 0) {
        throw "Baseline candidate capture failed: scene=$Scene exit=$ExitCode"
    }
}

function Invoke-StagingValidation {
    param(
        [Parameter(Mandatory = $true)][string]$Validator,
        [Parameter(Mandatory = $true)][string]$RunsRoot
    )
    Push-Location -LiteralPath $RunsRoot
    try {
        & $Validator '--validate-fixed-staging' | Out-Host
        return $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

function Invoke-R1GenerateCandidate {
    Assert-R1ProductionArguments $CodeHead
    $repoRoot = Get-NormalizedPath (Join-Path $PSScriptRoot '..')
    $buildRoot = Get-NormalizedPath (Join-Path $repoRoot 'build')
    $candidateRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\R1\BaselineCandidate')
    $stagingRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\BaselineStaging')
    $runsRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\Runs')
    $executable = Get-NormalizedPath (Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe')
    $validator = Get-NormalizedPath (Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe')
    Assert-PathWithinRoot $buildRoot $repoRoot 'Build root'
    Assert-PathWithinRoot $candidateRoot $buildRoot 'Candidate root'
    Assert-PathWithinRoot $stagingRoot $buildRoot 'Staging root'
    Assert-PathWithinRoot $runsRoot $buildRoot 'Runs root'
    Assert-PathWithinRoot $executable $buildRoot 'Golden executable'
    Assert-PathWithinRoot $validator $buildRoot 'Staging validator'
    foreach ($path in @($candidateRoot, $stagingRoot, $runsRoot, $executable, $validator)) {
        Assert-NoReparseAncestors $path $buildRoot 'R1 baseline path'
    }
    if (-not [IO.File]::Exists($executable) -or -not [IO.File]::Exists($validator)) {
        throw 'Golden executables are missing.'
    }
    $currentHead = Assert-R1CurrentHead $repoRoot $CodeHead
    $entries = Get-BaselineEntries $repoRoot $buildRoot $candidateRoot $stagingRoot
    foreach ($entry in $entries) {
        Assert-PathWithinRoot $entry.Source (Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation') "$($entry.Scene) source"
        Assert-PathWithinRoot $entry.Staging $stagingRoot "$($entry.Scene) staging"
        Assert-NoReparseAncestors $entry.Staging $stagingRoot "$($entry.Scene) staging"
        foreach ($path in @($entry.Candidate, $entry.CandidateTemporary, $entry.SourceTemporary, $entry.Backup, $entry.RollbackDiscard)) {
            Assert-PathWithinRoot $path $candidateRoot "$($entry.Scene) transaction path"
            Assert-NoReparseAncestors $path $candidateRoot "$($entry.Scene) transaction path"
        }
        Assert-NoReparseAncestors $entry.Source $repoRoot "$($entry.Scene) source"
    }
    $manifestPath = Get-NormalizedPath (Join-Path $candidateRoot 'Manifest.json')
    $manifestTemporary = Get-NormalizedPath (Join-Path $candidateRoot 'Manifest.json.tmp')
    $stalePaths = @($manifestPath, $manifestTemporary)
    foreach ($entry in $entries) {
        $stalePaths += @($entry.Staging, $entry.Candidate, $entry.CandidateTemporary, $entry.SourceTemporary, $entry.Backup, $entry.RollbackDiscard)
    }
    Remove-FixedPaths $stalePaths
    [IO.Directory]::CreateDirectory($candidateRoot) | Out-Null
    [IO.Directory]::CreateDirectory($stagingRoot) | Out-Null
    [IO.Directory]::CreateDirectory($runsRoot) | Out-Null
    $sourceHashes = @{}
    foreach ($entry in $entries) {
        if (-not [IO.File]::Exists($entry.Source)) {
            throw "Baseline source is missing: $($entry.Source)"
        }
        $sourceHashes[$entry.Scene] = (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    foreach ($entry in $entries) {
        $exitCode = Invoke-GoldenCapture $entry $executable $runsRoot
        Assert-R1CaptureExitCode $exitCode $entry.Scene
        if (-not [IO.File]::Exists($entry.Staging)) {
            throw "Baseline staging is missing: $($entry.Scene)"
        }
    }
    $validatorExitCode = Invoke-StagingValidation $validator $runsRoot
    if ($validatorExitCode -ne 0) {
        throw "Baseline staging validation failed: exit=$validatorExitCode"
    }
    foreach ($entry in $entries) {
        [IO.File]::Copy($entry.Staging, $entry.CandidateTemporary, $true)
        $stagingHash = (Get-FileHash -LiteralPath $entry.Staging -Algorithm SHA256).Hash
        $candidateTemporaryHash = (Get-FileHash -LiteralPath $entry.CandidateTemporary -Algorithm SHA256).Hash
        if ($stagingHash -cne $candidateTemporaryHash) {
            throw "Candidate bridge hash mismatch: $($entry.Scene)"
        }
        [IO.File]::Move($entry.CandidateTemporary, $entry.Candidate)
        if ((Get-FileHash -LiteralPath $entry.Candidate -Algorithm SHA256).Hash -cne $stagingHash) {
            throw "Candidate hash changed after atomic move: $($entry.Scene)"
        }
    }
    foreach ($entry in $entries) {
        if ((Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant() -cne $sourceHashes[$entry.Scene]) {
            throw "Baseline source changed during candidate generation: $($entry.Scene)"
        }
    }
    $manifest = [ordered]@{
        Schema = 'NorvesLib.RenderingBaselineCandidate.R1.v1'
        CodeHead = $CodeHead
        CaptureSource = 'back-buffer'
        Candidates = [ordered]@{
            Indoor = [ordered]@{
                RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Indoor.png'
                Sha256 = (Get-FileHash -LiteralPath $entries[0].Candidate -Algorithm SHA256).Hash.ToUpperInvariant()
            }
            Outdoor = [ordered]@{
                RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Outdoor.png'
                Sha256 = (Get-FileHash -LiteralPath $entries[1].Candidate -Algorithm SHA256).Hash.ToUpperInvariant()
            }
        }
        SourceStart = [ordered]@{
            Indoor = [ordered]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'
                Sha256 = $sourceHashes.Indoor
            }
            Outdoor = [ordered]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'
                Sha256 = $sourceHashes.Outdoor
            }
        }
    }
    Write-AtomicUtf8Text $manifestPath (($manifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
    Assert-BaselineManifest (Read-StrictJsonFile $manifestPath)
    Write-Output "baseline_candidate_manifest=$manifestPath"
    Write-Output "baseline_candidate_code_head=$CodeHead"
}

function Get-SectionText {
    param(
        [Parameter(Mandatory = $true)][string]$Document,
        [Parameter(Mandatory = $true)][string]$Heading
    )
    $headingPattern = '(?m)^' + [regex]::Escape($Heading) + '\s*$'
    $headingMatches = @([regex]::Matches($Document, $headingPattern))
    if ($headingMatches.Count -ne 1) {
        throw "Decision heading cardinality is invalid: $Heading"
    }
    $start = $headingMatches[0].Index + $headingMatches[0].Length
    $nextHeading = [regex]::Match($Document.Substring($start), '(?m)^#{1,2}\s+.*$')
    if ($nextHeading.Success) {
        return $Document.Substring($start, $nextHeading.Index)
    }
    return $Document.Substring($start)
}

function Invoke-BaselinePublishTransaction {
    param(
        [Parameter(Mandatory = $true)][object[]]$Entries,
        [Parameter(Mandatory = $true)][hashtable]$SourceHashes,
        [string]$FailureMode
    )
    if ($FailureMode -and $FailureMode -cnotin @('before-publish', 'after-first-publish')) {
        throw "Unsupported baseline transaction failure mode: $FailureMode"
    }
    $published = [System.Collections.Generic.List[object]]::new()
    $rollbackFailed = $false
    try {
        if ($FailureMode -ceq 'before-publish') {
            throw 'Injected baseline transaction failure before publish.'
        }
        foreach ($entry in $Entries) {
            [IO.File]::Copy($entry.Candidate, $entry.SourceTemporary, $true)
            if ((Get-FileHash -LiteralPath $entry.SourceTemporary -Algorithm SHA256).Hash.ToUpperInvariant() -cne
                (Get-FileHash -LiteralPath $entry.Candidate -Algorithm SHA256).Hash.ToUpperInvariant()) {
                throw "Source transaction temporary hash mismatch: $($entry.Scene)"
            }
            [IO.File]::Replace($entry.SourceTemporary, $entry.Source, $entry.Backup, $true)
            $published.Add($entry)
            if ($published.Count -eq 1 -and $FailureMode -ceq 'after-first-publish') {
                throw 'Injected baseline transaction failure after first publish.'
            }
        }
    } catch {
        $publishError = $_
        $rollbackError = $null
        for ($index = $published.Count - 1; $index -ge 0; --$index) {
            $entry = $published[$index]
            try {
                if (-not [IO.File]::Exists($entry.Backup)) {
                    throw "Rollback backup is missing: $($entry.Scene)"
                }
                [IO.File]::Copy($entry.Backup, $entry.SourceTemporary, $true)
                [IO.File]::Replace($entry.SourceTemporary, $entry.Source, $entry.RollbackDiscard, $true)
            } catch {
                $rollbackError = $_
                $rollbackFailed = $true
            }
        }
        if ($null -ne $rollbackError) {
            throw "Baseline publish failed and rollback also failed: publish=$publishError rollback=$rollbackError"
        }
        foreach ($entry in $Entries) {
            if (-not [IO.File]::Exists($entry.Source) -or
                (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant() -cne $SourceHashes[$entry.Scene]) {
                throw "Baseline rollback hash mismatch: $($entry.Scene)"
            }
        }
        throw $publishError
    } finally {
        foreach ($entry in $Entries) {
            foreach ($path in @($entry.SourceTemporary, $entry.RollbackDiscard)) {
                if ([IO.File]::Exists($path)) {
                    Remove-Item -LiteralPath $path -Force
                }
            }
            if (-not $rollbackFailed -and [IO.File]::Exists($entry.Backup)) {
                Remove-Item -LiteralPath $entry.Backup -Force
            }
        }
    }
}

function Assert-R1BaselineApproval {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$DecisionPath,
        [Parameter(Mandatory = $true)][string]$DecisionSection,
        [Parameter(Mandatory = $true)][string]$IndoorHash,
        [Parameter(Mandatory = $true)][string]$OutdoorHash,
        [Parameter(Mandatory = $true)][string]$ExpectedCodeHead
    )
    if ($DecisionPath -cne 'Docs/RenderingValidation/R1Acceptance.md' -or
        $DecisionSection -cne 'R1 visual approval') {
        throw 'R1 baseline decision path or section is not canonical.'
    }
    $absoluteDecisionPath = Get-NormalizedPath (Join-Path $RepoRoot ($DecisionPath -replace '/', '\'))
    Assert-PathWithinRoot $absoluteDecisionPath $RepoRoot 'Decision path'
    Assert-NoReparseAncestors $absoluteDecisionPath $RepoRoot 'Decision path'
    if (-not [IO.File]::Exists($absoluteDecisionPath)) {
        throw "Decision document is missing: $absoluteDecisionPath"
    }
    $document = [IO.File]::ReadAllText($absoluteDecisionPath, [Text.UTF8Encoding]::new($false))
    $section = Get-SectionText $document "## $DecisionSection"
    $approvalLine = "R1 baseline candidate Indoor=$IndoorHash Outdoor=$OutdoorHash CodeHead=$ExpectedCodeHead を承認する。"
    $allApprovalLines = @($document -split "`r?`n" | Where-Object { $_ -ceq $approvalLine })
    $sectionApprovalLines = @($section -split "`r?`n" | Where-Object { $_ -ceq $approvalLine })
    if ($allApprovalLines.Count -ne 1 -or $sectionApprovalLines.Count -ne 1) {
        throw 'R1 baseline approval line must occur exactly once inside its section.'
    }
}

function Invoke-R1PublishCandidate {
    Assert-R1ProductionArguments $CodeHead
    if ($DecisionPath -cne 'Docs/RenderingValidation/R1Acceptance.md' -or
        $DecisionSection -cne 'R1 visual approval') {
        throw 'R1 baseline publish decision arguments are not canonical.'
    }
    $repoRoot = Get-NormalizedPath (Join-Path $PSScriptRoot '..')
    $buildRoot = Get-NormalizedPath (Join-Path $repoRoot 'build')
    $candidateRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\R1\BaselineCandidate')
    $stagingRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\BaselineStaging')
    $sourceRoot = Get-NormalizedPath (Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation')
    $manifestPath = Join-Path $candidateRoot 'Manifest.json'
    $currentHead = Assert-R1CurrentHead $repoRoot $CodeHead
    $indoorApproved = $ApprovedIndoorSha256.ToUpperInvariant()
    $outdoorApproved = $ApprovedOutdoorSha256.ToUpperInvariant()
    $validated = Get-R1BaselinePublishInputs $repoRoot $buildRoot $candidateRoot $stagingRoot $sourceRoot `
        $manifestPath $currentHead $indoorApproved $outdoorApproved $DecisionPath $DecisionSection
    $entries = $validated.Entries
    $sourceHashes = $validated.SourceHashes
    $failureMode = [Environment]::GetEnvironmentVariable('NORVESLIB_BASELINE_TRANSACTION_TEST_FAILURE')
    Invoke-BaselinePublishTransaction $entries $sourceHashes $failureMode
    Write-Output "baseline_publish_code_head=$CodeHead"
    Write-Output 'baseline_publish=PASS'
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
        throw "Baseline R1 self-test negative '$Name' was accepted."
    }
}

function New-SyntheticBaselineManifest {
    return [pscustomobject]@{
        Schema = 'NorvesLib.RenderingBaselineCandidate.R1.v1'
        CodeHead = ('a' * 40)
        CaptureSource = 'back-buffer'
        Candidates = [pscustomobject]@{
            Indoor = [pscustomobject]@{
                RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Indoor.png'
                Sha256 = ('A' * 64)
            }
            Outdoor = [pscustomobject]@{
                RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Outdoor.png'
                Sha256 = ('B' * 64)
            }
        }
        SourceStart = [pscustomobject]@{
            Indoor = [pscustomobject]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'
                Sha256 = ('C' * 64)
            }
            Outdoor = [pscustomobject]@{
                RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'
                Sha256 = ('D' * 64)
            }
        }
    }
}

function Invoke-R1SelfTest {
    $repoRoot = Get-NormalizedPath (Join-Path $PSScriptRoot '..')
    $buildRoot = Get-NormalizedPath (Join-Path $repoRoot 'build')
    $selfTestRoot = Get-NormalizedPath (Join-Path $repoRoot 'build\RenderingValidation\R1\SelfTest\Baseline')
    Assert-PathWithinRoot $selfTestRoot $buildRoot 'Baseline self-test root'
    Assert-NoReparseAncestors $selfTestRoot $buildRoot 'Baseline self-test root'
    if ([IO.Directory]::Exists($selfTestRoot)) {
        Remove-Item -LiteralPath $selfTestRoot -Recurse -Force
    }
    [IO.Directory]::CreateDirectory($selfTestRoot) | Out-Null
    try {
        $manifest = New-SyntheticBaselineManifest
        Assert-BaselineManifest $manifest
        $manifestPath = Join-Path $selfTestRoot 'Manifest.json'
        Write-AtomicUtf8Text $manifestPath (($manifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
        Assert-BaselineManifest (Read-StrictJsonFile $manifestPath)
        $stagingPath = Join-Path $selfTestRoot 'Indoor.png.tmp'
        $candidatePath = Join-Path $selfTestRoot 'Indoor.png'
        $candidateTemporary = Join-Path $selfTestRoot 'Indoor.png.candidate.tmp'
        $bytes = [Text.Encoding]::UTF8.GetBytes("synthetic-png`n")
        [IO.File]::WriteAllBytes($stagingPath, $bytes)
        [IO.File]::Copy($stagingPath, $candidateTemporary, $true)
        if ((Get-FileHash -LiteralPath $stagingPath -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $candidateTemporary -Algorithm SHA256).Hash) {
            throw 'Synthetic staging-to-candidate bridge changed bytes.'
        }
        [IO.File]::Move($candidateTemporary, $candidatePath)
        $stalePath = Join-Path $selfTestRoot 'stale.tmp'
        [IO.File]::WriteAllText($stalePath, 'stale', [Text.UTF8Encoding]::new($false))
        Remove-FixedPaths @($stalePath)
        if ([IO.File]::Exists($stalePath)) {
            throw 'Synthetic stale cleanup left a residue.'
        }
        $duplicateJsonPath = Join-Path $selfTestRoot 'duplicate.json'
        [IO.File]::WriteAllText($duplicateJsonPath, '{"Schema":"a","Schema":"b"}', [Text.UTF8Encoding]::new($false))
        Assert-R1SelfTestRejects 'duplicate_json_key' { Read-StrictJsonFile $duplicateJsonPath }
        $unknownManifest = $manifest.psobject.Copy()
        Add-Member -InputObject $unknownManifest -NotePropertyName Unknown -NotePropertyValue 1
        Assert-R1SelfTestRejects 'unknown_manifest_property' { Assert-BaselineManifest $unknownManifest }
        $wrongCaseManifest = $manifest.psobject.Copy()
        $wrongCaseManifest.Schema = 'NorvesLib.RenderingBaselineCandidate.R1.V1'
        Assert-R1SelfTestRejects 'wrong_property_case' { Assert-BaselineManifest $wrongCaseManifest }
        $wrongTypeManifest = $manifest.psobject.Copy()
        $wrongTypeManifest.CaptureSource = 1
        Assert-R1SelfTestRejects 'wrong_json_type' { Assert-BaselineManifest $wrongTypeManifest }
        Assert-R1SelfTestRejects 'path_traversal' {
            Assert-PathWithinRoot (Join-Path $selfTestRoot '..\outside.png') $selfTestRoot 'synthetic traversal'
        }
        Assert-R1SelfTestRejects 'missing_manifest' { Read-StrictJsonFile (Join-Path $selfTestRoot 'missing.json') }

        $syntheticRepoRoot = Join-Path $selfTestRoot 'synthetic-repo'
        $syntheticBuildRoot = Join-Path $syntheticRepoRoot 'build'
        $syntheticCandidateRoot = Join-Path $syntheticBuildRoot 'RenderingValidation\R1\BaselineCandidate'
        $syntheticStagingRoot = Join-Path $syntheticBuildRoot 'RenderingValidation\BaselineStaging'
        $syntheticSourceRoot = Join-Path $syntheticRepoRoot 'Test\Core\Rendering\Baselines\RenderingValidation'
        $syntheticManifestPath = Join-Path $syntheticCandidateRoot 'Manifest.json'
        [IO.Directory]::CreateDirectory($syntheticCandidateRoot) | Out-Null
        [IO.Directory]::CreateDirectory($syntheticStagingRoot) | Out-Null
        [IO.Directory]::CreateDirectory($syntheticSourceRoot) | Out-Null
        $syntheticHead = 'a' * 40
        $syntheticIndoorSourcePath = Join-Path $syntheticSourceRoot 'Indoor.png'
        $syntheticOutdoorSourcePath = Join-Path $syntheticSourceRoot 'Outdoor.png'
        $syntheticIndoorCandidatePath = Join-Path $syntheticCandidateRoot 'Indoor.png'
        $syntheticOutdoorCandidatePath = Join-Path $syntheticCandidateRoot 'Outdoor.png'
        $syntheticIndoorStagingPath = Join-Path $syntheticStagingRoot 'Indoor.png.tmp'
        $syntheticOutdoorStagingPath = Join-Path $syntheticStagingRoot 'Outdoor.png.tmp'
        [IO.File]::WriteAllBytes($syntheticIndoorSourcePath, [Text.Encoding]::UTF8.GetBytes('source-indoor'))
        [IO.File]::WriteAllBytes($syntheticOutdoorSourcePath, [Text.Encoding]::UTF8.GetBytes('source-outdoor'))
        [IO.File]::WriteAllBytes($syntheticIndoorCandidatePath, [Text.Encoding]::UTF8.GetBytes('candidate-indoor'))
        [IO.File]::WriteAllBytes($syntheticOutdoorCandidatePath, [Text.Encoding]::UTF8.GetBytes('candidate-outdoor'))
        [IO.File]::Copy($syntheticIndoorCandidatePath, $syntheticIndoorStagingPath, $true)
        [IO.File]::Copy($syntheticOutdoorCandidatePath, $syntheticOutdoorStagingPath, $true)
        $syntheticIndoorCandidateHash = (Get-FileHash -LiteralPath $syntheticIndoorCandidatePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $syntheticOutdoorCandidateHash = (Get-FileHash -LiteralPath $syntheticOutdoorCandidatePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $syntheticIndoorSourceHash = (Get-FileHash -LiteralPath $syntheticIndoorSourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $syntheticOutdoorSourceHash = (Get-FileHash -LiteralPath $syntheticOutdoorSourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $syntheticBaselineManifest = [ordered]@{
            Schema = 'NorvesLib.RenderingBaselineCandidate.R1.v1'
            CodeHead = $syntheticHead
            CaptureSource = 'back-buffer'
            Candidates = [ordered]@{
                Indoor = [ordered]@{ RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Indoor.png'; Sha256 = $syntheticIndoorCandidateHash }
                Outdoor = [ordered]@{ RelativePath = 'build/RenderingValidation/R1/BaselineCandidate/Outdoor.png'; Sha256 = $syntheticOutdoorCandidateHash }
            }
            SourceStart = [ordered]@{
                Indoor = [ordered]@{ RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png'; Sha256 = $syntheticIndoorSourceHash }
                Outdoor = [ordered]@{ RelativePath = 'Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png'; Sha256 = $syntheticOutdoorSourceHash }
            }
        }
        $syntheticBaselineManifestText = (($syntheticBaselineManifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
        Write-AtomicUtf8Text $syntheticManifestPath $syntheticBaselineManifestText
        $syntheticDecisionDirectory = Join-Path $syntheticRepoRoot 'Docs\RenderingValidation'
        $syntheticDecisionPath = Join-Path $syntheticDecisionDirectory 'R1Acceptance.md'
        $syntheticBaselineApprovalLine = "R1 baseline candidate Indoor=$syntheticIndoorCandidateHash Outdoor=$syntheticOutdoorCandidateHash CodeHead=$syntheticHead を承認する。"
        $syntheticValidDecisionText = "# Synthetic R1`n`n## R1 visual approval`n$syntheticBaselineApprovalLine`n`n## Next`n"
        Write-AtomicUtf8Text $syntheticDecisionPath $syntheticValidDecisionText
        $syntheticBaselineEntries = Get-BaselineEntries $syntheticRepoRoot $syntheticBuildRoot $syntheticCandidateRoot $syntheticStagingRoot
        $syntheticBaselineValidate = {
            Get-R1BaselinePublishInputs $syntheticRepoRoot $syntheticBuildRoot $syntheticCandidateRoot $syntheticStagingRoot `
                $syntheticSourceRoot $syntheticManifestPath $syntheticHead $syntheticIndoorCandidateHash $syntheticOutdoorCandidateHash `
                'Docs/RenderingValidation/R1Acceptance.md' 'R1 visual approval' | Out-Null
        }
        $syntheticBaselineBefore = @(
            (Get-FileHash -LiteralPath $syntheticIndoorSourcePath -Algorithm SHA256).Hash,
            (Get-FileHash -LiteralPath $syntheticOutdoorSourcePath -Algorithm SHA256).Hash)
        & $syntheticBaselineValidate
        $syntheticBaselineAfter = @(
            (Get-FileHash -LiteralPath $syntheticIndoorSourcePath -Algorithm SHA256).Hash,
            (Get-FileHash -LiteralPath $syntheticOutdoorSourcePath -Algorithm SHA256).Hash)
        if (($syntheticBaselineBefore -join ',') -cne ($syntheticBaselineAfter -join ',')) {
            throw 'Baseline pure validation changed a source file.'
        }
        $partialManifest = Read-StrictJsonFile $syntheticManifestPath
        $partialManifest.Candidates.psobject.Properties.Remove('Outdoor')
        Write-AtomicUtf8Text $syntheticManifestPath (($partialManifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
        Assert-R1SelfTestRejects 'partial_candidate' $syntheticBaselineValidate
        Write-AtomicUtf8Text $syntheticManifestPath $syntheticBaselineManifestText
        Remove-Item -LiteralPath $syntheticOutdoorCandidatePath -Force
        Assert-R1SelfTestRejects 'missing_candidate' $syntheticBaselineValidate
        [IO.File]::WriteAllBytes($syntheticOutdoorCandidatePath, [Text.Encoding]::UTF8.GetBytes('candidate-outdoor'))
        [IO.File]::WriteAllBytes($syntheticOutdoorCandidatePath, [Text.Encoding]::UTF8.GetBytes('corrupt-candidate'))
        Assert-R1SelfTestRejects 'corrupt_candidate' $syntheticBaselineValidate
        [IO.File]::WriteAllBytes($syntheticOutdoorCandidatePath, [Text.Encoding]::UTF8.GetBytes('candidate-outdoor'))
        $candidateHashManifest = Read-StrictJsonFile $syntheticManifestPath
        $candidateHashManifest.Candidates.Indoor.Sha256 = 'E' * 64
        Write-AtomicUtf8Text $syntheticManifestPath (($candidateHashManifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
        Assert-R1SelfTestRejects 'candidate_hash_mismatch' $syntheticBaselineValidate
        Write-AtomicUtf8Text $syntheticManifestPath $syntheticBaselineManifestText
        $sourceStartManifest = Read-StrictJsonFile $syntheticManifestPath
        $sourceStartManifest.SourceStart.Indoor.Sha256 = 'E' * 64
        Write-AtomicUtf8Text $syntheticManifestPath (($sourceStartManifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
        Assert-R1SelfTestRejects 'source_start_hash_mismatch' $syntheticBaselineValidate
        Write-AtomicUtf8Text $syntheticManifestPath $syntheticBaselineManifestText
        $outsideSourceRoot = Join-Path $selfTestRoot 'outside-source'
        Assert-R1SelfTestRejects 'repo_outside' {
            Get-R1BaselinePublishInputs $syntheticRepoRoot $syntheticBuildRoot $syntheticCandidateRoot $syntheticStagingRoot `
                $outsideSourceRoot $syntheticManifestPath $syntheticHead $syntheticIndoorCandidateHash $syntheticOutdoorCandidateHash `
                'Docs/RenderingValidation/R1Acceptance.md' 'R1 visual approval' | Out-Null
        }
        Assert-R1SelfTestRejects 'staging_candidate_root_mismatch' {
            Assert-PathWithinRoot $syntheticBaselineEntries[0].Staging $syntheticCandidateRoot 'staging candidate root'
        }
        Assert-R1SelfTestRejects 'expected_current_head_mismatch' {
            Get-R1BaselinePublishInputs $syntheticRepoRoot $syntheticBuildRoot $syntheticCandidateRoot $syntheticStagingRoot `
                $syntheticSourceRoot $syntheticManifestPath ('b' * 40) $syntheticIndoorCandidateHash $syntheticOutdoorCandidateHash `
                'Docs/RenderingValidation/R1Acceptance.md' 'R1 visual approval' | Out-Null
        }
        $reparsePath = Join-Path $syntheticBuildRoot 'reparse-candidate'
        New-Item -ItemType Junction -Path $reparsePath -Target $syntheticBuildRoot | Out-Null
        try {
            Assert-R1SelfTestRejects 'synthetic_reparse_ancestor' {
                Get-R1BaselinePublishInputs $syntheticRepoRoot $syntheticBuildRoot $reparsePath $syntheticStagingRoot `
                    $syntheticSourceRoot (Join-Path $reparsePath 'Manifest.json') $syntheticHead $syntheticIndoorCandidateHash $syntheticOutdoorCandidateHash `
                    'Docs/RenderingValidation/R1Acceptance.md' 'R1 visual approval' | Out-Null
            }
        } finally {
            if ([IO.Directory]::Exists($reparsePath)) {
                Remove-Item -LiteralPath $reparsePath -Force
            }
        }
        $wrongHeadingText = "# Synthetic R1`n`n## Wrong heading`n$syntheticBaselineApprovalLine`n"
        Write-AtomicUtf8Text $syntheticDecisionPath $wrongHeadingText
        Assert-R1SelfTestRejects 'wrong_heading' $syntheticBaselineValidate
        $duplicateHeadingText = "# Synthetic R1`n`n## R1 visual approval`n$syntheticBaselineApprovalLine`n`n## R1 visual approval`n"
        Write-AtomicUtf8Text $syntheticDecisionPath $duplicateHeadingText
        Assert-R1SelfTestRejects 'duplicate_heading' $syntheticBaselineValidate
        $approvalZeroText = "# Synthetic R1`n`n## R1 visual approval`n"
        Write-AtomicUtf8Text $syntheticDecisionPath $approvalZeroText
        Assert-R1SelfTestRejects 'approval_zero' $syntheticBaselineValidate
        $approvalTwoText = "# Synthetic R1`n`n## R1 visual approval`n$syntheticBaselineApprovalLine`n$syntheticBaselineApprovalLine`n"
        Write-AtomicUtf8Text $syntheticDecisionPath $approvalTwoText
        Assert-R1SelfTestRejects 'approval_two' $syntheticBaselineValidate
        $approvalOutsideText = "$syntheticBaselineApprovalLine`n`n## R1 visual approval`n"
        Write-AtomicUtf8Text $syntheticDecisionPath $approvalOutsideText
        Assert-R1SelfTestRejects 'approval_outside_section' $syntheticBaselineValidate
        Write-AtomicUtf8Text $syntheticDecisionPath $syntheticValidDecisionText
        $exit125Script = Join-Path $selfTestRoot 'exit125.ps1'
        $nonzeroScript = Join-Path $selfTestRoot 'nonzero.ps1'
        Write-AtomicUtf8Text $exit125Script "param([Parameter(ValueFromRemainingArguments=`$true)][string[]]`$Arguments)`nexit 125`n"
        Write-AtomicUtf8Text $nonzeroScript "param([Parameter(ValueFromRemainingArguments=`$true)][string[]]`$Arguments)`nexit 7`n"
        $exit125 = Invoke-GoldenCapture $syntheticBaselineEntries[0] $exit125Script $selfTestRoot
        if ($exit125 -ne 125) {
            throw "Synthetic exit125 capture returned $exit125."
        }
        Assert-R1SelfTestRejects 'injected_exit125' { Assert-R1CaptureExitCode $exit125 'Indoor' }
        $nonzero = Invoke-GoldenCapture $syntheticBaselineEntries[0] $nonzeroScript $selfTestRoot
        if ($nonzero -ne 7) {
            throw "Synthetic nonzero capture returned $nonzero."
        }
        Assert-R1SelfTestRejects 'injected_crash_nonzero' { Assert-R1CaptureExitCode $nonzero 'Indoor' }

        $originalBytes = [Text.Encoding]::UTF8.GetBytes("original`n")
        $publishedBytes = [Text.Encoding]::UTF8.GetBytes("published`n")
        $transactionEntries = @(
            [pscustomobject]@{
                Scene = 'Indoor'
                Source = Join-Path $selfTestRoot 'Source-Indoor.png'
                Candidate = Join-Path $selfTestRoot 'Candidate-Indoor.png'
                SourceTemporary = Join-Path $selfTestRoot 'Source-Indoor.png.tmp'
                Backup = Join-Path $selfTestRoot 'Source-Indoor.png.bak'
                RollbackDiscard = Join-Path $selfTestRoot 'Source-Indoor.png.rollback-discard'
            },
            [pscustomobject]@{
                Scene = 'Outdoor'
                Source = Join-Path $selfTestRoot 'Source-Outdoor.png'
                Candidate = Join-Path $selfTestRoot 'Candidate-Outdoor.png'
                SourceTemporary = Join-Path $selfTestRoot 'Source-Outdoor.png.tmp'
                Backup = Join-Path $selfTestRoot 'Source-Outdoor.png.bak'
                RollbackDiscard = Join-Path $selfTestRoot 'Source-Outdoor.png.rollback-discard'
            }
        )
        $sourceHashes = @{}
        foreach ($entry in $transactionEntries) {
            [IO.File]::WriteAllBytes($entry.Source, $originalBytes)
            [IO.File]::WriteAllBytes($entry.Candidate, $publishedBytes)
            $sourceHashes[$entry.Scene] = (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant()
        }
        try {
            Invoke-BaselinePublishTransaction $transactionEntries $sourceHashes 'before-publish'
        } catch {
            $beforePublishRejected = $true
        }
        if (-not $beforePublishRejected) {
            throw 'Baseline transaction did not reject the injected before-publish failure.'
        }
        foreach ($entry in $transactionEntries) {
            if ((Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant() -cne $sourceHashes[$entry.Scene]) {
                throw "Baseline source changed during before-publish self-test: $($entry.Scene)"
            }
        }
        try {
            Invoke-BaselinePublishTransaction $transactionEntries $sourceHashes 'after-first-publish'
        } catch {
            $afterFirstRejected = $true
        }
        if (-not $afterFirstRejected) {
            throw 'Baseline transaction did not reject the injected after-first-publish failure.'
        }
        foreach ($entry in $transactionEntries) {
        if ((Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant() -cne $sourceHashes[$entry.Scene]) {
                throw "Baseline rollback did not restore source bytes: $($entry.Scene)"
            }
            foreach ($path in @($entry.SourceTemporary, $entry.Backup, $entry.RollbackDiscard)) {
                if ([IO.File]::Exists($path)) {
                    throw "Baseline transaction residue remains: $path"
                }
            }
        }
        Invoke-BaselinePublishTransaction $transactionEntries $sourceHashes
        foreach ($entry in $transactionEntries) {
            $sourceHash = (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash.ToUpperInvariant()
            $candidateHash = (Get-FileHash -LiteralPath $entry.Candidate -Algorithm SHA256).Hash.ToUpperInvariant()
            if ($sourceHash -cne $candidateHash) {
                throw "Baseline success publish hash mismatch: $($entry.Scene)"
            }
            foreach ($path in @($entry.SourceTemporary, $entry.Backup, $entry.RollbackDiscard)) {
                if ([IO.File]::Exists($path)) {
                    throw "Baseline successful transaction residue remains: $path"
                }
            }
        }
        Write-Output 'BASELINE_R1_SELF_TEST=PASS manifest_sentinel=valid bridge=PASS source_unchanged=1 rollback=restored'
        Write-Output 'BASELINE_R1_SELF_TEST_SUCCESS=PASS source_matches_candidate=1 residue=0'
    } finally {
        if ([IO.Directory]::Exists($selfTestRoot)) {
            Assert-NoReparseAncestors $selfTestRoot $buildRoot 'Baseline self-test cleanup root'
            Remove-Item -LiteralPath $selfTestRoot -Recurse -Force
        }
    }
}

function Invoke-R2SelfTest {
    $repoRoot = Get-NormalizedPath (Join-Path $PSScriptRoot '..')
    $r2BaselineRoot = Get-NormalizedPath (Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation')
    $r2ThresholdRoot = Get-NormalizedPath (Join-Path $repoRoot 'Test\Core\Rendering\Thresholds\RenderingValidation')
    $goldenPaths = @(
        (Join-Path $r2BaselineRoot 'R2SkyMorning.png'),
        (Join-Path $r2BaselineRoot 'R2SkyNoon.png'),
        (Join-Path $r2BaselineRoot 'R2SkyEvening.png')
    )
    $thresholdPaths = @(
        (Join-Path $r2ThresholdRoot 'R2SkyTimeSweep.tsv'),
        (Join-Path $r2ThresholdRoot 'R2CsmAcceptance.tsv')
    )
    foreach ($path in $goldenPaths + $thresholdPaths) {
        if (-not [IO.File]::Exists($path)) {
            throw "R2 acceptance artifact is missing: $path"
        }
    }
    foreach ($path in $goldenPaths) {
        $bytes = [IO.File]::ReadAllBytes($path)
        if ($bytes.Length -lt 8 -or
            $bytes[0] -ne 137 -or $bytes[1] -ne 80 -or $bytes[2] -ne 78 -or $bytes[3] -ne 71 -or
            $bytes[4] -ne 13 -or $bytes[5] -ne 10 -or $bytes[6] -ne 26 -or $bytes[7] -ne 10) {
            throw "R2 golden is not a PNG: $path"
        }
    }
    $skyText = [IO.File]::ReadAllText($thresholdPaths[0], [Text.UTF8Encoding]::new($false))
    $csmText = [IO.File]::ReadAllText($thresholdPaths[1], [Text.UTF8Encoding]::new($false))
    if ($skyText -notmatch '(?m)^schema=NorvesLib\.RenderingValidation\.R2SkyTimeSweep\.v1$' -or
        @('morning', 'noon', 'evening' | Where-Object { $skyText -notmatch "(?m)^case=$($_)\s" }).Count -ne 0) {
        throw 'R2 sky golden threshold contract is invalid.'
    }
    if ($csmText -notmatch '(?m)^schema=NorvesLib\.RenderingValidation\.R2CsmAcceptance\.v1$' -or
        $csmText -notmatch '(?m)^cascade_count=4\s') {
        throw 'R2 CSM threshold contract is invalid.'
    }

    $r1Paths = @(
        (Join-Path $r2BaselineRoot 'Indoor.png'),
        (Join-Path $r2BaselineRoot 'Outdoor.png')
    )
    $r1HashesBefore = @{}
    foreach ($path in $r1Paths) {
        if (-not [IO.File]::Exists($path)) {
            throw "R1 baseline is missing: $path"
        }
        $r1HashesBefore[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
    foreach ($path in $r1Paths) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $r1HashesBefore[$path]) {
            throw "R1 baseline changed during R2 golden self-test: $path"
        }
    }
    Write-Output 'BASELINE_R2_SELF_TEST=PASS goldens=3 thresholds=2 r1_baselines_unchanged=1'
}

if ($SelfTestR1Contract) {
    Invoke-R1SelfTest
    exit 0
}
if ($SelfTestR2Contract) {
    Invoke-R2SelfTest
    exit 0
}
if ($GenerateCandidate) {
    Invoke-R1GenerateCandidate
    exit 0
}
if ($PublishApprovedCandidate) {
    Invoke-R1PublishCandidate
    exit 0
}
throw 'Specify -GenerateCandidate, -PublishApprovedCandidate, -SelfTestR1Contract, or -SelfTestR2Contract.'
