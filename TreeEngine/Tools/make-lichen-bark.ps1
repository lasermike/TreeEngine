[CmdletBinding()]
param(
    # Folder holding bark_furrowed_color/normal/roughness.jpg (Game/Resources/Nature); the output goes there too.
    [string]$Nature = (Join-Path $PSScriptRoot '..\Game\Resources\Nature'),
    [int]$Seed = 7,
    # Share of the bark the lichen covers, 0 to 1.
    [double]$Cover = 0.28
)
# Makes bark_lichen_color/normal/roughness.jpg from the furrowed bark: the Douglas fir, hemlock and alder bark in
# photos of the south Puget Sound site are grey, flecked with pale grey-green crustose lichen. The bark is desaturated
# to a grey-brown, and lichen patches (tileable fractal noise, favouring the raised plates, which are the brighter
# texels) are painted over it: pale grey-green, flatter in the normal map, rough. The maps tile like the source.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class LichenBark
{
    static byte[] Read(string path, out int w, out int h)
    {
        using (var src = new Bitmap(path))
        using (var b = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppArgb))
        {
            using (var g = Graphics.FromImage(b)) g.DrawImage(src, 0, 0, src.Width, src.Height);
            w = b.Width; h = b.Height;
            var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var px = new byte[w * h * 4];
            for (int y = 0; y < h; y++) Marshal.Copy(d.Scan0 + y * d.Stride, px, y * w * 4, w * 4);
            b.UnlockBits(d);
            return px;
        }
    }

    static void Write(string path, byte[] px, int w, int h)
    {
        using (var b = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        {
            var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            for (int y = 0; y < h; y++) Marshal.Copy(px, y * w * 4, d.Scan0 + y * d.Stride, w * 4);
            b.UnlockBits(d);
            var codec = Array.Find(ImageCodecInfo.GetImageEncoders(), c => c.MimeType == "image/jpeg");
            var args = new EncoderParameters(1);
            args.Param[0] = new EncoderParameter(Encoder.Quality, 92L);
            b.Save(path, codec, args);
        }
    }

    // Periodic value noise: the lattice wraps every `period` cells, so the maps tile.
    static double Lattice(int x, int y, int period, int seed)
    {
        x = ((x % period) + period) % period; y = ((y % period) + period) % period;
        uint h = (uint)(x * 374761393 + y * 668265263 + seed * 1442695040);
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xffff) / 65535.0;
    }

    static double Noise(double u, double v, int period, int seed)
    {
        double x = u * period, y = v * period;
        int ix = (int)Math.Floor(x), iy = (int)Math.Floor(y);
        double fx = x - ix, fy = y - iy;
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
        double a = Lattice(ix, iy, period, seed), b = Lattice(ix + 1, iy, period, seed);
        double c = Lattice(ix, iy + 1, period, seed), d = Lattice(ix + 1, iy + 1, period, seed);
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    }

    static double Fbm(double u, double v, int seed)
    {
        double total = 0, amp = 0.5;
        int period = 40;
        for (int o = 0; o < 4; o++) { total += amp * Noise(u, v, period, seed + o * 31); amp *= 0.5; period *= 2; }
        return total;
    }

    public static void Make(string folder, int seed, double cover)
    {
        int w, h, nw, nh, rw, rh;
        var color = Read(System.IO.Path.Combine(folder, "bark_furrowed_color.jpg"), out w, out h);
        var normal = Read(System.IO.Path.Combine(folder, "bark_furrowed_normal.jpg"), out nw, out nh);
        var rough = Read(System.IO.Path.Combine(folder, "bark_furrowed_roughness.jpg"), out rw, out rh);
        if (nw != w || nh != h || rw != w || rh != h) throw new Exception("the furrowed bark maps differ in size");
        var lum = new double[w * h];
        double mean = 0;
        for (int i = 0; i < w * h; i++) { lum[i] = (0.2126 * color[i * 4 + 2] + 0.7152 * color[i * 4 + 1] + 0.0722 * color[i * 4]) / 255; mean += lum[i]; }
        mean /= w * h;
        double threshold = 0.62 - 0.3 * cover;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int i = y * w + x, p = i * 4;
                double u = (double)x / w, v = (double)y / h;
                // Patches favour the raised plates (brighter than the mean) and spare the dark furrows.
                double n = Fbm(u, v, seed) + 0.35 * (lum[i] - mean);
                double speck = Noise(u, v, 96, seed + 7);
                double m = 0.8 * Math.Max(Clamp((n - threshold) / 0.05), speck > 0.94 ? 0.8 * Clamp((speck - 0.94) / 0.03) : 0);
                // Bark: desaturated to grey-brown (the tints per species colour it).
                double b = color[p], g = color[p + 1], r = color[p + 2];
                double grey = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                r = grey + 0.3 * (r - grey); g = grey + 0.3 * (g - grey); b = grey + 0.3 * (b - grey);
                // Lichen: pale grey-green, mottled.
                double shade = 0.88 + 0.12 * Fbm(u * 4, v * 4, seed + 3) + 0.1 * (lum[i] - mean);
                double lr = 150 * shade, lg = 156 * shade, lb = 140 * shade;
                color[p + 2] = Byte(r + (lr - r) * m);
                color[p + 1] = Byte(g + (lg - g) * m);
                color[p] = Byte(b + (lb - b) * m);
                // Flatter where lichen covers the bark; rough there.
                normal[p + 2] = Byte(normal[p + 2] + (128 - normal[p + 2]) * 0.6 * m);
                normal[p + 1] = Byte(normal[p + 1] + (128 - normal[p + 1]) * 0.6 * m);
                normal[p] = Byte(normal[p] + (255 - normal[p]) * 0.6 * m);
                for (int c = 0; c < 3; c++) rough[p + c] = Byte(rough[p + c] + (245 - rough[p + c]) * m);
            }
        Write(System.IO.Path.Combine(folder, "bark_lichen_color.jpg"), color, w, h);
        Write(System.IO.Path.Combine(folder, "bark_lichen_normal.jpg"), normal, w, h);
        Write(System.IO.Path.Combine(folder, "bark_lichen_roughness.jpg"), rough, w, h);
    }

    static double Clamp(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
    static byte Byte(double v) { return (byte)(v < 0 ? 0 : v > 255 ? 255 : Math.Round(v)); }
}
'@
$folder = (Resolve-Path -LiteralPath $Nature).Path
[LichenBark]::Make($folder, $Seed, $Cover)
Write-Host "Wrote bark_lichen_color/normal/roughness.jpg in $folder"
