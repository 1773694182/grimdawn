$dirPath = "H:\github\grim dawn\src\GrimDawnTeleporter\bin\x64\Release\net8.0-windows\win-x64\"
Write-Host "Directory: $dirPath"
Write-Host "---"
Get-ChildItem $dirPath | ForEach-Object {
    Write-Host "$($_.Name) - $($_.Length) bytes"
}
