# BuildCheck.ps1
# Usage: .\BuildCheck.ps1 "D:\.User Files\Nextcloud\Documents\Unreal Projects\LostWithin\LostWithin.uproject"

param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectPath
)

$BuildBat = "D:\Softwares\Games\UE_5.6\Engine\Build\BatchFiles\Build.bat"
$Target = "LostWithinEditor Win64 Development"

Write-Host "Starting build check for: $ProjectPath" -ForegroundColor Cyan

# Run the build
$process = Start-Process -FilePath $BuildBat -ArgumentList "$Target -Project=`"$ProjectPath`"" -Wait -PassThru -NoNewWindow -RedirectStandardOutput "build_log.txt" -RedirectStandardError "error_log.txt"

if ($process.ExitCode -eq 0) {
    Write-Host "Build Succeeded!" -ForegroundColor Green
} else {
    Write-Host "Build Failed with Exit Code: $($process.ExitCode)" -ForegroundColor Red
    Write-Host "Checking for errors in log..." -ForegroundColor Yellow
    Get-Content "error_log.txt" | Select-String -Pattern "Error"
}

