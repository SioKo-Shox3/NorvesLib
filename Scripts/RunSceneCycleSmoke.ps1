# シーンの出入りを繰り返しても資源が増えないことを確かめる。
# Game を --scene-cycle-smoke=<id,...> --scene-cycle-count=<n> 付きで起動する。Game は何も置かないホームのモードと
# 指定のシーンの間を周回し、周回ごとに資源の数（材質・テクスチャ・メッシュ・Model・Entity）と GPU の割り当て量を
# Game.log へ出す。2周目以降の値が1周目と同じなら Game が SCENE_CYCLE_SMOKE result=pass を出して終了コード 0 で終わる。
# このスクリプトは Game.log を照合して結果を確かめ、pass のときだけ 0 を返す。
#
# 失敗にするもの（終了コード 1）: Game の起動失敗・終了コードが0でない・タイムアウト、SCENE_CYCLE_SMOKE の行が無い・fail、
# 周回のサンプル（SCENE_CYCLE_SAMPLE）の数が シーン数 × 周回数 と違う、Game.log のシェーダーのコンパイル失敗
# （Slang SDK 未設定の neural_material_decode.slang だけは既知として除外）、Debug の検証レイヤーのエラー。
# -Configuration Release はログが無効で Game.log を書かないため使えない（Debug か RelWithDebInfo を使う）。
# -Cycles は 2 以上（1周目と比べるため）。-OutDir を省くと .harness/runs/scene-cycle-smoke へ Game.log を残す。
# 成功すると SCENE_CYCLE_SMOKE_RUN result=pass を出す。
param(
    # シーンの id（カンマ区切りで1つの文字列でも、配列でもよい）
    [Parameter(Mandatory = $true)]
    [string[]]$Scenes,
    [ValidateRange(2, 1000)]
    [int]$Cycles = 3,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [string]$OutDir = '',
    [ValidateRange(10, 7200)]
    [int]$TimeoutSeconds = 900,
    # 既知のスレッド競合の検証エラー（UNASSIGNED-Threading-MultipleThreads）も失敗にする。付けないときは警告として数える
    [switch]$StrictValidation
)

$ErrorActionPreference = 'Stop'

$sceneIds = @($Scenes | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
if ($sceneIds.Count -eq 0)
{
    Write-Output 'SCENE_CYCLE_SMOKE_RUN result=fail reason=no_scene'
    exit 1
}
foreach ($sceneId in $sceneIds)
{
    if ($sceneId -notmatch '^[a-z0-9][a-z0-9-]*$')
    {
        Write-Output "SCENE_CYCLE_SMOKE_RUN result=fail reason=invalid_scene value=$sceneId"
        exit 1
    }
}
if (@($sceneIds | Select-Object -Unique).Count -ne $sceneIds.Count)
{
    Write-Output 'SCENE_CYCLE_SMOKE_RUN result=fail reason=duplicate_scene'
    exit 1
}
if ($Configuration -eq 'Release')
{
    Write-Output 'SCENE_CYCLE_SMOKE_RUN result=fail reason=release_has_no_log hint=use_Debug_or_RelWithDebInfo'
    exit 1
}

# Slang SDK を組み込んでいないビルドで neural_material_decode.slang が読めない場合だけを許す。
function Test-AllowedShaderFailure([string]$Line)
{
    return $Line -match 'Failed to compile shader \[neural_material_decode\.slang\]: Slang SDK not available\.'
}

function Stop-OwnedProcessTree([int]$ProcessId)
{
    & taskkill.exe /PID $ProcessId /T /F 2>&1 | Out-Null
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$gamePath = Join-Path $repoRoot "build\Game\$Configuration\Game.exe"
if (-not (Test-Path -LiteralPath $gamePath))
{
    Write-Output "SCENE_CYCLE_SMOKE_RUN result=fail reason=game_missing path=$gamePath"
    exit 1
}
if ($OutDir -eq '')
{
    $OutDir = Join-Path $repoRoot '.harness\runs\scene-cycle-smoke'
}
$outRoot = if ([IO.Path]::IsPathRooted($OutDir)) { [IO.Path]::GetFullPath($OutDir) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir)) }
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$gameLogPath = Join-Path $repoRoot 'Game.log'
$runLogPath = Join-Path $outRoot 'Game.log'
$validationLogPath = Join-Path $outRoot 'Validation.log'
foreach ($stale in @($gameLogPath, $runLogPath, $validationLogPath))
{
    Remove-Item -LiteralPath $stale -Force -ErrorAction SilentlyContinue
}

$sceneList = $sceneIds -join ','
$arguments = @("--scene-cycle-smoke=$sceneList", "--scene-cycle-count=$Cycles")

# Vulkan の検証レイヤーのメッセージ（VUID）は Game.log に入らないので、検証レイヤー自身のログ出力をファイルへ向けて
# エラーを数える（Debug だけ。他は検証レイヤーが無効）。起動した Game だけが受け継ぐよう、起動の直後に戻す。
$previousValidationSettings = @{}
foreach ($validationVariable in @('VK_KHRONOS_VALIDATION_DEBUG_ACTION', 'VK_KHRONOS_VALIDATION_LOG_FILENAME', 'VK_KHRONOS_VALIDATION_REPORT_FLAGS'))
{
    $previousValidationSettings[$validationVariable] = [Environment]::GetEnvironmentVariable($validationVariable)
}
if ($Configuration -eq 'Debug')
{
    $env:VK_KHRONOS_VALIDATION_DEBUG_ACTION = 'VK_DBG_LAYER_ACTION_LOG_MSG'
    $env:VK_KHRONOS_VALIDATION_LOG_FILENAME = $validationLogPath
    $env:VK_KHRONOS_VALIDATION_REPORT_FLAGS = 'error,warn'
}
# アセットは作業ディレクトリからの相対パスで読むため、リポジトリのルートで起動する。
try
{
    $process = Start-Process -FilePath $gamePath -ArgumentList $arguments -WorkingDirectory $repoRoot -PassThru
}
finally
{
    foreach ($validationVariable in $previousValidationSettings.Keys)
    {
        [Environment]::SetEnvironmentVariable($validationVariable, $previousValidationSettings[$validationVariable])
    }
}
[void]$process.Handle

$failures = @()
$exitCode = $null
if ($process.WaitForExit($TimeoutSeconds * 1000))
{
    $process.WaitForExit()
    $exitCode = [int]$process.ExitCode
}
else
{
    Stop-OwnedProcessTree $process.Id
    $failures += "timeout（$TimeoutSeconds 秒で終わらなかった）"
}
if ($null -ne $exitCode -and $exitCode -ne 0)
{
    $failures += "Game の終了コードが $exitCode"
}

if (Test-Path -LiteralPath $gameLogPath)
{
    Copy-Item -LiteralPath $gameLogPath -Destination $runLogPath -Force
}
$samples = @()
if (Test-Path -LiteralPath $runLogPath)
{
    $resultLines = @(Select-String -LiteralPath $runLogPath -Pattern 'SCENE_CYCLE_SMOKE result=')
    if ($resultLines.Count -eq 0)
    {
        $failures += 'Game.log に SCENE_CYCLE_SMOKE の結果の行が無い'
    }
    elseif ($resultLines[-1].Line -notmatch 'SCENE_CYCLE_SMOKE result=pass')
    {
        $failures += "Game.log の結果が pass でない: $($resultLines[-1].Line.Trim())"
        foreach ($diffLine in @(Select-String -LiteralPath $runLogPath -Pattern 'SCENE_CYCLE_DIFF|SCENE_CYCLE_UNSETTLED'))
        {
            $failures += $diffLine.Line.Trim()
        }
    }

    $samples = @(Select-String -LiteralPath $runLogPath -Pattern 'SCENE_CYCLE_SAMPLE')
    $expectedSamples = $sceneIds.Count * $Cycles
    if ($samples.Count -ne $expectedSamples)
    {
        $failures += "SCENE_CYCLE_SAMPLE が $($samples.Count) 件（期待 $expectedSamples 件 = シーン $($sceneIds.Count) × 周回 $Cycles）"
    }

    $shaderFailures = @(Select-String -LiteralPath $runLogPath -Pattern 'Failed to compile shader' -SimpleMatch |
        Where-Object { -not (Test-AllowedShaderFailure $_.Line) })
    foreach ($line in $shaderFailures)
    {
        $failures += $line.Line.Trim()
    }
    foreach ($line in @(Select-String -LiteralPath $runLogPath -Pattern 'SCENE_UNKNOWN|SCENE_CATALOG_INVALID'))
    {
        $failures += $line.Line.Trim()
    }
}
else
{
    $failures += 'Game.log が無い'
}

# 検証レイヤーのログ。"Validation Error:" の見出しの行を数え、1 件でもあれば失敗にする（解放した資源を GPU が使っていれば出る）。
# Debug でログのファイルが無いときは、検証レイヤーがログの設定を読んでいない（落ちようのない合格になる）ので失敗にする。
if (Test-Path -LiteralPath $validationLogPath)
{
    $validationLines = @(Get-Content -LiteralPath $validationLogPath -Encoding Default | Where-Object { $_ -match '\S' })
    $errorLines = @($validationLines | Where-Object { $_ -match 'Validation Error:' })
    # 2周目以降の Enter は RenderThread が描いている最中に GameThread が CreateTexture の初期データの転送
    # （GpuResourceStore::UploadTextureData が共有のコマンドプールからコマンドリストを作る）を行うため、
    # 同じコマンドプールへのスレッド競合が出る。解放した資源の使用を示す他の検証エラーと区別して警告にする。
    $threadingErrors = @($errorLines | Where-Object { $_ -match 'UNASSIGNED-Threading-MultipleThreads' })
    $otherErrors = @($errorLines | Where-Object { $_ -notmatch 'UNASSIGNED-Threading-MultipleThreads' })
    if ($otherErrors.Count -gt 0)
    {
        $failures += "検証レイヤーのエラーが $($otherErrors.Count) 件ある（Validation.log。先頭: $($otherErrors[0]))"
    }
    if ($threadingErrors.Count -gt 0)
    {
        if ($StrictValidation)
        {
            $failures += "検証レイヤーのスレッド競合のエラーが $($threadingErrors.Count) 件ある（Validation.log）"
        }
        else
        {
            Write-Output "SCENE_CYCLE_SMOKE_RUN warning: 検証レイヤーのスレッド競合のエラーが $($threadingErrors.Count) 件ある（Validation.log。既知。-StrictValidation で失敗にする）"
        }
    }
}
elseif ($Configuration -eq 'Debug')
{
    $failures += '検証レイヤーのログ（Validation.log）が無い（検証レイヤーがログの設定を読んでいない）'
}

foreach ($sample in $samples)
{
    Write-Output $sample.Line.Trim()
}
if ($failures.Count -gt 0)
{
    foreach ($failure in $failures)
    {
        Write-Output "SCENE_CYCLE_SMOKE_RUN failure: $failure"
    }
    Write-Output "SCENE_CYCLE_SMOKE_RUN result=fail failures=$($failures.Count) log=$runLogPath"
    exit 1
}
Write-Output "SCENE_CYCLE_SMOKE_RUN result=pass scenes=$sceneList cycles=$Cycles log=$runLogPath"
exit 0
