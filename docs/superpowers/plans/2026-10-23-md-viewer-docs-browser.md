# md_viewer 文档集阅读器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `examples/md_viewer` 从“单层文件列表查看器”改造成面向多层文档集的只读阅读器：递归文档树、文件级书签、文件名过滤、跨文件全文搜索与命中跳转、会话/阅读位置恢复，以及合并菜单与工具条的单行标题栏。

**Architecture:** 可测逻辑拆成四个无窗口模块：`navigation_history`（历史栈）、`document_index`（递归扫描与忽略规则）、`bookmarks`（书签与阅读位置存储）、`text_search`（流式扫描器 + QThread 门面）。侧栏 UI 收敛到 `documents_panel`；`MdViewerWindow` 只做编排和 `MarkdownView` 宿主策略。标题栏通过 vendored FluentUI 的 leading/trailing 两个注入点合并菜单、导航、缩放和全局搜索。

**Tech Stack:** C++17、Qt Widgets 5.12–6.x、CMake/Ninja、GoogleTest + QtTest、FluentUI + QWindowKit、QSettings、funq（仅本地 GUI 冒烟，外部工具）。

**Spec:** `docs/superpowers/specs/2026-10-23-md-viewer-docs-browser-design.md`

## Global Constraints

- 功能改动只落在 `examples/md_viewer/`、`tests/md_viewer/`、测试注册、`3rdparty/fluentui/frameless/`、`patches/FluentUIStyle/` 和文档；不改 `src/markdown/`。
- 新功能只使用 `MarkdownView` 现有公开 API：`LoadFile`、`FindText`、`ScrollPosition`、`SetScrollPosition`、`renderFinished`、`outline`。
- Qt 底线 5.12：不使用 Qt6-only API；`QOperatingSystemVersion` 用版本比较，不用 Qt6 的 `Windows11` 枚举。
- 遵循仓库 C++ 规则：C++17、无命名空间、`PascalCase`/`snake_case`、指针符号跟类型、单行 `if` 也加大括号、自有头文件用 `#define` 守卫、不用 RTTI/C 风格转型/`using namespace std`。
- 新增中文界面文本用 `QStringLiteral`；QObject 派生类里不写中文 `tr()`。
- 新测试一律 `LABELS MARKDOWN`，放在 `tests/md_viewer/`，确保现有 `ctest -L MARKDOWN` 覆盖；可无头运行（Widgets 测试在 `main()` 里 `qputenv("QT_QPA_PLATFORM", "offscreen")`）。
- 本机只验证 Windows VS2019 + Qt 5.15.2 x86：构建目录 `build_qt515_resvg_x86`，构建参数 `--parallel 2`；完整矩阵留给 GitHub CI，由维护者在功能完成后触发。
- 每个任务结束前对改动的 C++ 文件运行 `clang-format -i`，再跑 `clang-format --dry-run --Werror`；格式不过视为任务未完成。
- **不要提交、不要推送、不要触发 CI。** 仓库规则要求所有修改 git 仓库的命令先经维护者确认；任务结束时只报告改动与测试结果，由维护者统一确认提交。
- vendored FluentUI 的改动必须同步记录到 `patches/FluentUIStyle/`（本计划的补丁编号是 `0003-titlebar-widgets.patch`，`0002` 已被 ExWidgets 裁剪占用）。
- 所有临时脚本、样例文档、截图、结论放 `temp/` 下按主题命名的子目录；`temp/` 已在 `.gitignore`，不得入库。
- 本地 GUI 测试可用外部 funq：`D:\trae_workspace\qt-cmake-base-env`；funq 不加入本仓依赖、不进 CI，脚本只放 `temp/ui-tests/`。funq 只能操作普通 Qt 控件，**不能操作 qlitehtml 渲染窗口**：正文点击、选区、滚动位置、`FindText` 的实际效果仍由手工验收。

## Review Focus

1. **超长行 / CRLF / 非法 UTF-8**：100MB 级文件必须流式读取，单行超过读块时不能把整行或整个文件堆进内存，片段长度有上限。测试：`HandlesLongLineAndCrlf`、`HandlesInvalidUtf8`（Task 7）。
2. **书签目标被删除**：条目要保留、置灰，单击不崩溃并由窗口提示“文件已不存在”。测试：`MarksMissingBookmarks`（Task 6）。
3. **非 ASCII / 含空格路径**：书签、阅读位置、搜索结果、文档树都要原样往返，相对路径统一用 `/`。测试：`TogglesAndRoundTrips`（Task 5）、`ScansNestedDocsAndSkipsIgnoredDirs`（Task 2）。
4. **启动时根目录消失或不可读**：必须停在一个可恢复的空态（提示 + 重新选择目录），不能崩溃、不能擅自打开无关目录。测试：`MissingRootReportsNotFound`（Task 2）、`ShowsEmptyStateWhenRootMissing`（Task 4）。
5. **超大目录 / 递归监听**：超过 200 个目录时 watcher 只监听根目录，扫描仍要完成。测试：`WatcherFallsBackToRootBeyondLimit`、`ScansManyDirectories`（Task 2）。

## File Structure

**新建：**

- `examples/md_viewer/navigation_history.{h,cpp}` — 前进/后退栈，无 QObject。
- `examples/md_viewer/document_index.{h,cpp}` — 递归扫描、忽略目录、路径关系、监听目录清单。
- `examples/md_viewer/bookmarks.{h,cpp}` — `BookmarkStore`、`ReadingPositionStore`、书签显示路径。
- `examples/md_viewer/text_search.{h,cpp}` — 片段、单文件扫描、整根扫描、`TextSearch` 线程门面。
- `examples/md_viewer/documents_panel.{h,cpp}` — 侧栏三页签：文档树/书签/搜索；过滤、空态、结果 delegate。
- `tests/md_viewer/CMakeLists.txt` — 新测试目标注册。
- `tests/md_viewer/navigation_history_test.cpp`
- `tests/md_viewer/document_index_test.cpp`
- `tests/md_viewer/bookmarks_test.cpp`
- `tests/md_viewer/text_search_test.cpp`
- `tests/md_viewer/documents_panel_test.cpp`
- `tests/md_viewer/titlebar_hooks_test.cpp`
- `patches/FluentUIStyle/0003-titlebar-widgets.patch`

**修改：**

- `examples/md_viewer/md_viewer_window.{h,cpp}` — 编排、根目录、搜索、会话、大文件确认、watcher。
- `examples/md_viewer/md_viewer_settings.{h,cpp}` — 新增持久化键与局部保存方法。
- `examples/md_viewer/render_settings_panel.{h,cpp}` — 窗口材质下拉框。
- `examples/md_viewer/main.cpp` — 启动恢复入口、初始窗口材质。
- `examples/md_viewer/CMakeLists.txt` — 新增源文件。
- `3rdparty/fluentui/frameless/fluenttitlebar.{h,cpp}` — leading/trailing/title 可见性。
- `3rdparty/fluentui/frameless/fluentwindowframe.cpp` — 对注入控件做 hit test。
- `CMakeLists.txt` — `add_subdirectory(tests/md_viewer)`。
- `README.md`、`README.zh-CN.md`、`CLAUDE.md`、`CHANGELOG.md`、`docs/superpowers/specs/2026-10-23-md-viewer-docs-browser-design.md` — 功能、结构、状态与发布说明。
- `docs/images/screenshot.png` — 新界面截图（Task 10）。

---

### Task 1: NavigationHistory 抽取

**Files:**
- Create: `examples/md_viewer/navigation_history.h`
- Create: `examples/md_viewer/navigation_history.cpp`
- Modify: `examples/md_viewer/md_viewer_window.h`（删除 `HistoryEntry`、`history_`、`history_index_`）
- Modify: `examples/md_viewer/md_viewer_window.cpp`（`PushHistory`/`GoBack`/`GoForward`/`UpdateHistoryActions`）
- Create: `tests/md_viewer/CMakeLists.txt`
- Create: `tests/md_viewer/navigation_history_test.cpp`
- Modify: `CMakeLists.txt`（tests 块）

**Interfaces:**
- Consumes: 现有窗口的历史栈行为（同文件锚点与跨文件跳转共用一个栈）。
- Produces:
  ```cpp
  struct NavigationEntry {
    QString path;
    QString fragment;
    bool operator==(const NavigationEntry& other) const;
  };

  class NavigationHistory {
   public:
    void Push(const NavigationEntry& entry);
    bool CanGoBack() const;
    bool CanGoForward() const;
    std::optional<NavigationEntry> Back();
    std::optional<NavigationEntry> Forward();
    int size() const;
  };
  ```

- [ ] **Step 1: 注册测试目标**

在 `tests/md_viewer/CMakeLists.txt` 写入：

```cmake
set(VIEWER_DIR ${CMAKE_SOURCE_DIR}/examples/md_viewer)

add_unit_test(
    NAME test_viewer_navigation
    SOURCES navigation_history_test.cpp
            ${VIEWER_DIR}/navigation_history.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)
```

顶层 `CMakeLists.txt` 的 `if(QLITEMD_BUILD_TESTS)` 块里，在 `add_subdirectory(tests/markdown)` 后加：

```cmake
add_subdirectory(tests/md_viewer)
qlitemd_skip_subdir_install(tests/md_viewer)
```

- [ ] **Step 2: 写失败测试**

`tests/md_viewer/navigation_history_test.cpp`：

```cpp
#include <gtest/gtest.h>

#include "navigation_history.h"

TEST(NavigationHistoryTest, WalksBackAndForward) {
  NavigationHistory history;
  history.Push({QStringLiteral("/a"), QString()});
  history.Push({QStringLiteral("/b"), QStringLiteral("x")});
  history.Push({QStringLiteral("/c"), QString()});
  ASSERT_EQ(history.size(), 3);
  EXPECT_TRUE(history.CanGoBack());
  EXPECT_EQ(history.Back()->path, QStringLiteral("/b"));
  EXPECT_EQ(history.Back()->fragment, QStringLiteral("x"));
  EXPECT_EQ(history.Back()->path, QStringLiteral("/a"));
  EXPECT_FALSE(history.Back().has_value());
  EXPECT_TRUE(history.CanGoForward());
  EXPECT_EQ(history.Forward()->path, QStringLiteral("/b"));
}

TEST(NavigationHistoryTest, PushAfterBackDropsForwardEntries) {
  NavigationHistory history;
  history.Push({QStringLiteral("/a"), QString()});
  history.Push({QStringLiteral("/b"), QString()});
  history.Push({QStringLiteral("/c"), QString()});
  history.Back();
  history.Push({QStringLiteral("/d"), QString()});
  EXPECT_FALSE(history.CanGoForward());
  EXPECT_EQ(history.size(), 3);
}

TEST(NavigationHistoryTest, ConsecutiveDuplicateIsIgnored) {
  NavigationHistory history;
  history.Push({QStringLiteral("/a"), QString()});
  history.Push({QStringLiteral("/a"), QString()});
  EXPECT_EQ(history.size(), 1);
}
```

- [ ] **Step 3: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_viewer_navigation --parallel 2
```

预期：编译失败，`navigation_history.h: No such file or directory`。先在 VS2019 x86 开发者命令行中执行，或先 `call vcvarsall.bat x86`。

- [ ] **Step 4: 实现模块**

`navigation_history.h` 里包含 `<optional>`、`<QString>`、`<QVector>`（Qt 5.12 需要显式包含）。`Push` 先截断 `index + 1` 之后的项；若新的最后一项与 `entry` 相等则不追加；`index` 指向最后一项。`Back`/`Forward` 在边界返回 `std::nullopt` 且不移动。

- [ ] **Step 5: 运行测试确认通过**

```bat
cmake --build build_qt515_resvg_x86 --target test_viewer_navigation --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_viewer_navigation$" --output-on-failure
```

预期：`100% tests passed, 0 tests failed out of 1`。

- [ ] **Step 6: 窗口改用 NavigationHistory**

`md_viewer_window.h` 删除本地 `HistoryEntry` 结构、`history_`、`history_index_`，改为 `NavigationHistory history_;`。`PushHistory` 改为 `history_.Push({path, fragment}); UpdateHistoryActions();`；`GoBack`/`GoForward` 取 `auto entry = history_.Back()/Forward()`，无值直接返回；`UpdateHistoryActions` 用 `CanGoBack()`/`CanGoForward()`。

- [ ] **Step 7: 构建查看器并回归**

```bat
cmake --build build_qt515_resvg_x86 --target md_viewer --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "test_viewer_navigation|test_markdown_view_actions" --output-on-failure
```

预期：构建成功，两条测试通过。手工打开一篇文档，点几个页内锚点和跨文档链接，再后退/前进，行为与改动前一致。

- [ ] **Step 8: 格式化并报告**

```bat
clang-format -i examples/md_viewer/navigation_history.h examples/md_viewer/navigation_history.cpp examples/md_viewer/md_viewer_window.h examples/md_viewer/md_viewer_window.cpp tests/md_viewer/navigation_history_test.cpp
clang-format --dry-run --Werror examples/md_viewer/navigation_history.h examples/md_viewer/navigation_history.cpp examples/md_viewer/md_viewer_window.h examples/md_viewer/md_viewer_window.cpp tests/md_viewer/navigation_history_test.cpp
```

报告改动的文件与测试输出；不要提交。

---

### Task 2: DocumentIndex 递归扫描与路径规则

**Files:**
- Create: `examples/md_viewer/document_index.h`
- Create: `examples/md_viewer/document_index.cpp`
- Create: `tests/md_viewer/document_index_test.cpp`
- Modify: `tests/md_viewer/CMakeLists.txt`

**Interfaces:**
- Consumes: 无。
- Produces:
  ```cpp
  struct DocumentEntry {
    QString absolute_path;
    QString relative_path;  // 统一使用 '/'
    qint64 size = 0;
    QDateTime modified;
    bool is_dir = false;
  };

  struct DocumentScanResult {
    QString root;                // 绝对路径
    QVector<DocumentEntry> entries;  // 先目录、后文件，各自按 relative_path 不区分大小写排序
    int markdown_file_count = 0;
    bool root_exists = true;     // 根不存在、不是目录或不可读时为 false
  };

  bool IsMarkdownDocumentName(const QString& file_name);
  bool IsIgnoredDirectoryName(const QString& directory_name);
  DocumentScanResult ScanDocumentRoot(const QString& root);
  QStringList CollectMarkdownFiles(const QString& root);  // 绝对路径，按 relative_path 排序
  bool IsPathUnderRoot(const QString& path, const QString& root);
  QStringList DirectoriesToWatch(const DocumentScanResult& result,
                                 int max_directories = 200);
  ```

**行为锁定：**
- Markdown 后缀只有 `.md`、`.markdown`，不区分大小写。
- 忽略目录：名字以 `.` 开头；或等于 `node_modules`（不区分大小写）；或以 `build` 开头（不区分大小写）。树与全文搜索共用这一份规则。
- 只保留“子树中至少有一个 Markdown 文件”的目录。
- 不跟随符号链接（`QDirIterator` 默认行为），避免目录环。
- 根不存在/不可读：`root_exists=false`，`entries` 为空，`markdown_file_count=0`。
- `DirectoriesToWatch`：目录数 > `max_directories` 时只返回根；否则根在前，后接所有目录的绝对路径。

- [ ] **Step 1: 注册测试目标**

在 `tests/md_viewer/CMakeLists.txt` 追加：

```cmake
add_unit_test(
    NAME test_document_index
    SOURCES document_index_test.cpp
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)
```

- [ ] **Step 2: 写失败测试**

`tests/md_viewer/document_index_test.cpp` 至少包含：

```cpp
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "document_index.h"

namespace {
void WriteFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
}
}  // namespace

TEST(DocumentIndexTest, ScansNestedDocsAndSkipsIgnoredDirs) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString root = temp.path();
  ASSERT_TRUE(QDir(root).mkpath("guide/deep"));
  ASSERT_TRUE(QDir(root).mkpath("empty"));
  ASSERT_TRUE(QDir(root).mkpath(".hidden"));
  ASSERT_TRUE(QDir(root).mkpath("node_modules"));
  ASSERT_TRUE(QDir(root).mkpath("build-out"));
  WriteFile(root + "/a.md", "a");
  WriteFile(root + "/guide/b.markdown", "bb");
  WriteFile(root + "/guide/deep/中文 文档.md", "ccc");
  WriteFile(root + "/notes.txt", "n");
  WriteFile(root + "/empty/readme.txt", "n");
  WriteFile(root + "/.hidden/x.md", "x");
  WriteFile(root + "/node_modules/y.md", "y");
  WriteFile(root + "/build-out/z.md", "z");

  const DocumentScanResult result = ScanDocumentRoot(root);
  ASSERT_TRUE(result.root_exists);
  ASSERT_EQ(result.markdown_file_count, 3);
  ASSERT_EQ(result.entries.size(), 5);  // 2 个目录 + 3 个文件
  EXPECT_TRUE(result.entries[0].is_dir);
  EXPECT_EQ(result.entries[0].relative_path, QStringLiteral("guide"));
  EXPECT_TRUE(result.entries[1].is_dir);
  EXPECT_EQ(result.entries[1].relative_path, QStringLiteral("guide/deep"));
  EXPECT_EQ(result.entries[2].relative_path, QStringLiteral("a.md"));
  EXPECT_EQ(result.entries[3].relative_path, QStringLiteral("guide/b.markdown"));
  EXPECT_EQ(result.entries[4].relative_path,
            QStringLiteral("guide/deep/中文 文档.md"));
  EXPECT_EQ(result.entries[4].size, 3);
  EXPECT_EQ(CollectMarkdownFiles(root).size(), 3);
}

TEST(DocumentIndexTest, MissingRootReportsNotFound) {
  const DocumentScanResult result =
      ScanDocumentRoot(QStringLiteral("Z:/definitely/missing"));
  EXPECT_FALSE(result.root_exists);
  EXPECT_TRUE(result.entries.isEmpty());
  EXPECT_EQ(result.markdown_file_count, 0);
}

TEST(DocumentIndexTest, MatchesIgnoreRules) {
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral(".git")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("node_modules")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("Node_Modules")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("build")));
  EXPECT_TRUE(IsIgnoredDirectoryName(QStringLiteral("build_qt515")));
  EXPECT_FALSE(IsIgnoredDirectoryName(QStringLiteral("docs")));
  EXPECT_TRUE(IsMarkdownDocumentName(QStringLiteral("A.MD")));
  EXPECT_TRUE(IsMarkdownDocumentName(QStringLiteral("a.markdown")));
  EXPECT_FALSE(IsMarkdownDocumentName(QStringLiteral("a.txt")));
}

TEST(DocumentIndexTest, PathsRespectRootBoundary) {
  const QString root = QDir::tempPath() + QStringLiteral("/qlitemd-root");
  EXPECT_TRUE(IsPathUnderRoot(root + QStringLiteral("/a.md"), root));
  EXPECT_TRUE(IsPathUnderRoot(root, root));
  EXPECT_FALSE(IsPathUnderRoot(root + QStringLiteral("/../outside.md"), root));
  EXPECT_FALSE(IsPathUnderRoot(root + QStringLiteral("2/a.md"), root));
}

TEST(DocumentIndexTest, WatcherFallsBackToRootBeyondLimit) {
  DocumentScanResult result;
  result.root = QStringLiteral("/docs");
  for (int i = 0; i < 201; ++i) {
    DocumentEntry dir;
    dir.absolute_path = QStringLiteral("/docs/d%1").arg(i);
    dir.relative_path = QStringLiteral("d%1").arg(i);
    dir.is_dir = true;
    result.entries.append(dir);
  }
  EXPECT_EQ(DirectoriesToWatch(result), QStringList{result.root});
}

TEST(DocumentIndexTest, ScansManyDirectories) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  for (int i = 0; i < 205; ++i) {
    const QString dir = temp.path() + QStringLiteral("/d%1").arg(i);
    ASSERT_TRUE(QDir().mkpath(dir));
    WriteFile(dir + QStringLiteral("/x.md"), "x");
  }
  const DocumentScanResult result = ScanDocumentRoot(temp.path());
  EXPECT_TRUE(result.root_exists);
  EXPECT_EQ(result.markdown_file_count, 205);
}
```

- [ ] **Step 3: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_document_index --parallel 2
```

预期：编译失败（`document_index.h` 不存在）。

- [ ] **Step 4: 实现扫描与路径函数**

用 `QDirIterator(root, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)` 递归；遇到 `IsIgnoredDirectoryName(fileInfo.fileName())` 的目录时不进入。先收集 Markdown 文件及其所有祖先目录，再生成 `entries`：目录按 `relative_path` 排序在前，文件排序在后。`absolute_path` 用 `QFileInfo::absoluteFilePath()`；`relative_path` 用 `QDir(root).relativeFilePath(...)` 并 `QDir::fromNativeSeparators`。`DirectoriesToWatch` 目录数用 `entries` 中 `is_dir` 计数。`IsPathUnderRoot` 先用 `QDir::cleanPath` 规整两侧，根补尾部 `/` 后做前缀比较；Windows 不区分大小写，其它平台区分。根不是目录、不可读时 `root_exists=false`。

- [ ] **Step 5: 运行测试确认通过**

```bat
cmake --build build_qt515_resvg_x86 --target test_document_index --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_document_index$" --output-on-failure
```

预期：1/1 通过。

- [ ] **Step 6: 格式化并报告**

对 `document_index.{h,cpp}` 与测试运行 `clang-format -i` + `--dry-run --Werror`；报告结果，不提交。

---

### Task 3: FluentUI 标题栏注入点 + 单行标题栏 + 窗口材质

**Files:**
- Modify: `3rdparty/fluentui/frameless/fluenttitlebar.h`
- Modify: `3rdparty/fluentui/frameless/fluenttitlebar.cpp`
- Modify: `3rdparty/fluentui/frameless/fluentwindowframe.cpp`
- Create: `patches/FluentUIStyle/0003-titlebar-widgets.patch`
- Create: `tests/md_viewer/titlebar_hooks_test.cpp`
- Modify: `tests/md_viewer/CMakeLists.txt`
- Modify: `examples/md_viewer/md_viewer_window.{h,cpp}`（`createActions`/`initUi`/`initSignals`/新的 `resizeEvent`）
- Modify: `examples/md_viewer/md_viewer_settings.{h,cpp}`
- Modify: `examples/md_viewer/render_settings_panel.{h,cpp}`
- Modify: `examples/md_viewer/main.cpp`

**Interfaces:**
- Consumes: 现有 `FluentTitleBar`、`FluentWindowFrame`、`QMenuBar` 下的全部动作。
- Produces:
  ```cpp
  // fluenttitlebar.h
  QWidget* leadingWidget() const;
  QWidget* trailingWidget() const;
  void setLeadingWidget(QWidget* widget);   // 图标之后、标题之前
  void setTrailingWidget(QWidget* widget);  // 搜索框之前
  void setTitleVisible(bool visible);

  enum class WindowMaterial { kAuto = 0, kAcrylic = 1, kNone = 2 };
  ```
  `RenderSettingsPanel` 新增：
  ```cpp
  void SetWindowMaterial(WindowMaterial material);
  signals: void windowMaterialChanged(WindowMaterial material);
  ```
  窗口新增 `QWidget* nav_widget_`（objectName `navWidget`）、`QToolButton* menu_button_`（objectName `mainMenuButton`）、`QMenu* app_menu_`、`QMenu* view_menu_`、`WindowMaterial window_material_`。

- [ ] **Step 1: 写标题栏注入点测试**

`tests/md_viewer/titlebar_hooks_test.cpp`：

```cpp
#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>

#include <fluenttitlebar.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(TitleBarHooksTest, InsertsLeadingAndTrailingWidgets) {
  QMainWindow window;
  FluentTitleBar bar(&window);
  auto* title = bar.findChild<QLabel*>(QStringLiteral("fluent-title-label"));
  ASSERT_NE(title, nullptr);

  auto* leading = new QWidget(&bar);
  leading->setObjectName(QStringLiteral("leadingProbe"));
  bar.setLeadingWidget(leading);
  EXPECT_EQ(bar.leadingWidget(), leading);

  auto* trailing = new QWidget(&bar);
  trailing->setObjectName(QStringLiteral("trailingProbe"));
  bar.setTrailingWidget(trailing);
  EXPECT_EQ(bar.trailingWidget(), trailing);

  QLayout* layout = bar.layout();
  ASSERT_NE(layout, nullptr);
  EXPECT_LT(layout->indexOf(leading), layout->indexOf(title));
  EXPECT_LT(layout->indexOf(title), layout->indexOf(trailing));
  EXPECT_LT(layout->indexOf(trailing), layout->indexOf(bar.searchLineEdit()));
}

TEST(TitleBarHooksTest, TitleCanBeHidden) {
  QMainWindow window;
  FluentTitleBar bar(&window);
  auto* title = bar.findChild<QLabel*>(QStringLiteral("fluent-title-label"));
  ASSERT_NE(title, nullptr);
  EXPECT_FALSE(title->isHidden());
  bar.setTitleVisible(false);
  EXPECT_TRUE(title->isHidden());
  bar.setTitleVisible(true);
  EXPECT_FALSE(title->isHidden());
}
```

- [ ] **Step 2: 注册测试目标并确认失败**

在 `tests/md_viewer/CMakeLists.txt` 追加（FluentUI 只在构建 md_viewer 时存在）：

```cmake
if(TARGET Frameless)
    add_unit_test(
        NAME test_titlebar_hooks
        SOURCES titlebar_hooks_test.cpp
        INCLUDES ${CMAKE_SOURCE_DIR}/3rdparty/fluentui/frameless
        LIBS Frameless
        QT_MODULES Widgets Test
        LABELS MARKDOWN
        NO_GTEST_MAIN
    )
endif()
```

```bat
cmake --build build_qt515_resvg_x86 --target test_titlebar_hooks --parallel 2
```

预期：编译失败，`setLeadingWidget` 等成员不存在。

- [ ] **Step 3: 给 FluentTitleBar 加注入点**

在 `fluenttitlebar.h` 增加上面列出的五个公开方法；私有成员加 `QWidget* m_leadingWidget{nullptr};`、`QWidget* m_trailingWidget{nullptr};`、`QHBoxLayout* m_layout{nullptr};`。

`fluenttitlebar.cpp`：
- 构造时把 `auto *layout = new QHBoxLayout(this);` 改为赋给 `m_layout`。
- `setLeadingWidget`：若传入同一个指针直接返回；若已有旧控件，先从布局移除并 `deleteLater()`；新控件若没有父对象则 `setParent(this)`；`m_layout->insertWidget(1, widget)`（索引 0 是图标）；保存指针。
- `setTrailingWidget`：同上，但插入位置是 `m_layout->indexOf(m_searchLineEdit)`，即搜索框之前。
- `setTitleVisible`：`m_titleLabel->setVisible(visible)`。
- `leadingWidget()`/`trailingWidget()` 返回保存的指针。

- [ ] **Step 4: FluentWindowFrame 注册 hit test**

在 `attachChromeHeader()` 的 `#ifndef Q_OS_MAC` 块里，已注册的按钮后面加：

```cpp
if (auto *leading = m_titleBar->leadingWidget())
{
    m_windowAgent->setHitTestVisible(leading, true);
}
if (auto *trailing = m_titleBar->trailingWidget())
{
    m_windowAgent->setHitTestVisible(trailing, true);
}
```

容器控件覆盖其子按钮的矩形，QWK 的命中判断按控件矩形做，因此注册容器即可。注入控件在 `show()` 前设置；`FluentWindowFrame` 在 `QEvent::Show` 时会再次调用 `attachChromeHeader()`，因此 hit test 会在窗口显示时注册。

- [ ] **Step 5: 运行标题栏测试**

```bat
cmake --build build_qt515_resvg_x86 --target test_titlebar_hooks --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_titlebar_hooks$" --output-on-failure
```

预期：2/2 通过。

- [ ] **Step 6: 记录 vendor 补丁**

生成与 `patches/FluentUIStyle/0002-exwidgets-trim.patch` 同格式的补丁（`a/frameless/...`、`b/frameless/...`），内容覆盖 `fluenttitlebar.h`、`fluenttitlebar.cpp`、`fluentwindowframe.cpp` 三个文件相对上游 `FluentUIStyle-4.0` 的差异：

```bat
git diff --no-index -- D:\software\aa-sources\FluentUIStyle-4.0\frameless\fluenttitlebar.h 3rdparty\fluentui\frameless\fluenttitlebar.h > temp\titlebar-h.diff
```

对三个文件分别生成后合并、把路径改成 `a/frameless/...` / `b/frameless/...`，写入 `patches/FluentUIStyle/0003-titlebar-widgets.patch`。验证：

```bat
findstr /C:"setLeadingWidget" patches\FluentUIStyle\0003-titlebar-widgets.patch
findstr /C:"setHitTestVisible(leading" patches\FluentUIStyle\0003-titlebar-widgets.patch
```

预期：两条都能命中。

- [ ] **Step 7: 窗口改为单行标题栏**

`md_viewer_window.cpp`：

1. `setObjectName(QStringLiteral("mdViewerWindow"));`（funq 冒烟按 objectName 取路径）。
2. 删除 `addToolBar(...)` 整段与 `menuBar()->addMenu(...)`；不再包含 `<QMenuBar>`、`<QToolBar>`。
3. 在 `initUi()` 里先建 `fluent_frame_ = new FluentWindowFrame(this, this);`，再 `fluent_frame_->installChromeHeader(nullptr);`（`installChromeHeader` 已支持空菜单栏）。
4. **在四个 dock 创建之后**建 `app_menu_ = new QMenu(this);`，三个子菜单：
   - 文件：打开文件…、打开目录…、最近打开、分隔线、导出 HTML…、导出 PDF…、分隔线、退出
   - 视图：文档/大纲/渲染状态/渲染设置四个 dock 的 `toggleViewAction()`、分隔线、后退、前进、刷新（F5）
   - 帮助：关于
5. `menu_button_ = new QToolButton(fluent_frame_->titleBar());`，objectName `mainMenuButton`，`setIcon(style()->standardIcon(QStyle::SP_TitleBarMenuButton))`；图标为空时 `setText(QStringLiteral("≡"))` 兜底（不使用 emoji）；`setPopupMode(QToolButton::InstantPopup)`，`setMenu(app_menu_)`，tooltip `菜单`；`fluent_frame_->titleBar()->setLeadingWidget(menu_button_)`。
6. `nav_widget_ = new QWidget(fluent_frame_->titleBar());` objectName `navWidget`，水平布局、零边距、间距 2。依次放 `back_action_`、`forward_action_`、`zoom_out_action_`、`zoom_reset_action_`、`zoom_in_action_` 对应的 `QToolButton`（`setDefaultAction`）。后退/前进用 `SP_ArrowBack`/`SP_ArrowForward`；缩放按钮文本 `−`、`100%`、`+`，objectName `zoomOut`/`zoomReset`/`zoomIn`。`fluent_frame_->titleBar()->setTrailingWidget(nav_widget_)`。
7. 把这些动作也 `addAction` 到窗口，确保 `Ctrl+Alt+←`/`→` 快捷键在隐藏菜单时仍生效。
8. `resizeEvent(QResizeEvent*)`：宽度 `< 900` 隐藏缩放三个按钮；`< 780` 隐藏 `search_edit_`；`< 660` 调用 `fluent_frame_->titleBar()->setTitleVisible(false)`，否则 `true`。`initUi` 末尾手动调一次同样的逻辑（构造时没有 resize 事件）。
9. `FocusSearch()` 里若搜索框被隐藏，先 `search_edit_->setVisible(true)` 再聚焦。

- [ ] **Step 8: 窗口材质三档**

`md_viewer_settings.h` 定义 `enum class WindowMaterial { kAuto = 0, kAcrylic = 1, kNone = 2 };`，把 `bool window_blur` 换成 `WindowMaterial window_material = WindowMaterial::kAuto;`。

`md_viewer_settings.cpp`：
- 新键 `appearance/window_material`（int）；`Load` 时若没有该键，读旧的 `appearance/window_blur`（true→kAuto，false→kNone）；`Save` 同时写两个键（旧键写 `material != kNone`）。
- `main.cpp` 的 `_q_widget_mode` 改为 `settings.window_material == WindowMaterial::kNone ? 0 : 2`。

`render_settings_panel`：Windows 下把 `QCheckBox* window_blur_` 换成 `QComboBox* window_material_`，条目 `自动（Mica）`=kAuto、`亚克力`=kAcrylic、`关闭`=kNone；`SetWindowMaterial`/`windowMaterial`/`windowMaterialChanged` 按 Interfaces 实现。非 Windows 不创建该控件。

`md_viewer_window`：
- 新增 `void ApplyWindowMaterial(WindowMaterial material, bool announce);`
- `#ifdef Q_OS_WIN`：`_q_widget_mode` 设为 `material == kNone ? 0 : 2`，`qApp->setStyle(new FluentUI3Style);`，`clearWindowBackdrop()`；非 kNone 时选 key：kAcrylic → `acrylic-material`；kAuto → Windows 11（`QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000)`）用 `mica`，否则 `dwm-blur`；`setWindowBackdrop(key)` 失败时回退 `dwm-blur`。非 Windows 只保存值。
- `initUi` 里把 `render_settings_->SetWindowBlurEnabled(...)` 换成 `render_settings_->SetWindowMaterial(settings_->window_material)`；在创建 frame 后调 `ApplyWindowMaterial(settings_->window_material, /*announce=*/false);`。
- `initSignals` 把原来的 `windowBlurEnabledChanged` 连接换成 `windowMaterialChanged`，槽 `ApplyWindowMaterial(material, /*announce=*/true)`。
- `SaveSettings` 写 `window_material_`。
- 删除 `SetWindowBlur`。

- [ ] **Step 9: 构建并手工验收**

```bat
cmake --build build_qt515_resvg_x86 --target md_viewer --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "test_titlebar_hooks|test_render_settings_panel" --output-on-failure
```

**实现取舍：** 透明材质只作用于窗口壳、标题栏和侧栏；`MarkdownView` 的正文背景由库的主题 CSS 决定，本计划不改库，因此正文保持主题的不透明背景。验收 Mica/亚克力时以标题栏、侧栏和窗口边缘透出材质为准；这条差异在 Task 10 写进设计文档的实现备注。

手工清单：
- 标题栏只有一行；汉堡菜单能打开文件/视图/帮助，所有原快捷键（Ctrl+O、Ctrl+Q、Ctrl+Alt+←/→）仍生效。
- 搜索框、主题按钮、置顶、最小化/最大化/关闭都能点；标题栏空白处可拖动窗口。
- 窗口缩到 900/780/660 以下时，缩放组、搜索框、标题依次隐藏，菜单与前进/后退仍在。
- Windows 11 上默认 Mica 透出；切“亚克力”有材质变化；切“关闭”后背景不透明；重启后材质保持。

- [ ] **Step 10: 格式化并报告**

对本任务改动的 C++ 文件运行 `clang-format -i` 与 `--dry-run --Werror`；报告测试与手工结果，不提交。

---

### Task 4: DocumentsPanel 与递归文档树

**Files:**
- Create: `examples/md_viewer/documents_panel.h`
- Create: `examples/md_viewer/documents_panel.cpp`
- Create: `tests/md_viewer/documents_panel_test.cpp`
- Modify: `tests/md_viewer/CMakeLists.txt`
- Modify: `examples/md_viewer/CMakeLists.txt`
- Modify: `examples/md_viewer/md_viewer_settings.{h,cpp}`（加 `root_dir`/`last_file` 与 `SaveSession()`）
- Modify: `examples/md_viewer/md_viewer_window.{h,cpp}`

**Interfaces:**
- Consumes: `DocumentScanResult`、`DocumentEntry`、`ScanDocumentRoot`、`IsPathUnderRoot`（Task 2）。
- Produces:
  ```cpp
  constexpr int kPathRole = Qt::UserRole;
  constexpr int kRelativePathRole = Qt::UserRole + 1;
  constexpr int kIsDirectoryRole = Qt::UserRole + 2;
  constexpr int kMissingRole = Qt::UserRole + 3;
  constexpr int kSearchHitRole = Qt::UserRole + 4;

  class DocumentsPanel : public QWidget {
    Q_OBJECT
   public:
    enum class Tab { kDocuments = 0, kBookmarks = 1, kSearch = 2 };
    explicit DocumentsPanel(QWidget* parent = nullptr);

    void SetRoot(const DocumentScanResult& result);
    void SetRootMissing(const QString& root, const QString& reason);
    void SetFilter(const QString& text);
    void SetCurrentFile(const QString& absolute_path);
    void ShowTab(Tab tab);
    Tab currentTab() const;

   signals:
    void fileActivated(const QString& absolute_path);
    void chooseRootRequested();
  };
  ```
  objectName：`sidebarTabs`、`documentFilterEdit`、`documentTree`、`documentStack`、`sidebarEmptyPage`、`sidebarEmptyLabel`、`sidebarChooseRootButton`、`sidebarFooterLabel`、`sidebarCancelButton`。

  窗口新增：
  ```cpp
  DocumentScanResult SetRootDir(const QString& root);
  void EnsureRootForFile(const QString& path);
  QString OpenPathCore(const QString& path);
  ```
  `MdViewerSettings` 新增 `QString root_dir; QString last_file;`，键 `root/dir`、`session/last_file`；新增 `SaveSession() const`（只写这两个键）。
  `ShowFile` 的行为在 Task 9 再加 reason 参数；本任务先保持现有签名。

**树结构：**
- 顶层是根目录项（文本为根目录名；根为盘符时用绝对路径），展开。
- 目录可展开；文件显示文件名 + 大小（第二列）；tooltip 是绝对路径。
- 只显示 `DocumentScanResult` 里的条目（已由扫描剔除无 md 的目录与忽略目录）。
- 单击目录切换展开；单击文件发 `fileActivated`。
- 过滤框按相对路径不区分大小写匹配；命中的节点及其祖先可见，命中的目录自动展开；清空恢复全部；不重建模型。
- `SetRoot` 重建前记录所有展开目录的绝对路径与当前选中文件，重建后恢复仍存在的展开项与选中项。
- 无文档/根缺失时显示空态页（`documentStack` 第二页），含提示与“重新选择目录”按钮。

- [ ] **Step 1: 注册测试目标**

在 `tests/md_viewer/CMakeLists.txt` 追加：

```cmake
add_unit_test(
    NAME test_documents_panel
    SOURCES documents_panel_test.cpp
            ${VIEWER_DIR}/documents_panel.cpp
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Widgets Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)
```

在 `examples/md_viewer/CMakeLists.txt` 的 `ALL_SRCS` 加 `documents_panel.cpp`、`documents_panel.h`、`document_index.cpp`、`document_index.h`。

- [ ] **Step 2: 写失败测试**

`tests/md_viewer/documents_panel_test.cpp` 的 `main()` 用 offscreen QApplication；测试用手工构造的 `DocumentScanResult`：

```cpp
namespace {
DocumentScanResult SampleRoot() {
  DocumentScanResult result;
  result.root = QStringLiteral("/docs");
  result.root_exists = true;
  DocumentEntry guide;
  guide.absolute_path = result.root + QStringLiteral("/guide");
  guide.relative_path = QStringLiteral("guide");
  guide.is_dir = true;
  DocumentEntry deep = guide;
  deep.absolute_path += QStringLiteral("/deep");
  deep.relative_path = QStringLiteral("guide/deep");
  DocumentEntry a;
  a.absolute_path = result.root + QStringLiteral("/a.md");
  a.relative_path = QStringLiteral("a.md");
  DocumentEntry b = a;
  b.absolute_path = result.root + QStringLiteral("/guide/b.md");
  b.relative_path = QStringLiteral("guide/b.md");
  DocumentEntry c = a;
  c.absolute_path = result.root + QStringLiteral("/guide/deep/c.md");
  c.relative_path = QStringLiteral("guide/deep/c.md");
  result.entries = {guide, deep, a, b, c};
  result.markdown_file_count = 3;
  return result;
}
}  // namespace

TEST(DocumentsPanelTest, BuildsOnlyDocDirectories) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  QTreeWidgetItem* root = tree->topLevelItem(0);
  EXPECT_TRUE(root->isExpanded());
  ASSERT_EQ(root->childCount(), 2);  // guide, a.md
  EXPECT_EQ(root->child(0)->text(0), QStringLiteral("guide"));
  EXPECT_EQ(root->child(1)->text(0), QStringLiteral("a.md"));
}

TEST(DocumentsPanelTest, FilterHidesNonMatchingNodes) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  auto* filter =
      panel.findChild<QLineEdit*>(QStringLiteral("documentFilterEdit"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(filter, nullptr);
  QTreeWidgetItem* root = tree->topLevelItem(0);
  QTreeWidgetItem* guide = root->child(0);
  QTreeWidgetItem* a = root->child(1);
  QTreeWidgetItem* deep = guide->child(0);  // 目录先于文件
  QTreeWidgetItem* b = guide->child(1);

  filter->setText(QStringLiteral("B."));
  EXPECT_FALSE(root->isHidden());
  EXPECT_FALSE(guide->isHidden());
  EXPECT_FALSE(b->isHidden());
  EXPECT_TRUE(a->isHidden());
  EXPECT_TRUE(deep->isHidden());

  filter->clear();
  EXPECT_FALSE(a->isHidden());
  EXPECT_FALSE(deep->isHidden());
}

TEST(DocumentsPanelTest, PreservesExpansionAcrossRebuild) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  QTreeWidgetItem* root = tree->topLevelItem(0);
  QTreeWidgetItem* guide = root->child(0);
  guide->setExpanded(true);
  panel.SetRoot(SampleRoot());
  root = tree->topLevelItem(0);
  guide = root->child(0);
  EXPECT_TRUE(guide->isExpanded());
}

TEST(DocumentsPanelTest, ShowsEmptyStateWhenRootMissing) {
  DocumentsPanel panel;
  panel.SetRootMissing(QStringLiteral("/gone"),
                       QStringLiteral("目录不存在或不可读"));
  auto* stack = panel.findChild<QStackedWidget*>(QStringLiteral("documentStack"));
  auto* empty =
      panel.findChild<QWidget*>(QStringLiteral("sidebarEmptyPage"));
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(stack->currentWidget(), empty);
}
```

- [ ] **Step 3: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_documents_panel --parallel 2
```

预期：编译失败（`documents_panel.h` 不存在）。

- [ ] **Step 4: 实现 DocumentsPanel**

用 `QTabWidget`（与原型一致，设计稿允许 tab / 折叠二选一）。文档页结构：过滤框 + `QStackedWidget`（树页 / 空态页）。树用 `QTreeWidget`，两列、表头隐藏、第二列右对齐显示 `QLocale().formattedDataSize(size, 2, QLocale::DataSizeTraditionalFormat)`（Qt 5.12 可用）。构建时两遍插入：先目录（父目录一定先于子目录），再文件。过滤用一个递归函数：自身匹配或任一子项可见则可见；匹配的目录 `setExpanded(true)`。footer 居中显示 `共 N 篇文档`。空态页的按钮发 `chooseRootRequested()`。

- [ ] **Step 5: 运行面板测试**

```bat
cmake --build build_qt515_resvg_x86 --target test_documents_panel --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_documents_panel$" --output-on-failure
```

预期：4/4 通过。

- [ ] **Step 6: 窗口接入根目录管理**

`md_viewer_window`：
- 成员：`DocumentsPanel* documents_panel_`、`QString root_dir_`；删除 `QListWidget* file_list_`、`listed_dir_`、`RefreshDirectoryListing`、`SelectInList`。
- `initUi`：把 `QDockWidget("文档")` 的 widget 换成 `documents_panel_`，objectName `documentDock` 不变；连接 `fileActivated` → `NavigateTo(path, {}, true)`；`chooseRootRequested` → `OpenDirectoryDialog()`。菜单 `视图` 的 dock toggle 保持。
- `SetRootDir(root)`：`const DocumentScanResult result = ScanDocumentRoot(root);` 若 `root_exists`：`root_dir_ = result.root; settings_->root_dir = root_dir_; settings_->SaveSession();` 并 `documents_panel_->SetRoot(result);`；否则 `root_dir_ = QFileInfo(root).absoluteFilePath(); documents_panel_->SetRootMissing(root_dir_, ...)`。返回 `result`。`SaveSession` 只写 `root/dir` 与 `session/last_file`，避免用旧窗口快照覆盖其它设置。
- `EnsureRootForFile(path)`：`root_dir_` 非空且 `IsPathUnderRoot(path, root_dir_)` 时保持；否则 `SetRootDir(QFileInfo(path).absolutePath())`。
- `OpenPathCore`：目录 → `SetRootDir`，若 `markdown_file_count == 0` 返回空；否则取 `entries` 里第一个非目录项作为首篇并 `ShowFile`。文件 → `EnsureRootForFile` 后 `ShowFile`。书签等“保持当前根”的入口在后续任务单独处理。
- `ShowFile`：删除“刷新同目录列表”的一段；改为 `documents_panel_->SetCurrentFile(path)`。
- `OpenLastFile`：先看 `settings_->root_dir`；非空且是目录就 `SetRootDir` 并加载 `settings_->last_file`（存在时），否则维持现有最近文件逻辑；完整会话恢复在 Task 9 补齐。本任务只需保证打开目录/文件后树正确。

- [ ] **Step 7: 构建并手工验收**

```bat
cmake --build build_qt515_resvg_x86 --target md_viewer --parallel 2
```

打开一个多层文档目录：树递归、只显示含 md 的目录、忽略目录不出现；过滤框即时隐藏；点文件能打开；打开单文件时若在当前根下，树不换根。

- [ ] **Step 8: 格式化并报告**

格式检查同上；报告测试与手工结果，不提交。

---

### Task 5: 书签与阅读位置存储

**Files:**
- Create: `examples/md_viewer/bookmarks.h`
- Create: `examples/md_viewer/bookmarks.cpp`
- Create: `tests/md_viewer/bookmarks_test.cpp`
- Modify: `tests/md_viewer/CMakeLists.txt`
- Modify: `examples/md_viewer/md_viewer_settings.{h,cpp}`
- Modify: `examples/md_viewer/CMakeLists.txt`

**Interfaces:**
- Consumes: `IsPathUnderRoot`（Task 2）。
- Produces:
  ```cpp
  struct Bookmark {
    QString path;  // 绝对路径
    QString root;  // 星标时的根目录
    bool operator==(const Bookmark& other) const;
  };

  class BookmarkStore {
   public:
    static BookmarkStore FromJson(const QString& json);
    QString ToJson() const;
    bool Contains(const QString& path) const;
    bool Toggle(const QString& path, const QString& root);  // true=已收藏
    void Remove(const QString& path);
    const QVector<Bookmark>& items() const;
   private:
    QVector<Bookmark> items_;
  };

  struct ReadingPosition {
    QString path;
    int scroll = 0;
    qint64 touched = 0;
  };

  class ReadingPositionStore {
   public:
    static ReadingPositionStore FromJson(const QString& json,
                                         int capacity = 500);
    QString ToJson() const;
    int ScrollFor(const QString& path) const;  // 不存在返回 -1
    void Update(const QString& path, int scroll, qint64 now_seconds);
    int size() const;
  };

  QString DisplayBookmarkPath(const Bookmark& bookmark,
                              const QString& current_root);
  ```
- `MdViewerSettings` 在 Task 4 的 `root_dir`/`last_file` 之外新增字段 `QString bookmarks_json; QString positions_json;`，键 `bookmarks/files`、`session/positions`；新增 `SaveBookmarks() const`、`SavePositions() const`（各写一个键）。`Load()`/`Save()` 覆盖这些新键（`root`/`last_file` 由 Task 4 加入）。

**JSON 格式：**
- 书签：`[{"path":"…","root":"…"}]`，保持插入顺序（新收藏在前）。
- 位置：`[{"path":"…","scroll":123,"touched":1700000000}]`，数组顺序即 LRU 顺序（最近在前）。
- 非法 JSON / 缺字段：跳过该项，不抛异常。

- [ ] **Step 1: 注册测试目标**

在 `tests/md_viewer/CMakeLists.txt` 追加：

```cmake
add_unit_test(
    NAME test_bookmarks
    SOURCES bookmarks_test.cpp
            ${VIEWER_DIR}/bookmarks.cpp
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core
    LABELS MARKDOWN
)
```

同时把 `bookmarks.cpp`、`bookmarks.h` 加进 `examples/md_viewer/CMakeLists.txt` 的 `ALL_SRCS`。

- [ ] **Step 2: 写失败测试**

```cpp
#include <gtest/gtest.h>

#include <QDir>

#include "bookmarks.h"

TEST(BookmarkStoreTest, TogglesAndRoundTrips) {
  BookmarkStore store;
  EXPECT_TRUE(store.Toggle(QStringLiteral("C:/docs/甲 文档.md"),
                           QStringLiteral("C:/docs")));
  EXPECT_TRUE(store.Contains(QStringLiteral("C:/docs/甲 文档.md")));
  EXPECT_FALSE(store.Toggle(QStringLiteral("C:/docs/甲 文档.md"),
                            QStringLiteral("C:/docs")));
  EXPECT_FALSE(store.Contains(QStringLiteral("C:/docs/甲 文档.md")));

  store.Toggle(QStringLiteral("C:/docs/a.md"), QStringLiteral("C:/docs"));
  store.Toggle(QStringLiteral("C:/docs/b.md"), QStringLiteral("C:/docs"));
  const BookmarkStore loaded = BookmarkStore::FromJson(store.ToJson());
  ASSERT_EQ(loaded.items().size(), 2);
  EXPECT_EQ(loaded.items()[0].path, QStringLiteral("C:/docs/b.md"));
  EXPECT_EQ(loaded.items()[1].path, QStringLiteral("C:/docs/a.md"));
  EXPECT_EQ(BookmarkStore::FromJson(QStringLiteral("not json")).items().size(),
            0);
}

TEST(BookmarkStoreTest, DisplaysRelativeToCurrentRootFirst) {
  const QString root = QDir::tempPath() + QStringLiteral("/qlitemd-docs");
  const QString guide = root + QStringLiteral("/guide");
  const Bookmark bookmark{guide + QStringLiteral("/a.md"), root};
  EXPECT_EQ(DisplayBookmarkPath(bookmark, guide), QStringLiteral("a.md"));
  EXPECT_EQ(DisplayBookmarkPath(bookmark, QDir::tempPath() +
                                             QStringLiteral("/other")),
            QStringLiteral("guide/a.md"));
  const QString outside_path =
      QDir::tempPath() + QStringLiteral("/other/b.md");
  const Bookmark outside{outside_path,
                         QDir::tempPath() + QStringLiteral("/other")};
  EXPECT_EQ(DisplayBookmarkPath(outside, root),
            QDir::fromNativeSeparators(outside_path));
}

TEST(ReadingPositionStoreTest, EvictsLeastRecentlyUsed) {
  ReadingPositionStore store = ReadingPositionStore::FromJson(QString(), 3);
  store.Update(QStringLiteral("a"), 10, 1);
  store.Update(QStringLiteral("b"), 20, 2);
  store.Update(QStringLiteral("c"), 30, 3);
  store.Update(QStringLiteral("b"), 21, 4);
  store.Update(QStringLiteral("d"), 40, 5);  // a 应被淘汰
  EXPECT_EQ(store.ScrollFor(QStringLiteral("a")), -1);
  EXPECT_EQ(store.ScrollFor(QStringLiteral("b")), 21);
  EXPECT_EQ(store.size(), 3);
  const ReadingPositionStore loaded =
      ReadingPositionStore::FromJson(store.ToJson(), 3);
  EXPECT_EQ(loaded.ScrollFor(QStringLiteral("d")), 40);
}
```

- [ ] **Step 3: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_bookmarks --parallel 2
```

预期：编译失败（`bookmarks.h` 不存在）。

- [ ] **Step 4: 实现**

JSON 用 `QJsonDocument`/`QJsonArray`/`QJsonObject`，输出 `QJsonDocument::Compact`。`Toggle` 已存在则移除并返回 false，否则 prepend 并返回 true。`Update` 移除同路径项、prepend、截断到 capacity（`capacity < 1` 时按 1）。补 `bookmarks_json`/`positions_json` 字段与 `SaveBookmarks()`/`SavePositions()` 两个局部保存方法。

- [ ] **Step 5: 运行测试确认通过**

```bat
cmake --build build_qt515_resvg_x86 --target test_bookmarks --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_bookmarks$" --output-on-failure
```

预期：3/3 通过。

- [ ] **Step 6: 格式化并报告**

格式检查；不提交。

---

### Task 6: 书签 UI 与星标

**Files:**
- Modify: `examples/md_viewer/documents_panel.{h,cpp}`
- Modify: `examples/md_viewer/md_viewer_window.{h,cpp}`
- Modify: `tests/md_viewer/documents_panel_test.cpp`
- Modify: `tests/md_viewer/CMakeLists.txt`（`test_documents_panel` 加 `${VIEWER_DIR}/bookmarks.cpp`）

**Interfaces:**
- Consumes: `BookmarkStore`、`Bookmark`、`DisplayBookmarkPath`（Task 5）；`DocumentsPanel`（Task 4）。
- Produces:
  ```cpp
  // DocumentsPanel
  void SetBookmarks(const QVector<Bookmark>& bookmarks,
                    const QString& current_root);
  signals:
  void bookmarkActivated(const QString& absolute_path);
  void bookmarkRemoveRequested(const QString& absolute_path);
  ```
  窗口新增 `QAction* bookmark_action_`（Ctrl+D）、`QToolButton* star_button_`（objectName `starButton`，放进 `nav_widget_`，位于前进之后、缩放之前）。星标图标用现有 `TextActionIcon(QStringLiteral("☆"/"★"), active)`，不新增资源。

**行为锁定：**
- 书签页用 `QTreeWidget`（objectName `bookmarkTree`，两列、表头隐藏）：名称 + 显示路径（按当前根优先、其次星标根、最后绝对路径）。
- 缺失文件：`kMissingRole=true`，前景色用 `palette().color(QPalette::Disabled, QPalette::Text)`，仍可点击/右键。
- 单击书签发 `bookmarkActivated`；右键菜单“移除书签”发 `bookmarkRemoveRequested`。
- 星标按钮 `setCheckable(true)`；当前文档变化时窗口用 `QSignalBlocker` 同步 checked 状态。
- 打开书签**不改变当前根**：文件存在就 `ShowFile`（本任务先用现有签名），不在当前根下时状态栏提示“该书签不在当前目录内”；文件不存在提示“文件已不存在：<名>”并保留条目。
- 星标/取消后立即 `settings_->bookmarks_json = store.ToJson(); settings_->SaveBookmarks();` 并刷新书签页。

- [ ] **Step 1: 写失败的 UI 测试**

在 `documents_panel_test.cpp` 头部补 `#include <QFile>`、`<QTemporaryDir>`、`#include "bookmarks.h"`，然后追加：

```cpp
TEST(DocumentsPanelTest, MarksMissingBookmarks) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString existing = temp.path() + QStringLiteral("/a.md");
  QFile file(existing);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  DocumentsPanel panel;
  BookmarkStore store;
  store.Toggle(existing, temp.path());
  store.Toggle(temp.path() + QStringLiteral("/missing.md"), temp.path());
  panel.SetBookmarks(store.items(), temp.path());

  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("bookmarkTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 2);
  EXPECT_TRUE(tree->topLevelItem(0)->data(0, kMissingRole).toBool());   // 最新在前
  EXPECT_FALSE(tree->topLevelItem(1)->data(0, kMissingRole).toBool());
  EXPECT_EQ(tree->topLevelItem(1)->text(1), QStringLiteral("a.md"));
}
```

- [ ] **Step 2: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_documents_panel --parallel 2
```

预期：编译失败，`SetBookmarks`/`kMissingRole` 未定义。

- [ ] **Step 3: 实现书签页**

书签页放在 `QTabWidget` 第二页；`SetBookmarks` 清空并逐条插入，`QFileInfo(bookmark.path).isFile()` 决定缺失标记；工具提示用绝对路径。`itemClicked` 发 `bookmarkActivated`；`setContextMenuPolicy(Qt::CustomContextMenu)`，右键菜单移除。星标按钮由窗口的 `nav_widget_` 持有，面板不负责同步它的 checked 状态。

- [ ] **Step 4: 运行面板测试**

```bat
cmake --build build_qt515_resvg_x86 --target test_documents_panel --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_documents_panel$" --output-on-failure
```

预期：5/5 通过。

- [ ] **Step 5: 窗口接入**

- 构造时 `bookmarks_ = BookmarkStore::FromJson(settings_->bookmarks_json);`
- `star_button_`：`new QToolButton(titleBar)`，objectName `starButton`，checkable，tooltip `收藏当前文档（Ctrl+D）`；放进 `nav_widget_` 的前进按钮之后；`bookmark_action_` 作为它的 default action（快捷键 Ctrl+D，挂到窗口）。
- `ShowFile` 成功后：`const bool starred = bookmarks_.Contains(path); QSignalBlocker blocker(star_button_); star_button_->setChecked(starred);` 更新图标 `TextActionIcon(starred ? "★" : "☆", starred)`；`documents_panel_->SetBookmarks(bookmarks_.items(), root_dir_);`。
- `bookmark_action_` triggered：无当前文档则状态栏提示；否则 `bookmarks_.Toggle(current_file, root_dir_)`，保存、刷新按钮与书签页。
- 连接面板 `bookmarkActivated` → `OpenBookmark(path)`；`bookmarkRemoveRequested` → 移除、保存、刷新。
- `OpenBookmark`：文件不存在提示；`ShowFile(path)`；若 `!IsPathUnderRoot(path, root_dir_)` 额外提示“该书签不在当前目录内”。

- [ ] **Step 6: 构建并手工验收**

```bat
cmake --build build_qt515_resvg_x86 --target md_viewer --parallel 2
```

星标当前文档 → 书签页出现；重启后仍在；右键移除；删除书签指向的文件后条目置灰、点击有提示；跨根书签打开不改变树。

- [ ] **Step 7: 格式化并报告**

格式检查；不提交。

---

### Task 7: 全文搜索扫描器

**Files:**
- Create: `examples/md_viewer/text_search.h`
- Create: `examples/md_viewer/text_search.cpp`
- Create: `tests/md_viewer/text_search_test.cpp`
- Modify: `tests/md_viewer/CMakeLists.txt`
- Modify: `examples/md_viewer/CMakeLists.txt`

**Interfaces:**
- Consumes: `CollectMarkdownFiles`、`IsIgnoredDirectoryName`、`ScanDocumentRoot`（Task 2）。
- Produces:
  ```cpp
  constexpr int kSearchMinQueryLength = 2;
  constexpr int kSearchMaxHitsPerFile = 50;
  constexpr int kSearchMaxTotalHits = 2000;
  constexpr int kSearchSnippetLength = 200;

  struct SearchHit {
    QString file_path;      // 绝对路径
    QString relative_path;  // 相对根，'/'
    int line = 0;           // 1-based
    QString snippet;
    int match_offset = 0;   // 命中在 snippet 内的起点
    int file_ordinal = 0;   // 该文件内第几个命中（0-based，跳转用）
  };
  Q_DECLARE_METATYPE(SearchHit)

  struct SnippetResult {
    QString text;
    int match_offset = 0;
  };
  SnippetResult MakeSearchSnippet(const QString& line, int match_start,
                                  int query_length,
                                  int max_length = kSearchSnippetLength);

  struct FileScanResult {
    QVector<SearchHit> hits;
    bool canceled = false;
  };
  FileScanResult ScanFileForQuery(const QString& file_path,
                                  const QString& relative_path,
                                  const QString& query, int max_hits,
                                  const std::atomic_bool* cancel);

  using SearchBatchCallback =
      std::function<void(const QVector<SearchHit>&, int scanned, int total)>;
  struct RootScanResult {
    QVector<SearchHit> hits;
    int scanned_files = 0;
    int total_files = 0;
    bool truncated = false;
    bool canceled = false;
  };
  RootScanResult ScanRootForQuery(const QString& root, const QString& query,
                                  const std::atomic_bool* cancel,
                                  const SearchBatchCallback& on_batch);

  class TextSearch : public QObject {
    Q_OBJECT
   public:
    explicit TextSearch(QObject* parent = nullptr);
    ~TextSearch() override;
    void Start(const QString& root, const QString& query);
    void Cancel();
    bool IsRunning() const;
   signals:
    void batchReady(const QVector<SearchHit>& hits, int scanned_files,
                    int total_files);
    void finished(bool truncated, bool canceled);
    void failed(const QString& error);
  };
  ```

**行为锁定：**
- 匹配：不区分大小写的字面量子串；一行最多产出一个 `SearchHit`（`match_offset` 指向首次出现）。
- 读取：`QFile` 64KiB 分块 + 行缓冲；CRLF 去掉 `\r`；首行去掉 UTF-8 BOM；非法 UTF-8 用 `QString::fromUtf8` 的替换字符处理。
- 单行超过 1MiB 仍无换行时，把当前缓冲当作同一行号的一个片段扫描并清空，避免内存随文件增长；跨片段的命中可能漏掉（已知限制）。
- 每文件最多 50 条；全局最多 2000 条，达到后停止并置 `truncated=true`。
- 扫描中遇到打不开/读失败的文件（已删除、无权限）跳过该文件，不中断整轮扫描。
- 取消标志在每块、每条片段、每个文件前检查；取消时保留已产生的命中。
- `TextSearch::Start` 先取消并 `wait()` 旧线程再开新线程；`QThread::create`（Qt 5.10+）里调用 `ScanRootForQuery`，回调里发 `batchReady`；结束发 `finished`。构造函数里 `qRegisterMetaType<QVector<SearchHit>>("QVector<SearchHit>")`。
- 批次：累计 32 条或 100ms 先到者发一批；枚举完文件后先发一批空命中（`scanned=0, total=N`），让 UI 立即拿到总数；结束时 flush。

- [ ] **Step 1: 注册测试目标**

```cmake
add_unit_test(
    NAME test_text_search
    SOURCES text_search_test.cpp
            ${VIEWER_DIR}/text_search.cpp
            ${VIEWER_DIR}/document_index.cpp
    INCLUDES ${VIEWER_DIR}
    QT_MODULES Core Test
    LABELS MARKDOWN
    NO_GTEST_MAIN
)
set_tests_properties(test_text_search PROPERTIES TIMEOUT 120)
```

同时把 `text_search.cpp`、`text_search.h` 加进 `examples/md_viewer/CMakeLists.txt` 的 `ALL_SRCS`。

- [ ] **Step 2: 写失败测试**

```cpp
#include <gtest/gtest.h>

#include <atomic>

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "text_search.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {
void WriteFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
}
}  // namespace

TEST(TextSearchTest, FindsLiteralCaseInsensitiveHitsPerLine) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/a.md");
  WriteFile(path, QByteArray("Alpha render\nsecond RENDER render\nnone\r\n"));
  const FileScanResult result =
      ScanFileForQuery(path, QStringLiteral("a.md"), QStringLiteral("render"),
                       kSearchMaxHitsPerFile, nullptr);
  ASSERT_EQ(result.hits.size(), 2);  // 每行一条
  EXPECT_EQ(result.hits[0].line, 1);
  EXPECT_EQ(result.hits[1].line, 2);
  EXPECT_EQ(result.hits[1].file_ordinal, 1);
  EXPECT_EQ(result.hits[1].snippet.mid(result.hits[1].match_offset, 6),
            QStringLiteral("render"));
}

TEST(TextSearchTest, SnippetKeepsMatchAndBoundsLength) {
  const QString line =
      QString(300, QLatin1Char('a')) + QStringLiteral("needle") +
      QString(300, QLatin1Char('b'));
  const SnippetResult snippet = MakeSearchSnippet(line, 300, 6);
  EXPECT_LE(snippet.text.size(), kSearchSnippetLength + 2);
  EXPECT_EQ(snippet.text.mid(snippet.match_offset, 6),
            QStringLiteral("needle"));
}

TEST(TextSearchTest, HandlesLongLineAndCrlf) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/long.md");
  QByteArray bytes(2 * 1024 * 1024, 'x');
  bytes.insert(bytes.size() - 20, " needle");
  bytes.insert(0, "head\r\n");
  WriteFile(path, bytes);
  const FileScanResult result =
      ScanFileForQuery(path, QStringLiteral("long.md"),
                       QStringLiteral("needle"), kSearchMaxHitsPerFile, nullptr);
  ASSERT_EQ(result.hits.size(), 1);
  EXPECT_EQ(result.hits[0].line, 2);
  EXPECT_LE(result.hits[0].snippet.size(), kSearchSnippetLength + 2);
  EXPECT_EQ(result.hits[0].snippet.mid(result.hits[0].match_offset, 6),
            QStringLiteral("needle"));
}

TEST(TextSearchTest, HandlesInvalidUtf8) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/bad.md");
  QByteArray bytes = QByteArray("ok ") + QByteArray("\xff\xfe", 2) +
                     QByteArray(" needle\n");
  WriteFile(path, bytes);
  const FileScanResult result =
      ScanFileForQuery(path, QStringLiteral("bad.md"),
                       QStringLiteral("needle"), kSearchMaxHitsPerFile, nullptr);
  EXPECT_EQ(result.hits.size(), 1);
}

TEST(TextSearchTest, CapAndCancel) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.path() + QStringLiteral("/many.md");
  QByteArray bytes;
  for (int i = 0; i < 10; ++i) {
    bytes += "needle line\n";
  }
  WriteFile(path, bytes);
  const FileScanResult capped =
      ScanFileForQuery(path, QStringLiteral("many.md"),
                       QStringLiteral("needle"), 3, nullptr);
  EXPECT_EQ(capped.hits.size(), 3);

  std::atomic_bool cancel{true};
  const FileScanResult canceled =
      ScanFileForQuery(path, QStringLiteral("many.md"),
                       QStringLiteral("needle"), kSearchMaxHitsPerFile, &cancel);
  EXPECT_TRUE(canceled.canceled);
  EXPECT_TRUE(canceled.hits.isEmpty());
}

TEST(TextSearchTest, CancelsBetweenBatches) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  for (int f = 0; f < 20; ++f) {
    const QString dir = temp.path() + QStringLiteral("/d%1").arg(f);
    ASSERT_TRUE(QDir().mkpath(dir));
    QByteArray bytes;
    for (int i = 0; i < 200; ++i) {
      bytes += "needle line\n";
    }
    WriteFile(dir + QStringLiteral("/x.md"), bytes);
  }
  std::atomic_bool cancel{false};
  int batches = 0;
  const RootScanResult result = ScanRootForQuery(
      temp.path(), QStringLiteral("needle"), &cancel,
      [&](const QVector<SearchHit>& hits, int, int) {
        if (hits.isEmpty()) {
          return;  // 初始空批只带总数
        }
        ++batches;
        cancel = true;  // 第一批命中后取消
      });
  EXPECT_TRUE(result.canceled);
  EXPECT_GE(batches, 1);
  EXPECT_GT(result.hits.size(), 0);
  EXPECT_LT(result.hits.size(), 20 * 200);
}

TEST(TextSearchTest, SkipsIgnoredDirsAndTruncatesGlobally) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  ASSERT_TRUE(QDir(temp.path()).mkpath("node_modules"));
  WriteFile(temp.path() + QStringLiteral("/node_modules/x.md"),
            "needle\n");
  WriteFile(temp.path() + QStringLiteral("/a.md"), "needle\n");
  const RootScanResult skipped =
      ScanRootForQuery(temp.path(), QStringLiteral("needle"), nullptr, nullptr);
  EXPECT_EQ(skipped.hits.size(), 1);
  EXPECT_EQ(skipped.total_files, 1);

  QTemporaryDir big;
  ASSERT_TRUE(big.isValid());
  for (int f = 0; f < 41; ++f) {
    QByteArray bytes;
    for (int i = 0; i < 50; ++i) {
      bytes += "needle\n";
    }
    WriteFile(big.path() + QStringLiteral("/f%1.md").arg(f), bytes);
  }
  const RootScanResult truncated = ScanRootForQuery(
      big.path(), QStringLiteral("needle"), nullptr, nullptr);
  EXPECT_TRUE(truncated.truncated);
  EXPECT_EQ(truncated.hits.size(), kSearchMaxTotalHits);
}

TEST(TextSearchTest, EmitsBatchesAndFinishes) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  WriteFile(temp.path() + QStringLiteral("/a.md"), "needle\n");
  TextSearch search;
  QSignalSpy batches(&search, &TextSearch::batchReady);
  QSignalSpy finished(&search, &TextSearch::finished);
  search.Start(temp.path(), QStringLiteral("needle"));
  ASSERT_TRUE(finished.wait(30000));
  ASSERT_EQ(finished.count(), 1);
  EXPECT_FALSE(finished.at(0).at(0).toBool());  // truncated
  EXPECT_FALSE(finished.at(0).at(1).toBool());  // canceled
  EXPECT_GE(batches.count(), 1);
  EXPECT_FALSE(search.IsRunning());
}
```

- [ ] **Step 3: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_text_search --parallel 2
```

预期：编译失败。

- [ ] **Step 4: 实现扫描器**

`MakeSearchSnippet`：行长度 ≤ `max_length` 时原样返回；否则窗口起点 `max(0, match_start - 60)`，若窗口越过行尾则回退到 `line.size() - max_length`；截断侧加 `…`（U+2026）；`match_offset` = `match_start - window_start` 加前缀是否存在的偏移。保证 `snippet.mid(match_offset, query_length) == query`（query 在窗口内时）。

`ScanFileForQuery`：分块读取，维护 `line_buffer`、`line_number`、`file_ordinal`；每行 `indexOf(query, from, Qt::CaseInsensitive)` 一次，命中则 `MakeSearchSnippet` 并压入；达到 `max_hits` 停止。`ScanRootForQuery`：先 `CollectMarkdownFiles` 得到 `total`，立即 `on_batch({}, 0, total)`（回调为空时跳过），再逐文件调用；全局计数达到 2000 时置 `truncated` 并停止；每累计 32 条或 100ms 调 `on_batch`（有命中才发，初始那次空批除外）。

`TextSearch`：成员 `std::atomic_bool cancel_{false}; std::atomic_bool running_{false}; QThread* thread_{nullptr};`；`Start` 中先 `Cancel()` 并 `wait()`、删除旧线程；置 `running_=true`；`thread_ = QThread::create([this, root, query] { ... });`，回调 `emit batchReady(...)`，结束前 `running_=false; emit finished(...)`；`connect(thread_, &QThread::finished, thread_, &QObject::deleteLater)` 需小心 `thread_` 指针置空：在 `finished` 的 queued 连接里置空。`IsRunning()` 返回 `running_.load()`。析构里 `Cancel(); if (thread_) { thread_->wait(); thread_->deleteLater(); }`。

- [ ] **Step 5: 运行测试确认通过**

```bat
cmake --build build_qt515_resvg_x86 --target test_text_search --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_text_search$" --output-on-failure
```

预期：8/8 通过。

- [ ] **Step 6: 格式化并报告**

格式检查；不提交。

---

### Task 8: 搜索 UI 与命中跳转

**Files:**
- Modify: `examples/md_viewer/documents_panel.{h,cpp}`
- Modify: `examples/md_viewer/md_viewer_window.{h,cpp}`
- Modify: `tests/md_viewer/documents_panel_test.cpp`

**Interfaces:**
- Consumes: `TextSearch`、`SearchHit`、`kSearchMinQueryLength`（Task 7）；`DocumentsPanel`（Task 4）。
- Produces:
  ```cpp
  // DocumentsPanel
  void BeginSearch(const QString& query);
  void AppendSearchHits(const QVector<SearchHit>& hits, int scanned_files,
                        int total_files);
  void EndSearch(bool truncated, bool canceled);
  void SetSearchError(const QString& message);
  signals:
  void searchHitActivated(const SearchHit& hit);
  void cancelSearchRequested();
  ```
  objectName：`searchSummaryLabel`、`searchResultTree`、`searchEmptyLabel`。

  窗口新增私有记录：
  ```cpp
  struct PendingJump {
    QString path;
    int ordinal = 0;
    QString query;
  };
  std::optional<PendingJump> pending_search_hit_;
  ```

**行为锁定：**
- 标题栏搜索框成为唯一全文搜索入口：placeholder `搜索文档与正文（Ctrl+K）`；`Ctrl+K` 与 `Ctrl+F` 都聚焦它并切到搜索页签；删除区分大小写/全词匹配/上一个/下一个动作及其图标。
- 输入 ≥2 字符后 300ms 防抖开始；回车立即开始；每次新输入先 `Cancel()` 并清空结果；清空输入取消扫描并清空结果。窗口用 `std::optional<QString> active_search_query_` 记录“当前展示结果对应的查询”：`textChanged` 时置空，`Start` 时写入；`batchReady`/`finished` 处理函数先检查 `active_search_query_.has_value() && search_edit_->text() == *active_search_query_`，不满足直接丢弃，防止防抖窗口内旧扫描的排队信号污染新结果。
- 新的 `TextSearch` 是窗口成员，`batchReady` → 面板追加；`finished` → 面板结束；`failed` → 面板错误。
- 结果按文件分组，组项显示相对路径 + 命中数；子项显示行号 + 片段，片段里的关键词用高亮底色；摘要行显示 `在 %1 篇文档中搜索 “%2” · %3 处命中`，随批次更新；单文件最多 50、全局 2000，超出显示“已截断”；取消显示“已取消（部分结果）”。
- 点击命中：文件存在则打开；若文件已在当前文档且未在渲染，直接按 `file_ordinal` 跳；否则记录 pending（路径、序号、查询文本），走 `NavigateTo`，在 `renderFinished` 里跳。跳转前 `SetScrollPosition(0)`，然后调用 `view_->FindText(query, MarkdownView::FindNoFlag, &wrapped, false)` 共 `ordinal + 1` 次；任一次失败则停在顶部并提示“无法精确定位该命中”。跳转抑制阅读位置恢复。
- 搜索命中一律视为用户主动打开，走大文件确认（Task 9 加 reason 后自动生效）。

- [ ] **Step 1: 写失败的 UI 测试**

在 `documents_panel_test.cpp` 追加：

```cpp
TEST(DocumentsPanelTest, GroupsSearchHitsByFile) {
  DocumentsPanel panel;
  panel.BeginSearch(QStringLiteral("render"));
  SearchHit a0{QStringLiteral("/docs/a.md"), QStringLiteral("a.md"), 3,
               QStringLiteral("x render y"), 2, 0};
  SearchHit a1{QStringLiteral("/docs/a.md"), QStringLiteral("a.md"), 9,
               QStringLiteral("z render"), 2, 1};
  SearchHit b0{QStringLiteral("/docs/guide/b.md"),
               QStringLiteral("guide/b.md"), 1,
               QStringLiteral("render"), 0, 0};
  panel.AppendSearchHits({a0, a1, b0}, 2, 10);

  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("searchResultTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 2);
  ASSERT_EQ(tree->topLevelItem(0)->childCount(), 2);
  EXPECT_EQ(tree->topLevelItem(0)->child(1)
                ->data(0, kSearchHitRole)
                .value<SearchHit>()
                .file_ordinal,
            1);
  panel.EndSearch(true, false);
  auto* summary =
      panel.findChild<QLabel*>(QStringLiteral("searchSummaryLabel"));
  ASSERT_NE(summary, nullptr);
  EXPECT_TRUE(summary->text().contains(QStringLiteral("已截断")));
}
```

- [ ] **Step 2: 运行测试确认失败**

```bat
cmake --build build_qt515_resvg_x86 --target test_documents_panel --parallel 2
```

预期：编译失败。

- [ ] **Step 3: 实现搜索结果页**

搜索页布局：`searchSummaryLabel` + `searchResultTree` + 空态标签。结果树一列、表头隐藏；组项存文件路径，子项存 `QVariant::fromValue(hit)`（`kSearchHitRole`）。组项加粗，显示 `相对路径 (N)`。`BeginSearch` 把查询存进面板的 `current_query_`。子项用私有 `SearchResultDelegate`：组项走默认绘制；子项先画行背景（选中/交替色），再画 `L%1` 行号，最后画片段并在 `match_offset .. match_offset + current_query_.size()` 处给关键词加高亮底色；片段用 `QFontMetrics::elidedText(..., Qt::ElideRight, ...)`，高亮范围按可见前缀裁剪。`BeginSearch` 清空树、摘要 `正在搜索…`；`AppendSearchHits` 即使 hits 为空也更新摘要里的文件总数/进度，按 `file_path` 复用组项、更新计数、追加子项；`EndSearch` 更新摘要（完成/已截断/已取消（部分结果））并隐藏取消按钮；`SetSearchError` 显示错误。搜索进行中 footer 显示 `已扫描 %1/%2` 与取消按钮；文档页 footer 显示 `共 N 篇文档`。`itemClicked` 只在子项上发 `searchHitActivated`。footer 的取消按钮发 `cancelSearchRequested()`。

- [ ] **Step 4: 运行面板测试**

```bat
cmake --build build_qt515_resvg_x86 --target test_documents_panel --parallel 2
ctest --test-dir build_qt515_resvg_x86 -R "^test_documents_panel$" --output-on-failure
```

预期：6/6 通过。

- [ ] **Step 5: 窗口接入搜索**

- 删除 `case_action_`、`word_action_`、`find_next_action_`、`find_prev_action_`、`UpdateSearchActionIcons`、`FindNext`；`find_action_` 保留但快捷键改为 `Ctrl+F` + `Ctrl+K`，触发 `FocusGlobalSearch()`。
- `FocusGlobalSearch()`：若搜索框隐藏则显示；`selectAll()`；面板 `ShowTab(kSearch)`。
- `initUi`：`search_edit_->setPlaceholderText("搜索文档与正文（Ctrl+K）")`；不再添加 A/T action；`search_timer_` 间隔改 300ms。
- `initSignals`：`textChanged` → `search_->Cancel(); active_search_query_.reset();`；若长度 `< kSearchMinQueryLength`：停 timer、面板清空（调用 `BeginSearch(QString())` 或专用清空方法）；否则 `BeginSearch(text)` 清空旧结果、重启 timer，并 `ShowTab(kSearch)`。`returnPressed` → 停 timer、立即 `StartSearch()`。`search_timer_` timeout → `StartSearch()`。
- `StartSearch()`：root 为空或查询长度 < 2 则返回；`active_search_query_ = query; search_->Start(root_dir_, query);` 面板 `BeginSearch(query)`。
- `batchReady`/`finished` 槽开头：`if (!active_search_query_ || search_edit_->text() != *active_search_query_) return;`。
- 连接 `TextSearch` 三个信号到面板；连接面板 `cancelSearchRequested` → `search_->Cancel()`。
- `OpenSearchHit(hit)`：
  ```cpp
  if (!QFileInfo(hit.file_path).isFile()) { status; return; }
  const QString query = search_edit_->text();
  if (hit.file_path == view_->current_file() && !view_->is_rendering()) {
    JumpToSearchHit(hit, query);
    return;
  }
  pending_search_hit_ = PendingJump{hit.file_path, hit.file_ordinal, query};
  NavigateTo(hit.file_path, QString(), /*record_history=*/true);
  // 搜索跳转自己管滚动；Task 9 加阅读位置恢复后在这里补
  // pending_restore_scroll_ = -1;
  ```
- `renderFinished` 处理顺序：`pending_fragment_` → `pending_search_hit_`（路径匹配当前文件才执行，否则清空）→ `pending_restore_scroll_`（Task 9）。
- `JumpToSearchHit`：`view_->SetScrollPosition(0);` 循环 `ordinal + 1` 次 `FindText(query, MarkdownView::FindNoFlag, &wrapped, false)`；失败时状态栏提示并停止。
- 任何新的 `ShowFile` 若路径与 `pending_search_hit_->path` 不同则清空 pending。

- [ ] **Step 6: 构建并手工验收**

```bat
cmake --build build_qt515_resvg_x86 --target md_viewer --parallel 2
```

输入“渲染”后结果分批出现；取消保留部分并标注；新查询立即清空；点击命中打开文件并选中附近文本；在当前文档内点命中直接跳；源码独有命中会漂移但不乱跳。

- [ ] **Step 7: 格式化并报告**

格式检查；不提交。

---

### Task 9: 会话恢复、阅读位置、大文件确认与 watcher

**Files:**
- Modify: `examples/md_viewer/md_viewer_window.{h,cpp}`
- Modify: `examples/md_viewer/md_viewer_settings.{h,cpp}`
- Modify: `examples/md_viewer/main.cpp`
- Modify: `examples/md_viewer/documents_panel.{h,cpp}`（空态“重新选择目录”按钮已存在）
- Modify: `tests/md_viewer/document_index_test.cpp`（如 Task 2 已覆盖则不改）

**Interfaces:**
- Consumes: `ReadingPositionStore`（Task 5）、`DirectoriesToWatch`（Task 2）、`DocumentsPanel`（Task 4）。
- Produces:
  ```cpp
  enum class OpenReason { kUser, kRestore };
  bool ShowFile(const QString& path, OpenReason reason);
  void RememberCurrentPosition();
  void RestoreSession();          // main.cpp 无参数时调用，替代 OpenLastFile
  void RefreshRoot();             // F5 与 watcher 共用
  ```
  成员：`ReadingPositionStore positions_; QSet<QString> large_file_prompted_; QFileSystemWatcher* root_watcher_; QTimer* root_rescan_timer_; QStringList watched_dirs_; int pending_restore_scroll_ = -1; QString pending_restore_path_;`
- 大文件阈值常量 `constexpr qint64 kLargeDocumentBytes = 20 * 1024 * 1024;`（严格大于才提示）。

**行为锁定：**
- 启动：`main.cpp` 无位置参数时调 `window.RestoreSession()`；有参数仍 `OpenPath`。
- `RestoreSession`：`settings_->root_dir` 非空且是目录 → `SetRootDir`；`last_file` 存在 → `ShowFile(..., kRestore)`；`last_file` 不是文件则只加载树。根不存在 → `SetRootMissing`，不擅自换根。没有保存过根 → 最近文件分支也用 `kRestore`（启动自动恢复不弹大文件确认）；连最近文件都没有 → 空态。
- 根变化（打开目录、单文件在根外导致换根）立即写 `root/dir` 并 `SaveSession()`；`last_file` 在每次成功 `ShowFile` 后写入并保存。
- 阅读位置：`ShowFile` 切走前 `RememberCurrentPosition()`；`ShowFile` 设置 `pending_restore_path_=新路径`、`pending_restore_scroll_=positions_.ScrollFor(path)`；`renderFinished` 在没有任何 fragment/search pending 时应用一次并清空；若本次走的是 fragment/search 分支，也清空 restore pending。文件 mtime 变化也恢复。同一文件重开（树、最近、后退/前进）也恢复。
- 大文件：`ShowFile(reason==kUser)` 且 `size > 20MiB` 且不在 `large_file_prompted_` → `QMessageBox::warning`（是/否），文案“该文档约 %1 MB，渲染期间窗口可能无响应。仍要打开吗？”；同意后加入会话集合；拒绝返回 false。`kRestore` 不弹框，只在状态栏显示“已恢复 <名>（xx MB）”。所有用户入口（树、对话框、拖放、书签、最近、搜索结果）都走 `kUser`。
- Watcher：`SetRootDir` 成功后 `removePaths(watched_dirs_)`；`watched_dirs_ = DirectoriesToWatch(result)`；`addPaths`；目录/文件变化都启动 300ms 单次 `root_rescan_timer_`。`RefreshRoot` 重新扫描当前根，`documents_panel_->SetRoot` 内部保留展开/选中；根不存在显示空态。F5 动作挂到视图菜单与窗口。
- 根变化/搜索进行中：先取消搜索并清空结果，避免展示旧根的命中。

- [ ] **Step 1: 扩展设置与 main**

`md_viewer_settings` 确认 Task 4 加的 `root_dir`、`last_file` 与 `SaveSession()` 已被 `Load()`/`Save()` 覆盖；`main.cpp` 把无参数分支改成 `window.RestoreSession();`，并把“有参数 OpenPath / 无参数 RestoreSession”这段移到 `window.show()` **之前**（设计稿要求恢复在窗口显示前完成）。单实例的 `newInstanceLaunched`/`showApplication` 连接仍留在 `show()` 前。

- [ ] **Step 2: 实现阅读位置**

在 `ShowFile` 开头调用 `RememberCurrentPosition()`（若当前有文档且路径要变化）；`RememberCurrentPosition` 取 `view_->ScrollPosition()`，`positions_.Update(path, scroll, QDateTime::currentSecsSinceEpoch())`，`settings_->positions_json = positions_.ToJson(); settings_->SavePositions();`。`SaveSettings()` 里也调用一次 `RememberCurrentPosition()`，保证关闭时记录当前文档位置。构造时 `positions_ = ReadingPositionStore::FromJson(settings_->positions_json);`。`renderFinished` 增加 restore 分支。在 Task 8 的 `OpenSearchHit` 里补上 `pending_restore_scroll_ = -1;`，让搜索跳转优先于位置恢复。

- [ ] **Step 3: 实现大文件确认**

`ShowFile(path, OpenReason reason)` 检查 `QFileInfo(path).size()`；`kUser` 且超限且未提示过 → 确认框。所有调用点补 `OpenReason`；搜索跳转传 `kUser`，恢复传 `kRestore`。

- [ ] **Step 4: 实现会话恢复**

`RestoreSession` 按上面的行为锁定实现；`OpenLastFile` 保留为它的“没有保存过根”分支 helper，内部用 `kRestore` 打开最近文件。`ShowFile` 成功后写 `settings_->last_file = path; settings_->SaveSession();`。`SetRootDir` 成功后写 `settings_->root_dir`。

- [ ] **Step 5: 实现 watcher 与 F5**

构造 `root_watcher_ = new QFileSystemWatcher(this);`、`root_rescan_timer_ = new QTimer(this)`（singleShot，300ms）；连接 `directoryChanged`/`fileChanged` → `root_rescan_timer_->start()`；timeout → `RefreshRoot()`。`RefreshRoot` 调 `SetRootDir(root_dir_)`（注意 `SetRootDir` 会重配 watcher，避免递归触发：重配前 `root_rescan_timer_->stop()`）。`refresh_action_`（F5）加到视图菜单与窗口。

- [ ] **Step 6: 构建**

```bat
cmake --build build_qt515_resvg_x86 --target md_viewer --parallel 2
```

- [ ] **Step 7: 手工验收**

- 打开一套文档、切几篇、滚到中部，关闭；重启后根、文档、滚动位置都恢复。
- 删除当前根后重启：显示空态与“重新选择目录”，不崩溃。
- 打开 >20MB 文档：首次确认、同会话再次打开不确认；重启自动恢复不确认。
- 在外部新建/删除 md 文件，300ms 内树刷新且展开状态保留；F5 立即刷新。
- 打开文档位于当前根之外时根切到其父目录；书签打开不换根。

- [ ] **Step 8: 格式化并报告**

格式检查；不提交。

---

### Task 10: 本地 funq GUI 冒烟、回归与文档

**Files:**
- Modify: `README.md`（md_viewer 功能列表）
- Modify: `README.zh-CN.md`（同上）
- Modify: `CLAUDE.md`（代码结构加 `tests/md_viewer/` 与新模块；测试命令不变）
- Modify: `CHANGELOG.md`（新增 Unreleased 段）
- Modify: `docs/superpowers/specs/2026-10-23-md-viewer-docs-browser-design.md`（状态改为“已实现”，补实现备注）
- Modify: `docs/images/screenshot.png`（新界面截图）
- Create（临时，不入库）: `temp/viewer_smoke/make_tree.ps1`
- Create（临时，不入库）: `temp/ui-tests/run_md_viewer_funq.py`
- Create（临时，不入库）: `temp/ui-tests/probe_widgets.py`
- Create（临时，不入库）: `temp/ui-tests/funq_smoke.py`
- Create（临时，不入库）: `temp/ui-tests/md_viewer.alias`

**Interfaces:**
- Consumes: 前九个任务的全部窗口/面板 objectName。
- Produces: 可重复执行的本地 GUI 冒烟与截图工具（temp），以及更新后的仓库文档。

**前置条件（外部工具）：**
- `D:\trae_workspace\qt-cmake-base-env\3rdparty\python-3.8.8-embed-win32\python.exe` 存在（x86，与 Target x86 匹配）。
- `D:\trae_workspace\qt-cmake-base-env\build\ninja-relwithdebinfo-x86\bin\Funq.dll` 存在（RelWithDebInfo，与我们的构建一致）。
- 运行时关闭任何已打开的 `md_viewer.exe`（单实例互斥）。
- **funq 只能操作普通 Qt 控件，进不了 qlitehtml 的渲染窗口。** 它能覆盖窗口壳、标题栏、侧栏（树/过滤/书签/搜索结果）、动作触发、`windowTitle` 变化和整窗截图；**不能**点击或读取正文、验证 `FindText` 选区/滚动位置、代码块复制、图片点击。正文相关行为仍由手工清单和库测试覆盖，funq 步骤里不要尝试点击正文或断言选区。

- [ ] **Step 1: 准备样例文档树**

`temp/viewer_smoke/make_tree.ps1` 生成：

```text
temp/viewer_smoke/docs/
  index.md                  （含“渲染”多次）
  guide/intro.md
  guide/deep/中文 文档.md
  guide/deep/big.md         （>20MB）
  .hidden/secret.md
  node_modules/pkg/readme.md
  build-out/x.md
  notes.txt
```

每个 md 写几段标题、列表和“渲染/搜索/书签”关键词；`big.md` 用 21MB 的重复段落生成。运行：

```bat
powershell -ExecutionPolicy Bypass -File temp\viewer_smoke\make_tree.ps1
```

预期：目录树存在，`.hidden`/`node_modules`/`build-out` 含 md 但应在应用里被忽略。

- [ ] **Step 2: funq runner 包装**

`temp/ui-tests/run_md_viewer_funq.py` 参考 `D:\trae_workspace\qt-cmake-base-env\temp\run_md_viewer_funq.py`：

- `REPO_ROOT` 指向 `D:\trae_workspace\qlitemd`。
- 从 `build_qt515_resvg_x86/bin/run_md_viewer.bat` 解析 `PATH`、`QT_PLUGIN_PATH`（去掉 `;%PATH%`）。
- `PYTHONPATH` 加外部 funq 的 `client`、`server` 目录（嵌入式 Python 的 `._pth` 已含，仍显式设置）。
- 命令：`[python, "-m", "funq_server.runner", "--port", "9010", "--library", Funq.dll, md_viewer.exe] + sys.argv[1:]`。

用 `temp/viewer_smoke/docs` 作参数运行一次，确认进程能起、能注入。

- [ ] **Step 3: 探针取路径**

`temp/ui-tests/probe_widgets.py` 适配外部 `tests/funq_demo/probe_widgets.py`：连接 9010 端口，打印包含关键字的控件路径与属性。运行：

```bat
D:\trae_workspace\qt-cmake-base-env\3rdparty\python-3.8.8-embed-win32\python.exe temp\ui-tests\probe_widgets.py QTreeWidget
```

把 `mdViewerWindow`、`documentTree`、`documentFilterEdit`、`titleSearchEdit`、`bookmarkTree`、`searchResultTree`、`starButton` 的完整路径写进 `temp/ui-tests/md_viewer.alias`（`友好名 = 路径`）。探针输出保存到 `temp/ui-tests/probe.txt`。

- [ ] **Step 4: funq 冒烟脚本**

`temp/ui-tests/funq_smoke.py` 用 `FunqClient(port=9010)` 连上后：

1. 取 `main_window`，断言 `windowTitle` 含 `Markdown 查看器`。
2. 取 `document_tree`（`TreeView`），断言模型至少有一个顶层项；截图 `temp/ui-tests/artifacts/tree.png`。
3. 取 `filter_edit`，`set_property("text", "deep")`，等待 300ms，截图 `filter.png`；清空。
4. 取 `search_edit`，`set_property("text", "渲染")`，等待 1.5s；取 `search_results` 断言至少一个顶层组；截图 `search.png`。
5. 点击第一个组的第一条命中（`TreeView.click_item`），等待 1s，断言 `windowTitle` 含 `index.md` 或对应文档名。**只验证“打开的是哪篇”；正文选区/滚动位置 funq 看不到，留给手工清单。**
6. 取 `star_button`，`click()`；取 `bookmark_tree` 断言条目数为 1；截图 `bookmarks.png`。
7. 打印 `FUNQ SMOKE OK`，关闭窗口，终止 runner。

运行：

```bat
D:\trae_workspace\qt-cmake-base-env\3rdparty\python-3.8.8-embed-win32\python.exe temp\ui-tests\funq_smoke.py
```

预期：输出 `FUNQ SMOKE OK`，`temp/ui-tests/artifacts/` 下有四张截图。若 funq 环境不可用（位数不匹配、Funq.dll 缺失），记录原因并退回 Step 5 的手工清单；不要为此改本仓依赖。

- [ ] **Step 5: 手工验收清单**

在 `temp/ui-tests/` 记录结果：

- 树/过滤/忽略目录/中文路径。
- 书签增删、重启持久化、缺失置灰、跨根不换根。
- 搜索流式、取消部分结果、新查询清空；点击命中后用**肉眼**确认正文跳到附近并出现原生选区（funq 只验证加载了哪篇文档，验证不了选区）。
- 当前文档内点击命中直接跳转；源码独有命中漂移到最近可定位处；找不到时停在顶部并提示。
- 会话恢复（根/文档/滚动位置）、根删除空态、大文件确认一次、恢复不弹框。
- 标题栏单行、菜单/快捷键、窄宽度隐藏顺序、Mica/亚克力/关闭。
- 原有功能回归：大纲、主题切换、缩放、图片预览/另存、公式/Mermaid 源码、HTML/PDF 导出、渲染状态面板。

- [ ] **Step 6: 全量测试**

```bat
ctest --test-dir build_qt515_resvg_x86 --output-on-failure -L MARKDOWN
```

预期：所有既有测试（19 项基础上）加 6 个新测试全部通过；若有失败，先修到全绿再进入文档步骤。

- [ ] **Step 7: 更新文档**

- `README.zh-CN.md` 的“示例程序”段落改为：递归目录树、文件名过滤、文件级书签、跨文件全文搜索与命中跳转、会话/阅读位置恢复、单行标题栏、Mica/亚克力材质、大文件确认；保留原有导出/图片/公式/Mermaid 描述。
- `README.md` 对应段落同步英文。
- `CLAUDE.md` 代码结构加 `tests/md_viewer/`，`examples/md_viewer/` 说明补新模块；测试命令仍是 `ctest -L MARKDOWN`。
- `CHANGELOG.md` 顶部加 `## Unreleased` 条目，概述本次 md_viewer 产品化（递归树、书签、全文搜索、会话恢复、单行标题栏、材质）。funq 是本地临时工具，不写进发布说明。
- 设计文档状态改为“已实现”，并在 §9 后补一段实现差异（若实现中调整了 tab/折叠、命中跳转细节等）。

- [ ] **Step 8: 截图**

用 funq 的 `grab`（按 `oid` 抓窗口）或手工截图，选一张浅色/暗色下显示树 + 书签 + 搜索结果的界面，保存为 `docs/images/screenshot.png`。确认 README 的图片链接仍有效。

- [ ] **Step 9: 最终格式与报告**

```powershell
$files = @(git diff --name-only --diff-filter=ACM) +
         @(git ls-files --others --exclude-standard)
$files = $files | Where-Object { $_ -match '\.(h|cpp)$' -and (Test-Path $_) }
foreach ($file in $files) { clang-format --dry-run --Werror $file }
```

报告：改动文件清单、新测试数量、`ctest` 输出摘要、funq 冒烟结果、截图路径、已知限制（源码独有命中漂移、单行 >1MiB 跨片段漏命中、>200 目录只监听根）。**不要提交、不要推送、不要触发 CI。**

---

## 执行顺序与依赖

```text
Task 1 (history) ─┐
Task 2 (index)  ──┼─→ Task 4 (panel/tree) ─→ Task 6 (bookmarks UI) ─┐
Task 3 (titlebar) ┘                        └→ Task 8 (search UI) ────┼─→ Task 9 (session/watch) ─→ Task 10 (funq/docs)
Task 5 (stores) ───────────────────────────────────────────────────┘
Task 7 (scanner) ──────────────────────────────────────────────────┘
```

Task 3 与 Task 1/2 可并行；Task 5/7 只依赖 Task 2，可与 Task 4/6 并行。除此之外按编号顺序执行，每个任务结束时先让新测试全绿再进入下一个。
