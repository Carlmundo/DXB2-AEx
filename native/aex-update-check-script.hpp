#pragma once

namespace aex_update {
inline constexpr wchar_t kUpdateCheckScript[]=LR"AEXCHECK(
param([string]$GamePath, [int]$GameId, [string]$InstalledVersion, [string]$SignalName, [scriptblock]$InstallUpdate)
$ErrorActionPreference = 'Stop'
function Read-AexVersion($value) {
    if ($value -isnot [string] -or $value -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$') {
        throw 'Invalid AEx version.'
    }
    $core = @($Matches[1], $Matches[2], $Matches[3])
    $pre = @()
    if ($Matches[4]) { $pre = @($Matches[4].Split('.')) }
    foreach ($id in $pre) {
        if ($id -cmatch '^[0-9]+$' -and $id.Length -gt 1 -and $id.StartsWith('0')) {
            throw 'Invalid AEx prerelease version.'
        }
    }
    return [pscustomobject]@{ Core = $core; Pre = $pre }
}
function Compare-AexNumber([string]$left, [string]$right) {
    if ($left.Length -ne $right.Length) { return [Math]::Sign($left.Length - $right.Length) }
    return [Math]::Sign([string]::CompareOrdinal($left, $right))
}
function Compare-AexVersion($left, $right) {
    $a = Read-AexVersion $left
    $b = Read-AexVersion $right
    for ($i = 0; $i -lt 3; ++$i) {
        $comparison = Compare-AexNumber $a.Core[$i] $b.Core[$i]
        if ($comparison -ne 0) { return $comparison }
    }
    if (!$a.Pre.Count -and !$b.Pre.Count) { return 0 }
    if (!$a.Pre.Count) { return 1 }
    if (!$b.Pre.Count) { return -1 }
    for ($i = 0; $i -lt [Math]::Min($a.Pre.Count, $b.Pre.Count); ++$i) {
        $an = $a.Pre[$i] -cmatch '^[0-9]+$'
        $bn = $b.Pre[$i] -cmatch '^[0-9]+$'
        if ($an -and $bn) { $comparison = Compare-AexNumber $a.Pre[$i] $b.Pre[$i] }
        elseif ($an -ne $bn) { if ($an) { return -1 } else { return 1 } }
        else { $comparison = [Math]::Sign([string]::CompareOrdinal($a.Pre[$i], $b.Pre[$i])) }
        if ($comparison -ne 0) { return $comparison }
    }
    return [Math]::Sign($a.Pre.Count - $b.Pre.Count)
}
$running = $ready = $install = $cancel = $null
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $response = Invoke-WebRequest -UseBasicParsing -TimeoutSec 15 -Uri 'https://raw.githubusercontent.com/Carlmundo/DXB2-AEx/refs/heads/main/package.json'
    $metadata = ConvertFrom-Json -InputObject $response.Content
    if ((Compare-AexVersion $metadata.version $InstalledVersion) -le 0) { return }
    $running = Get-Process -Id $GameId -ErrorAction SilentlyContinue
    if (!$running -or $running.HasExited -or $running.Path -ne $GamePath) { return }
    $null = $running.Handle
    $ready = [Threading.EventWaitHandle]::OpenExisting($SignalName + '.ready')
    $install = [Threading.EventWaitHandle]::OpenExisting($SignalName + '.install')
    $cancel = [Threading.EventWaitHandle]::OpenExisting($SignalName + '.cancel')
    # Notify only. The game's main-menu update owns the dialog and its timing.
    $null = $ready.Set()
    while (!$running.HasExited) {
        $decision = [Threading.WaitHandle]::WaitAny(@($install, $cancel), 250)
        if ($decision -eq 0) { & $InstallUpdate -GamePath $GamePath -GameId $GameId; return }
        if ($decision -eq 1) { return }
        $running.Refresh()
    }
} catch {
    # A metadata/network failure must not interrupt game startup. The shared
    # installer records its own errors in aex-update.log if installation began.
} finally {
    foreach ($signal in @($ready, $install, $cancel)) { if ($signal) { $signal.Dispose() } }
    if ($running) { $running.Dispose() }
}
)AEXCHECK";
} // namespace aex_update
