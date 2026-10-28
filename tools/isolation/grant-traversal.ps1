# Provision directory-only read/traverse at a volume root for GetLongPathName.
# Run elevated once per volume containing the browser or tenant profiles.
# No inheritance: this permits listing root names, never reading descendants.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string[]]$Directory)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class RbTraversalAcl {
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool SetFileSecurityW(string path, uint info, byte[] sd);
    public static void Set(string path, byte[] descriptor) {
        // The low-level API updates only this directory. MAXIMUM_ALLOWED
        // handles can fail with sharing violations on in-use ancestors.
        if (!SetFileSecurityW(path, 4, descriptor)) throw new Win32Exception();
    }
}
'@
foreach ($item in $Directory) {
    $resolved = (Resolve-Path -LiteralPath $item).ProviderPath
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "Not a directory: $resolved"
    }
    $acl = Get-Acl -LiteralPath $resolved
    foreach ($sid in @('S-1-15-2-1', 'S-1-15-2-2')) {
        $identity = [Security.Principal.SecurityIdentifier]::new($sid)
        $rule = [Security.AccessControl.FileSystemAccessRule]::new($identity,
            'ReadAndExecute', 'None', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    [RbTraversalAcl]::Set($resolved, $acl.GetSecurityDescriptorBinaryForm())
    Write-Output "Directory-only package RX: $resolved"
}
