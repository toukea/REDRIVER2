param([ValidateSet('Debug','Release','Release_dev')][string]$Configuration = 'Release', [switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path src_rebuild\dependencies | Out-Null
    function Ensure-Dependency([string]$Uri, [string]$Archive, [string]$Destination, [string]$Required) {
        if (!(Test-Path -LiteralPath $Required)) {
            Write-Host "Downloading $Uri"
            Invoke-WebRequest -Uri $Uri -OutFile $Archive
            Expand-Archive -LiteralPath $Archive -DestinationPath $Destination -Force
            if (!(Test-Path -LiteralPath $Required)) { throw "Dependency not found after extraction: $Required" }
        }
    }
    Ensure-Dependency 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.2/SDL2-devel-2.30.2-VC.zip' 'src_rebuild/dependencies/sdl.zip' 'src_rebuild/dependencies' 'src_rebuild/dependencies/SDL2-2.30.2/include/SDL.h'
    if (!$TestsOnly) {
        Ensure-Dependency 'https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-windows.zip' 'src_rebuild/dependencies/premake.zip' 'src_rebuild' 'src_rebuild/premake5.exe'
        Ensure-Dependency 'https://github.com/kcat/openal-soft/releases/download/1.23.1/openal-soft-1.23.1-bin.zip' 'src_rebuild/dependencies/openal.zip' 'src_rebuild/dependencies' 'src_rebuild/dependencies/openal-soft-1.23.1-bin/include/AL/al.h'
        Ensure-Dependency 'https://www.ijg.org/files/jpegsr9d.zip' 'src_rebuild/dependencies/jpeg.zip' 'src_rebuild/dependencies' 'src_rebuild/dependencies/jpeg-9d/jpeglib.h'
    }
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$installation) { throw 'Visual Studio C++ build tools are required.' }
    $compiler = Get-ChildItem -LiteralPath "$installation\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $cl = Join-Path $compiler.FullName 'bin\Hostx64\x64\cl.exe'
    $vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
    New-Item -ItemType Directory -Force -Path src_rebuild\bin\tests | Out-Null
    & cmd.exe /d /c "`"$vcvars`" >nul && `"$cl`" /nologo /EHsc /std:c++14 /W4 tests\wheel_model_tests.cpp /Fesrc_rebuild\bin\tests\wheel_model_tests.exe /Fosrc_rebuild\bin\tests\wheel_model_tests.obj"
    if ($LASTEXITCODE) { throw 'Wheel model test compilation failed.' }
    & .\src_rebuild\bin\tests\wheel_model_tests.exe
    if ($LASTEXITCODE) { throw 'Wheel model tests failed.' }
    & cmd.exe /d /c "`"$vcvars`" >nul && `"$cl`" /nologo /EHsc /std:c++14 /W4 tests\cockpit_model_tests.cpp /Fesrc_rebuild\bin\tests\cockpit_model_tests.exe /Fosrc_rebuild\bin\tests\cockpit_model_tests.obj"
    if ($LASTEXITCODE) { throw 'Cockpit test compilation failed.' }
    & .\src_rebuild\bin\tests\cockpit_model_tests.exe
    if ($LASTEXITCODE) { throw 'Cockpit geometry tests failed.' }
    & cmd.exe /d /c "`"$vcvars`" >nul && `"$cl`" /nologo /EHsc /std:c++14 /W4 tests\vr_math_tests.cpp /Fesrc_rebuild\bin\tests\vr_math_tests.exe /Fosrc_rebuild\bin\tests\vr_math_tests.obj"
    if ($LASTEXITCODE) { throw 'VR maths test compilation failed.' }
    & .\src_rebuild\bin\tests\vr_math_tests.exe
    if ($LASTEXITCODE) { throw 'VR maths tests failed.' }
    & cmd.exe /d /c "`"$vcvars`" >nul && `"$cl`" /nologo /EHsc /std:c++14 /DNTSC_VERSION /D_CRT_SECURE_NO_WARNINGS /Isrc_rebuild\Game /Isrc_rebuild\PsyCross\include /Isrc_rebuild\PsyCross\include\psx tests\wheel_game_tests.cpp /Fesrc_rebuild\bin\tests\wheel_game_tests.exe /Fosrc_rebuild\bin\tests\wheel_game_tests.obj"
    if ($LASTEXITCODE) { throw 'Wheel game test compilation failed.' }
    & .\src_rebuild\bin\tests\wheel_game_tests.exe
    if ($LASTEXITCODE) { throw 'Wheel game integration tests failed.' }
    & cmd.exe /d /c "`"$vcvars`" >nul && `"$cl`" /nologo /EHsc /std:c++14 /TP /D_CRT_SECURE_NO_WARNINGS /Isrc_rebuild\dependencies\SDL2-2.30.2\include tests\wheel_runtime_tests.cpp src_rebuild\utils\wheel.cpp src_rebuild\utils\wheel_ui.cpp src_rebuild\utils\ini.c /Fesrc_rebuild\bin\tests\wheel_runtime_tests.exe /Fosrc_rebuild\bin\tests\ /link src_rebuild\dependencies\SDL2-2.30.2\lib\x64\SDL2.lib"
    if ($LASTEXITCODE) { throw 'Wheel SDL test compilation failed.' }
    Copy-Item src_rebuild/dependencies/SDL2-2.30.2/lib/x64/SDL2.dll src_rebuild/bin/tests/
    & .\src_rebuild\bin\tests\wheel_runtime_tests.exe
    if ($LASTEXITCODE) { throw 'Wheel SDL tests failed.' }
    if ($TestsOnly) { return }
    # Keep the upstream submodule reference; apply the small, tracked compatibility patches
    # (wheel input, then OpenXR stereo rendering).
    foreach ($patch in @('psycross-wheel.patch', 'psycross-vr.patch')) {
        git -C src_rebuild/PsyCross apply --reverse --check ../../patches/$patch 2>$null
        if ($LASTEXITCODE) {
            git -C src_rebuild/PsyCross apply --check ../../patches/$patch
            if ($LASTEXITCODE) { throw "PsyCross has conflicting changes; inspect patches/$patch." }
            git -C src_rebuild/PsyCross apply ../../patches/$patch
            if ($LASTEXITCODE) { throw "Cannot apply PsyCross patch $patch." }
        }
    }
    $env:SDL2_DIR = 'dependencies/SDL2-2.30.2'
    $env:OPENAL_DIR = 'dependencies/openal-soft-1.23.1-bin'
    $env:JPEG_DIR = 'dependencies/jpeg-9d'
    if (!(Test-Path src_rebuild/dependencies/jpeg-9d/jconfig.h)) {
        Copy-Item src_rebuild/dependencies/jpeg-9d/jconfig.vc src_rebuild/dependencies/jpeg-9d/jconfig.h
    }
    Push-Location src_rebuild
    try {
        & .\premake5.exe vs2022
        if ($LASTEXITCODE) { throw 'Premake failed.' }
        & "$installation\MSBuild\Current\Bin\MSBuild.exe" build\REDRIVER2.sln /m /p:Configuration=$Configuration /p:Platform=Win32 /verbosity:minimal /nologo
        if ($LASTEXITCODE) { throw 'REDRIVER2 compilation failed.' }
    } finally { Pop-Location }
    Copy-Item src_rebuild/dependencies/SDL2-2.30.2/lib/x86/SDL2.dll "src_rebuild/bin/$Configuration/"
    Copy-Item src_rebuild/dependencies/openal-soft-1.23.1-bin/bin/Win32/soft_oal.dll "src_rebuild/bin/$Configuration/OpenAL32.dll"
    $executable = if ($Configuration -eq 'Debug') { 'REDRIVER2_dbg.exe' } elseif ($Configuration -eq 'Release_dev') { 'REDRIVER2_dev.exe' } else { 'REDRIVER2.exe' }
    Copy-Item "src_rebuild/bin/$Configuration/$executable" "src_rebuild/bin/$Configuration/WheelConfig.exe"
} finally { Pop-Location }
