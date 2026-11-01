[CmdletBinding()]
param(
    # Manifest describing the atlases to build; see Game/Resources/Nature/canyon-atlases.json.
    [Parameter(Mandatory = $true)][string]$Manifest,
    # Folder the manifest's source images are relative to (the Nature Pack textures).
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    # Optional folder for previews that draw each region's box and stem.
    [string]$PreviewDirectory
)

# Composes leaf sprites from several source atlases into new color, normal and roughness atlases that share one
# layout, so a scene spends three texture slots per atlas instead of three per source image.
#
# For each sprite the tool keeps only its own connected pixels, rotates it upright (turning normal-map vectors too),
# scales it to a shared texel density times its weight, and shelf-packs it at the largest scale that fits. Color is
# bled into transparent texels so mipmaps do not darken leaf edges. It writes the maps next to the manifest and a
# <atlas>.regions.json holding the scene "atlases" entry: each region's rect and stem in 0-1 UV.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class LeafPacker
{
    public class Pixels
    {
        public int W, H; public byte[] Data; // BGRA, 4 bytes per pixel.
        public Pixels(int w, int h) { W = w; H = h; Data = new byte[w * h * 4]; }
        public static Pixels FromBitmap(Bitmap b)
        {
            var p = new Pixels(b.Width, b.Height);
            using (var c = new Bitmap(b.Width, b.Height, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(c)) { g.CompositingMode = CompositingMode.SourceCopy; g.DrawImage(b, 0, 0, b.Width, b.Height); }
                var d = c.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                for (int y = 0; y < b.Height; y++) Marshal.Copy(d.Scan0 + y * d.Stride, p.Data, y * b.Width * 4, b.Width * 4);
                c.UnlockBits(d);
            }
            return p;
        }
        public Bitmap ToBitmap()
        {
            var b = new Bitmap(W, H, PixelFormat.Format32bppArgb);
            var d = b.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            for (int y = 0; y < H; y++) Marshal.Copy(Data, y * W * 4, d.Scan0 + y * d.Stride, W * 4);
            b.UnlockBits(d);
            return b;
        }
        public Pixels Crop(int x0, int y0, int w, int h)
        {
            var p = new Pixels(w, h);
            for (int y = 0; y < h; y++) Buffer.BlockCopy(Data, ((y0 + y) * W + x0) * 4, p.Data, y * w * 4, w * 4);
            return p;
        }
        // Clockwise quarter turns.
        public Pixels Rotate(int quarters)
        {
            quarters = ((quarters % 4) + 4) % 4;
            if (quarters == 0) return this;
            var p = (quarters % 2 == 1) ? new Pixels(H, W) : new Pixels(W, H);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    int nx, ny;
                    if (quarters == 1) { nx = H - 1 - y; ny = x; }
                    else if (quarters == 2) { nx = W - 1 - x; ny = H - 1 - y; }
                    else { nx = y; ny = W - 1 - x; }
                    Buffer.BlockCopy(Data, (y * W + x) * 4, p.Data, (ny * p.W + nx) * 4, 4);
                }
            return p;
        }
    }

    public class Sprite
    {
        public string Name;
        public Pixels Color, Normal, Rough;
        public double StemX, StemY;      // Pixels in Color, after isolation and rotation.
        public double Density;           // Packed pixels per sprite pixel at scale 1.
        public int X, Y, PW, PH;         // Packed placement.
    }

    static Pixels Resample(Pixels src, int w, int h, bool keepAlpha)
    {
        using (var b = src.ToBitmap())
        using (var o = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        {
            using (var g = Graphics.FromImage(o))
            {
                g.CompositingMode = CompositingMode.SourceCopy;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                using (var attr = new ImageAttributes())
                {
                    attr.SetWrapMode(WrapMode.TileFlipXY);
                    g.DrawImage(b, new Rectangle(0, 0, w, h), 0, 0, src.W, src.H, GraphicsUnit.Pixel, attr);
                }
            }
            var p = Pixels.FromBitmap(o);
            if (!keepAlpha) for (int i = 3; i < p.Data.Length; i += 4) p.Data[i] = 255;
            return p;
        }
    }

    // Builds one sprite. rect is in source UV; maps are resampled to the color map's resolution.
    public static Sprite Load(string name, Bitmap color, Bitmap normal, Bitmap rough, double[] rect, int quarters,
        bool isolate, double[] stem, double weight)
    {
        int cw = color.Width, ch = color.Height;
        int x0 = (int)Math.Floor(rect[0] * cw), y0 = (int)Math.Floor(rect[1] * ch);
        int x1 = (int)Math.Ceiling(rect[2] * cw), y1 = (int)Math.Ceiling(rect[3] * ch);
        x0 = Math.Max(0, x0); y0 = Math.Max(0, y0); x1 = Math.Min(cw, x1); y1 = Math.Min(ch, y1);
        int w = x1 - x0, h = y1 - y0;
        var c = Pixels.FromBitmap(color).Crop(x0, y0, w, h);
        Pixels n = normal != null ? CropScaled(normal, rect, w, h) : Flat(w, h, 255, 128, 128); // BGRA: flat normal (128,128,255)
        Pixels r = rough != null ? CropScaled(rough, rect, w, h) : Flat(w, h, 204, 204, 204);

        // Keep the largest alpha-connected component (with a 3 px bridge), so neighbours overlapping the rect drop out.
        var keep = new bool[w * h];
        if (isolate) Isolate(c, keep); else for (int i = 0; i < keep.Length; i++) keep[i] = c.Data[i * 4 + 3] >= 32;
        int tx0 = w, ty0 = h, tx1 = -1, ty1 = -1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                if (!keep[y * w + x]) { c.Data[(y * w + x) * 4 + 3] = 0; continue; }
                tx0 = Math.Min(tx0, x); ty0 = Math.Min(ty0, y); tx1 = Math.Max(tx1, x); ty1 = Math.Max(ty1, y);
            }
        if (tx1 < 0) throw new Exception(name + ": no opaque pixels in rect");
        int tw = tx1 - tx0 + 1, th = ty1 - ty0 + 1;
        c = c.Crop(tx0, ty0, tw, th); n = n.Crop(tx0, ty0, tw, th); r = r.Crop(tx0, ty0, tw, th);
        double sx = 0, sy = 0;
        if (stem != null) { sx = stem[0] * cw - x0 - tx0; sy = stem[1] * ch - y0 - ty0; }

        c = c.Rotate(quarters); r = r.Rotate(quarters); n = RotateNormals(n.Rotate(quarters), quarters);
        if (stem != null)
        {
            for (int i = 0; i < ((quarters % 4) + 4) % 4; i++) { double t = sx; sx = th - sy; sy = t; int s = tw; tw = th; th = s; }
        }
        var sprite = new Sprite { Name = name, Color = c, Normal = n, Rough = r, Density = weight * 2048.0 / Math.Max(cw, ch) };
        if (stem != null) { sprite.StemX = sx; sprite.StemY = sy; }
        else
        {
            // Mean x of the opaque pixels in the bottom 1.5% of the upright sprite.
            int band = Math.Max(2, (int)(c.H * 0.015)); double sum = 0; int count = 0;
            for (int y = c.H - band; y < c.H; y++)
                for (int x = 0; x < c.W; x++)
                    if (c.Data[(y * c.W + x) * 4 + 3] >= 32) { sum += x + 0.5; count++; }
            sprite.StemX = count > 0 ? sum / count : c.W / 2.0; sprite.StemY = c.H;
        }
        return sprite;
    }

    static Pixels Flat(int w, int h, byte b, byte g, byte r)
    {
        var p = new Pixels(w, h);
        for (int i = 0; i < w * h; i++) { p.Data[i * 4] = b; p.Data[i * 4 + 1] = g; p.Data[i * 4 + 2] = r; p.Data[i * 4 + 3] = 255; }
        return p;
    }

    static Pixels CropScaled(Bitmap map, double[] rect, int w, int h)
    {
        int x0 = (int)Math.Floor(rect[0] * map.Width), y0 = (int)Math.Floor(rect[1] * map.Height);
        int x1 = (int)Math.Ceiling(rect[2] * map.Width), y1 = (int)Math.Ceiling(rect[3] * map.Height);
        x0 = Math.Max(0, x0); y0 = Math.Max(0, y0); x1 = Math.Min(map.Width, x1); y1 = Math.Min(map.Height, y1);
        var p = Pixels.FromBitmap(map).Crop(x0, y0, x1 - x0, y1 - y0);
        return (p.W == w && p.H == h) ? p : Resample(p, w, h, false);
    }

    // Tangent-space XY turns with the image. Green is treated as up (OpenGL convention).
    static Pixels RotateNormals(Pixels n, int quarters)
    {
        quarters = ((quarters % 4) + 4) % 4;
        if (quarters == 0) return n;
        for (int i = 0; i < n.W * n.H; i++)
        {
            double nx = n.Data[i * 4 + 2] / 127.5 - 1, ny = n.Data[i * 4 + 1] / 127.5 - 1, rx, ry;
            if (quarters == 1) { rx = ny; ry = -nx; } else if (quarters == 2) { rx = -nx; ry = -ny; } else { rx = -ny; ry = nx; }
            n.Data[i * 4 + 2] = (byte)Math.Max(0, Math.Min(255, Math.Round((rx + 1) * 127.5)));
            n.Data[i * 4 + 1] = (byte)Math.Max(0, Math.Min(255, Math.Round((ry + 1) * 127.5)));
        }
        return n;
    }

    static void Isolate(Pixels c, bool[] keep)
    {
        int w = c.W, h = c.H;
        var solid = new bool[w * h];
        for (int i = 0; i < w * h; i++) solid[i] = c.Data[i * 4 + 3] >= 32;
        // Components on solid pixels, joined across gaps of up to 3 px.
        var label = new int[w * h]; var sizes = new List<int> { 0 }; var stack = new Stack<int>();
        for (int s = 0; s < w * h; s++)
        {
            if (!solid[s] || label[s] != 0) continue;
            int id = sizes.Count, size = 0; label[s] = id; stack.Push(s);
            while (stack.Count > 0)
            {
                int i = stack.Pop(), px = i % w, py = i / w; size++;
                for (int dy = -3; dy <= 3; dy++)
                    for (int dx = -3; dx <= 3; dx++)
                    {
                        int x = px + dx, y = py + dy;
                        if (x < 0 || y < 0 || x >= w || y >= h) continue;
                        int j = y * w + x;
                        if (solid[j] && label[j] == 0) { label[j] = id; stack.Push(j); }
                    }
            }
            sizes.Add(size);
        }
        int best = 1;
        for (int i = 2; i < sizes.Count; i++) if (sizes[i] > sizes[best]) best = i;
        for (int i = 0; i < w * h; i++) keep[i] = label[i] == best;
    }

    // Shelf packing, tallest first, at the largest scale that fits; returns the scale.
    public static double Pack(List<Sprite> sprites, int size, int padding)
    {
        double lo = 0, hi = 4;
        for (int iteration = 0; iteration < 40; iteration++)
        {
            double mid = (lo + hi) / 2;
            if (TryPack(sprites, size, padding, mid)) lo = mid; else hi = mid;
        }
        if (!TryPack(sprites, size, padding, lo)) throw new Exception("sprites do not fit");
        return lo;
    }

    static bool TryPack(List<Sprite> sprites, int size, int padding, double scale)
    {
        foreach (var s in sprites)
        {
            s.PW = Math.Max(1, (int)Math.Round(s.Color.W * s.Density * scale));
            s.PH = Math.Max(1, (int)Math.Round(s.Color.H * s.Density * scale));
        }
        var order = new List<Sprite>(sprites);
        order.Sort((a, b) => b.PH.CompareTo(a.PH));
        int x = padding, y = padding, shelf = 0;
        foreach (var s in order)
        {
            if (s.PW + 2 * padding > size) return false;
            if (x + s.PW + padding > size) { x = padding; y += shelf + padding; shelf = 0; }
            if (y + s.PH + padding > size) return false;
            s.X = x; s.Y = y; x += s.PW + padding; shelf = Math.Max(shelf, s.PH);
        }
        return true;
    }

    public static void Compose(List<Sprite> sprites, int size, string colorPath, string normalPath, string roughPath)
    {
        var color = new Pixels(size, size);
        var normal = Flat(size, size, 255, 128, 128);
        var rough = Flat(size, size, 204, 204, 204);
        foreach (var s in sprites)
        {
            Blit(Resample(s.Color, s.PW, s.PH, true), color, s.X, s.Y);
            Blit(Resample(s.Normal, s.PW, s.PH, false), normal, s.X, s.Y);
            Blit(Resample(s.Rough, s.PW, s.PH, false), rough, s.X, s.Y);
        }
        Bleed(color);
        using (var b = color.ToBitmap()) b.Save(colorPath, ImageFormat.Png);
        using (var b = normal.ToBitmap()) b.Save(normalPath, ImageFormat.Png);
        using (var b = rough.ToBitmap()) b.Save(roughPath, ImageFormat.Png);
    }

    static void Blit(Pixels src, Pixels dst, int x0, int y0)
    {
        for (int y = 0; y < src.H; y++) Buffer.BlockCopy(src.Data, y * src.W * 4, dst.Data, ((y0 + y) * dst.W + x0) * 4, src.W * 4);
    }

    // Fills transparent texels' color from their nearest opaque neighbours, keeping alpha, so filtering and mips
    // average real leaf color instead of black.
    static void Bleed(Pixels p)
    {
        int w = p.W, h = p.H;
        var known = new bool[w * h];
        for (int i = 0; i < w * h; i++) known[i] = p.Data[i * 4 + 3] >= 16;
        var frontier = new List<int>();
        for (int pass = 0; pass < 24; pass++)
        {
            frontier.Clear();
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    int i = y * w + x;
                    if (known[i]) continue;
                    int r = 0, g = 0, b = 0, n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= w || yy >= h || !known[yy * w + xx]) continue;
                            int j = (yy * w + xx) * 4; b += p.Data[j]; g += p.Data[j + 1]; r += p.Data[j + 2]; n++;
                        }
                    if (n == 0) continue;
                    p.Data[i * 4] = (byte)(b / n); p.Data[i * 4 + 1] = (byte)(g / n); p.Data[i * 4 + 2] = (byte)(r / n);
                    frontier.Add(i);
                }
            if (frontier.Count == 0) break;
            foreach (int i in frontier) known[i] = true;
        }
        // Far from any leaf: the mean leaf color.
        long sr = 0, sg = 0, sb = 0, count = 0;
        for (int i = 0; i < w * h; i++) if (p.Data[i * 4 + 3] >= 16) { sb += p.Data[i * 4]; sg += p.Data[i * 4 + 1]; sr += p.Data[i * 4 + 2]; count++; }
        if (count == 0) return;
        for (int i = 0; i < w * h; i++)
            if (!known[i]) { p.Data[i * 4] = (byte)(sb / count); p.Data[i * 4 + 1] = (byte)(sg / count); p.Data[i * 4 + 2] = (byte)(sr / count); }
    }

    public static void Preview(string colorPath, List<Sprite> sprites, int size, string path)
    {
        const int view = 1024;
        using (var atlas = Image.FromFile(colorPath))
        using (var b = new Bitmap(view, view))
        using (var g = Graphics.FromImage(b))
        using (var font = new Font("Segoe UI", 8))
        {
            g.Clear(System.Drawing.Color.FromArgb(40, 40, 48));
            g.DrawImage(atlas, 0, 0, view, view);
            float k = view / (float)size;
            foreach (var s in sprites)
            {
                g.DrawRectangle(Pens.Magenta, s.X * k, s.Y * k, s.PW * k, s.PH * k);
                float stemX = (float)(s.X + s.StemX * s.PW / s.Color.W) * k, stemY = (float)(s.Y + s.StemY * s.PH / s.Color.H) * k;
                g.FillEllipse(Brushes.Yellow, stemX - 3, stemY - 3, 6, 6);
                g.DrawString(s.Name, font, Brushes.White, s.X * k + 2, s.Y * k + 2);
            }
            b.Save(path, ImageFormat.Png);
        }
    }
}
'@

$manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
$outputRoot = Split-Path $manifestPath
$definition = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$cache = @{}
function Get-Map([string]$relative) {
    if (-not $relative) { return $null }
    if (-not $cache.ContainsKey($relative)) { $cache[$relative] = [Drawing.Bitmap]::FromFile((Join-Path $SourceRoot $relative)) }
    return $cache[$relative]
}
$round = { param($v) [math]::Round([double]$v, 4) }

foreach ($atlas in $definition.atlases) {
    $sprites = New-Object 'System.Collections.Generic.List[LeafPacker+Sprite]'
    foreach ($s in $atlas.sprites) {
        $source = $definition.sources.($s.source)
        if (-not $source) { throw "$($s.name): unknown source '$($s.source)'" }
        $props = $s.PSObject.Properties.Name
        $rotate = if ($props -contains 'rotate') { [int]$s.rotate / 90 } else { 0 }
        $isolate = -not ($props -contains 'isolate') -or [bool]$s.isolate
        $stem = if ($props -contains 'stem') { [double[]]$s.stem } else { $null }
        $weight = if ($props -contains 'weight') { [double]$s.weight } else { 1.0 }
        $sprites.Add([LeafPacker]::Load($s.name, (Get-Map $source.color), (Get-Map $source.normal), (Get-Map $source.roughness),
            [double[]]$s.rect, $rotate, $isolate, $stem, $weight))
    }
    $size = [int]$atlas.size
    $scale = [LeafPacker]::Pack($sprites, $size, [int]$atlas.padding)
    $paths = @('color', 'normal', 'roughness') | ForEach-Object { Join-Path $outputRoot "$($atlas.name)_$_.png" }
    [LeafPacker]::Compose($sprites, $size, $paths[0], $paths[1], $paths[2])
    if ($PreviewDirectory) {
        New-Item -ItemType Directory -Force $PreviewDirectory | Out-Null
        [LeafPacker]::Preview($paths[0], $sprites, $size, (Join-Path $PreviewDirectory "$($atlas.name)-preview.png"))
    }

    $regions = [ordered]@{}
    foreach ($s in $sprites) {
        $stemX = $s.X + $s.StemX * $s.PW / $s.Color.W
        $stemY = $s.Y + $s.StemY * $s.PH / $s.Color.H
        $regions[$s.Name] = [ordered]@{
            rect = @((& $round ($s.X / $size)), (& $round ($s.Y / $size)), (& $round (($s.X + $s.PW) / $size)), (& $round (($s.Y + $s.PH) / $size)))
            stem = @((& $round ([math]::Min($s.X + $s.PW, [math]::Max($s.X, $stemX)) / $size)), (& $round ([math]::Min($s.Y + $s.PH, [math]::Max($s.Y, $stemY)) / $size)))
        }
    }
    $regionsPath = Join-Path $outputRoot "$($atlas.name).regions.json"
    [IO.File]::WriteAllText($regionsPath, ([ordered]@{ regions = $regions } | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    Write-Host ("{0}: {1} sprites at {2:N3}x density, regions in {3}" -f $atlas.name, $sprites.Count, $scale, $regionsPath)
}
foreach ($bitmap in $cache.Values) { $bitmap.Dispose() }
