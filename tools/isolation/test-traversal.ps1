# Regression: busy ancestors must permit ACL updates without touching children.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class RbTraversalTest {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr CreateFileW(string p, uint a, uint s,
        IntPtr sa, uint c, uint f, IntPtr t);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool SetFileSecurityW(string p, uint info, byte[] sd);
}
'@
$root = Join-Path ([IO.Path]::GetTempPath()) ('rk-traversal-' + [guid]::NewGuid())
$child = Join-Path $root 'child'
$lock = [IntPtr]::Zero
try {
    New-Item -ItemType Directory $child -Force | Out-Null
    $before = (Get-Acl -LiteralPath $child).Sddl
    # Stage an inheritable ACE without propagating it. A later high-level
    # SetSecurityInfo call would push this pre-existing ACE to the child.
    $acl = Get-Acl -LiteralPath $root
    $sid = [Security.Principal.SecurityIdentifier]::new('S-1-5-21-1-2-3-1234')
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $sid, 'ReadAndExecute', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    if (![RbTraversalTest]::SetFileSecurityW($root, 4, $acl.GetSecurityDescriptorBinaryForm())) {
        throw [ComponentModel.Win32Exception]::new()
    }
    # Model a process holding the directory without FILE_SHARE_DELETE.
    $lock = [RbTraversalTest]::CreateFileW($root, 1, 3, [IntPtr]::Zero, 3, 0x02000000, [IntPtr]::Zero)
    if ($lock -eq [IntPtr](-1)) { throw [ComponentModel.Win32Exception]::new() }
    $old = [RbTraversalTest]::CreateFileW($root, 0x02000000, 7, [IntPtr]::Zero, 3, 0x02000000, [IntPtr]::Zero)
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if ($old -ne [IntPtr](-1)) {
        [RbTraversalTest]::CloseHandle($old) | Out-Null
        throw 'Expected MAXIMUM_ALLOWED to fail on the busy directory'
    }
    if ($errorCode -ne 32) { throw "Expected sharing violation, got $errorCode" }
    & "$PSScriptRoot/grant-traversal.ps1" -Directory $root
    $after = (Get-Acl -LiteralPath $root).Sddl
    foreach ($package in @('S-1-15-2-1', 'S-1-15-2-2')) {
        $rules = (Get-Acl -LiteralPath $root).GetAccessRules(
            $true, $false, [Security.Principal.SecurityIdentifier])
        if (!($rules | Where-Object {
            $_.IdentityReference.Value -eq $package -and
            $_.InheritanceFlags -eq 'None' -and
            $_.AccessControlType -eq 'Allow' -and
            ($_.FileSystemRights -band 0x200a9) -eq 0x200a9
        })) { throw "Missing directory-only RX grant for $package" }
    }
    & "$PSScriptRoot/grant-traversal.ps1" -Directory $root
    if ((Get-Acl -LiteralPath $root).Sddl -ne $after) { throw 'Repeated update changed DACL' }
    if ((Get-Acl -LiteralPath $child).Sddl -ne $before) { throw 'Child DACL changed' }
    Write-Output 'PASS: sharing conflict reproduced; update succeeds; child unchanged; repeat is stable'
} finally {
    if ($lock -ne [IntPtr]::Zero -and $lock -ne [IntPtr](-1)) {
        [RbTraversalTest]::CloseHandle($lock) | Out-Null
    }
    # Only remove the unique temporary tree created above.
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath())) -and
        (Split-Path $root -Leaf).StartsWith('rk-traversal-')) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}
