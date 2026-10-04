# Assets/AssetSets/ の一覧（"cook_assets": true のもの）から、テクスチャを用途別に NVTEX（BC7・BC5・BC4・R16）へ、
# モデル（glTF）を NVMESH（既定は LOD の階層を持つ v1）へ焼いて build/CookedAssets/ へ書く。CMake の対象 CookAssets から呼ぶ。
# 一覧の "textures"（必須）の各項目は cook_usage（albedo|normal|orm|single|height16）を持つ。normal は "flip_normal_y": true で
# 入力の法線の Y を反転する（OpenGL の向きの入力を DirectX の向きへ）。orm は orm_ao・orm_roughness・orm_metallic の別々の元画像か、
# source_path の詰め済みの1枚（glTF の ARM。R=AO・G=粗さ・B=メタリック）のどちらか。
# 一覧の "models"（省略可）の各項目は logical_path・source_path・package_name・entry_name・format（省略時は NVMESH v1）と、
# 変更の検出に含める "extra_sources"（glTF が読む .bin など。省略可）と、フォールバックの段の三角形数の目標の下限
# "fallback_min_triangles"（省略可。小さなメッシュで根の段までの粗さが影・RT の形を崩すときに上げる）を持つ。
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

# 項目 1 つ分: 印で前回の結果が使えるかを判定し、使えなければ AssetCook を実行する。
# 戻り値は、マニフェストへ載せる項目（前回のものか、今回焼いたもの）。元画像が無いときは $null（COOK_ASSETS missing=<path> を出す）。
function Invoke-CookEntry {
    param(
        [string]$SetName,
        [string]$PackageRootName,
        [string]$Variant,
        [string]$Kind,
        [string]$LogicalPath,
        [string]$CookedPackage,
        [string]$EntryName,
        [object]$Item,
        [string[]]$SourceTexts,
        [string[]]$StampTexts,
        [string[]]$CookArguments,
        [string]$SetStateDir
    )

    $missing = @($SourceTexts | Where-Object { -not (Test-Path -LiteralPath (Resolve-RepoPath $_) -PathType Leaf) })
    if ($missing.Count -gt 0) {
        foreach ($missingText in $missing) {
            # 関数の戻り値（項目）に混ざらないよう、成功ストリームではなくホストへ出す
            Write-Host "COOK_ASSETS missing=$missingText"
        }
        $script:missingEntryCount++
        return $null
    }

    $packagePath = [System.IO.Path]::GetFullPath((Join-Path $script:ResolvedRuntimeRoot ($CookedPackage -replace '/', '\')))
    # 状態ファイル名は出力先の相対パス全体のハッシュにする（"A/B" と "A__B" のような別パッケージが同じ名前にならない）
    $statePath = Join-Path $SetStateDir ((Get-TextSha256 $CookedPackage) + ".json")

    # 印: AssetCook・一覧の項目・一覧の共通設定・元画像の内容・品質のどれかが変われば変わる
    $stampSource = New-Object System.Text.StringBuilder
    [void]$stampSource.Append("assetcook=$($script:AssetCookHash);set=$SetName;root=$PackageRootName;variant=$Variant;quality=$($script:Quality);")
    [void]$stampSource.Append("entry=" + ($Item | ConvertTo-Json -Compress -Depth 8) + ";")
    foreach ($stampText in $StampTexts) {
        $stampPath = Resolve-RepoPath $stampText
        if (-not (Test-Path -LiteralPath $stampPath -PathType Leaf)) {
            throw "変更の検出に使うファイルが見つかりません: $stampText"
        }
        [void]$stampSource.Append("src:$stampText=" + (Get-FileSha256 $stampPath) + ";")
    }
    $stamp = Get-TextSha256 $stampSource.ToString()

    if ((Test-Path -LiteralPath $statePath -PathType Leaf) -and (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
        try {
            $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
            if (($state.stamp -eq $stamp) -and ($null -ne $state.asset)) {
                $script:skippedCount++
                return $state.asset
            }
        }
        catch {
            # 状態ファイルが壊れていれば焼き直す
        }
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $packagePath) -Force | Out-Null
    Remove-Item -LiteralPath $script:TemporaryManifestPath -ErrorAction SilentlyContinue

    $arguments = @($CookArguments)
    $arguments += @(
        "--out", $packagePath,
        "--manifest", $script:TemporaryManifestPath,
        "--logical", $LogicalPath,
        "--kind", $Kind,
        "--entry", $EntryName,
        "--variant", $Variant
    )

    # AssetCook の標準出力が関数の戻り値（項目）に混ざらないよう、ホストへ流す
    & $script:AssetCookPath @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "AssetCook が失敗しました（exit code $LASTEXITCODE）: $LogicalPath"
    }

    $cookedManifest = Get-Content -LiteralPath $script:TemporaryManifestPath -Raw | ConvertFrom-Json
    $cookedAssets = @($cookedManifest.assets)
    if ($cookedAssets.Count -ne 1) {
        throw "AssetCook のマニフェストは 1 項目のはずが $($cookedAssets.Count) 項目です: $LogicalPath"
    }
    $asset = $cookedAssets[0]
    if (($asset.logical_path -ne $LogicalPath) -or ($asset.cooked_package -ne $CookedPackage)) {
        throw "AssetCook のマニフェストが一覧と食い違います: $LogicalPath"
    }
    Remove-Item -LiteralPath $script:TemporaryManifestPath -ErrorAction SilentlyContinue

    $stateJson = [ordered]@{ stamp = $stamp; asset = $asset } | ConvertTo-Json -Depth 16
    Write-TextUtf8 -Path $statePath -Text $stateJson

    $script:cookedCount++
    return $asset
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

        # 元画像: 通常は source_path 1 枚。orm は orm_ao・orm_roughness・orm_metallic のうち指定された枠か、
        # 詰め済みの1枚（source_path。glTF の ARM）のどちらか
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
            if (Test-PropertyExists -Object $texture -Name "source_path") {
                if ($sourceTexts.Count -gt 0) {
                    throw "$context の orm は source_path（詰め済みの1枚）か orm_*（別々の元画像）のどちらか一方にしてください"
                }
                $text = Get-StringField -Object $texture -Name "source_path" -Context $context
                $sourceTexts += $text
                $sourceArguments += @{ Argument = "--input"; Text = $text }
            }
            if ($sourceTexts.Count -eq 0) {
                throw "$context の orm には source_path か orm_ao・orm_roughness・orm_metallic のどれか 1 つが要ります"
            }
        }
        else {
            $text = Get-StringField -Object $texture -Name "source_path" -Context $context
            $sourceTexts += $text
            $sourceArguments += @{ Argument = "--input"; Text = $text }
        }

        $bFlipNormalY = (Test-PropertyExists -Object $texture -Name "flip_normal_y") -and ($texture.flip_normal_y -eq $true)
        if ($bFlipNormalY -and ($usage -ne "normal")) {
            throw "$context の flip_normal_y は cook_usage が normal のときだけ指定できます"
        }

        $cookArguments = @()
        foreach ($source in $sourceArguments) {
            $cookArguments += $source.Argument
            $cookArguments += (Resolve-RepoPath $source.Text)
        }
        $cookArguments += @("--entry-type", "Tex0", "--usage", $usage)
        if ($bFlipNormalY) {
            $cookArguments += "--flip-normal-y"
        }
        if (-not [string]::IsNullOrWhiteSpace($Quality)) {
            $cookArguments += @("--quality", $Quality)
        }

        $asset = Invoke-CookEntry -SetName $setName -PackageRootName $packageRoot -Variant $variant -Kind "texture" `
            -LogicalPath $logicalPath -CookedPackage $cookedPackage -EntryName $entryName -Item $texture `
            -SourceTexts $sourceTexts -StampTexts $sourceTexts -CookArguments $cookArguments -SetStateDir $setStateDir
        if ($null -ne $asset) {
            $aggregateAssets += $asset
        }
    }

    # モデル（glTF）。LOD の階層を持つ NVMESH v1 へ焼く。glTF が参照する画像は焼かない（textures に別に並べる）。
    $models = @()
    if (Test-PropertyExists -Object $spec -Name "models") {
        if ($spec.models -isnot [System.Array]) {
            throw "$specContext の models は配列にしてください"
        }
        $models = @($spec.models)
    }

    $modelIndex = 0
    foreach ($model in $models) {
        $context = "$specContext models[$modelIndex]"
        $modelIndex++

        $logicalPath = ConvertTo-LogicalPath `
            -Path (Get-StringField -Object $model -Name "logical_path" -Context $context) `
            -Name "$context の logical_path"
        $entryName = ConvertTo-LogicalPath `
            -Path (Get-StringField -Object $model -Name "entry_name" -Context $context) `
            -Name "$context の entry_name"
        $packageName = ConvertTo-RelativeManifestPath `
            -Path (Get-StringField -Object $model -Name "package_name" -Context $context) `
            -Name "$context の package_name"
        $sourceText = Get-StringField -Object $model -Name "source_path" -Context $context
        $format = "nvmesh.v1.mesh3d.pnt.u32.lodgraph"
        if (Test-PropertyExists -Object $model -Name "format") {
            $format = Get-StringField -Object $model -Name "format" -Context $context
        }
        $variant = $defaultVariant
        if (Test-PropertyExists -Object $model -Name "variant") {
            $variant = Get-StringField -Object $model -Name "variant" -Context $context
        }

        $key = "$logicalPath|model|$variant"
        if ($seenKeys.ContainsKey($key)) {
            throw "$context の logical_path|kind|variant が重複しています: $key"
        }
        $seenKeys[$key] = $true
        $cookedPackage = "$packageRoot/$packageName"
        if ($seenPackages.ContainsKey($cookedPackage)) {
            throw "$context の出力パッケージが重複しています: $cookedPackage"
        }
        $seenPackages[$cookedPackage] = $true

        # 変更の検出には glTF 本体と、それが読む外部の .bin など（extra_sources）を含める
        $stampTexts = @($sourceText)
        if (Test-PropertyExists -Object $model -Name "extra_sources") {
            if ($model.extra_sources -isnot [System.Array]) {
                throw "$context の extra_sources は配列にしてください"
            }
            $stampTexts += @($model.extra_sources)
        }

        $cookArguments = @(
            "--input", (Resolve-RepoPath $sourceText),
            "--entry-type", "Msh0",
            "--format", $format
        )
        if (Test-PropertyExists -Object $model -Name "fallback_min_triangles") {
            $fallbackMin = $model.fallback_min_triangles
            if (($fallbackMin -isnot [int]) -or ($fallbackMin -lt 0)) {
                throw "$context の fallback_min_triangles は 0 以上の整数にしてください"
            }
            $cookArguments += @("--fallback-min-triangles", [string]$fallbackMin)
        }
        $asset = Invoke-CookEntry -SetName $setName -PackageRootName $packageRoot -Variant $variant -Kind "model" `
            -LogicalPath $logicalPath -CookedPackage $cookedPackage -EntryName $entryName -Item $model `
            -SourceTexts @($sourceText) -StampTexts $stampTexts -CookArguments $cookArguments -SetStateDir $setStateDir
        if ($null -ne $asset) {
            $aggregateAssets += $asset
        }
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
