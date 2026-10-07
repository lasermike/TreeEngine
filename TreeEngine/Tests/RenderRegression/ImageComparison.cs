using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace TreeRenderTests
{
    public sealed class ComparisonResult
    {
        public bool SameDimensions;
        public int ActualWidth, ActualHeight, GoldWidth, GoldHeight;
        public double Rms, MeanAbsoluteError, MaxError;
        public long ChangedPixels;
    }

    public static class ImageComparison
    {
        private static Bitmap RgbBitmap(Image image)
        {
            Bitmap bitmap = new Bitmap(image.Width, image.Height, PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(bitmap))
                graphics.DrawImageUnscaled(image, 0, 0);
            return bitmap;
        }

        // Compare displayed RGB code values, normalized to [0,1]. Ignore alpha.
        public static ComparisonResult Compare(string goldPath, string actualPath, string diffPath)
        {
            using (Image goldImage = Image.FromFile(goldPath))
            using (Image actualImage = Image.FromFile(actualPath))
            {
                ComparisonResult result = new ComparisonResult();
                result.GoldWidth = goldImage.Width; result.GoldHeight = goldImage.Height;
                result.ActualWidth = actualImage.Width; result.ActualHeight = actualImage.Height;
                result.SameDimensions = goldImage.Width == actualImage.Width && goldImage.Height == actualImage.Height;
                if (!result.SameDimensions) return result;

                using (Bitmap gold = RgbBitmap(goldImage))
                using (Bitmap actual = RgbBitmap(actualImage))
                using (Bitmap diff = new Bitmap(actual.Width, actual.Height, PixelFormat.Format32bppArgb))
                {
                    Rectangle area = new Rectangle(0, 0, actual.Width, actual.Height);
                    BitmapData goldData = gold.LockBits(area, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                    BitmapData actualData = null, diffData = null;
                    try
                    {
                        actualData = actual.LockBits(area, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                        diffData = diff.LockBits(area, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                        byte[] goldRow = new byte[actual.Width * 4];
                        byte[] actualRow = new byte[goldRow.Length], diffRow = new byte[goldRow.Length];
                        double squares = 0, absolute = 0;
                        int maximum = 0;
                        for (int y = 0; y < actual.Height; y++)
                        {
                            Marshal.Copy(IntPtr.Add(goldData.Scan0, y * goldData.Stride), goldRow, 0, goldRow.Length);
                            Marshal.Copy(IntPtr.Add(actualData.Scan0, y * actualData.Stride), actualRow, 0, actualRow.Length);
                            for (int x = 0; x < actual.Width; x++)
                            {
                                bool changed = false;
                                for (int channel = 0; channel < 3; channel++)
                                {
                                    int index = x * 4 + channel;
                                    int error = Math.Abs(goldRow[index] - actualRow[index]);
                                    squares += error * error; absolute += error;
                                    maximum = Math.Max(maximum, error);
                                    changed |= error != 0;
                                    diffRow[index] = (byte)Math.Min(255, error * 4);
                                }
                                diffRow[x * 4 + 3] = 255;
                                if (changed) result.ChangedPixels++;
                            }
                            Marshal.Copy(diffRow, 0, IntPtr.Add(diffData.Scan0, y * diffData.Stride), diffRow.Length);
                        }
                        double channels = (double)actual.Width * actual.Height * 3;
                        result.Rms = Math.Sqrt(squares / channels) / 255.0;
                        result.MeanAbsoluteError = absolute / channels / 255.0;
                        result.MaxError = maximum / 255.0;
                    }
                    finally
                    {
                        gold.UnlockBits(goldData);
                        if (actualData != null) actual.UnlockBits(actualData);
                        if (diffData != null) diff.UnlockBits(diffData);
                    }
                    diff.Save(diffPath, ImageFormat.Png);
                    return result;
                }
            }
        }
    }
}
