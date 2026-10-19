# 回帰の撮影: ランチャーに載ったシーンの既定の視点を撮り、変更の前（before）と後（after）を並べる。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item <ID> -Phase before|after [-Motion]
# 撮るシーン: rendering3dtest・mega-bananas・sandbox と、ランチャーに載っていれば night-village（Game/Scenes/SceneCatalog.h に
# シーンの名前があるか見る）。どれも既定の視点 overview を Debug で撮る（検証レイヤーが入るのは Debug だけ。Core の BootConfig.h）。
# 最初のシーンだけ -Build し、以後は同じ Game.exe で撮る。
# -Motion を付けると、rendering3dtest をカメラ台本 Scripts/CameraScripts/rendering3dtest-pan.txt で撮る組も足す。
# 出力は .harness/runs/regression/<ID>/<before|after>/<シーン>/（CaptureScene.ps1 の出力）。after は撮り終えてから
# ReferenceCheck.py --regression で before と並べ、撮影の欠け・真っ黒・検証レイヤーのエラーがあれば失敗にする
# （差の PSNR・平均の差は <after>/regression_check.json に記録する）。全体で 9 分を超えたら失敗にする。
param(
    [Parameter(Mandatory = $true)][string]$Item,
    [Parameter(Mandatory = $true)][ValidateSet('before', 'after')][string]$Phase,
    [switch]$Motion
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repoRoot
$limitSeconds = 540
$root = ".harness/runs/regression/$Item"
$phaseDir = "$root/$Phase"
$motionScript = 'Scripts/CameraScripts/rendering3dtest-pan.txt'
$sw = [Diagnostics.Stopwatch]::StartNew()
$allOk = $true

$catalog = Get-Content -LiteralPath 'Game/Scenes/SceneCatalog.h' -Raw -Encoding UTF8
$scenes = @('rendering3dtest', 'mega-bananas', 'sandbox')
if ($catalog -match '"night-village"') { $scenes += 'night-village' }
$shots = @($scenes | ForEach-Object { @{ name = $_; scene = $_; args = @() } })
if ($Motion) { $shots += @{ name = 'rendering3dtest-motion'; scene = 'rendering3dtest'; args = @("camera-script=$motionScript") } }

if (Test-Path -LiteralPath $phaseDir) { Remove-Item -LiteralPath $phaseDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $phaseDir | Out-Null
$log = "$phaseDir/capture.txt"
"start $(Get-Date -Format o) item=$Item phase=$Phase scenes=$($scenes -join ',') motion=$([int][bool]$Motion)" | Set-Content $log -Encoding utf8

$first = $true
foreach ($shot in $shots)
{
    $outDir = "$phaseDir/$($shot.name)"
    $capture = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'Scripts/CaptureScene.ps1', '-Scene', $shot.scene,
        '-Configuration', 'Debug', '-RequireAssets', '-Views', 'overview', '-OutDir', $outDir)
    if ($first) { $capture += '-Build' }
    if ($shot.args.Count -gt 0) { $capture += '-GameArgs'; $capture += ($shot.args -join ',') }
    $stepWatch = [Diagnostics.Stopwatch]::StartNew()
    # CaptureScene の標準エラーで止めない（終わりの値で判定する）
    $ErrorActionPreference = 'Continue'
    & powershell @capture *>> "$phaseDir/$($shot.name).capture.log"
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    "$($shot.name) exit=$code sec=$([int]$stepWatch.Elapsed.TotalSeconds)" | Add-Content $log -Encoding utf8
    Write-Output "CAPTURE_REGRESSION shot=$($shot.name) exit=$code sec=$([int]$stepWatch.Elapsed.TotalSeconds)"
    if ($code -ne 0) { $allOk = $false }
    $first = $false
}

$exitCode = if ($allOk) { 0 } else { 1 }
if ($Phase -eq 'after')
{
    $ErrorActionPreference = 'Continue'
    & python Scripts/Terrain/ReferenceCheck.py --regression --capture $phaseDir --reference "$root/before" --verify 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath "$phaseDir/regression.txt"
    $checkCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($checkCode -ne 0) { $exitCode = 1 }
}
$seconds = [int]$sw.Elapsed.TotalSeconds
if ($seconds -gt $limitSeconds)
{
    Write-Output "CAPTURE_REGRESSION failure: 全体で $seconds 秒かかり、上限 $limitSeconds 秒を超えた"
    $exitCode = 1
}
"end $(Get-Date -Format o) sec=$seconds all_ok=$([int]$allOk) exit=$exitCode" | Add-Content $log -Encoding utf8
Write-Output "CAPTURE_REGRESSION result=$(if ($exitCode -eq 0) { 'pass' } else { 'fail' }) item=$Item phase=$Phase sec=$seconds dir=$phaseDir"
exit $exitCode
