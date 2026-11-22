param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WorkshopDescriptionMemory {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process,IntPtr address,byte[] buffer,UIntPtr length,out UIntPtr read);
}
'@
$GameRoot=(Resolve-Path -LiteralPath $GameRoot).Path
$workshop=Start-Process -FilePath (Join-Path $GameRoot 'Tools\DXB2Workshop.exe') -WorkingDirectory $GameRoot -WindowStyle Hidden -PassThru
try {
  Start-Sleep -Seconds 2
  $workshop.Refresh()
  if($workshop.HasExited){throw "Workshop exited during startup: $($workshop.ExitCode)"}
  $proxy=@($workshop.Modules | Where-Object { $_.FileName -ieq (Join-Path $GameRoot 'Tools\steam_api.dll') })
  $original=@($workshop.Modules | Where-Object { $_.FileName -ieq (Join-Path $GameRoot 'steam_api.dll') })
  if($proxy.Count -ne 1 -or $original.Count -ne 1){throw 'Workshop did not load both proxy and original Steam DLL'}
  $at=$workshop.MainModule.BaseAddress.ToInt64()+0x11305
  $bytes=New-Object byte[] 37
  $read=[UIntPtr]::Zero
  if(-not [WorkshopDescriptionMemory]::ReadProcessMemory($workshop.Handle,[IntPtr]$at,$bytes,[UIntPtr]37,[ref]$read) -or $read.ToUInt64() -ne 37){throw 'Cannot read live description hook'}
  if($bytes[0] -ne 0x57 -or $bytes[1] -ne 0xe8){throw 'Workshop description hook is missing'}
  for($i=6;$i -lt $bytes.Length;$i++){if($bytes[$i] -ne 0x90){throw 'Incomplete Workshop description hook'}}
  $destination=$at+6+[BitConverter]::ToInt32($bytes,2)
  $start=$proxy[0].BaseAddress.ToInt64()
  if($destination -lt $start -or $destination -ge $start+$proxy[0].ModuleMemorySize){throw 'Description hook does not point into the Tools DLL'}
  Write-Output 'PASS: Workshop starts, forwards original Steam DLL, and installs the live description hook; no upload performed'
} finally {
  if(-not $workshop.HasExited){$workshop.Kill();$workshop.WaitForExit()}
  $workshop.Dispose()
}
