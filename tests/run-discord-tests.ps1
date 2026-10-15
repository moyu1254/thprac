$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $repo 'thprac\src'

# Check coverage against the same executable registry used by the injector.
# This guards against silently excluding a supported version when it is added.
$registry = Get-Content -LiteralPath (Join-Path $sourceDirectory 'thprac_identify.cpp') -Raw
$modules = Get-ChildItem -LiteralPath $sourceDirectory -Filter '*.cpp' | ForEach-Object {
    [pscustomobject]@{ Name = $_.Name; Content = Get-Content -LiteralPath $_.FullName -Raw }
}
$titles = [System.Collections.Generic.HashSet[string]]::new()
$entries = [regex]::Matches($registry, '\.initFunc\s*=\s*TH_INIT_FUNC_(32|64)\((\w+)\)')
if ($entries.Count -eq 0) { throw 'No executable entry points were found in the injector registry.' }
foreach ($entry in $entries) {
    $function = $entry.Groups[2].Value + 'Real'
    $module = @($modules | Where-Object { $_.Content -match ('void\s+' + $function + '\s*\(') })
    if ($module.Count -ne 1) { throw "Missing or ambiguous init function: $function" }
    $capture = [regex]::Match($module[0].Content, 'DiscordPresence::Initialize\("([^"]+)"')
    if (-not $capture.Success) { throw "Discord capture is not registered for $function" }
    [void]$titles.Add($capture.Groups[1].Value)
    $project = Get-Content -LiteralPath (Join-Path $repo ('thprac\thprac' + $entry.Groups[1].Value + '.vcxproj')) -Raw
    if (-not $project.Contains('src\' + $module[0].Name)) { throw "Module is not built: $($module[0].Name)" }
}
Write-Host "PASS capture registration for $($entries.Count) executable entries / $($titles.Count) titles"

$generator = Join-Path $repo 'thprac\loc_json.exe'
if (-not (Test-Path -LiteralPath $generator)) { throw 'Build thprac/loc_json.exe with loc_json_build_msvc.bat first.' }
& $generator ($sourceDirectory + '\')
if ($LASTEXITCODE -ne 0) { throw 'Locale generation failed.' }
foreach ($architecture in @('x86', 'x64')) {
    & (Join-Path $PSScriptRoot 'run-discord-presence-tests.ps1') -Architecture $architecture
    & (Join-Path $PSScriptRoot 'run-discord-early-tests.ps1') -Architecture $architecture
}
& (Join-Path $PSScriptRoot 'run-discord-late-tests.ps1') -Architecture x86
& (Join-Path $PSScriptRoot 'run-discord-scenes-tests.ps1') -Architecture x86
