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

    $resolved = Resolve-RepoRelativePath -Path $Path -Name $Name
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Name not found: $resolved"
    }

    return $resolved
}

function Read-JsonFile {
    param(
        [string]$Path,
        [string]$Name
    )

    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        throw "Failed to read ${Name}: $Path`n$($_.Exception.Message)"
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

function Get-RequiredStringField {
    param(
        [object]$Object,
        [string]$Name,
        [string]$Context
    )

    $value = Get-JsonPropertyValue -Object $Object -Name $Name -Context $Context
    if (($value -isnot [string]) -or [string]::IsNullOrWhiteSpace($value)) {
        throw "$Context must have a non-empty string field: $Name"
    }

    return $value
}

function Get-OptionalStringField {
    param(
        [object]$Object,
        [string]$Name,
        [string]$Context,
        [string]$DefaultValue
    )

    if (-not (Test-JsonPropertyExists -Object $Object -Name $Name)) {
        return $DefaultValue
    }

    $value = Get-JsonPropertyValue -Object $Object -Name $Name -Context $Context
    if (($value -isnot [string]) -or [string]::IsNullOrWhiteSpace($value)) {
        throw "$Context must have a non-empty string field when present: $Name"
    }

    return $value
}

function Assert-VersionOne {
    param(
        [object]$Value,
        [string]$Context
    )

    $integerTypes = @(
        [byte], [sbyte], [int16], [uint16],
        [int], [uint32], [long], [uint64]
    )
    $isInteger = $false
    foreach ($type in $integerTypes) {
        if ($Value -is $type) {
            $isInteger = $true
            break
        }
    }

    if ((-not $isInteger) -or ([int64]$Value -ne 1)) {
        throw "$Context version must be integer 1"
    }
}

function Normalize-ManifestPath {
    param(
        [string]$Path,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Name must not be empty"
    }

    $trimmed = $Path.Trim()
    if ($trimmed -match '[\x00-\x1f\x7f]') {
        throw "$Name must not contain control characters: $Path"
    }

    if ((Test-IsAbsolutePath $trimmed) -or
        (Test-IsDriveRelativePath $trimmed) -or
        (Test-IsRootRelativePath $trimmed)) {
        throw "$Name must be a relative manifest path: $Path"
    }

    $slashPath = $trimmed -replace '\\', '/'
    $segments = @($slashPath -split '/')
    $normalizedSegments = @()
    foreach ($segment in $segments) {
        if ([string]::IsNullOrWhiteSpace($segment)) {
            throw "$Name contains an empty path segment: $Path"
        }
        if (($segment -eq ".") -or ($segment -eq "..")) {
            throw "$Name must not contain traversal segments: $Path"
        }

        $normalizedSegments += $segment
    }

    return $normalizedSegments -join '/'
}

function ConvertTo-AssetCookLogicalPath {
    param(
        [string]$Path,
        [string]$Name
    )

    $normalizedPath = Normalize-ManifestPath -Path $Path -Name $Name
    if ($normalizedPath -eq "Assets") {
        throw "$Name must include a path below Assets: $Path"
    }

    if ($normalizedPath.StartsWith("Assets/", [System.StringComparison]::Ordinal)) {
        $normalizedPath = $normalizedPath.Substring(7)
    }

    if ([string]::IsNullOrWhiteSpace($normalizedPath)) {
        throw "$Name must have a non-empty logical path: $Path"
    }

    return $normalizedPath
}

function Join-ManifestRelativePath {
    param(
        [string]$LeftPath,
        [string]$RightPath
    )

    if ([string]::IsNullOrWhiteSpace($LeftPath)) {
        return $RightPath
    }

    return "$LeftPath/$RightPath"
}

function Join-NormalizedRelativePath {
    param(
        [string]$BasePath,
        [string]$RelativePath
    )

    $result = $BasePath
    foreach ($segment in @($RelativePath -split '/')) {
        $result = Join-Path $result $segment
    }

    return [System.IO.Path]::GetFullPath($result)
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

function Assert-AssetStringFieldEquals {
    param(
        [object]$Asset,
        [string]$FieldName,
        [string]$ExpectedValue,
        [string]$Context
    )

    $actualValue = Get-JsonPropertyValue -Object $Asset -Name $FieldName -Context $Context
    if (($actualValue -isnot [string]) -or [string]::IsNullOrWhiteSpace($actualValue)) {
        throw "$Context must have a non-empty string field: $FieldName"
    }

    if (-not [string]::Equals($actualValue, $ExpectedValue, [System.StringComparison]::Ordinal)) {
        throw "$Context field $FieldName expected '$ExpectedValue', got '$actualValue'"
    }
}

function Assert-SingleTextureManifestEntry {
    param(
        [object]$Asset,
        [object]$TextureSpec,
        [string]$Context
    )

    Assert-AssetStringFieldEquals -Asset $Asset -FieldName "logical_path" -ExpectedValue $TextureSpec.LogicalPath -Context $Context
    Assert-AssetStringFieldEquals -Asset $Asset -FieldName "kind" -ExpectedValue "texture" -Context $Context
    Assert-AssetStringFieldEquals -Asset $Asset -FieldName "variant" -ExpectedValue $TextureSpec.Variant -Context $Context
    Assert-AssetStringFieldEquals -Asset $Asset -FieldName "format" -ExpectedValue $TextureSpec.Format -Context $Context
    Assert-AssetStringFieldEquals -Asset $Asset -FieldName "entry_name" -ExpectedValue $TextureSpec.EntryName -Context $Context
    Assert-AssetStringFieldEquals -Asset $Asset -FieldName "entry_type" -ExpectedValue "Tex0" -Context $Context

    $cookedPackage = Get-JsonPropertyValue -Object $Asset -Name "cooked_package" -Context $Context
    if (($cookedPackage -isnot [string]) -or [string]::IsNullOrWhiteSpace($cookedPackage)) {
        throw "$Context must have a non-empty string field: cooked_package"
    }

    $normalizedCookedPackage = Normalize-ManifestPath -Path $cookedPackage -Name "$Context cooked_package"
    if (-not [string]::Equals($normalizedCookedPackage, $TextureSpec.CookedPackage, [System.StringComparison]::Ordinal)) {
        throw "$Context field cooked_package expected '$($TextureSpec.CookedPackage)', got '$cookedPackage'"
    }
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
            if (($nonEmptyStringFields -contains $field) -and
                (($value -isnot [string]) -or [string]::IsNullOrWhiteSpace($value))) {
                throw "$context has an empty required field: $field"
            }
        }

        $logicalPath = Normalize-ManifestPath `
            -Path (Get-JsonPropertyValue -Object $asset -Name "logical_path" -Context $context) `
            -Name "$context logical_path"
        $kind = Get-JsonPropertyValue -Object $asset -Name "kind" -Context $context
        $variant = Get-JsonPropertyValue -Object $asset -Name "variant" -Context $context
        $key = "$logicalPath|$kind|$variant"
        if ($seenKeys.ContainsKey($key)) {
            throw "$ManifestName contains duplicate logical_path|kind|variant key: $key"
        }
        $seenKeys[$key] = $true

        $cookedPackage = Normalize-ManifestPath `
            -Path (Get-JsonPropertyValue -Object $asset -Name "cooked_package" -Context $context) `
            -Name "$context cooked_package"
        $packagePath = Join-NormalizedRelativePath -BasePath $RuntimeRoot -RelativePath $cookedPackage
        if (-not (Test-PathUnderOrEqual -Path $packagePath -ParentPath $RuntimeRoot)) {
            throw "$context cooked_package escapes RuntimeRoot: $cookedPackage"
        }
        if (-not (Test-PathUnderOrEqual -Path $packagePath -ParentPath $PackageParentPath)) {
            throw "$context cooked_package must be under the asset set package root: $cookedPackage"
        }
        if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
            throw "$context cooked package not found: $packagePath"
        }
    }
}

function Read-TextureAssetSetSpec {
    param(
        [string]$ResolvedSpecPath,
        [string]$ResolvedRuntimeRoot
    )

    $spec = Read-JsonFile -Path $ResolvedSpecPath -Name "asset set spec"
    $version = Get-JsonPropertyValue -Object $spec -Name "version" -Context "Asset set spec"
    Assert-VersionOne -Value $version -Context "Asset set spec"

    $name = Get-RequiredStringField -Object $spec -Name "name" -Context "Asset set spec"
    $packageRoot = Normalize-ManifestPath `
        -Path (Get-RequiredStringField -Object $spec -Name "package_root" -Context "Asset set spec") `
        -Name "Asset set spec package_root"
    $defaultVariant = Get-RequiredStringField -Object $spec -Name "default_variant" -Context "Asset set spec"
    $texturesValue = Get-JsonPropertyValue -Object $spec -Name "textures" -Context "Asset set spec"
    if ($texturesValue -isnot [System.Array]) {
        throw "Asset set spec textures must be an array"
    }

    $textures = @($texturesValue)
    if ($textures.Count -eq 0) {
        throw "Asset set spec textures array must not be empty"
    }

    $packageRootPath = Join-NormalizedRelativePath -BasePath $ResolvedRuntimeRoot -RelativePath $packageRoot
    if (-not (Test-PathUnderOrEqual -Path $packageRootPath -ParentPath $ResolvedRuntimeRoot)) {
        throw "Asset set package_root escapes RuntimeRoot: $packageRoot"
    }

    $textureSpecs = @()
    $seenKeys = @{}
    $seenPackagePaths = @{}

    for ($i = 0; $i -lt $textures.Count; ++$i) {
        $texture = $textures[$i]
        $context = "Asset set spec textures[$i]"

        $logicalPath = ConvertTo-AssetCookLogicalPath `
            -Path (Get-RequiredStringField -Object $texture -Name "logical_path" -Context $context) `
            -Name "$context logical_path"
        $sourcePathText = Get-RequiredStringField -Object $texture -Name "source_path" -Context $context
        $format = Get-RequiredStringField -Object $texture -Name "format" -Context $context
        $packageName = Normalize-ManifestPath `
            -Path (Get-RequiredStringField -Object $texture -Name "package_name" -Context $context) `
            -Name "$context package_name"
        $entryName = ConvertTo-AssetCookLogicalPath `
            -Path (Get-RequiredStringField -Object $texture -Name "entry_name" -Context $context) `
            -Name "$context entry_name"
        $variant = Get-OptionalStringField -Object $texture -Name "variant" -Context $context -DefaultValue $defaultVariant

        $sourcePath = Resolve-RepoRelativePath -Path $sourcePathText -Name "$context source_path"
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "$context source file not found: $sourcePath"
        }

        $cookedPackage = Join-ManifestRelativePath -LeftPath $packageRoot -RightPath $packageName
        $packagePath = Join-NormalizedRelativePath -BasePath $packageRootPath -RelativePath $packageName
        if (-not (Test-PathUnderOrEqual -Path $packagePath -ParentPath $ResolvedRuntimeRoot)) {
            throw "$context package output escapes RuntimeRoot: $packagePath"
        }
        if (-not (Test-PathUnderOrEqual -Path $packagePath -ParentPath $packageRootPath)) {
            throw "$context package output escapes package_root: $packagePath"
        }

        $key = "$logicalPath|texture|$variant"
        if ($seenKeys.ContainsKey($key)) {
            throw "Asset set spec contains duplicate logical_path|texture|variant key: $key"
        }
        $seenKeys[$key] = $true

        $packageKey = Normalize-PathForBoundary $packagePath
        if ($seenPackagePaths.ContainsKey($packageKey)) {
            throw "Asset set spec contains duplicate package output path: $packagePath"
        }
        $seenPackagePaths[$packageKey] = $true

        $textureSpecs += [pscustomobject]@{
            LogicalPath = $logicalPath
            SourcePath = $sourcePath
            Format = $format
            PackageName = $packageName
            EntryName = $entryName
            Variant = $variant
            CookedPackage = $cookedPackage
            PackagePath = $packagePath
        }
    }

    return [pscustomobject]@{
        Name = $name
        PackageRoot = $packageRoot
        PackageRootPath = $packageRootPath
        DefaultVariant = $defaultVariant
        Textures = @($textureSpecs)
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

$AssetSetSpec = Read-TextureAssetSetSpec -ResolvedSpecPath $ResolvedSpecPath -ResolvedRuntimeRoot $ResolvedRuntimeRoot

New-Item -ItemType Directory -Path $ResolvedRuntimeRoot -Force | Out-Null
New-Item -ItemType Directory -Path $AssetSetSpec.PackageRootPath -Force | Out-Null

$AggregateAssets = @()
foreach ($textureSpec in $AssetSetSpec.Textures) {
    if ((Test-Path -LiteralPath $textureSpec.PackagePath) -and
        -not (Test-Path -LiteralPath $textureSpec.PackagePath -PathType Leaf)) {
        throw "Package output must be a file path: $($textureSpec.PackagePath)"
    }

    $packageDirectory = Split-Path -Parent $textureSpec.PackagePath
    New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null

    Invoke-CheckedProcess `
        -ExePath $AssetCookPath `
        -WorkingDirectory $RepoRoot `
        -TimeoutSeconds $TimeoutSeconds `
        -Name "AssetCook" `
        -Arguments @(
            "--input", $textureSpec.SourcePath,
            "--out", $textureSpec.PackagePath,
            "--manifest", $ResolvedManifestPath,
            "--logical", $textureSpec.LogicalPath,
            "--kind", "texture",
            "--entry", $textureSpec.EntryName,
            "--entry-type", "Tex0",
            "--format", $textureSpec.Format,
            "--variant", $textureSpec.Variant
        )

    $singleManifest = Read-JsonFile `
        -Path $ResolvedManifestPath `
        -Name "AssetCook manifest for $($textureSpec.LogicalPath)"
    $singleAssets = @(Get-ManifestAssets `
        -Manifest $singleManifest `
        -ManifestName "AssetCook manifest for $($textureSpec.LogicalPath)")
    if ($singleAssets.Count -ne 1) {
        throw "AssetCook manifest for $($textureSpec.LogicalPath) must contain exactly 1 asset, got $($singleAssets.Count)"
    }

    Assert-SingleTextureManifestEntry `
        -Asset $singleAssets[0] `
        -TextureSpec $textureSpec `
        -Context "AssetCook manifest for $($textureSpec.LogicalPath) asset[0]"

    $AggregateAssets += $singleAssets[0]
}

Assert-AggregateManifest `
    -Assets $AggregateAssets `
    -ExpectedCount $AssetSetSpec.Textures.Count `
    -RuntimeRoot $ResolvedRuntimeRoot `
    -PackageParentPath $AssetSetSpec.PackageRootPath `
    -ManifestName "Aggregate manifest"

$AggregateManifest = [ordered]@{
    version = 1
    assets = @($AggregateAssets)
}
$AggregateJson = $AggregateManifest | ConvertTo-Json -Depth 16
[System.IO.File]::WriteAllText($ResolvedManifestPath, $AggregateJson, [System.Text.UTF8Encoding]::new($false))

Write-Host "Cooked texture asset set passed."
Write-Host "Spec: $ResolvedSpecPath"
Write-Host "Asset set: $($AssetSetSpec.Name)"
Write-Host "Runtime root: $ResolvedRuntimeRoot"
Write-Host "Manifest: $ResolvedManifestPath"
Write-Host "Assets: $($AssetSetSpec.Textures.Count)"
