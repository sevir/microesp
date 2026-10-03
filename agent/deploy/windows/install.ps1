<#
.SYNOPSIS
  Instala (o desinstala) microesp-agent como servicio de Windows.
.DESCRIPTION
  Ejecutar en PowerShell como Administrador:
    .\install.ps1 [-Binary .\microesp-agent.exe]
    .\install.ps1 -Uninstall [-Purge]
    .\install.ps1 -DryRun            # muestra las acciones sin aplicarlas
  El servicio corre como LocalSystem (necesario para "shutdown /s|/r /t 0").
  Tras instalar, empareja con:  & "$env:ProgramFiles\MicroESP\microesp-agent.exe" pair
  (detén antes el servicio: Stop-Service MicroESPAgent).
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
        throw 'Ejecuta este script como Administrador (o usa -DryRun).'
    }
}

$existing = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue

if ($Uninstall) {
    if ($existing) {
        Invoke-Step "detener y eliminar el servicio $ServiceName" {
            Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
            sc.exe delete $ServiceName | Out-Null
        }
    }
    Invoke-Step "borrar $InstallDir" { Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue }
    if ($Purge) {
        Invoke-Step "borrar $DataDir (configuración y clave)" { Remove-Item -Recurse -Force $DataDir -ErrorAction SilentlyContinue }
    }
    return
}

if (-not $DryRun -and -not (Test-Path $Binary)) { throw "No se encuentra el binario: $Binary (usa -Binary)" }

if ($existing) {
    Invoke-Step "detener el servicio existente" { Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue }
}
Invoke-Step "copiar binario a $Exe" {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Force $Binary $Exe
}
Invoke-Step "crear $DataDir con acceso solo para SYSTEM y Administradores" {
    New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
    icacls $DataDir /inheritance:r /grant:r 'SYSTEM:(OI)(CI)F' 'Administrators:(OI)(CI)F' | Out-Null
}
if (-not (Test-Path $ConfigFile)) {
    Invoke-Step "crear configuración por defecto $ConfigFile" {
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
    Invoke-Step "registrar el servicio $ServiceName" {
        New-Service -Name $ServiceName -DisplayName 'MicroESP Agent' `
            -Description 'Enlace con el dongle USB MicroESP (telemetría y apagado remoto)' `
            -BinaryPathName "`"$Exe`" run --config `"$ConfigFile`"" -StartupType Automatic | Out-Null
        sc.exe failure $ServiceName reset= 86400 actions= restart/3000/restart/3000/restart/3000 | Out-Null
    }
}
Invoke-Step "arrancar el servicio" { Start-Service -Name $ServiceName }
Write-Host "Listo. Si aún no está emparejado: Stop-Service $ServiceName; & `"$Exe`" pair --config `"$ConfigFile`"; Start-Service $ServiceName"
