# 起動画面（Rendering3DTest）の地面の見本の区画に使う Poly Haven（CC0）のテクスチャを
# Assets/Textures/PolyHaven/<id>/ へ落とす。保存先は git の管理外（.gitignore）で、ファイルが無い区画は
# 起動画面で石畳のまま描く。
#
# 各素材の色（diff）・法線（nor_dx。このエンジンの余接フレームは DirectX の向き）・粗さ（rough）・
# AO（ao）・高さ（disp）を 4K の JPG で取る。ファイルの URL・大きさ・MD5 は Poly Haven の API
# （https://api.polyhaven.com/files/<id>）から引き、既にあって MD5 が一致するファイルは落とし直さない。
# 素材の並びは Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp の地面の区画の表と同じにする。
#
# -StressSet を付けると、テクスチャの負荷モード（Game の --stress-textures）が使う負荷用の材質の組
# （地面・壁・木・金属・屋根などの CC0 の材質 24 種、4K）も落とす。負荷用は色・法線・粗さ・AO の4枚
# （高さは使わない）で、保存先は同じ Assets/Textures/PolyHaven/<id>/。全部で約 0.8 GB になる。
# 負荷用の材質の並びは Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp の kStressMaterials・
# Assets/AssetSets/Rendering3DTestStressTextures.json と同じにする。
#
# 終了コード: すべてそろえば0、どれかを落とせない・MD5が合わなければ1。
[CmdletBinding()]
param(
    # 既にあるファイルも落とし直す
    [switch]$Force,

    # 負荷モード用の材質の組も落とす
    [switch]$StressSet
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repoRoot = Split-Path -Parent $PSScriptRoot
$destinationRoot = Join-Path $repoRoot 'Assets\Textures\PolyHaven'

# 地面の区画に並べる素材（Poly Haven の資産ID）
$assetIds = @(
    'brown_mud_leaves_01',
    'forrest_ground_01',
    'sand_01',
    'asphalt_02',
    'snow_02',
    'marble_01'
)
# 負荷モード用の材質（-StressSet のときだけ落とす）
$stressAssetIds = @(
    'aerial_rocks_02',
    'coast_sand_rocks_02',
    'dry_ground_rocks',
    'gravel_ground_01',
    'forest_ground_04',
    'rock_04',
    'castle_brick_01',
    'concrete_wall_003',
    'quarry_wall',
    'mossy_stone_wall',
    'brown_planks_03',
    'dark_planks',
    'herringbone_parquet',
    'bark_brown_01',
    'cobblestone_floor_01',
    'brick_floor',
    'concrete_floor_worn_001',
    'terracotta_floor_tiles',
    'corrugated_iron',
    'rusty_metal_02',
    'metal_plate',
    'clay_roof_tiles',
    'roof_slates_02',
    'grey_roof_tiles'
)
# API の地図の名前 → ファイル名の接尾辞（Game とクックの一覧が <id>_<接尾辞>_4k.jpg の名前で読むため、合わない素材は失敗にする）
$mapSuffixes = @{ Diffuse = 'diff'; nor_dx = 'nor_dx'; Rough = 'rough'; AO = 'ao'; Displacement = 'disp' }
# API の地図の名前 → 使う解像度と形式。見本の区画は高さも使い、負荷用は使わない。
$swatchMaps = @('Diffuse', 'nor_dx', 'Rough', 'AO', 'Displacement')
$stressMaps = @('Diffuse', 'nor_dx', 'Rough', 'AO')
$targets = @($assetIds | ForEach-Object { [pscustomobject]@{ Id = $_; Maps = $swatchMaps } })
if ($StressSet)
{
    $targets += @($stressAssetIds | ForEach-Object { [pscustomobject]@{ Id = $_; Maps = $stressMaps } })
}
$resolution = '4k'
$format = 'jpg'

function Get-FileMd5([string]$Path)
{
    # 実行の仕方によっては Get-FileHash のモジュールが読めないため、.NET で求める
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try
    {
        return ([System.BitConverter]::ToString($md5.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $stream.Dispose()
        $md5.Dispose()
    }
}

$failures = New-Object System.Collections.Generic.List[string]
$downloadedBytes = 0L
$skippedCount = 0
$downloadedCount = 0

foreach ($target in $targets)
{
    $assetId = $target.Id
    try
    {
        $files = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$assetId" -UseBasicParsing
    }
    catch
    {
        $failures.Add("${assetId}: API の取得に失敗（$($_.Exception.Message)）")
        continue
    }

    $assetDirectory = Join-Path $destinationRoot $assetId
    New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null

    foreach ($map in $target.Maps)
    {
        $entry = $files.$map.$resolution.$format
        if ($null -eq $entry)
        {
            $failures.Add("${assetId}: $map の $resolution $format が API に無い")
            continue
        }

        $fileName = Split-Path -Leaf ([Uri]$entry.url).AbsolutePath
        $expectedFileName = "${assetId}_$($mapSuffixes[$map])_${resolution}.${format}"
        if ($fileName -ne $expectedFileName)
        {
            $failures.Add("${assetId}: $map のファイル名が想定と違う（期待 $expectedFileName、実際 $fileName）。別の素材に替える")
            continue
        }
        $destination = Join-Path $assetDirectory $fileName
        if (-not $Force -and (Test-Path -LiteralPath $destination) -and (Get-FileMd5 $destination) -eq $entry.md5)
        {
            $skippedCount++
            continue
        }

        $temporary = "$destination.download"
        try
        {
            Invoke-WebRequest -Uri $entry.url -OutFile $temporary -UseBasicParsing
        }
        catch
        {
            Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
            $failures.Add("${assetId}: $fileName を落とせない（$($_.Exception.Message)）")
            continue
        }

        $actualMd5 = Get-FileMd5 $temporary
        if ($actualMd5 -ne $entry.md5)
        {
            Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
            $failures.Add("${assetId}: $fileName の MD5 が合わない（期待 $($entry.md5)、実際 $actualMd5）")
            continue
        }

        Move-Item -LiteralPath $temporary -Destination $destination -Force
        $downloadedBytes += (Get-Item -LiteralPath $destination).Length
        $downloadedCount++
        Write-Output "FETCH_POLYHAVEN downloaded=$assetId/$fileName bytes=$((Get-Item -LiteralPath $destination).Length)"
    }
}

foreach ($failure in $failures)
{
    Write-Output "FETCH_POLYHAVEN failure: $failure"
}
$result = if ($failures.Count -eq 0) { 'pass' } else { 'fail' }
Write-Output ("FETCH_POLYHAVEN result={0} downloaded={1} skipped={2} downloaded_mb={3:N1} destination={4}" -f
    $result, $downloadedCount, $skippedCount, ($downloadedBytes / 1MB), $destinationRoot)
if ($failures.Count -ne 0)
{
    exit 1
}
Write-Output 'FETCH_POLYHAVEN next: 落としたテクスチャを BC に焼くには `cmake --build build --config RelWithDebInfo --target CookAssets` を実行する（build/CookedAssets/ へ差分クックする）'
exit 0
