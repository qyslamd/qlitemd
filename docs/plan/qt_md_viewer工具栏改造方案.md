# qt_md_viewer 顶部工具栏改造方案

日期：2026-11-24。状态：**已实施**（`8d7b9b0`、`da12dec`）。
本文取代 `qt_md_viewer多标签页改造方案.md` §12 中"文档条放在页栈上方"
的宿主描述：文档条仍然是窗口级控件，但宿主从 `centralWidget` 改为
`QMainWindow` 的顶部工具条区。

## 1. 目标

1. 把文档条提升为真正的 `QToolBar`，横跨整窗、位于两个 dock 之上，
   让"标题栏 / 工具栏 / 内容 + 侧栏"成为完整的三层结构。
2. 欢迎标签有自己专用的工具栏，不再复用文档条、留一堆灰着的文档控件。
3. 宽度收缩时工具栏自身按优先级让位，不再被两侧 dock 挤压。

非目标：

- 不改 `MarkdownView` 与 `qlitemd_mdhtml`。
- 不做工具栏可停靠/可浮动：这一条是固定的窗口横带。
- 不做用户自定义工具栏内容。

## 2. 现状与问题

现状（`MdViewerWindow::initUi`）：

```text
QMainWindow
├─ menuWidget: FluentWindowFrame 的标题栏（标签条装在它的 leading 区）
├─ centralWidget: centralColumn
│   ├─ DocumentBar（32 px）
│   └─ pages_（欢迎页 + 各标签的 ReaderPage）
├─ LeftDockWidgetArea: documentDock
└─ RightDockWidgetArea: outlineDock / renderStatusDock
```

问题：

1. 文档条在 `centralWidget` 里，只占两个 dock 之间的宽度；dock 顶边
   与文档条顶边齐平，视觉上是两条互不相干的横线。
2. 窗口变窄时，文档条的可用宽度是"窗口宽 - 两个 dock"，比实际窗口
   窄得多，900/780/680/600 这组让位阈值实际是按中央列宽度定的。
3. 欢迎标签复用文档条，只把路径清空：后退/前进变灰、收藏可点但无意
   义、缩放显示 100% 仍可点。

## 3. 结构

```text
QMainWindow
├─ menuWidget: 标题栏（不变）
├─ TopToolBarArea: TopToolBar（32 px，横跨整窗）
│   └─ barStack_ (QStackedWidget)
│       ├─ page 0: DocumentBar
│       └─ page 1: WelcomeBar
├─ centralWidget: pages_
├─ LeftDockWidgetArea / RightDockWidgetArea（不变）
└─ statusBar（不变）
```

选择"一个工具条 + 页栈"而不是"两个 QToolBar"：

- 视觉与行为等价，但两条工具条要各自处理显隐、`saveState` 条目、
  恢复后的复位，状态多一份、出错面更大。
- 切标签只切栈页，切换是原子的，窗口不会跳。
- 将来若确实需要两条独立工具条，`barStack_` 可以拆成两个 `QToolBar`，
  `WelcomeBar` 本身不用重写。

## 4. TopToolBar

新增 `TopToolBar : QToolBar`：

| 项 | 取值 | 原因 |
| --- | --- | --- |
| `objectName` | `topToolBar` | GUI 工具锚点 |
| `movable` | `false` | 固定横带 |
| `floatable` | `false` | 同上 |
| 高度 | `setFixedHeight(32)` | 与文档条一致，切换不跳动 |
| 上下文菜单 | `Qt::PreventContextMenu` | 用户不能把工具栏关掉 |
| 内边距 | `0` | 文档条自己带 8 px 左右边距 |
| 内容 | `addWidget(barStack_)` | 不改文档条内部结构 |

两点实现要求：

1. **右键菜单**：`QMainWindow` 自带"显示/隐藏工具条与 dock"的弹出
   菜单（`createPopupMenu()`）。必须覆盖它，去掉工具条条目、保留 dock
   条目，否则用户能把文档条关掉且无法恢复。
2. **绘制**：`QToolBar` 会画自己的背景、边框与拖动手柄。`TopToolBar`
   重写 `paintEvent`：用与中心区一致的底色填充、不画 frame、不画手柄。
   底色取窗口调色板的 Window 角色，亮/暗两套都以截图取色核对；不允许
   出现 1 px 分隔线。

`DocumentBar` 与 `WelcomeBar` 内部都保持 32 px 固定高度、8 px 左右
边距，保证栈内两页对齐。

## 5. 两类栏的内容

### 5.1 DocumentBar（文档标签）

控件集合：后退、前进、收藏、路径、复制路径、搜索、阅读外观、
缩放组（− 100% +）。设置按钮后来移到了活动栏底部，见 §14。

改动只有一处：路径不再由中央列宽度决定省略长度，而是由工具栏实际
宽度决定；省略逻辑本身不变。

搜索框的语义按"当前文档查找"收敛，见 §6。

### 5.2 WelcomeBar（欢迎标签，新增）

| 位置 | 内容 |
| --- | --- |
| 左 | 欢迎标识 + "欢迎" + 工作区名（未设工作区显示"未打开工作区"） |
| 右 | 快速跳转（设置按钮在活动栏底部，见 §14） |

明确不放：

- 后退/前进/收藏/复制/缩放/阅读外观：欢迎态没有作用对象。
- 搜索框：欢迎态既没有当前文档，工作区搜索又属于侧栏，见 §6。
- "打开文件/打开目录"：欢迎页正文已经是这两个动作的主入口，工具栏
  再放一遍是冗余。

## 6. 搜索语义

### 6.1 现状与问题

文档条上的搜索框（`globalSearchEdit`）实际做的是**工作区搜索**：
输入后防抖，`TextSearch` 扫描 `root_dir` 下所有文件，结果显示在侧栏
的"搜索"页签，点结果打开文件并用 `MarkdownView::FindText` 定位。
侧栏里另一个输入框（`documentFilterEdit`，"筛选文件与路径"）只过滤
文档树，不搜正文；侧栏"搜索"页签自身没有输入框。

两个问题叠在一起：per-document 的位置放了 workspace 级功能，而侧栏
作为工作区面板反而没有自己的搜索入口。

### 6.2 分工

| 入口 | 语义 | 快捷键 |
| --- | --- | --- |
| 文档条搜索框 | 当前文档内查找 | Ctrl+F |
| 侧栏"搜索"页签顶部输入框 | 工作区搜索：文件名 + 所有文档正文 | Ctrl+Shift+F |
| 欢迎标签 | 不提供搜索 | — |

### 6.3 文档内查找

1. Ctrl+F 聚焦文档条搜索框并全选已有查询。
2. 输入即查找，防抖 150–200 ms，从当前位置向后定位并高亮当前命中。
3. Enter 下一个，Shift+Enter 上一个；`FindText` 的 `wrapped` 出参为真
   时，状态栏提示"已回到开头"/"已回到结尾"。
4. 没有命中时只更新状态栏文字，不改控件颜色（不在代码里写死样式）。
5. Esc 清空查询、清除选区并把焦点还给正文。
6. 当前文档正在渲染时输入控件禁用，渲染完成后恢复。
7. 切标签保留查询文字（搜索框是窗口级），但清除选区，等下一次
   Enter 再定位。
8. 第一版不做匹配计数。`MarkdownView` 只有 `FindText` 与
   `ClearSelection`，"第 3 个，共 17 个"需要在库层增加计数 API，
   另立迭代。

### 6.4 工作区搜索

1. 侧栏"搜索"页签顶部增加输入框，`objectName` 为
   `workspaceSearchEdit`，占位文字"搜索工作区文件与内容"。
2. 触发方式沿用现有的防抖加回车；结果渲染、点击打开并定位的逻辑
   不变。
3. "文档"页签顶部的"筛选文件与路径"保持原样。两者不同屏：一个筛
   树，一个搜正文，不会在同一视图里出现两个框。
4. 未设工作区时输入框禁用，占位文字改为"先打开一个目录"，不再等
   用户输完才在结果区报错。

### 6.5 快捷键

| 快捷键 | 行为 |
| --- | --- |
| Ctrl+F | 聚焦文档条搜索框（当前文档查找） |
| Ctrl+Shift+F | 聚焦侧栏搜索框并切到"搜索"页签 |
| Ctrl+P | 快速跳转（不变） |
| Ctrl+K | 退休，不再绑定搜索 |

Ctrl+K 原本指向那个全局搜索框；拆分后它与 Ctrl+Shift+F 重复，保留
只会让同一个功能有两个名字。

### 6.6 状态隔离

1. 文档查找的 query 不再写进侧栏搜索页签，也不切换侧栏页签。
2. 工作区搜索不再改动当前文档的选区。
3. 两边的取消、关闭、空查询各管各的，不互相触发重置。

### 6.7 欢迎标签

欢迎工具栏不放搜索框。欢迎页下 Ctrl+F 没有目标：状态栏提示"先打开
一篇文档"并忽略。Ctrl+Shift+F 仍然可用——工作区搜索不依赖当前文档，
直接聚焦侧栏搜索框。

### 6.8 影响面

- 控件：`globalSearchEdit` 拆成 `docFindEdit`（文档条）与
  `workspaceSearchEdit`（侧栏）。
- 文档：`CLAUDE.md` 的点分 `objectName` 示例同步更新。
- 测试：文档查找的下一个/上一个/绕回提示、Esc 清除、工作区搜索从
  侧栏发起、欢迎页无搜索框、Ctrl+Shift+F 聚焦侧栏、两套状态互不
  影响。

### 6.9 第一版不做

- 匹配计数（需要 `MarkdownView` 新增 API）。
- 每标签独立的查找历史。
- 正则、全词匹配、大小写选项。

## 7. 状态切换

`MdViewerWindow::UpdateShellForActiveTab` 里：

```cpp
const bool welcome = tab == nullptr || tab->path.isEmpty();
bar_stack_->setCurrentIndex(welcome ? 1 : 0);
```

- 删除现在那种"清空路径但保留控件"的半状态。
- 现有的 `FocusGlobalSearch` 拆成两个动作：`FocusDocumentFind`
  （Ctrl+F，聚焦文档条搜索框）与 `FocusWorkspaceSearch`
  （Ctrl+Shift+F，聚焦侧栏搜索框并切页签）；欢迎态下前者只给状态栏
  提示。
- 阅读外观弹层仍然锚定 `document_bar_->appearance_button()`，它只在
  文档态可见。

## 8. 持久化

现在 `dock_state` 直接存 `QMainWindow::saveState()`，里面也包含工具条。

既然当前几乎没有用户，不做旧状态迁移，直接把键名换成
`layout_state_v2`：旧键忽略，所有人回到默认布局。

恢复流程：

1. `restoreGeometry`（不变）。
2. `restoreState(layout_state_v2)` 只用来恢复 dock 布局。
3. 恢复后统一复位工具条：`top_tool_bar_->show()`、固定 32 px、
   `barStack_` 切回当前标签对应的页、`setMovable(false)` 等不变量
   再断言一次（`restoreState` 可能改动工具条可见性）。

保存路径不变：仍然只由主窗口写，次窗口只读设置副本。

## 9. 宽度让位

工具栏宽度 = 窗口宽度，不再受 dock 影响。阈值按窗口宽度定：

| 可见项 | 窗口宽度 |
| --- | --- |
| 缩放组 | ≥ 860 |
| 阅读外观 | ≥ 720 |
| 复制路径 | ≥ 620 |
| 搜索框压缩（240 → 160） | < 620 |
| 搜索框始终显示（只压缩不隐藏） | 任意 |
| 路径标签始终保留 | 任意 |

`WelcomeBar` 的快速跳转在宽度 `< 480` 时隐藏，只留欢迎与工作区名。

窗口最小宽度已降到 600：左侧 dock 最小宽度收到 200、右侧大纲收到
140、欢迎页内容卡最小宽度收到 320，窗口再显式 `setMinimumWidth(600)`。
这样 860 / 720 / 620 三档都能在真实窗口里触发；600 宽时工具条只剩
后退/前进/收藏、路径与 160 宽搜索框；大纲栏按既有规则自动隐藏。

验证截图（`temp/agent_desktop_probe/`）：

| 状态 | 文件 | 实际宽度 |
| --- | --- | --- |
| 亮色全宽 | `thr_light_1200.png` | 1186 |
| 亮色，缩放隐藏 | `thr_light_860.png` | 846 |
| 亮色，外观隐藏 | `thr_light_720.png` | 706 |
| 亮色，dock 全关 | `thr_light_620.png` | 618 |
| 暗色全宽 | `thr_dark_1200.png` | 1186 |
| 暗色，dock 全关 | `thr_dark_620.png` | 636 |
| 亮色，最小窗口 | `min600.png` | 602 |
| 亮色，最小窗口欢迎页 | `min600_welcome.png` | 602 |

各档均无控件重叠，搜索框只在最窄时被压缩的路径尚未出现（见上面的
最小宽度）。

## 10. 测试

无头（`tests/qt_md_viewer/`）：

1. 工具栏存在、固定 32 px、不可移动/不可浮动、上下文菜单被禁用。
2. 两个 dock 都显示时，工具栏宽度等于窗口宽度。
3. 欢迎标签 → 栈当前页是欢迎栏；打开文档 → 栈当前页是文档栏。
4. 欢迎栏没有搜索框；文档栏有。
5. Ctrl+F 在文档态聚焦文档搜索框；欢迎态不动作并给出状态栏提示。
6. 侧栏"搜索"页签的输入框启动工作区搜索（用测试根目录的假文件）。
7. Ctrl+Shift+F 聚焦侧栏搜索框并切到"搜索"页签。
8. 文档查找与工作区搜索互不影响：一边的输入不改另一边的结果与选区。
9. 现有 `document_bar_test` 保持控件级断言不变。

GUI（agent-desktop，不入 CI）：

- 100%、两个 dock 打开 → 工具栏横跨整窗、dock 顶边在其下方；
- 窄窗 640 → 让位顺序正确，无重叠；
- 暗色 → 无多余分隔线；
- 切欢迎/文档标签 → 工具栏内容切换且高度不变。

## 11. 实施拆分（分支 `feat/top-toolbar`）

1. 引入 `TopToolBar` 与 `barStack_`，把 `DocumentBar` 搬进去，行为
   不变（先不加欢迎栏），出截图确认外壳。
2. 新增 `WelcomeBar`，接上 `UpdateShellForActiveTab` 的切页逻辑，
   补测试 1–4。
3. 搜索拆分：文档查找、侧栏搜索输入、快捷键，补测试 5–6。
4. 阈值重调、暗色与窄窗截图、文档与 `CHANGELOG`。

每一步单独提交，第 1 步的截图确认后再继续。

## 12. 风险

| 风险 | 处理 |
| --- | --- |
| Fluent 对 `QToolBar` 的绘制（背景/边框/分隔线） | `TopToolBar::paintEvent` 兜底；亮暗两套截图核对 |
| `QMainWindow` 右键菜单能关掉工具条 | 覆盖 `createPopupMenu()`，去掉工具条条目 |
| `restoreState` 改动工具条可见性/尺寸 | 恢复后显式复位不变量 |
| 自动化路径变化（`centralColumn` 消失） | 同步 `CLAUDE.md` 示例与本机 funq 配方 |
| 两套搜索状态互相干扰 | 状态完全隔离，各自持有 query 与结果 |

备选方案：顶层 `QDockWidget`（隐藏标题栏、`NoDockWidgetFeatures`）
也能横跨整窗，但它同样进 dock 状态且会画 dock 边框，不如 `QToolBar`
干净，暂不采用。

## 13. 验收标准

- 活动栏、侧栏、正文与右侧大纲在同一水平带；大纲可收起，窄窗自动隐藏。
- 工具栏横跨整窗；切换标签时工具栏高度不变、无跳动。
- 欢迎标签下不再出现任何文档级控件，且有专门的欢迎工具栏。
- 600–1200 宽度区间内不出现控件重叠或省略异常。
- 文档查找与工作区搜索互不影响，快捷键行为与 §6 一致。
- 侧栏当前视图、侧栏显隐与大纲显隐在重启后恢复。

## 14. 侧栏改为活动栏（2026-11-24 追加）

原方案把侧栏留作 `QDockWidget`，实际落地改成固定布局，本节取代第 3 节
与第 8 节里与 dock 有关的部分：

- 左侧 `ActivityBar`：固定 30px，四个视图（文档 / 搜索 / 收藏 / 渲染
  状态）；点击当前已选中的项会收起或展开侧栏；当前项用左侧强调条标出。
- 一个固定 280px 的侧栏承载四个视图；`DocumentsPanel` 隐藏内部页签条，
  由活动栏驱动 `ShowTab()`；渲染状态从 dock 变成侧栏的第四页。
- 大纲改为正文右侧固定列（212px），标题行带收起按钮；窗口窄于 980px
  时自动隐藏，用户手动收起的状态优先。
- 不再使用 `QDockWidget`：`saveState/restoreState`、视图菜单里的 dock
  开关、`layout_state_v2` 全部移除；改为保存 `sidebar_view`、
  `sidebar_visible`、`outline_visible` 三个设置项。
- 快捷键：`Ctrl+Shift+E/F/B/R` 切四个视图，`Ctrl+Shift+O` 切换大纲；
  `Ctrl+Shift+F` 仍聚焦工作区搜索输入框。
- 中央区改为 `QSplitter`：活动栏固定，侧栏/正文/大纲三块可拖动改宽；
  侧栏与大纲宽度存 `sidebar_width` / `outline_width`，正文用拉伸因子吃掉
  剩余空间，`setChildrenCollapsible(false)` 防止误拖成 0。
- 搜索视图去掉底部 footer：进度/命中数与取消按钮并进搜索框下方那一行，
  文档树与正文页底边对齐。
- 设置按钮（齿轮）移到活动栏底部，文档条与欢迎栏不再各放一个；
  `Ctrl+,` 与菜单里的「设置…」保持不变。
