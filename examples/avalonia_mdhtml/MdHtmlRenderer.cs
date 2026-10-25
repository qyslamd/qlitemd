using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace AvaloniaMdhtml;

internal enum MdHtmlAssetKind
{
    Formula = 0,
    Mermaid = 1,
}

internal sealed record MdHtmlAsset(string Id, string Svg, int Width,
                                   int Height, bool Display,
                                   MdHtmlAssetKind Kind);

internal sealed record MdHtmlCodeBlock(string Id, string Language, string Code);

internal sealed record MdHtmlRenderResult(
    string Html, IReadOnlyList<MdHtmlAsset> Assets,
    IReadOnlyDictionary<string, MdHtmlCodeBlock> CodeBlocks);

internal sealed class MdHtmlRenderer : IDisposable
{
    private static readonly MdHtmlNative.ProgressCallback ProgressThunk =
        OnProgress;

    private sealed class ProgressState
    {
        public Action<int, int, int>? Callback { get; init; }
    }

    private IntPtr pipeline_;

    public MdHtmlRenderer()
    {
        var abi = MdHtmlNative.mdhtml_abi_version();
        if (abi != MdHtmlNative.AbiVersion)
        {
            throw new InvalidOperationException(
                $"qlitemd_mdhtml_c ABI {abi} does not match the binding " +
                $"({MdHtmlNative.AbiVersion}).");
        }
        pipeline_ = MdHtmlNative.mdhtml_pipeline_create();
        if (pipeline_ == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "mdhtml_pipeline_create returned NULL.");
        }
    }

    public string Version =>
        MdHtmlNative.Utf8(MdHtmlNative.mdhtml_version_string()) ?? "?";

    public async Task<MdHtmlRenderResult> RenderAsync(
        string markdown, bool dark, Action<int, int, int>? progress)
    {
        if (pipeline_ == IntPtr.Zero)
        {
            throw new ObjectDisposedException(nameof(MdHtmlRenderer));
        }

        var options = new MdHtmlNative.RenderOptions();
        MdHtmlNative.mdhtml_render_options_init(ref options);
        options.theme = dark ? 1 : 0;
        // Same foreground colours as md_viewer's theme formula colours.
        options.formula_color_rgba = dark ? 0xE6EDF3FFu : 0x24292EFFu;
        options.formula_enabled = 1;
        options.mermaid_enabled = 1;
        options.code_line_numbers = 1;

        var state = new ProgressState { Callback = progress };
        var stateHandle = GCHandle.Alloc(state);
        var job = IntPtr.Zero;
        try
        {
            job = MdHtmlNative.mdhtml_render_async(
                pipeline_, markdown, ref options, ProgressThunk,
                GCHandle.ToIntPtr(stateHandle));
            if (job == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "mdhtml_render_async returned NULL.");
            }
            var raw = await Task.Run(() => MdHtmlNative.mdhtml_job_wait(job));
            if (raw == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "mdhtml_job_wait returned NULL.");
            }
            try
            {
                if (MdHtmlNative.mdhtml_result_ok(raw) == 0)
                {
                    var error =
                        MdHtmlNative.Utf8(MdHtmlNative.mdhtml_result_error(raw)) ??
                        "render failed";
                    throw new InvalidOperationException(error);
                }
                return ReadResult(raw);
            }
            finally
            {
                MdHtmlNative.mdhtml_result_destroy(raw);
            }
        }
        finally
        {
            if (job != IntPtr.Zero)
            {
                MdHtmlNative.mdhtml_job_destroy(job);
            }
            stateHandle.Free();
        }
    }

    private static void OnProgress(IntPtr userData, int completed, int total,
                                   int inFlight)
    {
        if (userData == IntPtr.Zero)
        {
            return;
        }
        var handle = GCHandle.FromIntPtr(userData);
        if (handle.Target is ProgressState state)
        {
            state.Callback?.Invoke(completed, total, inFlight);
        }
    }

    private static MdHtmlRenderResult ReadResult(IntPtr raw)
    {
        var html = MdHtmlNative.Utf8(MdHtmlNative.mdhtml_result_html(raw)) ??
                   string.Empty;
        var assets = new List<MdHtmlAsset>();
        var assetCount = MdHtmlNative.mdhtml_result_asset_count(raw);
        for (nuint i = 0; i < assetCount; ++i)
        {
            var index = new UIntPtr(i);
            var id = MdHtmlNative.Utf8(
                MdHtmlNative.mdhtml_result_asset_id(raw, index));
            var svg = MdHtmlNative.Utf8(
                MdHtmlNative.mdhtml_result_asset_svg(raw, index));
            if (id is null || svg is null)
            {
                continue;
            }
            assets.Add(new MdHtmlAsset(
                id, svg,
                MdHtmlNative.mdhtml_result_asset_width(raw, index),
                MdHtmlNative.mdhtml_result_asset_height(raw, index),
                MdHtmlNative.mdhtml_result_asset_display(raw, index) != 0,
                MdHtmlNative.mdhtml_result_asset_kind(raw, index) == 1
                    ? MdHtmlAssetKind.Mermaid
                    : MdHtmlAssetKind.Formula));
        }

        var codeBlocks =
            new Dictionary<string, MdHtmlCodeBlock>(StringComparer.Ordinal);
        var codeCount = MdHtmlNative.mdhtml_result_code_block_count(raw);
        for (nuint i = 0; i < codeCount; ++i)
        {
            var index = new UIntPtr(i);
            var id = MdHtmlNative.Utf8(
                MdHtmlNative.mdhtml_result_code_block_id(raw, index));
            var language = MdHtmlNative.Utf8(
                MdHtmlNative.mdhtml_result_code_block_language(raw, index)) ??
                string.Empty;
            var code = MdHtmlNative.Utf8(
                MdHtmlNative.mdhtml_result_code_block_code(raw, index)) ??
                string.Empty;
            if (id is null)
            {
                continue;
            }
            codeBlocks[id] = new MdHtmlCodeBlock(id, language, code);
        }
        return new MdHtmlRenderResult(html, assets, codeBlocks);
    }

    public void Dispose()
    {
        if (pipeline_ != IntPtr.Zero)
        {
            MdHtmlNative.mdhtml_pipeline_destroy(pipeline_);
            pipeline_ = IntPtr.Zero;
        }
    }
}
