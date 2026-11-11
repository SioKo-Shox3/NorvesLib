# 撮影・地形の cook・地図の合成・地図の cook を順に実行する。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Config,
    [string]$GameExe = "build/Game/Debug/Game.exe",
    [string]$AssetCookExe = "build/Tools/AssetCook/Debug/AssetCook.exe",
    [string]$Python = "python",
    [switch]$SkipCapture,
    [switch]$Force
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))

function Get-RequiredString {
    param($Object, [string]$Name, [string]$Context)
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $property.Value -isnot [string] -or
        [string]::IsNullOrWhiteSpace($property.Value)) {
        throw "$Context の $Name には空でない文字列を指定してください。"
    }
    return $property.Value
}

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Resolve-Executable {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path) -or $Path.Contains('/') -or $Path.Contains('\')) {
        $resolved = Resolve-RepoPath $Path
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "実行ファイルが見つかりません: $resolved"
        }
        return $resolved
    }
    $command = Get-Command -Name $Path -CommandType Application -ErrorAction Stop | Select-Object -First 1
    return $command.Source
}

function Read-Json {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "ファイルが見つかりません: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-Json {
    param([string]$Path, $Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 40
    [System.IO.File]::WriteAllText($Path, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
}

function Get-FileDigest {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "入力または出力ファイルが見つかりません: $Path"
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

function Get-TextDigest {
    param([string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Add-FileInput {
    param([System.Collections.ArrayList]$Inputs, [string]$Name, [string]$Path)
    [void]$Inputs.Add([ordered]@{ name = $Name; path = $Path; sha256 = (Get-FileDigest $Path) })
}

function Get-InputStamp {
    param([System.Collections.ArrayList]$Inputs)
    return Get-TextDigest (ConvertTo-Json -InputObject @($Inputs.ToArray()) -Depth 40 -Compress)
}

function Test-Stamp {
    param([string]$Path, [string]$InputStamp, [string[]]$Outputs)
    if ($Force -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    try {
        $state = Read-Json $Path
        if ($state.input_sha256 -ne $InputStamp -or @($state.outputs).Count -ne $Outputs.Count) {
            return $false
        }
        for ($index = 0; $index -lt $Outputs.Count; $index++) {
            if ($state.outputs[$index].path -ne $Outputs[$index] -or
                $state.outputs[$index].sha256 -ne (Get-FileDigest $Outputs[$index])) {
                return $false
            }
        }
        return $true
    }
    catch {
        return $false
    }
}

function Save-Stamp {
    param([string]$Path, [string]$InputStamp, [string[]]$Outputs)
    $records = @(
        foreach ($outputPath in $Outputs) {
            [ordered]@{ path = $outputPath; sha256 = (Get-FileDigest $outputPath) }
        }
    )
    Write-Json $Path ([ordered]@{ input_sha256 = $InputStamp; outputs = $records })
}

function ConvertTo-RelativePath {
    param([string]$Path, [string]$Context)
    $normalized = $Path.Replace('\', '/')
    if ([System.IO.Path]::IsPathRooted($normalized) -or $normalized.Contains(':')) {
        throw "$Context は相対パスにしてください: $Path"
    }
    $parts = @($normalized.Split('/'))
    if (@($parts | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        throw "$Context に空の要素、.、.. は使えません: $Path"
    }
    return $parts -join '/'
}

function ConvertTo-LogicalPath {
    param([string]$Path, [string]$Context)
    $relative = ConvertTo-RelativePath $Path $Context
    if ($relative.StartsWith('Assets/', [System.StringComparison]::Ordinal)) {
        $relative = $relative.Substring(7)
    }
    if ([string]::IsNullOrWhiteSpace($relative)) {
        throw "$Context が空です。"
    }
    return $relative
}

function Get-CaptureFiles {
    param([string]$ManifestPath, [string]$TilesDir)
    $manifest = Read-Json $ManifestPath
    if ($manifest.format -ne 'norves.map_capture_manifest' -or $manifest.version -ne 1 -or
        $manifest.tiles -isnot [System.Array] -or $manifest.tiles.Count -eq 0) {
        throw "撮影目録の形式またはタイルの一覧が不正です: $ManifestPath"
    }
    $files = @($ManifestPath)
    foreach ($tile in $manifest.tiles) {
        $name = ConvertTo-RelativePath (Get-RequiredString $tile 'file' '撮影目録のタイル') 'タイルの file'
        $files += [System.IO.Path]::GetFullPath((Join-Path $TilesDir $name))
    }
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "撮影の出力が見つかりません: $file"
        }
    }
    return $files
}

function Invoke-External {
    param([string]$Executable, [string[]]$Arguments, [string]$Description)
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Description が失敗しました（終了コード: $LASTEXITCODE）。"
    }
}

try {
    $configPath = Resolve-RepoPath $Config
    $settings = Read-Json $configPath
    $region = Get-RequiredString $settings 'region' '設定'
    $scene = Get-RequiredString $settings 'scene_id' '設定'
    if ($scene -notmatch '^[A-Za-z0-9_-]+$') {
        throw 'scene_id には英数字、ハイフン、アンダースコアを指定してください。'
    }
    $terrainPath = Resolve-RepoPath (Get-RequiredString $settings 'terrain' '設定')
    $captureSpec = Resolve-RepoPath (Get-RequiredString $settings 'capture_spec' '設定')
    $assetSetPath = Resolve-RepoPath (Get-RequiredString $settings 'asset_set' '設定')
    $captureDir = Resolve-RepoPath (Get-RequiredString $settings 'capture_dir' '設定')
    $composeDir = Resolve-RepoPath (Get-RequiredString $settings 'compose_dir' '設定')
    $cookDir = Resolve-RepoPath (Get-RequiredString $settings 'cook_dir' '設定')
    $optionsProperty = $settings.PSObject.Properties['compose_options']
    if ($null -eq $optionsProperty -or $null -eq $optionsProperty.Value -or
        $optionsProperty.Value -isnot [pscustomobject]) {
        throw '設定の compose_options にはオブジェクトを指定してください。'
    }
    $options = $optionsProperty.Value
    $tilesDir = $captureDir
    $tilesProperty = $options.PSObject.Properties['tiles_dir']
    if ($null -ne $tilesProperty -and $null -ne $tilesProperty.Value) {
        $tilesDir = Resolve-RepoPath (Get-RequiredString $options 'tiles_dir' 'compose_options')
        $tilesProperty.Value = $tilesDir
    }
    $optionsJson = ConvertTo-Json -InputObject $options -Depth 40 -Compress
    $assetCook = Resolve-Executable $AssetCookExe
    $pythonExe = Resolve-Executable $Python
    $composeScript = Join-Path $PSScriptRoot 'compose_map.py'
    $readerScript = Join-Path $PSScriptRoot 'nvtrn_reader.py'
    $captureManifest = Join-Path $captureDir 'capture_manifest.json'

    Push-Location $RepoRoot
    try {
        if ($SkipCapture) {
            Write-Host '撮影: -SkipCapture の指定により飛ばします。'
        }
        else {
            $game = Resolve-Executable $GameExe
            $captureInputs = New-Object System.Collections.ArrayList
            [void]$captureInputs.Add([ordered]@{ scene_id = $scene })
            Add-FileInput $captureInputs 'capture_spec' $captureSpec
            Add-FileInput $captureInputs 'game' $game
            $captureStamp = Get-InputStamp $captureInputs
            $captureStampPath = Join-Path $captureDir '.map-build-capture.json'
            $captureOutputs = @()
            try { $captureOutputs = @(Get-CaptureFiles $captureManifest $captureDir) } catch { }
            if ($captureOutputs.Count -gt 0 -and (Test-Stamp $captureStampPath $captureStamp $captureOutputs)) {
                Write-Host '撮影: 入力が同じなので飛ばします。'
            }
            else {
                Write-Host "撮影: 実行します（地域: $region）。"
                New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
                $backup = $null
                if (Test-Path -LiteralPath $captureManifest -PathType Leaf) {
                    $backup = Join-Path $captureDir ([System.IO.Path]::GetRandomFileName())
                    Move-Item -LiteralPath $captureManifest -Destination $backup
                }
                try {
                    $gameArguments = @("--scene=$scene", "--map-capture=`"$captureSpec`"", '--window-background')
                    $process = Start-Process -FilePath $game -ArgumentList $gameArguments -WorkingDirectory $RepoRoot -PassThru -Wait
                    if ($process.ExitCode -ne 0) {
                        throw "撮影が失敗しました（終了コード: $($process.ExitCode)）。"
                    }
                    $captureOutputs = @(Get-CaptureFiles $captureManifest $captureDir)
                    Save-Stamp $captureStampPath $captureStamp $captureOutputs
                    if ($null -ne $backup) { Remove-Item -LiteralPath $backup -Force }
                }
                catch {
                    if ($null -ne $backup) {
                        if (Test-Path -LiteralPath $captureManifest -PathType Leaf) {
                            Remove-Item -LiteralPath $captureManifest -Force
                        }
                        Move-Item -LiteralPath $backup -Destination $captureManifest
                    }
                    throw
                }
            }
        }

        $captureFiles = @(Get-CaptureFiles $captureManifest $tilesDir)
        $composeInputs = New-Object System.Collections.ArrayList
        [void]$composeInputs.Add([ordered]@{ options = $optionsJson })
        foreach ($file in $captureFiles) { Add-FileInput $composeInputs 'capture' $file }
        Add-FileInput $composeInputs 'terrain' $terrainPath
        Add-FileInput $composeInputs 'compose_script' $composeScript
        Add-FileInput $composeInputs 'terrain_reader' $readerScript
        $terrainExtension = [System.IO.Path]::GetExtension($terrainPath).ToLowerInvariant()
        if ($terrainExtension -eq '.json') {
            $terrainSpec = Read-Json $terrainPath
            $heightPath = Get-RequiredString $terrainSpec.height_source 'path' '地形スペックの height_source'
            if (-not [System.IO.Path]::IsPathRooted($heightPath)) {
                $heightPath = Join-Path (Split-Path -Parent $terrainPath) $heightPath
            }
            Add-FileInput $composeInputs 'terrain_height' ([System.IO.Path]::GetFullPath($heightPath))
            Add-FileInput $composeInputs 'terrain_cooker' $assetCook
        }
        elseif ($terrainExtension -ne '.nvtrn' -and $terrainExtension -ne '.nvpkg') {
            throw "terrain は地形スペックの JSON、NVTRN または NVPKG にしてください: $terrainPath"
        }
        $composeStamp = Get-InputStamp $composeInputs
        $composeStampPath = Join-Path $composeDir '.map-build-compose.json'
        $composeOutputs = @('map_full.png', 'map_2048.png', 'height_r8.png', 'water_r8.png',
            'road_r8.png', 'map_transform.json', 'compose_report.json') | ForEach-Object { Join-Path $composeDir $_ }
        if (Test-Stamp $composeStampPath $composeStamp $composeOutputs) {
            Write-Host '合成: 入力が同じなので飛ばします。'
        }
        else {
            Write-Host '合成: 実行します。'
            New-Item -ItemType Directory -Path $composeDir -Force | Out-Null
            $terrainInput = $terrainPath
            if ($terrainExtension -eq '.json') {
                $terrainDir = Join-Path $composeDir '.terrain'
                New-Item -ItemType Directory -Path $terrainDir -Force | Out-Null
                $terrainInput = Join-Path $terrainDir 'terrain.nvpkg'
                $terrainManifest = Join-Path $terrainDir 'manifest.json'
                Invoke-External $assetCook @('--input', $terrainPath, '--entry-type', 'Trn0',
                    '--format', 'nvtrn.v0.height16', '--out', $terrainInput, '--manifest', $terrainManifest,
                    '--logical', 'Terrain/map.nvtrn', '--kind', 'world', '--entry', 'Terrain/map.nvtrn',
                    '--variant', 'default') '地形の cook'
            }
            $optionsPath = [System.IO.Path]::GetTempFileName()
            $previousPythonEncoding = [System.Environment]::GetEnvironmentVariable('PYTHONIOENCODING', 'Process')
            try {
                [System.IO.File]::WriteAllText($optionsPath, $optionsJson, (New-Object System.Text.UTF8Encoding($false)))
                # PowerShell が外部コマンドの出力を読む文字コードと Python の出力を合わせる。
                $env:PYTHONIOENCODING = "$([System.Console]::OutputEncoding.WebName):backslashreplace"
                Invoke-External $pythonExe @($composeScript, '--manifest', $captureManifest, '--terrain', $terrainInput,
                    '--out', $composeDir, '--tiles-dir', $tilesDir, '--options', $optionsPath) '地図の合成'
            }
            finally {
                [System.Environment]::SetEnvironmentVariable('PYTHONIOENCODING', $previousPythonEncoding, 'Process')
                Remove-Item -LiteralPath $optionsPath -Force -ErrorAction SilentlyContinue
            }
            Save-Stamp $composeStampPath $composeStamp $composeOutputs
        }

        $assetSet = Read-Json $assetSetPath
        $packageRoot = ConvertTo-RelativePath (Get-RequiredString $assetSet 'package_root' '一覧') 'package_root'
        $defaultVariant = Get-RequiredString $assetSet 'default_variant' '一覧'
        if ($assetSet.textures -isnot [System.Array] -or $assetSet.raws -isnot [System.Array]) {
            throw '一覧の textures と raws は配列にしてください。'
        }
        $items = New-Object System.Collections.ArrayList
        $cookInputs = New-Object System.Collections.ArrayList
        Add-FileInput $cookInputs 'asset_set' $assetSetPath
        Add-FileInput $cookInputs 'asset_cooker' $assetCook
        $seenPackages = @{}
        $seenKeys = @{}
        foreach ($kind in @('texture', 'raw')) {
            $entries = if ($kind -eq 'texture') { $assetSet.textures } else { $assetSet.raws }
            foreach ($entry in $entries) {
                $logical = ConvertTo-LogicalPath (Get-RequiredString $entry 'logical_path' '一覧の項目') 'logical_path'
                $entryName = ConvertTo-LogicalPath (Get-RequiredString $entry 'entry_name' '一覧の項目') 'entry_name'
                $packageName = ConvertTo-RelativePath (Get-RequiredString $entry 'package_name' '一覧の項目') 'package_name'
                $source = Resolve-RepoPath (Get-RequiredString $entry 'source_path' '一覧の項目')
                $variant = $defaultVariant
                if ($null -ne $entry.PSObject.Properties['variant']) {
                    $variant = Get-RequiredString $entry 'variant' '一覧の項目'
                }
                $usage = ''
                if ($kind -eq 'texture') {
                    $usage = Get-RequiredString $entry 'cook_usage' '一覧のテクスチャ'
                    if ($usage -notin @('albedo', 'single')) {
                        throw "地図の cook_usage は albedo または single にしてください: $usage"
                    }
                }
                elseif ($entryName -ne '__raw__') {
                    throw 'raw の entry_name は __raw__ にしてください。'
                }
                $package = [System.IO.Path]::GetFullPath((Join-Path $cookDir "$packageRoot/$packageName"))
                $key = "$logical|$kind|$variant"
                if ($seenPackages.ContainsKey($package) -or $seenKeys.ContainsKey($key)) {
                    throw "出力パッケージまたは論理パスが重複しています: $logical"
                }
                $seenPackages[$package] = $true
                $seenKeys[$key] = $true
                Add-FileInput $cookInputs 'source' $source
                [void]$items.Add([ordered]@{ kind = $kind; logical = $logical; entry = $entryName;
                    package = $package; source = $source; variant = $variant; usage = $usage })
            }
        }
        if ($items.Count -eq 0) { throw '一覧に cook する項目がありません。' }
        $cookStamp = Get-InputStamp $cookInputs
        $cookStampPath = Join-Path $cookDir '.map-build-cook.json'
        $manifestPath = Join-Path $cookDir 'manifest.json'
        $cookOutputs = @($manifestPath) + @($items | ForEach-Object { $_.package })
        if (Test-Stamp $cookStampPath $cookStamp $cookOutputs) {
            Write-Host 'cook: 入力が同じなので飛ばします。'
        }
        else {
            Write-Host 'cook: 実行します。'
            New-Item -ItemType Directory -Path $cookDir -Force | Out-Null
            $itemManifestPath = Join-Path $cookDir '.map-build-item-manifest.json'
            $assets = New-Object System.Collections.ArrayList
            foreach ($item in $items) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $item.package) -Force | Out-Null
                if (Test-Path -LiteralPath $itemManifestPath -PathType Leaf) {
                    Remove-Item -LiteralPath $itemManifestPath -Force
                }
                $arguments = @('--input', $item.source)
                if ($item.kind -eq 'texture') {
                    $arguments += @('--entry-type', 'Tex0', '--usage', $item.usage)
                }
                else {
                    $arguments += @('--entry-type', 'Raw', '--format', 'raw.v0')
                }
                $arguments += @('--out', $item.package, '--manifest', $itemManifestPath, '--logical', $item.logical,
                    '--kind', $item.kind, '--entry', $item.entry, '--variant', $item.variant)
                Invoke-External $assetCook $arguments "地図の cook（$($item.logical)）"
                $single = Read-Json $itemManifestPath
                if (@($single.assets).Count -ne 1 -or $single.assets[0].logical_path -ne $item.logical -or
                    $single.assets[0].kind -ne $item.kind -or $single.assets[0].entry_name -ne $item.entry -or
                    $single.assets[0].variant -ne $item.variant) {
                    throw "AssetCook のマニフェストが一覧と食い違います: $($item.logical)"
                }
                [void]$assets.Add($single.assets[0])
            }
            Write-Json $manifestPath ([ordered]@{ version = 1; assets = @($assets.ToArray()) })
            Remove-Item -LiteralPath $itemManifestPath -Force
            Save-Stamp $cookStampPath $cookStamp $cookOutputs
        }
        Write-Host "地図の生成が完了しました: $manifestPath"
    }
    finally {
        Pop-Location
    }
    exit 0
}
catch {
    Write-Host "地図の生成に失敗しました: $($_.Exception.Message)"
    exit 1
}
