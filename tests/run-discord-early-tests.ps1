param([ValidateSet('x86', 'x64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ tools were not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$output = Join-Path $env:TEMP ('thprac-early-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$batch = Join-Path $output 'build.cmd'
$source = Join-Path $repo 'thprac\src\thprac_presence_early.cpp'
$localeSource = Join-Path $repo 'thprac\src\thprac_locale_def.cpp'
if (-not (Test-Path -LiteralPath $localeSource)) { throw 'Generate thprac locale sources with loc_json before running capture tests.' }
$testSource = Join-Path $PSScriptRoot 'discord_early_tests.cpp'
$executable = Join-Path $output 'discord_early_tests.exe'
@"
@echo off
call "$vcvars" $Architecture >nul
if errorlevel 1 exit /b 1
cd /d "$output"
cl /nologo /std:c++20 /EHsc /utf-8 /W4 /O2 /MD /DNOMINMAX "$source" "$localeSource" "$testSource" /Fe"$executable"
if errorlevel 1 exit /b 1
"$executable"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Early capture tests failed: $LASTEXITCODE" }
Write-Host "Test executable: $executable"
