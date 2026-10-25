using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using Avalonia.Threading;
using LiteHtmlSharp.Avalonia;

namespace AvaloniaMdhtml;

public partial class MainWindow : Window
{
    private const string AssetPrefix = "qlitemd-asset:";

    private readonly MdHtmlRenderer renderer_ = new();
    private readonly string lightCss_;
    private readonly string darkCss_;
    private readonly string markdown_;
    private readonly AvaloniaContainer container_;
    private readonly LiteHtmlAvaloniaControl html_;
    private readonly Dictionary<string, byte[]> imageAssets_ =
        new(StringComparer.Ordinal);

    private MdHtmlRenderResult? result_;
    private bool dark_;
    private bool rendering_;
    private readonly string? screenshotPath_;

    public MainWindow() : this(Array.Empty<string>())
    {
    }

    public MainWindow(string[] args)
    {
        InitializeComponent();
        screenshotPath_ = ArgumentValue(args, "--screenshot");
        dark_ = Array.Exists(args, value => value == "--dark");
        var baseCss = LoadResource("base.css");
        lightCss_ = baseCss + "\n" + LoadResource("light.css");
        darkCss_ = baseCss + "\n" + LoadResource("dark.css");
        markdown_ = LoadResource("sample.md");

        container_ = new AvaloniaContainer(lightCss_, GetResourceString,
                                           GetResourceBytes);
        html_ = new LiteHtmlAvaloniaControl(HtmlScrollViewer, container_,
                                            lightCss_, null)
        {
            HorizontalContentAlignment = Avalonia.Layout.HorizontalAlignment.Stretch,
            VerticalContentAlignment = Avalonia.Layout.VerticalAlignment.Stretch,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top,
        };
        if (html_.Content is Control panel)
        {
            // LiteHtmlSharp's panel has VerticalAlignment=Top and a zero
            // desired size; stretch it so its Render pass covers the document.
            panel.HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Stretch;
            panel.VerticalAlignment = Avalonia.Layout.VerticalAlignment.Stretch;
        }
        html_.LinkClicked += OnLinkClicked;

        LightButton.Click += async (_, _) => await SetThemeAsync(false);
        DarkButton.Click += async (_, _) => await SetThemeAsync(true);
        RenderButton.Click += async (_, _) => await RenderAsync();
        FooterText.Text = $"qlitemd_mdhtml_c {renderer_.Version} | " +
                          "LiteHtmlSharp.Avalonia 2.0.7 | " +
                          "SVG rasterized with Svg.Skia";
        Opened += async (_, _) =>
        {
            await RenderAsync();
            if (screenshotPath_ is not null)
            {
                await Task.Delay(2500);
                SaveScreenshot(screenshotPath_);
                Close();
            }
        };
    }

    private async Task SetThemeAsync(bool dark)
    {
        dark_ = dark;
        await RenderAsync();
    }

    private async Task RenderAsync()
    {
        RenderButton.IsEnabled = false;
        rendering_ = true;
        StatusText.Text = "Rendering…";
        try
        {
            var result = await renderer_.RenderAsync(
                markdown_, dark_,
                (completed, total, inFlight) => Dispatcher.UIThread.Post(() =>
                {
                    if (rendering_ && completed < total)
                    {
                        StatusText.Text =
                            $"Rendering {completed}/{total} " +
                            $"(in flight {inFlight})";
                    }
                }));
            result_ = result;
            imageAssets_.Clear();
            foreach (var asset in result.Assets)
            {
                var png = SvgAssetRasterizer.ToPng(asset.Svg, asset.Width,
                                                   asset.Height);
                if (png.Length > 0)
                {
                    imageAssets_[asset.Id] = png;
                }
            }

            container_.Document.SetMasterCSS(dark_ ? darkCss_ : lightCss_);
            var document =
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\">" +
                "</head><body><div class=\"md-page\">" + result.Html +
                "</div></body></html>";
            html_.LoadHtml(document);
            RequestedThemeVariant =
                dark_ ? ThemeVariant.Dark : ThemeVariant.Light;
            StatusText.Text =
                $"Rendered {result.Assets.Count} assets, " +
                $"{result.CodeBlocks.Count} code blocks";
        }
        catch (Exception error)
        {
            StatusText.Text = "Render failed: " + error.Message;
        }
        finally
        {
            rendering_ = false;
            RenderButton.IsEnabled = true;
        }
    }

    private void OnLinkClicked(string url)
    {
        if (url.StartsWith("copy:", StringComparison.Ordinal))
        {
            var id = url.Substring("copy:".Length);
            if (result_?.CodeBlocks.TryGetValue(id, out var block) == true)
            {
                var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
                if (clipboard is not null)
                {
                    _ = clipboard.SetTextAsync(block.Code);
                    StatusText.Text = $"Copied code block {id}";
                }
            }
            return;
        }
        if (Uri.TryCreate(url, UriKind.Absolute, out var uri) &&
            (uri.Scheme == Uri.UriSchemeHttp ||
             uri.Scheme == Uri.UriSchemeHttps))
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            return;
        }
        StatusText.Text = "Link: " + url;
    }

    private string GetResourceString(string resource) => string.Empty;

    private byte[] GetResourceBytes(string resource)
    {
        var id = resource;
        if (id.StartsWith(AssetPrefix, StringComparison.Ordinal))
        {
            id = id.Substring(AssetPrefix.Length);
        }
        if (imageAssets_.TryGetValue(id, out var png))
        {
            return png;
        }
        // litehtml may resolve custom URLs before asking the loader; unknown
        // resources fall back to an empty payload.
        return Array.Empty<byte>();
    }

    private void SaveScreenshot(string path)
    {
        var width = Math.Max(1, (int)Math.Ceiling(Bounds.Width));
        var height = Math.Max(1, (int)Math.Ceiling(Bounds.Height));
        using var bitmap = new RenderTargetBitmap(new PixelSize(width, height),
                                                  new Vector(96, 96));
        bitmap.Render(this);
        bitmap.Save(path);

        var controlWidth = Math.Max(1, (int)Math.Ceiling(html_.Bounds.Width));
        var controlHeight = Math.Max(1, (int)Math.Ceiling(html_.Bounds.Height));
        using var controlBitmap = new RenderTargetBitmap(
            new PixelSize(controlWidth, controlHeight), new Vector(96, 96));
        controlBitmap.Render(html_);
        controlBitmap.Save(path + ".control.png");
    }

    private static string? ArgumentValue(string[] args, string name)
    {
        for (var i = 0; i + 1 < args.Length; ++i)
        {
            if (args[i] == name)
            {
                return args[i + 1];
            }
        }
        return null;
    }

    private static string LoadResource(string name)
    {
        var assembly = Assembly.GetExecutingAssembly();
        using var stream = assembly.GetManifestResourceStream(name) ??
                           throw new InvalidOperationException(
                               "Missing embedded resource " + name);
        using var reader = new StreamReader(stream, Encoding.UTF8);
        return reader.ReadToEnd();
    }
}
