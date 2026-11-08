Add-Type -Namespace W -Name U -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
"@
$p = Get-Process Reader -ErrorAction Stop
$h = $p.MainWindowHandle
"handle=$h iconic=$(  [W.U]::IsIconic($h) )"
if ([W.U]::IsIconic($h)) { [void][W.U]::ShowWindow($h, 9) }
Start-Sleep -Milliseconds 300
"fg=$([W.U]::SetForegroundWindow($h))"
