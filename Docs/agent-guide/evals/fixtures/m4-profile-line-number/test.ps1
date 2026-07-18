param(
    [string]$ContractPath = (Join-Path $PSScriptRoot 'CookedModelGameProfileContract.ps1'),
    [string]$ContractText = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrEmpty($ContractText)) {
    if (-not (Test-Path -LiteralPath $ContractPath -PathType Leaf)) {
        throw "Missing contract: $ContractPath"
    }
    $ContractText = [System.IO.File]::ReadAllText($ContractPath)
}
[void][scriptblock]::Create($ContractText)
. ([scriptblock]::Create($ContractText))

function Assert-Throws {
    param(
        [scriptblock]$Action,
        [string]$Name
    )

    try {
        & $Action
    }
    catch {
        return
    }
    throw "Expected rejection: $Name"
}

function New-ApprovedLine {
    param(
        [hashtable]$Record,
        [string]$LineNumber
    )

    return "[2026-07-17 17:48:01.921] [ERROR] [T:62468] [$($Record.Category)] [$($Record.File):$($Record.Function):$LineNumber] $($Record.Message)"
}

function Get-RuntimePositiveLineNumbers {
    $visibleLineNumbers = @('1', '2', '10', '4242', '2147483647', '851', '857', '215', '216', '97', '98')
    $numbers = @()
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        foreach ($length in @(3, 8, 16)) {
            do {
                $bytes = New-Object byte[] $length
                $rng.GetBytes($bytes)
                $digits = New-Object char[] $length
                $digits[0] = [char](49 + ($bytes[0] % 9))
                for ($index = 1; $index -lt $length; ++$index) {
                    $digits[$index] = [char](48 + ($bytes[$index] % 10))
                }
                $candidate = -join $digits
            } while ($visibleLineNumbers -contains $candidate -or $numbers -contains $candidate)
            $numbers += $candidate
        }
    }
    finally {
        $rng.Dispose()
    }
    return $numbers
}

$records = @(
    @{
        Name = 'TextureResources'
        CurrentLine = '851'
        ChangedLine = '857'
        Category = 'TextureResources'
        File = 'TextureAssetRuntime.cpp'
        Function = 'NorvesLib::Core::Rendering::TextureAssetRuntime::FlushCompletedTextureLoads'
        Message = 'Async texture load failed: C:\RuntimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png'
        Path = 'C:\RuntimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png'
    },
    @{
        Name = 'SlangCompiler'
        CurrentLine = '215'
        ChangedLine = '216'
        Category = 'SlangCompiler'
        File = 'VulkanSlangCompiler.cpp'
        Function = 'NorvesLib::RHI::Vulkan::VulkanSlangCompiler::CompileFromSource'
        Message = 'Cannot compile [neural_material_decode.slang]: Slang SDK not available. Rebuild with NORVES_HAS_SLANG to enable.'
        Path = 'neural_material_decode.slang'
    },
    @{
        Name = 'ShaderManager'
        CurrentLine = '97'
        ChangedLine = '98'
        Category = 'ShaderManager'
        File = 'ShaderManager.cpp'
        Function = 'NorvesLib::Core::Rendering::ShaderManager::LoadShader'
        Message = 'Failed to compile shader [neural_material_decode.slang]: Slang SDK not available. Rebuild with NORVES_HAS_SLANG to enable.'
        Path = 'neural_material_decode.slang'
    }
)
$runtimePositiveLineNumbers = @(Get-RuntimePositiveLineNumbers)
$positiveLineNumbers = @('1', '2', '10', '4242', '2147483647') + $runtimePositiveLineNumbers

foreach ($record in $records) {
    foreach ($lineNumber in @($positiveLineNumbers + $record.CurrentLine + $record.ChangedLine)) {
        Assert-ProfileRuntimeSignals -Lines @(New-ApprovedLine -Record $record -LineNumber $lineNumber)
    }

    $approvedLine = New-ApprovedLine -Record $record -LineNumber $record.CurrentLine
    foreach ($invalidLine in @('0', '-1', '85x', ('1' + [char]0x0661))) {
        Assert-Throws {
            Assert-ProfileRuntimeSignals -Lines @($approvedLine.Replace(":$($record.CurrentLine)]", ":$invalidLine]"))
        } "$($record.Name) invalid physical line number"
    }
    foreach ($mutatedLine in @(
            $approvedLine.Replace("[$($record.Category)]", '[OtherCategory]'),
            $approvedLine.Replace($record.File, "Other$($record.File)"),
            $approvedLine.Replace($record.Function, "$($record.Function)Other"),
            $approvedLine.Replace($record.Message, 'Other message'),
            $approvedLine.Replace($record.Path, "other_$($record.Path)")
        )) {
        Assert-Throws {
            Assert-ProfileRuntimeSignals -Lines @($mutatedLine)
        } "$($record.Name) envelope mutation"
    }
    foreach ($injectedLine in @("prefix $approvedLine", "$approvedLine suffix")) {
        Assert-Throws {
            Assert-ProfileRuntimeSignals -Lines @($injectedLine)
        } "$($record.Name) structural injection"
    }
}

Write-Host 'm4-profile-line-number passed'
