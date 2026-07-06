// skill_mask.exe — Standalone mask management tool for mask_manager expert
// Build: cl /EHsc /std:c++20 skill_mask_main.cpp /Fe:../skills/skill_mask.exe
// Protocol: stdin args, stdout JSON, stderr errors
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static std::string wide_to_utf8(const wchar_t *wstr) {
  if (!wstr || !wstr[0])
    return "";
  int ulen =
      WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
  if (ulen <= 0)
    return "";
  std::string utf8(ulen, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &utf8[0], ulen, nullptr, nullptr);
  if (!utf8.empty())
    utf8.pop_back();
  return utf8;
}
#endif

// JSON escape helper
static std::string js_esc(const std::string &s) {
  std::string r;
  for (char c : s) {
    switch (c) {
    case '"':
      r += "\\\"";
      break;
    case '\\':
      r += "\\\\";
      break;
    case '\n':
      r += "\\n";
      break;
    case '\t':
      r += "\\t";
      break;
    case '\r':
      r += "\\r";
      break;
    default:
      r += c;
      break;
    }
  }
  return r;
}

static void out_json(bool ok, const std::string &msg,
                     const std::string &data = "{}") {
  std::string result = "{\"ok\":" + std::string(ok ? "true" : "false") +
                       ",\"msg\":\"" + js_esc(msg) + "\",\"data\":" + data +
                       "}\n";
  std::cout << result << std::flush;
}

// Parse cmd_masks.json in JSON object format:
// {"WRITE": "block", "DELETE": "approve", ...}
static std::map<std::string, std::string> parse_masks(const std::string &path) {
  std::map<std::string, std::string> masks;
  std::ifstream in(path);
  if (!in.is_open())
    return masks;
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  in.close();
  auto ob_start = content.find('{');
  auto ob_end = content.find('}', ob_start);
  if (ob_start == std::string::npos || ob_end == std::string::npos)
    return masks;
  std::string obj = content.substr(ob_start + 1, ob_end - ob_start - 1);
  size_t pos = 0;
  while (true) {
    auto q = obj.find('"', pos);
    if (q == std::string::npos)
      break;
    auto q2 = obj.find('"', q + 1);
    if (q2 == std::string::npos)
      break;
    std::string cmd = obj.substr(q + 1, q2 - q - 1);
    auto col = obj.find(':', q2);
    if (col == std::string::npos)
      break;
    auto vq = obj.find('"', col);
    if (vq == std::string::npos)
      break;
    auto vq2 = obj.find('"', vq + 1);
    if (vq2 == std::string::npos)
      break;
    std::string val = obj.substr(vq + 1, vq2 - vq - 1);
    std::string upper = cmd;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    masks[upper] = val;
    pos = vq2 + 1;
  }
  return masks;
}

static bool write_masks(const std::string &path,
                        const std::map<std::string, std::string> &masks) {
  // Ensure directory exists
  auto dir_pos = path.find_last_of("\\/");
  if (dir_pos != std::string::npos) {
    std::string dir = path.substr(0, dir_pos);
    std::filesystem::create_directories(dir);
  }
  std::ofstream out(path);
  if (!out.is_open())
    return false;
  out << "{\n";
  size_t count = 0;
  for (auto &[cmd, val] : masks) {
    if (count > 0)
      out << ",\n";
    out << "  \"" << cmd << "\": \"" << val << "\"";
    count++;
  }
  out << "\n}\n";
  out.close();
  return true;
}

// Build mask data JSON for output
static std::string
build_mask_json(const std::map<std::string, std::string> &masks) {
  std::string data =
      "{\"count\":" + std::to_string(masks.size()) + ",\"rules\":[";
  bool first = true;
  for (auto &[cmd, val] : masks) {
    if (!first)
      data += ",";
    first = false;
    data +=
        "{\"cmd\":\"" + js_esc(cmd) + "\",\"action\":\"" + js_esc(val) + "\"}";
  }
  data += "]}";
  return data;
}

#ifdef _WIN32
int wmain(int argc, wchar_t *argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> args;
  for (int i = 0; i < argc; i++)
    args.push_back(wide_to_utf8(argv[i]));
  auto get_arg = [&](int i) -> const std::string & {
    static std::string empty;
    return (i < 0 || i >= (int)args.size()) ? empty : args[i];
  };
#else
int main(int argc, char *argv[]) {
  auto get_arg = [&](int i) -> const std::string & {
    static std::string empty;
    return (i < 0 || i >= argc) ? empty : std::string(argv[i]);
  };
#endif
  if (argc < 2) {
    out_json(false, "用法: skill_mask.exe <命令> [参数...]");
    return 1;
  }
  std::string cmd = get_arg(1);
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

  if (cmd == "READMASK") {
    // READMASK [workfolder_path]
    std::string wf = (argc >= 3) ? get_arg(2) : "";
    if (wf.empty()) {
      out_json(false, "请提供工作目录路径");
      return 1;
    }
    // Normalize path: ensure trailing backslash
    if (wf.back() != '\\')
      wf += '\\';
    std::string mask_path = wf + "cmd_masks.json";
    if (!std::filesystem::exists(mask_path)) {
      out_json(false, "该工作目录没有掩码配置文件: " + mask_path);
      return 1;
    }
    auto masks = parse_masks(mask_path);
    if (masks.empty()) {
      out_json(false, "掩码配置文件为空或格式无效: " + mask_path);
      return 1;
    }
    out_json(true, "已读取 " + std::to_string(masks.size()) + " 条掩码规则",
             build_mask_json(masks));

  } else if (cmd == "WRITEMASK") {
    // WRITEMASK [workfolder_path] [cmd] [action]
    // action: block / approve
    if (argc < 5) {
      out_json(false, "用法: WRITEMASK <workfolder> <command> <block|approve>");
      return 1;
    }
    std::string wf = get_arg(2);
    if (wf.back() != '\\')
      wf += '\\';
    std::string mask_path = wf + "cmd_masks.json";
    std::string target_cmd = get_arg(3);
    std::string action = get_arg(4);
    std::transform(target_cmd.begin(), target_cmd.end(), target_cmd.begin(),
                   ::toupper);
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    if (action != "block" && action != "approve") {
      out_json(false, "操作必须是 'block' 或 'approve'");
      return 1;
    }
    auto masks = parse_masks(mask_path);
    masks[target_cmd] = action;
    if (!write_masks(mask_path, masks)) {
      out_json(false, "写入掩码配置失败: " + mask_path);
      return 1;
    }
    out_json(true, "已设置 " + target_cmd + " → " + action,
             "{\"cmd\":\"" + target_cmd + "\",\"action\":\"" + action +
                 "\",\"total\":" + std::to_string(masks.size()) + "}");

  } else if (cmd == "DELETEMASK") {
    // DELETEMASK [workfolder_path] [cmd ...]
    if (argc < 4) {
      out_json(false, "用法: DELETEMASK <workfolder> <command> [command...]");
      return 1;
    }
    std::string wf = get_arg(2);
    if (wf.back() != '\\')
      wf += '\\';
    std::string mask_path = wf + "cmd_masks.json";
    auto masks = parse_masks(mask_path);
    int deleted = 0;
    for (int i = 3; i < argc; i++) {
      std::string tc = get_arg(i);
      std::transform(tc.begin(), tc.end(), tc.begin(), ::toupper);
      if (masks.erase(tc))
        deleted++;
    }
    if (deleted == 0) {
      out_json(false, "没有找到要删除的指令掩码");
      return 1;
    }
    if (!write_masks(mask_path, masks)) {
      out_json(false, "写入掩码配置失败: " + mask_path);
      return 1;
    }
    out_json(true, "已删除 " + std::to_string(deleted) + " 条掩码规则",
             "{\"deleted\":" + std::to_string(deleted) +
                 ",\"remaining\":" + std::to_string(masks.size()) + "}");

  } else if (cmd == "CLEARMASK") {
    // CLEARMASK [workfolder_path] — delete the entire cmd_masks.json
    if (argc < 3) {
      out_json(false, "用法: CLEARMASK <workfolder>");
      return 1;
    }
    std::string wf = get_arg(2);
    if (wf.back() != '\\')
      wf += '\\';
    std::string mask_path = wf + "cmd_masks.json";
    if (!std::filesystem::exists(mask_path)) {
      out_json(false, "文件不存在: " + mask_path);
      return 1;
    }
    if (!std::filesystem::remove(mask_path)) {
      out_json(false, "删除失败: " + mask_path);
      return 1;
    }
    out_json(true, "已删除掩码配置文件",
             "{\"path\":\"" + js_esc(mask_path) + "\"}");

  } else if (cmd == "CHECKMASK") {
    // CHECKMASK [workfolder_path] — check if cmd_masks.json exists
    std::string wf = (argc >= 3) ? get_arg(2) : "";
    if (wf.empty()) {
      out_json(false, "请提供工作目录路径");
      return 1;
    }
    if (wf.back() != '\\')
      wf += '\\';
    std::string mask_path = wf + "cmd_masks.json";
    bool exists = std::filesystem::exists(mask_path);
    if (exists) {
      auto masks = parse_masks(mask_path);
      out_json(true, "掩码配置文件存在",
               "{\"exists\":true,\"path\":\"" + js_esc(mask_path) +
                   "\",\"count\":" + std::to_string(masks.size()) +
                   ",\"rules\":" + build_mask_json(masks) + "}");
    } else {
      out_json(true, "该工作目录没有掩码配置文件",
               "{\"exists\":false,\"path\":\"" + js_esc(mask_path) + "\"}");
    }

  } else if (cmd == "GETWF") {
    // GETWF — just echo back the current workfolder as provided
    // Useful for mask_manager to confirm the active workfolder
    std::string wf = (argc >= 3) ? get_arg(2) : "";
    out_json(true, "当前工作目录: " + wf,
             "{\"workfolder\":\"" + js_esc(wf) + "\"}");

  } else {
    out_json(false, "未知命令: " + cmd +
                        "。可用命令: READMASK, WRITEMASK, "
                        "DELETEMASK, CLEARMASK, CHECKMASK, GETWF");
    return 1;
  }
  return 0;
}