param(
    [Parameter(Mandatory = $true)]
    [string]$AssetCookExe,

    [Parameter(Mandatory = $true)]
    [string]$GameExe,

    [int]$FrameCount = 30000,

    [int]$TimeoutSeconds = 180,

    [string]$SmokeDir = ".\build\CookedTextureGameSmoke\Debug",

    [string]$SpecPath = "Assets/AssetSets/Rendering3DTestSilverTextures.json",

    [string]$ModelPath = "",

    [ValidateSet("Direct", "GltfPrepared")]
    [string]$ExpectedLoadMode = "Direct",

    [string]$LogPath = ".\Game.log"
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

function Read-TextureLogPathsFromSpec {
    param([string]$ResolvedSpecPath)

    try {
        $spec = Get-Content -LiteralPath $ResolvedSpecPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Failed to read texture asset set spec: $ResolvedSpecPath`n$($_.Exception.Message)"
    }

    $version = Get-JsonPropertyValue -Object $spec -Name "version" -Context "Texture asset set spec"
    Assert-VersionOne -Value $version -Context "Texture asset set spec"

    $texturesValue = Get-JsonPropertyValue -Object $spec -Name "textures" -Context "Texture asset set spec"
    if ($texturesValue -isnot [System.Array]) {
        throw "Texture asset set spec textures must be an array"
    }

    $textures = @($texturesValue)
    if ($textures.Count -eq 0) {
        throw "Cooked texture Game smoke expects at least one texture spec"
    }

    $logPaths = @()
    $seenLogicalPaths = @{}
    for ($i = 0; $i -lt $textures.Count; ++$i) {
        $context = "Texture asset set spec textures[$i]"
        $requestPath = Normalize-ManifestPath `
            -Path (Get-JsonPropertyValue -Object $textures[$i] -Name "logical_path" -Context $context) `
            -Name "$context logical_path"
        $logicalPath = ConvertTo-AssetCookLogicalPath `
            -Path $requestPath `
            -Name "$context logical_path"
        if ($seenLogicalPaths.ContainsKey($logicalPath)) {
            throw "Texture asset set spec contains duplicate logical_path: $logicalPath"
        }
        $seenLogicalPaths[$logicalPath] = $true

        $sourcePath = Normalize-ManifestPath `
            -Path (Get-JsonPropertyValue -Object $textures[$i] -Name "source_path" -Context $context) `
            -Name "$context source_path"
        $resolvedSourcePath = Resolve-RepoRelativePath -Path $sourcePath -Name "$context source_path"
        if (-not (Test-Path -LiteralPath $resolvedSourcePath -PathType Leaf)) {
            throw "$context source_path not found: $resolvedSourcePath"
        }

        $usage = "standard"
        if (Test-JsonPropertyExists -Object $textures[$i] -Name "usage") {
            $usageValue = Get-JsonPropertyValue -Object $textures[$i] -Name "usage" -Context $context
            if (($usageValue -isnot [string]) -or [string]::IsNullOrWhiteSpace($usageValue)) {
                throw "$context usage must be a non-empty string when present"
            }
            $usage = $usageValue.Trim()
        }
        if (($usage -ne "standard") -and ($usage -ne "arm")) {
            throw "$context usage must be standard or arm: $usage"
        }

        $logPaths += [pscustomobject]@{
            RequestPath = $requestPath
            LogicalPath = $logicalPath
            SourcePath = $sourcePath
            ResolvedSourcePath = $resolvedSourcePath
            Usage = $usage
        }
    }

    return ,$logPaths
}

if ($TimeoutSeconds -le 0) {
    throw "TimeoutSeconds must be greater than zero"
}

$AssetCookPath = Assert-ExistingFile -Path $AssetCookExe -Name "AssetCook"
$GamePath = Assert-ExistingFile -Path $GameExe -Name "Game"
$ResolvedSpecPath = Assert-ExistingFile -Path $SpecPath -Name "Texture asset set spec"
$TextureLogPaths = Read-TextureLogPathsFromSpec -ResolvedSpecPath $ResolvedSpecPath
$GameModelPath = ""
if (-not [string]::IsNullOrWhiteSpace($ModelPath)) {
    $GameModelPath = Normalize-ManifestPath -Path $ModelPath -Name "ModelPath"
    [void](Assert-ExistingFile -Path $GameModelPath -Name "ModelPath")
}
if (($ExpectedLoadMode -eq "GltfPrepared") -and [string]::IsNullOrWhiteSpace($GameModelPath)) {
    throw "GltfPrepared smoke requires -ModelPath"
}
$CookTextureAssetSetScript = Assert-ExistingFile `
    -Path "Scripts/CookTextureAssetSet.ps1" `
    -Name "CookTextureAssetSet helper"

$ResolvedSmokeDir = Resolve-RepoRelativePath -Path $SmokeDir -Name "SmokeDir"
$ResolvedLogPath = Resolve-RepoRelativePath -Path $LogPath -Name "LogPath"

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
$ManifestPath = Join-Path $RuntimeRoot "manifest.json"

& $CookTextureAssetSetScript `
    -AssetCookExe $AssetCookPath `
    -SpecPath $ResolvedSpecPath `
    -RuntimeRoot $RuntimeRoot `
    -ManifestPath $ManifestPath `
    -TimeoutSeconds $TimeoutSeconds

if (Test-Path -LiteralPath $ResolvedLogPath) {
    Remove-Item -LiteralPath $ResolvedLogPath -Force
}

$GameArguments = @(
    "--exit-after-frames=$FrameCount",
    "--texture-asset-root", $RuntimeRoot,
    "--texture-asset-manifest", $ManifestPath
)
if (-not [string]::IsNullOrWhiteSpace($GameModelPath)) {
    $GameArguments += @("--rendering3dtest-model", $GameModelPath)
}

Invoke-CheckedProcess `
    -ExePath $GamePath `
    -WorkingDirectory $RepoRoot `
    -TimeoutSeconds $TimeoutSeconds `
    -Name "Game" `
    -Arguments $GameArguments

if (-not (Test-Path -LiteralPath $ResolvedLogPath -PathType Leaf)) {
    throw "Game log was not written: $ResolvedLogPath"
}

$LogLines = Get-Content -LiteralPath $ResolvedLogPath
$CookedSourceMatchCount = 0
$CookedUploadMatchCount = 0
$PreparedUploadMatchCount = 0
$PreparedFinalizeMatchCount = 0
$PreparedSplitMatchCount = 0

foreach ($textureLogPath in $TextureLogPaths) {
    $escapedRequestPath = [regex]::Escape($textureLogPath.RequestPath)
    $escapedLogicalPath = [regex]::Escape($textureLogPath.LogicalPath)

    if ($ExpectedLoadMode -eq "Direct") {
        $cookedSourceMatches = Require-LogMatch `
            -Lines $LogLines `
            -Pattern "stage=texture_asset_resolve (?=.*source=cooked_nvtex)(?=.*path=`"$escapedRequestPath`")(?=.*logical_path=`"$escapedLogicalPath`")(?=.*success=1)" `
            -Reason "Expected successful cooked nvtex source log for $($textureLogPath.RequestPath)"
        $CookedSourceMatchCount += $cookedSourceMatches.Count

        $cookedUploadMatches = Require-LogMatch `
            -Lines $LogLines `
            -Pattern "stage=texture_cooked_upload (?=.*source=cooked_nvtex)(?=.*path=`"$escapedRequestPath`")(?=.*logical_path=`"$escapedLogicalPath`")(?=.*success=1)" `
            -Reason "Expected successful cooked texture upload log for $($textureLogPath.RequestPath)"
        $CookedUploadMatchCount += $cookedUploadMatches.Count

        Forbid-LogMatch `
            -Lines $LogLines `
            -Pattern "(?=.*source=loose_stbi)(?=.*path=`"$escapedRequestPath`")" `
            -Reason "Unexpected loose_stbi source log for $($textureLogPath.RequestPath)"
        continue
    }

    $prepareMatches = Require-LogMatch `
        -Lines $LogLines `
        -Pattern "stage=texture_prepare_asset (?=.*path=`"$escapedRequestPath`")(?=.*logical_path=`"$escapedLogicalPath`")(?=.*status=CookedReady)" `
        -Reason "Expected CookedReady prepared texture log for $($textureLogPath.RequestPath)"
    $CookedSourceMatchCount += $prepareMatches.Count

    if ($textureLogPath.Usage -eq "arm") {
        $splitMatches = Require-LogMatch `
            -Lines $LogLines `
            -Pattern "stage=texture_prepared_split (?=.*path=`"$escapedRequestPath`")(?=.*logical_path=`"$escapedLogicalPath`")(?=.*success=1)" `
            -Reason "Expected prepared ARM split log for $($textureLogPath.RequestPath)"
        $PreparedSplitMatchCount += $splitMatches.Count
    }
    else {
        $preparedUploadMatches = Require-LogMatch `
            -Lines $LogLines `
            -Pattern "stage=texture_prepared_cooked_upload (?=.*source=cooked_nvtex)(?=.*path=`"$escapedRequestPath`")(?=.*logical_path=`"$escapedLogicalPath`")(?=.*success=1)" `
            -Reason "Expected prepared cooked upload log for $($textureLogPath.RequestPath)"
        $PreparedUploadMatchCount += $preparedUploadMatches.Count

        $preparedFinalizeMatches = Require-LogMatch `
            -Lines $LogLines `
            -Pattern "stage=texture_prepared_finalize (?=.*source=cooked_nvtex)(?=.*path=`"$escapedRequestPath`")(?=.*success=1)" `
            -Reason "Expected prepared finalize log for $($textureLogPath.RequestPath)"
        $PreparedFinalizeMatchCount += $preparedFinalizeMatches.Count
    }

    $sourcePathVariants = @(
        $textureLogPath.ResolvedSourcePath,
        ($textureLogPath.ResolvedSourcePath -replace '\\', '/')
    ) | Select-Object -Unique
    foreach ($sourcePathVariant in $sourcePathVariants) {
        $escapedSourcePath = [regex]::Escape($sourcePathVariant)
        Forbid-LogMatch `
            -Lines $LogLines `
            -Pattern "stage=gltf_image_(read|decode) (?=.*path=`"$escapedSourcePath`")" `
            -Reason "Unexpected glTF image read/decode for cooked-ready source path $sourcePathVariant"
    }

    Forbid-LogMatch `
        -Lines $LogLines `
        -Pattern "(?=.*source=loose_stbi)(?=.*path=`"$escapedRequestPath`")" `
        -Reason "Unexpected loose_stbi source log for $($textureLogPath.RequestPath)"
}

if ($ExpectedLoadMode -eq "Direct") {
    Require-LogMatch `
        -Lines $LogLines `
        -Pattern "Silver PBR material textures loaded" `
        -Reason "Expected Silver PBR material textures loaded log"
}
else {
    Require-LogMatch `
        -Lines $LogLines `
        -Pattern "Boulder model loaded and added to World" `
        -Reason "Expected glTF model loaded log"
}

Write-Host "Cooked texture Game smoke passed."
Write-Host "Runtime root: $RuntimeRoot"
Write-Host "Manifest: $ManifestPath"
Write-Host "Log: $ResolvedLogPath"
Write-Host "Cooked source logs: $CookedSourceMatchCount"
Write-Host "Cooked upload logs: $CookedUploadMatchCount"
Write-Host "Prepared upload logs: $PreparedUploadMatchCount"
Write-Host "Prepared finalize logs: $PreparedFinalizeMatchCount"
Write-Host "Prepared split logs: $PreparedSplitMatchCount"
