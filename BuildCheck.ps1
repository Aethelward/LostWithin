# Run on Windows with Unreal Editor closed. The VS Code build task calls this script.
# Optional: .\BuildCheck.ps1 -ProjectPath <project.uproject> -EngineRoot <engine-folder>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$ProjectPath = '',
    [string]$EngineRoot = $env:UE_ENGINE_ROOT
)

$ErrorActionPreference = 'Stop'

# Reads one registry value, or $null when the key or the value is missing. Get-ItemPropertyValue throws a terminating
# error for a missing value that -ErrorAction SilentlyContinue does not suppress, so check the value first.
function Get-RegistryValue([string]$Key, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Key)) {
        return $null
    }
    $Item = Get-ItemProperty -LiteralPath $Key
    if ($null -eq $Item -or $null -eq $Item.PSObject.Properties[$Name]) {
        return $null
    }
    return [string]$Item.PSObject.Properties[$Name].Value
}

try {
    if ($env:OS -ne 'Windows_NT') {
        throw 'Run this build in a local Windows VS Code window, not a Remote-SSH terminal.'
    }
    if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
        # Windows PowerShell 5.1 leaves $PSScriptRoot empty while it evaluates parameter defaults, so resolve here.
        $ScriptFolder = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
        $ProjectPath = Join-Path $ScriptFolder 'LostWithin.uproject'
    }
    $ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).ProviderPath
    if ([System.IO.Path]::GetExtension($ProjectPath) -ne '.uproject') {
        throw 'ProjectPath must point to an Unreal .uproject file.'
    }
    if (Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue) {
        throw 'Save your work and close Unreal Editor before building; it locks the game DLL.'
    }

    $Project = Get-Content -LiteralPath $ProjectPath -Raw | ConvertFrom-Json
    $Association = [string]$Project.EngineAssociation
    if ([string]::IsNullOrWhiteSpace($EngineRoot) -and $Association) {
        # Source builds register under HKCU by GUID or name; launcher installs register under HKLM by version.
        $EngineRoot = Get-RegistryValue 'HKCU:\Software\Epic Games\Unreal Engine\Builds' $Association
        if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
            foreach ($LauncherKey in @(
                "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association",
                "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$Association"
            )) {
                $EngineRoot = Get-RegistryValue $LauncherKey 'InstalledDirectory'
                if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) {
                    break
                }
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
        throw "Cannot find the project's Unreal Engine. Set UE_ENGINE_ROOT or pass -EngineRoot with the folder containing Engine."
    }
    $BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
    if (-not (Test-Path -LiteralPath $BuildBat -PathType Leaf)) {
        throw "Unreal build script not found: $BuildBat. Check UE_ENGINE_ROOT or -EngineRoot."
    }

    $Target = [System.IO.Path]::GetFileNameWithoutExtension($ProjectPath) + 'Editor'
    Write-Host "Building $Target Win64 Development..." -ForegroundColor Cyan
    & $BuildBat $Target Win64 Development "-Project=`"$ProjectPath`"" -WaitMutex -NoHotReloadFromIDE
    $BuildExitCode = $LASTEXITCODE
    if ($BuildExitCode -ne 0) {
        [Console]::Error.WriteLine("Build failed with exit code $BuildExitCode. See the compiler output above.")
        exit $BuildExitCode
    }
    Write-Host 'Build succeeded. Reopen the Unreal project to load the new code.' -ForegroundColor Green
    exit 0
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

