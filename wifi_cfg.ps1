# JARVIS ESP32 WiFi Configuration via Bluetooth
Write-Host "`n=== JARVIS WiFi Config ===" -ForegroundColor Cyan

Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() |
    Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function AsTask($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

[void][Windows.Devices.Bluetooth.BluetoothAdapter,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.BluetoothLEDevice,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
[void][Windows.Devices.Enumeration.DeviceInformation,Windows.Devices.Enumeration,ContentType=WindowsRuntime]
[void][Windows.Devices.Enumeration.DeviceInformationCollection,Windows.Devices.Enumeration,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService,Windows.Devices.Bluetooth.GenericAttributeProfile,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult,Windows.Devices.Bluetooth.GenericAttributeProfile,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic,Windows.Devices.Bluetooth.GenericAttributeProfile,ContentType=WindowsRuntime]
[void][Windows.Security.Cryptography.CryptographicBuffer,Windows.Security.Cryptography,ContentType=WindowsRuntime]

$adapter = AsTask ([Windows.Devices.Bluetooth.BluetoothAdapter]::GetDefaultAsync()) ([Windows.Devices.Bluetooth.BluetoothAdapter])
if (-not $adapter) { Write-Host "ERROR: No Bluetooth adapter!" -ForegroundColor Red; pause; exit 1 }
Write-Host "Bluetooth OK" -ForegroundColor Green

Write-Host "`nScanning for JARVIS-CONFIG (15s)..." -ForegroundColor Yellow
$found = $null
$deadline = [DateTime]::UtcNow.AddSeconds(15)

while (-not $found -and [DateTime]::UtcNow -lt $deadline) {
    # Try unpaired devices
    $sel1 = [Windows.Devices.Bluetooth.BluetoothLEDevice]::GetDeviceSelectorFromPairingState($false)
    $devs1 = AsTask ([Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($sel1)) ([Windows.Devices.Enumeration.DeviceInformationCollection])
    foreach ($d in $devs1) {
        if ($d.Name -and $d.Name.Contains("JARVIS")) { $found = $d; break }
    }
    # Also try paired devices
    if (-not $found) {
        $sel2 = [Windows.Devices.Bluetooth.BluetoothLEDevice]::GetDeviceSelectorFromPairingState($true)
        $devs2 = AsTask ([Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($sel2)) ([Windows.Devices.Enumeration.DeviceInformationCollection])
        foreach ($d in $devs2) {
            if ($d.Name -and $d.Name.Contains("JARVIS")) { $found = $d; break }
        }
    }
    if (-not $found) {
        Write-Host "." -NoNewline
        Start-Sleep -Milliseconds 1000
    }
}

if (-not $found) {
    Write-Host ""
    Write-Host "ERROR: JARVIS-CONFIG not found!" -ForegroundColor Red
    Write-Host "Make sure ESP32 screen says 'BLE advertising...'" -ForegroundColor Yellow
    pause; exit 1
}
Write-Host ""
Write-Host "Found: $($found.Name)" -ForegroundColor Green

$bleDevice = AsTask ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromIdAsync($found.Id)) ([Windows.Devices.Bluetooth.BluetoothLEDevice])
if (-not $bleDevice) { Write-Host "ERROR: Connection failed!" -ForegroundColor Red; pause; exit 1 }
Write-Host "Connected!" -ForegroundColor Green

$serviceUuid = [Guid]::Parse("12345678-1234-1234-1234-123456789abc")
$charUuid = [Guid]::Parse("12345678-1234-1234-1234-123456789abd")

$servicesResult = AsTask ($bleDevice.GetGattServicesAsync()) ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult])
$targetService = $null
foreach ($svc in $servicesResult.Services) {
    if ($svc.Uuid -eq $serviceUuid) { $targetService = $svc; break }
}
if (-not $targetService) { Write-Host "ERROR: Service not found!" -ForegroundColor Red; $bleDevice.Dispose(); pause; exit 1 }

$targetChar = $null
foreach ($ch in $targetService.Characteristics) {
    if ($ch.Uuid -eq $charUuid) { $targetChar = $ch; break }
}
if (-not $targetChar) { Write-Host "ERROR: Characteristic not found!" -ForegroundColor Red; $bleDevice.Dispose(); pause; exit 1 }

Write-Host ""
$ssid = Read-Host "Enter WiFi SSID"
if (-not $ssid) { Write-Host "Cancelled." -ForegroundColor Red; $bleDevice.Dispose(); exit 1 }
$pass = Read-Host "Enter WiFi Password"

$json = "{`"ssid`":`"$ssid`",`"pass`":`"$pass`"}"
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$buffer = [Windows.Security.Cryptography.CryptographicBuffer]::CreateFromByteArray($bytes)
$writeOp = $targetChar.WriteValueAsync($buffer)
$writeResult = AsTask ($writeOp) ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus])

if ($writeResult -eq [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus]::Success) {
    Write-Host "`nCredentials sent!" -ForegroundColor Green
} else {
    Write-Host "`nWrite failed: $writeResult" -ForegroundColor Red
}

Write-Host "ESP32 connecting to: $ssid" -ForegroundColor Cyan
Start-Sleep -Seconds 2
$bleDevice.Dispose()
Write-Host "Done!" -ForegroundColor Green
pause
