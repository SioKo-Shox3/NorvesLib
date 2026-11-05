# 最後のセーブからのリセットを繰り返し、資源の数・進行の復元・検証レイヤーを確かめる。
[CmdletBinding()]
param(
    [int]$SoakCount = 5,
    [ValidateSet('Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [ValidateRange(1, 2147483)]
    [int]$TimeoutSeconds = 300,
    [string]$EvidencePath
)

$ErrorActionPreference = 'Stop'

function Assert-GameReset([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw "リセットの検査に失敗しました: $Message"
    }
}

function Get-GameResetDescendantProcessIds([int]$RootProcessId)
{
    $processTable = @(Get-CimInstance Win32_Process -ErrorAction Stop | Select-Object ProcessId, ParentProcessId)
    $pending = [Collections.Generic.Queue[int]]::new()
    $descendants = [Collections.Generic.List[int]]::new()
    $pending.Enqueue($RootProcessId)
    while ($pending.Count -gt 0)
    {
        $parentId = $pending.Dequeue()
        foreach ($candidate in $processTable)
        {
            if ([int]$candidate.ParentProcessId -eq $parentId)
            {
                $childId = [int]$candidate.ProcessId
                $descendants.Add($childId)
                $pending.Enqueue($childId)
            }
        }
    }
    return $descendants.ToArray()
}

function Read-GameResetFields([string]$Line)
{
    $fields = @{}
    $marker = [regex]::Match($Line, '\bGAME_RESET\s+(?<fields>.*)$')
    foreach ($pair in [regex]::Matches($marker.Groups['fields'].Value, '(?:^|\s)(?<key>[a-z]+)=(?<value>\S+)'))
    {
        $key = $pair.Groups['key'].Value
        Assert-GameReset (-not $fields.ContainsKey($key)) "ログのキーが重複しています: $Line"
        $fields[$key] = $pair.Groups['value'].Value
    }
    return $fields
}

function Read-GameResetNumber([hashtable]$Fields, [string]$Key, [string]$Line)
{
    Assert-GameReset ($Fields.ContainsKey($Key) -and $Fields[$Key] -cmatch '^\d+$') "ログに非負の整数がありません: key=$Key 行=$Line"
    return [uint64]::Parse($Fields[$Key], [Globalization.CultureInfo]::InvariantCulture)
}

$process = $null
$gameLogPath = $null
$runLogPath = $null
try
{
    Assert-GameReset ($SoakCount -ge 2) 'SoakCount は 2 以上を指定してください'
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $gamePath = Join-Path $repoRoot "build\Game\$Configuration\Game.exe"
    Assert-GameReset (Test-Path -LiteralPath $gamePath -PathType Leaf) "Game.exe がありません: $gamePath"
    if ([string]::IsNullOrWhiteSpace($EvidencePath))
    {
        $EvidencePath = Join-Path $repoRoot ".harness\runs\game-reset\$([guid]::NewGuid().ToString('N'))"
    }
    elseif (-not [IO.Path]::IsPathRooted($EvidencePath))
    {
        $EvidencePath = Join-Path $repoRoot $EvidencePath
    }
    $EvidencePath = [IO.Path]::GetFullPath($EvidencePath)
    # 既存の証跡やセーブは消さず、毎回空の保存先から始める。
    if (Test-Path -LiteralPath $EvidencePath)
    {
        Assert-GameReset (Test-Path -LiteralPath $EvidencePath -PathType Container) "証跡の保存先がディレクトリではありません: $EvidencePath"
        Assert-GameReset (@(Get-ChildItem -LiteralPath $EvidencePath -Force).Count -eq 0) "証跡の保存先が空ではありません: $EvidencePath"
    }
    else
    {
        New-Item -ItemType Directory -Path $EvidencePath | Out-Null
    }
    $savePath = Join-Path $EvidencePath 'save'
    New-Item -ItemType Directory -Path $savePath | Out-Null
    $stdoutPath = Join-Path $EvidencePath 'stdout.txt'
    $stderrPath = Join-Path $EvidencePath 'stderr.txt'
    $validationLogPath = Join-Path $EvidencePath 'Validation.log'
    $gameLogPath = Join-Path $repoRoot 'Game.log'
    $runLogPath = Join-Path $EvidencePath 'Game.log'
    if (Test-Path -LiteralPath $gameLogPath)
    {
        Remove-Item -LiteralPath $gameLogPath -Force
    }
    $arguments = @("--soak-reset=$SoakCount", "--save-dir=$savePath", '--window-background', '--validation-layer')
    # Start-Process が結合する引数を引用し、空白を含む保存先も一つの引数で渡す。
    $quotedArguments = foreach ($argument in $arguments)
    {
        Assert-GameReset (-not $argument.Contains('"')) "引数に引用符を含められません: $argument"
        '"' + $argument + '"'
    }

    # CaptureScene.ps1 と同じ設定で、検証レイヤー自身のログを起動ごとに残す。
    $previousValidationSettings = @{}
    foreach ($name in @('VK_KHRONOS_VALIDATION_DEBUG_ACTION', 'VK_KHRONOS_VALIDATION_LOG_FILENAME', 'VK_KHRONOS_VALIDATION_REPORT_FLAGS'))
    {
        $previousValidationSettings[$name] = [Environment]::GetEnvironmentVariable($name)
    }
    try
    {
        $env:VK_KHRONOS_VALIDATION_DEBUG_ACTION = 'VK_DBG_LAYER_ACTION_LOG_MSG'
        $env:VK_KHRONOS_VALIDATION_LOG_FILENAME = $validationLogPath
        $env:VK_KHRONOS_VALIDATION_REPORT_FLAGS = 'error,warn'
        # 相対指定のアセットを読むため、既存の撮影スクリプトと同じルートで起動する。
        $process = Start-Process -FilePath $gamePath -ArgumentList $quotedArguments -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        [void]$process.Handle
    }
    finally
    {
        foreach ($name in $previousValidationSettings.Keys)
        {
            [Environment]::SetEnvironmentVariable($name, $previousValidationSettings[$name])
        }
    }
    $rootProcessId = [int]$process.Id
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        $ownedProcessIds = @($rootProcessId)
        try
        {
            $ownedProcessIds += @(Get-GameResetDescendantProcessIds $rootProcessId)
        }
        finally
        {
            # 列挙に失敗しても、この起動で所有するプロセスツリーを停止する。
            $taskkillOutput = & taskkill.exe /PID $rootProcessId /T /F 2>&1
            $taskkillExitCode = $LASTEXITCODE
            $rootStopped = $process.WaitForExit(5000)
        }
        $process.Refresh()
        $remainingOwnedProcessIds = @($ownedProcessIds | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
        Assert-GameReset ($taskkillExitCode -eq 0 -and $rootStopped -and $process.HasExited -and $remainingOwnedProcessIds.Count -eq 0) "時間切れの後始末が完了していません: pid=$rootProcessId remaining_owned=$($remainingOwnedProcessIds -join ',') 詳細=$($taskkillOutput -join ' ')"
        throw "Game が時間切れになりました: 秒数=$TimeoutSeconds pid=$rootProcessId root_alive=0 remaining_owned=0"
    }
    $process.Refresh()
    $remainingOwnedProcessIds = @(Get-GameResetDescendantProcessIds $rootProcessId | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
    Assert-GameReset ($remainingOwnedProcessIds.Count -eq 0) "Game の子プロセスが残っています: $($remainingOwnedProcessIds -join ',')"
    Assert-GameReset ($process.ExitCode -eq 0) "Game の終了コードが 0 ではありません: exit=$($process.ExitCode)"
    Assert-GameReset (Test-Path -LiteralPath $gameLogPath -PathType Leaf) 'Game.log がありません'
    Copy-Item -LiteralPath $gameLogPath -Destination $runLogPath
    $log = [IO.File]::ReadAllText($runLogPath)
    Assert-GameReset (-not [regex]::IsMatch($log, '\bGAME_RESET\s+stage=failure(?=\s|$)')) 'GAME_RESET stage=failure があります'

    $samples = @{ enter = @{}; ready = @{} }
    $restores = @{}
    $doneCount = 0
    foreach ($line in ($log -split '\r?\n'))
    {
        if ($line -notmatch '\bGAME_RESET\s+') { continue }
        # failure の診断には stage が二つある場合があるため、上で先に拒否する。
        $fields = Read-GameResetFields $line
        switch -CaseSensitive ($fields['stage'])
        {
            { $_ -ceq 'enter' -or $_ -ceq 'ready' }
            {
                $index = Read-GameResetNumber $fields 'index' $line
                Assert-GameReset ($index -le $SoakCount) "範囲外のサンプルです: $line"
                $stage = $fields['stage']
                Assert-GameReset (-not $samples[$stage].ContainsKey($index)) "サンプルが重複しています: $line"
                $counts = @{}
                foreach ($key in @('roots', 'textures', 'meshes', 'models', 'pending', 'voices'))
                {
                    $counts[$key] = Read-GameResetNumber $fields $key $line
                }
                $samples[$stage][$index] = $counts
            }
            'restore'
            {
                $index = Read-GameResetNumber $fields 'reset' $line
                Assert-GameReset ($index -ge 1 -and $index -le $SoakCount) "範囲外の復元です: $line"
                Assert-GameReset (-not $restores.ContainsKey($index)) "復元の行が重複しています: $line"
                Assert-GameReset ($fields['defeated'] -ceq '1') "撃破状態が復元されていません: $line"
                $restores[$index] = $true
            }
            'done'
            {
                $resets = Read-GameResetNumber $fields 'resets' $line
                Assert-GameReset ($fields['result'] -ceq 'pass' -and $resets -eq $SoakCount) "完了の行が一致しません: $line"
                ++$doneCount
            }
        }
    }
    Assert-GameReset ($doneCount -eq 1) "完了の行の数が不正です: count=$doneCount"
    foreach ($stage in @('enter', 'ready'))
    {
        for ($index = 0; $index -le $SoakCount; ++$index)
        {
            $sampleIndex = [uint64]$index
            Assert-GameReset ($samples[$stage].ContainsKey($sampleIndex)) "サンプルがありません: stage=$stage index=$index"
            if ($index -lt 2) { continue }
            # StopOwner は最短 2/60 秒のフェードがあるので、enter の voices は比較しない。
            $keys = @('roots', 'textures', 'meshes', 'models', 'pending')
            if ($stage -ceq 'ready') { $keys += 'voices' }
            foreach ($key in $keys)
            {
                $baseline = $samples[$stage][[uint64]1][$key]
                $actual = $samples[$stage][$sampleIndex][$key]
                Assert-GameReset ($actual -eq $baseline) "基準の数と違います: stage=$stage index=$index key=$key baseline=$baseline actual=$actual"
            }
        }
    }
    for ($index = 1; $index -le $SoakCount; ++$index)
    {
        Assert-GameReset ($restores.ContainsKey([uint64]$index)) "復元の行がありません: reset=$index"
    }
    $saveOverride = [regex]::Match($log, '(?m)\bSAVE_DIR override=(?<path>[^\r\n]+)')
    Assert-GameReset $saveOverride.Success 'SAVE_DIR override の行がありません'
    $actualSavePath = [IO.Path]::GetFullPath($saveOverride.Groups['path'].Value.Trim()).TrimEnd('\', '/')
    $expectedSavePath = [IO.Path]::GetFullPath((Join-Path $savePath 'NorvesLib\Game')).TrimEnd('\', '/')
    Assert-GameReset ($actualSavePath -ieq $expectedSavePath) "セーブの保存先が違います: expected=$expectedSavePath actual=$actualSavePath"
    Assert-GameReset ($log -match 'VALIDATION_LAYER enabled layer=VK_LAYER_KHRONOS_validation') '検証レイヤーが有効になった記録がありません'
    Assert-GameReset (Test-Path -LiteralPath $validationLogPath -PathType Leaf) '検証レイヤーの Validation.log がありません'
    $transcript = $log
    foreach ($path in @($stdoutPath, $stderrPath, $validationLogPath))
    {
        $transcript += "`n" + [IO.File]::ReadAllText($path)
    }
    $vuidCount = [regex]::Matches($transcript, 'VUID-').Count
    Assert-GameReset ($vuidCount -eq 0) "Vulkan の VUID があります: count=$vuidCount"
    Assert-GameReset ($transcript -notmatch 'Validation Error:') 'Vulkan の検証エラーがあります'
    Write-Output "GAME_RESET_CHECK result=pass resets=$SoakCount evidence=$EvidencePath"
    exit 0
}
catch
{
    # 時間切れや失敗の際も、読み取れる Game のログは証跡に残す。
    if ($null -ne $process -and $null -ne $gameLogPath -and $null -ne $runLogPath -and (Test-Path -LiteralPath $gameLogPath))
    {
        Copy-Item -LiteralPath $gameLogPath -Destination $runLogPath -Force -ErrorAction SilentlyContinue
    }
    Write-Output "GAME_RESET_CHECK result=fail resets=$SoakCount evidence=$EvidencePath"
    Write-Output "失敗理由: $($_.Exception.Message)"
    exit 1
}
finally
{
    if ($null -ne $process) { $process.Dispose() }
}
