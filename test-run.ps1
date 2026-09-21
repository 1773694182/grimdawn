$exePath = "H:\github\grim dawn\src\GrimDawnTeleporter\bin\x64\Release\net8.0-windows\win-x64\GrimDawnTeleporter.exe"
Write-Host "EXE Path: $exePath"

if (Test-Path $exePath) {
    Write-Host "✅ File exists"
    Write-Host "Running program..."
    try {
        Start-Process $exePath -Wait -ErrorAction Stop
        Write-Host "Program closed"
    } catch {
        Write-Host "❌ Error running program: $($_.Exception.Message)"
    }
} else {
    Write-Host "❌ File does not exist"
}
