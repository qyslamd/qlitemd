# FluentUI3Style 分割条手柄绘制方案

日期：2026-11-26。状态：**已实施**（见 §10）。
涉及第三方补丁：`3rdparty/patches/FluentUIStyle/0006-splitter-handle.patch`（拟）。

## 1. 目标

1. `QSplitter` 的手柄（`QSplitterHandle`）在悬停和按下时有可见的分隔条，
   不再是一块完全没有反馈的空隙；常态保持透明。
2. 手柄区分**常态 / 悬停 / 按下**三种状态，悬停与按下要有可见反馈。
3. 横、竖两个方向都正确；高对比模式下不依赖固定颜色。
4. 只动样式层，不动 `qt_md_viewer` 的宿主代码（`handleWidth` 仍由调用方决定）。

非目标：

- 不做手柄上的抓握点纹理、双击折叠等交互（Qt 未提供，需要宿主自己实现）。
- 不覆盖 `PM_SplitterWidth`：宽度继续由 `QSplitter::handleWidth()` 决定，
  见 §6.3。
- 不改 `QDockWidget` 的分隔条外观（但要注意它会共用 `CE_Splitter`，见 §8）。

## 2. 现状

### 2.1 Qt 这一侧把状态都给了

`QSplitterHandle::paintEvent()`（Qt 5.15 `qsplitter.cpp`）每次都把三个状态位
塞进 `option`，然后交给**分割条自己的 style**：

```cpp
void QSplitterHandle::paintEvent(QPaintEvent *) {
    QStyleOption opt(0);
    opt.rect = contentsRect();
    opt.palette = palette();
    if (orientation() == Qt::Horizontal) opt.state = QStyle::State_Horizontal;
    else                                opt.state = QStyle::State_None;
    if (d->hover)   opt.state |= QStyle::State_MouseOver;
    if (d->pressed) opt.state |= QStyle::State_Sunken;
    if (isEnabled()) opt.state |= QStyle::State_Enabled;
    parentWidget()->style()->drawControl(QStyle::CE_Splitter, &opt, &p, d->s);
}
```

两个容易踩的点：

- `option` 只是一个裸 `QStyleOption`，没有 `styleObject`，别去 `qstyleoption_cast`。
- 第四个参数 `widget` 传的是 **`QSplitter`**（`d->s`），不是 `QSplitterHandle`。
  样式里按 `widget` 取 `QSplitterHandle` 的属性一定拿不到。

### 2.2 FluentUI3Style 没有接管 `CE_Splitter`

在 `3rdparty/fluentui/fluentui3style/` 整个目录里搜不到 `Splitter`：没有
`CE_Splitter`、没有 `PM_SplitterWidth`、没有对 `QSplitterHandle` 的事件过滤。
`CE_Splitter` 因此经 `QProxyStyle::drawControl` 落到基样式；本仓基样式是
`windowsvista`，最终是 `QWindowsStyle::drawControl`：

```cpp
#if QT_CONFIG(splitter)
    case CE_Splitter:
        p->eraseRect(opt->rect);
        break;
#endif
```

只擦背景，不画线，也不看 `State_MouseOver` / `State_Sunken`。

### 2.3 实测：三种状态像素完全一致

用临时探针（`temp/splitter_probe.cpp`，不入库）在 `FluentUI3Style` 下建一个
`handleWidth(4)` 的水平 `QSplitter`，分别抓普通、`HoverEnter`、左键按下的
手柄图：

| 状态 | 尺寸 | 颜色 | MD5 |
| --- | --- | --- | --- |
| 普通 | 4x200 | `#F3F3F3` | `5e4cbddd...` |
| 悬停 | 4x200 | `#F3F3F3` | `5e4cbddd...` |
| 按下 | 4x200 | `#F3F3F3` | `5e4cbddd...` |

三张图逐字节相同，手柄上没有任何绘制。

### 2.4 关键前提：5.15 的手柄不带 `WA_Hover`

`QSplitterHandle::event()` 里虽然写了 `HoverEnter` / `HoverLeave` 的处理，但
Qt 5.15.2 的 `QSplitterHandle` 从不设置 `WA_Hover`。没有这个属性，Qt 不会向
它投递 hover 事件，`d->hover` 永远为 false，`State_MouseOver` 根本不会出现。

实测（`temp/splitter_hover_probe.cpp`）：

```text
WA_Hover: false underMouse: false hover-recv: no
```

补上属性后，用探针打印 `CE_Splitter` 收到的 state（`temp/splitter_state_probe.cpp`）：

```text
CE_Splitter Horizontal+Enabled QRect(0,0 4x150)          # 普通
CE_Splitter Horizontal+MouseOver+Enabled ...             # HoverEnter
CE_Splitter Horizontal+MouseOver+Sunken+Enabled ...      # 按下
CE_Splitter Horizontal+MouseOver+Enabled ...             # 松开
```

所以“在样式里画 hover 效果”这件事，**必须先在 `polish()` 里给手柄打开
`WA_Hover`**，否则画了也永远不会触发。这是本方案的第一项改动。

## 3. 改动一：`polish()` 打开 `WA_Hover`

`FluentUI3Style::polish(QWidget*)`（`fluentui3style.cpp`，现有分支已处理
combo box、scroll area、tree view 等）增加一条：

```cpp
else if ( auto* splitterHandle = qobject_cast<QSplitterHandle*>( widget ) )
{
    // Qt 5.15 的 QSplitterHandle 没开 WA_Hover，类里的 hover 分支
    // 永远不会执行；这里补上，CE_Splitter 才能收到 State_MouseOver
    splitterHandle->setAttribute( Qt::WA_Hover, true );
}
```

- 只在 `QSplitterHandle` 上开，不影响其他控件。
- 属性在样式 polish 阶段设置即可；`unpolish()` 不需要回收（留 true 无害，
  Qt 自己也不清理）。
- 代价：手柄多收 Enter/Leave，`QSplitterHandle::event()` 里各一次
  `update()`，可忽略。

## 4. 改动二：`drawControl()` 新增 `CE_Splitter`

位置：`FluentUI3Style::drawControl` 的 switch 内，紧跟 `CE_ShapedFrame`
之后（`fluentui3style.cpp` 约 6078 行，`#if QT_CONFIG(progressbar)` 之前），
沿用文件现有的 `case X :` 空格风格与 4 空格缩进。

### 4.1 外观规范

三种状态共用一条**居中圆角短条**，只用粗细和颜色区分：

| 状态 | 判定 | 粗细 | 颜色（浅色/深色主题） | 长度 |
| --- | --- | --- | --- | --- |
| 常态 | 无状态位 | 不绘制 | 保持透明（原来的纯空隙） | — |
| 悬停 | `State_MouseOver` | 2 px | `frameColorStrong`（alpha 0x9B） | 两端各留 4 px |
| 按下 | `State_Sunken` | 3 px | `accentColor(option)` | 两端各留 2 px |
| 禁用 | 无 `State_Enabled` | 不画 | — | — |

- 方向由 `State_Horizontal` 判断：**有**该位表示手柄按水平排列，画竖条；
  否则画横条。与 Qt 的命名一致，别按 `splitter->orientation()` 反推。
- 高对比模式走 `option->palette.color(QPalette::ButtonText)`，和文件里其他
  分支的写法保持一致。
- 常态不绘制：保持现在的纯空隙，避免和面板自带的边框叠成双线；拖动区域
  由光标形状和悬停反馈提示。
- 圆角取 `thickness / 2`，用 `drawRoundedRect` + `Antialiasing`（`drawControl`
  开头已统一设过渲染提示）。
- 不做 hover 淡入动画。样式里有 `QNumberStyleAnimation` +
  `startAnimationEx` 的基础设施，但需要额外的属性键与 `update()` 调度，
  第一版先静态三态，观察实际手感后再决定（见 §9）。

### 4.2 代码骨架

```cpp
        case CE_Splitter :
        {
            if ( !( option->state & State_Enabled ) )
            {
                break;
            }

            const bool horizontal = option->state & State_Horizontal;
            const bool hovered    = option->state & State_MouseOver;
            const bool pressed    = option->state & State_Sunken;

            QColor color;
            qreal thickness  = 1.0;
            qreal endInset   = 4.0;
            if ( pressed )
            {
                color     = accentColor( option );
                thickness = 3.0;
                endInset  = 2.0;
            }
            else if ( hovered )
            {
                color     = highContrastTheme ? option->palette.color( QPalette::ButtonText )
                                              : winUI3Color( frameColorStrong );
                thickness = 2.0;
            }
            else
            {
                color = highContrastTheme ? option->palette.color( QPalette::ButtonText )
                                          : winUI3Color( dividerStrokeDefault );
            }

            const QRectF area = QRectF( option->rect );
            const QRectF bar  = horizontal
                ? QRectF( area.center().x() - thickness / 2.0,
                          area.top() + endInset,
                          thickness,
                          area.height() - 2 * endInset )
                : QRectF( area.left() + endInset,
                          area.center().y() - thickness / 2.0,
                          area.width() - 2 * endInset,
                          thickness );

            painter->setPen( Qt::NoPen );
            painter->setBrush( color );
            painter->drawRoundedRect( bar, thickness / 2.0, thickness / 2.0 );
            break;
        }
```

注意 `drawControl` 开头已经 `painter->save()` 并设了 Antialiasing，分支里
不用再 save/restore；不要把 brush 带出分支。

实施时按 §8 的高 DPI 预案提前加了设备像素对齐：细轴起点用
`qRound(value * devicePixelRatioF()) / dpr` 吸附，粗细也换算成整数设备
像素。否则 1px 线正好落在半像素上，会被抗锯齿糊成两列浅灰（实测确认）。

## 5. 需要同步的文件

| 文件 | 改什么 |
| --- | --- |
| `3rdparty/fluentui/fluentui3style/fluentui3style.cpp` | `polish()` 一条分支 + `drawControl()` 一个 case；补 `#include <QSplitterHandle>`（`QT_CONFIG(splitter)` 保护） |
| `3rdparty/patches/FluentUIStyle/0006-splitter-handle.patch` | 新增，格式同 `0005-menu-indicator-icon-only.patch`（`diff --git a/fluentui3style/...`，路径相对 `3rdparty/fluentui`） |
| `CLAUDE.md` | 「第三方依赖」里 FluentUI 的补丁列表补一条 0006，说明用途 |
| `NOTICE` | 不需要改（第三方许可未变） |

补丁必须入库：干净 checkout 的 CI 直接用 `3rdparty/fluentui` 的现有文件，
不重放补丁就会丢改动。升级 FluentUI 时按现有流程重放 0001–0006。

## 6. 设计取舍

### 6.1 为什么画在 `CE_Splitter` 而不是在宿主里塞自定义 handle

`QSplitter::createHandle()` 是 `protected` 虚函数，宿主可以造 `QSplitterHandle`
子类自己画。但那样每个用 `QSplitter` 的地方都要写一遍子类，`MarkdownView`
之外的消费方也享受不到；而 FluentUI3Style 本来就是“统一外观”的地方。

### 6.2 为什么不用 QSS

`QSplitter::handle` 的 QSS 能做背景色，但拿不到 `:pressed` 以外更细的控制
（粗细按状态变化要重写 `subcontrol-origin` 之类），而且项目约定界面样式不放
QSS。更重要的是：任何控件上一旦设了 stylesheet，`QStyleSheetStyle` 会接管
`CE_Splitter`，我们写的分支直接失效。

### 6.3 为什么不覆盖 `PM_SplitterWidth`

覆盖它会把所有 `QSplitter`/`QDockWidget` 的手柄宽度一并改掉，包括宿主明确
设置过 `handleWidth(4)` 的 `main_splitter_`。宽度归调用方，样式只管画。

### 6.4 常态要不要画

已定：**不画**。两个面板本身已有边框，再画 1 px 会和边框叠成双线；
常态保持纯空隙，只有鼠标移入和按下时才出现分隔条，拖动区域靠光标形状
（`SplitHCursor`）与悬停反馈提示。

## 7. 验证

1. **状态链路**：`temp/splitter_state_probe.cpp`（子类化样式打印 state）确认
   普通 / 悬停 / 按下 / 松开四种情况下 `State_MouseOver`、`State_Sunken`
   按预期出现。已验证前置条件，改动后重跑一次。
2. **外观**：`temp/splitter_probe.cpp` 抓三张手柄图，逐像素对比，确认只有
   设计中的粗细与颜色；水平与垂直 `QSplitter` 各来一张。
3. **应用内**：`qt_md_viewer` 的 `main_splitter_`（`handleWidth(4)`，水平），
   截图确认侧栏 / 正文 / 大纲之间的手柄在悬停与拖动时可见；深色主题各一张。
4. **高对比**：系统高对比模式下确认用 `ButtonText`，仍可见。
5. **编译**：Windows CI 的 `QLITEMD_BUILD_QT_MD_VIEWER=ON` 会编 FluentUI3Style；
   Linux CI 不构建该 app，不覆盖这条路径。
6. **不新增无头测试**：现有 gtest 层没有样式绘制断言手段，靠 `grab()` 比像素
   太脆。样式的回归靠上面的探针图，探针放在 `temp/` 不入库。

## 8. 风险

- **`QDockWidget` 共用 `CE_Splitter`**：Fusion 基样式会在
  `drawComplexControl(CC_TitleBar)` 里用 `CE_Splitter` 画 dock 分隔条。本仓
  基样式是 `windowsvista`，走不到那条路径，但将来换基样式时 dock 的
  分隔条会跟着变，届时需要按 `widget` 类型区分（`widget` 是 `QSplitter`
  才按新规范画）。
- **`widget` 参数是 `QSplitter`**：想在样式里读 `QSplitterHandle` 的属性
  （比如 `objectName`）读不到，别写这种分支。
- **tiny handle**：`handleWidth < 5` 时 `QSplitterHandle` 会设 mask 和
  contents margins，绘制区域是 `contentsRect()`；条不要画出这个矩形。
- **高 DPI**：1 px 线在 125%/150% 缩放下可能变成 2 px 的糊线。骨架里用
  `qreal` 并开 Antialiasing 是折中；若实测发虚，改为按
  `painter->device()->devicePixelRatioF()` 对齐到整数像素。
- **性能**：手柄每次 hover 进出各一次重绘，4 px 宽区域，可忽略。

## 9. 后续可选：悬停淡入

样式里已有 `transitionsEnabled()`、`QNumberStyleAnimation`、`startAnimationEx`
的现成用法（见 checkbox / scrollbar 分支）。要做淡入时：

1. 在 `polish()` 里给 handle 建一个动画上下文对象（参照 `QTreeView` 的
   `_q_branch_anim_ctx` 写法，用 `setProperty` 挂在 handle 上）；
2. `drawControl(CE_Splitter)` 里按动画进度插值 `thickness` 与颜色 alpha；
3. 在 `eventFilter` 里监听 Enter/Leave 触发动画并 `update()`。

这一步会增加状态与清理路径，等静态版本在应用里看顺了再决定要不要做。

## 10. 实施记录（2026-11-26）

- 改动落在 `fluentui3style.cpp` 的 `polish()` 与 `drawControl()`；补丁为
  `3rdparty/patches/FluentUIStyle/0006-splitter-handle.patch`，已对 pristine
  文件用 `git apply --check` 验证可干净应用。
- 离屏探针（`temp/splitter_render_probe.cpp`，不入库）结果：
  - 横、竖手柄 `WA_Hover` 均为 true；
  - 水平：常态不绘制（整条都是背景色 `#F3F3F3`）、悬停 2px `#5F5F5F`、
    按下 3px `#004B84`；
  - 垂直方向与水平一致（横条）。
- 应用内截图：`main_splitter_` 常态保持背景色、无绘制，与改动前一致。
- 悬停/按下未能在应用内截图：本会话无法把窗口切到前台（focus-window 被
  系统拒绝），状态链路以离屏探针为准。
- `ctest -L MARKDOWN`：50/50 通过；未新增无头测试（理由见 §7.6）。
