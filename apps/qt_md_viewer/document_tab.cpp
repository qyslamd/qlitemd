#include "document_tab.h"

#include "reader_page.h"

// 定义在 .cpp 里，让 unique_ptr<ReaderPage> 在析构点看到完整类型
DocumentTab::~DocumentTab() = default;
