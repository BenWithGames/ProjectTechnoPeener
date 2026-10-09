param(
    [ValidateSet('Core', 'Mod')][string]$Target = 'Core',
    [string]$UE4SSSource,
    [string]$BuildDirectory,
    [string]$ModConfiguration = 'Game__Shipping__Win64'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if ($Target -eq 'Core') {
    if (!$BuildDirectory) { $BuildDirectory = Join-Path $project 'build-core' }
    & (Join-Path $PSScriptRoot 'Build-Core.cmd') $BuildDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Core build/tests failed.' }
    return
}
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ tools and CMake.' }
    $installations = & $vswhere -all -products '*' -property installationPath
    $cmake = $installations | ForEach-Object {
        Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (!$cmake) { throw 'CMake was not found in PATH or Visual Studio.' }
}
if (!$BuildDirectory) { $BuildDirectory = Join-Path $project "build-$($Target.ToLower())" }

function Invoke-CMake {
    & $cmake @args
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE" }
}

if ($Target -eq 'Core') {
    Invoke-CMake -S $project -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 -DSC6_BUILD_MOD=OFF
    Invoke-CMake --build $BuildDirectory --config Debug
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $ctest --test-dir $BuildDirectory -C Debug --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
} else {
    if (!$UE4SSSource -or !(Test-Path -LiteralPath (Join-Path $UE4SSSource 'CMakeLists.txt'))) {
        throw 'Provide -UE4SSSource pointing to a matching UE4SS source checkout.'
    }
    if (!(Test-Path -LiteralPath (Join-Path $UE4SSSource 'deps\first\Unreal\CMakeLists.txt'))) {
        throw 'The UE4SS Unreal dependency is missing. Obtain authorized UEPseudo access and initialize the pinned submodules first.'
    }
    Invoke-CMake -S $project -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 -DSC6_BUILD_MOD=ON "-DUE4SS_SOURCE_DIR=$UE4SSSource"
    Invoke-CMake --build $BuildDirectory --config $ModConfiguration --target SC6Rollback
    Invoke-CMake --install $BuildDirectory --config $ModConfiguration --prefix (Join-Path $BuildDirectory 'package')
}
