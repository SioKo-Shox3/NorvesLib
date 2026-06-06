param(
    [Parameter(Mandatory = $true)]
    [string]$AssetCookExe,

    [Parameter(Mandatory = $true)]
    [string]$GameExe,

    [int]$FrameCount = 30000,

    [int]$TimeoutSeconds = 180,

    [string]$SmokeDir = ".\build\CookedTextureGameSmoke\Debug",

    [string]$LogPath = ".\Game.log"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$TargetLogicalPath = "Assets/Textures/Silver/silver_albedo.png"

function Resolve-RepoRelativePath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Normalize-PathForBoundary {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    return $fullPath.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
}

function Test-PathUnderOrEqual {
    param(
        [string]$Path,
        [string]$ParentPath
    )

    $normalizedPath = Normalize-PathForBoundary $Path
    $normalizedParent = Normalize-PathForBoundary $ParentPath

    if ([string]::Equals($normalizedPath, $normalizedParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }

    $parentWithSeparator = $normalizedParent + [System.IO.Path]::DirectorySeparatorChar
    return $normalizedPath.StartsWith($parentWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-ExistingFile {
    param(
        [string]$Path,
        [string]$Name
    )

    $resolved = Resolve-RepoRelativePath $Path
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

function Require-LogMatch {
    param(
        [string[]]$Lines,
        [string]$Pattern,
        [string]$Reason
    )

    $matches = @($Lines | Where-Object { $_ -match $Pattern })
    if ($matches.Count -eq 0) {
        throw $Reason
    }

    return ,$matches
}

function Forbid-LogMatch {
    param(
        [string[]]$Lines,
        [string]$Pattern,
        [string]$Reason
    )

    $matches = @($Lines | Where-Object { $_ -match $Pattern })
    if ($matches.Count -gt 0) {
        $sample = $matches | Select-Object -First 3
        throw ($Reason + "`n" + ($sample -join "`n"))
    }
}

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$AssetCookPath = Assert-ExistingFile -Path $AssetCookExe -Name "AssetCook"
$GamePath = Assert-ExistingFile -Path $GameExe -Name "Game"
$SourceTexturePath = Assert-ExistingFile -Path ".\Assets\Textures\Silver\silver_albedo.png" -Name "Source texture"
$ResolvedSmokeDir = Resolve-RepoRelativePath $SmokeDir
$ResolvedLogPath = Resolve-RepoRelativePath $LogPath

$BuildRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "build"))
if (-not (Test-PathUnderOrEqual -Path $ResolvedSmokeDir -ParentPath $BuildRoot)) {
    throw "SmokeDir must be under the repository build directory: $ResolvedSmokeDir"
}

if (Test-Path -LiteralPath $ResolvedSmokeDir) {
    Remove-Item -LiteralPath $ResolvedSmokeDir -Recurse -Force
}

$RuntimeRoot = Join-Path $ResolvedSmokeDir "RuntimeRoot"
$CookedDir = Join-Path $RuntimeRoot "Cooked"
New-Item -ItemType Directory -Path $CookedDir -Force | Out-Null

$PackagePath = Join-Path $CookedDir "silver_albedo.nvpkg"
$ManifestPath = Join-Path $RuntimeRoot "manifest.json"
$EntryPath = "Assets/Textures/Silver/silver_albedo.nvtex"

Invoke-CheckedProcess `
    -ExePath $AssetCookPath `
    -WorkingDirectory $RepoRoot `
    -TimeoutSeconds $TimeoutSeconds `
    -Name "AssetCook" `
    -Arguments @(
        "--input", $SourceTexturePath,
        "--out", $PackagePath,
        "--manifest", $ManifestPath,
        "--logical", $TargetLogicalPath,
        "--kind", "texture",
        "--entry", $EntryPath,
        "--entry-type", "Tex0",
        "--format", "nvtex.v0.rgba8.srgb",
        "--variant", "default"
    )

if (Test-Path -LiteralPath $ResolvedLogPath) {
    Remove-Item -LiteralPath $ResolvedLogPath -Force
}

Invoke-CheckedProcess `
    -ExePath $GamePath `
    -WorkingDirectory $RepoRoot `
    -TimeoutSeconds $TimeoutSeconds `
    -Name "Game" `
    -Arguments @(
        "--exit-after-frames=$FrameCount",
        "--texture-asset-root", $RuntimeRoot,
        "--texture-asset-manifest", $ManifestPath
    )

if (-not (Test-Path -LiteralPath $ResolvedLogPath -PathType Leaf)) {
    throw "Game log was not written: $ResolvedLogPath"
}

$LogLines = Get-Content -LiteralPath $ResolvedLogPath
$EscapedTarget = [regex]::Escape($TargetLogicalPath)

$CookedSourceMatches = Require-LogMatch `
    -Lines $LogLines `
    -Pattern "source=cooked_nvtex .*path=`"$EscapedTarget`"" `
    -Reason "Expected cooked nvtex source log for $TargetLogicalPath"

$CookedUploadMatches = Require-LogMatch `
    -Lines $LogLines `
    -Pattern "stage=texture_cooked_upload .*source=cooked_nvtex .*path=`"$EscapedTarget`".*success=1" `
    -Reason "Expected successful cooked texture upload log for $TargetLogicalPath"

Forbid-LogMatch `
    -Lines $LogLines `
    -Pattern "source=loose_stbi .*path=`"$EscapedTarget`"" `
    -Reason "Unexpected loose_stbi source log for $TargetLogicalPath"

Write-Host "Cooked texture Game smoke passed."
Write-Host "Runtime root: $RuntimeRoot"
Write-Host "Manifest: $ManifestPath"
Write-Host "Log: $ResolvedLogPath"
Write-Host "Cooked source logs: $($CookedSourceMatches.Count)"
Write-Host "Cooked upload logs: $($CookedUploadMatches.Count)"
