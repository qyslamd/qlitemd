using System;
using System.Runtime.InteropServices;

namespace AvaloniaMdhtml;

// Flat P/Invoke surface of qlitemd_mdhtml_c.dll. Keep this in sync with
// src/mdhtml/mdhtml_c.h; the ABI version is checked at startup.
internal static class MdHtmlNative
{
    internal const string Library = "qlitemd_mdhtml_c";
    internal const uint AbiVersion = 4;

    [StructLayout(LayoutKind.Sequential)]
    internal struct RenderOptions
    {
        public uint struct_size;
        public int theme;
        public uint formula_color_rgba;
        public int formula_enabled;
        public int mermaid_enabled;
        public int code_line_numbers;
        public int admonition_mode;  // 0 = GitHub, 1 = MPE callouts
        public int mermaid_backend;  // 0 = auto, 1 = merman, 2 = quickjs
        public int formula_backend;  // 0 = auto, 1 = ratex, 2 = quickjs
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ProgressCallback(IntPtr userData, int completed,
                                            int total, int inFlight);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint mdhtml_abi_version();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_version_string();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mdhtml_render_options_init(
        ref RenderOptions options);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_pipeline_create();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mdhtml_pipeline_destroy(IntPtr pipeline);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_render_async(
        IntPtr pipeline, [MarshalAs(UnmanagedType.LPUTF8Str)] string markdown,
        ref RenderOptions options, ProgressCallback? progress,
        IntPtr userData);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mdhtml_job_cancel(IntPtr job);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mdhtml_job_is_finished(IntPtr job);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_job_wait(IntPtr job);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mdhtml_job_destroy(IntPtr job);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mdhtml_result_destroy(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mdhtml_result_ok(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_error(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_html(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr mdhtml_result_asset_count(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_asset_id(IntPtr result,
                                                         UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_asset_svg(IntPtr result,
                                                          UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mdhtml_result_asset_kind(IntPtr result,
                                                        UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mdhtml_result_asset_width(IntPtr result,
                                                         UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mdhtml_result_asset_height(IntPtr result,
                                                          UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mdhtml_result_asset_display(IntPtr result,
                                                           UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr mdhtml_result_code_block_count(
        IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_code_block_id(IntPtr result,
                                                              UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_code_block_language(
        IntPtr result, UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_code_block_code(
        IntPtr result, UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr mdhtml_result_outline_count(IntPtr result);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_outline_anchor(IntPtr result,
                                                               UIntPtr index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr mdhtml_result_outline_id(IntPtr result,
                                                           UIntPtr index);

    internal static string? Utf8(IntPtr value) =>
        value == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(value);
}
