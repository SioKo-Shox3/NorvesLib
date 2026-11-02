# Assets/AssetSets/ の一覧（"cook_assets": true のもの）から、テクスチャを用途別に NVTEX（BC7・BC5・BC4・R16）へ、
# モデル（glTF）を NVMESH（既定は LOD の階層を持つ v1）へ焼いて build/CookedAssets/ へ書く。CMake の対象 CookAssets から呼ぶ。
# 一覧の "textures"（必須）の各項目は cook_usage（albedo|normal|orm|single|height16）を持つ。normal は "flip_normal_y": true で
# 入力の法線の Y を反転する（OpenGL の向きの入力を DirectX の向きへ）。orm は orm_ao・orm_roughness・orm_metallic の別々の元画像か、
# source_path の詰め済みの1枚（glTF の ARM。R=AO・G=粗さ・B=メタリック）のどちらか。
# 一覧の "models"（省略可）の各項目は logical_path・source_path・package_name・entry_name・format（省略時は NVMESH v1）と、
# 変更の検出に含める "extra_sources"（glTF が読む .bin など。省略可）と、フォールバックの段の三角形数の目標の下限
# "fallback_min_triangles"（省略可。小さなメッシュで根の段までの粗さが影・RT の形を崩すときに上げる）と、
# 根のクラスタ数の上限 "dag_root_max_clusters"（省略可。1 以上なら、根がこの数以下のクラスタになるまで簡略化を続ける）と、
# glTF を読まずにメッシュを作らせる "generate"（省略可。displaced-sphere は source_path の高さマップで変位した
# 起動画面の大きな球。仕様は Library/Core/Public/Rendering/MegaGeometry/StartupBigSphereSpec.h。terrain-tile:<x>,<z> は
# 荒野の手続きの地形の区画、terrain-tile:dem:<x>,<z> は DEM の格子の区画で、source_path が格子の記述（height.json）、
# extra_sources が格子のファイルと区画の割り方のヘッダ。一覧の DEM の項目は Scripts/Terrain/WriteTerrainAssetSet.py が書く）を持つ。
#
# 差分クック: 元のファイル・一覧の項目・共通設定・品質の内容から印を求め、変わらないものは前回の結果を使う。
# 地形（モデルの generate が terrain-tile: で始まる項目）の印は exe を含まず、崖の細部の版と地形の経路のソースの内容を含む。
# それ以外の印は AssetCook の exe の内容を含む。一覧外のソースやビルド設定・コンパイラの変更による出力差は
# ReferenceCheck.py --cook-coverage の代表区画の焼き比べで検出する。
# <RuntimeRoot>/.cookstate/ の stamp は今の exe による旧形式の印で、旧スクリプトとの互換用に残す。
# 地形だけに持つ terrain_stamp が地形用の印。これが無く stamp が一致するときは、焼かずに terrain_stamp を追加して移行する。
# terrain_stamp があるのに不一致なら焼き直す。移行した実行は COOK_ASSETS terrain_stamp_migrated=<件数> を出す。
# 地形を実際に焼く前に、exe が地形のソース一覧のどのファイルよりも古くないことを求める。
# 古ければ AssetCook のビルドし直しを求めて止める（飛ばし・移行・-CheckStamps には適用しない）。
# 元画像が無い項目は飛ばし、"COOK_ASSETS missing=<path>" を出す（クックの失敗にはしない）。
#
# 対象を絞る: -OnlyLogicalPath（ワイルドカード可。論理パスと -like で照合）に合う項目だけを焼き（印が同じなら飛ばす。
# -Force で印が同じでも焼き直す）、合わない項目は焼かず、既存の manifest.json の項目をそのまま引き継ぐ（パッケージにも状態にも触れない）。
# AssetCook の実行ファイルが変わって全項目の印が変わっていても、対象外は焼き直されない。既存のマニフェストに無い項目があれば失敗する
# （先に全体をクックする）。
#
# 印だけ照らす: -CheckStamps は焼かず、-OnlyLogicalPath に合う項目ごとに今の入力から印を求めて状態の印と比べ、
# "COOK_STAMP logical=<論理パス> package=<パッケージ> status=<match|stale|state_missing|state_invalid|package_missing>" を出す
# （マニフェストもパッケージも状態も書かない。-OnlyLogicalPath が要る）。
# 地形は terrain_stamp があればそれと比較し、無ければ stamp と比較する（match なら移行可能。確認だけでは移行しない）。
# 対象に地形があれば印の元を 1 回だけ COOK_STAMP_TERRAIN cliff_detail_version=<版> sources_sha256=<一覧の印> sources=<数> と、
# ファイルごとの COOK_STAMP_TERRAIN_SOURCE path=<リポジトリ相対パス> sha256=<印> で出す。
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
    [string]$Quality = "",

    # 焼く項目の論理パス（"Assets/" を除いた形。ワイルドカード可）。空なら全項目
    [string[]]$OnlyLogicalPath = @(),

    # 印が同じでも対象の項目を焼き直す（-OnlyLogicalPath と併せて使う）
    [switch]$Force,

    # 焼かずに、対象の項目の印を今の入力と照らして出す（-OnlyLogicalPath と併せて使う）
    [switch]$CheckStamps,

    # 付けたときは DEM の区画を焼いても接地用の高さ格子を書かない（作業者ごとの置き場で並列に焼き、後で共有の置き場で 1 回書くとき）
    [switch]$SkipPlacementGround
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$ValidUsages = @("albedo", "normal", "orm", "single", "height16")
# orm_occlusion は A に詰める照明用の遮蔽（省略可。R の AO を地形の層の混ぜの高さに使う材質が、遮蔽だけを別の値にする）
$OrmSourceKeys = @("orm_ao", "orm_roughness", "orm_metallic", "orm_occlusion")
$OrmArguments = @{ orm_ao = "--orm-ao"; orm_roughness = "--orm-roughness"; orm_metallic = "--orm-metallic";
    orm_occlusion = "--orm-occlusion" }

# 地形生成・崖・DEM・DAG/簡略化・クラスタ化・NVMESH/パッケージ/マニフェストの値と形式、入力の読み取りを含む。
# SingleAssetCook は崖の切り替えと要求の既定値も含める。meshoptimizer は呼び出し先の取り漏れを避け、上流のソース全体を名前順で固定する。
# Main の引数の配線・検査、CookOutputPackage の検証、glTF/リグ/VFX/音/テクスチャ専用の処理、ビルド設定は含めない。
# 一覧外やコンパイラの変更は ReferenceCheck.py --cook-coverage の代表区画の焼き比べで検出する。
$TerrainStampSourcePaths = @(
    "Tools/AssetCook/SingleAssetCook.cpp",
    "Tools/AssetCook/SingleAssetCook.h",
    "Tools/AssetCook/AssetCookLegacyOptions.h",
    "Tools/AssetCook/MeshCooker.cpp",
    "Tools/AssetCook/MeshCooker.h",
    "Tools/AssetCook/TerrainCliffDetail.cpp",
    "Tools/AssetCook/TerrainCliffDetail.h",
    "Tools/AssetCook/TerrainCliffCarveBattleship.h",
    "Tools/AssetCook/TerrainCliffShelfBattleship.h",
    "Tools/AssetCook/CookMeshDag.cpp",
    "Tools/AssetCook/CookMeshDag.h",
    "Tools/AssetCook/CookMeshOptimizer.cpp",
    "Tools/AssetCook/CookMeshOptimizer.h",
    "Tools/AssetCook/Sha256Digest.cpp",
    "Tools/AssetCook/Sha256Digest.h",
    "Tools/AssetCook/AssetCookOutput.cpp",
    "Tools/AssetCook/AssetCookOutput.h",
    "Library/Core/Public/Rendering/Terrain/TerrainHeightField.h",
    "Library/Core/Private/Rendering/Terrain/TerrainHeightField.cpp",
    "Library/Core/Public/Rendering/MegaGeometry/MeshClusterizer.h",
    "Library/Core/Private/Rendering/MegaGeometry/MeshClusterizer.cpp",
    "Library/Core/Public/Rendering/MegaGeometry/MegaGeometryTypes.h",
    "Library/Core/Public/Asset/CookedMeshFormat.h",
    "Library/Core/Public/Asset/CookedMeshWireFormat.h",
    "Library/Core/Public/Asset/CookedMaterialFormat.h",
    "Library/Core/Private/Asset/CookedMeshLoader.cpp",
    "Library/Core/Private/Asset/CookedMeshWireValidation.cpp",
    "Library/Core/Private/Asset/CookedMeshWireValidation.h",
    "Library/Core/Public/Asset/AssetPackageFormat.h",
    "Library/Core/Public/FileStream/Package.h",
    "Library/Core/Private/FileStream/Package.cpp",
    "Library/Core/Public/Asset/AssetFileReader.h",
    "Library/Core/Private/Asset/AssetFileReader.cpp",
    "Library/Core/Public/Asset/AssetReadRequest.h",
    "Library/Core/Public/Asset/AssetPath.h",
    "Library/Core/Private/Asset/AssetPath.cpp",
    "Library/Core/Public/Asset/AssetBlob.h",
    "Library/Core/Private/Asset/AssetBlob.cpp",
    "Library/Core/Public/FileStream/FileStream.h",
    "Library/Core/Private/FileStream/FileStream.cpp",
    "Library/Core/Public/FileStream/IFileStream.h",
    "Library/Core/Public/Text/JsonDocument.h",
    "Library/Core/Private/Text/JsonDocument.cpp",
    "Library/Core/Private/Text/JsonUnicodeScalar.h",
    "Library/Core/Private/Text/UnicodeText.h",
    "Library/ThirdParty/meshoptimizer/upstream/allocator.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/clusterizer.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/indexanalyzer.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/indexcodec.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/indexgenerator.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/meshletcodec.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/meshletutils.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/meshoptimizer.h",
    "Library/ThirdParty/meshoptimizer/upstream/opacitymap.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/overdrawoptimizer.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/partition.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/quantization.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/rasterizer.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/remesher.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/simplifier.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/spatialorder.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/stripifier.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/tangentspace.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/vcacheoptimizer.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/vertexcodec.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/vertexfilter.cpp",
    "Library/ThirdParty/meshoptimizer/upstream/vfetchoptimizer.cpp"
)
$TerrainStampInputs = $null
$TerrainCookExecutableChecked = $false

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

# 1 回の実行の中で同じファイルを何度も求めない（DEM の区画は全項目が同じ 86 MB の格子を変更の検出に含める）。
# 大きさと更新時刻が変われば求め直す
$FileSha256Cache = @{}

function Get-FileSha256 {
    param([string]$Path)

    $info = Get-Item -LiteralPath $Path
    $cacheKey = "$($info.FullName)|$($info.Length)|$($info.LastWriteTimeUtc.Ticks)"
    if ($script:FileSha256Cache.ContainsKey($cacheKey)) {
        return $script:FileSha256Cache[$cacheKey]
    }

    # MSBuild 経由の powershell では Get-FileHash のモジュールが読めないことがあるので .NET で求める
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $hash = ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $sha.Dispose()
    }
    $script:FileSha256Cache[$cacheKey] = $hash
    return $hash
}

function Write-TextUtf8 {
    param([string]$Path, [string]$Text)

    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Initialize-TerrainStampInputs {
    if ($null -ne $script:TerrainStampInputs) {
        return
    }

    $versionPath = Resolve-RepoPath "Tools/AssetCook/TerrainCliffDetail.h"
    $versionText = Get-Content -LiteralPath $versionPath -Raw -Encoding UTF8
    $versionMatch = [regex]::Match($versionText, 'kTerrainCliffDetailVersion\s*=\s*(\d+)u?;')
    if (-not $versionMatch.Success) {
        throw "地形の崖の細部の版を読めません: $versionPath"
    }

    $sourceText = New-Object System.Text.StringBuilder
    $sources = @(
        foreach ($path in $script:TerrainStampSourcePaths) {
            $resolved = Resolve-RepoPath $path
            if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
                throw "地形の変更の検出に使うソースが見つかりません: $path"
            }
            $hash = Get-FileSha256 $resolved
            [void]$sourceText.Append("src:$path=$hash;")
            [pscustomobject]@{ Path = $path; Hash = $hash }
        }
    )
    $version = $versionMatch.Groups[1].Value
    $script:TerrainStampInputs = "terrain-stamp-v1;cliff_detail_version=$version;" + $sourceText.ToString()
    if ($script:CheckStampsOnly) {
        $sourcesHash = Get-TextSha256 $sourceText.ToString()
        Write-Host "COOK_STAMP_TERRAIN cliff_detail_version=$version sources_sha256=$sourcesHash sources=$($sources.Count)"
        foreach ($source in $sources) {
            Write-Host "COOK_STAMP_TERRAIN_SOURCE path=$($source.Path) sha256=$($source.Hash)"
        }
    }
}

function Assert-TerrainCookExecutableCurrent {
    if ($script:TerrainCookExecutableChecked) {
        return
    }

    $executableWriteTime = (Get-Item -LiteralPath $script:AssetCookPath).LastWriteTimeUtc
    # 崖の細部の版を持つ TerrainCliffDetail.h もソース一覧に含む。
    foreach ($path in $script:TerrainStampSourcePaths) {
        $sourceWriteTime = (Get-Item -LiteralPath (Resolve-RepoPath $path)).LastWriteTimeUtc
        if ($executableWriteTime -lt $sourceWriteTime) {
            throw "AssetCook が地形のソース $path より古い。先に AssetCook をビルドし直す"
        }
    }
    $script:TerrainCookExecutableChecked = $true
}

# 項目 1 つ分: 印で前回の結果が使えるかを判定し、使えなければ AssetCook を実行する。
# 戻り値は、マニフェストへ載せる項目（前回のものか、今回焼いたもの）。元画像が無いときは $null（COOK_ASSETS missing=<path> を出す）。
function Test-OnlyTarget {
    param([string]$LogicalPath)

    foreach ($pattern in $script:OnlyPatterns) {
        if ($LogicalPath -like $pattern) {
            return $true
        }
    }
    return $false
}

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
        [string]$SetStateDir,
        [bool]$IsTerrain = $false
    )

    # 対象を絞っているとき、対象外は焼かず、既存のマニフェストの項目（埋め込みの画像を含む）を引き継ぐ
    if (($script:OnlyPatterns.Count -gt 0) -and (-not (Test-OnlyTarget -LogicalPath $LogicalPath))) {
        $carried = @($script:PreviousAssets | Where-Object {
                ($_.cooked_package -eq $CookedPackage) -or $_.cooked_package.StartsWith("$CookedPackage.")
            })
        if (($carried.Count -lt 1) -or ($carried[0].cooked_package -ne $CookedPackage)) {
            throw "対象外の項目が既存のマニフェストにありません（先に全体をクックしてください）: $LogicalPath"
        }
        $script:untouchedCount++
        return $carried
    }

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

    # 旧形式の印は互換用に維持し、地形用には exe を除いた共通の入力に地形のソースを足す。
    $stampSource = New-Object System.Text.StringBuilder
    [void]$stampSource.Append("set=$SetName;root=$PackageRootName;variant=$Variant;quality=$($script:Quality);")
    [void]$stampSource.Append("entry=" + ($Item | ConvertTo-Json -Compress -Depth 8) + ";")
    foreach ($stampText in $StampTexts) {
        $stampPath = Resolve-RepoPath $stampText
        if (-not (Test-Path -LiteralPath $stampPath -PathType Leaf)) {
            throw "変更の検出に使うファイルが見つかりません: $stampText"
        }
        [void]$stampSource.Append("src:$stampText=" + (Get-FileSha256 $stampPath) + ";")
    }
    $stamp = Get-TextSha256 ("assetcook=$($script:AssetCookHash);" + $stampSource.ToString())
    $terrainStamp = $null
    if ($IsTerrain) {
        Initialize-TerrainStampInputs
        $terrainStamp = Get-TextSha256 ($script:TerrainStampInputs + $stampSource.ToString())
    }

    if ($script:CheckStampsOnly) {
        $stampStatus = "match"
        if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
            $stampStatus = "package_missing"
        }
        elseif (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
            $stampStatus = "state_missing"
        }
        else {
            try {
                $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
                $stampMatches = if ($IsTerrain -and (Test-PropertyExists -Object $state -Name "terrain_stamp")) {
                    $state.terrain_stamp -eq $terrainStamp
                } else {
                    $state.stamp -eq $stamp
                }
                if (-not $stampMatches) {
                    $stampStatus = "stale"
                }
            }
            catch {
                $stampStatus = "state_invalid"
            }
        }
        Write-Host "COOK_STAMP logical=$LogicalPath package=$CookedPackage status=$stampStatus"
        $script:untouchedCount++
        return @($script:PreviousAssets | Where-Object {
                ($_.cooked_package -eq $CookedPackage) -or $_.cooked_package.StartsWith("$CookedPackage.")
            })
    }

    if ((-not $script:ForceCook) -and (Test-Path -LiteralPath $statePath -PathType Leaf) -and (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
        $state = $null
        try {
            $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        }
        catch {
            # 状態ファイルが壊れていれば焼き直す
        }
        if ((Test-PropertyExists -Object $state -Name "asset") -and ($null -ne $state.asset)) {
            $hasTerrainStamp = Test-PropertyExists -Object $state -Name "terrain_stamp"
            $stampMatches = if ($IsTerrain -and $hasTerrainStamp) {
                $state.terrain_stamp -eq $terrainStamp
            } else {
                (Test-PropertyExists -Object $state -Name "stamp") -and ($state.stamp -eq $stamp)
            }
            if ($stampMatches) {
                if ($IsTerrain -and (-not $hasTerrainStamp)) {
                    # 既存の欄を保ち、旧形式の印が今の exe と一致する状態だけを焼かずに移行する。
                    $state | Add-Member -MemberType NoteProperty -Name "terrain_stamp" -Value $terrainStamp
                    Write-TextUtf8 -Path $statePath -Text ($state | ConvertTo-Json -Depth 16)
                    $script:terrainStampMigratedCount++
                }
                $script:skippedCount++
                $previous = @($state.asset)
                if ((Test-PropertyExists -Object $state -Name "embedded_assets") -and ($null -ne $state.embedded_assets)) {
                    $previous += @($state.embedded_assets)
                }
                return $previous
            }
        }
    }

    if ($IsTerrain) {
        Assert-TerrainCookExecutableCurrent
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

    $cookedManifest = Get-Content -LiteralPath $script:TemporaryManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $cookedAssets = @($cookedManifest.assets)
    if ($cookedAssets.Count -lt 1) {
        throw "AssetCook のマニフェストが空です: $LogicalPath"
    }
    $asset = $cookedAssets[0]
    if (($asset.logical_path -ne $LogicalPath) -or ($asset.cooked_package -ne $CookedPackage)) {
        throw "AssetCook のマニフェストが一覧と食い違います: $LogicalPath"
    }
    # GLB に内蔵された画像は、AssetCook がモデルと同じ呼び出しでテクスチャとして焼き、2 項目目以降に並べる
    # （logical_path が <モデル>.img<n>.png、cooked_package が <モデルのパッケージ>.img<n>.nvpkg）。
    $embeddedAssets = @($cookedAssets | Select-Object -Skip 1)
    foreach ($embedded in $embeddedAssets) {
        if (($embedded.kind -ne "texture") -or (-not $embedded.logical_path.StartsWith("$LogicalPath.")) `
                -or (-not $embedded.cooked_package.StartsWith("$CookedPackage."))) {
            throw "AssetCook のマニフェストに一覧に無い項目があります: $($embedded.logical_path)"
        }
    }
    Remove-Item -LiteralPath $script:TemporaryManifestPath -ErrorAction SilentlyContinue

    $stateFields = [ordered]@{ stamp = $stamp; asset = $asset; embedded_assets = $embeddedAssets }
    if ($IsTerrain) {
        $stateFields["terrain_stamp"] = $terrainStamp
    }
    $stateJson = $stateFields | ConvertTo-Json -Depth 16
    Write-TextUtf8 -Path $statePath -Text $stateJson

    $script:cookedCount++
    return @($asset) + $embeddedAssets
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
# 対象を分けた複数の実行が同時に走っても、AssetCook の 1 項目ぶんのマニフェストを取り違えないようプロセスごとに分ける
$TemporaryManifestPath = Join-Path $ResolvedRuntimeRoot "manifest.cook.$PID.tmp.json"
$AggregateManifestPath = Join-Path $ResolvedRuntimeRoot "manifest.json"

if (-not $CheckStamps) {
    New-Item -ItemType Directory -Path $ResolvedRuntimeRoot -Force | Out-Null
}

$AssetCookHash = Get-FileSha256 $AssetCookPath
$cookedCount = 0
$skippedCount = 0
$terrainStampMigratedCount = 0
$untouchedCount = 0
$ForceCook = $Force.IsPresent
$CheckStampsOnly = $CheckStamps.IsPresent
$OnlyPatterns = @($OnlyLogicalPath | ForEach-Object { ConvertTo-LogicalPath -Path $_ -Name "OnlyLogicalPath" })
$PreviousAssets = @()
if ($CheckStampsOnly -and ($OnlyPatterns.Count -eq 0)) {
    throw "-CheckStamps には -OnlyLogicalPath が要ります"
}
if ($OnlyPatterns.Count -gt 0) {
    if (-not (Test-Path -LiteralPath $AggregateManifestPath -PathType Leaf)) {
        throw "対象を絞るには既存の manifest.json が要ります（先に全体をクックしてください）: $AggregateManifestPath"
    }
    $PreviousAssets = @((Get-Content -LiteralPath $AggregateManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json).assets)
}
$missingEntryCount = 0
$placementGroundSelected = $false
$aggregateAssets = @()
$seenKeys = @{}
$seenPackages = @{}

$specFiles = @(Get-ChildItem -LiteralPath $ResolvedSpecDir -Filter "*.json" -File | Sort-Object Name)
foreach ($specFile in $specFiles) {
    $spec = Get-Content -LiteralPath $specFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
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
    if (-not $CheckStampsOnly) {
        New-Item -ItemType Directory -Path $setStateDir -Force | Out-Null
    }

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
            # pwsh 7.6 の ConvertFrom-Json は整数を Int64 で返す
            if ((($fallbackMin -isnot [int]) -and ($fallbackMin -isnot [long])) -or ($fallbackMin -lt 0)) {
                throw "$context の fallback_min_triangles は 0 以上の整数にしてください"
            }
            $cookArguments += @("--fallback-min-triangles", [string]$fallbackMin)
        }
        if (Test-PropertyExists -Object $model -Name "dag_root_max_clusters") {
            $dagRootMax = $model.dag_root_max_clusters
            if ((($dagRootMax -isnot [int]) -and ($dagRootMax -isnot [long])) -or ($dagRootMax -lt 0) -or ($dagRootMax -gt 1000)) {
                throw "$context の dag_root_max_clusters は 0〜1000 の整数にしてください"
            }
            $cookArguments += @("--dag-root-max-clusters", [string]$dagRootMax)
        }
        # "generate": "displaced-sphere" は、source_path の高さマップ（16 ビットのグレーの PNG）で変位した起動画面の大きな球を
        # AssetCook に作らせて焼く（glTF は読まない）。"terrain-tile:<x>,<z>" は荒野の地形の区画を高さ場の関数から作って焼く
        # （source_path は高さ場の仕様のヘッダで、変更の検出だけに使う）。"terrain-tile:dem:<x>,<z>" は DEM の格子の区画を焼く
        # （source_path は格子の記述で、AssetCook が高さ場の読む記述と同じか・格子のファイルの SHA-256 が記述と合うかを確かめる。
        # 格子のファイルと区画の割り方のヘッダは extra_sources に入れ、どれが変わっても焼き直す）。
        # "terrain-tile:dem-plain:<x>,<z>" は dem と同じ区画を崖の細部（TerrainCliffDetail.cpp）なしで焼く（撮り比べ用）。
        $isPlacementTerrain = $false
        $isTerrain = $false
        if (Test-PropertyExists -Object $model -Name "generate") {
            $generate = Get-StringField -Object $model -Name "generate" -Context $context
            if (($generate -ne "displaced-sphere") -and ($generate -notmatch '^terrain-tile:(dem:|dem-plain:)?\d+,\d+$')) {
                throw "$context の generate は displaced-sphere か terrain-tile:<x>,<z>・terrain-tile:dem:<x>,<z>・terrain-tile:dem-plain:<x>,<z> だけを指定できます"
            }
            $cookArguments += @("--generate", $generate)
            $isTerrain = $generate -match '^terrain-tile:'
            $isPlacementTerrain = ($generate -match '^terrain-tile:dem:\d+,\d+$') -and `
                ($logicalPath -match '^Models/WastelandTerrain/WastelandDem_\d+_\d+\.generated$') -and `
                (($OnlyPatterns.Count -eq 0) -or (Test-OnlyTarget -LogicalPath $logicalPath))
        }
        $asset = Invoke-CookEntry -SetName $setName -PackageRootName $packageRoot -Variant $variant -Kind "model" `
            -LogicalPath $logicalPath -CookedPackage $cookedPackage -EntryName $entryName -Item $model `
            -SourceTexts @($sourceText) -StampTexts $stampTexts -CookArguments $cookArguments -SetStateDir $setStateDir -IsTerrain $isTerrain
        if ($null -ne $asset) {
            $aggregateAssets += $asset
            if ($isPlacementTerrain) {
                $placementGroundSelected = $true
            }
        }
    }
}

if ($CheckStampsOnly) {
    Write-Output "COOK_ASSETS check_stamps=$untouchedCount"
    exit 0
}

$aggregateJson = [ordered]@{ version = 1; assets = @($aggregateAssets) } | ConvertTo-Json -Depth 16
$existingJson = $null
if (Test-Path -LiteralPath $AggregateManifestPath -PathType Leaf) {
    $existingJson = [System.IO.File]::ReadAllText($AggregateManifestPath)
}
if ($existingJson -ne $aggregateJson) {
    Write-TextUtf8 -Path $AggregateManifestPath -Text $aggregateJson
}

# DEM の区画が今回の対象に含まれるときだけ、集約済みの参照から接地用の格子を更新する。
# 地形以外だけの絞り込み・印だけの確認・元の DEM が欠けた実行・-SkipPlacementGround を付けた実行では呼ばない。
if ($placementGroundSelected -and -not $SkipPlacementGround) {
    $placementGroundPath = Join-Path $ResolvedRuntimeRoot "Terrain/WastelandPlacementGround.bin"
    $placementArguments = @(
        "--write-placement-ground",
        "--manifest", $AggregateManifestPath,
        "--out", $placementGroundPath,
        "--skip-if-current"
    )
    & $AssetCookPath @placementArguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "接地用の高さ格子の書き出しが失敗しました（exit code $LASTEXITCODE）"
    }
} elseif ($placementGroundSelected -and $SkipPlacementGround) {
    Write-Output "COOK_ASSETS placement_ground=skipped"
}

if ($missingEntryCount -gt 0) {
    Write-Output "COOK_ASSETS missing_entries=$missingEntryCount manifest_assets=$($aggregateAssets.Count)"
}
if ($OnlyPatterns.Count -gt 0) {
    Write-Output "COOK_ASSETS only=$($OnlyPatterns -join ',') untouched=$untouchedCount"
}
if ($terrainStampMigratedCount -gt 0) {
    Write-Output "COOK_ASSETS terrain_stamp_migrated=$terrainStampMigratedCount"
}
Write-Output "COOK_ASSETS cooked=$cookedCount skipped=$skippedCount"
exit 0
