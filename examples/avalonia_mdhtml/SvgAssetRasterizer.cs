using System;
using System.IO;
using System.Text;
using SkiaSharp;
using Svg.Skia;

namespace AvaloniaMdhtml;

internal static class SvgAssetRasterizer
{
    // litehtml's Avalonia backend decodes raster images through SkiaSharp and
    // has no SVG decoder, so formula/Mermaid SVGs are rasterized here.
    public static byte[] ToPng(string svg, int layoutWidth, int layoutHeight,
                               double scale = 2.0)
    {
        if (string.IsNullOrEmpty(svg))
        {
            return Array.Empty<byte>();
        }
        using var document = new SKSvg();
        using var stream = new MemoryStream(Encoding.UTF8.GetBytes(svg));
        document.Load(stream);
        var picture = document.Picture;
        if (picture is null)
        {
            return Array.Empty<byte>();
        }

        var sourceWidth = Math.Max(1.0, picture.CullRect.Width);
        var sourceHeight = Math.Max(1.0, picture.CullRect.Height);
        var width = Math.Max(1, layoutWidth);
        var height = Math.Max(1, layoutHeight);
        var scaleX = width * scale / sourceWidth;
        var scaleY = height * scale / sourceHeight;
        var pixelWidth = Math.Max(1, (int)Math.Ceiling(width * scale));
        var pixelHeight = Math.Max(1, (int)Math.Ceiling(height * scale));

        using var bitmap = new SKBitmap(pixelWidth, pixelHeight,
                                        SKColorType.Bgra8888,
                                        SKAlphaType.Premul);
        using (var canvas = new SKCanvas(bitmap))
        {
            canvas.Clear(SKColors.Transparent);
            canvas.Scale((float)scaleX, (float)scaleY);
            canvas.Translate(-picture.CullRect.Left, -picture.CullRect.Top);
            canvas.DrawPicture(picture);
        }
        using var image = SKImage.FromBitmap(bitmap);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return data?.ToArray() ?? Array.Empty<byte>();
    }
}
