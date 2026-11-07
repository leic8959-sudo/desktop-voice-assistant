param([switch]$NearSpeech, [switch]$HumanNearSpeech, [switch]$DirectPlayback, [switch]$CaptureAudio, [switch]$SkipReset, [switch]$SkipSerial, [string]$Port = 'COM3')
$ErrorActionPreference = 'Stop'
$bargeRoot = Split-Path -Parent $PSScriptRoot
$bargeScenario = if ($HumanNearSpeech) { 'human-near' } elseif ($NearSpeech) { 'near-speech' } else { 'far-only' }
$bargeStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bargeStem = Join-Path $bargeRoot ('evidence\barge-' + $bargeScenario + '-' + $bargeStamp)
$bargeOwner = (Get-NetTCPConnection -LocalPort 8765 -State Listen).OwningProcess
$bargeBridge = Get-CimInstance Win32_Process -Filter "ProcessId=$bargeOwner"
if (-not $bargeBridge -or $bargeBridge.CommandLine -notmatch 'gateway.py') { throw '8765端口不是本项目桥接，未停止其他服务。' }
$bargeArguments = @('-u', 'tools/barge_in_device_check.py', '--far-file', 'runtime/wake_samples/aec_far_speech.wav', '--output', ('"' + $bargeStem + '.json"'))
if (-not $DirectPlayback) { $bargeArguments += @('--wake-file', 'runtime/wake_samples/ni_hao_xiao_yan_zi.wav') }
if ($NearSpeech -and -not $HumanNearSpeech) { $bargeArguments += @('--near-file', 'runtime/wake_samples/barge_near.wav') }
if ($HumanNearSpeech) { $bargeArguments += '--human-near' }
if ($DirectPlayback) { $bargeArguments += '--direct-playback' }
if ($CaptureAudio) { $bargeArguments += '--capture-audio' }
Stop-Process -Id $bargeOwner
$bargeSerial = $null
$bargeText = ''
$bargeTest = $null
try {
    $bargeTest = Start-Process -FilePath $bargeBridge.ExecutablePath -ArgumentList $bargeArguments -WorkingDirectory $bargeRoot -WindowStyle Hidden -RedirectStandardOutput ($bargeStem + '.stdout.log') -RedirectStandardError ($bargeStem + '.stderr.log') -PassThru
    $null = $bargeTest.Handle
    if (-not $SkipReset) {
        & (Join-Path $bargeRoot 'firmware\flash_tool\esptool-standalone\esptool-windows-amd64\esptool.exe') --chip esp32s3 --port $Port --before usb-reset --after watchdog-reset --connect-attempts 2 flash-id *> ($bargeStem + '.reset.log')
        if ($LASTEXITCODE -ne 0) { throw '设备复位失败，保留检查日志。' }
    }
    $bargeSerial = [System.IO.Ports.SerialPort]::new($Port, 115200)
    $bargeSerial.Encoding = [System.Text.Encoding]::UTF8
    $bargeSerial.DtrEnable = $false
    $bargeSerial.RtsEnable = $false
    if (-not $SkipSerial) { try { $bargeSerial.Open() } catch { Write-Output '串口未能打开，继续等待局域网检查结果。' } }
    $bargeEnd = (Get-Date).AddSeconds($(if ($HumanNearSpeech) { 165 } else { 31 }))
    while ((Get-Date) -lt $bargeEnd -and -not $bargeTest.HasExited) {
        if ($bargeSerial.IsOpen) {
            try { $bargeText += $bargeSerial.ReadExisting() } catch { $bargeText += "`nSERIAL_DISCONNECTED`n" }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $bargeTest.WaitForExit($(if ($HumanNearSpeech) { 180000 } else { 60000 }))) { Stop-Process -Id $bargeTest.Id; throw '设备检查超时。' }
    Get-Content -LiteralPath ($bargeStem + '.stdout.log') -Tail 12
    Get-Content -LiteralPath ($bargeStem + '.stderr.log') -Tail 5
    if ($bargeTest.ExitCode -ne 0) { throw ('设备检查失败，退出码=' + $bargeTest.ExitCode) }
    $bargeText -split "`n" | Select-String 'barge_state|barge_ack|播放完成|aec_stats|watchdog|abort\(|Guru Meditation' | Select-Object -Last 14
} finally {
    $bargeText | Set-Content -Encoding utf8 -LiteralPath ($bargeStem + '.serial.log')
    if ($bargeSerial) { if ($bargeSerial.IsOpen) { $bargeSerial.Close() }; $bargeSerial.Dispose() }
    if ($bargeTest -and -not $bargeTest.HasExited) { Stop-Process -Id $bargeTest.Id }
    $bargeRestored = Start-Process -FilePath $bargeBridge.ExecutablePath -ArgumentList @('gateway.py', '--config', 'config.local.json') -WorkingDirectory $bargeRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $bargeRoot 'runtime\bridge-barge.stdout.log') -RedirectStandardError (Join-Path $bargeRoot 'runtime\bridge-barge.stderr.log') -PassThru
    Write-Output ('BRIDGE_PID=' + $bargeRestored.Id)
}
