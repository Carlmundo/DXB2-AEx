# Run the embedded checker with fake HTTP/process/prompt/installer boundaries.
# No network request, desktop dialog, process termination, or installation occurs.
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$header = Get-Content -LiteralPath (Join-Path $project 'native\aex-update-check-script.hpp') -Raw
$source = [regex]::Match($header, '(?s)LR"AEXCHECK\((.*?)\)AEXCHECK"').Groups[1].Value
if (!$source) { throw 'Cannot find embedded checker.' }
$checker = [scriptblock]::Create($source)
$installerHeader = Get-Content -LiteralPath (Join-Path $project 'native\aex-update-script.hpp') -Raw
$installer = [regex]::Match($installerHeader, '(?s)LR"AEX\((.*?)\)AEX"').Groups[1].Value
$combined = "& {`n" + $source + "`n} -GamePath 'C:\Game''s\DXBall2.exe' -GameId 4242 -InstalledVersion '1.0.3' -SignalName 'Local\AEx-test' -InstallUpdate {`n" + $installer + "`n}"
$tokens = $null
$parseErrors = $null
$null = [Management.Automation.Language.Parser]::ParseInput($combined, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'The combined native checker/installer command is not valid PowerShell.' }
if ($source -match 'MessageBox|MainWindowTitle|MainWindowHandle') { throw 'The helper must never display a dialog or infer menu readiness from a window.' }
Write-Output 'PASS: combined checker/installer syntax; helper contains no dialog/window lookup'
function Check($ok, $message) { if (!$ok) { throw $message } }
function Invoke-WebRequest {
    param($Uri, [switch]$UseBasicParsing, $TimeoutSec)
    $script:requests++
    Check ($Uri -eq 'https://raw.githubusercontent.com/Carlmundo/DXB2-AEx/refs/heads/main/package.json') 'Wrong version URL.'
    Check ($TimeoutSec -eq 15 -and $UseBasicParsing) 'Missing timeout or legacy PowerShell support.'
    if ($script:scenario -eq 'offline') { throw 'Simulated network failure.' }
    return [pscustomobject]@{ Content = $script:json }
}
function Get-Process {
    param($Id, $ErrorAction)
    Check ($Id -eq 4242) 'Checker inspected the wrong process.'
    if ($script:scenario -eq 'game-closed') { return $null }
    $path = $script:game
    if ($script:scenario -eq 'wrong-process') { $path = 'C:\Other\DXBall2.exe' }
    $process = [pscustomobject]@{ Path = $path; HasExited = ($script:scenario -eq 'game-exited'); Handle = 0 }
    $process | Add-Member ScriptMethod Refresh {
        $script:refreshes++
        Check ($script:ready.WaitOne(0) -and $script:installs -eq 0) 'Installer ran before the game allowed its prompt/decision.'
        if ($script:scenario -eq 'exit-while-pending') { $this.HasExited = $true }
        elseif ($script:refreshes -eq 3) {
            if ($script:scenario -eq 'deferred-yes') { $null = $script:yes.Set() }
            else { $null = $script:no.Set() }
        }
    }
    $process | Add-Member ScriptMethod Dispose { $script:disposed++ }
    return $process
}
$install = {
    param($GamePath, $GameId)
    Check ($GamePath -eq $script:game -and $GameId -eq 4242) 'Installer target changed.'
    Check ($script:ready.WaitOne(0) -and $script:yes.WaitOne(0)) 'Installer did not wait for the game Yes signal.'
    $script:installs++
}
$cases = @(
    @('newer-yes', '1.0.3', '1.0.4', 6, 1, 1),
    @('deferred-yes', '1.0.3', '1.0.4', 6, 1, 1),
    @('deferred-no', '1.0.3', '1.0.4', 7, 1, 0),
    @('exit-while-pending', '1.0.3', '1.0.4', 0, 1, 0),
    @('newer-no', '1.0.3', '1.0.4', 7, 1, 0),
    @('prompt-closed', '1.0.3', '1.0.4', 0, 1, 0),
    @('same', '1.0.3', '1.0.3', 6, 0, 0),
    @('older', '1.0.3', '1.0.2', 6, 0, 0),
    @('numeric-patch', '1.0.9', '1.0.10', 6, 1, 1),
    @('numeric-minor', '1.9.99', '1.10.0', 6, 1, 1),
    @('major', '1.99.99', '2.0.0', 6, 1, 1),
    @('stable-over-prerelease', '1.0.4-beta.2', '1.0.4', 6, 1, 1),
    @('prerelease-below-stable', '1.0.4', '1.0.4-beta.2', 6, 0, 0),
    @('prerelease-numbers', '1.0.4-beta.2', '1.0.4-beta.10', 6, 1, 1),
    @('prerelease-length', '1.0.4-beta', '1.0.4-beta.1', 6, 1, 1),
    @('prerelease-numeric-first', '1.0.4-1', '1.0.4-alpha', 6, 1, 1),
    @('prerelease-ordinal', '1.0.4-beta', '1.0.4-BETA', 6, 0, 0),
    @('build-ignored', '1.0.4+old', '1.0.4+new', 6, 0, 0),
    @('large-number', '1.0.99999999999999999999', '1.0.100000000000000000000', 6, 1, 1),
    @('invalid-version', '1.0.3', 'invalid', 6, 0, 0),
    @('core-leading-zero', '1.0.3', '01.0.4', 6, 0, 0),
    @('prerelease-leading-zero', '1.0.3', '1.0.4-beta.01', 6, 0, 0),
    @('invalid-installed', 'invalid', '1.0.4', 6, 0, 0),
    @('invalid-json', '1.0.3', '1.0.4', 6, 0, 0),
    @('missing-version', '1.0.3', '1.0.4', 6, 0, 0),
    @('numeric-version', '1.0.3', '1.0.4', 6, 0, 0),
    @('offline', '1.0.3', '1.0.4', 6, 0, 0),
    @('game-closed', '1.0.3', '1.0.4', 6, 0, 0),
    @('game-exited', '1.0.3', '1.0.4', 6, 0, 0),
    @('wrong-process', '1.0.3', '1.0.4', 6, 0, 0)
)
foreach ($case in $cases) {
    $script:scenario = $case[0]
    $script:json = @{ version = $case[2] } | ConvertTo-Json -Compress
    if ($scenario -eq 'invalid-json') { $script:json = '{invalid' }
    if ($scenario -eq 'missing-version') { $script:json = '{}' }
    if ($scenario -eq 'numeric-version') { $script:json = '{"version": 123}' }
    $script:game = "C:\Game's & % !\DXBall2.exe"
    $script:refreshes = 0
    $script:requests = 0
    $script:installs = 0
    $script:disposed = 0
    $signalName = 'Local\AEx-check-test-' + [guid]::NewGuid().ToString('N')
    $script:ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $signalName + '.ready')
    $script:yes = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $signalName + '.install')
    $script:no = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $signalName + '.cancel')
    try {
        if ($scenario -notin @('deferred-yes', 'deferred-no', 'exit-while-pending')) {
            if ($case[3] -eq 6) { $null = $yes.Set() } else { $null = $no.Set() }
        }
        & $checker -GamePath $game -GameId 4242 -InstalledVersion $case[1] -SignalName $signalName -InstallUpdate $install
    Check ($requests -eq 1) "No metadata request for $scenario."
    Check ($ready.WaitOne(0) -eq [bool]$case[4]) "Wrong readiness signal for $scenario."
    Check ($installs -eq $case[5]) "Wrong install count for $scenario."
    if ($case[4]) { Check ($disposed -eq 1) 'Game process handle was not disposed.' }
    if ($scenario -in @('deferred-yes', 'deferred-no')) { Check ($refreshes -eq 3) 'Checker did not stay pending until the game decision.' }
    if ($scenario -eq 'exit-while-pending') { Check ($refreshes -eq 1) 'Checker did not stop when the game exited.' }
    Write-Output "PASS: $scenario"
    } finally { $ready.Dispose(); $yes.Dispose(); $no.Dispose() }
}
