# Exercise the exact script embedded in the DLL, with download/process commands
# replaced locally. ZIP validation, extraction, installation and rollback are real
# filesystem operations within a disposable fixture; no network or game launch.
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$header = Get-Content -LiteralPath (Join-Path $project 'native\aex-update-script.hpp') -Raw
$source = [regex]::Match($header, '(?s)LR"AEX\((.*?)\)AEX"').Groups[1].Value
if (!$source) { throw 'Cannot find embedded update script.' }
$update = [scriptblock]::Create($source)
$fixtureRoot = Join-Path $project ('.scratch\aex-update-tests-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixtureRoot
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function Check($ok, $message) { if (!$ok) { throw $message } }
function Invoke-WebRequest {
    param($Uri, $OutFile, [switch]$UseBasicParsing)
    Check ($Uri -eq 'https://github.com/Carlmundo/DXB2-AEx/releases/latest/download/aex.zip') 'Download URL changed.'
    $script:events.Add('download')
    if ($script:mode -eq 'download-failure') { throw 'Simulated download failure.' }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $script:fixtureZip -Destination $OutFile
}
function Get-Process {
    param([int]$Id, $ErrorAction)
    Check ($Id -eq 4242) 'Updater targeted another PID.'
    $script:events.Add('lookup')
    if ($script:mode -eq 'exited') { return $null }
    return $script:fakeProcess
}
function Stop-Process {
    param($InputObject, [switch]$Force)
    Check ([object]::ReferenceEquals($InputObject, $script:fakeProcess)) 'Updater stopped another process.'
    $script:events.Add('stop')
}
function Start-Process {
    param($FilePath, $WorkingDirectory)
    Check ($FilePath -eq $script:game -and $WorkingDirectory -eq $script:caseDir) 'Restart path or working directory changed.'
    $script:events.Add('start')
    if ($script:mode -eq 'restart-failure' -and !$script:failedOnce) {
        $script:failedOnce = $true
        throw 'Simulated restart failure.'
    }
}
function Copy-Item {
    param($LiteralPath, $Destination, [switch]$Force)
    if ($script:mode -eq 'copy-failure' -and !$script:failedOnce -and $LiteralPath -like '*\payload\version.dll') {
        $script:failedOnce = $true
        throw 'Simulated locked destination.'
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
}
try {
    foreach ($mode in @('success', 'exited', 'download-failure', 'invalid-zip', 'traversal', 'missing-dll', 'wrong-process', 'copy-failure', 'restart-failure', 'locked-editor')) {
        $script:mode = $mode
        $script:caseDir = Join-Path $fixtureRoot ($mode + " Game's & % !")
        $null = New-Item -ItemType Directory -Path $caseDir
        $script:game = Join-Path $caseDir 'DXBall2.exe'
        Set-Content -LiteralPath $game -Value 'fixture only; never executed'
        Set-Content -LiteralPath (Join-Path $caseDir 'aex.cfg') -Value 'old-config' -NoNewline
        Set-Content -LiteralPath (Join-Path $caseDir 'version.dll') -Value 'old-dll' -NoNewline
        $script:events = [Collections.Generic.List[string]]::new()
        $script:failedOnce = $false
        $script:fakeProcess = [pscustomobject]@{ Path = $game; Handle = 0; HasExited = $false }
        if ($mode -eq 'wrong-process') { $fakeProcess.Path = Join-Path $caseDir 'another.exe' }
        $fakeProcess | Add-Member ScriptMethod WaitForExit { $script:events.Add('wait') }
        $fakeProcess | Add-Member ScriptMethod Dispose { $script:events.Add('dispose') }
        $script:fixtureZip = Join-Path $caseDir 'fixture.zip'
        if ($mode -eq 'invalid-zip') { Set-Content -LiteralPath $fixtureZip -Value 'invalid ZIP' }
        else {
            $archive = [IO.Compression.ZipFile]::Open($fixtureZip, [IO.Compression.ZipArchiveMode]::Create)
            try {
                $entries = @{ 'aex.cfg' = 'new-config'; 'Tools/steam_api.dll' = 'new-editor-dll' }
                if ($mode -ne 'missing-dll') { $entries['version.dll'] = 'new-dll' }
                if ($mode -eq 'traversal') { $entries['../escaped.txt'] = 'must never escape' }
                foreach ($name in $entries.Keys) {
                    $writer = [IO.StreamWriter]::new($archive.CreateEntry($name).Open())
                    try { $writer.Write($entries[$name]) } finally { $writer.Dispose() }
                }
            } finally { $archive.Dispose() }
        }
        $locked = $null
        if ($mode -eq 'locked-editor') {
            $null = New-Item -ItemType Directory -Path (Join-Path $caseDir 'Tools')
            $editor = Join-Path $caseDir 'Tools\steam_api.dll'
            Set-Content -LiteralPath $editor -Value 'old-editor' -NoNewline
            $locked = [IO.File]::Open($editor, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        }
        $failed = $false
        try { & $update -GamePath $game -GameId 4242 } catch { $failed = $true }
        finally { if ($locked) { $locked.Dispose() } }
        $success = $mode -in @('success', 'exited')
        Check ($failed -ne $success) "Unexpected outcome for $mode."
        if ($success) {
            Check ((Get-Content -LiteralPath (Join-Path $caseDir 'version.dll') -Raw) -eq 'new-dll') 'DLL not installed.'
            Check ((Get-Content -LiteralPath (Join-Path $caseDir 'aex.cfg') -Raw) -eq 'new-config') 'Config not overwritten.'
            Check ((Get-Content -LiteralPath (Join-Path $caseDir 'Tools\steam_api.dll') -Raw) -eq 'new-editor-dll') 'Nested file not installed.'
            Check (@($events | Where-Object { $_ -eq 'start' }).Count -eq 1) 'Game was not restarted exactly once.'
            if ($mode -eq 'success') {
                Check ($events.IndexOf('download') -lt $events.IndexOf('stop')) 'Game closed before download.'
                Check ($events.IndexOf('wait') -lt $events.IndexOf('start')) 'Restart did not wait for old game.'
            } else { Check (!$events.Contains('stop')) 'Already-exited game was stopped.' }
        } else {
            Check ((Get-Content -LiteralPath (Join-Path $caseDir 'version.dll') -Raw) -eq 'old-dll') "DLL was damaged in $mode."
            Check ((Get-Content -LiteralPath (Join-Path $caseDir 'aex.cfg') -Raw) -eq 'old-config') "Config was damaged in $mode."
            Check (Test-Path -LiteralPath (Join-Path $caseDir 'aex-update.log')) 'Failure was not logged.'
            if ($mode -in @('copy-failure', 'restart-failure', 'locked-editor')) {
                Check ($events.Contains('stop') -and $events.Contains('start')) 'Stopped game not recovered after failure.'
                if ($mode -eq 'locked-editor') {
                    Check ((Get-Content -LiteralPath $editor -Raw) -eq 'old-editor') 'Locked editor file was modified.'
                } else {
                    Check (!(Test-Path -LiteralPath (Join-Path $caseDir 'Tools\steam_api.dll'))) 'Rollback left a newly installed file.'
                }
            } else {
                Check (!$events.Contains('stop') -and !$events.Contains('start')) 'Pre-install failure stopped or restarted the game.'
            }
        }
        Check (!(Test-Path -LiteralPath (Join-Path $caseDir 'escaped.txt'))) 'ZIP path traversal escaped staging.'
        Check (@(Get-ChildItem -LiteralPath $caseDir -Directory -Force | Where-Object Name -Like '.aex-update-*').Count -eq 0) 'Staging directory not cleaned.'
        Write-Output "PASS: $mode"
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $project '.scratch'))
    if ([IO.Path]::GetDirectoryName($resolved) -eq $expectedParent -and [IO.Path]::GetFileName($resolved).StartsWith('aex-update-tests-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
