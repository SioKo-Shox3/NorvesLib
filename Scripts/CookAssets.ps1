# Assets/AssetSets/ の一覧（"cook_assets": true のもの）から、テクスチャを用途別に NVTEX（BC7・BC5・BC4・R16）へ焼いて
# build/CookedAssets/ へ書く。CMake の対象 CookAssets から呼ぶ。
#
# 差分クック: 元画像の内容・一覧の項目・AssetCook の実行ファイルのどれかが変わったものだけを焼き、変わらないものは
# 前回の結果（<RuntimeRoot>/.cookstate/ に項目ごとの印とマニフェスト項目を残す）をそのまま使う。
# 元画像が無い項目は飛ばし、"COOK_ASSETS missing=<path>" を出す（クックの失敗にはしない）。
#
# 出力: <RuntimeRoot>/manifest.json（全項目を集めたマニフェスト）と、一覧の package_root 以下の .nvpkg。
# 標準出力の最後の行: COOK_ASSETS cooked=<焼いた数> skipped=<前回のまま使った数>
# 終了コード: クックまで通れば0、一覧の誤り・AssetCook の失敗は1。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AssetCookExe,

    # 一覧（*.json）を探すディレクトリ
    [string]$SpecDir = "Assets/AssetSets",

    # 焼いた NVTEX とマニフェストの出力先
    [string]$RuntimeRoot = "build/CookedAssets",

    # BC7 の品質（fast|normal|best）。空なら AssetCook の既定
    [string]$Quality = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$ValidUsages = @("albedo", "normal", "orm", "single", "height16")
$OrmSourceKeys = @("orm_ao", "orm_roughness", "orm_metallic")
$OrmArguments = @{ orm_ao = "--orm-ao"; orm_roughness = "--orm-roughness"; orm_metallic = "--orm-metallic" }

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Test-PropertyExists {
    param([object]$Object, [string]$Name)

    return ($null -ne $Object) -and ($Object.PSObject.Properties.Name -contains $Name)
}

function Get-StringField {
    param([object]$Object, [string]$Name, [string]$Context)

    if (-not (Test-PropertyExists -Object $Object -Name $Name)) {
        throw "$Context に必須の項目がありません: $Name"
    }

    $value = $Object.PSObject.Properties[$Name].Value
    if (($value -isnot [string]) -or [string]::IsNullOrWhiteSpace($value)) {
        throw "$Context の $Name は空でない文字列にしてください"
    }

    return $value
}

# マニフェスト・パッケージ名に使う相対パスを "/" 区切りに正規化する（絶対パス・".." は拒否）
function ConvertTo-RelativeManifestPath {
    param([string]$Path, [string]$Name)

    $slashPath = $Path.Trim() -replace '\\', '/'
    if (($slashPath -match '^[A-Za-z]:') -or $slashPath.StartsWith('/')) {
        throw "$Name は相対パスにしてください: $Path"
    }

    $segments = @($slashPath -split '/')
    foreach ($segment in $segments) {
        if ([string]::IsNullOrWhiteSpace($segment) -or ($segment -eq '.') -or ($segment -eq '..')) {
            throw "$Name に空の区切りか . / .. があります: $Path"
        }
    }

    return $segments -join '/'
}

# 論理パス・エントリ名は先頭の "Assets/" を外した形で AssetCook へ渡す（CookTextureAssetSet.ps1 と同じ）
function ConvertTo-LogicalPath {
    param([string]$Path, [string]$Name)

    $normalized = ConvertTo-RelativeManifestPath -Path $Path -Name $Name
    if ($normalized.StartsWith("Assets/", [System.StringComparison]::Ordinal)) {
        $normalized = $normalized.Substring(7)
    }
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        throw "$Name の論理パスが空です: $Path"
    }

    return $normalized
}

function Get-TextSha256 {
    param([string]$Text)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Get-FileSha256 {
    param([string]$Path)

    # MSBuild 経由の powershell では Get-FileHash のモジュールが読めないことがあるので .NET で求める
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

function Write-TextUtf8 {
    param([string]$Path, [string]$Text)

    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

$AssetCookPath = Resolve-RepoPath $AssetCookExe
if (-not (Test-Path -LiteralPath $AssetCookPath -PathType Leaf)) {
    throw "AssetCook が見つかりません: $AssetCookPath"
}
$ResolvedSpecDir = Resolve-RepoPath $SpecDir
if (-not (Test-Path -LiteralPath $ResolvedSpecDir -PathType Container)) {
    throw "一覧のディレクトリが見つかりません: $ResolvedSpecDir"
}
$ResolvedRuntimeRoot = Resolve-RepoPath $RuntimeRoot
$StateRoot = Join-Path $ResolvedRuntimeRoot ".cookstate"
$TemporaryManifestPath = Join-Path $ResolvedRuntimeRoot "manifest.cook.tmp.json"
$AggregateManifestPath = Join-Path $ResolvedRuntimeRoot "manifest.json"

New-Item -ItemType Directory -Path $ResolvedRuntimeRoot -Force | Out-Null

$AssetCookHash = Get-FileSha256 $AssetCookPath
$cookedCount = 0
$skippedCount = 0
$missingEntryCount = 0
$aggregateAssets = @()
$seenKeys = @{}
$seenPackages = @{}

$specFiles = @(Get-ChildItem -LiteralPath $ResolvedSpecDir -Filter "*.json" -File | Sort-Object Name)
foreach ($specFile in $specFiles) {
    $spec = Get-Content -LiteralPath $specFile.FullName -Raw | ConvertFrom-Json
    # 検証用の別の一覧（Silver の v0 形式など）は cook_assets を持たないので対象外
    if (-not ((Test-PropertyExists -Object $spec -Name "cook_assets") -and ($spec.cook_assets -eq $true))) {
        continue
    }

    $specContext = "一覧 $($specFile.Name)"
    $setName = Get-StringField -Object $spec -Name "name" -Context $specContext
    $packageRoot = ConvertTo-RelativeManifestPath `
        -Path (Get-StringField -Object $spec -Name "package_root" -Context $specContext) `
        -Name "$specContext の package_root"
    $defaultVariant = Get-StringField -Object $spec -Name "default_variant" -Context $specContext
    if (-not (Test-PropertyExists -Object $spec -Name "textures") -or ($spec.textures -isnot [System.Array])) {
        throw "$specContext の textures は配列にしてください"
    }

    $setStateDir = Join-Path $StateRoot $setName
    New-Item -ItemType Directory -Path $setStateDir -Force | Out-Null

    $index = 0
    foreach ($texture in @($spec.textures)) {
        $context = "$specContext textures[$index]"
        $index++

        $usage = Get-StringField -Object $texture -Name "cook_usage" -Context $context
        if ($ValidUsages -notcontains $usage) {
            throw "$context の cook_usage は $($ValidUsages -join '・') のどれかにしてください: $usage"
        }

        $logicalPath = ConvertTo-LogicalPath `
            -Path (Get-StringField -Object $texture -Name "logical_path" -Context $context) `
            -Name "$context の logical_path"
        $entryName = ConvertTo-LogicalPath `
            -Path (Get-StringField -Object $texture -Name "entry_name" -Context $context) `
            -Name "$context の entry_name"
        $packageName = ConvertTo-RelativeManifestPath `
            -Path (Get-StringField -Object $texture -Name "package_name" -Context $context) `
            -Name "$context の package_name"
        $variant = $defaultVariant
        if (Test-PropertyExists -Object $texture -Name "variant") {
            $variant = Get-StringField -Object $texture -Name "variant" -Context $context
        }

        $key = "$logicalPath|texture|$variant"
        if ($seenKeys.ContainsKey($key)) {
            throw "$context の logical_path|kind|variant が重複しています: $key"
        }
        $seenKeys[$key] = $true
        $cookedPackage = "$packageRoot/$packageName"
        if ($seenPackages.ContainsKey($cookedPackage)) {
            throw "$context の出力パッケージが重複しています: $cookedPackage"
        }
        $seenPackages[$cookedPackage] = $true

        # 元画像: 通常は source_path 1 枚、orm は orm_ao・orm_roughness・orm_metallic のうち指定された枠
        $sourceArguments = @()
        $sourceTexts = @()
        if ($usage -eq "orm") {
            foreach ($ormKey in $OrmSourceKeys) {
                if (Test-PropertyExists -Object $texture -Name $ormKey) {
                    $text = Get-StringField -Object $texture -Name $ormKey -Context $context
                    $sourceTexts += $text
                    $sourceArguments += @{ Argument = $OrmArguments[$ormKey]; Text = $text }
                }
            }
            if ($sourceTexts.Count -eq 0) {
                throw "$context の orm には orm_ao・orm_roughness・orm_metallic のどれか 1 つが要ります"
            }
        }
        else {
            $text = Get-StringField -Object $texture -Name "source_path" -Context $context
            $sourceTexts += $text
            $sourceArguments += @{ Argument = "--input"; Text = $text }
        }

        $missing = @($sourceTexts | Where-Object { -not (Test-Path -LiteralPath (Resolve-RepoPath $_) -PathType Leaf) })
        if ($missing.Count -gt 0) {
            foreach ($missingText in $missing) {
                Write-Output "COOK_ASSETS missing=$missingText"
            }
            $missingEntryCount++
            continue
        }

        $packagePath = [System.IO.Path]::GetFullPath((Join-Path $ResolvedRuntimeRoot ($cookedPackage -replace '/', '\')))
        $statePath = Join-Path $setStateDir (($packageName -replace '/', '__') + ".json")

        # 印: AssetCook・一覧の項目・一覧の共通設定・元画像の内容・品質のどれかが変われば変わる
        $stampSource = New-Object System.Text.StringBuilder
        [void]$stampSource.Append("assetcook=$AssetCookHash;set=$setName;root=$packageRoot;variant=$variant;quality=$Quality;")
        [void]$stampSource.Append("entry=" + ($texture | ConvertTo-Json -Compress -Depth 8) + ";")
        foreach ($sourceText in $sourceTexts) {
            [void]$stampSource.Append("src:$sourceText=" + (Get-FileSha256 (Resolve-RepoPath $sourceText)) + ";")
        }
        $stamp = Get-TextSha256 $stampSource.ToString()

        $cachedAsset = $null
        if ((Test-Path -LiteralPath $statePath -PathType Leaf) -and (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
            try {
                $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
                if (($state.stamp -eq $stamp) -and ($null -ne $state.asset)) {
                    $cachedAsset = $state.asset
                }
            }
            catch {
                $cachedAsset = $null
            }
        }

        if ($null -ne $cachedAsset) {
            $skippedCount++
            $aggregateAssets += $cachedAsset
            continue
        }

        New-Item -ItemType Directory -Path (Split-Path -Parent $packagePath) -Force | Out-Null
        Remove-Item -LiteralPath $TemporaryManifestPath -ErrorAction SilentlyContinue

        $arguments = @()
        foreach ($source in $sourceArguments) {
            $arguments += $source.Argument
            $arguments += (Resolve-RepoPath $source.Text)
        }
        $arguments += @(
            "--out", $packagePath,
            "--manifest", $TemporaryManifestPath,
            "--logical", $logicalPath,
            "--kind", "texture",
            "--entry", $entryName,
            "--entry-type", "Tex0",
            "--usage", $usage,
            "--variant", $variant
        )
        if (-not [string]::IsNullOrWhiteSpace($Quality)) {
            $arguments += @("--quality", $Quality)
        }

        & $AssetCookPath @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "AssetCook が失敗しました（exit code $LASTEXITCODE）: $logicalPath"
        }

        $cookedManifest = Get-Content -LiteralPath $TemporaryManifestPath -Raw | ConvertFrom-Json
        $cookedAssets = @($cookedManifest.assets)
        if ($cookedAssets.Count -ne 1) {
            throw "AssetCook のマニフェストは 1 項目のはずが $($cookedAssets.Count) 項目です: $logicalPath"
        }
        $asset = $cookedAssets[0]
        if (($asset.logical_path -ne $logicalPath) -or ($asset.cooked_package -ne $cookedPackage)) {
            throw "AssetCook のマニフェストが一覧と食い違います: $logicalPath"
        }
        Remove-Item -LiteralPath $TemporaryManifestPath -ErrorAction SilentlyContinue

        $stateJson = [ordered]@{ stamp = $stamp; asset = $asset } | ConvertTo-Json -Depth 16
        Write-TextUtf8 -Path $statePath -Text $stateJson

        $cookedCount++
        $aggregateAssets += $asset
    }
}

$aggregateJson = [ordered]@{ version = 1; assets = @($aggregateAssets) } | ConvertTo-Json -Depth 16
$existingJson = $null
if (Test-Path -LiteralPath $AggregateManifestPath -PathType Leaf) {
    $existingJson = [System.IO.File]::ReadAllText($AggregateManifestPath)
}
if ($existingJson -ne $aggregateJson) {
    Write-TextUtf8 -Path $AggregateManifestPath -Text $aggregateJson
}

if ($missingEntryCount -gt 0) {
    Write-Output "COOK_ASSETS missing_entries=$missingEntryCount manifest_assets=$($aggregateAssets.Count)"
}
Write-Output "COOK_ASSETS cooked=$cookedCount skipped=$skippedCount"
exit 0
