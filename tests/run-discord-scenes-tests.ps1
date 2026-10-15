param([ValidateSet('x86', 'x64')][string]$Architecture = 'x86')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ tools were not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$output = Join-Path $env:TEMP ('thprac-discord-scenes-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$source = Join-Path $PSScriptRoot 'discord_scenes_tests.cpp'
$executable = Join-Path $output 'discord_scenes_tests.exe'
$object = Join-Path $output 'discord_scenes_tests.obj'
$batch = Join-Path $output 'build.cmd'
$generator = Join-Path $repo 'thprac\loc_json.exe'
$generated = Join-Path $repo 'thprac\src\'
if (-not (Test-Path -LiteralPath $generator)) { throw 'Build thprac/loc_json.exe with loc_json_build_msvc.bat first.' }
& $generator $generated
if ($LASTEXITCODE -ne 0) { throw 'Locale generation failed.' }
@"
@echo off
call "$vcvars" $Architecture >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /W4 /O2 /MD "$source" /Fo"$object" /Fe"$executable"
if errorlevel 1 exit /b 1
"$executable"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Scene Discord snapshot tests failed: $LASTEXITCODE" }
Write-Host "Test executable: $executable"
