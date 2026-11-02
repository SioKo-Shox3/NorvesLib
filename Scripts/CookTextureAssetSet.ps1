param(
    [Parameter(Mandatory = $true)]
    [string]$AssetCookExe,

    [Parameter(Mandatory = $true)]
    [string]$SpecPath,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeRoot,

    [string]$ManifestPath = "",

    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

function Test-IsUncPath {
    param([string]$Path)

    return $Path.Trim() -match '^[\\/]{2}'
}

function Test-IsDriveQualifiedAbsolutePath {
    param([string]$Path)

    return $Path.Trim() -match '^[A-Za-z]:[\\/]'
}

function Test-IsDriveRelativePath {
    param([string]$Path)

    return $Path.Trim() -match '^[A-Za-z]:(?![\\/])'
}

function Test-IsRootRelativePath {
    param([string]$Path)

    $trimmed = $Path.Trim()
    if (Test-IsUncPath $trimmed) {
        return $false
    }

    return $trimmed.StartsWith('\') -or $trimmed.StartsWith('/')
}

function Test-IsAbsolutePath {
    param([string]$Path)

    return (Test-IsUncPath $Path) -or (Test-IsDriveQualifiedAbsolutePath $Path)
}

function Resolve-RepoRelativePath {
    param(
        [string]$Path,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Name must not be empty"
    }

    if ((Test-IsDriveRelativePath $Path) -or (Test-IsRootRelativePath $Path)) {
        throw "$Name must be absolute or repository-relative: $Path"
    }

    if (Test-IsAbsolutePath $Path) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Normalize-PathForBoundary {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetPathRoot($fullPath)

    while (($fullPath.Length -gt $root.Length) -and
           ($fullPath.EndsWith([string][System.IO.Path]::DirectorySeparatorChar) -or
            $fullPath.EndsWith([string][System.IO.Path]::AltDirectorySeparatorChar))) {
        $fullPath = $fullPath.Substring(0, $fullPath.Length - 1)
    }

    return $fullPath
}

function Test-PathEqual {
    param(
        [string]$LeftPath,
        [string]$RightPath
    )

    $normalizedLeft = Normalize-PathForBoundary $LeftPath
    $normalizedRight = Normalize-PathForBoundary $RightPath
    return [string]::Equals($normalizedLeft, $normalizedRight, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-ExistingFile {
    param(
        [string]$Path,
        [string]$Name
    )

    $resolved = Resolve-RepoRelativePath -Path $Path -Name $Name
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Name not found: $resolved"
    }

    return $resolved
}

function Quote-ProcessArgument {
    param([string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    return '"' + ($Value -replace '"', '\"') + '"'
}

function Invoke-CheckedProcess {
    param(
        [string]$ExePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$TimeoutSeconds,
        [string]$Name
    )

    $argumentLine = ($Arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join " "
    Write-Host "$Name $argumentLine"

    $process = Start-Process `
        -FilePath $ExePath `
        -ArgumentList $argumentLine `
        -WorkingDirectory $WorkingDirectory `
        -WindowStyle Hidden `
        -PassThru

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw "$Name timed out after $TimeoutSeconds seconds"
    }

    if ($process.ExitCode -ne 0) {
        throw "$Name failed with exit code $($process.ExitCode)"
    }
}

if ($TimeoutSeconds -le 0) {
    throw "TimeoutSeconds must be greater than zero"
}

$AssetCookPath = Assert-ExistingFile -Path $AssetCookExe -Name "AssetCook"
$ResolvedSpecPath = Assert-ExistingFile -Path $SpecPath -Name "Asset set spec"
$ResolvedRuntimeRoot = Resolve-RepoRelativePath -Path $RuntimeRoot -Name "RuntimeRoot"

if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ResolvedManifestPath = [System.IO.Path]::GetFullPath((Join-Path $ResolvedRuntimeRoot "manifest.json"))
}
else {
    $ResolvedManifestPath = Resolve-RepoRelativePath -Path $ManifestPath -Name "ManifestPath"
}

$manifestParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $ResolvedManifestPath))
if (-not (Test-PathEqual -LeftPath $manifestParent -RightPath $ResolvedRuntimeRoot)) {
    throw "ManifestPath must be directly under RuntimeRoot: $ResolvedManifestPath"
}
if ((Test-Path -LiteralPath $ResolvedRuntimeRoot) -and -not (Test-Path -LiteralPath $ResolvedRuntimeRoot -PathType Container)) {
    throw "RuntimeRoot must be a directory path: $ResolvedRuntimeRoot"
}
if ((Test-Path -LiteralPath $ResolvedManifestPath) -and -not (Test-Path -LiteralPath $ResolvedManifestPath -PathType Leaf)) {
    throw "ManifestPath must be a file path: $ResolvedManifestPath"
}

$runtimeParent = Split-Path -Parent (Normalize-PathForBoundary $ResolvedRuntimeRoot)
New-Item -ItemType Directory -Path $runtimeParent -Force | Out-Null

Invoke-CheckedProcess `
    -ExePath $AssetCookPath `
    -WorkingDirectory $RepoRoot `
    -TimeoutSeconds $TimeoutSeconds `
    -Name "AssetCook" `
    -Arguments @(
        "--asset-set", $ResolvedSpecPath,
        "--runtime-root", $ResolvedRuntimeRoot,
        "--source-root", $RepoRoot,
        "--manifest", $ResolvedManifestPath
    )

Write-Host "Cooked texture asset set passed."
Write-Host "Spec: $ResolvedSpecPath"
Write-Host "Runtime root: $ResolvedRuntimeRoot"
Write-Host "Manifest: $ResolvedManifestPath"
