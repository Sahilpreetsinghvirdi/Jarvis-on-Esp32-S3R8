# JARVIS ESP32 WiFi Configuration via Bluetooth
# Zero installs - uses built-in Windows 10/11 BLE APIs
# Run: powershell .\wifi_cfg.ps1

Write-Host "`n=== JARVIS WiFi Config ===" -ForegroundColor Cyan

# Load WinRT types
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function AsTask($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

$asTaskAction = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0]
function AsTaskAction($WinRtTask) {
    $netTask = $asTaskAction.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
}

[void][Windows.Devices.Bluetooth.BluetoothAdapter,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.BluetoothLEDevice,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
[void][Windows.Devices.Enumeration.DeviceInformation,Windows.Devices.Enumeration,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService,Windows.Devices.Bluetooth.GenericAttributeProfile,ContentType=WindowsRuntime]
[void][Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic,Windows.Devices.Bluetooth.GenericAttributeProfile,ContentType=WindowsRuntime]

# Check Bluetooth
$adapter = AsTask ([Windows.Devices.Bluetooth.BluetoothAdapter]::GetDefaultAsync()) ([Windows.Devices.Bluetooth.BluetoothAdapter])
if (-not $adapter) {
    Write-Host "ERROR: No Bluetooth adapter found!" -ForegroundColor Red; pause; exit 1
}
Write-Host "Bluetooth OK" -ForegroundColor Green

# Scan for JARVIS-CONFIG
Write-Host "`nScanning for JARVIS-CONFIG (10s)..." -ForegroundColor Yellow
$found = $null
$deadline = [DateTime]::UtcNow.AddSeconds(10)

while (-not $found -and [DateTime]::UtcNow -lt $deadline) {
    $selector = [Windows.Devices.Bluetooth.BluetoothLEDevice]::GetDeviceSelectorFromPairingState($false)
    $devices = [Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($selector).GetAwaiter().GetResult()
    foreach ($dev in $devices) {
        if ($dev.Name -and $dev.Name.Contains("JARVIS")) {
            $found = $dev; break
        }
    }
    if (-not $found) { Start-Sleep -Milliseconds 500 }
}

if (-not $found) {
    Write-Host "ERROR: JARVIS-CONFIG not found!" -ForegroundColor Red
    Write-Host "Make sure ESP32 is on the BLE config screen." -ForegroundColor Yellow
    pause; exit 1
}
Write-Host "Found: $($found.Name)" -ForegroundColor Green

# Connect
$bleDevice = AsTask ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromIdAsync($found.Id)) ([Windows.Devices.Bluetooth.BluetoothLEDevice])
if (-not $bleDevice) { Write-Host "ERROR: Connection failed!" -ForegroundColor Red; pause; exit 1 }
Write-Host "Connected!" -ForegroundColor Green

# Get service
$serviceUuid = [Guid]::Parse("12345678-1234-1234-1234-123456789abc")
$charUuid = [Guid]::Parse("12345678-1234-1234-1234-123456789abd")

$services = $bleDevice.GetGattServicesAsync().GetAwaiter().GetResult()
$targetService = $null
foreach ($svc in $services.Services) {
    if ($svc.Uuid -eq $serviceUuid) { $targetService = $svc; break }
}
if (-not $targetService) { Write-Host "ERROR: Service not found!" -ForegroundColor Red; $bleDevice.Dispose(); pause; exit 1 }

$targetChar = $null
foreach ($ch in $targetService.Characteristics) {
    if ($ch.Uuid -eq $charUuid) { $targetChar = $ch; break }
}
if (-not $targetChar) { Write-Host "ERROR: Characteristic not found!" -ForegroundColor Red; $bleDevice.Dispose(); pause; exit 1 }

# Get credentials
Write-Host ""
$ssid = Read-Host "Enter WiFi SSID"
if (-not $ssid) { Write-Host "Cancelled." -ForegroundColor Red; $bleDevice.Dispose(); exit 1 }
$pass = Read-Host "Enter WiFi Password"

# Send via BLE
$json = "{`"ssid`":`"$ssid`",`"pass`":`"$pass`"}"
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$buffer = [Windows.Security.Cryptography.CryptographicBuffer]::CreateFromByteArray($bytes)
$writeResult = $targetChar.WriteValueAsync($buffer).GetAwaiter().GetResult()

if ($writeResult -eq [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus]::Success) {
    Write-Host "`nCredentials sent!" -ForegroundColor Green
} else {
    Write-Host "`nWrite failed: $writeResult" -ForegroundColor Red
}

Write-Host "ESP32 is connecting to: $ssid" -ForegroundColor Cyan
Start-Sleep -Seconds 2
$bleDevice.Dispose()
Write-Host "Done. You can close this window." -ForegroundColor Green
pause
