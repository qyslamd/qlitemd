# qt_md_viewer 首次启动布局与重置设置方案

> 日期：2026-11-27
> 状态：待维护者确认，确认前不改代码、不打 release tag
> 前置：编辑器式外壳改造已提交 `2db8d8f` 并推送；GitHub CI 全绿；
> Qt5 x64 本地 56/56 通过。发布流程暂停在本方案确认之后。

## 1. 问题

### 1.1 首次启动没有确定性的布局

新用户没有 `qt_md_viewer.ini`，`MdViewerSettings::Load()` 只能提供字段默认值：

- `sidebar_width = 280`
- `outline_width = 212`
- `sidebar_visible = true` / `outline_visible = true`

这些值是“逻辑默认”，不是按窗口实际宽度算出的三栏比例。新外壳改成双层
`QSplitter` 后，`show()` 之前 splitter 没有真实宽度，构造期 `setSizes()`
会被后续布局覆盖；`showEvent()` 才补一次尺寸。首次启动可能出现：

- 侧栏/大纲宽度不符合当前窗口；
- 窗口没有居中、大小不是首启推荐值；
- 用户先拖动 splitter 前，三栏比例看起来偏窄/偏宽。

### 1.2 没有重置入口

设置面板现在只有缓存目录的 “Default”，没有整体恢复布局或恢复偏好设置的入口。
用户把三栏拖乱、或想恢复默认主题/阅读参数时，只能删 ini。

## 2. 目标

1. **无 ini 的首次启动**：窗口大小、位置、侧栏/大纲/正文比例一次到位，
   用户不用先手动拖 splitter。
2. **已有 ini**：完全尊重用户保存的几何与宽度，不能被“默认布局”覆盖。
3. **设置对话框**：增加明确的“恢复布局”和“恢复默认偏好”入口，执行前确认，
   执行后立即生效。
4. 所有默认比例逻辑抽成纯函数，可无头测试；不依赖 `show()` 才能验证。

## 3. 首次启动判定

`MdViewerSettings::Load()` 读取 ini 前先记录：

```cpp
bool first_run_ = false;
void Load() {
  MigrateLegacyFile();
  first_run_ = !QFileInfo::exists(DefaultPath());
  // ... 读 QSettings
}
bool IsFirstRun() const { return first_run_; }
```

- 旧版 `md_viewer.ini` 迁移产生的配置**不算首次启动**（迁移后文件存在）。
- 首次启动标志只在内存里用，不写入 ini；ini 由后续 `Save()` 创建。

## 4. 默认布局

### 4.1 窗口

首次启动且没有保存几何时：

- 默认客户区 `1240 × 820`，受屏幕可用区域约束；
- 使用 `QScreen::availableGeometry()` 居中；
- 不小于现有 `setMinimumWidth(600)` 和可用区域；
- 多显示器用窗口所在屏幕；启动时用主屏。

已有 `window_geometry` 时不改窗口大小/位置。

### 4.2 三栏比例

新增纯函数（建议放 `side_view.h` 或新 `shell_layout.h`）：

```cpp
struct ShellMetrics {
  int sidebar_width;
  int outline_width;
};

ShellMetrics DefaultShellMetrics(int total_width, bool outline_available);
```

规则（逻辑像素，Qt 自动处理 DPI）：

| 条件 | 侧栏 | 大纲 | 正文 |
| --- | --- | --- | --- |
| `total_width >= 980` | `clamp(24%, 240, 340)` | `clamp(18%, 200, 300)` | 剩余 |
| `total_width < 980` | `clamp(32%, 220, 300)` | 0（沿用现有窄窗隐藏） | 剩余 |

例：

- 1100px：侧栏 264，大纲 198，正文 638；
- 1240px：侧栏 298，大纲 223，正文 719；
- 600px：侧栏 220，正文 380，大纲隐藏。

首次启动在 `showEvent()` 拿到真实宽度后调用；已有 ini 时用保存值。

### 4.3 与现有 show 顺序的配合

保留 `main.cpp` 的 `window.show()` 再 `RestoreSession()`。首次启动的标志
由 `MdViewerWindow` 构造时读取，`showEvent()` 里：

```cpp
if (!shell_sizes_applied_) {
  shell_sizes_applied_ = true;
  if (settings_->IsFirstRun()) {
    ApplyDefaultShellMetrics(width());  // 同时写入 layout_ 并落 splitter
  } else {
    ApplySplitterSizes();
  }
}
```

## 5. 重置设置

### 5.1 入口

设置对话框（`SettingsPanel`）新增一个 **General** 分类页，放在 Appearance 之前：

```text
General
  Window layout
    [ Reset Layout ]      恢复三栏比例、侧栏/大纲显隐和窗口几何
  Preferences
    [ Reset Preferences ] 恢复主题、材质、阅读、内容、缓存/诊断设置
```

两个按钮都弹 `QMessageBox::question` 二次确认。目录/缓存清理继续留在
Cache/Diagnostics 页，不与“重置设置”混在一起。

### 5.2 Reset Layout（推荐先做）

恢复：

- `SideView::kDocuments`；
- `sidebar_visible = true`、`outline_visible = true`；
- 侧栏/大纲宽度 = `DefaultShellMetrics(当前宽度)`；
- 窗口几何 = 4.1 的默认窗口（居中、1240×820）。

保留：当前打开的所有文档、标签、滚动位置、阅读偏好、主题、书签、最近文件。

实现：

```cpp
void MdViewerWindow::ResetLayout() {
  layout_ = ShellLayoutState{};
  layout_.sidebar_width = DefaultShellMetrics(width()).sidebar_width;
  layout_.outline_width = DefaultShellMetrics(width()).outline_width;
  ApplySidebarVisible(true);
  ApplySplitterSizes();
  ApplySideView(SideView::kDocuments);
  ResizeToDefaultWindow();      // 清空 settings_->window_geometry
  session_controller_->SaveShellLayout(layout_);
}
```

### 5.3 Reset Preferences（需要你确认范围）

建议恢复**偏好设置**，保留**用户数据**：

恢复：

- 主题、窗口材质；
- 阅读区样式、透明页面、字号/行高/版心；
- 代码行号、admonition、Mermaid/公式后端与开关；
- 持久缓存开关、缓存/诊断目录、trace/dump 开关。

保留：

- 最近文件、最近工作区；
- 书签；
- 阅读位置；
- 当前打开的标签/工作区；
- 窗口几何与三栏宽度（布局只由 Reset Layout 管）。

实现要点：

- `MdViewerSettings::ResetPreferences()` 把上述字段恢复为 `MdViewerSettings{}`
  的默认值，但不动 recents/bookmarks/positions/open_tabs/root/last_file；
- 宿主按新设置重新 `ApplyTheme`、`ApplyWindowMaterial`、`ApplyReadingStyle`；
- `SettingsPanel` 重新同步各控件，不需要重建对话框。

如果你希望“恢复出厂”连书签/最近/会话一起清，需要再增加第三个按钮，
默认不做，避免误删用户数据。

### 5.4 信号与测试

`SettingsPanel` 新增：

```cpp
signals:
  void resetLayoutRequested();
  void resetPreferencesRequested();
```

测试：

- `DefaultShellMetrics` 在 600/980/1100/1240/1440 的取值与边界；
- 首次启动 `IsFirstRun()`：无 ini 为 true，迁移/已有 ini 为 false；
- `ResetPreferences()` 保留 recents/bookmarks/positions/open_tabs；
- `SettingsPanel` 有 General 页、两个按钮 objectName、点击发信号；
- 重置后布局字段与保存值一致（无头验证 helper，不拉起完整窗口）。

## 6. 风险与取舍

| 风险 | 对策 |
| --- | --- |
| 首次默认值覆盖用户 ini | 只在 `IsFirstRun()` 且无 geometry 时用默认，已有值永远优先 |
| 重置偏好误删书签/最近 | 默认保留用户数据；出厂重置单独按钮，需明确确认 |
| 重置布局把用户窗口挪走 | 二次确认；提示“会重置窗口大小和位置” |
| 多显示器/DPI | 用逻辑像素 + 当前屏幕可用区域；150% 下按 Qt 缩放 |
| 次窗口重置 | 次窗口设置只读，重置入口只对主窗口开放 |
| reset 时正在渲染 | 布局重置不重新加载文档；偏好重置按现有 Apply* 链路重绘 |

## 7. 实施顺序

1. `DefaultShellMetrics` + `IsFirstRun` + 单元测试；
2. 首次启动窗口/三栏默认值接入 `showEvent`；
3. `ResetPreferences` 纯设置层 + 保留用户数据测试；
4. SettingsPanel General 页、两个按钮、确认框、信号；
5. MdViewerWindow 的 ResetLayout/ResetPreferences 接线；
6. 手动验证：删 ini 首次启动、拖乱后 Reset Layout、Reset Preferences、
   重启保存值、150% DPI；
7. 重新跑 Qt5 x86 / Qt6 x64 测试后，再谈 0.9.0 发布 tag。

## 8. 需要你确认

1. 默认窗口 `1240×820`、侧栏 24%、大纲 18% 是否接受？
2. Reset Preferences 是否按建议**保留书签/最近/会话**，只重置偏好？
3. Reset Layout 是否同时重置窗口大小和位置，还是只重置三栏 splitter？
