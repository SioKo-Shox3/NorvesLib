[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"

function Assert-M8([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw "M8 physics acceptance failed: $Message"
    }
}

function Get-M8Transcript([string]$Path)
{
    if (-not (Test-Path -LiteralPath $Path))
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

function Stop-M8OwnedProcessTree([System.Diagnostics.Process]$Process, [string[]]$Arguments)
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

function Invoke-M8Game([string]$GamePath, [string[]]$Arguments, [string]$RunDirectory, [int]$Timeout)
{
    $stdoutPath = Join-Path $RunDirectory "stdout.txt"
    $stderrPath = Join-Path $RunDirectory "stderr.txt"
    $process = Start-Process -FilePath $GamePath -ArgumentList $Arguments -WorkingDirectory $RunDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle

    if (-not $process.WaitForExit($Timeout * 1000))
    {
        $termination = Stop-M8OwnedProcessTree $process $Arguments
        throw "Game timeout pid=$($process.Id) args=$($termination.Arguments) taskkill_exit=$($termination.TaskKillExitCode) termination_wait_completed=$($termination.WaitCompleted) root_alive=$($termination.RootAlive) evidence_path=$RunDirectory stdout_path=$stdoutPath stderr_path=$stderrPath log_path=$(Join-Path $RunDirectory 'Game.log') taskkill_output=$($termination.TaskKillOutput)"
    }

    $logPath = Join-Path $RunDirectory "Game.log"
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        ProcessId = $process.Id
        Transcript = "$(Get-M8Transcript $stdoutPath)`n$(Get-M8Transcript $stderrPath)`n$(Get-M8Transcript $logPath)"
    }
}

function Assert-M8DebugNormal($Run)
{
    Assert-M8 ($Run.ExitCode -eq 0) "Debug normal exit=$($Run.ExitCode) pid=$($Run.ProcessId)"
    Assert-M8 (-not ($Run.Transcript -match 'M8_PHYSICS_SMOKE')) "Debug normal emitted M8 marker"
}

function Assert-M8DebugSmoke($Run)
{
    Assert-M8 ($Run.ExitCode -eq 0) "Debug smoke exit=$($Run.ExitCode) pid=$($Run.ProcessId)"
    foreach ($stage in @("ready", "complete"))
    {
        $count = [regex]::Matches($Run.Transcript, "M8_PHYSICS_SMOKE stage=$stage ").Count
        Assert-M8 ($count -eq 1) "Debug smoke marker count stage=$stage count=$count"
    }
    Assert-M8 ([regex]::Matches($Run.Transcript, 'M8_PHYSICS_SMOKE stage=failure ').Count -eq 0) "Debug smoke emitted failure marker"
    Assert-M8 ($Run.Transcript -match 'M8_PHYSICS_SMOKE stage=ready colliders=4 bodies=3') "Debug smoke ready payload missing"
    Assert-M8 ($Run.Transcript -match 'M8_PHYSICS_SMOKE stage=complete query_stable=1 stack_stable=1 rendered_positive=1 exit_code=0') "Debug smoke complete payload missing"
    Assert-M8 ($Run.Transcript -match 'exit-after-rendered-frames reached rendered=[1-9]\d*') "Debug smoke rendered-frame exit witness missing"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$gamePath = Join-Path $repoRoot "$BuildDirectory\Game\$Configuration\Game.exe"
$runRoot = Join-Path ([IO.Path]::GetTempPath()) "NorvesLibM8Physics-$([guid]::NewGuid().ToString('N'))"
$runDirectoryCreated = $false
$runSucceeded = $false
$primaryFailure = $null
$cleanupFailure = $null

try
{
    Assert-M8 (Test-Path -LiteralPath $gamePath) "Game executable not found: $gamePath"
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $runDirectoryCreated = $true

    $normalDirectory = Join-Path $runRoot "normal"
    New-Item -ItemType Directory -Path $normalDirectory | Out-Null
    $normalRun = Invoke-M8Game $gamePath @("--exit-after-rendered-frames=120") $normalDirectory $TimeoutSeconds

    $smokeDirectory = Join-Path $runRoot "smoke"
    New-Item -ItemType Directory -Path $smokeDirectory | Out-Null
    $smokeRun = Invoke-M8Game $gamePath @("--rendering3dtest-physics-smoke", "--render-thread=mt", "--exit-after-rendered-frames=120") $smokeDirectory $TimeoutSeconds

    if ($Configuration -eq "Debug")
    {
        Assert-M8DebugNormal $normalRun
        Assert-M8DebugSmoke $smokeRun
    }
    else
    {
        Assert-M8 ($normalRun.ExitCode -eq 0) "Release normal exit=$($normalRun.ExitCode) pid=$($normalRun.ProcessId)"
        Assert-M8 ($smokeRun.ExitCode -eq 0) "Release smoke exit=$($smokeRun.ExitCode) pid=$($smokeRun.ProcessId)"
    }

    $runSucceeded = $true
    Write-Output "M8_MINIMAL_PHYSICS_ACCEPTANCE result=pass configuration=$Configuration normal_exit=$($normalRun.ExitCode) smoke_exit=$($smokeRun.ExitCode)"
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
    elseif ($runDirectoryCreated)
    {
        Write-Output "M8_MINIMAL_PHYSICS_ACCEPTANCE result=failed evidence_path=$runRoot"
    }
}

if ($null -ne $primaryFailure)
{
    throw $primaryFailure
}

if ($null -ne $cleanupFailure)
{
    throw $cleanupFailure
}
