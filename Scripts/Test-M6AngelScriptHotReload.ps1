param(
    [string]$BuildDirectory = "build",
    [string]$Configuration = "Debug",
    [int]$TimeoutSeconds = 90,
    [switch]$V2XMutationProbe,
    [switch]$QuietPrimaryProbe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-M6([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw $Message
    }
}

function Add-M6CleanupFailure([System.Collections.ArrayList]$CleanupFailures, [string]$Context, $ErrorRecord)
{
    [void]$CleanupFailures.Add("${Context}: $($ErrorRecord.Exception.Message)")
}

function Assert-M6ExactChildPath([string]$Candidate, [string]$ExpectedParent, [string]$ExpectedLeaf)
{
    $candidateFull = [IO.Path]::GetFullPath($Candidate)
    $parentFull = [IO.Path]::GetFullPath($ExpectedParent).TrimEnd('\')
    Assert-M6 ([IO.Path]::GetDirectoryName($candidateFull).TrimEnd('\') -eq $parentFull) "cleanup parent mismatch: $candidateFull"
    Assert-M6 ([IO.Path]::GetFileName($candidateFull) -eq $ExpectedLeaf) "cleanup basename mismatch: $candidateFull"
}

function Remove-M6FileIfPresent([string]$Path, [System.Collections.ArrayList]$CleanupFailures, [string]$Context)
{
    try
    {
        if (Test-Path -LiteralPath $Path)
        {
            Remove-Item -LiteralPath $Path -Force
        }
        Assert-M6 (-not (Test-Path -LiteralPath $Path)) "$Context still exists: $Path"
    }
    catch
    {
        Add-M6CleanupFailure $CleanupFailures $Context $_
    }
}

function Replace-M6File([string]$Target, [string]$Text, [System.Collections.ArrayList]$CleanupFailures)
{
    $stage = "$Target.m6-staging-$([guid]::NewGuid().ToString('N'))"
    $backup = "$Target.m6-backup-$([guid]::NewGuid().ToString('N'))"
    $operationFailure = $null
    $cleanupFailureCount = $CleanupFailures.Count
    try
    {
        $normalizedText = $Text -replace '\r?\n', "`r`n"
        [IO.File]::WriteAllText($stage, $normalizedText, [Text.UTF8Encoding]::new($false))
        [IO.File]::Replace($stage, $Target, $backup)
    }
    catch
    {
        $operationFailure = $_
    }

    Remove-M6FileIfPresent $stage $CleanupFailures "staging cleanup"
    Remove-M6FileIfPresent $backup $CleanupFailures "backup cleanup"

    if ($null -ne $operationFailure)
    {
        throw $operationFailure
    }
    if ($CleanupFailures.Count -ne $cleanupFailureCount)
    {
        throw "replacement cleanup failed"
    }
}

function Get-M6Marker([string]$LogPath, [string]$StdoutPath, [string]$Stage)
{
    foreach ($path in @($LogPath, $StdoutPath))
    {
        if (-not (Test-Path -LiteralPath $path))
        {
            continue
        }
        $matches = @(Get-Content -LiteralPath $path | Where-Object { $_ -match "M6_SCRIPT_SMOKE stage=$Stage " })
        Assert-M6 ($matches.Count -le 1) "duplicate marker stage=$Stage"
        if ($matches.Count -eq 1)
        {
            return $matches[0]
        }
    }
    return $null
}

function Get-M6TranscriptText([string]$LogPath, [string]$StdoutPath)
{
    foreach ($path in @($LogPath, $StdoutPath))
    {
        if (Test-Path -LiteralPath $path)
        {
            $text = [IO.File]::ReadAllText($path)
            if ($text -match 'M6_SCRIPT_SMOKE')
            {
                return $text
            }
        }
    }
    return ""
}

function Wait-M6Marker([string]$LogPath, [string]$StdoutPath, [string]$Stage, [datetime]$Deadline, [System.Diagnostics.Process]$Process)
{
    while ((Get-Date) -lt $Deadline)
    {
        $marker = Get-M6Marker $LogPath $StdoutPath $Stage
        if ($null -ne $marker)
        {
            return $marker
        }
        $Process.Refresh()
        if ($Process.HasExited)
        {
            $Process.WaitForExit()
            $detail = Get-M6TranscriptText $LogPath $StdoutPath
            if ([string]::IsNullOrEmpty($detail)) { $detail = 'M6 marker output missing' }
            throw "Game exited before stage=$Stage exit=$($Process.ExitCode) log=$detail"
        }
        Start-Sleep -Milliseconds 100
    }
    throw "deadline exceeded waiting for stage=$Stage"
}

function ConvertFrom-M6Marker([string]$Line, [string]$ExpectedStage)
{
    $pattern = '.*M6_SCRIPT_SMOKE stage=(?<stage>ready_good|ready_bad|complete) pid=(?<pid>\d+) generation=(?<generation>\d+) runtime_generation=(?<runtime>\d+) active_bindings=(?<bindings>\d+) position_x=(?<x>-?\d+(?:\.\d+)?) position_y=(?<y>-?\d+(?:\.\d+)?) anchor_z=(?<z>-?\d+(?:\.\d+)?)(?<v2_initial> v2_initial_x=(?<v2_x>-?\d+(?:\.\d+)?))?(?<complete> bad_source_observed=(?<bad_source>\d+) old_generation_continues=(?<old_generation>\d+) exit_code=(?<exit>\d+))?$'
    $match = [regex]::Match($Line, $pattern)
    Assert-M6 $match.Success "malformed marker: $Line"
    Assert-M6 ($match.Groups['stage'].Value -eq $ExpectedStage) "marker stage mismatch: $Line"
    if ($ExpectedStage -eq 'complete')
    {
        Assert-M6 $match.Groups['complete'].Success "complete marker fields missing"
        Assert-M6 ($match.Groups['bad_source'].Value -eq '1' -and $match.Groups['old_generation'].Value -eq '1' -and $match.Groups['exit'].Value -eq '0') "complete state mismatch"
    }
    else
    {
        Assert-M6 (-not $match.Groups['complete'].Success) "unexpected complete fields: $Line"
    }
    if ($ExpectedStage -eq 'ready_good')
    {
        Assert-M6 (-not $match.Groups['v2_initial'].Success) "unexpected v2 baseline: $Line"
    }
    else
    {
        Assert-M6 $match.Groups['v2_initial'].Success "v2 baseline missing: $Line"
    }
    return [pscustomobject]@{
        Pid = [uint32]$match.Groups['pid'].Value
        Generation = [uint32]$match.Groups['generation'].Value
        RuntimeGeneration = [uint64]$match.Groups['runtime'].Value
        ActiveBindings = [uint32]$match.Groups['bindings'].Value
        X = [double]$match.Groups['x'].Value
        Y = [double]$match.Groups['y'].Value
        Z = [double]$match.Groups['z'].Value
        V2InitialX = if ($match.Groups['v2_initial'].Success) { [double]$match.Groups['v2_x'].Value } else { [double]::NaN }
    }
}

function Assert-M6Transcript([string]$ReadyGood, [string]$ReadyBad, [string]$Complete, [uint32]$LaunchedProcessId, [string]$LogText)
{
    foreach ($stage in @('ready_good', 'ready_bad', 'complete'))
    {
        $count = [regex]::Matches($LogText, "M6_SCRIPT_SMOKE stage=$stage ").Count
        Assert-M6 ($count -eq 1) "marker count mismatch stage=$stage count=$count"
    }
    $goodIndex = $LogText.IndexOf($ReadyGood)
    $badIndex = $LogText.IndexOf($ReadyBad)
    $completeIndex = $LogText.IndexOf($Complete)
    Assert-M6 ($goodIndex -ge 0 -and $goodIndex -lt $badIndex -and $badIndex -lt $completeIndex) "marker order mismatch"

    $good = ConvertFrom-M6Marker $ReadyGood 'ready_good'
    $bad = ConvertFrom-M6Marker $ReadyBad 'ready_bad'
    $finished = ConvertFrom-M6Marker $Complete 'complete'
    foreach ($marker in @($good, $bad, $finished))
    {
        Assert-M6 ($marker.Pid -eq $LaunchedProcessId) "marker PID mismatch"
    }
    Assert-M6 ($good.Generation -eq 1 -and $bad.Generation -eq 2 -and $finished.Generation -eq 2) "script generation mismatch"
    Assert-M6 ($good.RuntimeGeneration -lt [uint64]::MaxValue) "runtime generation overflow"
    $expectedRuntimeGeneration = $good.RuntimeGeneration + 1
    Assert-M6 ($bad.RuntimeGeneration -eq $expectedRuntimeGeneration -and $finished.RuntimeGeneration -eq $expectedRuntimeGeneration) "runtime generation transition mismatch"
    Assert-M6 ($good.ActiveBindings -eq $bad.ActiveBindings -and $bad.ActiveBindings -eq $finished.ActiveBindings) "binding count changed"
    Assert-M6 ($good.Y -eq 0.0 -and $good.X -gt 0.0 -and $good.Z -eq 1.0) "v1 position or anchor mismatch"
    Assert-M6 ($bad.X -eq $bad.V2InitialX -and $bad.Y -gt $good.Y -and $bad.Z -eq 2.0) "v2 position or anchor mismatch"
    Assert-M6 ($finished.X -eq $finished.V2InitialX -and $finished.X -eq $bad.X -and $finished.Y -gt $bad.Y -and $finished.Z -eq 2.0) "bad-source position or anchor mismatch"
    return [pscustomobject]@{ Good = $good; Bad = $bad; Complete = $finished }
}

function Assert-M6Summary([string]$Summary, [uint32]$LaunchedProcessId, $Transcript, [string]$ScriptHash, [string]$SceneHash)
{
    $pattern = '^M6_ANGELSCRIPT_ACCEPTANCE result=pass pid=(?<pid>\d+) script_generations=(?<script>\d+->\d+->\d+) runtime_generations=(?<runtime>\d+->\d+->\d+) tracked_script_sha256_before=(?<script_before>[A-F0-9]{64}) tracked_script_sha256_after=(?<script_after>[A-F0-9]{64}) tracked_scene_sha256_before=(?<scene_before>[A-F0-9]{64}) tracked_scene_sha256_after=(?<scene_after>[A-F0-9]{64}) temp_asset_cleanup=(?<asset_cleanup>[01]) external_cwd_cleanup=(?<cwd_cleanup>[01]) no_option_baseline=(?<baseline>[01]) no_option_cleanup=(?<baseline_cleanup>[01]) exit_code=(?<exit>\d+)$'
    $match = [regex]::Match($Summary, $pattern)
    Assert-M6 $match.Success "malformed acceptance summary"
    Assert-M6 ([uint32]$match.Groups['pid'].Value -eq $LaunchedProcessId) "summary PID mismatch"
    Assert-M6 ($match.Groups['script'].Value -eq '1->2->2') "summary script generations mismatch"
    $expectedRuntime = "$($Transcript.Good.RuntimeGeneration)->$($Transcript.Bad.RuntimeGeneration)->$($Transcript.Complete.RuntimeGeneration)"
    Assert-M6 ($match.Groups['runtime'].Value -eq $expectedRuntime) "summary runtime generations mismatch"
    Assert-M6 ($match.Groups['script_before'].Value -eq $ScriptHash -and $match.Groups['script_after'].Value -eq $ScriptHash) "summary script hash mismatch"
    Assert-M6 ($match.Groups['scene_before'].Value -eq $SceneHash -and $match.Groups['scene_after'].Value -eq $SceneHash) "summary scene hash mismatch"
    Assert-M6 ($match.Groups['asset_cleanup'].Value -eq '1' -and $match.Groups['cwd_cleanup'].Value -eq '1' -and $match.Groups['baseline'].Value -eq '1' -and $match.Groups['baseline_cleanup'].Value -eq '1' -and $match.Groups['exit'].Value -eq '0') "summary cleanup or exit mismatch"
}

function Assert-M6NoSmokeMarker([string]$Path, [string]$Name)
{
    if (Test-Path -LiteralPath $Path)
    {
        Assert-M6 (-not ([IO.File]::ReadAllText($Path) -match 'M6_SCRIPT_SMOKE')) "no-option marker found in $Name"
    }
}

function Assert-M6Rejected([scriptblock]$Action, [string]$Name)
{
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    Assert-M6 $rejected "synthetic parser mutation accepted: $Name"
}

function Assert-M6PrimaryFailurePreserved([bool]$Quiet)
{
    $primaryFailure = $null
    $secondaryFailures = [System.Collections.ArrayList]@()
    try
    {
        throw "primary failure probe"
    }
    catch
    {
        $primaryFailure = $_
    }
    try
    {
        throw "secondary cleanup probe"
    }
    catch
    {
        Add-M6CleanupFailure $secondaryFailures "probe cleanup" $_
    }

    $thrownMessage = $null
    try
    {
        foreach ($secondaryFailure in $secondaryFailures)
        {
            if (-not $Quiet)
            {
                [Console]::Error.WriteLine("M6 cleanup failure: $secondaryFailure")
            }
        }
        throw $primaryFailure
    }
    catch
    {
        $thrownMessage = $_.Exception.Message
    }
    Assert-M6 ($thrownMessage -eq "primary failure probe") "primary failure probe was replaced"
    Write-Output "M6_PRIMARY_FAILURE_PROBE THROWN_MESSAGE=$thrownMessage"
}

function Assert-M6SyntheticParser()
{
    $hash = ('A' * 64) -join ''
    $good = "M6_SCRIPT_SMOKE stage=ready_good pid=42 generation=1 runtime_generation=7 active_bindings=1 position_x=1 position_y=0 anchor_z=1"
    $bad = "M6_SCRIPT_SMOKE stage=ready_bad pid=42 generation=2 runtime_generation=8 active_bindings=1 position_x=1 position_y=2 anchor_z=2 v2_initial_x=1"
    $complete = "M6_SCRIPT_SMOKE stage=complete pid=42 generation=2 runtime_generation=8 active_bindings=1 position_x=1 position_y=4 anchor_z=2 v2_initial_x=1 bad_source_observed=1 old_generation_continues=1 exit_code=0"
    $log = "$good`n$bad`n$complete"
    $transcript = Assert-M6Transcript $good $bad $complete 42 $log
    $summary = "M6_ANGELSCRIPT_ACCEPTANCE result=pass pid=42 script_generations=1->2->2 runtime_generations=7->8->8 tracked_script_sha256_before=$hash tracked_script_sha256_after=$hash tracked_scene_sha256_before=$hash tracked_scene_sha256_after=$hash temp_asset_cleanup=1 external_cwd_cleanup=1 no_option_baseline=1 no_option_cleanup=1 exit_code=0"
    Assert-M6Summary $summary 42 $transcript $hash $hash

    $runtimeJump = $bad -replace 'runtime_generation=8', 'runtime_generation=9'
    Assert-M6Rejected { Assert-M6Transcript $good $runtimeJump $complete 42 "$good`n$runtimeJump`n$complete" } 'runtime jump'
    $bindingLeak = $bad -replace 'active_bindings=1', 'active_bindings=2'
    Assert-M6Rejected { Assert-M6Transcript $good $bindingLeak $complete 42 "$good`n$bindingLeak`n$complete" } 'binding leak'
    $v1YChanged = $good -replace 'position_y=0', 'position_y=1'
    Assert-M6Rejected { Assert-M6Transcript $v1YChanged $bad $complete 42 "$v1YChanged`n$bad`n$complete" } 'v1 Y changed'
    $v2XChanged = $bad -replace 'position_x=1', 'position_x=2'
    Assert-M6Rejected { Assert-M6Transcript $good $v2XChanged $complete 42 "$good`n$v2XChanged`n$complete" } 'v2 X changed'
    $v2PositionUnchanged = $bad -replace 'position_y=2', 'position_y=0'
    Assert-M6Rejected { Assert-M6Transcript $good $v2PositionUnchanged $complete 42 "$good`n$v2PositionUnchanged`n$complete" } 'v2 position unchanged'
    $badPositionUnchanged = $complete -replace 'position_y=4', 'position_y=2'
    Assert-M6Rejected { Assert-M6Transcript $good $bad $badPositionUnchanged 42 "$good`n$bad`n$badPositionUnchanged" } 'bad-source position unchanged'
    Assert-M6Rejected { Assert-M6Transcript $good $bad $complete 42 "$log`n$good" } 'duplicate marker'
    Assert-M6Rejected { Assert-M6Transcript $good $bad $complete 43 $log } 'PID mismatch'
    $otherHash = ('B' * 64) -join ''
    Assert-M6Rejected { Assert-M6Summary ($summary -replace 'tracked_script_sha256_after=A{64}', "tracked_script_sha256_after=$otherHash") 42 $transcript $hash $hash } 'hash mismatch'
    Assert-M6Rejected { Assert-M6Summary ($summary -replace 'temp_asset_cleanup=1', 'temp_asset_cleanup=0') 42 $transcript $hash $hash } 'cleanup mismatch'
    Assert-M6Rejected { Assert-M6Summary ($summary -replace 'no_option_baseline=1', 'no_option_baseline=0') 42 $transcript $hash $hash } 'no-option baseline mismatch'
    Assert-M6Rejected { Assert-M6Summary ($summary -replace 'pid=42', 'pid=43') 42 $transcript $hash $hash } 'summary PID mismatch'
    Assert-M6Rejected { Assert-M6Summary ($summary -replace 'exit_code=0', 'exit_code=1') 42 $transcript $hash $hash } 'exit mismatch'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$gamePath = Join-Path $repoRoot "$BuildDirectory\Game\$Configuration\Game.exe"
$trackedScript = Join-Path $repoRoot "Assets\Scripts\M6Mover.as"
$trackedScene = Join-Path $repoRoot "Assets\Scenes\M6AngelScriptDemo.scene.json"
$assetRoot = Join-Path $repoRoot "Assets"
$tempAsset = $null
$tempAssetLeaf = $null
$externalCwd = $null
$externalCwdLeaf = $null
$process = $null
$primaryFailure = $null
$cleanupFailures = [System.Collections.ArrayList]@()
$tempAssetCreated = $false
$externalCwdCreated = $false
$tempAssetCleanup = 0
$externalCwdCleanup = 0
$noOptionBaseline = 0
$processExitCode = $null
$logPath = $null
$stdout = $null
$stderr = $null
$scriptBefore = (Get-FileHash -LiteralPath $trackedScript -Algorithm SHA256).Hash
$sceneBefore = (Get-FileHash -LiteralPath $trackedScene -Algorithm SHA256).Hash
$scriptAfter = $null
$sceneAfter = $null
$transcript = $null

try
{
    Assert-M6 (Test-Path -LiteralPath $gamePath) "Game executable not found: $gamePath"
    Assert-M6SyntheticParser
    Assert-M6PrimaryFailurePreserved $QuietPrimaryProbe
    if (-not $V2XMutationProbe)
    {
        $probeOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -BuildDirectory $BuildDirectory -Configuration $Configuration -TimeoutSeconds $TimeoutSeconds -V2XMutationProbe -QuietPrimaryProbe 2>$null
        Assert-M6 ($LASTEXITCODE -eq 0) "v2 X mutation probe harness failed: $($probeOutput -join ' | ')"
        Assert-M6 (($probeOutput -join "`n") -match 'M6_ANGELSCRIPT_ACCEPTANCE result=mutation_rejected .*game_exit_code=1') "v2 X mutation probe did not reject the Game run"
    }
    $tempAssetLeaf = ".m6-acceptance-$([guid]::NewGuid().ToString('N'))"
    $tempAsset = Join-Path $assetRoot $tempAssetLeaf
    New-Item -ItemType Directory -Path $tempAsset | Out-Null
    $tempAssetCreated = $true
    $tempScript = Join-Path $tempAsset "M6Mover.as"
    $tempScene = Join-Path $tempAsset "M6AngelScriptDemo.scene.json"
    Copy-Item -LiteralPath $trackedScript -Destination $tempScript
    $sceneText = [IO.File]::ReadAllText($trackedScene)
    $logicalScript = "$tempAssetLeaf/M6Mover.as"
    [IO.File]::WriteAllText($tempScene, $sceneText.Replace("Scripts/M6Mover.as", $logicalScript), [Text.UTF8Encoding]::new($false))

    $externalCwdLeaf = "NorvesLibM6Acceptance-$([guid]::NewGuid().ToString('N'))"
    $externalCwd = Join-Path ([IO.Path]::GetTempPath()) $externalCwdLeaf
    New-Item -ItemType Directory -Path $externalCwd | Out-Null
    $externalCwdCreated = $true
    if (-not $V2XMutationProbe)
    {
        Assert-M6ExactChildPath $externalCwd ([IO.Path]::GetTempPath()) $externalCwdLeaf
        $baselineStdout = Join-Path $externalCwd "no-option.stdout.txt"
        $baselineStderr = Join-Path $externalCwd "no-option.stderr.txt"
        $baselineLog = Join-Path $externalCwd "Game.log"
        $baselineProcess = Start-Process -FilePath $gamePath -ArgumentList "--exit-after-frames=2","--render-thread=st" -WorkingDirectory $externalCwd -WindowStyle Hidden -PassThru -RedirectStandardOutput $baselineStdout -RedirectStandardError $baselineStderr
        $null = $baselineProcess.Handle
        $baselineProcess.WaitForExit()
        Assert-M6 ($baselineProcess.ExitCode -eq 0) "no-option Game exit=$($baselineProcess.ExitCode)"
        Assert-M6NoSmokeMarker $baselineStdout "no-option stdout"
        Assert-M6NoSmokeMarker $baselineStderr "no-option stderr"
        Assert-M6NoSmokeMarker $baselineLog "no-option Game.log"
        if ($Configuration -eq "Debug")
        {
            Assert-M6 ((Test-Path -LiteralPath $baselineLog) -and ([IO.File]::ReadAllText($baselineLog) -match 'ApplicationProcessor::Tick\(\) - exit-after-frames reached frame=')) "no-option Debug exit witness missing"
        }
        $noOptionBaseline = 1
    }
    $stdout = Join-Path $externalCwd "stdout.txt"
    $stderr = Join-Path $externalCwd "stderr.txt"
    $process = Start-Process -FilePath $gamePath -ArgumentList "--m6-script-smoke=$tempAssetLeaf/M6AngelScriptDemo.scene.json","--render-thread=st" -WorkingDirectory $externalCwd -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $logPath = Join-Path $externalCwd "Game.log"

    $readyGood = Wait-M6Marker $logPath $stdout "ready_good" $deadline $process
    $v2 = @"
class M6Mover
{
    void BeginPlay(EntityRef owner)
    {
        Vector3 position = owner.GetPosition();
        position.z = 2.0f;
        owner.SetPosition(position);
    }

    void Tick(EntityRef owner, float deltaSeconds)
    {
        Vector3 position = owner.GetPosition();
        position.y += 2.0f;
        owner.SetPosition(position);
    }
}
"@
    if ($V2XMutationProbe)
    {
        $v2 = $v2.Replace("position.y += 2.0f;", "position.x += 100.0f;`r`n        position.y += 2.0f;")
    }
    Replace-M6File $tempScript $v2 $cleanupFailures
    $readyBad = Wait-M6Marker $logPath $stdout "ready_bad" $deadline $process

    $badFixture = @"
class M6Mover
{
    void Tick(EntityRef owner, float deltaSeconds)
    {
        this is a compile error
    }
}
"@
    $badFixturePath = Join-Path $tempAsset "M6Mover.bad.fixture"
    $badFixtureText = $badFixture -replace '\r?\n', "`r`n"
    [IO.File]::WriteAllText($badFixturePath, $badFixtureText, [Text.UTF8Encoding]::new($false))
    Replace-M6File $tempScript $badFixture $cleanupFailures
    Assert-M6 ((Get-FileHash -LiteralPath $tempScript -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $badFixturePath -Algorithm SHA256).Hash) "bad fixture SHA256 mismatch"
    $complete = Wait-M6Marker $logPath $stdout "complete" $deadline $process
    $process.WaitForExit()
    Assert-M6 ($process.ExitCode -eq 0) "Game exit=$($process.ExitCode)"
    $logText = Get-M6TranscriptText $logPath $stdout
    $transcript = Assert-M6Transcript $readyGood $readyBad $complete $process.Id $logText
    $readyBadIndex = $logText.IndexOf($readyBad)
    $completeIndex = $logText.IndexOf($complete)
    if (Test-Path -LiteralPath $logPath)
    {
        $maintenanceLog = [IO.File]::ReadAllText($logPath)
        if ($maintenanceLog -match 'M6_SCRIPT_SMOKE')
        {
            $maintenanceIndex = $maintenanceLog.IndexOf("ScriptRuntime BeginFrameMaintenance failed", $maintenanceLog.IndexOf($readyBad))
            Assert-M6 ($maintenanceIndex -gt 0 -and $maintenanceIndex -lt $maintenanceLog.IndexOf($complete)) "maintenance failure log order mismatch"
        }
    }
}
catch
{
    $primaryFailure = $_
}
finally
{
    try
    {
        if ($null -ne $process)
        {
            $process.Refresh()
            if (-not $process.HasExited)
            {
                Stop-Process -Id $process.Id -Force
            }
            $process.WaitForExit()
            $processExitCode = $process.ExitCode
        }
    }
    catch
    {
        Add-M6CleanupFailure $cleanupFailures "process collection" $_
    }

    if ($tempAssetCreated)
    {
        try
        {
            Assert-M6ExactChildPath $tempAsset $assetRoot $tempAssetLeaf
            if (Test-Path -LiteralPath $tempAsset)
            {
                Remove-Item -LiteralPath $tempAsset -Recurse -Force
            }
            Assert-M6 (-not (Test-Path -LiteralPath $tempAsset)) "temp asset cleanup left target behind"
            $tempAssetCleanup = 1
        }
        catch
        {
            Add-M6CleanupFailure $cleanupFailures "temp asset cleanup" $_
        }
    }

    if ($externalCwdCreated)
    {
        try
        {
            Assert-M6ExactChildPath $externalCwd ([IO.Path]::GetTempPath()) $externalCwdLeaf
            if (Test-Path -LiteralPath $externalCwd)
            {
                Remove-Item -LiteralPath $externalCwd -Recurse -Force
            }
            Assert-M6 (-not (Test-Path -LiteralPath $externalCwd)) "external cwd cleanup left target behind"
            $externalCwdCleanup = 1
        }
        catch
        {
            Add-M6CleanupFailure $cleanupFailures "external cwd cleanup" $_
        }
    }
}

try
{
    $scriptAfter = (Get-FileHash -LiteralPath $trackedScript -Algorithm SHA256).Hash
    $sceneAfter = (Get-FileHash -LiteralPath $trackedScene -Algorithm SHA256).Hash
    Assert-M6 ($scriptBefore -eq $scriptAfter) "tracked script hash changed"
    Assert-M6 ($sceneBefore -eq $sceneAfter) "tracked scene hash changed"
}
catch
{
    if ($null -eq $primaryFailure)
    {
        $primaryFailure = $_
    }
    else
    {
        Add-M6CleanupFailure $cleanupFailures "post-run hash validation" $_
    }
}

if ($V2XMutationProbe)
{
    Assert-M6 ($null -ne $primaryFailure) "v2 X mutation unexpectedly completed"
    Assert-M6 ($primaryFailure.Exception.Message -match 'v2_changed_x') "v2 X mutation did not report controller failure: $($primaryFailure.Exception.Message)"
    Assert-M6 ($cleanupFailures.Count -eq 0) "v2 X mutation cleanup failures: $($cleanupFailures -join ' | ')"
    Assert-M6 ($scriptBefore -eq $scriptAfter -and $sceneBefore -eq $sceneAfter) "v2 X mutation changed tracked hashes"
    Assert-M6 ($tempAssetCleanup -eq 1 -and $externalCwdCleanup -eq 1 -and $processExitCode -eq 1) "v2 X mutation cleanup or Game exit mismatch"
    Write-Output "M6_ANGELSCRIPT_ACCEPTANCE result=mutation_rejected game_exit_code=$processExitCode tracked_script_sha256_before=$scriptBefore tracked_script_sha256_after=$scriptAfter tracked_scene_sha256_before=$sceneBefore tracked_scene_sha256_after=$sceneAfter temp_asset_cleanup=$tempAssetCleanup external_cwd_cleanup=$externalCwdCleanup"
    exit 0
}
if ($null -ne $primaryFailure)
{
    foreach ($cleanupFailure in $cleanupFailures)
    {
        [Console]::Error.WriteLine("M6 cleanup failure: $cleanupFailure")
    }
    throw $primaryFailure
}
if ($cleanupFailures.Count -ne 0)
{
    throw "M6 cleanup failures: $($cleanupFailures -join ' | ')"
}
Assert-M6 ($null -ne $process -and $null -ne $transcript -and $processExitCode -eq 0) "process collection or transcript missing"
Assert-M6 ($tempAssetCleanup -eq 1 -and $externalCwdCleanup -eq 1) "required cleanup did not complete"
Assert-M6 ($noOptionBaseline -eq 1) "no-option baseline did not complete"
$summary = "M6_ANGELSCRIPT_ACCEPTANCE result=pass pid=$($process.Id) script_generations=1->2->2 runtime_generations=$($transcript.Good.RuntimeGeneration)->$($transcript.Bad.RuntimeGeneration)->$($transcript.Complete.RuntimeGeneration) tracked_script_sha256_before=$scriptBefore tracked_script_sha256_after=$scriptAfter tracked_scene_sha256_before=$sceneBefore tracked_scene_sha256_after=$sceneAfter temp_asset_cleanup=$tempAssetCleanup external_cwd_cleanup=$externalCwdCleanup no_option_baseline=$noOptionBaseline no_option_cleanup=$externalCwdCleanup exit_code=$processExitCode"
Assert-M6Summary $summary $process.Id $transcript $scriptBefore $sceneBefore
Write-Output $summary
