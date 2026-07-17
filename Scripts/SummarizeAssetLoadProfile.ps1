param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [switch]$RequireCompleteModelFlush,

    [switch]$RequireCompleteCookedModel,

    [string]$ExpectedCookedModelLogicalPath = "",

    [string]$ExpectedCookedModelDebugName = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($RequireCompleteModelFlush -and $RequireCompleteCookedModel) {
    [Console]::Error.WriteLine("Cooked model contract failed: incompatible switches")
    exit 2
}

$InvariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
$DurationKeys = @(
    "ms",
    "read_ms",
    "decode_ms",
    "copy_ms",
    "resolve_ms",
    "parse_ms",
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

function Fail-CookedModelContract {
    param([string]$Reason)

    [Console]::Error.WriteLine("Cooked model contract failed: $Reason")
    exit 2
}

function Get-SingleCookedContractRecord {
    param([string]$Stage)

    $matches = @($records | Where-Object { $_.Stage -eq $Stage })
    if ($matches.Count -eq 0) {
        Fail-CookedModelContract "missing required stage=$Stage"
    }
    if ($matches.Count -ne 1) {
        Fail-CookedModelContract "stage=$Stage must occur exactly once"
    }
    return $matches[0]
}

if ($RequireCompleteCookedModel) {
    if ([string]::IsNullOrWhiteSpace($ExpectedCookedModelLogicalPath)) {
        Fail-CookedModelContract "ExpectedCookedModelLogicalPath is required"
    }
    if ([string]::IsNullOrWhiteSpace($ExpectedCookedModelDebugName)) {
        Fail-CookedModelContract "ExpectedCookedModelDebugName is required"
    }

    $forbiddenStages = @(
        "gltf_text_read",
        "gltf_json_parse",
        "gltf_buffer_metadata_parse",
        "gltf_buffer_read",
        "gltf_buffer_read_total",
        "gltf_mesh_extract",
        "gltf_clusterize",
        "gltf_texture_staging",
        "gltf_staging_total",
        "gltf_model_flush"
    )
    foreach ($stage in $forbiddenStages) {
        if ($stageSet.ContainsKey($stage)) {
            Fail-CookedModelContract "forbidden stage=$stage"
        }
    }

    $resolveRecord = Get-SingleCookedContractRecord "model_asset_resolve"
    $parseRecord = Get-SingleCookedContractRecord "model_cooked_parse"
    foreach ($record in @($resolveRecord, $parseRecord)) {
        $source = Get-Field $record.Fields "source"
        if ($source -ne "cooked_nvmesh") {
            Fail-CookedModelContract "stage=$($record.Stage) source must be cooked_nvmesh"
        }
        if ((Get-Field $record.Fields "role") -ne "worker") {
            Fail-CookedModelContract "stage=$($record.Stage) role must be worker"
        }

        $requestId = Get-NumberField $record.Fields "request_id"
        if ($null -eq $requestId -or $requestId -le 0 -or $requestId -ne [math]::Truncate($requestId)) {
            Fail-CookedModelContract "stage=$($record.Stage) nonzero request_id required"
        }
        if ((Get-NumberField $record.Fields "status") -ne 0) {
            Fail-CookedModelContract "stage=$($record.Stage) status must be 0"
        }
        if ((Get-NumberField $record.Fields "success") -ne 1) {
            Fail-CookedModelContract "stage=$($record.Stage) success must be 1"
        }
    }

    $contractRequestId = Get-Field $resolveRecord.Fields "request_id"
    if ((Get-Field $parseRecord.Fields "request_id") -ne $contractRequestId) {
        Fail-CookedModelContract "resolve/parse request_id mismatch"
    }
    $contractPath = Get-Field $resolveRecord.Fields "normalized_path"
    if ([string]::IsNullOrWhiteSpace($contractPath)) {
        Fail-CookedModelContract "normalized_path must be non-empty"
    }
    if ((Get-Field $parseRecord.Fields "normalized_path") -ne $contractPath) {
        Fail-CookedModelContract "resolve/parse normalized_path mismatch"
    }
    if ($contractPath -ne $ExpectedCookedModelLogicalPath) {
        Fail-CookedModelContract "normalized_path mismatch: expected=$ExpectedCookedModelLogicalPath actual=$contractPath"
    }

    $finalizeStages = @(
        "model_finalize_textures",
        "model_finalize_megamesh",
        "model_finalize_register",
        "model_finalize_total"
    )
    $finalizeRecords = @()
    foreach ($stage in $finalizeStages) {
        $matches = @($records | Where-Object { $_.Stage -eq $stage })
        if ($matches.Count -eq 0) {
            Fail-CookedModelContract "missing finalize stage=$stage"
        }
        if ($matches.Count -ne 1) {
            Fail-CookedModelContract "finalize stage=$stage must occur exactly once"
        }
        $finalizeRecords += $matches[0]
    }

    $contractDebugName = Get-Field $finalizeRecords[0].Fields "debug_name"
    if ([string]::IsNullOrWhiteSpace($contractDebugName)) {
        Fail-CookedModelContract "finalize debug_name must be non-empty"
    }
    if ($contractDebugName -ne $ExpectedCookedModelDebugName) {
        Fail-CookedModelContract "finalize debug_name mismatch: expected=$ExpectedCookedModelDebugName actual=$contractDebugName"
    }
    foreach ($record in $finalizeRecords) {
        if ((Get-Field $record.Fields "role") -ne "main_render") {
            Fail-CookedModelContract "stage=$($record.Stage) role must be main_render"
        }
        if ((Get-Field $record.Fields "request_id") -ne $contractRequestId) {
            Fail-CookedModelContract "stage=$($record.Stage) request_id mismatch"
        }
        if ((Get-Field $record.Fields "debug_name") -ne $contractDebugName) {
            Fail-CookedModelContract "stage=$($record.Stage) debug_name mismatch"
        }
        if ((Get-NumberField $record.Fields "success") -ne 1) {
            Fail-CookedModelContract "stage=$($record.Stage) success must be 1"
        }
    }

    $registerPath = Get-Field $finalizeRecords[2].Fields "path"
    $totalPath = Get-Field $finalizeRecords[3].Fields "path"
    if ($registerPath -ne $totalPath -or $registerPath -ne $ExpectedCookedModelLogicalPath) {
        Fail-CookedModelContract "finalize path mismatch"
    }

    $gpuRecords = @($records | Where-Object {
        $_.Stage -eq "megamesh_gpu_upload" -and
        (Get-Field $_.Fields "debug_name") -eq $ExpectedCookedModelDebugName
    })
    if ($gpuRecords.Count -eq 0) {
        Fail-CookedModelContract "missing required stage=megamesh_gpu_upload"
    }
    foreach ($record in $gpuRecords) {
        if ((Get-Field $record.Fields "role") -ne "main_render") {
            Fail-CookedModelContract "stage=megamesh_gpu_upload role must be main_render"
        }
        if ((Get-NumberField $record.Fields "success") -ne 1) {
            Fail-CookedModelContract "stage=megamesh_gpu_upload success must be 1"
        }
    }

    $flushRecords = @($records | Where-Object { $_.Stage -eq "model_async_flush" })
    if ($flushRecords.Count -eq 0) {
        Fail-CookedModelContract "missing required stage=model_async_flush"
    }
    $hasSuccessfulFlush = $false
    foreach ($record in $flushRecords) {
        $processed = Get-NumberField $record.Fields "processed"
        $success = Get-NumberField $record.Fields "success"
        if ($null -ne $processed -and $processed -ge 1 -and $success -eq 1) {
            $hasSuccessfulFlush = $true
            break
        }
    }
    if (-not $hasSuccessfulFlush) {
        Fail-CookedModelContract "stage=model_async_flush processed must be >= 1 and success must be 1"
    }
}

$requiredMissing = @()
if ($RequireCompleteModelFlush) {
    foreach ($stage in @("gltf_staging_total", "model_finalize_total", "megamesh_gpu_upload", "renderworld_model_flush")) {
        if (-not $stageSet.ContainsKey($stage)) {
            $requiredMissing += $stage
        }
    }

    $hasTextureStage = $false
    foreach ($stage in $stageSet.Keys) {
        if ($stage -in @(
                "texture_async_worker",
                "texture_asset_resolve",
                "texture_cooked_upload",
                "texture_prepare_asset",
                "texture_prepared_cooked_upload",
                "texture_prepared_finalize",
                "texture_prepared_split"
            ) -or $stage -match "texture.*flush") {
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
    $_.Stage -in @(
        "texture_sync_stbi_file",
        "texture_sync_stbi_memory",
        "texture_async_worker",
        "texture_asset_resolve",
        "texture_cooked_parse",
        "texture_cooked_upload",
        "texture_prepare_asset",
        "texture_prepared_cooked_upload",
        "texture_prepared_finalize",
        "texture_prepared_split",
        "texture_async_upload_fallback_decode",
        "gltf_image_read",
        "gltf_image_decode",
        "gltf_image_copy"
    )
}

$textureRows = @()
foreach ($group in ($textureRecords | Group-Object { Get-Field $_.Fields "path" } | Sort-Object Name)) {
    if ([string]::IsNullOrWhiteSpace($group.Name)) {
        continue
    }

    $items = @($group.Group)
    $source = Get-FirstFieldFromStages $items @(
        "texture_cooked_upload",
        "texture_prepared_cooked_upload",
        "texture_prepared_finalize",
        "texture_prepare_asset",
        "texture_cooked_parse",
        "texture_async_worker",
        "texture_sync_stbi_file",
        "texture_sync_stbi_memory",
        "texture_asset_resolve",
        "texture_async_upload_fallback_decode"
    ) "source"

    $readMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "read_ms")
    $syncFileReadMs = (Sum-StageMetric -Records $items -Stage "texture_sync_stbi_file" -Metric "ms" -UseMsField)
    if ($null -ne $syncFileReadMs) {
        $readMs = (0.0 + $(if ($null -ne $readMs) { $readMs } else { 0.0 }) + $syncFileReadMs)
    }

    $gltfReadMs = (Sum-StageMetric -Records $items -Stage "gltf_image_read" -Metric "ms" -UseMsField)
    if ($null -ne $gltfReadMs) {
        $readMs = (0.0 + $(if ($null -ne $readMs) { $readMs } else { 0.0 }) + $gltfReadMs)
    }

    $decodeMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "decode_ms")
    $syncMemoryDecodeMs = (Sum-StageMetric -Records $items -Stage "texture_sync_stbi_memory" -Metric "decode_ms")
    if ($null -ne $syncMemoryDecodeMs) {
        $decodeMs = (0.0 + $(if ($null -ne $decodeMs) { $decodeMs } else { 0.0 }) + $syncMemoryDecodeMs)
    }

    $fallbackDecodeMs = (Sum-StageMetric -Records $items -Stage "texture_async_upload_fallback_decode" -Metric "decode_ms")
    if ($null -ne $fallbackDecodeMs) {
        $decodeMs = (0.0 + $(if ($null -ne $decodeMs) { $decodeMs } else { 0.0 }) + $fallbackDecodeMs)
    }

    $gltfDecodeMs = (Sum-StageMetric -Records $items -Stage "gltf_image_decode" -Metric "ms" -UseMsField)
    if ($null -ne $gltfDecodeMs) {
        $decodeMs = (0.0 + $(if ($null -ne $decodeMs) { $decodeMs } else { 0.0 }) + $gltfDecodeMs)
    }

    $copyMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "copy_ms")
    $syncMemoryCopyMs = (Sum-StageMetric -Records $items -Stage "texture_sync_stbi_memory" -Metric "copy_ms")
    if ($null -ne $syncMemoryCopyMs) {
        $copyMs = (0.0 + $(if ($null -ne $copyMs) { $copyMs } else { 0.0 }) + $syncMemoryCopyMs)
    }

    $fallbackCopyMs = (Sum-StageMetric -Records $items -Stage "texture_async_upload_fallback_decode" -Metric "copy_ms")
    if ($null -ne $fallbackCopyMs) {
        $copyMs = (0.0 + $(if ($null -ne $copyMs) { $copyMs } else { 0.0 }) + $fallbackCopyMs)
    }

    $gltfCopyMs = (Sum-StageMetric -Records $items -Stage "gltf_image_copy" -Metric "ms" -UseMsField)
    if ($null -ne $gltfCopyMs) {
        $copyMs = (0.0 + $(if ($null -ne $copyMs) { $copyMs } else { 0.0 }) + $gltfCopyMs)
    }

    $resolveMs = (Sum-StageMetric -Records $items -Stage "texture_asset_resolve" -Metric "resolve_ms")
    $workerResolveMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "resolve_ms")
    if ($null -ne $workerResolveMs) {
        $resolveMs = (0.0 + $(if ($null -ne $resolveMs) { $resolveMs } else { 0.0 }) + $workerResolveMs)
    }

    $parseMs = (Sum-StageMetric -Records $items -Stage "texture_cooked_parse" -Metric "parse_ms")
    $workerParseMs = (Sum-StageMetric -Records $items -Stage "texture_async_worker" -Metric "parse_ms")
    if ($null -ne $workerParseMs) {
        $parseMs = (0.0 + $(if ($null -ne $parseMs) { $parseMs } else { 0.0 }) + $workerParseMs)
    }

    $uploadMs = (Sum-StageMetric -Records $items -Stage "texture_cooked_upload" -Metric "upload_ms")
    $preparedUploadMs = (Sum-StageMetric -Records $items -Stage "texture_prepared_cooked_upload" -Metric "upload_ms")
    if ($null -ne $preparedUploadMs) {
        $uploadMs = (0.0 + $(if ($null -ne $uploadMs) { $uploadMs } else { 0.0 }) + $preparedUploadMs)
    }

    $preparedSplitCount = @($items | Where-Object { $_.Stage -eq "texture_prepared_split" }).Count
    $preparedFinalizeSuccess = $null
    $preparedFinalizeRecords = @($items | Where-Object { $_.Stage -eq "texture_prepared_finalize" })
    if ($preparedFinalizeRecords.Count -gt 0) {
        $preparedFinalizeSuccessTotal = 0.0
        foreach ($preparedFinalizeRecord in $preparedFinalizeRecords) {
            $successValue = Get-NumberField $preparedFinalizeRecord.Fields "success"
            if ($null -ne $successValue) {
                $preparedFinalizeSuccessTotal += $successValue
            }
        }
        $preparedFinalizeSuccess = $preparedFinalizeSuccessTotal
    }

    $stageNames = ($items | ForEach-Object { $_.Stage } | Sort-Object -Unique) -join ","
    $preparedStatus = Get-FirstFieldFromStages $items @("texture_prepare_asset") "status"
    $width = Get-FirstFieldFromStages $items @("texture_prepared_cooked_upload", "texture_prepared_split", "texture_cooked_upload", "texture_async_worker", "texture_sync_stbi_file", "texture_sync_stbi_memory", "gltf_image_decode", "gltf_image_copy") "width"
    $height = Get-FirstFieldFromStages $items @("texture_prepared_cooked_upload", "texture_prepared_split", "texture_cooked_upload", "texture_async_worker", "texture_sync_stbi_file", "texture_sync_stbi_memory", "gltf_image_decode", "gltf_image_copy") "height"
    $channels = Get-FirstFieldFromStages $items @("texture_async_worker", "texture_sync_stbi_file", "texture_sync_stbi_memory", "gltf_image_decode") "channels"

    $textureRows += ,@(
        $group.Name,
        $source,
        $stageNames,
        (Format-Number $readMs),
        (Format-Number $resolveMs),
        (Format-Number $parseMs),
        (Format-Number $decodeMs),
        (Format-Number $copyMs),
        (Format-Number $uploadMs),
        $preparedStatus,
        (Format-Integer $preparedSplitCount),
        (Format-Integer $preparedFinalizeSuccess),
        (Format-Integer (Get-MaxFieldFromRecords $items @("file_bytes", "bytes"))),
        (Format-Integer (Get-MaxFieldFromRecords $items @("pixel_bytes", "uploaded_bytes", "pixels"))),
        $width,
        $height,
        $channels
    )
}

Write-Output "## Texture Source"
Write-Output ""
if ($textureRows.Count -gt 0) {
    Write-MarkdownTable `
        -Headers @("path", "source", "stages", "read_ms", "resolve_ms", "parse_ms", "decode_ms", "copy_ms", "upload_ms", "prepared_status", "prepared_split_count", "prepared_finalize_success", "file_bytes", "pixel_bytes", "width", "height", "channels") `
        -Rows $textureRows
}
else {
    Write-Output "_No texture source records._"
}
Write-Output ""

$modelStages = @(
    "model_asset_resolve",
    "model_cooked_parse",
    "gltf_text_read",
    "gltf_json_parse",
    "gltf_buffer_read",
    "gltf_buffer_read_total",
    "gltf_mesh_extract",
    "gltf_clusterize",
    "gltf_texture_staging",
    "gltf_staging_total",
    "model_finalize_textures",
    "model_finalize_total"
)
$modelRecords = $records | Where-Object { $_.Stage -in $modelStages }
$modelRows = @()
foreach ($group in ($modelRecords | Group-Object { Get-Field $_.Fields "request_id" } | Sort-Object Name)) {
    $items = @($group.Group)
    $modelPath = Get-FirstFieldFromStages $items @("model_asset_resolve", "model_cooked_parse") "normalized_path"
    if ([string]::IsNullOrWhiteSpace($modelPath)) {
        $modelPath = Get-FirstFieldFromStages $items @("gltf_staging_total", "gltf_text_read", "gltf_json_parse") "path"
    }
    $modelSource = Get-FirstFieldFromStages $items @("model_asset_resolve", "model_cooked_parse") "source"
    if ([string]::IsNullOrWhiteSpace($modelSource)) {
        $modelSource = "loose_gltf"
    }

    $bufferTotal = Sum-StageMetric -Records $items -Stage "gltf_buffer_read_total" -Metric "ms" -UseMsField
    if ($null -eq $bufferTotal) {
        $bufferTotal = Sum-StageMetric -Records $items -Stage "gltf_buffer_read" -Metric "ms" -UseMsField
    }

    $modelRows += ,@(
        $modelSource,
        $group.Name,
        $modelPath,
        (Get-FirstFieldFromStages $items @("model_asset_resolve") "status"),
        (Get-FirstFieldFromStages $items @("model_cooked_parse") "status"),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_json_parse" -Metric "ms" -UseMsField)),
        (Format-Number $bufferTotal),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_mesh_extract" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_clusterize" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_texture_staging" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "gltf_staging_total" -Metric "ms" -UseMsField)),
        (Format-Number (Sum-StageMetric -Records $items -Stage "model_finalize_total" -Metric "ms" -UseMsField)),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "vertices"),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "indices"),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "clusters"),
        (Get-FirstFieldFromStages $items @("gltf_staging_total") "cpu_staging_bytes"),
        (Format-Integer (Get-MaxFieldFromRecords $items @("loose_texture_bytes"))),
        (Format-Integer (Get-MaxFieldFromRecords $items @("prepared_textures")))
    )
}

Write-Output "## glTF / Model"
Write-Output ""
if ($modelRows.Count -gt 0) {
    Write-MarkdownTable `
        -Headers @("source", "request_id", "path", "resolve_status", "parse_status", "json_parse_ms", "buffer_read_ms", "mesh_extract_ms", "clusterize_ms", "texture_staging_ms", "staging_total_ms", "finalize_ms", "vertices", "indices", "clusters", "cpu_staging_bytes", "loose_texture_bytes", "prepared_textures") `
        -Rows $modelRows
}
else {
    Write-Output "_No glTF/model records._"
}
Write-Output ""

if ($RequireCompleteCookedModel) {
    Write-Output "Cooked model contract: PASS"
    Write-Output ""
}

$mainRenderRecords = $records | Where-Object {
    (Get-Field $_.Fields "role") -eq "main_render" -or
    $_.Stage -in @("texture_async_flush", "renderworld_texture_flush", "model_finalize_total", "megamesh_gpu_upload", "renderworld_model_flush")
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
