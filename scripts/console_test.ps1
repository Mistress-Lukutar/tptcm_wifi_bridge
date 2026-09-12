param([string]$BridgeIp = '192.168.1.41', [string]$Port = 'COM13')
$p = New-Object System.IO.Ports.SerialPort($Port, 115200, 'None', 8, 'One')
$p.ReadTimeout = 500
$p.DtrEnable = $true
$p.RtsEnable = $false
$p.Open()

# Generate a fresh log event through the print port while listening
$job = Start-Job -ScriptBlock {
  param([string]$BridgeIp)
  Start-Sleep -Seconds 2
  try {
    $e = New-Object System.Net.Sockets.TcpClient($BridgeIp, 9100)
    Start-Sleep -Milliseconds 400
    $e.Close()
  } catch {}
} -ArgumentList $BridgeIp

$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 10) {
  try {
    $line = $p.ReadLine()
    Write-Output $line
  } catch [TimeoutException] {
  }
}
$p.Close()
Remove-Job $job -Force -ErrorAction SilentlyContinue
