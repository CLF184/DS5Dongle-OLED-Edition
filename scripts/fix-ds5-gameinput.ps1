# ============================================================
# fix-ds5-gameinput.ps1
# Fix Windows GameInput axis parsing for DualSense (054C:0CE6)
# ------------------------------------------------------------
# Sony report layout: bytes 1-6 = LX,LY,RX,RY,L2,R2 (axis 0-5).
# Windows generic template misreads them, causing the "ghost
# controller" (WGI RightTrigger=0.5 / RightThumbstickY=1.0).
# This writes the correct GameInput custom mapping.
# Usage: Run as Administrator in PowerShell, then rerun
#        ds5_capture.py to verify (expect WGI all zeros).
# Restore: delete the 054C0CE600010005 registry key below.
# ============================================================

$ErrorActionPreference = 'Stop'

# Admin check
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Need Administrator! Right-click PowerShell and Run as Administrator." -ForegroundColor Red
    exit 1
}

$base = 'HKLM:\SYSTEM\CurrentControlSet\Control\GameInput\Devices\054C0CE600010005'
$gp   = "$base\Gamepad"

Write-Host "== Writing DualSense (054C:0CE6) GameInput mapping =="

# Clean any existing mapping
if (Test-Path $base) {
    Remove-Item -Path $base -Recurse -Force
    Write-Host "  old mapping cleaned"
}
New-Item -Path $gp -Force | Out-Null

function Set-Axis([string]$name, [int]$index, [bool]$invert) {
    $k = "$gp\$name"
    New-Item -Path $k -Force | Out-Null
    New-ItemProperty -Path $k -Name 'AxisIndex' -Value $index -PropertyType DWord | Out-Null
    if ($invert) {
        New-ItemProperty -Path $k -Name 'Invert' -Value 1 -PropertyType DWord | Out-Null
    }
    $inv = ''
    if ($invert) { $inv = ' [invert]' }
    Write-Host "  $name -> axis $index$inv"
}

Set-Axis 'LeftThumbstickX'  0 $false
Set-Axis 'LeftThumbstickY'  1 $true
Set-Axis 'RightThumbstickX' 2 $false
Set-Axis 'RightThumbstickY' 3 $true
Set-Axis 'LeftTrigger'      4 $false
Set-Axis 'RightTrigger'     5 $false

Write-Host ""
Write-Host "== Restarting GameInput service =="
$svc = Get-Service -Name 'GameInputSvc' -ErrorAction SilentlyContinue
if ($svc) {
    Restart-Service -Name 'GameInputSvc' -Force -ErrorAction SilentlyContinue
    if ($?) {
        Write-Host "  GameInputSvc restarted"
    }
    else {
        Write-Host "  GameInputSvc restart failed (ignored); log off/on or reboot to apply"
    }
}
else {
    Write-Host "  GameInputSvc not found (Win11 only); log off/on or reboot to apply"
}

Write-Host ""
Write-Host "Done. Replug the controller (or log off/on), then rerun:" -ForegroundColor Green
Write-Host "  python 2.py --seconds 10" -ForegroundColor Cyan
Write-Host ""
Write-Host "If WGI becomes all zeros (LT=0 RT=0 RX=0 RY=0), the fix worked." -ForegroundColor Green
Write-Host "To restore original behavior:" -ForegroundColor Yellow
Write-Host "  Remove-Item 'HKLM:\SYSTEM\CurrentControlSet\Control\GameInput\Devices\054C0CE600010005' -Recurse -Force" -ForegroundColor Yellow
