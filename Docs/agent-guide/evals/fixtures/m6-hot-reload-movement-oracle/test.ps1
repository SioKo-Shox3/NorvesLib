param(
    [string]$ContractPath = (Join-Path $PSScriptRoot 'M6HotReloadMovementContract.ps1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ContractPath -PathType Leaf)) {
    throw "Missing contract: $ContractPath"
}

$contractText = [System.IO.File]::ReadAllText($ContractPath)
[void][scriptblock]::Create($contractText)
. ([scriptblock]::Create($contractText))

function Assert-Accepts {
    param(
        [string[]]$Lines,
        [string]$Name
    )

    try {
        Assert-M6HotReloadMovementContract -Lines $Lines
    }
    catch {
        throw "Expected acceptance: $Name. $($_.Exception.Message)"
    }
}

function Assert-Rejects {
    param(
        [string[]]$Lines,
        [string]$Name
    )

    try {
        Assert-M6HotReloadMovementContract -Lines $Lines
    }
    catch {
        return
    }
    throw "$Name was accepted"
}

function New-M6MovementMarkers {
    param(
        [string]$GoodX,
        [string]$BadX = $GoodX,
        [string]$CompleteX = $GoodX,
        [string]$BadV2InitialX = $GoodX,
        [string]$CompleteV2InitialX = $GoodX,
        [string]$GoodY = '0',
        [string]$BadY = '2',
        [string]$CompleteY = '4',
        [string]$GoodAnchor = '1',
        [string]$BadAnchor = '2',
        [string]$CompleteAnchor = '2'
    )

    return @(
        "M6_SCRIPT_SMOKE stage=ready_good position_x=$GoodX position_y=$GoodY anchor_z=$GoodAnchor",
        "M6_SCRIPT_SMOKE stage=ready_bad position_x=$BadX position_y=$BadY anchor_z=$BadAnchor v2_initial_x=$BadV2InitialX",
        "M6_SCRIPT_SMOKE stage=complete position_x=$CompleteX position_y=$CompleteY anchor_z=$CompleteAnchor v2_initial_x=$CompleteV2InitialX"
    )
}

foreach ($goodX in @('1.25', '7.5', '42.75')) {
    Assert-Accepts -Lines (New-M6MovementMarkers -GoodX $goodX) -Name "positive v1 good X $goodX"
}

Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -BadX '9.75' -CompleteX '9.75' -BadV2InitialX '9.75' -CompleteV2InitialX '9.75') -Name 'Coordinated X mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -BadX '9.75' -BadV2InitialX '9.75') -Name 'ready_bad-only coordinated X mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -BadX '9.75') -Name 'ready_bad self-inconsistent X mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -CompleteX '9.75') -Name 'complete self-inconsistent X mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -CompleteX '9.75' -CompleteV2InitialX '9.75') -Name 'complete-only coordinated X mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -BadY '0') -Name 'non-increasing bad Y mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -CompleteY '2') -Name 'non-increasing complete Y mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -BadAnchor '1') -Name 'bad anchor mutation'
Assert-Rejects -Lines (New-M6MovementMarkers -GoodX '1.25' -CompleteAnchor '1') -Name 'complete anchor mutation'

Write-Host 'm6-hot-reload-movement-oracle passed'
