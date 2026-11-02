[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$BuildDirectory = "build",
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 120,
    [switch]$ContractOnly,
    [ValidateSet("All", "Cleanup", "Process", "Configuration")]
    [string]$ReviewContract = "All",
    [switch]$LegacyNvskelCompare,
    [string]$EvidenceDir
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

function Get-M9BraceBlock([string]$Source, [string]$DeclarationPattern, [string]$Description)
{
    $declaration = [regex]::Match($Source, $DeclarationPattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    Assert-M9 $declaration.Success "$Description declaration missing"
    $openBrace = $Source.IndexOf('{', $declaration.Index + $declaration.Length)
    Assert-M9 ($openBrace -ge 0) "$Description body missing"
    $depth = 0
    for ($index = $openBrace; $index -lt $Source.Length; ++$index)
    {
        if ($Source[$index] -eq '{') { ++$depth }
        elseif ($Source[$index] -eq '}')
        {
            --$depth
            if ($depth -eq 0) { return $Source.Substring($declaration.Index, $index - $declaration.Index + 1) }
        }
    }
    throw "M9 world acceptance failed: $Description body is unterminated"
}

function Assert-M9OrderedTokens([string]$Source, [string[]]$Tokens, [string]$Description)
{
    $offset = 0
    foreach ($token in $Tokens)
    {
        $found = $Source.IndexOf($token, $offset, [StringComparison]::Ordinal)
        Assert-M9 ($found -ge 0) "$Description missing or out of order token=$token"
        $offset = $found + $token.Length
    }
}

function Test-M9CleanupSourceContract([string]$RepoRoot)
{
    $routinePath = Join-Path $RepoRoot "Game\GameModes\Rendering3DTest\Rendering3DTestRoutine.cpp"
    $source = [IO.File]::ReadAllText($routinePath)
    $cleanup = Get-M9BraceBlock $source '(?m)^\s*void\s+CleanupM9WorldAcceptance\s*\(' "M9 transactional cleanup"
    Assert-M9OrderedTokens $cleanup @(
        'Preparation.Cancel()',
        'UnregisterController(&data.m_CameraController)',
        'audio.Shutdown()',
        'ReleaseBakedAtlas',
        'ReleaseTexture(data.m_F6AtlasTextureHandle)',
        'data.m_pM9SkinnedObject = nullptr',
        'data.m_M9WorldAcceptance->SkeletalAsset.reset()',
        'data.m_M9EffectVoice = {}',
        'data.m_M9StatsHistory.clear()') "M9 transactional cleanup order"
    foreach ($reason in @('skeletal_load_failed', 'skeletal_clip_selection_failed', 'skeletal_attach_failed', 'xaudio2_module_or_clip_unavailable', 'xaudio2_play_failed'))
    {
        $escapedReason = [regex]::Escape($reason)
        $pattern = 'CleanupM9WorldAcceptance\(ctx, data\);\s*FailM9WorldSmoke\(ctx, "{0}"\);' -f $escapedReason
        Assert-M9 ([regex]::IsMatch($source, $pattern)) "failure stage lacks cleanup-before-fail reason=$reason"
    }
    Assert-M9 ([regex]::Matches($source, 'CleanupM9WorldAcceptance\(ctx, data\);').Count -ge 4) "cleanup is not shared by failed Enter and Leave"
}

function Test-M9OwnedProcessSourceContract([string]$ScriptPath)
{
    $source = [IO.File]::ReadAllText($ScriptPath)
    $owned = Get-M9BraceBlock $source '(?m)^function\s+Invoke-M9OwnedProcess\s*\(' "owned process wrapper"
    Assert-M9OrderedTokens $owned @(
        'Start-Process',
        '$process.Id',
        'WaitForExit(',
        'Get-M9OwnedDescendantProcessIds',
        'taskkill.exe',
        '/PID',
        '/T',
        '/F',
        'Refresh()',
        'HasExited',
        '$remainingOwnedProcessIds',
        'evidence_path=') "owned process timeout contract"
    Assert-M9 ($owned -match 'RemainingOwnedProcessIds\s*=') "owned process result omits remaining child evidence"
    $tool = Get-M9BraceBlock $source '(?m)^function\s+Invoke-M9Tool\s*\(' "AssetCook wrapper"
    $game = Get-M9BraceBlock $source '(?m)^function\s+Invoke-M9Game\s*\(' "Game wrapper"
    Assert-M9 ($tool -match 'Invoke-M9OwnedProcess') "AssetCook does not use owned process wrapper"
    Assert-M9 ($game -match 'Invoke-M9OwnedProcess') "Game does not use owned process wrapper"
    Assert-M9 (-not ($source -match '(?i)(Stop-Process|taskkill(?:\.exe)?)\s+[^\r\n]*(?:-Name|/IM)')) "name-based process termination is forbidden"
    Test-M9OwnedProcessBehavior
}

function Test-M9OwnedProcessBehavior()
{
    $selfTestRoot = Join-Path ([IO.Path]::GetTempPath()) "NorvesLibM9OwnedProcessSelfTest-$([guid]::NewGuid().ToString('N'))"
    $selfTestSucceeded = $false
    try
    {
        $successDirectory = Join-Path $selfTestRoot "success"
        $timeoutDirectory = Join-Path $selfTestRoot "timeout"
        New-Item -ItemType Directory -Path $successDirectory, $timeoutDirectory | Out-Null
        $pwshPath = (Get-Process -Id $PID).Path
        $successCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes('exit 7'))
        $successResult = Invoke-M9OwnedProcess $pwshPath @('-NoProfile', '-EncodedCommand', $successCommand) $successDirectory (Join-Path $successDirectory 'stdout.txt') (Join-Path $successDirectory 'stderr.txt') 10 $successDirectory
        Assert-M9 ($successResult.ExitCode -eq 7 -and -not $successResult.RootAlive -and $successResult.RemainingOwnedProcessIds.Count -eq 0) "owned process success-path result is incomplete exit=$($successResult.ExitCode) root_alive=$($successResult.RootAlive) remaining_owned=$($successResult.RemainingOwnedProcessIds.Count) result_type=$($successResult.GetType().FullName)"

        $childCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes('Start-Sleep -Seconds 30'))
        $escapedPwshPath = $pwshPath.Replace("'", "''")
        $parentScript = "`$child = Start-Process -FilePath '$escapedPwshPath' -ArgumentList '-NoProfile','-EncodedCommand','$childCommand' -WindowStyle Hidden -PassThru; Wait-Process -Id `$child.Id"
        $parentCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($parentScript))
        $timeoutObserved = $false
        try
        {
            [void](Invoke-M9OwnedProcess $pwshPath @('-NoProfile', '-EncodedCommand', $parentCommand) $timeoutDirectory (Join-Path $timeoutDirectory 'stdout.txt') (Join-Path $timeoutDirectory 'stderr.txt') 1 $timeoutDirectory)
        }
        catch
        {
            $message = $_.Exception.Message
            Assert-M9 ($message.Contains('owned process timeout')) "owned process self-test saw unexpected failure: $message"
            Assert-M9 ($message.Contains('root_alive=0') -and $message.Contains('remaining_owned=0')) "owned process timeout did not prove root/child cleanup: $message"
            Assert-M9 ($message.Contains("evidence_path=$timeoutDirectory")) "owned process timeout omitted bounded evidence path"
            $timeoutObserved = $true
        }
        Assert-M9 $timeoutObserved "owned process timeout self-test did not time out"
        $selfTestSucceeded = $true
        Write-Output "M9_OWNED_PROCESS_SELF_TEST result=pass root_alive=0 remaining_owned=0 child_tree=1"
    }
    finally
    {
        if ($selfTestSucceeded -and (Test-Path -LiteralPath $selfTestRoot))
        {
            Remove-Item -LiteralPath $selfTestRoot -Recurse -Force
        }
    }
}

function Test-M9ConfigurationSourceContract([string]$RepoRoot)
{
    $cmake = [IO.File]::ReadAllText((Join-Path $RepoRoot "Test\M9\CMakeLists.txt"))
    Assert-M9 ($cmake.Contains('-Configuration "$<CONFIG>"')) "CTest configuration is not selected with `$<CONFIG>"
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

function Get-M9OwnedDescendantProcessIds([int]$RootProcessId)
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

function Invoke-M9OwnedProcess(
    [string]$Exe,
    [string[]]$Arguments,
    [string]$WorkingDirectory,
    [string]$StdoutPath,
    [string]$StderrPath,
    [int]$Timeout,
    [string]$EvidencePath)
{
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath
    $rootProcessId = [int]$process.Id
    [void]$process.Handle
    if (-not $process.WaitForExit($Timeout * 1000))
    {
        $ownedProcessIds = @($rootProcessId) + @(Get-M9OwnedDescendantProcessIds $rootProcessId)
        $taskkillOutput = & taskkill.exe /PID $rootProcessId /T /F 2>&1
        $taskkillExitCode = $LASTEXITCODE
        $rootStopped = $process.WaitForExit(5000)
        $process.Refresh()
        $rootAlive = -not $process.HasExited
        $remainingOwnedProcessIds = @(
            $ownedProcessIds | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
        if ($taskkillExitCode -ne 0 -or -not $rootStopped -or $rootAlive -or $remainingOwnedProcessIds.Count -ne 0)
        {
            throw "owned process cleanup failed pid=$rootProcessId taskkill_exit=$taskkillExitCode root_alive=$([int]$rootAlive) remaining_owned=$($remainingOwnedProcessIds -join ',') output=$($taskkillOutput -join ' ') evidence_path=$EvidencePath"
        }
        throw "owned process timeout pid=$rootProcessId args=$($Arguments -join ' ') root_alive=0 remaining_owned=0 evidence_path=$EvidencePath"
    }
    $process.Refresh()
    if (-not $process.HasExited)
    {
        throw "owned process root remained alive after bounded wait pid=$rootProcessId evidence_path=$EvidencePath"
    }
    $remainingOwnedProcessIds = @(
        Get-M9OwnedDescendantProcessIds $rootProcessId |
            Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
    if ($remainingOwnedProcessIds.Count -ne 0)
    {
        throw "owned child process remained after root exit pid=$rootProcessId remaining_owned=$($remainingOwnedProcessIds -join ',') evidence_path=$EvidencePath"
    }
    return [pscustomobject]@{
        RootProcessId = $rootProcessId
        ExitCode = [int]$process.ExitCode
        RootAlive = $false
        RemainingOwnedProcessIds = $remainingOwnedProcessIds
    }
}

function Invoke-M9Tool([string]$Exe, [string[]]$Arguments, [string]$RunDirectory)
{
    $script:AssetCookInvocations++
    $toolDirectory = Join-Path $RunDirectory "assetcook-$($script:AssetCookInvocations)"
    New-Item -ItemType Directory -Path $toolDirectory | Out-Null
    $stdout = Join-Path $toolDirectory "stdout.txt"
    $stderr = Join-Path $toolDirectory "stderr.txt"
    $result = Invoke-M9OwnedProcess $Exe $Arguments $toolDirectory $stdout $stderr $TimeoutSeconds $toolDirectory
    if ($result.ExitCode -ne 0)
    {
        $text = ""
        foreach ($path in @($stdout, $stderr))
        {
            if (Test-Path -LiteralPath $path) { $text += [IO.File]::ReadAllText($path) + "`n" }
        }
        throw "AssetCook failed exit=$($result.ExitCode) pid=$($result.RootProcessId) evidence_path=$toolDirectory transcript=$text"
    }
}

function Invoke-M9Game([string]$GamePath, [string[]]$Arguments, [string]$RunDirectory)
{
    $stdout = Join-Path $RunDirectory "stdout.txt"
    $stderr = Join-Path $RunDirectory "stderr.txt"
    $result = Invoke-M9OwnedProcess $GamePath $Arguments $RunDirectory $stdout $stderr $TimeoutSeconds $RunDirectory
    $log = Join-Path $RunDirectory "Game.log"
    $text = ""
    foreach ($path in @($stdout, $stderr, $log)) { if (Test-Path -LiteralPath $path) { $text += [IO.File]::ReadAllText($path) + "`n" } }
    return [pscustomobject]@{
        RootProcessId = $result.RootProcessId
        ExitCode = $result.ExitCode
        RootAlive = $result.RootAlive
        RemainingOwnedProcessIds = $result.RemainingOwnedProcessIds
        Transcript = $text
    }
}

function Read-M9PixelImage([string]$Path)
{
    if ([IO.Path]::GetExtension($Path) -ieq '.png')
    {
        Add-Type -AssemblyName System.Drawing
        $bitmap = [Drawing.Bitmap]::new($Path)
        try
        {
            $rectangle = [Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
            $locked = $bitmap.LockBits($rectangle, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try
            {
                $bytes = [byte[]]::new($bitmap.Width * $bitmap.Height * 4)
                for ($y = 0; $y -lt $bitmap.Height; ++$y)
                {
                    [Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($locked.Scan0, $y * $locked.Stride), $bytes, $y * $bitmap.Width * 4, $bitmap.Width * 4)
                }
                return [pscustomobject]@{ Width = $bitmap.Width; Height = $bitmap.Height; BytesPerPixel = 4; BottomUp = $false; Bytes = $bytes }
            }
            finally { $bitmap.UnlockBits($locked) }
        }
        finally { $bitmap.Dispose() }
    }

    # 書き出し側の PFM は 3 行のヘッダとリトルエンディアンの float。数値へ丸めず全ビットを比較する。
    $bytes = [IO.File]::ReadAllBytes($Path)
    $offset = 0
    $lines = @()
    for ($line = 0; $line -lt 3; ++$line)
    {
        $start = $offset
        while ($offset -lt $bytes.Length -and $bytes[$offset] -ne 10) { ++$offset }
        Assert-M9 ($offset -lt $bytes.Length) "PFM のヘッダが途中で終わっています: $Path"
        $lines += [Text.Encoding]::ASCII.GetString($bytes, $start, $offset - $start).TrimEnd("`r")
        ++$offset
    }
    Assert-M9 ($lines[0] -ceq 'PF' -or $lines[0] -ceq 'Pf') "PFM の形式が不正です: $Path"
    $dimensions = [regex]::Match($lines[1], '^([1-9]\d*) ([1-9]\d*)$')
    Assert-M9 ($dimensions.Success -and $lines[2] -ceq '-1.0') "PFM の寸法またはスケールが不正です: $Path"
    $width = [int]$dimensions.Groups[1].Value
    $height = [int]$dimensions.Groups[2].Value
    $pixelBytes = if ($lines[0] -ceq 'PF') { 12 } else { 4 }
    $length = [long]$width * $height * $pixelBytes
    Assert-M9 ($length -eq $bytes.LongLength - $offset) "PFM の画素列の長さが不正です: $Path"
    $pixels = [byte[]]::new($length)
    [Buffer]::BlockCopy($bytes, $offset, $pixels, 0, $pixels.Length)
    return [pscustomobject]@{ Width = $width; Height = $height; BytesPerPixel = $pixelBytes; BottomUp = $true; Bytes = $pixels }
}

function Compare-M9PixelImage([string]$ReferencePath, [string]$ActualPath)
{
    if ($null -eq ('M9PixelComparison' -as [type]))
    {
        # 全画素をバイト単位で調べ、複数のチャンネルが違っても 1 画素として数える。
        Add-Type -TypeDefinition @"
public static class M9PixelComparison
{
    public static long[] Compare(byte[] reference, byte[] actual, int bytesPerPixel)
    {
        long count = 0;
        long first = -1;
        for (int offset = 0; offset < reference.Length; offset += bytesPerPixel)
        {
            for (int channel = 0; channel < bytesPerPixel; ++channel)
            {
                if (reference[offset + channel] != actual[offset + channel])
                {
                    if (first < 0) { first = offset / bytesPerPixel; }
                    ++count;
                    break;
                }
            }
        }
        return new long[] { count, first };
    }
}
"@
    }
    $reference = Read-M9PixelImage $ReferencePath
    $actual = Read-M9PixelImage $ActualPath
    Assert-M9 ($reference.Width -eq $actual.Width -and $reference.Height -eq $actual.Height -and
        $reference.BytesPerPixel -eq $actual.BytesPerPixel -and $reference.BottomUp -eq $actual.BottomUp) "比較する画像の寸法・形式が一致しません: $ReferencePath / $ActualPath"
    $difference = [M9PixelComparison]::Compare($reference.Bytes, $actual.Bytes, $reference.BytesPerPixel)
    $first = $null
    if ($difference[1] -ge 0)
    {
        $y = [int][Math]::Floor($difference[1] / $reference.Width)
        if ($reference.BottomUp) { $y = $reference.Height - 1 - $y }
        $first = [pscustomobject]@{ x = $difference[1] % $reference.Width; y = $y }
    }
    return [pscustomobject]@{
        matched = $difference[0] -eq 0
        width = $reference.Width
        height = $reference.Height
        mismatched_pixels = $difference[0]
        first_mismatch = $first
    }
}

function ConvertTo-M9QuotedArguments([string[]]$Arguments)
{
    foreach ($argument in $Arguments)
    {
        Assert-M9 (-not $argument.Contains('"')) "引数に引用符を含められません: $argument"
        '"' + $argument + '"'
    }
}

function Invoke-M9LegacyNvskelCompare([string]$GamePath, [string]$AssetCookPath, [string]$TrackedModel, [string]$RunRoot, [string]$EvidencePath)
{
    $evidence = [IO.Path]::GetFullPath($EvidencePath)
    Assert-M9 ($Configuration -eq 'Debug') '旧 NVSKEL の比較には開発ビルドの Debug を指定してください'
    New-Item -ItemType Directory -Path $evidence -Force | Out-Null
    Assert-M9 (@(Get-ChildItem -LiteralPath $evidence -Force).Count -eq 0) "証跡の保存先には空のディレクトリを指定してください: $evidence"
    $faces = @('albedo', 'normal', 'roughness', 'depth', 'sun_visibility', 'rtgi_temporal_code', 'rtgi_filtered_sky', 'geometric_normal', 'terrain_id')
    $results = [Collections.Generic.List[object]]::new()
    $mismatchedPixels = [long]0
    $passed = $false
    $errorText = $null
    try
    {
        Assert-M9 (Test-Path -LiteralPath $GamePath) "Game が見つかりません: $GamePath"
        Assert-M9 (Test-Path -LiteralPath $AssetCookPath) "AssetCook が見つかりません: $AssetCookPath"
        $runtimeRoot = Join-Path $RunRoot 'RuntimeRoot'
        $sourceRoot = Join-Path $RunRoot 'Source'
        New-Item -ItemType Directory -Path (Join-Path $runtimeRoot 'Cooked'), $sourceRoot | Out-Null
        $gltf = Join-Path $sourceRoot 'ValidU8Float.gltf'
        Copy-Item -LiteralPath $TrackedModel -Destination $gltf
        Write-M9FixtureBin (Join-Path $sourceRoot 'fixture.bin')
        $manifest = Join-Path $runtimeRoot 'manifest.json'
        $legacyManifest = Join-Path $runtimeRoot 'legacy-manifest.json'
        $package = Join-Path $runtimeRoot 'Cooked\m9.nvpkg'
        $legacyPackage = Join-Path $runtimeRoot 'Cooked\m9-v01.nvpkg'
        Set-Content -LiteralPath $manifest -Value '{"version":1,"assets":[]}' -Encoding utf8
        $cookArguments = ConvertTo-M9QuotedArguments @('--input', $gltf, '--out', $package, '--manifest', $manifest, '--logical', 'Models/M9Skinned/ValidU8Float.gltf', '--kind', 'model', '--entry', 'm9.nvskel', '--entry-type', 'Skl0', '--format', 'nvskel.v0.skinned.pnujiw.u32', '--variant', 'default')
        Invoke-M9Tool $AssetCookPath $cookArguments $RunRoot
        Assert-M9 ($script:AssetCookInvocations -eq 1) "比較経路の AssetCook 呼び出し数が不正です: $script:AssetCookInvocations"

        $oracle = Join-Path $repoRoot 'Test\Tools\NvskelLegacyV01Oracle.py'
        $python = (Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        $oracleArguments = ConvertTo-M9QuotedArguments @($oracle, '--package', $package, '--manifest', $manifest, '--out-package', $legacyPackage, '--out-manifest', $legacyManifest)
        $oracleResult = Invoke-M9OwnedProcess $python $oracleArguments $RunRoot (Join-Path $evidence 'oracle.stdout.txt') (Join-Path $evidence 'oracle.stderr.txt') $TimeoutSeconds $evidence
        Assert-M9 ($oracleResult.ExitCode -eq 0) "旧 NVSKEL の生成が失敗しました。証跡: $evidence"

        foreach ($variant in @('current', 'legacy'))
        {
            $directory = Join-Path $evidence $variant
            New-Item -ItemType Directory -Path $directory | Out-Null
            $selectedManifest = if ($variant -eq 'current') { $manifest } else { $legacyManifest }
            $png = Join-Path $directory 'capture.png'
            $gameArguments = ConvertTo-M9QuotedArguments @('--scene=rendering3dtest', '--window-background', '--m9-world-compare', '--texture-asset-root', $runtimeRoot, '--texture-asset-manifest', $selectedManifest, '--render-thread=st', '--visibility-buffer=off', '--capture-deterministic', '--capture-aov', "--capture-png=$png")
            $game = Invoke-M9Game $GamePath $gameArguments $directory
            Assert-M9 ($game.ExitCode -eq 0) "比較の Game が失敗しました: variant=$variant exit=$($game.ExitCode) 証跡: $directory"
            Assert-M9 (-not $game.RootAlive -and $game.RemainingOwnedProcessIds.Count -eq 0) "比較の Game の終了後にプロセスが残っています: $directory"
            Assert-M9 ([regex]::Matches($game.Transcript, 'M9_WORLD_COMPARE stage=captured pose_time=0 faces=9(?=\s|$)').Count -eq 1) "比較の撮影完了の印がありません: $directory"
            Assert-M9 (-not ($game.Transcript -match 'M9_WORLD_SMOKE stage=failure')) "M9 の組み立てが失敗しました: $directory"
            Assert-M9 (Test-Path -LiteralPath $png) "比較の PNG がありません: $png"
            Assert-M9 (Test-Path -LiteralPath (Join-Path $directory 'capture.aov.json')) "比較の AOV の情報がありません: $directory"
            Assert-M9 (@(Get-ChildItem -LiteralPath $directory -Filter 'capture.aov.*.pfm').Count -eq $faces.Count) "AOV の面数が不正です: $directory"
        }
        foreach ($face in $faces)
        {
            $reference = Join-Path $evidence "current\capture.aov.$face.pfm"
            $actual = Join-Path $evidence "legacy\capture.aov.$face.pfm"
            $comparison = Compare-M9PixelImage $reference $actual
            $results.Add([pscustomobject]@{ face = $face; current = $reference; legacy = $actual; comparison = $comparison })
            $mismatchedPixels += $comparison.mismatched_pixels
            if (-not $comparison.matched)
            {
                Write-Output "M9_LEGACY_NVSKEL_FACE face=$face result=fail mismatched_pixels=$($comparison.mismatched_pixels) first_x=$($comparison.first_mismatch.x) first_y=$($comparison.first_mismatch.y)"
            }
        }
        Assert-M9 ((Get-M9Sha256 $TrackedModel) -eq $trackedHashBefore) '追跡中のモデルのハッシュが変わっています'
        $passed = $mismatchedPixels -eq 0
    }
    catch
    {
        $errorText = $_.Exception.Message
    }
    finally
    {
        $result = if ($passed) { 'pass' } else { 'fail' }
        [pscustomobject]@{
            result = $result
            faces = $results.Count
            expected_faces = $faces.Count
            mismatched_pixels = $mismatchedPixels
            pose_time = 0
            capture_deterministic = $true
            visibility_buffer = 'off'
            render_thread = 'st'
            assetcook_calls = $script:AssetCookInvocations
            evidence = $evidence
            comparisons = @($results.ToArray())
            error = $errorText
        } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding utf8
        Write-Output "M9_LEGACY_NVSKEL_COMPARE result=$result faces=$($results.Count) mismatched_pixels=$mismatchedPixels evidence=$evidence"
        if ($passed -and (Test-Path -LiteralPath $RunRoot)) { Remove-Item -LiteralPath $RunRoot -Recurse -Force }
    }
    Assert-M9 $passed "旧 NVSKEL と一致しません。証跡: $evidence $errorText"
}


$repoRoot = Split-Path -Parent $PSScriptRoot
if ($LegacyNvskelCompare)
{
    Assert-M9 (-not [string]::IsNullOrWhiteSpace($EvidenceDir)) '-LegacyNvskelCompare には -EvidenceDir が必要です'
}
if ($ReviewContract -eq "All" -or $ReviewContract -eq "Cleanup") { Test-M9CleanupSourceContract $repoRoot }
if ($ReviewContract -eq "All" -or $ReviewContract -eq "Process") { Test-M9OwnedProcessSourceContract $PSCommandPath }
if ($ReviewContract -eq "All" -or $ReviewContract -eq "Configuration") { Test-M9ConfigurationSourceContract $repoRoot }
if ($ContractOnly)
{
    Write-Output "M9_REVIEW_CONTRACT result=pass contract=$ReviewContract"
    return
}
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
if ($LegacyNvskelCompare)
{
    # 比較の一時資産は作業ツリー内へ閉じ込め、画像と判定は明示した保存先に残す。
    $runRoot = Join-Path $repoRoot ".harness\tmp\M9LegacyNvskel-$([guid]::NewGuid().ToString('N'))"
    Invoke-M9LegacyNvskelCompare $gamePath $assetCookPath $trackedModel $runRoot $EvidenceDir
    return
}

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

    Invoke-M9Tool $assetCookPath @("--input", $gltf, "--out", (Join-Path $runtimeRoot "Cooked\m9.nvpkg"), "--manifest", $manifest, "--logical", "Models/M9Skinned/ValidU8Float.gltf", "--kind", "model", "--entry", "m9.nvskel", "--entry-type", "Skl0", "--format", "nvskel.v0.skinned.pnujiw.u32", "--variant", "default") $runRoot
    Invoke-M9Tool $assetCookPath @("--input", $effect, "--out", (Join-Path $runtimeRoot "Cooked\effect.nvpkg"), "--manifest", $manifest, "--logical", "Audio/M9/effect.wav", "--kind", "audio", "--entry", "effect.nvaud", "--entry-type", "Aud0", "--format", "nvaud.v0.pcm16", "--variant", "default") $runRoot
    Invoke-M9Tool $assetCookPath @("--input", $loop, "--out", (Join-Path $runtimeRoot "Cooked\loop.nvpkg"), "--manifest", $manifest, "--logical", "Audio/M9/loop.wav", "--kind", "audio", "--entry", "loop.nvaud", "--entry-type", "Aud0", "--format", "nvaud.v0.pcm16", "--variant", "default") $runRoot
    Assert-M9 ($script:AssetCookInvocations -eq 3) "AssetCook invocation count=$script:AssetCookInvocations expected=3"

    $normalDir = Join-Path $runRoot "normal"; New-Item -ItemType Directory -Path $normalDir | Out-Null
    $normal = Invoke-M9Game $gamePath @("--scene=rendering3dtest", "--window-background", "--render-thread=st", "--exit-after-rendered-frames=4") $normalDir
    Assert-M9 ($normal.ExitCode -eq 0) "normal Game exit=$($normal.ExitCode)"
    Assert-M9 (-not $normal.RootAlive -and $normal.RemainingOwnedProcessIds.Count -eq 0) "normal Game owned process cleanup incomplete"
    Assert-M9 (-not ($normal.Transcript -match "M9_WORLD_SMOKE")) "normal Game emitted M9 marker"

    $smokeDir = Join-Path $runRoot "smoke"; New-Item -ItemType Directory -Path $smokeDir | Out-Null
    # スキンの GBuffer の描画数（gbuffer=）は GBuffer のラスタの経路で数える。既定のビジビリティバッファでは
    # スキンを VisibilityRaster が描き、この数が 0 のままになるので、GBuffer のラスタの経路で起動する。
    $smoke = Invoke-M9Game $gamePath @("--scene=rendering3dtest", "--window-background", "--m9-world-smoke", "--texture-asset-root", $runtimeRoot, "--texture-asset-manifest", $manifest, "--render-thread=st", "--visibility-buffer=off") $smokeDir
    Assert-M9 ($smoke.ExitCode -eq 0) "M9 Game exit=$($smoke.ExitCode) transcript=$($smoke.Transcript)"
    Assert-M9 (-not $smoke.RootAlive -and $smoke.RemainingOwnedProcessIds.Count -eq 0) "M9 Game owned process cleanup incomplete"
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
    foreach ($line in ($smoke.Transcript -split "\r?\n"))
    {
        if ($line -match "M9_WORLD_SMOKE stage=") { Write-Output $line.Trim() }
    }
    $success = $true
    Write-Output "M9_WORLD_ACCEPTANCE result=pass configuration=$Configuration assetcook_calls=3 tracked_asset_hash_unchanged=1 normal_baseline=1 owned_process_cleanup=1"
}
finally
{
    if ($success -and (Test-Path -LiteralPath $runRoot)) { Remove-Item -LiteralPath $runRoot -Recurse -Force }
}
