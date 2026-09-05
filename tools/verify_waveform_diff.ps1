# Verify: interval-cache differential waveform == full-redraw waveform, pixel by pixel
$W=320; $H=240; $WF_X0=80; $WF_W=240; $N=600

function Fill($s, [int]$x, [int]$y, [int]$wd, [int]$ht, [int]$c) {
  for ($yy=$y; $yy -lt $y+$ht; $yy++) {
    if ($yy -lt 0 -or $yy -ge $H) { continue }
    for ($xx=$x; $xx -lt $x+$wd; $xx++) {
      if ($xx -ge 0 -and $xx -lt $W) { $s[$yy*$W+$xx] = $c }
    }
  }
}

# ---- full redraw version (reference) ----
function Push-Full($s, $wf, [double]$G) {
  if ($G -lt 0.0){$G=0.0}; if ($G -gt 50.0){$G=50.0}
  $ny = [int](239.0 - $G*239.0/50.0)
  for ($i=0;$i -lt $WF_W-1;$i++) {
    $a=$wf[$i]; $b=$wf[$i+1]
    if ($a -eq 0xFF -or $b -eq 0xFF){continue}
    if ($a -gt $b){$t=$a;$a=$b;$b=$t}
    Fill $s ($WF_X0+$i) $a 1 ($b-$a+1) 0
  }
  if ($wf[$WF_W-1] -ne 0xFF){ Fill $s ($WF_X0+$WF_W-1) $wf[$WF_W-1] 1 1 0 }
  for ($i=0;$i -lt $WF_W-1;$i++){ $wf[$i]=$wf[$i+1] }
  $wf[$WF_W-1]=$ny
  for ($i=0;$i -lt $WF_W-1;$i++) {
    $a=$wf[$i]; $b=$wf[$i+1]
    if ($a -eq 0xFF -or $b -eq 0xFF){continue}
    if ($a -gt $b){$t=$a;$a=$b;$b=$t}
    Fill $s ($WF_X0+$i) $a 1 ($b-$a+1) 1
  }
  Fill $s ($WF_X0+$WF_W-1) $ny 1 1 1
}

# ---- interval-cache differential version (under test) ----
$script:calls = 0
function Push-Diff($s, $wf, $arrA, $arrB, [double]$G) {
  if ($G -lt 0.0){$G=0.0}; if ($G -gt 50.0){$G=50.0}
  $ny = [int](239.0 - $G*239.0/50.0)
  $newY = New-Object 'byte[]' $WF_W
  for ($i=0;$i -lt $WF_W-1;$i++){ $newY[$i]=$wf[$i+1] }
  $newY[$WF_W-1]=$ny
  for ($i=0;$i -lt $WF_W;$i++) {
    $yy = $newY[$i]
    if ($yy -eq 0xFF){continue}
    $yn = $yy
    if ($i -lt $WF_W-1){ $yn=$newY[$i+1] }
    if ($yn -eq 0xFF){$yn=$yy}
    $a = $yy; $b = $yn
    if ($a -gt $b){$t=$a;$a=$b;$b=$t}
    $da=$arrA[$i]; $db=$arrB[$i]
    if ($da -ne $a -or $db -ne $b) {
      if ($da -ne 0xFF){ Fill $s ($WF_X0+$i) $da 1 ($db-$da+1) 0; $script:calls++ }
      Fill $s ($WF_X0+$i) $a 1 ($b-$a+1) 1; $script:calls++
      $arrA[$i]=$a; $arrB[$i]=$b
    }
    $wf[$i]=$yy
  }
}

# ---- run both on identical input ----
$sF = New-Object 'byte[]' ($W*$H)
$sD = New-Object 'byte[]' ($W*$H)
$wfF = New-Object 'byte[]' $WF_W
$wfD = New-Object 'byte[]' $WF_W
$dA = New-Object 'byte[]' $WF_W
$dB = New-Object 'byte[]' $WF_W
for ($i=0;$i -lt $WF_W;$i++){ $wfF[$i]=0xFF; $wfD[$i]=0xFF; $dA[$i]=0xFF; $dB[$i]=0xFF }

$rng = New-Object System.Random 3
$totCalls=0; $maxCalls=0; $mismatch=0; $firstMis = 0
for ($k=0; $k -lt $N; $k++) {
  # signal: slow drift + step + noise + spike
  $base = 20.0 + 8*[math]::Sin($k/200.0)
  if ($k -ge 400 -and $k -lt 500) { $base = 42.0 }
  if ($k -eq 700) { $base = 5.0 }
  $G = $base + ($rng.NextDouble()-0.5)*2.0

  $script:calls = 0
  Push-Full $sF $wfF $G
  Push-Diff $sD $wfD $dA $dB $G

  # fast compare via string
  $strF = [System.Text.Encoding]::ASCII.GetString($sF)
  $strD = [System.Text.Encoding]::ASCII.GetString($sD)
  if ($strF -ne $strD) {
    $mismatch++
    if ($firstMis -lt 3) {
      $found = $false
      for ($x=$WF_X0; $x -lt $W -and -not $found; $x++) {
        for ($y=0; $y -lt $H -and -not $found; $y++) {
          if ($sF[$y*$W+$x] -ne $sD[$y*$W+$x]) {
            Write-Host "MISMATCH frame=$k x=$x y=$y full=$($sF[$y*$W+$x]) diff=$($sD[$y*$W+$x])"
            $found = $true; $firstMis++
          }
        }
      }
    }
    if ($mismatch -gt 5) { Write-Host "ABORT: too many mismatches at frame $k"; break }
  }
  $totCalls += $script:calls
  if ($script:calls -gt $maxCalls){$maxCalls=$script:calls}
}
Write-Host "frames=$N  pixelMismatch=$mismatch"
Write-Host "differential fill calls per frame: avg=$([math]::Round($totCalls/$N,2)) max=$maxCalls (full redraw would be ~480/frame)"
if ($mismatch -eq 0) { Write-Host "PASS: differential screen == full-redraw screen every frame" } else { Write-Host "FAIL" }
