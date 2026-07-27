Set-StrictMode -Version Latest

function ConvertFrom-M6HotReloadMovementMarker {
    param(
        [string]$Line
    )

    $pattern = '^M6_SCRIPT_SMOKE stage=(?<Stage>ready_good|ready_bad|complete) position_x=(?<X>-?\d+(?:\.\d+)?) position_y=(?<Y>-?\d+(?:\.\d+)?) anchor_z=(?<AnchorZ>-?\d+(?:\.\d+)?)(?: v2_initial_x=(?<V2InitialX>-?\d+(?:\.\d+)?))?$'
    if ($Line -notmatch $pattern) {
        throw "Unexpected movement marker: $Line"
    }

    $v2InitialX = $null
    if ($matches.ContainsKey('V2InitialX') -and -not [string]::IsNullOrEmpty($matches['V2InitialX'])) {
        $v2InitialX = [double]$matches['V2InitialX']
    }

    return [pscustomobject]@{
        Stage = $matches.Stage
        X = [double]$matches.X
        Y = [double]$matches.Y
        AnchorZ = [double]$matches.AnchorZ
        V2InitialX = $v2InitialX
    }
}

function Assert-M6HotReloadMovementContract {
    param(
        [string[]]$Lines
    )

    $markers = @()
    foreach ($line in $Lines) {
        $markers += ConvertFrom-M6HotReloadMovementMarker -Line $line
    }

    $goodMarkers = @($markers | Where-Object { $_.Stage -eq 'ready_good' })
    $badMarkers = @($markers | Where-Object { $_.Stage -eq 'ready_bad' })
    $completeMarkers = @($markers | Where-Object { $_.Stage -eq 'complete' })
    if ($goodMarkers.Count -ne 1 -or $badMarkers.Count -ne 1 -or $completeMarkers.Count -ne 1) {
        throw 'Expected exactly one ready_good, ready_bad, and complete marker'
    }

    $good = $goodMarkers[0]
    $bad = $badMarkers[0]
    $complete = $completeMarkers[0]
    if ($good.X -le 0.0) {
        throw 'ready_good X must be positive'
    }
    if ($good.AnchorZ -ne 1.0 -or $bad.AnchorZ -ne 2.0 -or $complete.AnchorZ -ne 2.0) {
        throw 'Movement anchor mismatch'
    }
    if ($bad.Y -le $good.Y -or $complete.Y -le $bad.Y) {
        throw 'Y movement did not increase across hot reload stages'
    }
    if ($null -eq $bad.V2InitialX -or $null -eq $complete.V2InitialX) {
        throw 'v2 markers require V2InitialX'
    }
    if ($bad.X -ne $bad.V2InitialX) {
        throw 'ready_bad X differs from its V2InitialX'
    }
    if ($complete.X -ne $complete.V2InitialX) {
        throw 'complete X differs from its V2InitialX'
    }
}
