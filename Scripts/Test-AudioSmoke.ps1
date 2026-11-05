<#
目的: 既定の雨ループと足音を XAudio2 で再生し、音声の終了と排出を検査する。
引数: Configuration は Debug または RelWithDebInfo（既定 Debug）、BuildDirectory はビルド先（既定 build）、
      TimeoutSeconds は各プロセスの待機上限秒数（既定 120）、EvidenceDir は省略可能な証跡の保存先。
出力: AUDIO_SMOKE の進行マーカーと result=pass。EvidenceDir 指定時はログと result.json も保存する。
終了コード: 成功は 0。準備・起動・時間切れ・マーカー検査の失敗は例外で 1。
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "RelWithDebInfo")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 120,
    [string]$EvidenceDir
)

$ErrorActionPreference = "Stop"

function Assert-AudioSmoke([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw $Message
    }
}

function Get-AudioOwnedDescendantProcessIds([int]$RootProcessId)
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
                if (-not $descendants.Contains($childId))
                {
                    $descendants.Add($childId)
                    $pending.Enqueue($childId)
                }
            }
        }
    }
    return $descendants.ToArray()
}

function ConvertTo-AudioProcessArgument([string]$Argument)
{
    # Start-Process が連結する引数でも、空白・引用符・末尾の区切りを保持する。
    $quoted = [regex]::Replace($Argument, '(\\*)"', '$1$1\"')
    $quoted = [regex]::Replace($quoted, '(\\+)$', '$1$1')
    return '"' + $quoted + '"'
}

function Invoke-AudioOwnedProcess([string]$Exe, [string[]]$Arguments, [string]$RunDirectory)
{
    $stdout = Join-Path $RunDirectory "stdout.txt"
    $stderr = Join-Path $RunDirectory "stderr.txt"
    $argumentLine = ($Arguments | ForEach-Object { ConvertTo-AudioProcessArgument $_ }) -join ' '
    $process = Start-Process -FilePath $Exe -ArgumentList $argumentLine -WorkingDirectory $RunDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $rootProcessId = [int]$process.Id
    [void]$process.Handle
    try
    {
        if (-not $process.WaitForExit($TimeoutSeconds * 1000))
        {
            # 親が終了する前に子孫を控え、ツリー停止後も残ったプロセスを確かめる。
            $ownedProcessIds = @($rootProcessId) + @(Get-AudioOwnedDescendantProcessIds $rootProcessId)
            $taskkillOutput = & taskkill.exe /PID $rootProcessId /T /F 2>&1
            $taskkillExitCode = $LASTEXITCODE
            $rootStopped = $process.WaitForExit(5000)
            $process.Refresh()
            $remaining = @($ownedProcessIds | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
            Assert-AudioSmoke ($taskkillExitCode -eq 0 -and $rootStopped -and $process.HasExited -and $remaining.Count -eq 0) "所有プロセスの停止に失敗しました: pid=$rootProcessId taskkill_exit=$taskkillExitCode remaining=$($remaining -join ',') output=$($taskkillOutput -join ' ')"
            throw "プロセスが時間切れになりました: pid=$rootProcessId timeout_seconds=$TimeoutSeconds"
        }
        $process.Refresh()
        $remaining = @(Get-AudioOwnedDescendantProcessIds $rootProcessId | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
        if ($remaining.Count -ne 0)
        {
            foreach ($childId in $remaining)
            {
                & taskkill.exe /PID $childId /T /F 2>&1 | Out-Null
            }
            throw "親の終了後に子孫が残りました: pid=$rootProcessId children=$($remaining -join ',')"
        }
        return [int]$process.ExitCode
    }
    finally
    {
        $process.Refresh()
        if (-not $process.HasExited)
        {
            & taskkill.exe /PID $rootProcessId /T /F 2>&1 | Out-Null
            [void]$process.WaitForExit(5000)
        }
        $process.Dispose()
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuild = if ([IO.Path]::IsPathRooted($BuildDirectory))
{
    [IO.Path]::GetFullPath($BuildDirectory)
}
else
{
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$runRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot "NorvesLibAudioSmoke-$([guid]::NewGuid().ToString('N'))"))
$success = $false
$errorText = ""
$gameExitCode = $null

try
{
    try
    {
        $gamePath = Join-Path $resolvedBuild "Game\$Configuration\Game.exe"
        $assetCookPath = Join-Path $resolvedBuild "Tools\AssetCook\$Configuration\AssetCook.exe"
        Assert-AudioSmoke (Test-Path -LiteralPath $gamePath -PathType Leaf) "Game がありません: $gamePath"
        Assert-AudioSmoke (Test-Path -LiteralPath $assetCookPath -PathType Leaf) "AssetCook がありません: $assetCookPath"
        $runtimeRoot = Join-Path $runRoot "RuntimeRoot"
        $gameDirectory = Join-Path $runRoot "game"
        New-Item -ItemType Directory -Path (Join-Path $runtimeRoot "Cooked"), $gameDirectory | Out-Null
        $manifest = Join-Path $runtimeRoot "manifest.json"
        [IO.File]::WriteAllText($manifest, '{"version":1,"assets":[]}', [Text.UTF8Encoding]::new($false))

        foreach ($name in @("rain_loop", "footstep_01", "footstep_02", "footstep_03"))
        {
            $inputPath = Join-Path $repoRoot "Assets\Audio\Provisional\$name.wav"
            Assert-AudioSmoke (Test-Path -LiteralPath $inputPath -PathType Leaf) "WAV がありません: $inputPath"
            $toolDirectory = Join-Path $runRoot "assetcook-$name"
            New-Item -ItemType Directory -Path $toolDirectory | Out-Null
            $toolExit = Invoke-AudioOwnedProcess $assetCookPath @("--input", $inputPath, "--out", (Join-Path $runtimeRoot "Cooked\$name.nvpkg"), "--manifest", $manifest, "--logical", "Audio/Provisional/$name.wav", "--kind", "audio", "--entry", "$name.nvaud", "--entry-type", "Aud0", "--format", "nvaud.v0.pcm16", "--variant", "default") $toolDirectory
            Assert-AudioSmoke ($toolExit -eq 0) "WAV の cook に失敗しました: name=$name exit=$toolExit"
        }
        $toolDirectory = Join-Path $runRoot "assetcook-bank"
        New-Item -ItemType Directory -Path $toolDirectory | Out-Null
        $bankInput = Join-Path $repoRoot "Assets\Audio\Banks\default.soundbank.json"
        $toolExit = Invoke-AudioOwnedProcess $assetCookPath @("--input", $bankInput, "--out", (Join-Path $runtimeRoot "Cooked\default.soundbank.nvpkg"), "--manifest", $manifest, "--logical", "Audio/Banks/default.soundbank.json", "--kind", "raw", "--entry", "__raw__", "--entry-type", "Raw", "--format", "raw.v0", "--variant", "default") $toolDirectory
        Assert-AudioSmoke ($toolExit -eq 0) "バンクの cook に失敗しました: exit=$toolExit"

        $gameExitCode = Invoke-AudioOwnedProcess $gamePath @("--audio-smoke", "--texture-asset-root", $runtimeRoot, "--texture-asset-manifest", $manifest, "--window-background", "--render-thread=st") $gameDirectory
        # ロガーのファイル出力を唯一のマーカー列とし、標準出力との重複を数えない。
        $logPath = Join-Path $gameDirectory "Game.log"
        Assert-AudioSmoke (Test-Path -LiteralPath $logPath -PathType Leaf) "Game.log がありません"
        $transcript = [IO.File]::ReadAllText($logPath)
        Assert-AudioSmoke (-not ($transcript -match 'AUDIO_SMOKE stage=failure')) "失敗マーカーが出ています"
        Assert-AudioSmoke ($gameExitCode -eq 0) "Game が異常終了しました: exit=$gameExitCode"
        $previousIndex = -1
        foreach ($marker in @(
            'AUDIO_SMOKE stage=registered',
            'AUDIO_SMOKE stage=bank_loaded sounds=2',
            'AUDIO_SMOKE stage=rain_started fade_in_ms=3000',
            'AUDIO_SMOKE stage=footsteps played=100 failed=0',
            'AUDIO_SMOKE stage=shutdown voices=0 callbacks=0 shutdown_complete=1'))
        {
            $matches = [regex]::Matches($transcript, [regex]::Escape($marker) + '(?=\s|$)')
            Assert-AudioSmoke ($matches.Count -eq 1) "マーカーの数が一致しません: marker=$marker count=$($matches.Count)"
            Assert-AudioSmoke ($matches[0].Index -gt $previousIndex) "マーカーの順序が違います: $marker"
            $previousIndex = $matches[0].Index
            Write-Output $marker
        }
        $success = $true
    }
    catch
    {
        $errorText = $_.Exception.Message
        throw
    }
    finally
    {
        try
        {
            if (-not [string]::IsNullOrWhiteSpace($EvidenceDir))
            {
                $evidence = [IO.Path]::GetFullPath($EvidenceDir)
                Assert-AudioSmoke (-not ($evidence.Equals($runRoot, [StringComparison]::OrdinalIgnoreCase) -or $evidence.StartsWith($runRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) "証跡の保存先は一時ディレクトリの外へ指定してください"
                New-Item -ItemType Directory -Path $evidence -Force | Out-Null
                if (Test-Path -LiteralPath $runRoot)
                {
                    Get-ChildItem -LiteralPath $runRoot -Directory | Where-Object { $_.Name -ne 'RuntimeRoot' } | ForEach-Object {
                        Copy-Item -LiteralPath $_.FullName -Destination $evidence -Recurse -Force
                    }
                }
                [pscustomobject]@{
                    result = $(if ($success) { 'pass' } else { 'fail' })
                    configuration = $Configuration
                    game_exit_code = $gameExitCode
                    error = $errorText
                } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding utf8
            }
        }
        finally
        {
            # GUID で作成した直下の一時領域だけを削除する。
            Assert-AudioSmoke ([IO.Path]::GetDirectoryName($runRoot).Equals($tempRoot.TrimEnd('\', '/'), [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($runRoot) -match '^NorvesLibAudioSmoke-[0-9a-f]{32}$') "一時ディレクトリの削除先が不正です"
            if (Test-Path -LiteralPath $runRoot)
            {
                Remove-Item -LiteralPath $runRoot -Recurse -Force
            }
        }
    }
    Write-Output "AUDIO_SMOKE result=pass"
}
catch
{
    throw "AUDIO_SMOKE result=fail reason=$($_.Exception.Message)"
}
