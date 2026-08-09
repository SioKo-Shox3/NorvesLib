param(
    [Parameter(Mandatory = $true)]
    [switch]$Approve
)

$ErrorActionPreference = 'Stop'

if (-not $Approve) {
    throw 'Baseline update requires -Approve.'
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

function Remove-FixedTemporaryFiles {
    param([Parameter(Mandatory = $true)][array]$Entries)
    foreach ($entry in $Entries) {
        foreach ($path in @($entry.Staging, $entry.Backup, $entry.RollbackDiscard)) {
            if ([IO.File]::Exists($path)) {
                Remove-Item -LiteralPath $path -Force
            }
        }
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Get-NormalizedPath (Join-Path $repoRoot 'build')
$sourceRoot = Get-NormalizedPath (Join-Path $repoRoot 'Test\Core\Rendering\Baselines\RenderingValidation')
$stagingRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\BaselineStaging')
$backupRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\BaselineBackup')
$runsRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\Runs')
$exe = Get-NormalizedPath (Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe')
$validator = Get-NormalizedPath (Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageComparatorTest.exe')

Assert-PathWithinRoot $buildRoot $repoRoot 'Build root'
Assert-PathWithinRoot $sourceRoot $repoRoot 'Baseline source root'
Assert-PathWithinRoot $stagingRoot $buildRoot 'Baseline staging root'
Assert-PathWithinRoot $backupRoot $buildRoot 'Baseline backup root'
Assert-PathWithinRoot $runsRoot $buildRoot 'Rendering validation runs root'
Assert-PathWithinRoot $exe $buildRoot 'Golden executable'
Assert-PathWithinRoot $validator $buildRoot 'Staging validator'

if (-not [IO.File]::Exists($exe) -or -not [IO.File]::Exists($validator)) {
    Write-Error 'Golden executables are missing. Run: cmake --build build --config Debug --target RenderingGoldenImageTest RenderingGoldenImageComparatorTest'
    exit 1
}

New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
New-Item -ItemType Directory -Force -Path $runsRoot | Out-Null

$entries = @(
    [pscustomobject]@{
        Scene = 'indoor'
        Source = Get-NormalizedPath (Join-Path $sourceRoot 'Indoor.png')
        Staging = Get-NormalizedPath (Join-Path $stagingRoot 'Indoor.png.tmp')
        Backup = Get-NormalizedPath (Join-Path $backupRoot 'Indoor.png.bak')
        RollbackDiscard = Get-NormalizedPath (Join-Path $backupRoot 'Indoor.png.rollback-discard')
        Existed = $false
    },
    [pscustomobject]@{
        Scene = 'outdoor'
        Source = Get-NormalizedPath (Join-Path $sourceRoot 'Outdoor.png')
        Staging = Get-NormalizedPath (Join-Path $stagingRoot 'Outdoor.png.tmp')
        Backup = Get-NormalizedPath (Join-Path $backupRoot 'Outdoor.png.bak')
        RollbackDiscard = Get-NormalizedPath (Join-Path $backupRoot 'Outdoor.png.rollback-discard')
        Existed = $false
    }
)

foreach ($entry in $entries) {
    Assert-PathWithinRoot $entry.Source $sourceRoot 'Baseline source file'
    Assert-PathWithinRoot $entry.Staging $stagingRoot 'Baseline staging file'
    Assert-PathWithinRoot $entry.Backup $backupRoot 'Baseline backup file'
    Assert-PathWithinRoot $entry.RollbackDiscard $backupRoot 'Baseline rollback discard file'
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
        [IO.Path]::GetPathRoot($entry.Source),
        [IO.Path]::GetPathRoot($entry.Staging))) {
        throw "Source and staging must be on the same volume: $($entry.Scene)"
    }
}

$failureMode = [Environment]::GetEnvironmentVariable('NORVESLIB_BASELINE_TRANSACTION_TEST_FAILURE')
if ($failureMode -and $failureMode -notin @('after-indoor-staging', 'after-first-publish')) {
    throw "Unsupported transaction test failure mode: $failureMode"
}

Write-Host 'Current baseline status:'
& git -C $repoRoot status --short -- $entries[0].Source $entries[1].Source
if ($LASTEXITCODE -ne 0) {
    throw 'git status failed for the fixed baseline paths.'
}

Remove-FixedTemporaryFiles $entries
$publishedIndices = @()

function Invoke-GoldenCapture {
    param([Parameter(Mandatory = $true)][string]$Scene)
    Push-Location $runsRoot
    try {
        & $exe "--scene=$Scene" '--write-baseline-staging' | Out-Host
        return $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}

function Invoke-StagingValidator {
    Push-Location $runsRoot
    try {
        & $validator '--validate-fixed-staging' | Out-Host
        return $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}

try {
    foreach ($entry in $entries) {
        $exitCode = Invoke-GoldenCapture $entry.Scene
        if ($exitCode -eq 125) {
            throw "GPU skip is not a baseline update: $($entry.Scene)"
        }
        if ($exitCode -ne 0) {
            throw "Baseline update failed: $($entry.Scene) exit=$exitCode"
        }
        if (-not [IO.File]::Exists($entry.Staging)) {
            throw "Golden executable did not create fixed staging: $($entry.Scene)"
        }
        if ($entry.Scene -eq 'indoor' -and $failureMode -eq 'after-indoor-staging') {
            throw 'Injected baseline transaction failure: after-indoor-staging'
        }
    }

    $validatorExitCode = Invoke-StagingValidator
    if ($validatorExitCode -ne 0) {
        throw "Staging PNG validation failed: exit=$validatorExitCode"
    }

    for ($index = 0; $index -lt $entries.Count; ++$index) {
        $entry = $entries[$index]
        $entry.Existed = [IO.File]::Exists($entry.Source)
        if ($entry.Existed) {
            [IO.File]::Replace($entry.Staging, $entry.Source, $entry.Backup, $true)
        }
        else {
            [IO.File]::Move($entry.Staging, $entry.Source)
        }
        $publishedIndices += $index
        if ($index -eq 0 -and $failureMode -eq 'after-first-publish') {
            throw 'Injected baseline transaction failure: after-first-publish'
        }
    }

    Write-Host 'Published Indoor.png and Outdoor.png as one baseline transaction.'
    & git -C $repoRoot status --short -- $entries[0].Source $entries[1].Source
}
catch {
    $primaryError = $_
    $rollbackError = $null
    for ($publishedIndex = $publishedIndices.Count - 1; $publishedIndex -ge 0; --$publishedIndex) {
        $entry = $entries[$publishedIndices[$publishedIndex]]
        try {
            if ($entry.Existed) {
                if (-not [IO.File]::Exists($entry.Backup)) {
                    throw "Rollback backup is missing: $($entry.Scene)"
                }
                if ([IO.File]::Exists($entry.RollbackDiscard)) {
                    Remove-Item -LiteralPath $entry.RollbackDiscard -Force
                }
                [IO.File]::Replace($entry.Backup, $entry.Source, $entry.RollbackDiscard, $true)
            }
            elseif ([IO.File]::Exists($entry.Source)) {
                Remove-Item -LiteralPath $entry.Source -Force
            }
        }
        catch {
            $rollbackError = $_
        }
    }
    if ($rollbackError) {
        throw "Baseline publish failed and rollback also failed. publish=$primaryError rollback=$rollbackError"
    }
    throw $primaryError
}
finally {
    Remove-FixedTemporaryFiles $entries
}
