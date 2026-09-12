param([string]$BridgeIp = '192.168.1.41')
$t = New-Object System.Net.Sockets.TcpClient($BridgeIp, 3333)
$s = $t.GetStream()
$s.ReadTimeout = 3000

# Generate a fresh log event through the print port
$e = New-Object System.Net.Sockets.TcpClient($BridgeIp, 9100)
Start-Sleep -Milliseconds 400
$e.Close()

$buf = New-Object byte[] 4096
$sw = [Diagnostics.Stopwatch]::StartNew()
$out = New-Object System.Text.StringBuilder
$done = $false
while (-not $done -and $sw.Elapsed.TotalSeconds -lt 6) {
  try {
    $n = $s.Read($buf, 0, $buf.Length)
    if ($n -gt 0) { [void]$out.Append([Text.Encoding]::ASCII.GetString($buf, 0, $n)) }
  } catch [System.IO.IOException] {
  }
  if ($out.ToString() -match 'Client disconnected') { $done = $true }
}
$t.Close()
Write-Output $out.ToString()
