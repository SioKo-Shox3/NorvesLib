param(
    [ValidateRange(1, 100)]
    [int]$Iterations = 10,
    [switch]$RequireGpu
)

$ErrorActionPreference = 'Stop'

function Get-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

function Assert-PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $normalizedPath = Get-NormalizedPath $Path
    $normalizedRoot = (Get-NormalizedPath $Root).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $prefix = $normalizedRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $normalizedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside its required root: $normalizedPath"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Get-NormalizedPath (Join-Path $repoRoot 'build')
$runsRoot = Get-NormalizedPath (Join-Path $buildRoot 'RenderingValidation\Runs')
$exe = Get-NormalizedPath (Join-Path $buildRoot 'Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe')

Assert-PathWithinRoot $buildRoot $repoRoot 'Build root'
Assert-PathWithinRoot $runsRoot $buildRoot 'Rendering validation runs root'
Assert-PathWithinRoot $exe $buildRoot 'Golden executable'

if (-not [IO.File]::Exists($exe)) {
    Write-Error 'Golden executable is missing. Run: cmake --build build --config Debug --target RenderingGoldenImageTest'
    exit 1
}

New-Item -ItemType Directory -Force -Path $runsRoot | Out-Null

function Invoke-GoldenComparison {
    param([Parameter(Mandatory = $true)][string]$Scene)
    Push-Location $runsRoot
    try {
        & $exe "--scene=$Scene" | Out-Host
        return $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}

for ($run = 1; $run -le $Iterations; ++$run) {
    foreach ($scene in @('indoor', 'outdoor')) {
        $exitCode = Invoke-GoldenComparison $scene
        if ($exitCode -eq 125) {
            if ($RequireGpu) {
                throw "GPU was required: $scene run=$run"
            }
            Write-Host "GPU skip: $scene run=$run"
            continue
        }
        if ($exitCode -ne 0) {
            throw "Golden run failed: $scene run=$run exit=$exitCode"
        }
        Write-Host "Golden pass: $scene run=$run"
    }
}
