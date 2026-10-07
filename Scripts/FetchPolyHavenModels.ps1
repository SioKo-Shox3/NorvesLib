# 起動画面（Rendering3DTest）の地面の外周に並べる高ポリのスキャン資産（Poly Haven、CC0 のモデル）を
# Assets/Models/PolyHaven/<id>/ へ落とす。保存先は git の管理外（.gitignore）で、ファイルが無い資産は
# 起動画面に置かない（Game が警告して飛ばす）。
#
# 各資産の glTF（4K 版）と、それが読む .bin・テクスチャ（色 diff・法線 nor_gl・AO/粗さ/金属 arm の 4K JPG）を取る。
# URL・大きさ・MD5 は Poly Haven の API（https://api.polyhaven.com/files/<id>）から引き、既にあって MD5 が
# 一致するファイルは落とし直さない。資産の並びは Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp の
# kStartupScanProps と Assets/AssetSets/Rendering3DTestStartupScanProps.json と同じにする。
#
# 終了コード: すべてそろえば0、どれかを落とせない・MD5が合わなければ1。
[CmdletBinding()]
param(
    # 既にあるファイルも落とし直す
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repoRoot = Split-Path -Parent $PSScriptRoot
$destinationRoot = Join-Path $repoRoot 'Assets\Models\PolyHaven'

# 起動画面に並べるスキャン資産（Poly Haven の資産ID）
$assetIds = @(
    'coast_rocks_05',
    'sand_rocks_small_01',
    'coast_land_rocks_03'
)
$resolution = '4k'

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

# 1 ファイルを取る。既にあって MD5 が合えば飛ばす。落とせない・MD5 が合わないときは失敗へ積む。
function Save-ModelFile([string]$AssetId, [string]$RelativePath, [string]$Url, [string]$ExpectedMd5, [string]$AssetDirectory)
{
    $destination = Join-Path $AssetDirectory ($RelativePath -replace '/', '\')
    if (-not $Force -and (Test-Path -LiteralPath $destination) -and (Get-FileMd5 $destination) -eq $ExpectedMd5)
    {
        $script:skippedCount++
        return
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    $temporary = "$destination.download"
    try
    {
        Invoke-WebRequest -Uri $Url -OutFile $temporary -UseBasicParsing
    }
    catch
    {
        Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
        $script:failures.Add("${AssetId}: $RelativePath を落とせない（$($_.Exception.Message)）")
        return
    }

    $actualMd5 = Get-FileMd5 $temporary
    if ($actualMd5 -ne $ExpectedMd5)
    {
        Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
        $script:failures.Add("${AssetId}: $RelativePath の MD5 が合わない（期待 $ExpectedMd5、実際 $actualMd5）")
        return
    }

    Move-Item -LiteralPath $temporary -Destination $destination -Force
    $length = (Get-Item -LiteralPath $destination).Length
    $script:downloadedBytes += $length
    $script:downloadedCount++
    Write-Output "FETCH_POLYHAVEN_MODEL downloaded=$AssetId/$RelativePath bytes=$length"
}

foreach ($assetId in $assetIds)
{
    try
    {
        $files = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$assetId" -UseBasicParsing
    }
    catch
    {
        $failures.Add("${assetId}: API の取得に失敗（$($_.Exception.Message)）")
        continue
    }

    $entry = $files.gltf.$resolution.gltf
    if ($null -eq $entry)
    {
        $failures.Add("${assetId}: gltf の $resolution が API に無い")
        continue
    }

    $gltfName = Split-Path -Leaf ([Uri]$entry.url).AbsolutePath
    $expectedGltfName = "${assetId}_${resolution}.gltf"
    if ($gltfName -ne $expectedGltfName)
    {
        $failures.Add("${assetId}: glTF のファイル名が想定と違う（期待 $expectedGltfName、実際 $gltfName）。別の資産に替える")
        continue
    }

    $assetDirectory = Join-Path $destinationRoot $assetId
    Save-ModelFile $assetId $gltfName $entry.url $entry.md5 $assetDirectory
    foreach ($include in $entry.include.PSObject.Properties)
    {
        Save-ModelFile $assetId $include.Name $include.Value.url $include.Value.md5 $assetDirectory
    }
}

foreach ($failure in $failures)
{
    Write-Output "FETCH_POLYHAVEN_MODEL failure: $failure"
}
$result = if ($failures.Count -eq 0) { 'pass' } else { 'fail' }
Write-Output ("FETCH_POLYHAVEN_MODEL result={0} downloaded={1} skipped={2} downloaded_mb={3:N1} destination={4}" -f
    $result, $downloadedCount, $skippedCount, ($downloadedBytes / 1MB), $destinationRoot)
if ($failures.Count -ne 0)
{
    exit 1
}
Write-Output 'FETCH_POLYHAVEN_MODEL next: 落としたモデルを NVMESH v1・BC・VT に焼くには `cmake --build build --config RelWithDebInfo --target CookAssets` を実行する（build/CookedAssets/ へ差分クックする）'
exit 0
