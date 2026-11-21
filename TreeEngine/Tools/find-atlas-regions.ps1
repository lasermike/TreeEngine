[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$Images,
    [int]$Cell = 4,               # Analyse alpha in Cell x Cell pixel blocks.
    [int]$AlphaThreshold = 64,
    [int]$MergeCells = 2,         # Dilation (in cells) that joins fern leaflets into one sprite.
    [double]$MinArea = 0.0015,    # Minimum box area as a fraction of the image.
    [string]$PreviewDirectory
)

# Finds the sprites in a leaf atlas from its alpha channel. Prints each sprite's rectangle and stem
# (bottom-most opaque point) in 0-1 UV from the top-left, ready for a scene's "atlases" section.
# Name the regions yourself, and check the preview: packed sprites can merge into one box, and the
# stem guess is wrong for sideways or upside-down sprites.

$ErrorActionPreference = 'Stop'
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class AtlasScan
{
    public class Region { public int X0, Y0, X1, Y1; public double StemX, StemY; }
    public class Result { public int Width, Height; public List<Region> Regions; }

    public static Result Find(string path, int cell, int threshold, int merge, double minArea, string preview)
    {
        using (var bitmap = new Bitmap(path))
        {
            int w = bitmap.Width, h = bitmap.Height;
            var data = bitmap.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var pixels = new byte[data.Stride * h];
            Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
            bitmap.UnlockBits(data);
            int stride = data.Stride;
            Func<int, int, byte> alpha = (x, y) => pixels[y * stride + x * 4 + 3];

            int gw = (w + cell - 1) / cell, gh = (h + cell - 1) / cell;
            var solid = new bool[gw * gh];
            for (int gy = 0; gy < gh; gy++)
                for (int gx = 0; gx < gw; gx++)
                {
                    int count = 0;
                    for (int y = gy * cell; y < Math.Min(h, gy * cell + cell); y += 2)
                        for (int x = gx * cell; x < Math.Min(w, gx * cell + cell); x += 2)
                            if (alpha(x, y) >= threshold) count++;
                    solid[gy * gw + gx] = count >= 2;
                }
            var grown = new bool[gw * gh];
            for (int gy = 0; gy < gh; gy++)
                for (int gx = 0; gx < gw; gx++)
                {
                    if (!solid[gy * gw + gx]) continue;
                    for (int dy = -merge; dy <= merge; dy++)
                        for (int dx = -merge; dx <= merge; dx++)
                        {
                            int x = gx + dx, y = gy + dy;
                            if (x >= 0 && y >= 0 && x < gw && y < gh) grown[y * gw + x] = true;
                        }
                }

            var label = new int[gw * gh];
            var regions = new List<Region>();
            var stack = new Stack<int>();
            for (int start = 0; start < gw * gh; start++)
            {
                if (!grown[start] || label[start] != 0) continue;
                int id = regions.Count + 1;
                int x0 = int.MaxValue, y0 = int.MaxValue, x1 = -1, y1 = -1;
                label[start] = id;
                stack.Push(start);
                while (stack.Count > 0)
                {
                    int i = stack.Pop(), gx = i % gw, gy = i / gw;
                    if (solid[i])
                    {
                        x0 = Math.Min(x0, gx); y0 = Math.Min(y0, gy); x1 = Math.Max(x1, gx); y1 = Math.Max(y1, gy);
                    }
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            int x = gx + dx, y = gy + dy;
                            if (x < 0 || y < 0 || x >= gw || y >= gh) continue;
                            int j = y * gw + x;
                            if (grown[j] && label[j] == 0) { label[j] = id; stack.Push(j); }
                        }
                }
                var region = new Region { X0 = x0 * cell, Y0 = y0 * cell, X1 = Math.Min(w, (x1 + 1) * cell), Y1 = Math.Min(h, (y1 + 1) * cell) };
                regions.Add(region);
            }
            var result = new List<Region>();
            for (int index = 0; index < regions.Count; index++)
            {
                var r = regions[index];
                int id = index + 1;
                // Only this sprite's own pixels count; a neighbour may reach into its box.
                Func<int, int, bool> mine = (x, y) => label[(y / cell) * gw + x / cell] == id && alpha(x, y) >= threshold;
                if (r.X1 <= r.X0 || (double)(r.X1 - r.X0) * (r.Y1 - r.Y0) < minArea * w * h) continue;
                // Tighten to exact opaque pixels.
                int tx0 = r.X1, ty0 = r.Y1, tx1 = r.X0, ty1 = r.Y0;
                for (int y = r.Y0; y < r.Y1; y++)
                    for (int x = r.X0; x < r.X1; x++)
                        if (mine(x, y)) { tx0 = Math.Min(tx0, x); ty0 = Math.Min(ty0, y); tx1 = Math.Max(tx1, x); ty1 = Math.Max(ty1, y); }
                if (tx1 < tx0) continue;
                r.X0 = tx0; r.Y0 = ty0; r.X1 = tx1 + 1; r.Y1 = ty1 + 1;
                // Stem: mean x of this sprite's opaque pixels in the bottom 1.5% of the sprite.
                int band = Math.Max(2, (int)((r.Y1 - r.Y0) * 0.015));
                double sum = 0; int n = 0;
                for (int y = r.Y1 - band; y < r.Y1; y++)
                    for (int x = r.X0; x < r.X1; x++)
                        if (mine(x, y)) { sum += x; n++; }
                r.StemX = n > 0 ? sum / n + 0.5 : (r.X0 + r.X1) / 2.0;
                r.StemY = r.Y1;
                result.Add(r);
            }
            result.Sort((a, b) => a.Y0 / (h / 4) != b.Y0 / (h / 4) ? a.Y0.CompareTo(b.Y0) : a.X0.CompareTo(b.X0));

            if (!string.IsNullOrEmpty(preview))
            {
                const int size = 768;
                using (var thumb = new Bitmap(size, size))
                using (var g = Graphics.FromImage(thumb))
                using (var font = new Font("Segoe UI", 11, FontStyle.Bold))
                {
                    g.Clear(Color.FromArgb(40, 40, 48));
                    g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBilinear;
                    g.DrawImage(bitmap, 0, 0, size, size);
                    float sx = size / (float)w, sy = size / (float)h;
                    for (int i = 0; i < result.Count; i++)
                    {
                        var r = result[i];
                        g.DrawRectangle(Pens.Magenta, r.X0 * sx, r.Y0 * sy, (r.X1 - r.X0) * sx, (r.Y1 - r.Y0) * sy);
                        g.FillEllipse(Brushes.Yellow, (float)r.StemX * sx - 4, (float)r.StemY * sy - 4, 8, 8);
                        g.DrawString((i + 1).ToString(), font, Brushes.White, r.X0 * sx + 2, r.Y0 * sy + 2);
                    }
                    thumb.Save(preview, ImageFormat.Png);
                }
            }
            return new Result { Width = w, Height = h, Regions = result };
        }
    }
}
'@

foreach ($image in $Images) {
    $full = (Resolve-Path -LiteralPath $image).Path
    $preview = $null
    if ($PreviewDirectory) {
        New-Item -ItemType Directory -Force $PreviewDirectory | Out-Null
        $preview = Join-Path $PreviewDirectory (([IO.Path]::GetFileNameWithoutExtension($full)) + '-regions.png')
    }
    $scan = [AtlasScan]::Find($full, $Cell, $AlphaThreshold, $MergeCells, $MinArea, $preview)
    $w = $scan.Width; $h = $scan.Height; $regions = $scan.Regions
    Write-Output "# $([IO.Path]::GetFileName($full)) ${w}x${h} aspect $([math]::Round($w / $h, 4)): $($regions.Count) regions"
    $i = 0
    foreach ($r in $regions) {
        $i++
        $rect = @(($r.X0 / $w), ($r.Y0 / $h), ($r.X1 / $w), ($r.Y1 / $h)) | ForEach-Object { [math]::Round($_, 4) }
        $stem = @(($r.StemX / $w), ($r.StemY / $h)) | ForEach-Object { [math]::Round($_, 4) }
        Write-Output ('"r{0}": {{ "rect": [{1}], "stem": [{2}] }},' -f $i, ($rect -join ', '), ($stem -join ', '))
    }
}
