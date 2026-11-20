#pragma once

namespace aex_update {
// Embedded in version.dll. Neither the URL nor any command text comes from Lua.
inline constexpr wchar_t kUpdateScript[]=LR"AEX(
param([string]$GamePath, [int]$GameId)
$ErrorActionPreference = 'Stop'
$dir = [IO.Path]::GetDirectoryName($GamePath)
$stage = $null
$stopped = $false
$rollbackFailed = $false
$installed = [Collections.Generic.List[object]]::new()
try {
    $running = Get-Process -Id $GameId -ErrorAction SilentlyContinue
    if ($running) {
        if ($running.Path -ne $GamePath) { throw 'The calling game process does not match its executable.' }
        $null = $running.Handle
    }
    $stage = Join-Path $dir ('.aex-update-' + [guid]::NewGuid().ToString('N'))
    $payload = Join-Path $stage 'payload'
    $backup = Join-Path $stage 'backup'
    $null = New-Item -ItemType Directory -Path $payload, $backup
    $zip = Join-Path $dir 'aex.zip'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/Carlmundo/DXB2-AEx/releases/latest/download/aex.zip' -OutFile $zip
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $root = [IO.Path]::GetFullPath($payload) + [IO.Path]::DirectorySeparatorChar
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($entry in $archive.Entries) {
            $target = [IO.Path]::GetFullPath((Join-Path $payload $entry.FullName))
            if (!$target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'The update ZIP contains a path outside its destination.'
            }
        }
    } finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $payload)
    if (!(Test-Path -LiteralPath (Join-Path $payload 'version.dll') -PathType Leaf)) {
        throw 'The update ZIP does not contain version.dll.'
    }
    $files = @(Get-ChildItem -LiteralPath $payload -File -Recurse -Force | ForEach-Object {
        $relative = $_.FullName.Substring($root.Length)
        $dest = Join-Path $dir $relative
        # Refuse existing junctions/symlinks in the destination path as well.
        $ancestor = $dest
        while ($ancestor -ne $dir) {
            if (Test-Path -LiteralPath $ancestor) {
                if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'The update destination contains a junction or symbolic link.'
                }
            }
            $ancestor = [IO.Path]::GetDirectoryName($ancestor)
        }
        $saved = Join-Path $backup $relative
        $exists = Test-Path -LiteralPath $dest -PathType Leaf
        if ($exists) {
            $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($saved)) -Force
            Copy-Item -LiteralPath $dest -Destination $saved
        }
        [pscustomobject]@{ Source = $_.FullName; Destination = $dest; Backup = $saved; Existed = $exists }
    })
    if ($running -and !$running.HasExited) {
        Stop-Process -InputObject $running -Force
        $running.WaitForExit()
        $stopped = $true
    }
    foreach ($file in $files) {
        $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($file.Destination)) -Force
        $installed.Add($file)
        Copy-Item -LiteralPath $file.Source -Destination $file.Destination -Force
    }
    # Do not reuse a numeric PID or enumerate/close any other game or editor.
    Set-Content -LiteralPath (Join-Path $dir 'aex-update.log') -Value 'AEx installed; restarting game.'
    Start-Process -FilePath $GamePath -WorkingDirectory $dir
    $stopped = $false
} catch {
    $failure = $_.Exception.Message
    $rollbackFailed = $false
    for ($i = $installed.Count - 1; $i -ge 0; --$i) {
        $file = $installed[$i]
        try {
            if ($file.Existed) {
                # A locked destination may have rejected the copy unchanged.
                # Avoid declaring rollback failed when it already matches.
                if (!(Test-Path -LiteralPath $file.Destination -PathType Leaf) -or
                    (Get-FileHash -LiteralPath $file.Backup).Hash -ne (Get-FileHash -LiteralPath $file.Destination).Hash) {
                    Copy-Item -LiteralPath $file.Backup -Destination $file.Destination -Force
                }
            }
            elseif (Test-Path -LiteralPath $file.Destination) { Remove-Item -LiteralPath $file.Destination -Force }
        } catch { $rollbackFailed = $true }
    }
    if ($stopped -and !$rollbackFailed) {
        try { Start-Process -FilePath $GamePath -WorkingDirectory $dir } catch { $failure += ' Restart failed: ' + $_.Exception.Message }
    }
    if ($rollbackFailed) { $failure += ' Rollback incomplete; backup retained in ' + $stage }
    Set-Content -LiteralPath (Join-Path $dir 'aex-update.log') -Value $failure
    throw $failure
} finally {
    if ($running) { $running.Dispose() }
    # Only remove the generated direct child of the game directory.
    if ($stage -and !$rollbackFailed -and [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stage)) -eq $dir -and
        [IO.Path]::GetFileName($stage).StartsWith('.aex-update-')) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
)AEX";
} // namespace aex_update
