param(
    [string]$BuildDirectory = 'build',
    [ValidateRange(1, 100)]
    [int]$ExpectedCount
)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedBuild = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $BuildDirectory))
$repoPrefix = $repoRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $resolvedBuild.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    Write-Error "BuildDirectory must resolve below the repository root: $resolvedBuild"
    exit 1
}

$jsonText = & ctest --test-dir $resolvedBuild -C Debug --show-only=json-v1 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "ctest JSON-v1 query failed with exit code $LASTEXITCODE`n$jsonText"
    exit 1
}

try {
    $document = ($jsonText -join [Environment]::NewLine) | ConvertFrom-Json
}
catch {
    Write-Error "ctest JSON-v1 output could not be parsed: $_"
    exit 1
}

function Get-TestProperty {
    param($Test, [string]$Name)
    $property = @($Test.properties) | Where-Object { $_.name -eq $Name } | Select-Object -First 1
    if ($null -eq $property) {
        return $null
    }
    return $property.value
}

function ConvertTo-PropertyValues {
    param($Value)
    if ($null -eq $Value) {
        return @()
    }
    if ($Value -is [System.Array]) {
        return @($Value | ForEach-Object { [string]$_ })
    }
    return @([string]$Value)
}

$gpuTests = @($document.tests | Where-Object {
    $labels = @(ConvertTo-PropertyValues (Get-TestProperty $_ 'LABELS'))
    $labels -contains 'GPU' -and $labels -contains 'RenderingValidation'
})

$failures = [System.Collections.Generic.List[string]]::new()
if ($gpuTests.Count -ne $ExpectedCount) {
    $failures.Add("count: expected $ExpectedCount, actual $($gpuTests.Count)")
}

foreach ($test in $gpuTests) {
    $skipCode = @(ConvertTo-PropertyValues (Get-TestProperty $test 'SKIP_RETURN_CODE'))
    if ($skipCode.Count -ne 1 -or $skipCode[0] -ne '125') {
        $failures.Add("$($test.name): SKIP_RETURN_CODE must be 125")
    }

    $resourceLock = @(ConvertTo-PropertyValues (Get-TestProperty $test 'RESOURCE_LOCK'))
    if ($resourceLock.Count -ne 1 -or $resourceLock[0] -ne 'NorvesLibGPU') {
        $failures.Add("$($test.name): RESOURCE_LOCK must be NorvesLibGPU")
    }

    $environment = @(ConvertTo-PropertyValues (Get-TestProperty $test 'ENVIRONMENT'))
    $hasForceSkip = $environment -contains 'NORVESLIB_FORCE_GPU_TEST_SKIP=1'
    $isSkipContract = $test.name.EndsWith('SkipContractTest', [System.StringComparison]::Ordinal)
    if ($hasForceSkip -ne $isSkipContract) {
        $failures.Add("$($test.name): ENVIRONMENT force-skip presence must match SkipContractTest suffix")
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

$sortedNames = @($gpuTests.name | Sort-Object)
Write-Output "Rendering GPU CTest contract passed: count=$($gpuTests.Count)"
$sortedNames | ForEach-Object { Write-Output $_ }
