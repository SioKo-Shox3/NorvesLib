[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "All")]
    [string]$Configuration = "All",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"

function Assert-CameraWorld([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw "Camera world smoke failed: $Message"
    }
}

function Get-CameraWorldDescendantProcessIds([int]$RootProcessId)
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

function Get-CameraWorldText([string[]]$Paths)
{
    $text = ""
    foreach ($path in $Paths)
    {
        if (Test-Path -LiteralPath $path)
        {
            $text += [IO.File]::ReadAllText($path) + "`n"
        }
    }
    return $text
}

function Invoke-CameraWorldGame(
    [string]$GamePath,
    [string]$RunDirectory,
    [int]$Timeout)
{
    $stdoutPath = Join-Path $RunDirectory "stdout.txt"
    $stderrPath = Join-Path $RunDirectory "stderr.txt"
    $arguments = @("--render-thread=st", "--exit-after-rendered-frames=120")
    $process = Start-Process -FilePath $GamePath -ArgumentList $arguments -WorkingDirectory $RunDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $rootProcessId = [int]$process.Id
    [void]$process.Handle

    if (-not $process.WaitForExit($Timeout * 1000))
    {
        $ownedProcessIds = @($rootProcessId) + @(Get-CameraWorldDescendantProcessIds $rootProcessId)
        $taskkillOutput = & taskkill.exe /PID $rootProcessId /T /F 2>&1
        $taskkillExitCode = $LASTEXITCODE
        $rootStopped = $process.WaitForExit(5000)
        $process.Refresh()
        $rootAlive = -not $process.HasExited
        $remainingOwnedProcessIds = @(
            $ownedProcessIds | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
        if ($taskkillExitCode -ne 0 -or -not $rootStopped -or $rootAlive -or $remainingOwnedProcessIds.Count -ne 0)
        {
            throw "owned process cleanup failed pid=$rootProcessId taskkill_exit=$taskkillExitCode root_alive=$([int]$rootAlive) remaining_owned=$($remainingOwnedProcessIds -join ',') output=$($taskkillOutput -join ' ') evidence_path=$RunDirectory"
        }
        throw "owned process timeout pid=$rootProcessId root_alive=0 remaining_owned=0 evidence_path=$RunDirectory"
    }

    $process.Refresh()
    Assert-CameraWorld $process.HasExited "root process remained alive pid=$rootProcessId evidence_path=$RunDirectory"
    $remainingOwnedProcessIds = @(
        Get-CameraWorldDescendantProcessIds $rootProcessId |
            Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
    $transcript = Get-CameraWorldText @($stdoutPath, $stderrPath, (Join-Path $RunDirectory "Game.log"))
    return [pscustomobject]@{
        RootProcessId = $rootProcessId
        ExitCode = [int]$process.ExitCode
        RootAlive = $false
        RemainingOwnedProcessIds = $remainingOwnedProcessIds
        Transcript = $transcript
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
$configurations = if ($Configuration -eq "All") { @("Debug", "Release") } else { @($Configuration) }
$runRoot = Join-Path ([IO.Path]::GetTempPath()) "NorvesLibCameraWorld-$([guid]::NewGuid().ToString('N'))"
$success = $false

try
{
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    foreach ($currentConfiguration in $configurations)
    {
        $gamePath = Join-Path $resolvedBuild "Game\$currentConfiguration\Game.exe"
        Assert-CameraWorld (Test-Path -LiteralPath $gamePath) "Game executable not found: $gamePath"
        $runDirectory = Join-Path $runRoot $currentConfiguration
        New-Item -ItemType Directory -Path $runDirectory | Out-Null

        $run = Invoke-CameraWorldGame $gamePath $runDirectory $TimeoutSeconds
        Assert-CameraWorld ($run.ExitCode -eq 0) "$currentConfiguration Game exit=$($run.ExitCode) pid=$($run.RootProcessId) evidence_path=$runDirectory"
        Assert-CameraWorld (-not $run.RootAlive) "$currentConfiguration root process remained"
        Assert-CameraWorld ($run.RemainingOwnedProcessIds.Count -eq 0) "$currentConfiguration remaining owned process ids=$($run.RemainingOwnedProcessIds -join ',')"
        foreach ($stage in @("registered", "sync", "complete", "exit"))
        {
            $count = [regex]::Matches($run.Transcript, "CAMERA_COMPONENT_SMOKE stage=$stage(?=\s|$)").Count
            Assert-CameraWorld ($count -eq 1) "$currentConfiguration marker count stage=$stage count=$count"
        }
        Assert-CameraWorld ($run.Transcript -match "CAMERA_COMPONENT_SMOKE stage=sync snapshot=1") "$currentConfiguration sync witness missing"
        $completeMarker = [regex]::Match(
            $run.Transcript,
            "CAMERA_COMPONENT_SMOKE stage=complete snapshot=1 live_pointer=0 rendered=(?<rendered>\d+)")
        Assert-CameraWorld $completeMarker.Success "$currentConfiguration complete rendered-frame witness missing"
        Assert-CameraWorld ([uint64]$completeMarker.Groups['rendered'].Value -eq 120) "$currentConfiguration complete rendered count=$($completeMarker.Groups['rendered'].Value) expected=120"
        $exitMarker = [regex]::Match(
            $run.Transcript,
            "CAMERA_COMPONENT_SMOKE stage=exit rendered=(?<rendered>\d+)")
        Assert-CameraWorld $exitMarker.Success "$currentConfiguration exit rendered-frame witness missing"
        Assert-CameraWorld ([uint64]$exitMarker.Groups['rendered'].Value -eq 120) "$currentConfiguration exit rendered count=$($exitMarker.Groups['rendered'].Value) expected=120"
        if ($currentConfiguration -eq "Debug")
        {
            $engineExitMarker = [regex]::Match(
                $run.Transcript,
                "exit-after-rendered-frames reached rendered=(?<rendered>\d+) baseline=0 target=120")
            Assert-CameraWorld $engineExitMarker.Success "$currentConfiguration engine rendered-frame exit target witness missing"
            Assert-CameraWorld ([uint64]$engineExitMarker.Groups['rendered'].Value -eq 120) "$currentConfiguration engine rendered count=$($engineExitMarker.Groups['rendered'].Value) expected=120"
        }
        Assert-CameraWorld (-not ($run.Transcript -match "(?i)(VUID-|Validation Error)")) "$currentConfiguration validation diagnostic detected"
        Write-Output "CAMERA_WORLD_SMOKE result=pass configuration=$currentConfiguration rendered=$($completeMarker.Groups['rendered'].Value) requested_target=120 exit=0 registered=1 sync=1 complete=1 exit_marker=1 root_alive=0 remaining_owned=0"
    }
    $success = $true
}
finally
{
    if ($success -and (Test-Path -LiteralPath $runRoot))
    {
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
    elseif (Test-Path -LiteralPath $runRoot)
    {
        Write-Output "CAMERA_WORLD_SMOKE result=failed evidence_path=$runRoot"
    }
}
