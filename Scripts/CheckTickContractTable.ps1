param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$TablePath
)

$ErrorActionPreference = 'Stop'
$missing = @()
$forbidden = @()

try {
    $RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    if ([string]::IsNullOrEmpty($TablePath)) {
        $TablePath = Join-Path $RepositoryRoot 'Docs/Architecture/TickStages.md'
    }

    # 修飾された型名は末尾の名前を使い、自身の登録はファイル名から求める。
    $registeredTypes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $qualifiedPattern = 'RegisterComponentTick\(\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)::StaticClass\(\)'
    $selfPattern = 'RegisterComponentTick\(\s*StaticClass\(\)'
    $thirdPartyRoot = (Join-Path $RepositoryRoot 'Library/ThirdParty') + [IO.Path]::DirectorySeparatorChar
    foreach ($directory in @('Library', 'Game')) {
        $sourceRoot = Join-Path $RepositoryRoot $directory
        foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Filter '*.cpp' -Recurse -File) {
            if ($file.FullName.StartsWith($thirdPartyRoot, [StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            $source = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
            foreach ($match in [regex]::Matches($source, $qualifiedPattern)) {
                [void]$registeredTypes.Add($match.Groups[1].Value)
            }
            if ([regex]::IsMatch($source, $selfPattern)) {
                [void]$registeredTypes.Add($file.BaseName)
            }
        }
    }
    if ($registeredTypes.Count -eq 0) {
        throw '毎フレームの処理を登録した型が見つかりません。収集対象と正規表現を確認してください。'
    }

    if (-not (Test-Path -LiteralPath $TablePath -PathType Leaf)) {
        throw "割り当て表のファイルが見つかりません: $TablePath"
    }
    $tableTypes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $tableLines = @(Get-Content -LiteralPath $TablePath -Encoding UTF8)
    for ($index = 0; $index -lt $tableLines.Count; $index++) {
        $line = $tableLines[$index]
        if ($line -match '^\|\s*`([A-Za-z_]\w*)`') {
            [void]$tableTypes.Add($Matches[1])
        }
        # 消えた API の記述は表の外も含め、語と行番号を報告する。
        foreach ($term in @('OnTickGroup', 'Entity::Tick')) {
            if ($line.Contains($term)) {
                $forbidden += '{0}:{1}' -f $term, ($index + 1)
            }
        }
    }
    $missing = @($registeredTypes | Where-Object { -not $tableTypes.Contains($_) } | Sort-Object)
    if ($missing.Count -gt 0 -or $forbidden.Count -gt 0) {
        Write-Output ('TICK_CONTRACT_TABLE result=fail missing={0} forbidden={1}' -f ($missing -join ','), ($forbidden -join ','))
        exit 1
    }

    Write-Output ('TICK_CONTRACT_TABLE result=pass types={0}' -f $registeredTypes.Count)
    exit 0
}
catch {
    Write-Output ('TICK_CONTRACT_TABLE result=fail missing={0} forbidden={1}' -f ($missing -join ','), ($forbidden -join ','))
    Write-Error ('割り当て表の検査に失敗しました: {0}' -f $_.Exception.Message) -ErrorAction Continue
    exit 1
}
