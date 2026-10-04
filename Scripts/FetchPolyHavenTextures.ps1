# 起動画面（Rendering3DTest）の地面の見本の区画に使う Poly Haven（CC0）のテクスチャを
# Assets/Textures/PolyHaven/<id>/ へ落とす。保存先は git の管理外（.gitignore）で、ファイルが無い区画は
# 起動画面で石畳のまま描く。
#
# 各素材の色（diff）・法線（nor_dx。このエンジンの余接フレームは DirectX の向き）・粗さ（rough）・
# AO（ao）・高さ（disp）を 4K の JPG で取る。ファイルの URL・大きさ・MD5 は Poly Haven の API
# （https://api.polyhaven.com/files/<id>）から引き、既にあって MD5 が一致するファイルは落とし直さない。
# 素材の並びは Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp の地面の区画の表と同じにする。
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
# API の地図の名前 → 使う解像度と形式
$maps = @('Diffuse', 'nor_dx', 'Rough', 'AO', 'Displacement')
$resolution = '4k'
$format = 'jpg'

function Get-FileMd5([string]$Path)
{
    return (Get-FileHash -LiteralPath $Path -Algorithm MD5).Hash.ToLowerInvariant()
}

$failures = New-Object System.Collections.Generic.List[string]
$downloadedBytes = 0L
$skippedCount = 0
$downloadedCount = 0

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

    $assetDirectory = Join-Path $destinationRoot $assetId
    New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null

    foreach ($map in $maps)
    {
        $entry = $files.$map.$resolution.$format
        if ($null -eq $entry)
        {
            $failures.Add("${assetId}: $map の $resolution $format が API に無い")
            continue
        }

        $fileName = Split-Path -Leaf ([Uri]$entry.url).AbsolutePath
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
