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
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CreateFile(string name, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("advapi32.dll", SetLastError=true)]
    static extern bool GetSecurityDescriptorDacl(IntPtr sd, out bool present,
        out IntPtr dacl, out bool defaulted);
    [DllImport("advapi32.dll")]
    static extern uint SetSecurityInfo(IntPtr h, int type, uint info,
        IntPtr owner, IntPtr group, IntPtr dacl, IntPtr sacl);
    public static void Set(string path, byte[] descriptor) {
        // MAXIMUM_ALLOWED explicitly suppresses recursive ACE propagation.
        IntPtr h = CreateFile(path, 0x02000000, 7, IntPtr.Zero, 3,
            0x02000000, IntPtr.Zero);
        if (h == new IntPtr(-1)) throw new Win32Exception();
        var pinned = GCHandle.Alloc(descriptor, GCHandleType.Pinned);
        try {
            bool present, defaulted; IntPtr dacl;
            if (!GetSecurityDescriptorDacl(pinned.AddrOfPinnedObject(),
                out present, out dacl, out defaulted) || !present)
                throw new Win32Exception();
            uint error = SetSecurityInfo(h, 1, 4, IntPtr.Zero, IntPtr.Zero,
                dacl, IntPtr.Zero);
            if (error != 0) throw new Win32Exception((int)error);
        } finally { pinned.Free(); CloseHandle(h); }
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
