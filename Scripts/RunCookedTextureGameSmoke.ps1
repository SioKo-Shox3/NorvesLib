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

$ExpectedTextureCount = 5
$TextureSpecs = @(
    [pscustomobject]@{
        LogicalPath = "Assets/Textures/Silver/silver_albedo.png"
        SourcePath = ".\Assets\Textures\Silver\silver_albedo.png"
        Format = "nvtex.v0.rgba8.srgb"
        PackageName = "silver_albedo.nvpkg"
        EntryPath = "Assets/Textures/Silver/silver_albedo.nvtex"
    },
    [pscustomobject]@{
        LogicalPath = "Assets/Textures/Silver/silver_normal-ogl.png"
        SourcePath = ".\Assets\Textures\Silver\silver_normal-ogl.png"
        Format = "nvtex.v0.rgba8.linear"
        PackageName = "silver_normal-ogl.nvpkg"
        EntryPath = "Assets/Textures/Silver/silver_normal-ogl.nvtex"
    },
    [pscustomobject]@{
        LogicalPath = "Assets/Textures/Silver/silver_metallic.png"
        SourcePath = ".\Assets\Textures\Silver\silver_metallic.png"
        Format = "nvtex.v0.r8.linear"
        PackageName = "silver_metallic.nvpkg"
        EntryPath = "Assets/Textures/Silver/silver_metallic.nvtex"
    },
    [pscustomobject]@{
        LogicalPath = "Assets/Textures/Silver/silver_roughness.png"
        SourcePath = ".\Assets\Textures\Silver\silver_roughness.png"
        Format = "nvtex.v0.r8.linear"
        PackageName = "silver_roughness.nvpkg"
        EntryPath = "Assets/Textures/Silver/silver_roughness.nvtex"
    },
    [pscustomobject]@{
        LogicalPath = "Assets/Textures/Silver/silver_ao.png"
        SourcePath = ".\Assets\Textures\Silver\silver_ao.png"
        Format = "nvtex.v0.r8.linear"
        PackageName = "silver_ao.nvpkg"
        EntryPath = "Assets/Textures/Silver/silver_ao.nvtex"
    }
)

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

function Test-JsonPropertyExists {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $false
    }

    return $Object.PSObject.Properties.Name -contains $Name
}

function Get-JsonPropertyValue {
    param(
        [object]$Object,
        [string]$Name,
        [string]$Context
    )

    if (-not (Test-JsonPropertyExists -Object $Object -Name $Name)) {
        throw "$Context is missing required field: $Name"
    }

    return $Object.PSObject.Properties[$Name].Value
}

function Get-ManifestAssets {
    param(
        [object]$Manifest,
        [string]$ManifestName
    )

    if (-not (Test-JsonPropertyExists -Object $Manifest -Name "assets")) {
        throw "$ManifestName is missing required field: assets"
    }

    $assets = Get-JsonPropertyValue -Object $Manifest -Name "assets" -Context $ManifestName
    if ($null -eq $assets) {
        return @()
    }

    return @($assets)
}

function Assert-AggregateManifest {
    param(
        [object[]]$Assets,
        [int]$ExpectedCount,
        [string]$RuntimeRoot,
        [string]$PackageParentPath,
        [string]$ManifestName
    )

    if ($Assets.Count -ne $ExpectedCount) {
        throw "$ManifestName must contain exactly $ExpectedCount assets, got $($Assets.Count)"
    }

    $requiredFields = @(
        "logical_path",
        "kind",
        "source_hash",
        "variant",
        "format",
        "cooked_package",
        "entry_name",
        "entry_type",
        "cooked_hash",
        "cooked_version"
    )
    $nonEmptyStringFields = @(
        "logical_path",
        "kind",
        "source_hash",
        "variant",
        "format",
        "cooked_package",
        "entry_name",
        "entry_type",
        "cooked_hash"
    )
    $seenKeys = @{}

    for ($i = 0; $i -lt $Assets.Count; ++$i) {
        $asset = $Assets[$i]
        $context = "$ManifestName asset[$i]"

        foreach ($field in $requiredFields) {
            $value = Get-JsonPropertyValue -Object $asset -Name $field -Context $context
            if (($nonEmptyStringFields -contains $field) -and [string]::IsNullOrWhiteSpace([string]$value)) {
                throw "$context has an empty required field: $field"
            }
        }

        $logicalPath = Get-JsonPropertyValue -Object $asset -Name "logical_path" -Context $context
        $kind = Get-JsonPropertyValue -Object $asset -Name "kind" -Context $context
        $variant = Get-JsonPropertyValue -Object $asset -Name "variant" -Context $context
        $key = "$logicalPath|$kind|$variant"
        if ($seenKeys.ContainsKey($key)) {
            throw "$ManifestName contains duplicate logical_path|kind|variant key: $key"
        }
        $seenKeys[$key] = $true

        $cookedPackage = Get-JsonPropertyValue -Object $asset -Name "cooked_package" -Context $context
        if ([System.IO.Path]::IsPathRooted($cookedPackage)) {
            throw "$context cooked_package must be RuntimeRoot-relative: $cookedPackage"
        }

        $packagePath = [System.IO.Path]::GetFullPath((Join-Path $RuntimeRoot $cookedPackage))
        if (-not (Test-PathUnderOrEqual -Path $packagePath -ParentPath $RuntimeRoot)) {
            throw "$context cooked_package escapes RuntimeRoot: $cookedPackage"
        }
        if (-not (Test-PathUnderOrEqual -Path $packagePath -ParentPath $PackageParentPath)) {
            throw "$context cooked_package must be under Cooked/Silver: $cookedPackage"
        }
        if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
            throw "$context cooked package not found: $packagePath"
        }
    }
}

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$AssetCookPath = Assert-ExistingFile -Path $AssetCookExe -Name "AssetCook"
$GamePath = Assert-ExistingFile -Path $GameExe -Name "Game"
$SourceTexturePathsByLogicalPath = @{}
foreach ($textureSpec in $TextureSpecs) {
    $SourceTexturePathsByLogicalPath[$textureSpec.LogicalPath] = Assert-ExistingFile `
        -Path $textureSpec.SourcePath `
        -Name "Source texture $($textureSpec.LogicalPath)"
}
$ResolvedSmokeDir = Resolve-RepoRelativePath $SmokeDir
$ResolvedLogPath = Resolve-RepoRelativePath $LogPath

$BuildRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "build"))
$CookedTextureGameSmokeRoot = [System.IO.Path]::GetFullPath((Join-Path $BuildRoot "CookedTextureGameSmoke"))
if (-not (Test-PathUnderOrEqual -Path $ResolvedSmokeDir -ParentPath $BuildRoot)) {
    throw "SmokeDir must be under the repository build directory: $ResolvedSmokeDir"
}
if (Test-PathEqual -LeftPath $ResolvedSmokeDir -RightPath $BuildRoot) {
    throw "SmokeDir must not be the repository build directory itself: $ResolvedSmokeDir"
}
if (-not (Test-PathUnderOrEqual -Path $ResolvedSmokeDir -ParentPath $CookedTextureGameSmokeRoot)) {
    throw "SmokeDir must be under the CookedTextureGameSmoke build directory: $ResolvedSmokeDir"
}
if (Test-PathEqual -LeftPath $ResolvedSmokeDir -RightPath $CookedTextureGameSmokeRoot) {
    throw "SmokeDir must be a child directory under CookedTextureGameSmoke: $ResolvedSmokeDir"
}

if (-not (Test-PathUnderOrEqual -Path $ResolvedLogPath -ParentPath $RepoRoot)) {
    throw "LogPath must be under the repository root: $ResolvedLogPath"
}
if (Test-PathEqual -LeftPath $ResolvedLogPath -RightPath $RepoRoot) {
    throw "LogPath must be a file path, not the repository root directory: $ResolvedLogPath"
}
if ((Test-Path -LiteralPath $ResolvedLogPath) -and -not (Test-Path -LiteralPath $ResolvedLogPath -PathType Leaf)) {
    throw "LogPath must be a file path: $ResolvedLogPath"
}

if (Test-Path -LiteralPath $ResolvedSmokeDir) {
    Remove-Item -LiteralPath $ResolvedSmokeDir -Recurse -Force
}

$RuntimeRoot = Join-Path $ResolvedSmokeDir "RuntimeRoot"
$CookedSilverDir = Join-Path $RuntimeRoot "Cooked\Silver"
New-Item -ItemType Directory -Path $CookedSilverDir -Force | Out-Null

$ManifestPath = Join-Path $RuntimeRoot "manifest.json"
$AggregateAssets = @()

foreach ($textureSpec in $TextureSpecs) {
    $packagePath = Join-Path $CookedSilverDir $textureSpec.PackageName
    $sourceTexturePath = $SourceTexturePathsByLogicalPath[$textureSpec.LogicalPath]

    Invoke-CheckedProcess `
        -ExePath $AssetCookPath `
        -WorkingDirectory $RepoRoot `
        -TimeoutSeconds $TimeoutSeconds `
        -Name "AssetCook" `
        -Arguments @(
            "--input", $sourceTexturePath,
            "--out", $packagePath,
            "--manifest", $ManifestPath,
            "--logical", $textureSpec.LogicalPath,
            "--kind", "texture",
            "--entry", $textureSpec.EntryPath,
            "--entry-type", "Tex0",
            "--format", $textureSpec.Format,
            "--variant", "default"
        )

    $singleManifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $singleAssets = @(Get-ManifestAssets `
        -Manifest $singleManifest `
        -ManifestName "AssetCook manifest for $($textureSpec.LogicalPath)")
    if ($singleAssets.Count -ne 1) {
        throw "AssetCook manifest for $($textureSpec.LogicalPath) must contain exactly 1 asset, got $($singleAssets.Count)"
    }

    $AggregateAssets += $singleAssets[0]
}

Assert-AggregateManifest `
    -Assets $AggregateAssets `
    -ExpectedCount $ExpectedTextureCount `
    -RuntimeRoot $RuntimeRoot `
    -PackageParentPath $CookedSilverDir `
    -ManifestName "Aggregate manifest"

$AggregateManifest = [ordered]@{
    version = 1
    assets = @($AggregateAssets)
}
$AggregateJson = $AggregateManifest | ConvertTo-Json -Depth 16
[System.IO.File]::WriteAllText($ManifestPath, $AggregateJson, [System.Text.UTF8Encoding]::new($false))

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
$CookedSourceMatchCount = 0
$CookedUploadMatchCount = 0

foreach ($textureSpec in $TextureSpecs) {
    $escapedTarget = [regex]::Escape($textureSpec.LogicalPath)

    $cookedSourceMatches = Require-LogMatch `
        -Lines $LogLines `
        -Pattern "stage=texture_asset_resolve (?=.*source=cooked_nvtex)(?=.*path=`"$escapedTarget`")(?=.*success=1)" `
        -Reason "Expected successful cooked nvtex source log for $($textureSpec.LogicalPath)"
    $CookedSourceMatchCount += $cookedSourceMatches.Count

    $cookedUploadMatches = Require-LogMatch `
        -Lines $LogLines `
        -Pattern "stage=texture_cooked_upload (?=.*source=cooked_nvtex)(?=.*path=`"$escapedTarget`")(?=.*success=1)" `
        -Reason "Expected successful cooked texture upload log for $($textureSpec.LogicalPath)"
    $CookedUploadMatchCount += $cookedUploadMatches.Count

    Forbid-LogMatch `
        -Lines $LogLines `
        -Pattern "(?=.*source=loose_stbi)(?=.*path=`"$escapedTarget`")" `
        -Reason "Unexpected loose_stbi source log for $($textureSpec.LogicalPath)"
}

Require-LogMatch `
    -Lines $LogLines `
    -Pattern "Silver PBR material textures loaded" `
    -Reason "Expected Silver PBR material textures loaded log"

Write-Host "Cooked texture Game smoke passed."
Write-Host "Runtime root: $RuntimeRoot"
Write-Host "Manifest: $ManifestPath"
Write-Host "Log: $ResolvedLogPath"
Write-Host "Cooked source logs: $CookedSourceMatchCount"
Write-Host "Cooked upload logs: $CookedUploadMatchCount"
