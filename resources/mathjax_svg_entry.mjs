// MathJax tex→svg 的打包入口（无 DOM 环境）。
//
// 产物同目录的 mathjax-svg.min.js 已入库：它是纯 JS，与平台无关，Windows 与
// Linux 共用同一份（实测两边输出的 SVG 逐字节一致），渲染时由 qjsc 在构建期
// 编成字节码（见 src/markdown/CMakeLists.txt），平时不需要 node。
// 只有升级 MathJax 时才重跑（node 与依赖的版本钉在下面）：
//
//   npm install mathjax-full@3.2.1 esbuild@0.28.2
//   <NPM_DIR>/node_modules/.bin/esbuild src/markdown/resources/mathjax_svg_entry.mjs \
//     --bundle --minify --format=iife \
//     --alias:mathjax-full=<NPM_DIR>/node_modules/mathjax-full \
//     --define:process.env.NODE_ENV='"production"' \
//     --define:PACKAGE_VERSION='"3.2.1"' --outfile=mathjax-svg.min.js
//
// 两个 --define 都不能省：mathjax-full 的版本探测写着 eval("require") 去读
// package.json，而 QuickJS 里没有 require。
//
// 重建后对 md5：bfdd6f73560f2e17c09b250cb84aa24c（1618089 字节，实测一致）。
//
// 入口本身的要点：
//  - 不用 es5/ 组件包（那些面向浏览器，引用 window/document），从 js/ 源码组
//  - liteAdaptor 是纯 JS 的极简 DOM，专为 node/嵌入式环境设计
//  - fontCache:'none'：每段 SVG 自带字形定义（global 会把路径放第一段 svg，
//    单独渲染后面几段会缺字）
//  - 挂到 globalThis.MathJaxAdapter，宿主 JS_Eval 之后直接调 convert()
import { mathjax } from 'mathjax-full/js/mathjax.js';
import { TeX } from 'mathjax-full/js/input/tex.js';
import { SVG } from 'mathjax-full/js/output/svg.js';
import { liteAdaptor } from 'mathjax-full/js/adaptors/liteAdaptor.js';
import { RegisterHTMLHandler } from 'mathjax-full/js/handlers/html.js';

const adaptor = liteAdaptor();
RegisterHTMLHandler(adaptor);
const tex = new TeX();
const svg = new SVG({ fontCache: 'none' });
const doc = mathjax.document('', { InputJax: tex, OutputJax: svg });

globalThis.MathJaxAdapter = {
  // 返回独立可渲染的 <svg> 字符串；display=true 时按 display 公式排版
  convert: function (texCode, display) {
    const node = doc.convert(texCode, {
      display: !!display,
      em: 16,
      ex: 8,
      containerWidth: 1280,
    });
    return adaptor.outerHTML(node);
  },
};
