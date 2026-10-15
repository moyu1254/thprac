param([ValidateSet('x86', 'x64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ tools were not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$output = Join-Path $env:TEMP ('thprac-discord-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$batch = Join-Path $output 'build.cmd'
$jsonSource = Join-Path $repo 'thprac\lib\yyjson\yyjson.c'
$jsonInclude = Join-Path $repo 'thprac\lib\yyjson'
$testSource = Join-Path $PSScriptRoot 'discord_presence_tests.cpp'
$jsonObject = Join-Path $output 'yyjson.obj'
$testObject = Join-Path $output 'discord_presence_tests.obj'
$executable = Join-Path $output 'discord_presence_tests.exe'
@"
@echo off
call "$vcvars" $Architecture >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /c /TC "$jsonSource" /Fo"$jsonObject"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /W4 /O2 /MD /I"$jsonInclude" "$testSource" "$jsonObject" /Fo"$testObject" /Fe"$executable" /link advapi32.lib
if errorlevel 1 exit /b 1
"$executable"
if errorlevel 1 exit /b 1
"$executable" --shutdown
if errorlevel 1 exit /b 1
"$executable" --shutdown-during-clear
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Discord presence tests failed: $LASTEXITCODE" }
Write-Host "Test executable: $executable"
