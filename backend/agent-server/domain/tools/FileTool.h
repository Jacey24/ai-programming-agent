#pragma once

#include "Tool.h"
#include "ToolRegistry.h"

namespace codepilot {

// ============================================================
// 注册所有文件/目录工具到 Registry
// 提供给 Bootstrap 和测试代码使用
// ============================================================
void registerFileTools(ToolRegistry &registry);

} // namespace codepilot
