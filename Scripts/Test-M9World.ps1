[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"

function Assert-M9([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw "M9 world acceptance failed: $Message"
    }
}

function Get-M9Sha256([string]$Path)
{
    $stream = $null
    $hasher = $null
    try
    {
        $stream = [IO.File]::OpenRead($Path)
        $hasher = [Security.Cryptography.SHA256]::Create()
        $hash = $hasher.ComputeHash($stream)
        return ([BitConverter]::ToString($hash)).Replace("-", "")
    }
    finally
    {
        if ($null -ne $hasher)
        {
            $hasher.Dispose()
        }
        if ($null -ne $stream)
        {
            $stream.Dispose()
        }
    }
}

function Write-M9FixtureBin([string]$Path)
{
    $bytes = [byte[]]::new(416)
    function Write-Single([int]$Offset, [single]$Value) { [BitConverter]::GetBytes($Value).CopyTo($bytes, $Offset) }
    function Write-U16([int]$Offset, [uint16]$Value) { [BitConverter]::GetBytes($Value).CopyTo($bytes, $Offset) }
    $positions = [single[]](0,0,0, 1,0,0, 0,1,0)
    $normals = [single[]](0,0,1, 0,0,1, 0,0,1)
    $texCoords = [single[]](0,0, 1,0, 0,1)
    for ($index = 0; $index -lt 9; ++$index) { Write-Single ($index * 4) $positions[$index]; Write-Single (36 + $index * 4) $normals[$index] }
    for ($index = 0; $index -lt 6; ++$index) { Write-Single (72 + $index * 4) $texCoords[$index] }
    $joints = [byte[]](0,1,0,0, 0,1,0,0, 1,0,0,0)
    $weights = [single[]](0.75,0.25,0,0, 0.5,0.5,0,0, 1,0,0,0)
    for ($index = 0; $index -lt 12; ++$index) { $bytes[96 + $index] = $joints[$index]; Write-Single (132 + $index * 4) $weights[$index] }
    Write-U16 216 0; Write-U16 218 1; Write-U16 220 2
    foreach ($element in [int[]](0,5,10,15)) { Write-Single (224 + $element * 4) 1; Write-Single (288 + $element * 4) 1 }
    Write-Single (288 + 13 * 4) -1; Write-Single 352 0; Write-Single 356 2
    $translations = [single[]](0,1,0, 0,3,0); $rotations = [single[]](0,0,0,1, 0,0,1,0)
    for ($index = 0; $index -lt 6; ++$index) { Write-Single (360 + $index * 4) $translations[$index] }
    for ($index = 0; $index -lt 8; ++$index) { Write-Single (384 + $index * 4) $rotations[$index] }
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Write-M9Wave([string]$Path, [int16]$Amplitude)
{
    $frameCount = 2880000
    $pcmBytes = $frameCount * 2
    $bytes = [byte[]]::new(44 + $pcmBytes)
    [Text.Encoding]::ASCII.GetBytes("RIFF").CopyTo($bytes, 0)
    [BitConverter]::GetBytes([uint32](36 + $pcmBytes)).CopyTo($bytes, 4)
    [Text.Encoding]::ASCII.GetBytes("WAVEfmt ").CopyTo($bytes, 8)
    [BitConverter]::GetBytes([uint32]16).CopyTo($bytes, 16)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 20)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 22)
    [BitConverter]::GetBytes([uint32]48000).CopyTo($bytes, 24)
    [BitConverter]::GetBytes([uint32]96000).CopyTo($bytes, 28)
    [BitConverter]::GetBytes([uint16]2).CopyTo($bytes, 32)
    [BitConverter]::GetBytes([uint16]16).CopyTo($bytes, 34)
    [Text.Encoding]::ASCII.GetBytes("data").CopyTo($bytes, 36)
    [BitConverter]::GetBytes([uint32]$pcmBytes).CopyTo($bytes, 40)
    for ($frame = 0; $frame -lt $frameCount; ++$frame) { [BitConverter]::GetBytes($Amplitude).CopyTo($bytes, 44 + $frame * 2) }
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Invoke-M9Tool([string]$Exe, [string[]]$Arguments)
{
    $script:AssetCookInvocations++
    $priorPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & $Exe @Arguments 2>&1
    $ErrorActionPreference = $priorPreference
    if ($LASTEXITCODE -ne 0) { throw "AssetCook failed: $($output -join "`n")" }
}

function Invoke-M9Game([string]$GamePath, [string[]]$Arguments, [string]$RunDirectory)
{
    $stdout = Join-Path $RunDirectory "stdout.txt"; $stderr = Join-Path $RunDirectory "stderr.txt"
    $process = Start-Process -FilePath $GamePath -ArgumentList $Arguments -WorkingDirectory $RunDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw "Game timeout pid=$($process.Id) args=$($Arguments -join ' ') evidence_path=$RunDirectory"
    }
    $process.Refresh()
    $log = Join-Path $RunDirectory "Game.log"
    $text = ""
    foreach ($path in @($stdout, $stderr, $log)) { if (Test-Path -LiteralPath $path) { $text += [IO.File]::ReadAllText($path) + "`n" } }
    return [pscustomobject]@{ ExitCode = [int]$process.ExitCode; Transcript = $text }
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
$gamePath = Join-Path $resolvedBuild "Game\$Configuration\Game.exe"
$assetCookPath = Join-Path $resolvedBuild "Tools\AssetCook\$Configuration\AssetCook.exe"
$trackedModel = Join-Path $repoRoot "Assets\Models\M9Skinned\ValidU8Float.gltf"
$trackedHashBefore = Get-M9Sha256 $trackedModel
$runRoot = Join-Path ([IO.Path]::GetTempPath()) "NorvesLibM9World-$([guid]::NewGuid().ToString('N'))"
$script:AssetCookInvocations = 0
$success = $false

try
{
    Assert-M9 (Test-Path -LiteralPath $gamePath) "Game executable not found: $gamePath"
    Assert-M9 (Test-Path -LiteralPath $assetCookPath) "AssetCook executable not found: $assetCookPath"
    New-Item -ItemType Directory -Path (Join-Path $runRoot "RuntimeRoot\Cooked"), (Join-Path $runRoot "Source") | Out-Null
    $runtimeRoot = Join-Path $runRoot "RuntimeRoot"; $sourceRoot = Join-Path $runRoot "Source"
    $gltf = Join-Path $sourceRoot "ValidU8Float.gltf"; $bin = Join-Path $sourceRoot "fixture.bin"
    $effect = Join-Path $sourceRoot "effect.wav"; $loop = Join-Path $sourceRoot "loop.wav"
    $manifest = Join-Path $runtimeRoot "manifest.json"
    Copy-Item -LiteralPath $trackedModel -Destination $gltf
    Write-M9FixtureBin $bin; Write-M9Wave $effect 256; Write-M9Wave $loop 512
    Set-Content -LiteralPath $manifest -Value '{"version":1,"assets":[]}' -Encoding utf8

    Invoke-M9Tool $assetCookPath @("--input", $gltf, "--out", (Join-Path $runtimeRoot "Cooked\m9.nvpkg"), "--manifest", $manifest, "--logical", "Models/M9Skinned/ValidU8Float.gltf", "--kind", "model", "--entry", "m9.nvskel", "--entry-type", "Skl0", "--format", "nvskel.v0.skinned.pnujiw.u32", "--variant", "default")
    Invoke-M9Tool $assetCookPath @("--input", $effect, "--out", (Join-Path $runtimeRoot "Cooked\effect.nvpkg"), "--manifest", $manifest, "--logical", "Audio/M9/effect.wav", "--kind", "audio", "--entry", "effect.nvaud", "--entry-type", "Aud0", "--format", "nvaud.v0.pcm16", "--variant", "default")
    Invoke-M9Tool $assetCookPath @("--input", $loop, "--out", (Join-Path $runtimeRoot "Cooked\loop.nvpkg"), "--manifest", $manifest, "--logical", "Audio/M9/loop.wav", "--kind", "audio", "--entry", "loop.nvaud", "--entry-type", "Aud0", "--format", "nvaud.v0.pcm16", "--variant", "default")
    Assert-M9 ($script:AssetCookInvocations -eq 3) "AssetCook invocation count=$script:AssetCookInvocations expected=3"

    $normalDir = Join-Path $runRoot "normal"; New-Item -ItemType Directory -Path $normalDir | Out-Null
    $normal = Invoke-M9Game $gamePath @("--render-thread=st", "--exit-after-rendered-frames=4") $normalDir
    Assert-M9 ($normal.ExitCode -eq 0) "normal Game exit=$($normal.ExitCode)"
    Assert-M9 (-not ($normal.Transcript -match "M9_WORLD_SMOKE")) "normal Game emitted M9 marker"

    $smokeDir = Join-Path $runRoot "smoke"; New-Item -ItemType Directory -Path $smokeDir | Out-Null
    $smoke = Invoke-M9Game $gamePath @("--m9-world-smoke", "--texture-asset-root", $runtimeRoot, "--texture-asset-manifest", $manifest, "--render-thread=st") $smokeDir
    Assert-M9 ($smoke.ExitCode -eq 0) "M9 Game exit=$($smoke.ExitCode) transcript=$($smoke.Transcript)"
    foreach ($stage in @("registered", "assets_ready", "skeletal_binding", "audio_play", "visual", "audio", "complete"))
    {
        Assert-M9 ([regex]::Matches($smoke.Transcript, "M9_WORLD_SMOKE stage=$stage(?=\s|$)").Count -eq 1) "marker count stage=$stage"
    }
    $visualMarker = [regex]::Match($smoke.Transcript, "M9_WORLD_SMOKE stage=visual t0_frame=\d+ t1_frame=(?<t1>[1-9]\d*) stats_frame=(?<stats>[1-9]\d*) changed_pixels=[1-9]\d* negative_changed_pixels=(?<negative>\d+) pose_changed=(?<pose>[01]) bbox_width=[1-9]\d* bbox_height=[1-9]\d* centroid_x=")
    Assert-M9 $visualMarker.Success "visual marker witness missing"
    Assert-M9 ([int]$visualMarker.Groups['negative'].Value -le 32) "negative control pixel delta exceeded threshold"
    Assert-M9 ($visualMarker.Groups['pose'].Value -eq "1") "skeletal pose did not change"
    Assert-M9 ($visualMarker.Groups['t1'].Value -eq $visualMarker.Groups['stats'].Value) "t1 capture and stats frame mismatch"
    Assert-M9 ($smoke.Transcript -match "gbuffer=[1-9]\d* shadow=[1-9]\d*") "skinned GBuffer/shadow witness missing"
    $skeletalBindingMarker = [regex]::Match(
        $smoke.Transcript,
        "M9_WORLD_SMOKE stage=skeletal_binding resource_translation_x=(?<resource>-?\d+(?:\.\d+)?) component_translation_x=(?<component>-?\d+(?:\.\d+)?)")
    Assert-M9 $skeletalBindingMarker.Success "runtime skeletal binding witness missing"
    Assert-M9 ([single]$skeletalBindingMarker.Groups['resource'].Value -eq 5.0) "resource mesh-node translation was not preserved"
    Assert-M9 ([single]$skeletalBindingMarker.Groups['component'].Value -eq 5.0) "component mesh-node translation was not preserved"
    Assert-M9 ($smoke.Transcript -match "backend=XAudio2") "XAudio2 play witness missing"
    Assert-M9 ($smoke.Transcript -match "effect_stop=1 effect_callback=1 loop_play=1 loop_stop=1 loop_callback=1 voices=0 callbacks=0 shutdown_complete=1") "audio drain witness missing"
    Assert-M9 (-not ($smoke.Transcript -match "M9_WORLD_SMOKE stage=failure")) "M9 failure marker emitted"
    Assert-M9 ((Get-M9Sha256 $trackedModel) -eq $trackedHashBefore) "tracked model hash changed"
    $success = $true
    Write-Output "M9_WORLD_ACCEPTANCE result=pass configuration=$Configuration assetcook_calls=3 tracked_asset_hash_unchanged=1 normal_baseline=1"
}
finally
{
    if ($success -and (Test-Path -LiteralPath $runRoot)) { Remove-Item -LiteralPath $runRoot -Recurse -Force }
}
