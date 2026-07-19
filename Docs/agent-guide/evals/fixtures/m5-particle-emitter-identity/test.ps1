param(
    [string]$SourcePath = (Join-Path $PSScriptRoot 'ParticleEmitterIdentity.cpp')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

$resolvedSourcePath = (Resolve-Path -LiteralPath $SourcePath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $resolvedSourcePath -PathType Leaf)) {
    throw "Source is not a file: $resolvedSourcePath"
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("m5-particle-emitter-identity-" + [System.Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Destination (Join-Path $temporaryRoot 'CMakeLists.txt')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ParticleEmitterIdentityTest.cpp') -Destination (Join-Path $temporaryRoot 'ParticleEmitterIdentityTest.cpp')
    Copy-Item -LiteralPath $resolvedSourcePath -Destination (Join-Path $temporaryRoot 'ParticleEmitterIdentity.cpp')

    $buildDirectory = Join-Path $temporaryRoot 'build'
    Invoke-Checked 'CMake configure' {
        cmake -S $temporaryRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
    }
    Invoke-Checked 'CMake build' {
        cmake --build $buildDirectory --config Release
    }
    Invoke-Checked 'CTest' {
        ctest --test-dir $buildDirectory -C Release --output-on-failure
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host 'm5-particle-emitter-identity passed'
