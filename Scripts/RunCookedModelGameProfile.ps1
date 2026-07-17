param(
    [Parameter(Mandatory = $true)]
    [string]$AssetCookExe,

    [Parameter(Mandatory = $true)]
    [string]$GameExe,

    [int]$RenderedFrameCount = 3,

    [int]$TimeoutSeconds = 600,

    [int]$PairCount = 3,

    [string]$OutputDir = ".\build\CookedModelGameProfile\Debug",

    [string]$SpecPath = "Assets/AssetSets/Rendering3DTestSilverGltfTextures.json",

    [string]$ModelPath = "Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$InvariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $PSScriptRoot 'CookedModelGameProfileContract.ps1')
$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "build/CookedModelGameProfile"))
$ModelLogicalPath = "Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf"
$ModelEntryName = "Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.nvmesh"
$ModelFormat = "nvmesh.v0.mesh3d.pnt.u32.clustered"
$ExpectedTextureLogicalPaths = @(
    "Models/Rendering3DTestSilverGltf/textures/silver_albedo.png",
    "Models/Rendering3DTestSilverGltf/textures/silver_normal-ogl.png",
    "Models/Rendering3DTestSilverGltf/textures/silver_arm.png"
)
$ExpectedDefaultTexturePaths = @(
    'Textures/Silver/silver_albedo.png',
    'Textures/Silver/silver_normal-ogl.png',
    'Textures/Silver/silver_metallic.png',
    'Textures/Silver/silver_roughness.png',
    'Textures/Silver/silver_ao.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_diff_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_nor_gl_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_rough_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_ao_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_disp_4k.png'
)

function Normalize-BoundaryPath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetPathRoot($fullPath)
    while ($fullPath.Length -gt $root.Length -and
           ($fullPath.EndsWith([string][System.IO.Path]::DirectorySeparatorChar) -or
            $fullPath.EndsWith([string][System.IO.Path]::AltDirectorySeparatorChar))) {
        $fullPath = $fullPath.Substring(0, $fullPath.Length - 1)
    }
    return $fullPath
}

function Test-PathEqual {
    param([string]$Left, [string]$Right)

    return [string]::Equals(
        (Normalize-BoundaryPath $Left),
        (Normalize-BoundaryPath $Right),
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-StrictChildPath {
    param([string]$Path, [string]$Parent)

    $normalizedPath = Normalize-BoundaryPath $Path
    $normalizedParent = Normalize-BoundaryPath $Parent
    if (Test-PathEqual $normalizedPath $normalizedParent) {
        return $false
    }
    $prefix = $normalizedParent + [System.IO.Path]::DirectorySeparatorChar
    return $normalizedPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Resolve-InputFile {
    param([string]$Path, [string]$Name)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Name must not be empty"
    }
    $resolved = if (Test-IsAbsolutePath $Path) {
        [System.IO.Path]::GetFullPath($Path)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
    }
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Name not found: $resolved"
    }
    return $resolved
}

function Normalize-RelativeManifestPath {
    param([string]$Path, [string]$Name)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Name must not be empty"
    }
    $slashPath = $Path.Trim() -replace '\\', '/'
    if ($slashPath.StartsWith('/') -or $slashPath -match '^[A-Za-z]:' -or $slashPath -match '^[\\/]{2}') {
        throw "$Name must be relative: $Path"
    }
    $segments = @($slashPath -split '/')
    foreach ($segment in $segments) {
        if ([string]::IsNullOrWhiteSpace($segment) -or $segment -eq '.' -or $segment -eq '..') {
            throw "$Name contains an invalid or traversal segment: $Path"
        }
    }
    return $segments -join '/'
}

function ConvertTo-LogicalPath {
    param([string]$Path, [string]$Name)

    $logical = Normalize-RelativeManifestPath $Path $Name
    if ($logical.StartsWith('Assets/', [System.StringComparison]::Ordinal)) {
        $logical = $logical.Substring(7)
    }
    if ([string]::IsNullOrWhiteSpace($logical)) {
        throw "$Name has no path below Assets"
    }
    return $logical
}

function Assert-MathematicalIntegerOne {
    param([object]$Value, [string]$Name)

    if ($null -eq $Value -or $Value -is [bool]) {
        throw "$Name must be mathematical integer 1"
    }
    $number = 0.0
    if (-not [double]::TryParse(
            [string]$Value,
            [System.Globalization.NumberStyles]::Float,
            $InvariantCulture,
            [ref]$number) -or
        [double]::IsNaN($number) -or
        [double]::IsInfinity($number) -or
        $number -ne 1.0 -or
        $number -ne [math]::Truncate($number)) {
        throw "$Name must be mathematical integer 1"
    }
}

function Get-RequiredProperty {
    param([object]$Object, [string]$Name, [string]$Context)

    if ($null -eq $Object -or $Object.PSObject.Properties.Name -notcontains $Name) {
        throw "$Context is missing required field: $Name"
    }
    return $Object.PSObject.Properties[$Name].Value
}

function Get-RequiredString {
    param([object]$Object, [string]$Name, [string]$Context)

    $value = Get-RequiredProperty $Object $Name $Context
    if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) {
        throw "$Context field $Name must be a non-empty string"
    }
    return $value
}

function Quote-ProcessArgument {
    param([string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

function Invoke-ToolProcess {
    param(
        [string]$ExePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$Timeout,
        [string]$Name
    )

    $stdoutPath = Join-Path $WorkingDirectory "$Name.stdout.txt"
    $stderrPath = Join-Path $WorkingDirectory "$Name.stderr.txt"
    $argumentLine = ($Arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join ' '
    $process = $null
    try {
        $process = Start-Process -FilePath $ExePath -ArgumentList $argumentLine `
            -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        $null = $process.Handle
        if (-not $process.WaitForExit($Timeout * 1000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
            throw "$Name timed out after $Timeout seconds"
        }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "$Name failed with exit code $($process.ExitCode)"
        }
    }
    finally {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
    }
}

function Assert-ManifestEntry {
    param([object]$Entry, [string]$RuntimeRoot, [hashtable]$SeenKeys, [string]$Context)

    $logicalPath = Normalize-RelativeManifestPath (Get-RequiredString $Entry 'logical_path' $Context) "$Context logical_path"
    $kind = Get-RequiredString $Entry 'kind' $Context
    $variant = Get-RequiredString $Entry 'variant' $Context
    $format = Get-RequiredString $Entry 'format' $Context
    $package = Normalize-RelativeManifestPath (Get-RequiredString $Entry 'cooked_package' $Context) "$Context cooked_package"
    $entryName = Normalize-RelativeManifestPath (Get-RequiredString $Entry 'entry_name' $Context) "$Context entry_name"
    [void](Get-RequiredString $Entry 'entry_type' $Context)
    [void](Get-RequiredString $Entry 'source_hash' $Context)
    [void](Get-RequiredString $Entry 'cooked_hash' $Context)

    $key = "$logicalPath|$kind|$variant"
    if ($SeenKeys.ContainsKey($key)) {
        throw "Manifest has duplicate logical_path|kind|variant: $key"
    }
    $SeenKeys[$key] = $true

    $packagePath = [System.IO.Path]::GetFullPath((Join-Path $RuntimeRoot ($package -replace '/', '\\')))
    if (-not (Test-StrictChildPath $packagePath $RuntimeRoot)) {
        throw "$Context cooked_package escapes or equals RuntimeRoot: $package"
    }
    if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
        throw "$Context cooked_package does not exist: $packagePath"
    }

    return [pscustomobject]@{
        LogicalPath = $logicalPath
        Kind = $kind
        Variant = $variant
        Format = $format
        Package = $package
        EntryName = $entryName
    }
}

function Read-AndValidateFixtureManifest {
    param([string]$ManifestPath, [string]$RuntimeRoot)

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    Assert-MathematicalIntegerOne (Get-RequiredProperty $manifest 'version' 'Runtime manifest') 'Runtime manifest version'
    $assetsValue = Get-RequiredProperty $manifest 'assets' 'Runtime manifest'
    if ($assetsValue -isnot [System.Array]) {
        throw 'Runtime manifest assets must be an array'
    }
    $assets = @($assetsValue)
    if ($assets.Count -ne 4) {
        throw "Runtime manifest must contain exactly 4 entries, got $($assets.Count)"
    }

    $seen = @{}
    $validated = @()
    for ($index = 0; $index -lt $assets.Count; ++$index) {
        $validated += Assert-ManifestEntry $assets[$index] $RuntimeRoot $seen "Runtime manifest assets[$index]"
    }

    $textureEntries = @($validated | Where-Object { $_.Kind -eq 'texture' })
    $modelEntries = @($validated | Where-Object { $_.Kind -eq 'model' })
    if ($textureEntries.Count -ne 3 -or $modelEntries.Count -ne 1) {
        throw 'Runtime manifest must contain exactly 3 textures and 1 model'
    }
    $actualTextures = @($textureEntries.LogicalPath | Sort-Object)
    $expectedTextures = @($ExpectedTextureLogicalPaths | Sort-Object)
    if (($actualTextures -join "`n") -ne ($expectedTextures -join "`n")) {
        throw "Runtime manifest texture logical paths do not match the fixture"
    }
    $model = $modelEntries[0]
    if ($model.LogicalPath -ne $ModelLogicalPath -or
        $model.EntryName -ne $ModelEntryName -or
        $model.Format -ne $ModelFormat -or
        $model.Variant -ne 'default') {
        throw 'Runtime manifest model fields do not match the cooked fixture contract'
    }
}

function Get-HashForText {
    param([string]$Text)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Invoke-GitText {
    param([string[]]$Arguments)

    $output = & git -C $RepoRoot @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed: $($output -join "`n")"
    }
    return ($output -join "`n")
}

function Get-Provenance {
    $status = Invoke-GitText @('status', '--porcelain=v1', '--untracked-files=all')
    $behaviorPaths = @('Game', 'Scripts', 'Test', 'Library')
    $behaviorTrackedDiff = Invoke-GitText @(
        'diff', '--binary', 'HEAD', '--',
        $behaviorPaths[0], $behaviorPaths[1], $behaviorPaths[2], $behaviorPaths[3])
    $untrackedText = Invoke-GitText @('ls-files', '--others', '--exclude-standard')
    $untrackedPaths = @()
    if (-not [string]::IsNullOrWhiteSpace($untrackedText)) {
        $untrackedPaths = @($untrackedText -split "`n" | Where-Object { $_ } | Sort-Object)
    }
    $untrackedParts = @()
    foreach ($path in $untrackedPaths) {
        $absolute = Join-Path $RepoRoot ($path -replace '/', '\\')
        $hash = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant()
        $untrackedParts += "$path=$hash"
    }
    $behaviorUntrackedParts = @($untrackedParts | Where-Object {
        $_ -match '^(Game|Scripts|Test|Library)/'
    })
    $behaviorTrackedDiffHash = Get-HashForText $behaviorTrackedDiff
    $behaviorDiffHashInput = @(
        "tracked_diff_sha256=$behaviorTrackedDiffHash",
        'untracked_files:',
        $behaviorUntrackedParts
    ) -join "`n"
    $gpu = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
        ForEach-Object { "$($_.Name) driver=$($_.DriverVersion)" } | Sort-Object)
    return [ordered]@{
        head = Invoke-GitText @('rev-parse', 'HEAD')
        tree = Invoke-GitText @('rev-parse', 'HEAD^{tree}')
        dirty = -not [string]::IsNullOrWhiteSpace($status)
        status = @($status -split "`n" | Where-Object { $_ })
        behavior_diff_paths = $behaviorPaths
        behavior_tracked_diff_sha256 = $behaviorTrackedDiffHash
        behavior_untracked = $behaviorUntrackedParts
        behavior_diff_sha256 = Get-HashForText $behaviorDiffHashInput
        untracked = $untrackedParts
        untracked_composite_sha256 = Get-HashForText ($untrackedParts -join "`n")
        game_exe_sha256 = (Get-FileHash -LiteralPath $ResolvedGamePath -Algorithm SHA256).Hash.ToLowerInvariant()
        asset_cook_exe_sha256 = (Get-FileHash -LiteralPath $ResolvedAssetCookPath -Algorithm SHA256).Hash.ToLowerInvariant()
        os = [Environment]::OSVersion.VersionString
        gpu = $gpu
    }
}

function Get-Median {
    param([double[]]$Values)

    $sorted = @($Values | Sort-Object)
    if ($sorted.Count % 2 -eq 1) {
        return [double]$sorted[[int][math]::Floor($sorted.Count / 2)]
    }
    return ([double]$sorted[$sorted.Count / 2 - 1] + [double]$sorted[$sorted.Count / 2]) / 2.0
}

function Invoke-GameProfileRun {
    param([string]$Mode, [int]$PairIndex, [int]$OrderIndex)

    $runName = ('pair-{0:D2}-{1:D2}-{2}' -f $PairIndex, $OrderIndex, $Mode.ToLowerInvariant())
    $runDir = Join-Path $ResolvedOutputDir $runName
    New-Item -ItemType Directory -Path $runDir | Out-Null
    $logPath = Join-Path $runDir 'Game.log'
    $stdoutPath = Join-Path $runDir 'stdout.txt'
    $stderrPath = Join-Path $runDir 'stderr.txt'
    if (Test-Path -LiteralPath $logPath) {
        throw "Stale Game.log exists before run: $logPath"
    }

    $arguments = @(
        '--render-thread=mt',
        '--wait-for-asset-settle',
        "--exit-after-rendered-frames=$RenderedFrameCount",
        '--texture-asset-root', $RuntimeRoot,
        '--texture-asset-manifest', $ManifestPath,
        '--rendering3dtest-model', $ModelRequestPath
    )
    if ($Mode -eq 'Cooked') {
        $arguments += '--rendering3dtest-use-cooked-model'
    }
    $argumentLine = ($arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join ' '
    $process = $null
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $process = Start-Process -FilePath $ResolvedGamePath -ArgumentList $argumentLine `
            -WorkingDirectory $runDir -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
            throw "Game $runName timed out after $TimeoutSeconds seconds"
        }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "Game $runName failed with exit code $($process.ExitCode)"
        }
    }
    finally {
        $stopwatch.Stop()
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
    }

    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Game $runName did not write Game.log"
    }
    $lines = @(Get-Content -LiteralPath $logPath)
    $runtimeLines = @($lines + @(Get-Content -LiteralPath $stdoutPath) + @(Get-Content -LiteralPath $stderrPath))
    Assert-ProfileRuntimeSignals -Lines $runtimeLines -RuntimeRoot $RuntimeRoot -ExpectedDefaultTexturePaths $ExpectedDefaultTexturePaths
    $combinedText = $runtimeLines -join "`n"
    if ($combinedText.Contains('[SKIP]')) {
        throw "Game $runName emitted [SKIP]"
    }

    $startMarker = "Boulder model async load started: $ModelRequestPath"
    $readyMarker = 'Boulder model loaded and added to World'
    $startMatches = @()
    $readyMatches = @()
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].Contains($startMarker)) {
            $startMatches += [pscustomobject]@{ Index = $index; Line = $lines[$index] }
        }
        if ($lines[$index].Contains($readyMarker)) {
            $readyMatches += [pscustomobject]@{ Index = $index; Line = $lines[$index] }
        }
    }
    if ($startMatches.Count -ne 1) {
        throw "Game $runName expected exactly one '$startMarker', got $($startMatches.Count)"
    }
    if ($readyMatches.Count -ne 1) {
        throw "Game $runName expected exactly one '$readyMarker', got $($readyMatches.Count)"
    }
    if ($readyMatches[0].Index -le $startMatches[0].Index) {
        throw "Game $runName ready marker did not follow its start marker"
    }

    $timestampPattern = '^\[(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]'
    $startTimestampMatch = [regex]::Match($startMatches[0].Line, $timestampPattern)
    $readyTimestampMatch = [regex]::Match($readyMatches[0].Line, $timestampPattern)
    if (-not $startTimestampMatch.Success -or -not $readyTimestampMatch.Success) {
        throw "Game $runName marker timestamps must use [yyyy-MM-dd HH:mm:ss.fff]"
    }
    $startTimestamp = [DateTime]::ParseExact(
        $startTimestampMatch.Groups['timestamp'].Value,
        'yyyy-MM-dd HH:mm:ss.fff',
        $InvariantCulture)
    $readyTimestamp = [DateTime]::ParseExact(
        $readyTimestampMatch.Groups['timestamp'].Value,
        'yyyy-MM-dd HH:mm:ss.fff',
        $InvariantCulture)
    $readyMs = ($readyTimestamp - $startTimestamp).TotalMilliseconds
    if ($readyMs -lt 0) {
        throw "Game $runName produced negative model-ready latency"
    }

    $summaryPath = Join-Path $runDir 'AssetLoadProfileSummary.md'
    $summaryParameters = @{ LogPath = $logPath }
    if ($Mode -eq 'Cooked') {
        $summaryParameters.RequireCompleteCookedModel = $true
        $summaryParameters.ExpectedCookedModelLogicalPath = $ModelLogicalPath
        $summaryParameters.ExpectedCookedModelDebugName = $ModelRequestPath
    }
    else {
        $summaryParameters.RequireCompleteModelFlush = $true
    }
    $summaryOutput = & $SummaryScript @summaryParameters 2>&1
    $summarySucceeded = $?
    [System.IO.File]::WriteAllLines($summaryPath, @($summaryOutput), [System.Text.UTF8Encoding]::new($false))
    if (-not $summarySucceeded) {
        throw "Game $runName profile contract failed"
    }
    if ($Mode -eq 'Cooked' -and (($summaryOutput -join "`n") -notmatch 'Cooked model contract: PASS')) {
        throw "Game $runName cooked summary did not emit its PASS marker"
    }
    $allPrepareLines = @($lines | Where-Object { $_ -match 'stage=texture_prepare_asset ' })
    if ($allPrepareLines.Count -ne $ExpectedTextureLogicalPaths.Count) {
        throw "Game $runName expected exact texture_prepare_asset set of $($ExpectedTextureLogicalPaths.Count) paths, got $($allPrepareLines.Count)"
    }
    foreach ($expectedLogicalPath in $ExpectedTextureLogicalPaths) {
        $expectedRequestPath = if ($Mode -eq 'Loose') {
            "Assets/$expectedLogicalPath"
        }
        else {
            $expectedLogicalPath
        }
        $escapedRequestPath = [regex]::Escape($expectedRequestPath)
        $escapedLogicalPath = [regex]::Escape($expectedLogicalPath)
        $matches = @($allPrepareLines | Where-Object {
            $_ -match "(?=.*path=`"$escapedRequestPath`")(?=.*logical_path=`"$escapedLogicalPath`")(?=.*status=CookedReady)(?=.*source=cooked_nvtex)"
        })
        if ($matches.Count -ne 1) {
            throw "Game $runName expected exactly one CookedReady texture_prepare_asset for path=$expectedRequestPath logical_path=$expectedLogicalPath, got $($matches.Count)"
        }
    }

    return [pscustomobject]@{
        pair = $PairIndex
        order = $OrderIndex
        mode = $Mode.ToLowerInvariant()
        ready_ms = [math]::Round($readyMs, 3)
        wall_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
        run_dir = $runDir
        log = $logPath
        stdout = $stdoutPath
        stderr = $stderrPath
        summary = $summaryPath
        started_at = $startTimestamp.ToString('yyyy-MM-dd HH:mm:ss.fff', $InvariantCulture)
        ready_at = $readyTimestamp.ToString('yyyy-MM-dd HH:mm:ss.fff', $InvariantCulture)
    }
}

if ($RenderedFrameCount -le 0) { throw 'RenderedFrameCount must be greater than zero' }
if ($TimeoutSeconds -le 0) { throw 'TimeoutSeconds must be greater than zero' }
if ($PairCount -le 0) { throw 'PairCount must be greater than zero' }

$ResolvedAssetCookPath = Resolve-InputFile $AssetCookExe 'AssetCookExe'
$ResolvedGamePath = Resolve-InputFile $GameExe 'GameExe'
$ResolvedSpecPath = Resolve-InputFile $SpecPath 'SpecPath'
$ResolvedModelPath = Resolve-InputFile $ModelPath 'ModelPath'
$CookTextureScript = Resolve-InputFile 'Scripts/CookTextureAssetSet.ps1' 'CookTextureAssetSet'
$SummaryScript = Resolve-InputFile 'Scripts/SummarizeAssetLoadProfile.ps1' 'SummarizeAssetLoadProfile'
$ResolvedOutputDir = if (Test-IsAbsolutePath $OutputDir) {
    [System.IO.Path]::GetFullPath($OutputDir)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))
}
if (-not (Test-StrictChildPath $ResolvedOutputDir $ProfileRoot)) {
    throw "OutputDir must be a strict child of $ProfileRoot"
}

# All cleanup happens only after the output boundary has been proven safe.
if (Test-Path -LiteralPath $ResolvedOutputDir) {
    Remove-Item -LiteralPath $ResolvedOutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ResolvedOutputDir | Out-Null
$FixtureDir = Join-Path $ResolvedOutputDir 'fixture'
$RuntimeRoot = Join-Path $FixtureDir 'RuntimeRoot'
$ManifestPath = Join-Path $RuntimeRoot 'manifest.json'
$ModelFragmentPath = Join-Path $RuntimeRoot 'model.fragment.json'
$ModelPackagePath = Join-Path $RuntimeRoot 'Cooked/SilverGltf/Rendering3DTestSilverGltf.nvpkg'
New-Item -ItemType Directory -Path $FixtureDir | Out-Null

# Validate the source spec and its three exact model-local logical paths before cooking.
$spec = Get-Content -LiteralPath $ResolvedSpecPath -Raw | ConvertFrom-Json
Assert-MathematicalIntegerOne (Get-RequiredProperty $spec 'version' 'Texture fixture spec') 'Texture fixture spec version'
$specTexturesValue = Get-RequiredProperty $spec 'textures' 'Texture fixture spec'
if ($specTexturesValue -isnot [System.Array] -or @($specTexturesValue).Count -ne 3) {
    throw 'Texture fixture spec must contain exactly 3 textures'
}
$specLogicalPaths = @()
foreach ($texture in @($specTexturesValue)) {
    $specLogicalPaths += ConvertTo-LogicalPath (Get-RequiredString $texture 'logical_path' 'Texture fixture spec texture') 'logical_path'
}
if ((@($specLogicalPaths | Sort-Object) -join "`n") -ne
    (@($ExpectedTextureLogicalPaths | Sort-Object) -join "`n")) {
    throw 'Texture fixture spec logical paths do not match the expected three model references'
}

$gltf = Get-Content -LiteralPath $ResolvedModelPath -Raw | ConvertFrom-Json
$imageUris = @($gltf.images | ForEach-Object { Get-RequiredString $_ 'uri' 'glTF image' })
if ($imageUris.Count -ne 3) {
    throw 'Rendering3DTest Silver glTF must contain exactly 3 image URIs'
}
$modelDirectoryLogical = 'Models/Rendering3DTestSilverGltf'
$gltfLogicalPaths = @($imageUris | ForEach-Object {
    Normalize-RelativeManifestPath "$modelDirectoryLogical/$_" 'glTF image logical path'
} | Sort-Object)
if (($gltfLogicalPaths -join "`n") -ne (@($ExpectedTextureLogicalPaths | Sort-Object) -join "`n")) {
    throw 'glTF image URIs do not match the exact fixture logical texture set'
}

& $CookTextureScript -AssetCookExe $ResolvedAssetCookPath -SpecPath $ResolvedSpecPath `
    -RuntimeRoot $RuntimeRoot -ManifestPath $ManifestPath -TimeoutSeconds $TimeoutSeconds
$textureManifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json

New-Item -ItemType Directory -Path (Split-Path -Parent $ModelPackagePath) -Force | Out-Null
Invoke-ToolProcess -ExePath $ResolvedAssetCookPath -WorkingDirectory $FixtureDir `
    -Timeout $TimeoutSeconds -Name 'AssetCookModel' -Arguments @(
        '--input', $ResolvedModelPath,
        '--out', $ModelPackagePath,
        '--manifest', $ModelFragmentPath,
        '--logical', $ModelLogicalPath,
        '--kind', 'model',
        '--entry', $ModelEntryName,
        '--entry-type', 'Msh0',
        '--format', $ModelFormat,
        '--variant', 'default'
    )
$modelManifest = Get-Content -LiteralPath $ModelFragmentPath -Raw | ConvertFrom-Json
Assert-MathematicalIntegerOne (Get-RequiredProperty $modelManifest 'version' 'Model fragment') 'Model fragment version'
$modelAssets = @(Get-RequiredProperty $modelManifest 'assets' 'Model fragment')
if ($modelAssets.Count -ne 1) {
    throw "Model fragment must contain exactly 1 asset, got $($modelAssets.Count)"
}

$aggregateManifest = [ordered]@{
    version = 1
    assets = @(@($textureManifest.assets) + @($modelAssets[0]))
}
[System.IO.File]::WriteAllText(
    $ManifestPath,
    ($aggregateManifest | ConvertTo-Json -Depth 16),
    [System.Text.UTF8Encoding]::new($false))
Read-AndValidateFixtureManifest $ManifestPath $RuntimeRoot

$ModelRequestPath = 'Assets/' + $ModelLogicalPath
$orders = @()
$results = @()
for ($pair = 1; $pair -le $PairCount; ++$pair) {
    $order = if ($pair % 2 -eq 1) { @('Loose', 'Cooked') } else { @('Cooked', 'Loose') }
    $orders += "pair-$('{0:D2}' -f $pair):$($order[0].ToLowerInvariant()),$($order[1].ToLowerInvariant())"
    for ($orderIndex = 0; $orderIndex -lt $order.Count; ++$orderIndex) {
        $result = Invoke-GameProfileRun $order[$orderIndex] $pair ($orderIndex + 1)
        $results += $result
        Write-Host ("{0}: ready_ms={1:F3} wall_ms={2:F3}" -f
            $result.mode, $result.ready_ms, $result.wall_ms)
    }
}

$looseReady = [double[]]@($results | Where-Object { $_.mode -eq 'loose' } | ForEach-Object { $_.ready_ms })
$cookedReady = [double[]]@($results | Where-Object { $_.mode -eq 'cooked' } | ForEach-Object { $_.ready_ms })
$looseMedian = Get-Median $looseReady
$cookedMedian = Get-Median $cookedReady
$provenance = Get-Provenance
$comparison = [ordered]@{
    generated_at = [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss.fff', $InvariantCulture)
    config = [ordered]@{
        rendered_frame_count = $RenderedFrameCount
        frame_metric = 'render_thread_frames_rendered_after_asset_settle'
        timeout_seconds = $TimeoutSeconds
        pair_count = $PairCount
        configuration = Split-Path -Leaf $ResolvedOutputDir
        orders = $orders
    }
    paths = [ordered]@{
        repository = $RepoRoot
        asset_cook = $ResolvedAssetCookPath
        game = $ResolvedGamePath
        spec = $ResolvedSpecPath
        model_source = $ResolvedModelPath
        model_request = $ModelRequestPath
        runtime_root = $RuntimeRoot
        manifest = $ManifestPath
        output = $ResolvedOutputDir
    }
    provenance = $provenance
    samples = $results
    medians = [ordered]@{
        loose_ready_ms = [math]::Round($looseMedian, 3)
        cooked_ready_ms = [math]::Round($cookedMedian, 3)
        cooked_shorter = $cookedMedian -lt $looseMedian
    }
}
$comparisonPath = Join-Path $ResolvedOutputDir 'comparison.json'
[System.IO.File]::WriteAllText(
    $comparisonPath,
    ($comparison | ConvertTo-Json -Depth 16),
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Loose ready samples (ms): $($looseReady -join ', ')"
Write-Host "Cooked ready samples (ms): $($cookedReady -join ', ')"
Write-Host ("Loose median ready_ms: {0:F3}" -f $looseMedian)
Write-Host ("Cooked median ready_ms: {0:F3}" -f $cookedMedian)
Write-Host "Comparison metadata: $comparisonPath"
if ($cookedMedian -ge $looseMedian) {
    throw ("Cooked median model-ready latency was not shorter: cooked={0:F3}ms loose={1:F3}ms. " +
           "The measured artifacts were retained; do not claim Phase 5 complete.") -f $cookedMedian, $looseMedian
}

Write-Host '[PASS] Cooked model Game profile completed without skips.'
