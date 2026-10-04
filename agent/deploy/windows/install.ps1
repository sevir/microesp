<#
.SYNOPSIS
  Installs (or uninstalls) microesp-agent as a Windows service.
.DESCRIPTION
  Run in PowerShell as Administrator:
    .\install.ps1 [-Binary .\microesp-agent.exe]
    .\install.ps1 -Uninstall [-Purge]
    .\install.ps1 -DryRun            # shows the actions without applying them
  The service runs as LocalSystem (required for "shutdown /s|/r /t 0").
  After installing, pair with:  & "$env:ProgramFiles\MicroESP\microesp-agent.exe" pair
  (stop the service first: Stop-Service MicroESPAgent).
#>
[CmdletBinding()]
param(
    [string]$Binary = (Join-Path $PSScriptRoot '..\..\microesp-agent.exe'),
    [switch]$Uninstall,
    [switch]$Purge,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$ServiceName = 'MicroESPAgent'
$InstallDir  = Join-Path $env:ProgramFiles 'MicroESP'
$DataDir     = Join-Path $env:ProgramData 'MicroESP'
$Exe         = Join-Path $InstallDir 'microesp-agent.exe'
$ConfigFile  = Join-Path $DataDir 'agent.toml'

function Invoke-Step([string]$Description, [scriptblock]$Action) {
    if ($DryRun) { Write-Host "[dry-run] $Description" } else { Write-Host "==> $Description"; & $Action }
}

if (-not $DryRun) {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script as Administrator (or use -DryRun).'
    }
}

$existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue

if ($Uninstall) {
    if ($existing) {
        Invoke-Step "stop and remove service $ServiceName" {
            Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
            sc.exe delete $ServiceName | Out-Null
        }
    }
    Invoke-Step "delete $InstallDir" { Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue }
    if ($Purge) {
        Invoke-Step "delete $DataDir (configuration and key)" { Remove-Item -Recurse -Force $DataDir -ErrorAction SilentlyContinue }
    }
    return
}

if (-not $DryRun -and -not (Test-Path $Binary)) { throw "Binary not found: $Binary (use -Binary)" }

if ($existing) {
    Invoke-Step "stop existing service" { Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue }
}
Invoke-Step "copy binary to $Exe" {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Force $Binary $Exe
}
Invoke-Step "create $DataDir with access only for SYSTEM and Administrators" {
    New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
    icacls $DataDir /inheritance:r /grant:r 'SYSTEM:(OI)(CI)F' 'Administrators:(OI)(CI)F' | Out-Null
}
if (-not (Test-Path $ConfigFile)) {
    Invoke-Step "create default configuration $ConfigFile" {
        @(
            'device = "auto"'
            "key_file = '$DataDir\agent.key'"
            'disks = ["C:\\"]'
            'telemetry_interval = "10s"'
            'heartbeat_interval = "5s"'
            'dry_run = false'
            'power_backend = "windows"'
            'log_level = "info"'
        ) | Set-Content -Encoding ascii $ConfigFile
    }
}
if (-not $existing) {
    Invoke-Step "register service $ServiceName" {
        New-Service -Name $ServiceName -DisplayName 'MicroESP Agent' `
            -Description 'Link to the MicroESP USB dongle (telemetry and remote shutdown)' `
            -BinaryPathName "`"$Exe`" run --config `"$ConfigFile`"" -StartupType Automatic | Out-Null
        sc.exe failure $ServiceName reset= 86400 actions= restart/3000/restart/3000/restart/3000 | Out-Null
    }
}
Invoke-Step "start service" { Start-Service -Name $ServiceName }
Write-Host "Done. If not yet paired: Stop-Service $ServiceName; & `"$Exe`" pair --config `"$ConfigFile`"; Start-Service $ServiceName"
