#pragma once

#include "BuiltinShell.h"
#include "Tool.h"
#include "ToolRegistry.h"
#include <memory>


namespace codepilot {

// ============================================================
// 注册所有文件/目录工具到 Registry
// 提供给 Bootstrap 和测试代码使用
// ============================================================
void registerFileTools(ToolRegistry &registry,
                       std::shared_ptr<BuiltinShell> shell);

} // namespace codepilot