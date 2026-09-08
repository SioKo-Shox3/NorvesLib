[CmdletBinding(DefaultParameterSetName = 'Production')]
param(
    [Parameter(ParameterSetName = 'Production')]
    [string]$BuildDirectory = 'build',

    [Parameter(ParameterSetName = 'Production')]
    [ValidateRange(1, 100)]
    [int]$ExpectedCount = 21,

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTestR1Contract
)

$ErrorActionPreference = 'Stop'

$script:ExpectedGpuTests = @(
    'RHIGPUTimestampVulkanTest',
    'RHIGPUTimestampVulkanSkipContractTest',
    'RHITextureToBufferReadbackVulkanTest',
    'RHITextureToBufferReadbackVulkanSkipContractTest',
    'RenderingValidationIndoorFixtureVulkanTest',
    'RenderingValidationOutdoorFixtureVulkanTest',
    'RenderingValidationFixtureVulkanSkipContractTest',
    'RHIImageLayoutVulkanValidationTest',
    'RHIImageLayoutVulkanNoEligibleLightSceneTest',
    'RHIImageLayoutVulkanNoCasterSceneTest',
    'RHIImageLayoutVulkanDrawSceneTest',
    'RHIImageLayoutVulkanDrawThenNoCasterSceneTest',
    'RHIImageLayoutVulkanValidationSkipContractTest',
    'FrameCaptureFloatReadbackVulkanTest',
    'FrameCaptureFloatReadbackVulkanSkipContractTest',
    'RenderingHdrIndoorSceneVulkanTest',
    'RenderingHdrOutdoorSceneVulkanTest',
    'RenderingHdrSceneVulkanSkipContractTest',
    'RenderingGoldenIndoorVulkanTest',
    'RenderingGoldenOutdoorVulkanTest',
    'RenderingGoldenVulkanSkipContractTest'
)

$script:ExpectedSkipTests = @(
    'RHIGPUTimestampVulkanSkipContractTest',
    'RHITextureToBufferReadbackVulkanSkipContractTest',
    'RenderingValidationFixtureVulkanSkipContractTest',
    'RHIImageLayoutVulkanValidationSkipContractTest',
    'FrameCaptureFloatReadbackVulkanSkipContractTest',
    'RenderingHdrSceneVulkanSkipContractTest',
    'RenderingGoldenVulkanSkipContractTest'
)

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

function Get-TestProperty {
    param(
        [Parameter(Mandatory = $true)]$Test,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $matches = @(@($Test.properties) | Where-Object { $_.name -ceq $Name })
    if ($matches.Count -gt 1) {
        throw "$($Test.name): duplicate property '$Name'."
    }
    if ($matches.Count -eq 0) {
        return $null
    }
    return $matches[0].value
}

function Get-TestCommand {
    param([Parameter(Mandatory = $true)]$Test)
    if ($null -eq $Test.command) {
        throw "$($Test.name): command is missing."
    }
    return @(ConvertTo-PropertyValues $Test.command)
}

function Get-ExpectedCommand {
    param([Parameter(Mandatory = $true)][string]$Name)
    $commands = @{
        RHIGPUTimestampVulkanTest = @('RHIGPUTimestampVulkanTest.exe')
        RHIGPUTimestampVulkanSkipContractTest = @('RHIGPUTimestampVulkanTest.exe')
        RHITextureToBufferReadbackVulkanTest = @('RHITextureToBufferReadbackVulkanTest.exe')
        RHITextureToBufferReadbackVulkanSkipContractTest = @('RHITextureToBufferReadbackVulkanTest.exe')
        RenderingValidationIndoorFixtureVulkanTest = @('RenderingValidationFixtureVulkanTest.exe', '--scene=indoor')
        RenderingValidationOutdoorFixtureVulkanTest = @('RenderingValidationFixtureVulkanTest.exe', '--scene=outdoor')
        RenderingValidationFixtureVulkanSkipContractTest = @('RenderingValidationFixtureVulkanTest.exe', '--scene=indoor')
        RHIImageLayoutVulkanValidationTest = @('RHIImageLayoutVulkanValidationTest.exe', '--scene=indoor', '--image-layout-case=micro')
        RHIImageLayoutVulkanNoEligibleLightSceneTest = @('RHIImageLayoutVulkanValidationTest.exe', '--scene=indoor', '--image-layout-case=no-eligible-light')
        RHIImageLayoutVulkanNoCasterSceneTest = @('RHIImageLayoutVulkanValidationTest.exe', '--scene=outdoor', '--image-layout-case=no-caster')
        RHIImageLayoutVulkanDrawSceneTest = @('RHIImageLayoutVulkanValidationTest.exe', '--scene=outdoor', '--image-layout-case=draw')
        RHIImageLayoutVulkanDrawThenNoCasterSceneTest = @('RHIImageLayoutVulkanValidationTest.exe', '--scene=outdoor', '--image-layout-case=draw-then-no-caster')
        RHIImageLayoutVulkanValidationSkipContractTest = @('RHIImageLayoutVulkanValidationTest.exe', '--scene=indoor', '--image-layout-case=micro')
        FrameCaptureFloatReadbackVulkanTest = @('FrameCaptureFloatReadbackVulkanTest.exe')
        FrameCaptureFloatReadbackVulkanSkipContractTest = @('FrameCaptureFloatReadbackVulkanTest.exe')
        RenderingHdrIndoorSceneVulkanTest = @('RenderingHdrSceneCaptureTest.exe', '--scene=indoor', '--capture-source=scene-color')
        RenderingHdrOutdoorSceneVulkanTest = @('RenderingHdrSceneCaptureTest.exe', '--scene=outdoor', '--capture-source=scene-color')
        RenderingHdrSceneVulkanSkipContractTest = @('RenderingHdrSceneCaptureTest.exe', '--scene=indoor', '--capture-source=scene-color')
        RenderingGoldenIndoorVulkanTest = @('RenderingGoldenImageTest.exe', '--scene=indoor', '--capture-source=back-buffer')
        RenderingGoldenOutdoorVulkanTest = @('RenderingGoldenImageTest.exe', '--scene=outdoor', '--capture-source=back-buffer')
        RenderingGoldenVulkanSkipContractTest = @('RenderingGoldenImageTest.exe', '--scene=indoor')
    }
    if (-not $commands.ContainsKey($Name)) {
        throw "No command contract exists for '$Name'."
    }
    return @($commands[$Name])
}

function Assert-ExactSequence {
    param(
        [Parameter(Mandatory = $true)][string[]]$Actual,
        [Parameter(Mandatory = $true)][string[]]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Actual.Count -ne $Expected.Count) {
        throw "$Label cardinality mismatch: expected=$($Expected.Count) actual=$($Actual.Count)."
    }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        if ($Actual[$index] -cne $Expected[$index]) {
            throw "$Label mismatch at ${index}: expected='$($Expected[$index])' actual='$($Actual[$index])'."
        }
    }
}

function Assert-ExactSet {
    param(
        [Parameter(Mandatory = $true)][string[]]$Actual,
        [Parameter(Mandatory = $true)][string[]]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Actual.Count -ne $Expected.Count -or
        @($Expected | Where-Object { $_ -cnotin $Actual }).Count -ne 0 -or
        @($Actual | Where-Object { $_ -cnotin $Expected }).Count -ne 0) {
        throw "$Label set mismatch: expected='$($Expected -join ',')' actual='$($Actual -join ',')'."
    }
}

function Assert-GpuTestContract {
    param(
        [Parameter(Mandatory = $true)]$Document,
        [Parameter(Mandatory = $true)][int]$ExpectedCount
    )
    if ($ExpectedCount -ne 21) {
        throw "R1 GPU CTest contract requires ExpectedCount=21, got $ExpectedCount."
    }
    if ($null -eq $Document.tests) {
        throw 'CTest JSON document has no tests array.'
    }

    $gpuTests = @($Document.tests | Where-Object {
        $labels = @(ConvertTo-PropertyValues (Get-TestProperty $_ 'LABELS'))
        $name = [string]$_.name
        ($script:ExpectedGpuTests -ccontains $name) -or
        ($labels -ccontains 'GPU' -and $labels -ccontains 'RenderingValidation')
    })
    if ($gpuTests.Count -ne 21) {
        throw "GPU RenderingValidation test count mismatch: expected=21 actual=$($gpuTests.Count)"
    }

    $actualNames = @($gpuTests | ForEach-Object { [string]$_.name })
    $duplicates = @($actualNames | Group-Object | Where-Object Count -gt 1)
    if ($duplicates.Count -ne 0) {
        throw "GPU test name duplicate: $($duplicates[0].Name)."
    }
    $missing = @($script:ExpectedGpuTests | Where-Object { $_ -cnotin $actualNames })
    $extra = @($actualNames | Where-Object { $_ -cnotin $script:ExpectedGpuTests })
    if ($missing.Count -ne 0 -or $extra.Count -ne 0) {
        throw "GPU registration delta is nonzero: missing=$($missing -join ',') extra=$($extra -join ',')."
    }

    foreach ($test in $gpuTests) {
        $name = [string]$test.name
        $propertyNames = @(@($test.properties) | ForEach-Object { [string]$_.name })
        $duplicateProperties = @($propertyNames | Group-Object | Where-Object Count -gt 1)
        if ($duplicateProperties.Count -ne 0) {
            throw "${name}: duplicate CTest property '$($duplicateProperties[0].Name)'."
        }
        foreach ($requiredProperty in @('LABELS', 'RESOURCE_LOCK', 'SKIP_RETURN_CODE')) {
            if ($requiredProperty -cnotin $propertyNames) {
                throw "${name}: required property '$requiredProperty' is missing."
            }
        }

        $labels = @(ConvertTo-PropertyValues (Get-TestProperty $test 'LABELS'))
        Assert-ExactSet $labels @('GPU', 'RenderingValidation') "$name LABELS"
        $skipCode = @(ConvertTo-PropertyValues (Get-TestProperty $test 'SKIP_RETURN_CODE'))
        Assert-ExactSequence $skipCode @('125') "$name SKIP_RETURN_CODE"
        $resourceLock = @(ConvertTo-PropertyValues (Get-TestProperty $test 'RESOURCE_LOCK'))
        Assert-ExactSequence $resourceLock @('NorvesLibGPU') "$name RESOURCE_LOCK"

        $isSkip = $script:ExpectedSkipTests -ccontains $name
        $environment = @(ConvertTo-PropertyValues (Get-TestProperty $test 'ENVIRONMENT'))
        if ($isSkip) {
            Assert-ExactSequence $environment @('NORVESLIB_FORCE_GPU_TEST_SKIP=1') "$name ENVIRONMENT"
        } elseif ($environment.Count -ne 0) {
            throw "${name}: normal GPU test must not define ENVIRONMENT."
        }

        $actualCommand = @(Get-TestCommand $test)
        if ($actualCommand.Count -gt 0) {
            $actualCommand[0] = [IO.Path]::GetFileName($actualCommand[0])
        }
        Assert-ExactSequence $actualCommand @(Get-ExpectedCommand $name) "$name command"
    }

    return [pscustomobject]@{
        Count = $gpuTests.Count
        NormalCount = @($gpuTests | Where-Object { $script:ExpectedSkipTests -cnotcontains $_.name }).Count
        ForceSkipCount = @($gpuTests | Where-Object { $script:ExpectedSkipTests -ccontains $_.name }).Count
    }
}

function New-SyntheticGpuDocument {
    param([string]$Mutation = 'valid')
    $tests = [System.Collections.Generic.List[object]]::new()
    foreach ($name in $script:ExpectedGpuTests) {
        $properties = [System.Collections.Generic.List[object]]::new()
        $properties.Add([pscustomobject]@{ name = 'LABELS'; value = @('GPU', 'RenderingValidation') })
        $properties.Add([pscustomobject]@{ name = 'RESOURCE_LOCK'; value = @('NorvesLibGPU') })
        $properties.Add([pscustomobject]@{ name = 'SKIP_RETURN_CODE'; value = 125 })
        if ($script:ExpectedSkipTests -contains $name) {
            $properties.Add([pscustomobject]@{ name = 'ENVIRONMENT'; value = @('NORVESLIB_FORCE_GPU_TEST_SKIP=1') })
        }
        $expectedCommand = @(Get-ExpectedCommand $name)
        $command = [System.Collections.Generic.List[string]]::new()
        $command.Add((Join-Path 'C:\synthetic\bin' $expectedCommand[0]))
        for ($argumentIndex = 1; $argumentIndex -lt $expectedCommand.Count; ++$argumentIndex) {
            $command.Add($expectedCommand[$argumentIndex])
        }
        $tests.Add([pscustomobject]@{
            name = $name
            command = $command.ToArray()
            properties = $properties.ToArray()
        })
    }
    switch ($Mutation) {
        'missing' { $tests.RemoveAt(0) }
        'extra' {
            $extraProperties = @($tests[0].properties | ForEach-Object {
                [pscustomobject]@{ name = $_.name; value = @(ConvertTo-PropertyValues $_.value) }
            })
            ($extraProperties | Where-Object { $_.name -ceq 'LABELS' }).value = @('GPU', 'RenderingValidation', 'Unexpected')
            $extra = [pscustomobject]@{
                name = 'UnexpectedGpuTest'
                command = @($tests[0].command)
                properties = $extraProperties
            }
            $tests.Add($extra)
        }
        'duplicate' { $tests.Add($tests[0]) }
        'label-order' {
            ($tests[0].properties | Where-Object name -ceq 'LABELS').value = @('RenderingValidation', 'GPU')
        }
        'properties' { ($tests[0].properties | Where-Object name -eq 'SKIP_RETURN_CODE').value = 124 }
        'commands' { $tests[0].command = @('RHIGPUTimestampVulkanTest.exe', '--unexpected') }
        'force-skip' { ($tests[1].properties | Where-Object name -eq 'ENVIRONMENT').value = @() }
        'valid' { }
        default { throw "Unknown synthetic GPU mutation '$Mutation'." }
    }
    return [pscustomobject]@{ tests = $tests.ToArray() }
}

function Assert-ContractRejects {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )
    $rejected = $false
    try {
        & $Action | Out-Null
    } catch {
        $rejected = $true
        Write-Output "gpu_contract_negative=$Name status=rejected"
    }
    if (-not $rejected) {
        throw "GPU CTest contract negative '$Name' was accepted."
    }
}

function Invoke-R1SelfTest {
    $valid = Assert-GpuTestContract (New-SyntheticGpuDocument) 21
    if ($valid.Count -ne 21 -or $valid.NormalCount -ne 14 -or $valid.ForceSkipCount -ne 7) {
        throw 'Synthetic valid GPU CTest contract counts are invalid.'
    }
    $reordered = Assert-GpuTestContract (New-SyntheticGpuDocument 'label-order') 21
    if ($reordered.Count -ne 21) {
        throw 'Synthetic reordered GPU label set was not accepted.'
    }
    Assert-ContractRejects 'missing' { Assert-GpuTestContract (New-SyntheticGpuDocument 'missing') 21 }
    Assert-ContractRejects 'extra' { Assert-GpuTestContract (New-SyntheticGpuDocument 'extra') 21 }
    Assert-ContractRejects 'duplicate' { Assert-GpuTestContract (New-SyntheticGpuDocument 'duplicate') 21 }
    Assert-ContractRejects 'properties' { Assert-GpuTestContract (New-SyntheticGpuDocument 'properties') 21 }
    Assert-ContractRejects 'commands' { Assert-GpuTestContract (New-SyntheticGpuDocument 'commands') 21 }
    Assert-ContractRejects 'force_skip' { Assert-GpuTestContract (New-SyntheticGpuDocument 'force-skip') 21 }
    Write-Output 'Rendering GPU CTest contract self-test passed: valid=1 missing=1 extra=1 duplicate=1 properties=1 commands=1 force_skip=1'
}

if ($SelfTestR1Contract) {
    Invoke-R1SelfTest
    exit 0
}

$repoRoot = [IO.Path]::GetFullPath($PSScriptRoot + '\..')
$resolvedBuild = [IO.Path]::GetFullPath((Join-Path (Get-Location) $BuildDirectory))
$repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedBuild.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildDirectory must resolve below the repository root: $resolvedBuild"
}

$jsonLines = @(& ctest --test-dir $resolvedBuild -C Debug --show-only=json-v1 2>&1 | ForEach-Object { $_.ToString() })
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "ctest JSON-v1 query failed with exit code $exitCode`n$($jsonLines -join [Environment]::NewLine)"
}
try {
    $document = ($jsonLines -join [Environment]::NewLine) | ConvertFrom-Json
} catch {
    throw "ctest JSON-v1 output could not be parsed: $_"
}
$result = Assert-GpuTestContract $document $ExpectedCount
Write-Output "Rendering GPU CTest contract passed: count=$($result.Count) normal=$($result.NormalCount) force_skip=$($result.ForceSkipCount) families=2+2+3+6+2+3+3 registration_delta=0"
