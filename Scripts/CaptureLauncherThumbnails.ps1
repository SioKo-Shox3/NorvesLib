# シーン一覧の見本用の視点を撮り、ランチャーのカード用の PNG に整えて保存する。
# -ListOnly は撮影せず、対象のシーン・視点・リポジトリからの出力先だけを表示する。
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WorkDir,
    [string[]]$Scenes = @(),
    [ValidateRange(1, 32768)]
    [int]$Width = 640,
    [ValidateRange(1, 32768)]
    [int]$Height = 360,
    [switch]$ListOnly,
    [switch]$Build
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$catalogPath = Join-Path $repoRoot 'Game/Scenes/SceneCatalog.h'
$catalogText = [IO.File]::ReadAllText($catalogPath, [Text.Encoding]::UTF8)
$pattern = '"Assets/Textures/Launcher/(?<id>[a-z0-9-]+)\.png"\s*,\s*(?:"(?<view>[A-Za-z0-9_-]+)"|nullptr)'
$catalogMatches = [regex]::Matches($catalogText, $pattern)
if ($catalogMatches.Count -eq 0)
{
    Write-Host 'シーン一覧から見本画像の設定を読み取れませんでした。'
    exit 1
}

$catalog = @(foreach ($match in $catalogMatches)
{
    $id = $match.Groups['id'].Value
    $view = if ($match.Groups['view'].Success) { $match.Groups['view'].Value } else { 'overview' }
    [pscustomobject]@{
        Id = $id
        View = $view
        ThumbnailPath = "Assets/Textures/Launcher/$id.png"
    }
})
# -File でもカンマ区切りの指定を受け付ける。
$requestedIds = @($Scenes | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' } | Select-Object -Unique)
$unknownIds = @($requestedIds | Where-Object { $catalog.Id -notcontains $_ })
$selected = if ($requestedIds.Count -gt 0) { @($catalog | Where-Object { $requestedIds -contains $_.Id }) } else { @($catalog) }

if ($ListOnly)
{
    foreach ($scene in $selected)
    {
        Write-Output "$($scene.Id) $($scene.View) $($scene.ThumbnailPath)"
    }
    foreach ($id in $unknownIds) { Write-Host "一覧にないシーンです: $id" }
    if ($unknownIds.Count -gt 0) { exit 1 }
    exit 0
}

$workRoot = if ([IO.Path]::IsPathRooted($WorkDir)) { [IO.Path]::GetFullPath($WorkDir) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $WorkDir)) }
$captureScript = Join-Path $repoRoot 'Scripts/CaptureScene.ps1'
$results = @()
foreach ($id in $unknownIds)
{
    $results += [pscustomobject]@{ Id = $id; Success = $false; Detail = '一覧にないシーンです' }
}

$buildPending = [bool]$Build
foreach ($scene in $selected)
{
    try
    {
        $captureDir = Join-Path $workRoot $scene.Id
        $captureArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $captureScript,
            '-Scene', $scene.Id, '-Views', $scene.View, '-Configuration', $Configuration, '-OutDir', $captureDir)
        if ($buildPending) { $captureArguments += '-Build' }
        $buildConfirmed = -not $buildPending
        Write-Host "撮影中: $($scene.Id) / $($scene.View)"
        # ビルド成功の印を受け取れない場合は、古い実行ファイルで撮らないよう次のシーンでもビルドする。
        & powershell.exe @captureArguments | ForEach-Object {
            if ($_ -match '^CAPTURE_SCENE build result=pass\b') { $buildConfirmed = $true }
            Write-Output $_
        }
        $captureExitCode = $LASTEXITCODE
        if ($buildConfirmed) { $buildPending = $false }
        if ($captureExitCode -ne 0) { throw "撮影に失敗しました（終了コード $captureExitCode）" }

        $sourcePath = Join-Path $captureDir "$($scene.View).png"
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "撮影した画像がありません: $sourcePath" }
        $destinationPath = Join-Path $repoRoot $scene.ThumbnailPath
        $destinationDir = Split-Path -Parent $destinationPath
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
        Add-Type -AssemblyName System.Drawing
        $source = $null
        $thumbnail = $null
        $graphics = $null
        try
        {
            $source = [System.Drawing.Image]::FromFile($sourcePath)
            $thumbnail = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
            $graphics = [System.Drawing.Graphics]::FromImage($thumbnail)
            $graphics.Clear([System.Drawing.Color]::Black)
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            # 縦横比を保って出力を埋め、余る部分を左右または上下から均等に切る。
            $scale = [Math]::Max($Width / [double]$source.Width, $Height / [double]$source.Height)
            $cropWidth = [single]($Width / $scale)
            $cropHeight = [single]($Height / $scale)
            $cropX = [single](($source.Width - $cropWidth) / 2.0)
            $cropY = [single](($source.Height - $cropHeight) / 2.0)
            $rectangle = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
            $graphics.DrawImage($source, $rectangle, $cropX, $cropY, $cropWidth, $cropHeight, [System.Drawing.GraphicsUnit]::Pixel)
            $graphics.Dispose()
            $graphics = $null
            $thumbnail.Save($destinationPath, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally
        {
            if ($null -ne $graphics) { $graphics.Dispose() }
            if ($null -ne $thumbnail) { $thumbnail.Dispose() }
            if ($null -ne $source) { $source.Dispose() }
        }
        $results += [pscustomobject]@{ Id = $scene.Id; Success = $true; Detail = $scene.ThumbnailPath }
    }
    catch
    {
        $results += [pscustomobject]@{ Id = $scene.Id; Success = $false; Detail = $_.Exception.Message }
    }
}

Write-Host '撮影結果:'
foreach ($result in $results)
{
    $status = if ($result.Success) { '成功' } else { '失敗' }
    Write-Host "$status $($result.Id): $($result.Detail)"
}
if (@($results | Where-Object { -not $_.Success }).Count -gt 0) { exit 1 }
exit 0
