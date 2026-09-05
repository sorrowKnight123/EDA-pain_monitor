# 16x16 中文点阵字形生成脚本（SimHei 黑体）
# 管线：96px 抗锯齿渲染 → 双线性缩放到 <=15px 居中 → 阈值 128 二值化（保留细笔画）
# 用法：修改 $chars（Unicode 码点）后直接运行，将 "HEX|..." 输出粘贴进
#       Core/Inc/BSP/cn_font_16.h（新增字形时同步追加 GB 码宏与 CN_GB 表项）
Add-Type -AssemblyName System.Drawing

$fontPath = "C:\Windows\Fonts\simhei.ttf"
$pfc = New-Object System.Drawing.Text.PrivateFontCollection
$pfc.AddFontFile($fontPath)
$family = $pfc.Families[0]

# 需要生成的字符：名称 + Unicode 码点
$chars = @(
  @{ N="皮"; U=0x76AE }, @{ N="肤"; U=0x80A4 }, @{ N="电"; U=0x7535 }, @{ N="导"; U=0x5BFC },
  @{ N="差"; U=0x5DEE }, @{ N="值"; U=0x503C }, @{ N="未"; U=0x672A },
  @{ N="麻"; U=0x9EBB }, @{ N="醉"; U=0x9189 }, @{ N="完"; U=0x5B8C }, @{ N="全"; U=0x5168 })

function Get-Glyph16([string]$ch) {
  $S = 96
  $src = New-Object System.Drawing.Bitmap $S,$S
  $g = [System.Drawing.Graphics]::FromImage($src)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
  $g.Clear([System.Drawing.Color]::White)
  $font = New-Object System.Drawing.Font($family, 64, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $sf = New-Object System.Drawing.StringFormat
  $sf.Alignment = [System.Drawing.StringAlignment]::Center
  $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
  $rect = New-Object System.Drawing.RectangleF(0,0,$S,$S)
  $g.DrawString($ch, $font, [System.Drawing.Brushes]::Black, $rect, $sf)
  $g.Dispose(); $font.Dispose()
  # 暗像素包围盒
  $minX=$S;$minY=$S;$maxX=-1;$maxY=-1
  for ($y=0;$y -lt $S;$y++){ for($x=0;$x -lt $S;$x++){
    if($src.GetPixel($x,$y).R -lt 160){
      if($x -lt $minX){$minX=$x}; if($x -gt $maxX){$maxX=$x}
      if($y -lt $minY){$minY=$y}; if($y -gt $maxY){$maxY=$y}
    }}}
  $bw = $maxX-$minX+1; $bh = $maxY-$minY+1
  # 适配最大 15px，双线性缩放（保留细笔画）
  $scale = [Math]::Min(15.0/$bw, 15.0/$bh)
  $dw = [Math]::Max(1,[Math]::Round($bw*$scale)); $dh = [Math]::Max(1,[Math]::Round($bh*$scale))
  $dest = New-Object System.Drawing.Bitmap 16,16
  $dg = [System.Drawing.Graphics]::FromImage($dest)
  $dg.Clear([System.Drawing.Color]::White)
  $dg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
  $sx = [Math]::Floor((16-$dw)/2); $sy = [Math]::Floor((16-$dh)/2)
  $srcRect = New-Object System.Drawing.Rectangle($minX,$minY,$bw,$bh)
  $destRect = New-Object System.Drawing.Rectangle($sx,$sy,$dw,$dh)
  $dg.DrawImage($src, $destRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
  $dg.Dispose()
  $bytes = New-Object byte[] 32
  for ($y=0;$y -lt 16;$y++){
    $b0=0;$b1=0
    for($x=0;$x -lt 8;$x++){ if($dest.GetPixel($x,$y).R -lt 128){ $b0 = $b0 -bor (0x80 -shr $x) } }
    for($x=8;$x -lt 16;$x++){ if($dest.GetPixel($x,$y).R -lt 128){ $b1 = $b1 -bor (0x80 -shr ($x-8)) } }
    $bytes[$y*2]=$b0; $bytes[$y*2+1]=$b1
  }
  $src.Dispose(); $dest.Dispose()
  return ,$bytes
}

# 8x16 半角字形（如 μ，备用；现行 μ 直接改 XGA_8x16.h 0xB5）
function Get-Glyph8x16([string]$ch) {
  $src = New-Object System.Drawing.Bitmap 64,64
  $g = [System.Drawing.Graphics]::FromImage($src)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
  $g.Clear([System.Drawing.Color]::White)
  $font = New-Object System.Drawing.Font($family, 48, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $sf = New-Object System.Drawing.StringFormat
  $sf.Alignment = [System.Drawing.StringAlignment]::Center
  $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
  $rect = New-Object System.Drawing.RectangleF(0,0,64,64)
  $g.DrawString($ch, $font, [System.Drawing.Brushes]::Black, $rect, $sf)
  $g.Dispose(); $font.Dispose()
  $minX=64;$minY=64;$maxX=-1;$maxY=-1
  for ($y=0;$y -lt 64;$y++){ for($x=0;$x -lt 64;$x++){
    if($src.GetPixel($x,$y).R -lt 128){
      if($x -lt $minX){$minX=$x}; if($x -gt $maxX){$maxX=$x}
      if($y -lt $minY){$minY=$y}; if($y -gt $maxY){$maxY=$y}
    }}}
  $bw = $maxX-$minX+1; $bh = $maxY-$minY+1
  $scale = [Math]::Min(8.0/$bw, 14.0/$bh)
  $dw = [Math]::Max(1,[Math]::Round($bw*$scale)); $dh = [Math]::Max(1,[Math]::Round($bh*$scale))
  $dest = New-Object System.Drawing.Bitmap 8,16
  $dg = [System.Drawing.Graphics]::FromImage($dest)
  $dg.Clear([System.Drawing.Color]::White)
  $dg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
  $sx = [Math]::Floor((8-$dw)/2); $sy = 16-$dh-1
  $srcRect = New-Object System.Drawing.Rectangle($minX,$minY,$bw,$bh)
  $destRect = New-Object System.Drawing.Rectangle($sx,$sy,$dw,$dh)
  $dg.DrawImage($src, $destRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
  $dg.Dispose()
  $bytes = New-Object byte[] 16
  for ($y=0;$y -lt 16;$y++){
    $b0=0
    for($x=0;$x -lt 8;$x++){ if($dest.GetPixel($x,$y).R -lt 128){ $b0 = $b0 -bor (0x80 -shr $x) } }
    $bytes[$y]=$b0
  }
  $src.Dispose(); $dest.Dispose()
  return ,$bytes
}

$enc = [System.Text.Encoding]::GetEncoding(936)
foreach ($c in $chars) {
  $ch = [string][char]$c.U
  $g = Get-Glyph16 $ch
  $gb = $enc.GetBytes($ch)
  $hex = ($g | ForEach-Object { "0x{0:X2}" -f $_ }) -join ','
  $line = "HEX|$($c.N)|0x{0:X2}{1:X2}|{2}" -f $gb[0], $gb[1], $hex
  Write-Host $line
  # ASCII 预览
  for ($row=0; $row -lt 16; $row++) {
    $art = ""
    for ($col=0; $col -lt 16; $col++) {
      $bit = ($g[$row*2 + [int]($col/8)] -shr (7-($col%8))) -band 1
      $art += $(if ($bit) { "#" } else { "." })
    }
    Write-Host $art
  }
  Write-Host ""
}
Write-Host "（将 HEX 行按顺序粘贴进 cn_font_16.h，同时补充 GB 宏与 CN_GB 表项）"