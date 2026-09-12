param([string]$Subnet = '192.168.1.', [int]$PrintPort = 9100)
$clients = @()
$tasks = @()
1..254 | ForEach-Object {
  $ip = "$Subnet$_"
  $c = New-Object System.Net.Sockets.TcpClient
  $clients += $c
  try {
    $t = $c.BeginConnect($ip, $PrintPort, $null, $null)
    $tasks += [pscustomobject]@{ Ip = $ip; C = $c; T = $t }
  } catch {
    $tasks += [pscustomobject]@{ Ip = $ip; C = $c; T = $null }
  }
}
Start-Sleep -Milliseconds 1200
foreach ($x in $tasks) {
  try {
    if ($x.T -ne $null -and $x.T.IsCompleted -and $x.C.Connected) {
      Write-Output $x.Ip
    }
  } catch {}
  try { $x.C.Close() } catch {}
}
