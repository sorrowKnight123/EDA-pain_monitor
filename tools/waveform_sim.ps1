# Simulate the exact LCD_WaveformPush algorithm (new self-healing version)
# with (A) hum-contaminated signal + EMA filter (current), (B) + moving-average filter (new)
Add-Type -AssemblyName System.Drawing

$W = 320; $H = 240
$WF_X0 = 80; $WF_W = 240

function Idx([int]$x, [int]$y) { return $y * $W + $x }

function Fill($s, [int]$x, [int]$y, [int]$wd, [int]$ht, [int]$c) {
  for ($yy=$y; $yy -lt $y+$ht; $yy++) {
    if ($yy -lt 0 -or $yy -ge $H) { continue }
    for ($xx=$x; $xx -lt $x+$wd; $xx++) {
      if ($xx -ge 0 -and $xx -lt $W) { $s[$yy*$W+$xx] = $c }
    }
  }
}

function Push-Waveform($s, $wf, [double]$G) {
  if ($G -lt 0.0) { $G = 0.0 }
  if ($G -gt 50.0) { $G = 50.0 }
  $ny = [int](239.0 - $G * 239.0 / 50.0)
  # 1) erase old trace
  for ($i=0; $i -lt $WF_W-1; $i++) {
    $a = $wf[$i]; $b = $wf[$i+1]
    if ($a -eq 0xFF -or $b -eq 0xFF) { continue }
    if ($a -gt $b) { $t=$a; $a=$b; $b=$t }
    Fill $s ($WF_X0+$i) $a 1 ($b-$a+1) 0
  }
  if ($wf[$WF_W-1] -ne 0xFF) { Fill $s ($WF_X0+$WF_W-1) $wf[$WF_W-1] 1 1 0 }
  # 2) shift left
  for ($i=0; $i -lt $WF_W-1; $i++) { $wf[$i] = $wf[$i+1] }
  $wf[$WF_W-1] = $ny
  # 3) draw new trace
  for ($i=0; $i -lt $WF_W-1; $i++) {
    $a = $wf[$i]; $b = $wf[$i+1]
    if ($a -eq 0xFF -or $b -eq 0xFF) { continue }
    if ($a -gt $b) { $t=$a; $a=$b; $b=$t }
    Fill $s ($WF_X0+$i) $a 1 ($b-$a+1) 1
  }
  Fill $s ($WF_X0+$WF_W-1) $ny 1 1 1
}

function Verify-Consistency($s, $wf, [string]$tag) {
  for ($i=0; $i -lt $WF_W; $i++) {
    $a = $wf[$i]; $b = $wf[$i]
    if ($i -lt $WF_W-1) { $b = $wf[$i+1] }
    $x = $WF_X0 + $i
    for ($yy=0; $yy -lt $H; $yy++) {
      $expect = 0
      if ($a -ne 0xFF -and $b -ne 0xFF) {
        $lo = [math]::Min($a,$b); $hi = [math]::Max($a,$b)
        if ($yy -ge $lo -and $yy -le $hi) { $expect = 1 }
      }
      if ($s[$yy*$W+$x] -ne $expect) { Write-Host "MISMATCH $tag col=$i y=$yy got=$($s[$yy*$W+$x]) expect=$expect"; return }
    }
  }
  Write-Host "consistency OK: $tag"
}

function Render-PNG($s, [string]$path) {
  $scale = 3
  $bmp = New-Object System.Drawing.Bitmap ($W*$scale), ($H*$scale)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  for ($x=0; $x -lt $W; $x++) {
    for ($y=0; $y -lt $H; $y++) {
      $c = $s[$y*$W+$x]
      if ($c -eq 1) { $g.FillRectangle([System.Drawing.Brushes]::Green, $x*$scale, $y*$scale, $scale, $scale) }
      elseif ($c -eq 2) { $g.FillRectangle([System.Drawing.Brushes]::Gray, $x*$scale, $y*$scale, $scale, $scale) }
    }
  }
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}

# ---------- signal: GSR baseline ~0.5uS + 50Hz hum + slow drift + noise ----------
$rng = New-Object System.Random 7
$NSAMPLES = 1200

function Get-Raw([int]$n) {
  $base = 0.5 + 0.15*[math]::Sin($n/300.0)
  $hum = 3.0*[math]::Sin(2*[math]::PI*50.0*$n/100.0 + 0.7)
  $noise = ($rng.NextDouble()-0.5)*0.4
  return $base + $hum + $noise
}

# ---------- Scenario A: current firmware (EMA alpha=0.25) ----------
$sA = New-Object 'byte[]' ($W*$H)
$wfA = New-Object 'byte[]' $WF_W
for ($i=0;$i -lt $WF_W;$i++){ $wfA[$i]=0xFF }
Fill $sA 79 0 1 240 2
$ema = 0.0
for ($idx=0; $idx -lt $NSAMPLES; $idx++) {
  $raw = Get-Raw $idx
  $ema = $ema + 0.25*($raw - $ema)
  Push-Waveform $sA $wfA $ema
}
Verify-Consistency $sA $wfA "A-final"
Render-PNG $sA "E:\STM32workplace\lcd\.tmp_sim\sim_A_current_EMA.png"

# ---------- Scenario B: new firmware (MA-16 filter) ----------
$sB = New-Object 'byte[]' ($W*$H)
$wfB = New-Object 'byte[]' $WF_W
for ($i=0;$i -lt $WF_W;$i++){ $wfB[$i]=0xFF }
Fill $sB 79 0 1 240 2
$ring = New-Object 'double[]' 16
$ridx = 0; $rcnt = 0
for ($idx=0; $idx -lt $NSAMPLES; $idx++) {
  $raw = Get-Raw $idx
  $ring[$ridx] = $raw
  $ridx = ($ridx + 1) % 16
  if ($rcnt -lt 16) { $rcnt++ }
  $sum = 0.0
  for ($k=0; $k -lt $rcnt; $k++) { $sum += $ring[$k] }
  if ($rcnt -gt 0) { $ma = $sum / $rcnt } else { $ma = 0.0 }
  Push-Waveform $sB $wfB $ma
}
Verify-Consistency $sB $wfB "B-final"
Render-PNG $sB "E:\STM32workplace\lcd\.tmp_sim\sim_B_new_MA16.png"

function Trace-Stats($s, [string]$tag) {
  $max = 0; $sum = 0
  for ($x=$WF_X0; $x -lt $W; $x++) {
    $cnt = 0
    for ($y=0; $y -lt $H; $y++) { if ($s[$y*$W+$x] -eq 1) { $cnt++ } }
    if ($cnt -gt $max) { $max = $cnt }
    $sum += $cnt
  }
  Write-Host "$tag : max green per column=$max avg=$([math]::Round($sum/240.0,1))"
}
Write-Host "--- results:"
Trace-Stats $sA "A (current EMA)"
Trace-Stats $sB "B (new MA16)"
Write-Host "Done."
