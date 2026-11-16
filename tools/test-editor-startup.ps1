param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EditorScriptMemory {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process,IntPtr address,byte[] buffer,UIntPtr length,out UIntPtr read);
}
'@
foreach($name in @('DXB2Editor.exe','DXB2Editor2.exe')) {
  $editor=Start-Process -FilePath (Join-Path $GameRoot ('Tools\'+$name)) -WorkingDirectory $GameRoot -WindowStyle Hidden -PassThru
  try {
    Start-Sleep -Seconds 3
    $editor.Refresh()
    if($editor.HasExited){throw "$name exited during startup: $($editor.ExitCode)"}
    $proxy=@($editor.Modules | Where-Object { $_.FileName -ieq (Join-Path $GameRoot 'Tools\steam_api.dll') })
    $original=@($editor.Modules | Where-Object { $_.FileName -ieq (Join-Path $GameRoot 'steam_api.dll') })
    if($proxy.Count -ne 1 -or $original.Count -ne 1){throw "$name did not load both the proxy and original Steam DLL"}
    $at=$editor.MainModule.BaseAddress.ToInt64()+0x32f20
    $bytes=New-Object byte[] 5
    $read=[UIntPtr]::Zero
    if(-not [EditorScriptMemory]::ReadProcessMemory($editor.Handle,[IntPtr]$at,$bytes,[UIntPtr]5,[ref]$read) -or $read.ToUInt64() -ne 5 -or $bytes[0] -ne 0xe9){throw "$name script hook is missing"}
    $destination=$at+5+[BitConverter]::ToInt32($bytes,1)
    $start=$proxy[0].BaseAddress.ToInt64()
    if($destination -lt $start -or $destination -ge $start+$proxy[0].ModuleMemorySize){throw "$name script hook does not point into the editor DLL"}
    $branch=New-Object byte[] 2
    if(-not [EditorScriptMemory]::ReadProcessMemory($editor.Handle,[IntPtr]($editor.MainModule.BaseAddress.ToInt64()+0x2c312),$branch,[UIntPtr]2,[ref]$read) -or $read.ToUInt64() -ne 2 -or $branch[0] -ne 0xeb -or $branch[1] -ne 0x25){throw "$name bundled-board editing patch is missing"}
    Write-Output "PASS: $name startup; original Steam DLL forwarded; live script hook and bundled-board editing verified"
  } finally {
    if(-not $editor.HasExited){$editor.Kill();$editor.WaitForExit()}
    $editor.Dispose()
  }
}
