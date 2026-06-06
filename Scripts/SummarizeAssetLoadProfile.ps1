param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [switch]$RequireCompleteModelFlush
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$InvariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
$DurationKeys = @(
    "ms",
    "read_ms",
    "decode_ms",
    "copy_ms",
    "texture_create_ms",
    "upload_ms",
    "mipgen_ms",
    "flush_ms",
    "detach_ms",
    "json_parse_ms",
    "buffer_read_ms",
    "mesh_extract_ms",
    "clusterize_ms",
    "finalize_ms"
)

function ConvertTo-DoubleOrNull {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }

    $result = 0.0
    if ([double]::TryParse($Value, [System.Globalization.NumberStyles]::Float, $InvariantCulture, [ref]$result)) {
        return $result
    }

    return $null
}

function Format-Number {
    param([AllowNull()]$Value)

    if ($null -eq $Value) {
        return ""
    }

    return [string]::Format($InvariantCulture, "{0:F3}", [double]$Value)
}

function Format-Integer {
    param([AllowNull()]$Value)

    if ($null -eq $Value) {
        return ""
    }

    return [string]::Format($InvariantCulture, "{0:0}", [double]$Value)
}

function Escape-Markdown {
    param([AllowNull()][string]$Value)

    if ($null -eq $Value) {
        return ""
    }

    return $Value.Replace("|", "\|")
}

function Get-Field {
    param(
        [hashtable]$Fields,
        [string]$Key
    )

    if ($Fields.ContainsKey($Key)) {
        return [string]$Fields[$Key]
    }

    return $null
}

function Get-NumberField {
    param(
        [hashtable]$Fields,
        [string]$Key
    )

    return ConvertTo-DoubleOrNull (Get-Field $Fields $Key)
}

function Get-StageDurationMs {
    param([hashtable]$Fields)

    $directMs = Get-NumberField $Fields "ms"
    if ($null -ne $directMs) {
        return $directMs
    }

    $total = 0.0
    $found = $false
    foreach ($key in $DurationKeys) {
        if ($key -eq "ms") {
            continue
        }

        $value = Get-NumberField $Fields $key
        if ($null -ne $value) {
            $total += $value
            $found = $true
        }
    }

    if ($found) {
        return $total
    }

    return $null
}

function Write-MarkdownTable {
    param(
        [string[]]$Headers,
        [object[]]$Rows
    )

    Write-Output ("| " + ($Headers -join " | ") + " |")
    Write-Output ("| " + (($Headers | ForEach-Object { "---" }) -join " | ") + " |")

    foreach ($row in $Rows) {
        Write-Output ("| " + (($row | ForEach-Object { Escape-Markdown ([string]$_) }) -join " | ") + " |")
    }
}

function Parse-AssetLoadProfileLine {
    param(
        [string]$Line,
        [int]$LineNumber
    )

    if (-not $Line.Contains("[AssetLoadProfile]")) {
        return $null
    }

    $fields = @{}
    $pattern = '(?<key>[A-Za-z_][A-Za-z0-9_]*)=(?:"(?<quoted>(?:\\.|[^"\\])*)"|(?<bare>\S+))'
    $matches = [regex]::Matches($Line, $pattern)

    foreach ($match in $matches) {
        $key = $match.Groups["key"].Value
        $value = $match.Groups["bare"].Value
        if ($match.Groups["quoted"].Success) {
            $value = $match.Groups["quoted"].Value
        }

        $fields[$key] = $value
    }

    if (-not $fields.ContainsKey("stage")) {
        $fields["stage"] = "<missing>"
    }

    return [pscustomobject]@{
        LineNumber = $LineNumber
        Stage = [string]$fields["stage"]
        Fields = $fields
        DurationMs = Get-StageDurationMs $fields
    }
}

function Sum-StageMetric {
    param(
        [object[]]$Records,
        [string]$Stage,
        [string]$Metric,
        [switch]$UseMsField
    )

    $total = 0.0
    $found = $false
    foreach ($record in $Records) {
        if ($record.Stage -ne $Stage) {
            continue
        }

        $value = $null
        if ($UseMsField) {
            $value = Get-NumberField $record.Fields "ms"
        }
        else {
            $value = Get-NumberField $record.Fields $Metric
        }

        if ($null -ne $value) {
            $total += $value
            $found = $true
        }
    }

    if ($found) {
        return $total
    }

    return $null
}

function Get-FirstFieldFromStages {
    param(
        [object[]]$Records,
        [string[]]$Stages,
        [string]$Field
    )

    foreach ($stage in $Stages) {
        foreach ($record in $Records) {
            if ($record.Stage -ne $stage) {
                continue
            }

            $value = Get-Field $record.Fields $Field
            if (-not [string]::IsNullOrWhiteSpace($value)) {
                return $value
            }
        }
    }

    return $null
}

function Get-MaxFieldFromRecords {
    param(
        [object[]]$Records,
        [string[]]$Fields
    )

    $max = $null
    foreach ($record in $Records) {
        foreach ($field in $Fields) {
            $value = Get-NumberField $record.Fields $field
            if ($null -ne $value -and ($null -eq $max -or $value -gt $max)) {
                $max = $value
            }
        }
    }

    return $max
}

$resolvedLogPath = Resolve-Path -LiteralPath $LogPath -ErrorAction Stop
$lineNumber = 0
$records = @()
foreach ($line in Get-Content -LiteralPath $resolvedLogPath) {
    $lineNumber++
    $record = Parse-AssetLoadProfileLine -Line $line -LineNumber $lineNumber
    if ($null -ne $record) {
        $records += $record
    }
}

if ($records.Count -eq 0) {
    Write-Error "No AssetLoadProfile records found in '$resolvedLogPath'."
    exit 1
}

$stageSet = @{}
foreach ($record in $records) {
    $stageSet[$record.Stage] = $true
}

$requiredMissing = @()
if ($RequireCompleteModelFlush) {
    foreach ($stage in @("gltf_staging_total", "gltf_finalize_total", "megamesh_gpu_upload", "renderworld_model_flush")) {
        if (-not $stageSet.ContainsKey($stage)) {
            $requiredMissing += $stage
        }
    }

    $hasTextureStage = $false
    foreach ($stage in $stageSet.Keys) {
        if ($stage -eq "texture_async_worker" -or $stage -match "texture.*flush") {
            $hasTextureStage = $true
            break
        }
    }

    if (-not $hasTextureStage) {
        $requiredMissing += "texture_async_worker_or_texture_flush"
    }
}

Write-Output "# AssetLoadProfile Summary"
Write-Output ""
Write-Output "- Log: $resolvedLogPath"
Write-Output "- Records: $($records.Count)"
Write-Output ""

$stageRows = @()
foreach ($group in ($records | Group-Object Stage | Sort-Object Name)) {
    $count = $group.Count
    $successCount = 0.0
    $failedCount = 0.0
    $durations = @()

    foreach ($record in $group.Group) {
        $successValue = Get-NumberField $record.Fields "success"
        $failedValue = Get-NumberField $record.Fields "failed"

        if ($null -ne $successValue) {
            $successCount += $successValue
        }

        if ($null -ne $failedValue) {
            $failedCount += $failedValue
        }
        elseif ($null -ne $successValue -and $successValue -eq 0.0) {
            $failedCount += 1.0
        }

        if ($null -ne $record.DurationMs) {
            $durations += [double]$record.DurationMs
        }
    }

    $totalMs = $null
    $avgMs = $null
    $maxMs = $null
    if ($durations.Count -gt 0) {
        $totalMs = ($durations | Measure-Object -Sum).Sum
        $avgMs = $totalMs / [double]$durations.Count
        $maxMs = ($durations | Measure-Object -Maximum).Maximum
    }

    $stageRows += ,@(
        $group.Name,
        $count,
        (Format-Integer $successCount),
        (Format-Integer $failedCount),
        (Format-Number $totalMs),
        (Format-Number $avgMs),
        (Format-Number $maxMs)
    )
}

Write-Output "## Stage Aggregate"
Write-Output ""
Write-MarkdownTable `
    -Headers @("stage", "count", "success", "failed", "total_ms", "avg_ms", "max_ms") `
    -Rows $stageRows
Write-Output ""

$textureRecords = $records | Where-Object {
    $_.Stage -in @("texture_async_worker", "gltf_image_read", "gltf_image_decode", "gltf_image_copy")
}

$textureRows = @()
foreach ($group in ($textureRecords | Group-Object { Get-Field $_.Fields "path" } | Sort-Object Name)) {
    if ([string]::IsNullOrWhiteSpace($group.Name)) {
        continue
    }

    $items = @($group.Group)
    $readMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "read_ms")
    $gltfReadMs = (Sum-StageMetric -Records $items -Stage "gltf_image_read" -Metric "ms" -UseMsField)
    if ($null -ne $gltfReadMs) {
        $readMs = (0.0 + $(if ($null -ne $readMs) { $readMs } else { 0.0 }) + $gltfReadMs)
    }

    $decodeMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "decode_ms")
    $gltfDecodeMs = (Sum-StageMetric -Records $items -Stage "gltf_image_decode" -Metric "ms" -UseMsField)
    if ($null -ne $gltfDecodeMs) {
        $decodeMs = (0.0 + $(if ($null -ne $decodeMs) { $decodeMs } else { 0.0 }) + $gltfDecodeMs)
    }

    $copyMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "copy_ms")
    $gltfCopyMs = (Sum-StageMetric -Records $items -Stage "gltf_image_copy" -Metric "ms" -UseMsField)
    if ($null -ne $gltfCopyMs) {
        $copyMs = (0.0 + $(if ($null -ne $copyMs) { $copyMs } else { 0.0 }) + $gltfCopyMs)
    }

    $stageNames = ($items | ForEach-Object { $_.Stage } | Sort-Object -Unique) -join ","
    $width = Get-FirstFieldFromStages $items @("texture_async_worker", "gltf_image_decode", "gltf_image_copy") "width"
    $height = Get-FirstFieldFromStages $items @("texture_async_worker", "gltf_image_decode", "gltf_image_copy") "height"
    $channels = Get-FirstFieldFromStages $items @("texture_async_worker", "gltf_image_decode") "channels"

    $textureRows += ,@(
        $group.Name,
        $stageNames,
        (Format-Number $readMs),
        (Format-Number $decodeMs),
        (Format-Number $copyMs),
        (Format-Integer (Get-MaxFieldFromRecords $items @("file_bytes", "bytes"))),
        (Format-Integer (Get-MaxFieldFromRecords $items @("pixel_bytes"))),
        $width,
        $height,
        $channels
    )
}

Write-Output "## Texture Source"
Write-Output ""
if ($textureRows.Count -gt 0) {
    Write-MarkdownTable `
        -Headers @("path", "stages", "read_ms", "decode_ms", "copy_ms", "file_bytes", "pixel_bytes", "width", "height", "channels") `
        -Rows $textureRows
}
else {
    Write-Output "_No texture source records._"
}
Write-Output ""

$modelStages = @(
    "gltf_text_read",
    "gltf_json_parse",
    "gltf_buffer_read",
    "gltf_buffer_read_total",
    "gltf_mesh_extract",
    "gltf_clusterize",
    "gltf_texture_staging",
    "gltf_staging_total",
    "gltf_finalize_total"
)
$modelRecords = $records | Where-Object { $_.Stage -in $modelStages }
$modelRows = @()
foreach ($group in ($modelRecords | Group-Object { Get-Field $_.Fields "request_id" } | Sort-Object Name)) {
    $items = @($group.Group)
    $modelPath = Get-FirstFieldFromStages $items @("gltf_staging_total", "gltf_text_read", "gltf_json_parse") "path"

    $bufferTotal = Sum-StageMetric -Records $items -Stage "gltf_buffer_read_total" -Metric "ms" -UseMsField
    if ($null -eq $bufferTotal) {
        $bufferTotal = Sum-StageMetric -Records $items -Stage "gltf_buffer_read" -Metric "ms" -UseMsField
    }

    $modelRows += ,@(
        $group.Name,
        $modelPath,
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_json_parse" -Metric "ms" -UseMsField)),
        (Format-Number $bufferTotal),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_mesh_extract" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_clusterize" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_texture_staging" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_staging_total" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_finalize_total" -Metric "ms" -UseMsField)),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "vertices"),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "indices"),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "clusters"),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "cpu_staging_bytes")
    )
}

Write-Output "## glTF / Model"
Write-Output ""
if ($modelRows.Count -gt 0) {
    Write-MarkdownTable `
        -Headers @("request_id", "path", "json_parse_ms", "buffer_read_ms", "mesh_extract_ms", "clusterize_ms", "texture_staging_ms", "staging_total_ms", "finalize_ms", "vertices", "indices", "clusters", "cpu_staging_bytes") `
        -Rows $modelRows
}
else {
    Write-Output "_No glTF/model records._"
}
Write-Output ""

$mainRenderRecords = $records | Where-Object {
    (Get-Field $_.Fields "role") -eq "main_render" -or
    $_.Stage -in @("texture_async_flush", "renderworld_texture_flush", "gltf_finalize_total", "megamesh_gpu_upload", "renderworld_model_flush")
}

$mainRows = @()
foreach ($group in ($mainRenderRecords | Group-Object Stage | Sort-Object Name)) {
    $durations = @()
    $processed = 0.0
    $success = 0.0
    $failed = 0.0
    foreach ($record in $group.Group) {
        if ($null -ne $record.DurationMs) {
            $durations += [double]$record.DurationMs
        }

        foreach ($metric in @("processed", "success", "failed")) {
            $value = Get-NumberField $record.Fields $metric
            if ($null -eq $value) {
                continue
            }

            switch ($metric) {
                "processed" { $processed += $value }
                "success" { $success += $value }
                "failed" { $failed += $value }
            }
        }
    }

    $totalMs = $null
    $maxMs = $null
    if ($durations.Count -gt 0) {
        $totalMs = ($durations | Measure-Object -Sum).Sum
        $maxMs = ($durations | Measure-Object -Maximum).Maximum
    }

    $mainRows += ,@(
        $group.Name,
        $group.Count,
        (Format-Integer $processed),
        (Format-Integer $success),
        (Format-Integer $failed),
        (Format-Number $totalMs),
        (Format-Number $maxMs)
    )
}

Write-Output "## Main-Render Flush"
Write-Output ""
if ($mainRows.Count -gt 0) {
    Write-MarkdownTable `
        -Headers @("stage", "count", "processed", "success", "failed", "total_ms", "max_ms") `
        -Rows $mainRows
}
else {
    Write-Output "_No main-render flush records._"
}
Write-Output ""

if ($requiredMissing.Count -gt 0) {
    Write-Error ("Missing required AssetLoadProfile stages: " + ($requiredMissing -join ", "))
    exit 2
}
