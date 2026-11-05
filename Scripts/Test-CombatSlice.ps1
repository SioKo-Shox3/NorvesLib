[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("combat", "encounter", "enemy", "bench")]
    [string]$Mode,
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 180,
    [AllowEmptyString()]
    [string]$CapturePng
)

$ErrorActionPreference = "Stop"

function Assert-CombatSlice([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw "戦闘スライスの確認に失敗しました: $Message"
    }
}

function Stop-CombatSliceOwnedProcessTree([System.Diagnostics.Process]$Process, [string[]]$Arguments)
{
    $taskKillOutput = & taskkill.exe /PID $Process.Id /T /F 2>&1
    $taskKillExitCode = $LASTEXITCODE
    $waitCompleted = $Process.WaitForExit(5000)
    $Process.Refresh()

    $residualRoot = Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
    $rootAlive = $null -ne $residualRoot
    return [pscustomobject]@{
        TaskKillExitCode = $taskKillExitCode
        TaskKillOutput = ($taskKillOutput -join " ")
        WaitCompleted = $waitCompleted
        RootAlive = $rootAlive
        Arguments = ($Arguments -join ' ')
    }
}

function Invoke-CombatSliceGame([string]$GamePath, [string[]]$Arguments, [string]$RunDirectory, [int]$Timeout)
{
    $stdoutPath = Join-Path $RunDirectory "stdout.txt"
    $stderrPath = Join-Path $RunDirectory "stderr.txt"
    $process = Start-Process -FilePath $GamePath -ArgumentList $Arguments -WorkingDirectory $RunDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle

    if (-not $process.WaitForExit($Timeout * 1000))
    {
        $termination = Stop-CombatSliceOwnedProcessTree $process $Arguments
        throw "Game の待機が時間切れになりました: pid=$($process.Id) args=$($termination.Arguments) taskkill_exit=$($termination.TaskKillExitCode) termination_wait_completed=$($termination.WaitCompleted) root_alive=$($termination.RootAlive) evidence_path=$RunDirectory stdout_path=$stdoutPath stderr_path=$stderrPath log_path=$(Join-Path $RunDirectory 'Game.log') taskkill_output=$($termination.TaskKillOutput)"
    }

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        ProcessId = $process.Id
        LogPath = (Join-Path $RunDirectory "Game.log")
    }
}

function Get-CombatSliceMarkers([string]$LogPath)
{
    Assert-CombatSlice (Test-Path -LiteralPath $LogPath -PathType Leaf) "ログが見つかりません: $LogPath"

    # 起動時に開いた Game.log だけを読み、同じメッセージの別出力を重複して数えない。
    foreach ($line in [IO.File]::ReadAllLines($LogPath, [Text.Encoding]::UTF8))
    {
        $match = [regex]::Match($line, '(?:^|\] )(?<marker>(?:COMBAT_SMOKE|ENCOUNTER_SMOKE|ENEMY_SMOKE|COMBAT_BENCH) .*)$')
        if ($match.Success)
        {
            $match.Groups['marker'].Value
        }
    }
}

function Get-CombatSliceRunMarkers($Run)
{
    Assert-CombatSlice ($Run.ExitCode -eq 0) "Game の終了コードが 0 ではありません: exit=$($Run.ExitCode) pid=$($Run.ProcessId)"
    $markers = @(Get-CombatSliceMarkers $Run.LogPath)
    foreach ($marker in $markers)
    {
        Assert-CombatSlice (-not [regex]::IsMatch($marker, '^(?:COMBAT_SMOKE|ENCOUNTER_SMOKE|ENEMY_SMOKE) stage=failure(?: |$)')) "失敗マーカーを検出しました: $marker"
    }
    return $markers
}

function Assert-CombatSliceSequence([string[]]$Markers, [string[]]$Patterns)
{
    Assert-CombatSlice ($Markers.Count -eq $Patterns.Count) "マーカーの行数が一致しません: actual=$($Markers.Count) expected=$($Patterns.Count)"
    for ($index = 0; $index -lt $Patterns.Count; $index++)
    {
        Assert-CombatSlice ([regex]::IsMatch($Markers[$index], $Patterns[$index])) "マーカーの順序または内容が一致しません: index=$index actual=$($Markers[$index]) expected=$($Patterns[$index])"
    }
}

function Assert-CombatSliceCombat([string[]]$Markers, [int]$ExpectedDt, [string]$CapturePath)
{
    Assert-CombatSlice ($Markers.Count -gt 0) "combat のマーカーがありません: dt_inv=$ExpectedDt"
    $ready = [regex]::Match($Markers[0], '^COMBAT_SMOKE stage=ready dt_inv=(\d+) hurtbox_parts=(\d+)$')
    Assert-CombatSlice ($ready.Success -and $ready.Groups[1].Value -ceq [string]$ExpectedDt) "ready の内容が一致しません: dt_inv=$ExpectedDt actual=$($Markers[0])"

    $index = 1
    $hitCount = 0
    $hitPattern = '^COMBAT_SMOKE stage=hit index=(\d+) dt_inv=(\d+) part=\d+ attack=\S+ attack_instance=\d+ point=-?\d+\.\d{3},-?\d+\.\d{3},-?\d+\.\d{3}$'
    while ($index -lt $Markers.Count -and $Markers[$index] -cmatch '^COMBAT_SMOKE stage=hit(?: |$)')
    {
        $hit = [regex]::Match($Markers[$index], $hitPattern)
        Assert-CombatSlice ($hit.Success -and $hit.Groups[1].Value -ceq [string]$hitCount -and $hit.Groups[2].Value -ceq [string]$ExpectedDt) "hit の内容または連番が一致しません: index=$hitCount actual=$($Markers[$index])"
        $hitCount++
        $index++
    }
    Assert-CombatSlice ($hitCount -gt 0) "hit がありません: dt_inv=$ExpectedDt"

    if (-not [string]::IsNullOrEmpty($CapturePath))
    {
        Assert-CombatSlice ($index -lt $Markers.Count) "撮影マーカーがありません: dt_inv=$ExpectedDt"
        Assert-CombatSlice ($Markers[$index] -ceq "COMBAT_SMOKE stage=capture path=$CapturePath") "撮影マーカーの順序またはパスが一致しません: actual=$($Markers[$index])"
        $index++
    }

    Assert-CombatSlice ($index -lt $Markers.Count) "summary マーカーがありません: dt_inv=$ExpectedDt"
    $summary = [regex]::Match($Markers[$index], '^COMBAT_SMOKE stage=summary dt_inv=(\d+) hits=(\d+) set=(\S+)$')
    Assert-CombatSlice ($summary.Success -and $summary.Groups[1].Value -ceq [string]$ExpectedDt -and $summary.Groups[2].Value -ceq [string]$hitCount) "summary の dt_inv または hits が一致しません: actual=$($Markers[$index])"
    $index++
    Assert-CombatSlice ($index -lt $Markers.Count) "complete マーカーがありません: dt_inv=$ExpectedDt"
    Assert-CombatSlice ($Markers[$index] -ceq 'COMBAT_SMOKE stage=complete exit_code=0') "complete マーカーが一致しません: actual=$($Markers[$index])"
    $index++
    Assert-CombatSlice ($index -eq $Markers.Count) "complete の後に余分なマーカーがあります: dt_inv=$ExpectedDt"

    return [pscustomobject]@{
        Hits = $hitCount
        Set = $summary.Groups[3].Value
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$runRoot = Join-Path ([IO.Path]::GetTempPath()) "NorvesLibCombatSlice-$([guid]::NewGuid().ToString('N'))"
$runDirectoryCreated = $false
$runSucceeded = $false
$primaryFailure = $null
$cleanupFailure = $null
$benchOutput = $null
$acceptanceDetails = ""
$captureRequested = $PSBoundParameters.ContainsKey('CapturePng')
$capturePath = ""

try
{
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $runDirectoryCreated = $true

    Assert-CombatSlice (-not $captureRequested -or $Mode -in @('combat', 'enemy')) "CapturePng は combat または enemy でのみ指定できます。"
    if ($captureRequested)
    {
        Assert-CombatSlice (-not [string]::IsNullOrWhiteSpace($CapturePng)) "CapturePng に保存先を指定してください。"
        if ([IO.Path]::IsPathRooted($CapturePng))
        {
            $capturePath = [IO.Path]::GetFullPath($CapturePng)
        }
        else
        {
            $capturePath = [IO.Path]::GetFullPath([IO.Path]::Combine($repoRoot, $CapturePng))
        }
        $captureParent = [IO.Path]::GetDirectoryName($capturePath)
        Assert-CombatSlice (-not [string]::IsNullOrEmpty($captureParent)) "撮影先の親フォルダを特定できません: $capturePath"
        $null = [IO.Directory]::CreateDirectory($captureParent)
        Assert-CombatSlice (-not (Test-Path -LiteralPath $capturePath -PathType Container)) "撮影先がフォルダです: $capturePath"
        $oldCapture = $null
        if (Test-Path -LiteralPath $capturePath -PathType Leaf)
        {
            $existingCapture = Get-Item -LiteralPath $capturePath
            $oldCapture = [pscustomobject]@{ Length = $existingCapture.Length; LastWriteTimeUtc = $existingCapture.LastWriteTimeUtc }
        }
    }

    if ([IO.Path]::IsPathRooted($BuildDirectory))
    {
        $buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
    }
    else
    {
        $buildRoot = [IO.Path]::GetFullPath([IO.Path]::Combine($repoRoot, $BuildDirectory))
    }
    $gamePath = Join-Path $buildRoot "Game\$Configuration\Game.exe"
    Assert-CombatSlice (Test-Path -LiteralPath $gamePath -PathType Leaf) "Game の実行ファイルが見つかりません: $gamePath"

    if ($Mode -eq 'combat')
    {
        $summaries = @()
        foreach ($dt in @(30, 144))
        {
            $runDirectory = Join-Path $runRoot "combat-$dt"
            New-Item -ItemType Directory -Path $runDirectory | Out-Null
            $arguments = @('--window-background', '--combat-smoke', "--fixed-frame-dt=1/$dt")
            $runCapturePath = ""
            if ($captureRequested -and $dt -eq 30)
            {
                $runCapturePath = $capturePath
                $arguments += ('--combat-slice-capture="{0}"' -f $capturePath)
            }
            $run = Invoke-CombatSliceGame $gamePath $arguments $runDirectory $TimeoutSeconds
            Assert-CombatSlice ($run.ExitCode -eq 0) "Game の終了コードが 0 ではありません: exit=$($run.ExitCode) pid=$($run.ProcessId) dt_inv=$dt"
            if ($Configuration -ne 'Release')
            {
                $markers = @(Get-CombatSliceRunMarkers $run)
                $summaries += Assert-CombatSliceCombat $markers $dt $runCapturePath
            }
        }
        if ($Configuration -ne 'Release')
        {
            Assert-CombatSlice ($summaries[0].Hits -eq $summaries[1].Hits -and $summaries[0].Set -ceq $summaries[1].Set) "固定刻み間の hits または set が一致しません: hits_30=$($summaries[0].Hits) hits_144=$($summaries[1].Hits) set_30=$($summaries[0].Set) set_144=$($summaries[1].Set)"
            $acceptanceDetails = " hits=$($summaries[0].Hits) set=$($summaries[0].Set)"
        }
    }
    else
    {
        $runDirectory = Join-Path $runRoot $Mode
        New-Item -ItemType Directory -Path $runDirectory | Out-Null
        $arguments = @('--window-background')
        switch ($Mode)
        {
            'encounter' { $arguments += '--encounter-smoke' }
            'enemy' { $arguments += '--enemy-smoke' }
            'bench' { $arguments += @('--combat-smoke', '--combat-bench') }
        }
        if ($captureRequested)
        {
            $arguments += ('--combat-slice-capture="{0}"' -f $capturePath)
        }
        if ($Mode -eq 'bench' -and $Configuration -eq 'Release')
        {
            Write-Warning "Release 構成では計測を行わず、終了コードだけを確認します。"
        }
        $run = Invoke-CombatSliceGame $gamePath $arguments $runDirectory $TimeoutSeconds
        Assert-CombatSlice ($run.ExitCode -eq 0) "Game の終了コードが 0 ではありません: exit=$($run.ExitCode) pid=$($run.ProcessId)"
        if ($Configuration -ne 'Release')
        {
            $markers = @(Get-CombatSliceRunMarkers $run)
            switch ($Mode)
            {
                'encounter'
                {
                    $patterns = @(
                        '^ENCOUNTER_SMOKE stage=ready$',
                        '^ENCOUNTER_SMOKE prompt=show action=combat_slice\.rest$',
                        '^ENCOUNTER_SMOKE prompt=hide action=combat_slice\.rest$',
                        '^ENCOUNTER_SMOKE stage=enter(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=engaged(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=exit(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=reset(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=enter(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=engaged(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=cleared(?: .*)?$',
                        '^ENCOUNTER_SMOKE stage=complete exit_code=0$'
                    )
                    Assert-CombatSliceSequence $markers $patterns
                }
                'enemy'
                {
                    $patterns = @(
                        '^ENEMY_SMOKE stage=ready$',
                        '^ENEMY_SMOKE stage=ZoneEntered$',
                        '^ENEMY_SMOKE stage=Alerted$',
                        '^ENEMY_SMOKE stage=Attack$',
                        '^ENEMY_SMOKE stage=Collapsed$',
                        '^ENEMY_SMOKE stage=Defeated$',
                        '^ENEMY_SMOKE stage=ProgressionChanged$',
                        '^ENEMY_SMOKE stage=FinalUnlocked$'
                    )
                    if ($captureRequested)
                    {
                        $patterns += '^ENEMY_SMOKE stage=capture path=' + [regex]::Escape($capturePath) + '$'
                    }
                    $patterns += '^ENEMY_SMOKE stage=complete exit_code=0$'
                    Assert-CombatSliceSequence $markers $patterns
                }
                'bench'
                {
                    $number = '[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'
                    $benchPattern = '^COMBAT_BENCH hitboxes=4 hurtboxes=64 nmax=16 frames=(?<frames>\d+) active_median_us=(?<active_median>' + $number + ') active_max_us=(?<active_max>' + $number + ') idle_median_us=(?<idle_median>' + $number + ') sweep_median_us=(?<sweep_median>' + $number + ')$'
                    Assert-CombatSliceSequence $markers @($benchPattern, '^COMBAT_SMOKE stage=complete exit_code=0$')
                    $bench = [regex]::Match($markers[0], $benchPattern)
                    Assert-CombatSlice ([long]$bench.Groups['frames'].Value -gt 0) "ベンチのフレーム数が 0 です。"
                    $benchOutput = 'COMBAT_SLICE_BENCH ' + $markers[0].Substring('COMBAT_BENCH '.Length)
                }
            }
        }
    }

    if ($captureRequested)
    {
        Assert-CombatSlice (Test-Path -LiteralPath $capturePath -PathType Leaf) "撮影ファイルが見つかりません: $capturePath"
        $capture = Get-Item -LiteralPath $capturePath
        Assert-CombatSlice ($capture.Length -gt 0) "撮影ファイルが空です: $capturePath"
        if ($null -ne $oldCapture)
        {
            Assert-CombatSlice ($capture.Length -ne $oldCapture.Length -or $capture.LastWriteTimeUtc -ne $oldCapture.LastWriteTimeUtc) "既存の撮影ファイルが更新されていません: $capturePath"
        }
    }
    $runSucceeded = $true
}
catch
{
    $primaryFailure = $_
}
finally
{
    if ($runSucceeded -and $runDirectoryCreated -and (Test-Path -LiteralPath $runRoot))
    {
        try
        {
            Remove-Item -LiteralPath $runRoot -Recurse -Force
        }
        catch
        {
            $cleanupFailure = $_
        }
    }
}

if ($null -ne $primaryFailure -or $null -ne $cleanupFailure)
{
    Write-Output "COMBAT_SLICE_ACCEPTANCE result=failed evidence_path=$runRoot"
    if ($null -ne $primaryFailure)
    {
        throw "戦闘スライスの確認に失敗しました: $($primaryFailure.Exception.Message)"
    }
    throw "一時フォルダを片付けられませんでした: $($cleanupFailure.Exception.Message)"
}

if ($null -ne $benchOutput)
{
    Write-Output $benchOutput
}
Write-Output "COMBAT_SLICE_ACCEPTANCE result=pass mode=$Mode configuration=$Configuration$acceptanceDetails"
