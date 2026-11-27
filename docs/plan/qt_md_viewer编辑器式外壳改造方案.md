# qt_md_viewer 编辑器式外壳改造方案

> 日期：2026-11-27
> 前置验证：`demo/shell/` 三个 QWindowKit 外壳 Demo（结论见其 README 与
> 个人笔记《QWindowKit 嵌套标题栏与编辑器式外壳实验记录》）
> 决策：**不再扩展 `FluentWindowFrame`**。窗口直接持有 `FluentTitleBar`
> 与 `QWK::WidgetWindowAgent`，外壳布局由 `MdViewerWindow` 自己组装。

## 1. 目标

把"全宽标题栏（menuWidget）+ 全宽工具条 + 下方内容"的浏览器式结构，改成
编辑器式外壳：

```text
┌────┬─────────────────┬──────────────────────────────────────────────┐
│ ⧨  │ 侧栏标题行(42px) │  ☰  标签条…        拖拽区      ☀ 📌 ─ □ ×   │
├────┼─────────────────┼──────────────────────────────────────────────┤
│ 活 │                 │  DocumentBar / WelcomeBar（仅右列宽）         │
│ 动 │  文档/搜索/       │  ──────────────────────────────────────────  │
│ 栏 │  收藏/渲染状态    │  正文                    │  大纲              │
│    │                 │                        │                     │
└────┴─────────────────┴──────────────────────────────────────────────┘
└──────────────── 状态栏（全宽不变） ─────────────────────────────────┘
```

- 左外壳（活动栏 + 侧栏）从窗口最上沿直通状态栏上沿；
- 标题栏只覆盖右列；最左上角是侧栏收起按钮；
- 中缝可拖动调宽；收起后活动栏保留、右列整体左移；
- 会话状态语义不变（`sidebar_visible/sidebar_width/outline_width/view`）。

## 2. 结构对照

### 2.1 现状

```text
QMainWindow
├─ menuWidget: chromeHeader(FluentWindowFrame) = VBox(FluentTitleBar)   ← 全宽
├─ TopToolBar(QToolBar) = bar_stack_(DocumentBar|WelcomeBar)            ← 全宽
└─ central: HBox ─ ActivityBar ─ main_splitter_(side_panel_|pages_|outline_column_)
```

### 2.2 目标

```text
QMainWindow
└─ central: shell_splitter_
   ├─ left_shell: HBox
   │  ├─ rail: VBox(收起按钮行42px + ActivityBar)
   │  └─ side_panel_(title行对齐42px)
   └─ right_shell: VBox
      ├─ FluentTitleBar(42px, leading=菜单+标签条+拖拽区)
      ├─ TopToolBar = bar_stack_(DocumentBar|WelcomeBar)
      └─ main_splitter_(pages_|outline_column_)
```

变化点：

| 项 | 现状 | 目标 |
| --- | --- | --- |
| 标题栏 | `FluentWindowFrame`→`setMenuWidget` | 直接 `FluentTitleBar` 嵌右列 + agent |
| agent | `FluentWindowFrame` 内部创建 | `MdViewerWindow` 持有 `agent_` |
| 侧栏宽度 | `main_splitter_` 第 0 列 | 新 `shell_splitter_`（左外壳 vs 右列） |
| 大纲宽度 | `main_splitter_` 第 2 列 | `main_splitter_` 变两列（正文|大纲） |
| 收起入口 | 点活动栏当前项 | 左上角收起按钮（Ctrl+B）+ 保留原交互 |
| 材质 | `fluent_frame_->setWindowBackdrop` | `agent_->setWindowAttribute`（本地 helper） |

## 3. 实现步骤

1. **窗口骨架重组**（`md_viewer_window.cpp initUi()`）
   - 删除 `fluent_frame_`；新增 `title_bar_`（FluentTitleBar）、`agent_`、
     `shell_splitter_`、`left_shell_`、`right_shell_`；
   - 标题栏 leading 复用现有 `title_leading_widget_`（菜单+标签条+拖拽区），
     拖拽区加 `setMinimumWidth(96)`；
   - `top_tool_bar_` 保留类不变，从 `addToolBar` 改为放进右列布局
     （`TopToolBar::paintEvent` 自绘 chrome 背景，仍然有效）；
   - `main_splitter_` 变两列；`ApplySplitterSizes` 改两段尺寸。
2. **收起按钮**
   - 左列顶部 42px 行放 `sidebarToggleButton`（新 `panel.svg` light/dark）；
   - 新 `actions_.toggle_sidebar`（Ctrl+B，checkable），与活动栏"点当前项收起"
     共用同一状态函数 `ApplySidebarVisible(bool)`。
3. **对齐**：`FluentTitleBar` 高度仍按 `4 + tabSizeHint` 计算（=42）；侧栏
   `sidePanelTitle` 行固定 42；收起按钮行 42。三行水平对齐。
4. **agent 登记**：`setup()` 后 `setTitleBar(title_bar_)`；hit-test 登记
   leading、菜单按钮、标签条、主题/置顶按钮；`setSystemButton` 接
   min/max/close；accent 按钮照旧隐藏。
5. **材质迁移**：新增 `SetWindowBackdrop(key)`（内部先清四个属性再设置），
   `ApplyWindowMaterial` 改调它；`RefreshChromePalette`/`RefreshThemeIcons`
   中 `fluent_frame_->titleBar()` 改 `title_bar_`。
6. **会话状态**：字段与存取函数不动；`SaveSettings`/`ApplySplitterSizes`/
   `EnsureSideView`/`ShowSideView` 改按新 splitter 取/设宽度；收起时保存
   宽度语义保持（先记宽再隐藏）。**首次 `show()` 前嵌套 splitter 没有真实
   宽度，`showEvent()` 里再落一次保存尺寸；启动顺序改为 `show()` 后再
   `RestoreSession()`**，否则 litehtml 在 92×22 退化视口下首次渲染会崩
   （详见笔记 §9）。
7. **测试**
   - 无头：外壳层级断言（标题栏在右列、活动栏+侧栏在左外壳）、收起/展开
     切换可见性与宽度、`toggle_sidebar` 动作与按钮同步、会话布局字段往返；
   - 手动（agent-desktop，沿用 Demo 清单）：拖动/八方向缩放/菜单/标签拖动/
     100%+150% DPI/Snap Layouts/`Alt+Space`/Mica·Acrylic·None 三种材质。

## 4. 风险与对策

| 风险 | 对策 |
| --- | --- |
| QToolBar 放进布局渲染异常 | `TopToolBar::paintEvent` 已完全自绘；若仍异常则换普通 QWidget band |
| 主题切换后标题栏配色漂移 | 保留现有"标题栏 palette 铺 ChromeColors"逻辑，对象换成 `title_bar_` |
| 次窗口（session_owner=false）路径 | 与主窗口同代码路径，无需分支 |
| 现有 GUI 锚点（objectName）变化 | 只增不改：`titleLeadingWidget`/`documentTabBar`/`mainStatusBar` 等全保留 |
| Snap Layouts / Alt+Space 未验证 | 改造完成后手动验证清单里补测 |

## 5. 不做的事

- 不改 `FluentWindowFrame` / `FluentTitleBar`（除既有补丁外）；
- 不新增侧栏头部动作按钮（打开/刷新继续走菜单与命令面板）；
- 不动 Observatory（无 FluentUI 依赖，不受影响）；
- 不改安装/发布规则。
