# ShishaShowcase の印を照合し、必要なら PNG と GPU 時間を記録する。
# 例: powershell -File Scripts/Test-ShishaShowcase.ps1 -Scene base -CapturePng .harness/runs/base.png
# 例: powershell -File Scripts/Test-ShishaShowcase.ps1 -Scene base -CapturePng .harness/runs/base.png -CaptureFrames 30,60 -GameArgs '--fixed-frame-dt=1/60 --shisha-scale=1'
# -Scene は英小文字・数字・ハイフン。-Configuration、-BuildDirectory、-TimeoutSeconds で実行環境を指定する。
# -GameArgs は空白で分けた追加引数、-ExpectStages は順番に現れるべき段をカンマで区切る。
# 印の形式: SHISHA_SHOWCASE scene=<場面> stage=<段> key=value ...（値に空白は含めない）。
# -CapturePng だけなら SceneReady 後の 30 描画フレーム目を撮る。-CaptureFrames は 1 以上のフレームをカンマで区切る。
# 1 枚なら指定パス、複数なら拡張子を除いたパスに _<フレーム>.png を付ける。相対パスはリポジトリ基準。
# -ExitAfterFrames は撮影しないときの終了フレーム数、-OutDir はログ・トレース・metrics.json の保存先。
# -GpuTimingFrames は 0 で無効、100 以上で計測する。Release は GPU 計測できない。
# GPU 計測は実時間で行い、SceneReady 後のウォームアップ 60 件と末尾 2 件を除く。終了までの余白は 240 フレーム。
# 撮影と GPU 計測を併用すると、撮影は連続撮影の仕組みで指定フレームに行い、計測の窓が撮影後になるまで描画を続ける。
# 連続撮影は基準点と実際に要求したフレームを Game.log で照合する。証拠が無い場合や指定より遅れた場合は失敗する。
# Release は通常この照合用ログを出さないため、複数フレームの撮影を成功と確認できない。
[CmdletBinding()]
param(
    [string]$Scene = 'base',
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = 'build',
    [int]$TimeoutSeconds = 180,
    [string]$GameArgs = '',
    [string]$ExpectStages = 'enter,ready',
    [string]$CapturePng = '',
    [string]$CaptureFrames = '',
    [int]$ExitAfterFrames = 60,
    [string]$OutDir = '',
    [int]$GpuTimingFrames = 0
)

$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Resolve-RepoPath([string]$Path, [string]$RepoRoot)
{
    if ([IO.Path]::IsPathRooted($Path))
    {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

# Start-Process は引数配列を連結するため、空白と引用符を含む各引数を Windows の規則で囲む。
function ConvertTo-ProcessArgument([string]$Argument)
{
    $escaped = [regex]::Replace($Argument, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Get-TextIfPresent([string]$Path)
{
    if (Test-Path -LiteralPath $Path -PathType Leaf)
    {
        return [IO.File]::ReadAllText($Path)
    }
    return ''
}

function ConvertTo-MarkerValue([string]$Value)
{
    if ($Value -cmatch '^[+-]?\d+$')
    {
        $integer = [long]0
        if ([long]::TryParse($Value, [Globalization.NumberStyles]::Integer, $invariant, [ref]$integer))
        {
            return $integer
        }
    }
    if ($Value -cmatch '^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$')
    {
        $number = [double]0
        if ([double]::TryParse($Value, [Globalization.NumberStyles]::Float, $invariant, [ref]$number) -and
            -not [double]::IsInfinity($number) -and -not [double]::IsNaN($number))
        {
            return $number
        }
    }
    return $Value
}

function Get-ShowcaseMarkers([string]$Text)
{
    $result = New-Object 'System.Collections.Generic.List[object]'
    foreach ($line in ($Text -split '\r?\n'))
    {
        $match = [regex]::Match($line, '(?<![A-Za-z0-9_])SHISHA_SHOWCASE\s+scene=(\S+)\s+stage=(\S+)(.*)$')
        if (-not $match.Success)
        {
            continue
        }
        $values = [ordered]@{ scene = $match.Groups[1].Value }
        $stage = $match.Groups[2].Value
        foreach ($field in ($match.Groups[3].Value -split '\s+'))
        {
            if ($field -cmatch '^([A-Za-z_][A-Za-z0-9_.-]*)=(\S+)$')
            {
                $values[$Matches[1]] = ConvertTo-MarkerValue $Matches[2]
            }
        }
        $signature = $match.Value.Trim()
        $result.Add([pscustomobject]@{ stage = $stage; values = $values; signature = $signature })
    }
    return $result.ToArray()
}

# 同じ印が複数の出力先にあるときは、重複を除いた上で順番を推測せず検証する。
function Test-MarkerSubsequence([object[]]$Needle, [object[]]$Haystack)
{
    $index = 0
    foreach ($marker in $Haystack)
    {
        if ($index -lt $Needle.Count -and $marker.signature -ceq $Needle[$index].signature)
        {
            $index++
        }
    }
    return $index -eq $Needle.Count
}

# 昇順に並べた標本の百分位（0〜1）。標本の添字は floor((n-1)*p)（GPU のフレーム時間の既存の求め方と同じ）。
function Get-Percentile([double[]]$Sorted, [double]$Fraction)
{
    if ($Sorted.Count -eq 0) { return $null }
    return $Sorted[[int][math]::Floor(($Sorted.Count - 1) * $Fraction)]
}

# トレース（--trace-file の CSV）から GPU・CPU のフレーム時間を集計する。Type=GPU の行は描画したフレームごとの FrameGPU と
# パスごとの区間（Frame 列は区間を記録したフレームの番号）。窓は、SceneReady のフレーム（ReadyFrame）以降の描画した
# 有効なフレームから、先頭の WarmupFrames 個（ウォームアップ）を件数で除き、さらに撮影のフレームの直前 TailFrames 個を
# 除いた残りの最後の Samples 件（ウォームアップと末尾の除外は別々に取る）。
# CPU 側（ゲームスレッド・スコープ）は、描画したフレームの行（RenderFrameMs > 0）で同じ手順の窓を取る。
# 描画スレッドの時間（RenderThreadMs）は RenderFrameMs と同じ行に載るとは限らない（ゲームスレッドが描画 1 フレームの間に
# 何百回も回ると別の行に出る）ので、その窓の最初〜最後のフレーム番号の範囲の Frame 行のうち RenderThreadMs > 0 の行から集める。
# 有効な標本（FrameGPU > 0 のフレーム）が除外の後に Samples 件に満たなければ失敗（Error）にする。
function Get-GpuTimingFromTrace([string]$TracePath, [int]$Samples, [double]$BudgetMs, [long]$ReadyFrame, [int]$WarmupFrames, [int]$TailFrames)
{
    $header = @((Get-Content -LiteralPath $TracePath -TotalCount 1) -split ',')
    $nameColumn = [array]::IndexOf($header, 'Name')
    $durationColumn = [array]::IndexOf($header, 'DurationMs')
    $renderFrameColumn = [array]::IndexOf($header, 'RenderFrameMs')
    $cpuFrameColumn = [array]::IndexOf($header, 'CPUFrameMs')
    $gameThreadColumn = [array]::IndexOf($header, 'GameThreadMs')
    $renderThreadColumn = [array]::IndexOf($header, 'RenderThreadMs')
    $threadColumn = [array]::IndexOf($header, 'ThreadId')
    $frameRowThreads = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    $gpuRowThreads = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    $cpuRows = New-Object System.Collections.Generic.List[object]
    $renderThreadRows = New-Object System.Collections.Generic.List[object]
    $gpuScopesByFrame = New-Object 'System.Collections.Generic.SortedDictionary[long,object]'
    foreach ($line in [IO.File]::ReadLines($TracePath))
    {
        if ($line.StartsWith('Frame,'))
        {
            $fields = $line -split ','
            $frameRowThreads[$fields[$threadColumn]] = $frameRowThreads[$fields[$threadColumn]] + 1
            $renderThreadMs = [double]::Parse($fields[$renderThreadColumn], $invariant)
            if ($renderThreadMs -gt 0.0) { $renderThreadRows.Add([pscustomobject]@{ Frame = [long]$fields[1]; RenderThread = $renderThreadMs }) }
            if ([double]::Parse($fields[$renderFrameColumn], $invariant) -le 0.0) { continue }
            $cpuRows.Add([pscustomobject]@{
                Frame = [long]$fields[1]
                Cpu = [double]::Parse($fields[$cpuFrameColumn], $invariant)
                GameThread = [double]::Parse($fields[$gameThreadColumn], $invariant)
            })
        }
        elseif ($line.StartsWith('GPU,'))
        {
            $fields = $line -split ','
            $gpuRowThreads[$fields[$threadColumn]] = $gpuRowThreads[$fields[$threadColumn]] + 1
            $frameNumber = [long]$fields[1]
            if (-not $gpuScopesByFrame.ContainsKey($frameNumber))
            {
                $gpuScopesByFrame[$frameNumber] = New-Object 'System.Collections.Generic.List[object]'
            }
            $gpuScopesByFrame[$frameNumber].Add([pscustomobject]@{
                Name = $fields[$nameColumn].Trim('"')
                Ms = [double]::Parse($fields[$durationColumn], $invariant)
            })
        }
    }
    $windowCount = $Samples
    $cpuReady = @($cpuRows.ToArray() | Where-Object { $_.Frame -ge $ReadyFrame })
    $cpuArray = if ($cpuReady.Count -gt $WarmupFrames) { @($cpuReady[$WarmupFrames..($cpuReady.Count - 1)]) } else { @() }
    $cpuUsable = if ($cpuArray.Count -gt $TailFrames) { @($cpuArray[0..($cpuArray.Count - $TailFrames - 1)]) } else { @() }
    $cpuWindow = if ($cpuUsable.Count -gt $windowCount) { @($cpuUsable[($cpuUsable.Count - $windowCount)..($cpuUsable.Count - 1)]) } else { $cpuUsable }
    $cpuSamples = [double[]]@($cpuWindow | ForEach-Object { $_.Cpu } | Where-Object { $_ -gt 0.0 } | Sort-Object)
    $gameThreadSamples = [double[]]@($cpuWindow | ForEach-Object { $_.GameThread } | Where-Object { $_ -gt 0.0 } | Sort-Object)
    $renderThreadFirst = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[0].Frame } else { [long]::MaxValue }
    $renderThreadLast = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[$cpuWindow.Count - 1].Frame } else { [long]::MinValue }
    $renderThreadSamples = [double[]]@($renderThreadRows.ToArray() | Where-Object { $_.Frame -ge $renderThreadFirst -and $_.Frame -le $renderThreadLast } | ForEach-Object { $_.RenderThread } | Sort-Object)
    $gpuReady = @($gpuScopesByFrame.Keys | Where-Object { $_ -ge $ReadyFrame -and @($gpuScopesByFrame[$_] | Where-Object { $_.Name -eq 'FrameGPU' -and $_.Ms -gt 0.0 }).Count -gt 0 })
    $gpuFrames = if ($gpuReady.Count -gt $WarmupFrames) { @($gpuReady[$WarmupFrames..($gpuReady.Count - 1)]) } else { @() }
    $gpuUsable = if ($gpuFrames.Count -gt $TailFrames) { @($gpuFrames[0..($gpuFrames.Count - $TailFrames - 1)]) } else { @() }
    $gpuWindow = if ($gpuUsable.Count -gt $windowCount) { @($gpuUsable[($gpuUsable.Count - $windowCount)..($gpuUsable.Count - 1)]) } else { $gpuUsable }
    $gpuFrameMs = @{}
    $passSamples = @{}
    foreach ($frameNumber in $gpuWindow)
    {
        foreach ($scope in $gpuScopesByFrame[$frameNumber])
        {
            if ($scope.Name -eq 'FrameGPU') { $gpuFrameMs[$frameNumber] = $scope.Ms; continue }
            if (-not $passSamples.ContainsKey($scope.Name)) { $passSamples[$scope.Name] = New-Object System.Collections.Generic.List[double] }
            $passSamples[$scope.Name].Add($scope.Ms)
        }
    }
    $gpuSamples = [double[]]@($gpuWindow | ForEach-Object { $gpuFrameMs[$_] } | Sort-Object)
    if ($gpuSamples.Count -lt $Samples)
    {
        return [pscustomobject]@{ Error = "GPU のフレーム時間の有効な標本が足りない（SceneReady のフレーム $ReadyFrame 以降の有効な描画フレーム $($gpuReady.Count) 件からウォームアップ $WarmupFrames 件と末尾 $TailFrames 件を除き、$($gpuSamples.Count) 件 / 指定 $Samples 件）。GPU タイムスタンプや余白を確認してください。"; Timing = $null }
    }
    $maximum = $gpuSamples[$gpuSamples.Count - 1]
    $passList = @($passSamples.Keys | ForEach-Object {
        $sortedPass = [double[]]@($passSamples[$_] | Sort-Object)
        [pscustomobject][ordered]@{
            pass = $_
            median_ms = [math]::Round((Get-Percentile $sortedPass 0.5), 3)
            p95_ms = [math]::Round((Get-Percentile $sortedPass 0.95), 3)
            max_ms = [math]::Round($sortedPass[$sortedPass.Count - 1], 3)
        }
    } | Sort-Object -Property median_ms -Descending)
    $passMedianList = @($passList | ForEach-Object { [pscustomobject][ordered]@{ pass = $_.pass; median_ms = $_.median_ms } })

    # CPU のスコープ（Type=Scope の行）: 窓の最初〜最後のフレーム番号の範囲に記録された全部の区間を、名前ごとに集計する。
    # Scope 行の Frame は記録時点のゲームフレーム番号で、描画を完了したフレームの行（窓）の番号とは一致しないため、
    # 窓の行の番号に所属するかではなく連続した範囲で選ぶ。
    # フレームあたりの値 = そのフレームに記録された同じ名前の区間の継続時間の合計（全スレッド・並列の仕事は足し合わせる）。
    # median・p95・max はその名前が出たフレーム（frames_present）だけで求める。ゲームスレッドが描画 1 フレームの間に何回も回ると
    # 描画スレッドの区間は範囲の一部のフレームにしか出ないので、出なかったフレームを 0 として数えると median が 0 になるため。
    # threads = その名前の区間が出たスレッド（ThreadId）の数、thread = その名前（GameThread・RenderThread・Worker を , で連ねる）、
    # frames_present = 出たフレームの数、samples = 区間の記録の件数。
    # スレッドの名前は、Frame の行を書くスレッドを GameThread、GPU の行を書くスレッドを RenderThread とし（それぞれ最も多い ThreadId）、
    # それ以外を Worker とする。
    # 名前の先頭 2 つの部分（A.B）が同じで 3 つ以上の部分を持つ名前は、A.B.* としてフレームごとに合計した値も書く。
    $scopeFrameFirst = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[0].Frame } else { [long]::MaxValue }
    $scopeFrameLast = if ($cpuWindow.Count -gt 0) { [long]$cpuWindow[$cpuWindow.Count - 1].Frame } else { [long]::MinValue }
    $scopeFrameCount = if ($cpuWindow.Count -gt 0) { [int]($scopeFrameLast - $scopeFrameFirst + 1) } else { 0 }
    $gameThreadId = if ($frameRowThreads.Count -gt 0) { @($frameRowThreads.Keys | Sort-Object { $frameRowThreads[$_] } -Descending)[0] } else { $null }
    $renderThreadId = if ($gpuRowThreads.Count -gt 0) { @($gpuRowThreads.Keys | Sort-Object { $gpuRowThreads[$_] } -Descending)[0] } else { $null }
    $scopePerFrame =New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.Dictionary[long,double]]'
    $scopeThreads = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.HashSet[string]]'
    $scopeRecords = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    $groupPerFrame = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.Dictionary[long,double]]'
    $groupThreads = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.HashSet[string]]'
    $groupRecords = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    foreach ($line in [IO.File]::ReadLines($TracePath))
    {
        if (-not $line.StartsWith('Scope,')) { continue }
        $frameEnd = $line.IndexOf(',', 6)
        $scopeFrame = [long]$line.Substring(6, $frameEnd - 6)
        if ($scopeFrame -lt $scopeFrameFirst -or $scopeFrame -gt $scopeFrameLast) { continue }
        $fields = $line.Split(',', 8)
        $scopeName = $fields[$nameColumn].Trim('"')
        $scopeMs = [double]::Parse($fields[$durationColumn], $invariant)
        $threadId = $fields[$threadColumn]
        $targets = @(, @($scopeName, $scopePerFrame, $scopeThreads, $scopeRecords))
        $nameParts = $scopeName.Split('.')
        if ($nameParts.Count -ge 3) { $targets += , @(($nameParts[0] + '.' + $nameParts[1] + '.*'), $groupPerFrame, $groupThreads, $groupRecords) }
        foreach ($target in $targets)
        {
            $key = $target[0]
            $perFrame = $target[1]
            if (-not $perFrame.ContainsKey($key))
            {
                $perFrame[$key] = New-Object 'System.Collections.Generic.Dictionary[long,double]'
                $target[2][$key] = New-Object 'System.Collections.Generic.HashSet[string]'
                $target[3][$key] = 0
            }
            $frameTotals = $perFrame[$key]
            $previous = 0.0
            $null = $frameTotals.TryGetValue($scopeFrame, [ref]$previous)
            $frameTotals[$scopeFrame] = $previous + $scopeMs
            $null = $target[2][$key].Add($threadId)
            $target[3][$key] = $target[3][$key] + 1
        }
    }
    function ConvertTo-ScopeStatList($PerFrame, $Threads, $Records, $GameThreadId, $RenderThreadId)
    {
        return @($PerFrame.Keys | ForEach-Object {
            $scopeName = $_
            $frameTotals = $PerFrame[$scopeName]
            $sortedScope = [double[]]@($frameTotals.Values | Sort-Object)
            $threadNames = @($Threads[$scopeName] | ForEach-Object {
                if ($_ -eq $GameThreadId) { 'GameThread' } elseif ($_ -eq $RenderThreadId) { 'RenderThread' } else { 'Worker' }
            } | Sort-Object { @('GameThread', 'RenderThread', 'Worker').IndexOf($_) } | Select-Object -Unique)
            [pscustomobject][ordered]@{
                scope = $scopeName
                median_ms = [math]::Round((Get-Percentile $sortedScope 0.5), 3)
                p95_ms = [math]::Round((Get-Percentile $sortedScope 0.95), 3)
                max_ms = [math]::Round($sortedScope[$sortedScope.Count - 1], 3)
                threads = $Threads[$scopeName].Count
                thread = $threadNames -join ','
                frames_present = $frameTotals.Count
                samples = $Records[$scopeName]
            }
        } | Sort-Object -Property @{ Expression = 'median_ms'; Descending = $true }, @{ Expression = 'max_ms'; Descending = $true }, @{ Expression = 'scope'; Descending = $false })
    }
    # 関数の戻りは 1 件だと配列でなくなり JSON がオブジェクトになるので、受け側で配列に包む（0 件・1 件でも配列を保つ）
    $cpuScopeList = @(ConvertTo-ScopeStatList $scopePerFrame $scopeThreads $scopeRecords $gameThreadId $renderThreadId)
    $cpuScopeGroupList = @(ConvertTo-ScopeStatList $groupPerFrame $groupThreads $groupRecords $gameThreadId $renderThreadId)

    $timing = [ordered]@{
        trace = [IO.Path]::GetFileName($TracePath)
        frames = $gpuSamples.Count
        valid_samples = $gpuSamples.Count
        requested_samples = $Samples
        scene_ready_frame = $ReadyFrame
        warmup_frames_removed = $WarmupFrames
        start_frame = [long]$gpuFrames[0]
        window_first_frame = [long]$gpuWindow[0]
        window_last_frame = [long]$gpuWindow[$gpuWindow.Count - 1]
        gpu_frame_ms_median = [math]::Round((Get-Percentile $gpuSamples 0.5), 3)
        gpu_frame_ms_mean = [math]::Round(($gpuSamples | Measure-Object -Average).Average, 3)
        gpu_frame_ms_p95 = [math]::Round((Get-Percentile $gpuSamples 0.95), 3)
        gpu_frame_ms_max = [math]::Round($maximum, 3)
        cpu_frame_ms_median = if ($cpuSamples.Count -gt 0) { [math]::Round((Get-Percentile $cpuSamples 0.5), 3) } else { $null }
        cpu_frame_ms_p95 = if ($cpuSamples.Count -gt 0) { [math]::Round((Get-Percentile $cpuSamples 0.95), 3) } else { $null }
        game_thread_ms_median = if ($gameThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $gameThreadSamples 0.5), 3) } else { $null }
        game_thread_ms_p95 = if ($gameThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $gameThreadSamples 0.95), 3) } else { $null }
        render_thread_ms_median = if ($renderThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $renderThreadSamples 0.5), 3) } else { $null }
        render_thread_ms_p95 = if ($renderThreadSamples.Count -gt 0) { [math]::Round((Get-Percentile $renderThreadSamples 0.95), 3) } else { $null }
        cpu_frames = $cpuSamples.Count
        budget_ms = $BudgetMs
        within_budget = ($maximum -le $BudgetMs)
        pass_median_ms = $passMedianList
        pass_ms = $passList
        gpu_pass_ms = $passList
        cpu_scope_frames = $scopeFrameCount
        cpu_scope_ms = $cpuScopeList
        cpu_scope_group_ms = $cpuScopeGroupList
    }
    return [pscustomobject]@{ Error = $null; Timing = [pscustomobject]$timing }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$outRoot = $null
$metricsPath = $null
$gamePath = $null
$processStartUtc = $null
$exitCode = $null
$actualArgs = @()
$markers = @()
$stages = [ordered]@{}
$captures = @()
$gpuTiming = $null
$failureCode = 'unexpected_error'
$failureDetails = New-Object 'System.Collections.Generic.List[object]'

try
{
    $failureCode = 'invalid_scene'
    if ($Scene -cnotmatch '\A[a-z0-9-]+\z') { throw '場面は英小文字・数字・ハイフンで指定してください。' }
    $failureCode = 'invalid_configuration'
    if ($Configuration -cnotin @('Debug', 'RelWithDebInfo', 'Release')) { throw '構成は Debug、RelWithDebInfo、Release のいずれかを指定してください。' }
    $failureCode = 'invalid_timeout'
    if ($TimeoutSeconds -lt 1 -or $TimeoutSeconds -gt 2147483) { throw '時間切れは 1〜2147483 秒で指定してください。' }
    $failureCode = 'invalid_exit_frames'
    if ($ExitAfterFrames -lt 1) { throw '終了フレーム数は 1 以上で指定してください。' }
    $failureCode = 'invalid_gpu_timing_frames'
    if ($GpuTimingFrames -lt 0 -or $GpuTimingFrames -gt 100000 -or ($GpuTimingFrames -gt 0 -and $GpuTimingFrames -lt 100))
    {
        throw 'GPU 計測の標本数は 0 または 100〜100000 で指定してください。'
    }
    $failureCode = 'gpu_timing_without_stats'
    if ($GpuTimingFrames -gt 0 -and $Configuration -ceq 'Release') { throw 'Release は GPU 統計を出せません。Debug または RelWithDebInfo を使ってください。' }

    $failureCode = 'invalid_expected_stages'
    $expected = @($ExpectStages -split ',' | ForEach-Object { $_.Trim() })
    if ($expected.Count -eq 0 -or @($expected | Where-Object { $_ -cnotmatch '\A[A-Za-z0-9_-]+\z' }).Count -gt 0)
    {
        throw '期待する段を空でない名前のカンマ区切りで指定してください。'
    }

    $failureCode = 'invalid_capture_frames'
    if ($CaptureFrames -ne '' -and $CapturePng -eq '') { throw '-CaptureFrames には -CapturePng も必要です。' }
    $frameNumbers = @()
    if ($CapturePng -ne '')
    {
        $parts = if ($CaptureFrames -eq '') { @('30') } else { @($CaptureFrames -split ',') }
        foreach ($part in $parts)
        {
            $frame = [uint32]0
            if ($part -cnotmatch '^\d+$' -or -not [uint32]::TryParse($part, [ref]$frame) -or $frame -eq 0)
            {
                throw '撮影フレームは 1 以上の整数をカンマで区切って指定してください。'
            }
            $frameNumbers += [long]$frame
        }
        $frameNumbers = @($frameNumbers | Sort-Object -Unique)
    }

    $failureCode = 'invalid_game_args'
    $extraArguments = @($GameArgs -split '\s+' | Where-Object { $_ -ne '' })
    foreach ($argument in $extraArguments)
    {
        if ($argument -cmatch '^--(?:shisha-showcase|capture-png|capture-sequence|capture-sequence-rendered-frames|exit-after-rendered-frames|exit-after-frames|trace-file)(?:=|$)' -or $argument -ceq '--wait-for-asset-settle')
        {
            throw "台本が管理する引数は -GameArgs に指定できません: $argument"
        }
        if ($GpuTimingFrames -gt 0 -and $argument -ceq '--capture-deterministic')
        {
            throw 'GPU の実時間計測には --capture-deterministic を併用できません。'
        }
    }

    $failureCode = 'output_setup_failed'
    if ($OutDir -eq '') { $OutDir = ".harness/runs/shisha-showcase/$Scene" }
    $outRoot = Resolve-RepoPath $OutDir $repoRoot
    [void][IO.Directory]::CreateDirectory($outRoot)
    $metricsPath = Join-Path $outRoot 'metrics.json'
    $stdoutPath = Join-Path $outRoot 'stdout.txt'
    $stderrPath = Join-Path $outRoot 'stderr.txt'
    $logPath = Join-Path $outRoot 'Game.log'
    $rootLogPath = Join-Path $repoRoot 'Game.log'
    $tracePath = Join-Path $outRoot 'trace.csv'
    $buildRoot = Resolve-RepoPath $BuildDirectory $repoRoot
    $gamePath = Join-Path $buildRoot "Game\$Configuration\Game.exe"
    $capturePath = if ($CapturePng -ne '') { Resolve-RepoPath $CapturePng $repoRoot } else { '' }
    if ($capturePath -ne '')
    {
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($capturePath))
    }
    $failureCode = 'game_not_found'
    if (-not (Test-Path -LiteralPath $gamePath -PathType Leaf)) { throw "Game が見つかりません: $gamePath" }

    $actualArgs = @("--shisha-showcase=$Scene", '--window-background') + $extraArguments
    $useSequence = ($frameNumbers.Count -gt 1 -or ($frameNumbers.Count -eq 1 -and $GpuTimingFrames -gt 0))
    $sequencePrefix = ''
    $sequenceTemporary = $false
    $targetExitFrames = [long]$ExitAfterFrames
    if ($useSequence)
    {
        if ($frameNumbers.Count -eq 1)
        {
            $sequencePrefix = Join-Path $outRoot 'timing-capture-'
            $sequenceTemporary = $true
        }
        else
        {
            $sequencePrefix = [IO.Path]::ChangeExtension($capturePath, $null) + '_'
        }
        $actualArgs += "--capture-sequence=$sequencePrefix"
        $actualArgs += "--capture-sequence-rendered-frames=$($frameNumbers -join ',')"
        # 取得は同時に 1 件だけ。最後の結果を受け取る時間も確保する。
        $targetExitFrames = $frameNumbers[-1] + 121 * $frameNumbers.Count + 1
        $actualArgs += '--wait-for-asset-settle'
    }
    elseif ($frameNumbers.Count -eq 1)
    {
        $actualArgs += "--capture-png=$capturePath"
        $targetExitFrames = $frameNumbers[0]
    }
    if ($GpuTimingFrames -gt 0)
    {
        $actualArgs += "--trace-file=$tracePath"
        $timingExitFrames = [long]$GpuTimingFrames + 60 + 2 + 240
        if ($useSequence)
        {
            # 撮影の取得と、その後の GPU 計測の窓が重ならないだけの時間も確保する。
            $timingExitFrames = [math]::Max($timingExitFrames, $targetExitFrames + $GpuTimingFrames + 2)
        }
        $targetExitFrames = [math]::Max($targetExitFrames, $timingExitFrames)
        if (-not $useSequence) { $actualArgs += '--wait-for-asset-settle' }
    }
    $actualArgs += "--exit-after-rendered-frames=$targetExitFrames"
    $failureCode = 'output_setup_failed'
    foreach ($reserved in @($metricsPath, $stdoutPath, $stderrPath, $logPath, $tracePath, $rootLogPath))
    {
        if ($capturePath -ne '' -and [string]::Equals($capturePath, $reserved, [StringComparison]::OrdinalIgnoreCase))
        {
            throw "撮影先がログや計測結果と重なっています: $capturePath"
        }
    }
    if ($GpuTimingFrames -gt 0 -and (Test-Path -LiteralPath $tracePath)) { Remove-Item -LiteralPath $tracePath -Force }
    if ((Test-Path -LiteralPath $logPath) -and -not [string]::Equals($logPath, $rootLogPath, [StringComparison]::OrdinalIgnoreCase))
    {
        Remove-Item -LiteralPath $logPath -Force
    }

    $failureCode = 'launch_failed'
    $commandLine = (@($actualArgs | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' ')
    $processStartUtc = [DateTime]::UtcNow
    # CaptureScene と同じく、相対パスで読むアセットのためリポジトリのルートから起動する。
    $process = Start-Process -FilePath $gamePath -ArgumentList $commandLine -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        $taskKillDetails = ''
        try
        {
            if (-not $process.HasExited)
            {
                $killOutput = & taskkill.exe /PID $process.Id /T /F 2>&1
                $taskKillDetails = "taskkill_exit=$LASTEXITCODE taskkill_output=$($killOutput -join ' ')"
            }
        }
        catch { $taskKillDetails = "taskkill_error=$($_.Exception.Message)" }
        $stopped = $process.WaitForExit(5000)
        $failureDetails.Add([pscustomobject]@{ code = 'timeout'; message = "時間切れ pid=$($process.Id) args=$($actualArgs -join ' ') evidence_path=$outRoot stdout_path=$stdoutPath stderr_path=$stderrPath log_path=$logPath stopped=$stopped $taskKillDetails" })
    }
    else
    {
        $process.WaitForExit()
        $exitCode = [int]$process.ExitCode
        if ($exitCode -ne 0) { $failureDetails.Add([pscustomobject]@{ code = 'game_exit_nonzero'; message = "Game の終了コードが $exitCode です。pid=$($process.Id)" }) }
    }

    $failureCode = 'evidence_read_failed'
    if ((Test-Path -LiteralPath $rootLogPath -PathType Leaf) -and (Get-Item -LiteralPath $rootLogPath).LastWriteTimeUtc -ge $processStartUtc)
    {
        if (-not [string]::Equals($rootLogPath, $logPath, [StringComparison]::OrdinalIgnoreCase))
        {
            Copy-Item -LiteralPath $rootLogPath -Destination $logPath -Force
        }
        $logText = Get-TextIfPresent $logPath
    }
    else { $logText = '' }
    $stdoutText = Get-TextIfPresent $stdoutPath
    $stderrText = Get-TextIfPresent $stderrPath
    $markerStreams = @(
        [pscustomobject]@{ items = @(Get-ShowcaseMarkers $logText) }
        [pscustomobject]@{ items = @(Get-ShowcaseMarkers $stdoutText) }
        [pscustomobject]@{ items = @(Get-ShowcaseMarkers $stderrText) }
    )
    foreach ($stream in $markerStreams)
    {
        $observed = @($stream.items)
        if (Test-MarkerSubsequence $markers $observed) { $markers = $observed }
        elseif (-not (Test-MarkerSubsequence $observed $markers))
        {
            $failureDetails.Add([pscustomobject]@{ code = 'marker_order_ambiguous'; message = 'ログと標準出力・標準エラーの印の順番を一意に統合できません。' })
            $markers += $observed
        }
    }
    $expectedIndex = 0
    foreach ($marker in $markers)
    {
        $stages[$marker.stage] = $marker.values
        if ($marker.stage -ceq 'failure') { $failureDetails.Add([pscustomobject]@{ code = 'showcase_failure'; message = "失敗の印: $($marker.signature)" }) }
        if ($marker.values.scene -ceq $Scene -and $expectedIndex -lt $expected.Count -and $marker.stage -ceq $expected[$expectedIndex]) { $expectedIndex++ }
    }
    if ($expectedIndex -lt $expected.Count)
    {
        $failureDetails.Add([pscustomobject]@{ code = 'expected_stage_missing'; message = "期待する段が順番に現れません: $($expected -join ',')" })
    }

    if ($useSequence)
    {
        # 既存の連続撮影は実時間では SceneReady を待たず、前の結果を待って目標を過ぎることがある。
        # ログで基準点と実際の要求フレームを確かめ、指定どおりと断定できないときは失敗にする。
        $sawReady = $false
        $baselineAfterReady = $false
        $verifiedFrames = @{}
        foreach ($line in ($logText -split '\r?\n'))
        {
            if ($line -cmatch ('SHISHA_SHOWCASE\s+scene=' + [regex]::Escape($Scene) + '\s+stage=ready(?:\s|$)')) { $sawReady = $true }
            if ($line -cmatch 'SEQUENCE_CAPTURE baseline rendered=\d+') { $baselineAfterReady = $sawReady }
            if ($line -cmatch 'SEQUENCE_CAPTURE requested target=(\d+) rendered=\d+ settled=(\d+)')
            {
                if ($baselineAfterReady -and [long]$Matches[1] -eq [long]$Matches[2]) { $verifiedFrames[[long]$Matches[1]] = $true }
            }
        }
        foreach ($frame in $frameNumbers)
        {
            if (-not $verifiedFrames.ContainsKey($frame))
            {
                $failureDetails.Add([pscustomobject]@{ code = 'capture_frame_unverified'; message = "SceneReady 後の指定フレームで撮影した根拠がありません: frame=$frame log=$logPath" })
            }
        }
    }

    foreach ($frame in $frameNumbers)
    {
        $generatedPath = if ($useSequence) { "$sequencePrefix$frame.png" } else { $capturePath }
        $present = (Test-Path -LiteralPath $generatedPath -PathType Leaf) -and (Get-Item -LiteralPath $generatedPath).LastWriteTimeUtc -ge $processStartUtc
        $resultPath = if ($sequenceTemporary) { $capturePath } else { $generatedPath }
        if ($present -and $sequenceTemporary -and -not [string]::Equals($generatedPath, $resultPath, [StringComparison]::OrdinalIgnoreCase))
        {
            Copy-Item -LiteralPath $generatedPath -Destination $resultPath -Force
        }
        $captures += [pscustomobject][ordered]@{ frame = $frame; path = $resultPath; exists = [bool]$present }
        if (-not $present) { $failureDetails.Add([pscustomobject]@{ code = 'capture_missing'; message = "今回の PNG がありません: $resultPath" }) }
    }

    if ($GpuTimingFrames -gt 0)
    {
        $failureCode = 'gpu_timing_failed'
        $readyScenes = @{}
        $readyFrame = $null
        foreach ($line in (($logText + "`n" + $stdoutText) -split '\r?\n'))
        {
            if ($line -cmatch '(?<![A-Za-z0-9_])SCENE_READY\s+scene=(\S+)(?:\s|$)') { $readyScenes[$Matches[1]] = $true }
            if ($line -cmatch '(?<![A-Za-z0-9_])SCENE_READY_FRAME\s+scene=(\S+)\s+frame=(\d+)(?:\s|$)' -and $readyScenes.ContainsKey($Matches[1])) { $readyFrame = [long]$Matches[2] }
        }
        if ($null -eq $readyFrame) { $failureDetails.Add([pscustomobject]@{ code = 'scene_ready_frame_missing'; message = 'SCENE_READY と SCENE_READY_FRAME が揃わず、計測の窓を決められません。' }) }
        elseif (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) { $failureDetails.Add([pscustomobject]@{ code = 'trace_missing'; message = "トレースがありません: $tracePath" }) }
        else
        {
            $timingResult = Get-GpuTimingFromTrace $tracePath $GpuTimingFrames 16.6 $readyFrame 60 2
            if ($null -ne $timingResult.Error) { $failureDetails.Add([pscustomobject]@{ code = 'gpu_timing_insufficient'; message = $timingResult.Error }) }
            else { $gpuTiming = $timingResult.Timing }
        }
    }
}
catch
{
    $failureDetails.Add([pscustomobject]@{ code = $failureCode; message = $_.Exception.Message })
}

$publicMarkers = @($markers | ForEach-Object { [pscustomobject][ordered]@{ stage = $_.stage; values = $_.values } })
$metrics = [ordered]@{
    scene = $Scene
    configuration = $Configuration
    exit_code = $exitCode
    game_args = @($actualArgs)
    markers = $publicMarkers
    stages = $stages
    captures = @($captures)
}
if ($GpuTimingFrames -gt 0) { $metrics['gpu_timing'] = $gpuTiming }
if ($null -ne $metricsPath)
{
    try
    {
        $json = $metrics | ConvertTo-Json -Depth 20
        [IO.File]::WriteAllText($metricsPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    }
    catch { $failureDetails.Add([pscustomobject]@{ code = 'metrics_write_failed'; message = "metrics.json を書けません: $($_.Exception.Message)" }) }
}
foreach ($failure in $failureDetails) { Write-Output "検証失敗: $($failure.message)" }
if ($failureDetails.Count -gt 0)
{
    Write-Output "SHISHA_SHOWCASE_TEST result=fail reason=$($failureDetails[0].code)"
    exit 1
}
Write-Output "SHISHA_SHOWCASE_TEST result=pass scene=$Scene metrics=$metricsPath"
exit 0
