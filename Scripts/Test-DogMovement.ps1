# 犬の移動スモークを自動走行し、ログと撮影結果から受け入れを判定する。
# --scene と併用できないため CaptureScene.ps1 は使わない。
# --capture-deterministic を有効にするため --capture-png も指定する。
[CmdletBinding()]
param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 300,
    [string]$EvidenceDir,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

# 受け入れの許容差と閾値は仮の値。坂の始まりはコースの床面 y=0 m とする。
$DogMovementLimits = @{
    StepClimbMinY = 0.15
    StepBlockMaxY = 0.30
    SlopeStartY = 0.0
    SlopeBlockMaxRise = 1.0
    PlatformMaxDrift = 0.05
    AbsoluteTolerance = 0.15
    RelativeTolerance = 0.20
    SprintFovIncrease = 1.0
    WallArmReduction = 0.30
}

function Assert-Dog([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw $Message
    }
}

function Get-DogFileText([string]$Path)
{
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return ""
    }

    try
    {
        return [IO.File]::ReadAllText($Path)
    }
    catch
    {
        return "<unreadable path=$Path error=$($_.Exception.Message)>"
    }
}

function Stop-DogOwnedProcessTree([System.Diagnostics.Process]$Process, [string[]]$Arguments)
{
    $taskKillOutput = & taskkill.exe /PID $Process.Id /T /F 2>&1
    $taskKillExitCode = $LASTEXITCODE
    $waitCompleted = $Process.WaitForExit(5000)
    $Process.Refresh()
    $rootAlive = $null -ne (Get-Process -Id $Process.Id -ErrorAction SilentlyContinue)
    return [pscustomobject]@{
        TaskKillExitCode = $taskKillExitCode
        TaskKillOutput = ($taskKillOutput -join " ")
        WaitCompleted = $waitCompleted
        RootAlive = $rootAlive
        Arguments = ($Arguments -join ' ')
    }
}

function Invoke-DogGame([string]$GamePath, [string[]]$Arguments, [string]$RunDirectory, [int]$Timeout)
{
    $stdoutPath = Join-Path $RunDirectory "stdout.txt"
    $stderrPath = Join-Path $RunDirectory "stderr.txt"
    $logPath = Join-Path $RunDirectory "Game.log"
    $transcriptPath = Join-Path $RunDirectory "transcript.txt"
    $process = Start-Process -FilePath $GamePath -ArgumentList $Arguments -WorkingDirectory $RunDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle

    $timedOut = -not $process.WaitForExit($Timeout * 1000)
    $termination = $null
    if ($timedOut)
    {
        $termination = Stop-DogOwnedProcessTree $process $Arguments
    }

    $transcript = "$(Get-DogFileText $stdoutPath)`n$(Get-DogFileText $stderrPath)`n$(Get-DogFileText $logPath)"
    [IO.File]::WriteAllText($transcriptPath, $transcript, (New-Object System.Text.UTF8Encoding $true))
    $exitCode = -1
    if (-not $timedOut)
    {
        $exitCode = $process.ExitCode
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        ProcessId = $process.Id
        TimedOut = $timedOut
        Termination = $termination
        Transcript = $transcript
    }
}

function Get-DogStage([hashtable]$Stages, [string]$Stage)
{
    if ($Stages.ContainsKey($Stage) -and $Stages[$Stage].Count -gt 0)
    {
        return $Stages[$Stage][0]
    }
    return $null
}

function Get-DogValue([hashtable]$Marker, [string]$Stage, [string]$Key, [System.Collections.Generic.List[string]]$Reasons)
{
    if ($null -eq $Marker)
    {
        return $null
    }
    if (-not $Marker.ContainsKey($Key) -or [string]::IsNullOrWhiteSpace($Marker[$Key]))
    {
        $Reasons.Add("missing_value_${Stage}_${Key}")
        return $null
    }
    return $Marker[$Key]
}

function Get-DogNumber([hashtable]$Marker, [string]$Stage, [string]$Key, [System.Collections.Generic.List[string]]$Reasons)
{
    $raw = Get-DogValue $Marker $Stage $Key $Reasons
    if ($null -eq $raw)
    {
        return $null
    }
    try
    {
        $number = [double]::Parse($raw, [Globalization.CultureInfo]::InvariantCulture)
        if ([double]::IsNaN($number) -or [double]::IsInfinity($number))
        {
            throw "nonfinite"
        }
        return $number
    }
    catch
    {
        $Reasons.Add("invalid_number_${Stage}_${Key}")
        return $null
    }
}

function Test-DogFieldEquals([hashtable]$Marker, [string]$Stage, [string]$Key, [string]$Expected, [System.Collections.Generic.List[string]]$Reasons)
{
    $value = Get-DogValue $Marker $Stage $Key $Reasons
    if ($null -ne $value -and $value -cne $Expected)
    {
        $Reasons.Add("unexpected_value_${Stage}_${Key}")
    }
}

function Get-DogCaptureFailures([string]$Directory)
{
    $reasons = New-Object 'System.Collections.Generic.List[string]'
    if ([string]::IsNullOrWhiteSpace($Directory))
    {
        $reasons.Add("capture_directory_missing")
    }
    else
    {
        foreach ($name in @("low", "sprint", "wall"))
        {
            try
            {
                if (-not (Test-Path -LiteralPath (Join-Path $Directory "dog_$name.png") -PathType Leaf))
                {
                    $reasons.Add("capture_file_missing_$name")
                }
            }
            catch
            {
                $reasons.Add("capture_file_unreadable_$name")
            }
        }
    }
    return $reasons.ToArray()
}

function Test-DogMovementTranscript([string]$Transcript, [int]$ExitCode, [bool]$ExpectCaptures)
{
    $reasons = New-Object 'System.Collections.Generic.List[string]'
    $stages = @{}
    foreach ($line in ($Transcript -split "`r?`n"))
    {
        $match = [regex]::Match($line, 'DOG_MOVEMENT_SMOKE stage=(?<stage>[a-z][a-z0-9_]*)(?:\s+|$)(?<payload>.*)$')
        if (-not $match.Success)
        {
            continue
        }

        $stage = $match.Groups['stage'].Value
        $values = @{}
        foreach ($field in [regex]::Matches($match.Groups['payload'].Value, '(?:^|\s+)(?<key>[A-Za-z_][A-Za-z0-9_]*)=(?<value>.*?)(?=\s+[A-Za-z_][A-Za-z0-9_]*=|$)'))
        {
            $key = $field.Groups['key'].Value
            if ($values.ContainsKey($key))
            {
                $reasons.Add("duplicate_key_${stage}_${key}")
            }
            else
            {
                $values[$key] = $field.Groups['value'].Value.Trim()
            }
        }
        if (-not $stages.ContainsKey($stage))
        {
            $stages[$stage] = @()
        }
        $stages[$stage] += ,$values
    }

    if ($ExitCode -ne 0)
    {
        $reasons.Add("nonzero_exit")
    }
    foreach ($stage in @("ready", "autopilot", "step_climb", "step_block", "slope_climb", "slope_block", "platform", "gallop", "turn", "jump", "camera_low", "camera_sprint", "camera_wall", "hitstop", "complete"))
    {
        $count = 0
        if ($stages.ContainsKey($stage))
        {
            $count = $stages[$stage].Count
        }
        if ($count -ne 1)
        {
            $reasons.Add("stage_count_${stage}_$count")
        }
    }
    foreach ($stage in @("hitstop_active", "hitstop_resumed"))
    {
        if (-not $stages.ContainsKey($stage) -or $stages[$stage].Count -lt 1)
        {
            $reasons.Add("stage_missing_$stage")
        }
    }
    if ($stages.ContainsKey("failed"))
    {
        $reasons.Add("stage_failed")
    }

    $ready = Get-DogStage $stages "ready"
    $drive = Get-DogValue $ready "ready" "drive" $reasons
    if ($null -ne $drive -and $drive -cne "fixed" -and $drive -cne "variable")
    {
        $reasons.Add("unexpected_value_ready_drive")
    }
    $autopilot = Get-DogStage $stages "autopilot"
    Test-DogFieldEquals $autopilot "autopilot" "segments" "12" $reasons
    $null = Get-DogValue $autopilot "autopilot" "source" $reasons
    foreach ($stage in @("step_climb", "slope_climb"))
    {
        Test-DogFieldEquals (Get-DogStage $stages $stage) $stage "climbed" "1" $reasons
    }
    foreach ($stage in @("step_block", "slope_block"))
    {
        Test-DogFieldEquals (Get-DogStage $stages $stage) $stage "blocked" "1" $reasons
    }
    Test-DogFieldEquals (Get-DogStage $stages "hitstop") "hitstop" "duration_ms" "100" $reasons
    Test-DogFieldEquals (Get-DogStage $stages "complete") "complete" "exit_code" "0" $reasons

    $numericFields = @{
        step_climb = @("height", "top_y")
        step_block = @("height", "max_y")
        slope_climb = @("angle_deg", "top_y")
        slope_block = @("angle_deg", "max_y")
        platform = @("drift_m", "rotated_deg", "travel_m")
        gallop = @("time_s", "expected_s")
        turn = @("radius_m", "expected_m", "speed")
        jump = @("landing_m", "expected_m")
        camera_low = @("fov_deg", "arm_m", "pitch_deg")
        camera_sprint = @("fov_deg", "arm_m", "speed")
        camera_wall = @("fov_deg", "arm_m", "nominal_arm_m")
    }
    $numbers = @{}
    foreach ($stage in $numericFields.Keys)
    {
        $marker = Get-DogStage $stages $stage
        foreach ($key in $numericFields[$stage])
        {
            $numbers["$stage.$key"] = Get-DogNumber $marker $stage $key $reasons
        }
    }
    $fixedValues = @{
        "step_climb.height" = 0.2
        "step_block.height" = 0.4
        "slope_climb.angle_deg" = 30.0
        "slope_block.angle_deg" = 60.0
        "camera_wall.nominal_arm_m" = 3.0
    }
    foreach ($key in $fixedValues.Keys)
    {
        if ($null -ne $numbers[$key] -and $numbers[$key] -ne $fixedValues[$key])
        {
            $reasons.Add("unexpected_number_$($key.Replace('.', '_'))")
        }
    }

    if ($null -ne $numbers['step_climb.top_y'] -and $numbers['step_climb.top_y'] -lt $DogMovementLimits.StepClimbMinY)
    {
        $reasons.Add("step_climb_top_y")
    }
    if ($null -ne $numbers['step_block.max_y'] -and $numbers['step_block.max_y'] -ge $DogMovementLimits.StepBlockMaxY)
    {
        $reasons.Add("step_block_max_y")
    }
    if ($null -ne $numbers['slope_block.max_y'] -and ($numbers['slope_block.max_y'] - $DogMovementLimits.SlopeStartY) -ge $DogMovementLimits.SlopeBlockMaxRise)
    {
        $reasons.Add("slope_block_max_y")
    }
    if ($null -ne $numbers['platform.drift_m'] -and $numbers['platform.drift_m'] -gt $DogMovementLimits.PlatformMaxDrift)
    {
        $reasons.Add("platform_drift")
    }
    foreach ($comparison in @(@("gallop", "time_s", "expected_s"), @("turn", "radius_m", "expected_m"), @("jump", "landing_m", "expected_m")))
    {
        $stage, $measuredKey, $expectedKey = $comparison
        $measured = $numbers["$stage.$measuredKey"]
        $expected = $numbers["$stage.$expectedKey"]
        if ($null -ne $measured -and $null -ne $expected)
        {
            $tolerance = [Math]::Max($DogMovementLimits.AbsoluteTolerance, $DogMovementLimits.RelativeTolerance * $expected)
            if ([Math]::Abs($measured - $expected) -gt $tolerance)
            {
                $reasons.Add("outside_tolerance_$stage")
            }
        }
    }
    if ($null -ne $numbers['camera_sprint.fov_deg'] -and $null -ne $numbers['camera_low.fov_deg'] -and $numbers['camera_sprint.fov_deg'] -le ($numbers['camera_low.fov_deg'] + $DogMovementLimits.SprintFovIncrease))
    {
        $reasons.Add("camera_sprint_fov")
    }
    if ($null -ne $numbers['camera_wall.arm_m'] -and $null -ne $numbers['camera_wall.nominal_arm_m'] -and $numbers['camera_wall.arm_m'] -ge ($numbers['camera_wall.nominal_arm_m'] - $DogMovementLimits.WallArmReduction))
    {
        $reasons.Add("camera_wall_arm")
    }

    # SelfTest でも撮影の印は 3 件とも照合し、ExpectCaptures は実ファイルの検査だけを切り替える。
    $captures = @()
    if ($stages.ContainsKey("capture"))
    {
        $captures = $stages['capture']
    }
    if ($captures.Count -ne 3)
    {
        $reasons.Add("capture_marker_count_$($captures.Count)")
    }
    $captureNames = @{}
    foreach ($capture in $captures)
    {
        $name = Get-DogValue $capture "capture" "name" $reasons
        $null = Get-DogValue $capture "capture" "path" $reasons
        if ($null -ne $name)
        {
            if (-not $captureNames.ContainsKey($name))
            {
                $captureNames[$name] = 0
            }
            $captureNames[$name]++
        }
    }
    foreach ($name in @("low", "sprint", "wall"))
    {
        if (-not $captureNames.ContainsKey($name) -or $captureNames[$name] -ne 1)
        {
            $reasons.Add("capture_marker_name_$name")
        }
    }
    if ($ExpectCaptures)
    {
        foreach ($reason in (Get-DogCaptureFailures $script:DogMovementCaptureDirectory))
        {
            $reasons.Add($reason)
        }
    }
    return [pscustomobject]@{ Passed = ($reasons.Count -eq 0); Reasons = $reasons.ToArray() }
}

if ($SelfTest)
{
    $sample = @'
DOG_MOVEMENT_SMOKE stage=ready drive=fixed
DOG_MOVEMENT_SMOKE stage=autopilot segments=12 source=C:\sample path\autopilot.json
DOG_MOVEMENT_SMOKE stage=step_climb height=0.2000 top_y=0.2000 climbed=1
DOG_MOVEMENT_SMOKE stage=step_block height=0.4000 max_y=0.1000 blocked=1
DOG_MOVEMENT_SMOKE stage=slope_climb angle_deg=30.0000 top_y=1.1000 climbed=1
DOG_MOVEMENT_SMOKE stage=slope_block angle_deg=60.0000 max_y=0.5000 blocked=1
DOG_MOVEMENT_SMOKE stage=platform drift_m=0.0200 rotated_deg=30.0000 travel_m=1.0000
DOG_MOVEMENT_SMOKE stage=gallop time_s=2.0000 expected_s=2.0000
DOG_MOVEMENT_SMOKE stage=turn radius_m=3.0000 expected_m=3.0000 speed=2.0000
DOG_MOVEMENT_SMOKE stage=jump landing_m=2.0000 expected_m=2.0000
DOG_MOVEMENT_SMOKE stage=camera_low fov_deg=60.0000 arm_m=3.0000 pitch_deg=15.0000
DOG_MOVEMENT_SMOKE stage=camera_sprint fov_deg=65.0000 arm_m=3.0000 speed=4.0000
DOG_MOVEMENT_SMOKE stage=camera_wall fov_deg=60.0000 arm_m=2.0000 nominal_arm_m=3.0000
DOG_MOVEMENT_SMOKE stage=hitstop duration_ms=100
DOG_MOVEMENT_SMOKE stage=hitstop_active world_dt=0 animation_dt=0 physics_ns=0 unscaled_dt=0.0167 body_step_held=1 camera_tick_advanced=1 camera_result=0
DOG_MOVEMENT_SMOKE stage=hitstop_resumed body_step_advanced=1 camera_tick_advanced=1 physics_ns=16666667
DOG_MOVEMENT_SMOKE stage=capture name=low path=C:\sample path\dog_low.png
DOG_MOVEMENT_SMOKE stage=capture name=sprint path=C:\sample path\dog_sprint.png
DOG_MOVEMENT_SMOKE stage=capture name=wall path=C:\sample path\dog_wall.png
DOG_MOVEMENT_SMOKE stage=complete exit_code=0
'@
    $withoutTurn = [regex]::Replace($sample, '(?m)^DOG_MOVEMENT_SMOKE stage=turn .*\r?\n', '')
    $badJump = $sample.Replace('landing_m=2.0000', 'landing_m=4.0000')
    $failedStage = $sample + "`nDOG_MOVEMENT_SMOKE stage=failed reason=jump"
    $cases = @(
        @{ Name = "complete"; Transcript = $sample; ExitCode = 0; Expected = $true },
        @{ Name = "missing_turn"; Transcript = $withoutTurn; ExitCode = 0; Expected = $false },
        @{ Name = "bad_jump"; Transcript = $badJump; ExitCode = 0; Expected = $false },
        @{ Name = "failed_stage"; Transcript = $failedStage; ExitCode = 0; Expected = $false },
        @{ Name = "nonzero_exit"; Transcript = $sample; ExitCode = 1; Expected = $false }
    )
    foreach ($case in $cases)
    {
        $result = Test-DogMovementTranscript $case.Transcript $case.ExitCode $false
        Assert-Dog ($result.Passed -eq $case.Expected) "SelfTest が期待と異なります: $($case.Name) reasons=$($result.Reasons -join ';')"
    }
    Write-Output "DOG_MOVEMENT_ACCEPTANCE_SELFTEST result=pass cases=$($cases.Count)"
    return
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([IO.Path]::IsPathRooted($BuildDirectory))
{
    $buildRoot = $BuildDirectory
}
else
{
    $buildRoot = Join-Path $repoRoot $BuildDirectory
}
$gamePath = Join-Path $buildRoot "Game\$Configuration\Game.exe"
$temporaryRun = [string]::IsNullOrWhiteSpace($EvidenceDir)
if ($temporaryRun)
{
    $runDirectory = Join-Path ([IO.Path]::GetTempPath()) "NorvesLibDogMovement-$([guid]::NewGuid().ToString('N'))"
}
else
{
    $runDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($EvidenceDir)
}
$script:DogMovementCaptureDirectory = $runDirectory
$runDirectoryCreated = $false
$failureReason = "execution_error"
$failure = $null
$run = $null

try
{
    if (Test-Path -LiteralPath $runDirectory)
    {
        Assert-Dog (-not $temporaryRun) "一時フォルダが既に存在します: $runDirectory"
        Assert-Dog (Test-Path -LiteralPath $runDirectory -PathType Container) "証拠の保存先がフォルダではありません: $runDirectory"
    }
    else
    {
        $null = [IO.Directory]::CreateDirectory($runDirectory)
        $runDirectoryCreated = $true
    }
    # 前回の画像やログを今回の結果と誤認したり、既存の証拠を上書きしたりしない。
    foreach ($name in @("stdout.txt", "stderr.txt", "Game.log", "transcript.txt", "dog_low.png", "dog_sprint.png", "dog_wall.png", "final.png"))
    {
        Assert-Dog (-not (Test-Path -LiteralPath (Join-Path $runDirectory $name))) "証拠ファイルが既に存在します: $name"
    }
    Assert-Dog (Test-Path -LiteralPath $gamePath -PathType Leaf) "Game 実行ファイルが見つかりません: $gamePath"

    # 引用符の直前の末尾バックスラッシュは Windows の引数解析で二重にする。
    $captureArgument = [regex]::Replace($runDirectory, '\\+$', { param($match) $match.Value + $match.Value })
    $finalPath = Join-Path $runDirectory "final.png"
    $arguments = @(
        "--dog-movement-smoke",
        "--dog-movement-autopilot",
        "--dog-movement-capture-dir=`"$captureArgument`"",
        "--capture-png=`"$finalPath`"",
        "--exit-after-rendered-frames=100000",
        "--capture-deterministic",
        "--window-background"
    )
    $run = Invoke-DogGame $gamePath $arguments $runDirectory $TimeoutSeconds
    if ($run.TimedOut)
    {
        $failureReason = "timeout"
        $termination = $run.Termination
        throw "Game が時間切れになりました: pid=$($run.ProcessId) args=$($termination.Arguments) taskkill_exit=$($termination.TaskKillExitCode) wait_completed=$($termination.WaitCompleted) root_alive=$($termination.RootAlive) taskkill_output=$($termination.TaskKillOutput)"
    }

    if ($Configuration -eq "Release")
    {
        $reasons = New-Object 'System.Collections.Generic.List[string]'
        if ($run.ExitCode -ne 0)
        {
            $reasons.Add("nonzero_exit")
        }
        foreach ($reason in (Get-DogCaptureFailures $runDirectory))
        {
            $reasons.Add($reason)
        }
        $result = [pscustomobject]@{ Passed = ($reasons.Count -eq 0); Reasons = $reasons.ToArray() }
    }
    else
    {
        $result = Test-DogMovementTranscript $run.Transcript $run.ExitCode $true
    }
    if (-not $result.Passed)
    {
        $failureReason = $result.Reasons -join ';'
        throw "犬の移動スモークの受け入れ判定に失敗しました。"
    }
    if ($temporaryRun -and $runDirectoryCreated)
    {
        $failureReason = "cleanup_error"
        Remove-Item -LiteralPath $runDirectory -Recurse -Force
    }
}
catch
{
    $failure = $_
}

if ($null -ne $failure)
{
    Write-Output "DOG_MOVEMENT_ACCEPTANCE result=failed reasons=$failureReason evidence_path=$runDirectory"
    throw $failure
}
Write-Output "DOG_MOVEMENT_ACCEPTANCE result=pass configuration=$Configuration exit=$($run.ExitCode)"
