// highlight.js 的打包入口：语法高亮走"QuickJS 里跑 bundle"的同一套管线
// （MathJax/mermaid 同款）。产物 highlight.min.js（66KB）入库；渲染时由
// qjsc 在构建期编成字节码（见 src/markdown/CMakeLists.txt）。纯 JS 与平台
// 无关，平时不需要 node；升级 highlight.js 时才重跑（版本钉在下面）：
//
//   npm install highlight.js@11.12.0 esbuild@0.28.2
//   <NPM_DIR>/node_modules/.bin/esbuild src/markdown/resources/highlight_entry.js \
//     --bundle --format=iife --minify \
//     --alias:highlight.js=<NPM_DIR>/node_modules/highlight.js \
//     --outfile=highlight.min.js
//
// --alias 不能省：入口住在仓库里，没有 node_modules。把入口拷到 node_modules
// 旁边也能建，但入口所在目录的 package.json `type` 会改变 esbuild 的 CJS 互操作
// 包装，产物差 20 字节（行为相同）——要复现入库那份就照上面原样跑。
//
// 入口做的事，按顺序：
//  - 只注册 9 种语言（core 版不带语言包，注册表外的一律拒绝——见 __highlight
//    开头的早退，那是与上游示例唯一的差异：上游示例对未知语言开
//    highlightAuto 自动猜，这里猜错比不高亮更难看，选择纯转义）
//  - ALIAS：围栏标签到 hljs 语言名的映射（py→python、bat→dos…）
//  - CLASS_MAP：hljs-xxx 类名压成本仓 CSS 的短类名（.kw/.str/…），配色改动
//    就不用碰这份 JS
//  - splitSpansPerLine：把跨行 span 在每个换行处闭合再按序重开
//
// 重建后对 md5：439dc3414567e31c1bc58f8172643a04（highlight.min.js，66390 字节）。
// bundle 里能 grep 到 highlightAuto 是 core 库自带的函数定义，不是本入口在调——
// 行为判据见 test_code_highlighter 的 UnknownLanguageFallsBackToPlainEscape

import hljs from 'highlight.js/lib/core';

import cpp from 'highlight.js/lib/languages/cpp';
import python from 'highlight.js/lib/languages/python';
import bash from 'highlight.js/lib/languages/bash';
import dos from 'highlight.js/lib/languages/dos';
import javascript from 'highlight.js/lib/languages/javascript';
import json from 'highlight.js/lib/languages/json';
import xml from 'highlight.js/lib/languages/xml';
import css from 'highlight.js/lib/languages/css';
import sql from 'highlight.js/lib/languages/sql';

hljs.registerLanguage('cpp', cpp);
hljs.registerLanguage('python', python);
hljs.registerLanguage('bash', bash);
hljs.registerLanguage('dos', dos);
hljs.registerLanguage('javascript', javascript);
hljs.registerLanguage('json', json);
hljs.registerLanguage('xml', xml);
hljs.registerLanguage('css', css);
hljs.registerLanguage('sql', sql);

const ALIAS = {
  'c++': 'cpp', 'c': 'cpp', 'cc': 'cpp', 'h': 'cpp', 'hpp': 'cpp',
  'py': 'python',
  'sh': 'bash', 'shell': 'bash', 'zsh': 'bash',
  'cmd': 'dos', 'bat': 'dos', 'batch': 'dos',
  'js': 'javascript', 'jsx': 'javascript', 'mjs': 'javascript',
  'html': 'xml', 'xhtml': 'xml', 'svg': 'xml',
};

// hljs-xxx → 你 CSS 里的短类名（.kw/.str/.num/.cmt）
const CLASS_MAP = {
  // 关键字 / 类型 / 字面量
  keyword: 'kw',
  type: 'ty',
  literal: 'lit',
  built_in: 'bi',

  // 字符串 / 正则 / 数字
  string: 'str',
  char: 'str',
  subst: 'str',
  regexp: 're',
  number: 'num',

  // 注释
  comment: 'cmt',
  doctag: 'cmt',

  // 名字 / 标题 / 函数
  title: 'tt',
  section: 'tt',
  function: 'fn',
  params: 'var',

  // 变量 / 属性 / 元数据
  variable: 'var',
  attr: 'attr',
  attribute: 'attr',
  property: 'attr',
  meta: 'meta',

  // 标记 / 选择器 / 符号
  tag: 'tag',
  name: 'tag',
  symbol: 'sym',
  'selector-tag': 'tag',
  'selector-class': 'sel',
  'selector-id': 'sel',
  'selector-attr': 'sel',
  'selector-pseudo': 'sel',

  // 操作符 / 标点
  operator: 'op',
  punctuation: 'op',
  keyframePosition: 'kw',
};

function fixClasses(html) {
  return html.replace(/class="([^"]*)"/g, (m, classes) => {
    const mapped = classes.split(/\s+/).map((c) => {
      const name = c.startsWith('hljs-') ? c.slice(5) : c;
      return CLASS_MAP[name] || c;
    });
    return `class="${mapped.join(' ')}"`;
  });
}

// 把跨行 span 拆成每行独立闭合：C++ 侧 buildCodeBlock 是按 \n 拆行塞 <td> 的，
// 跨行 span 被切开就会断掉配对
function splitSpansPerLine(html) {
  const open = [];
  let out = '';
  let i = 0;
  while (i < html.length) {
    if (html.startsWith('<span class="', i)) {
      const end = html.indexOf('>', i);
      const tag = html.slice(i, end + 1);
      const m = tag.match(/class="([^"]*)"/);
      open.push(m ? m[1] : '');
      out += tag;
      i = end + 1;
    } else if (html.startsWith('</span>', i)) {
      if (open.length) open.pop();
      out += '</span>';
      i += 7;
    } else if (html[i] === '\n') {
      for (let k = 0; k < open.length; ++k) out += '</span>';
      out += '\n';
      for (const cls of open) out += `<span class="${cls}">`;
      i += 1;
    } else {
      out += html[i];
      i += 1;
    }
  }
  return out;
}

globalThis.__highlight = function (code, lang) {
  let l = String(lang || '').toLowerCase();
  l = ALIAS[l] || l;
  // 未注册的语言返回 null（C++ 侧退回纯转义），不开 highlightAuto：猜错的
  // 配色比不高亮更难看，且语言标签是文档作者写的、该信
  if (!l || !hljs.getLanguage(l)) return null;
  try {
    const out = hljs.highlight(code, { language: l, ignoreIllegals: true }).value;
    return splitSpansPerLine(fixClasses(out));
  } catch (e) {
    return null;
  }
};