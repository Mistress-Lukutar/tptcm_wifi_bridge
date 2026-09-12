param([string]$Port = 'COM13', [int]$Seconds = 35, [switch]$Dtr)
$p = New-Object System.IO.Ports.SerialPort($Port, 115200, 'None', 8, 'One')
$p.ReadTimeout = 500
$p.DtrEnable = [bool]$Dtr
$p.RtsEnable = $false
$p.Open()
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  try {
    $line = $p.ReadLine()
    Write-Output $line
  } catch [TimeoutException] {
  } catch {
    break
  }
}
$p.Close()
