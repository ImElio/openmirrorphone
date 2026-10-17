# Builds the installer's side image (164x314, 24-bit BMP, as NSIS wants) from the
# rendered welcome.svg background plus the app icon. Called by package.sh.
param([string]$Background, [string]$Icon, [string]$Out)
$ErrorActionPreference = 'Stop' # any failure must stop package.sh
Add-Type -AssemblyName System.Drawing
$bgImage = [System.Drawing.Image]::FromFile($Background)
$iconImage = [System.Drawing.Image]::FromFile($Icon)
$bmp = New-Object System.Drawing.Bitmap 164, 314, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($bgImage, 0, 0, 164, 314)
$g.DrawImage($iconImage, 30, 92, 104, 104)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Bmp)
$g.Dispose(); $bmp.Dispose(); $bgImage.Dispose(); $iconImage.Dispose()
