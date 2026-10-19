# Configure + build with the VS 2026 Build Tools (no Developer Prompt needed).
#   .\build.ps1            Release build
#   .\build.ps1 -Debug     Debug build
param([switch]$Debug)

$ErrorActionPreference = 'Stop'
$config = if ($Debug) { 'Debug' } else { 'Release' }
$buildDir = Join-Path $PSScriptRoot "build/$config"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC build tools not found' }

$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

# Import the vcvars environment into this PowerShell session.
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}
$env:PATH = "$cmakeBin\CMake\bin;$cmakeBin\Ninja;$env:PATH"

cmake -S $PSScriptRoot -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $buildDir
exit $LASTEXITCODE
