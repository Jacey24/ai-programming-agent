// skill_mem.exe — Standalone memory graph tool
// Build: cl /EHsc /std:c++20 skill_mem_main.cpp /Fe:../skills/skill_mem.exe
// Protocol: stdin args, stdout JSON, stderr errors
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ========== Encoding ==========
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Convert UTF-16 wstring (from wmain argv) to UTF-8
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

struct MemNode {
  std::string id, content, hint;
  int access_count = 0;
  std::vector<std::string> related;
};

class MemGraph {
  std::map<std::string, MemNode> nodes_;
  std::map<std::string, std::map<std::string, double>> adj_;
  std::vector<std::string> entries_;

public:
  std::string upsert(const std::string &id, const std::string &c,
                     const std::string &h = "") {
    auto &n = nodes_[id];
    n.id = id;
    if (!c.empty())
      n.content = c;
    if (!h.empty())
      n.hint = h;
    n.access_count++;
    if (entries_.empty())
      entries_.push_back(id);
    return id;
  }
  std::optional<MemNode> get(const std::string &id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? std::nullopt : std::optional(it->second);
  }
  bool has(const std::string &id) const { return nodes_.count(id); }
  size_t count() const { return nodes_.size(); }
  std::vector<std::string> all_ids() const {
    std::vector<std::string> ids;
    for (auto &[k, _] : nodes_)
      ids.push_back(k);
    return ids;
  }
  std::vector<std::pair<std::string, double>>
  adjacent(const std::string &id) const {
    std::vector<std::pair<std::string, double>> r;
    auto it = adj_.find(id);
    if (it != adj_.end())
      for (auto &[to, w] : it->second)
        r.push_back({to, w});
    std::sort(r.begin(), r.end(),
              [](auto &a, auto &b) { return a.second > b.second; });
    return r;
  }
  bool relate(const std::string &from, const std::string &to, double w = 1.0) {
    if (!has(from) || !has(to))
      return false;
    adj_[from][to] += w;
    adj_[to][from] += w;
    nodes_[from].related.push_back(to);
    nodes_[to].related.push_back(from);
    return true;
  }
  bool del(const std::string &id) {
    for (auto &r : nodes_[id].related)
      adj_[r].erase(id);
    adj_.erase(id);
    nodes_.erase(id);
    entries_.erase(std::remove(entries_.begin(), entries_.end(), id),
                   entries_.end());
    return true;
  }

  // --- save/load: JSON format (hand-written, replace with nlohmann/json later)
  // ---
  bool save(const std::string &path) const {
    std::ofstream f(path);
    if (!f)
      return false;
    f << "{\n\"nodes\":{\n";
    bool first_node = true;
    for (auto &[id, n] : nodes_) {
      if (!first_node)
        f << ",\n";
      first_node = false;
      f << "  " << json_esc(id) << ":{";
      f << "\"c\":" << json_esc(n.content) << ",";
      f << "\"h\":" << json_esc(n.hint) << ",";
      f << "\"a\":" << n.access_count;
      f << "}";
    }
    f << "\n},\n\"edges\":{\n";
    bool first_edge = true;
    for (auto &[from, tos] : adj_) {
      for (auto &[to, w] : tos) {
        if (from >= to)
          continue;
        if (!first_edge)
          f << ",\n";
        first_edge = false;
        f << "  " << json_esc(from) << ":" << json_esc(to) << ":" << w;
      }
    }
    f << "\n}\n}\n";
    return true;
  }

  bool load(const std::string &path) {
    std::ifstream f(path);
    if (!f)
      return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.empty())
      return false;

    nodes_.clear();
    adj_.clear();
    entries_.clear();

    // Simple JSON parser for the saved format
    // Expected: {"nodes":{"id":{"c":"...","h":"...","a":N},...},"edges":{...}}
    size_t pos = 0;

    // Find "nodes" key
    auto nk = text.find("\"nodes\"");
    if (nk == std::string::npos)
      return false;
    auto no = text.find('{', nk + 7);
    if (no == std::string::npos)
      return false;
    pos = no + 1;

    // Parse nodes
    while (pos < text.size()) {
      pos = skip_ws(text, pos);
      if (pos >= text.size() || text[pos] == '}')
        break;
      if (text[pos] != '"') {
        pos++;
        continue;
      }

      // Node ID
      auto id_end = find_string_end(text, pos);
      std::string nid = json_unesc(text.substr(pos + 1, id_end - pos - 2));
      pos = id_end;

      pos = skip_ws(text, pos);
      if (pos < text.size() && text[pos] == ':')
        pos++;
      pos = skip_ws(text, pos);
      if (pos < text.size() && text[pos] == '{')
        pos++;

      MemNode n;
      n.id = nid;

      // Parse fields: "c":content, "h":hint, "a":access_count
      while (pos < text.size()) {
        pos = skip_ws(text, pos);
        if (pos >= text.size() || text[pos] == '}')
          break;
        if (text[pos] != '"') {
          pos++;
          continue;
        }

        auto fk_end = find_string_end(text, pos);
        std::string fkey = text.substr(pos + 1, fk_end - pos - 2);
        pos = fk_end;
        pos = skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ':')
          pos++;

        if (fkey == "c") {
          auto sv = extract_json_str(text, pos);
          if (!sv.val.empty())
            n.content = sv.val;
          pos = sv.pos;
        } else if (fkey == "h") {
          auto sv = extract_json_str(text, pos);
          if (!sv.val.empty())
            n.hint = sv.val;
          pos = sv.pos;
        } else if (fkey == "a") {
          auto nv = extract_json_num(text, pos);
          if (!nv.val.empty())
            n.access_count = std::stoi(nv.val);
          pos = nv.pos;
        } else {
          pos = skip_json_value(text, pos);
        }
        // skip comma
        pos = skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',')
          pos++;
      }
      if (pos < text.size() && text[pos] == '}')
        pos++;
      nodes_[nid] = n;
      if (entries_.empty())
        entries_.push_back(nid);

      // skip comma
      pos = skip_ws(text, pos);
      if (pos < text.size() && text[pos] == ',')
        pos++;
    }

    // Find "edges" key
    auto ek = text.find("\"edges\"");
    if (ek == std::string::npos)
      return true; // edges are optional
    auto eo = text.find('{', ek + 7);
    if (eo == std::string::npos)
      return true;
    pos = eo + 1;

    // edges stored as "from":"to":weight (non-redundant, from < to)
    while (pos < text.size()) {
      pos = skip_ws(text, pos);
      if (pos >= text.size() || text[pos] == '}')
        break;
      if (text[pos] != '"') {
        pos++;
        continue;
      }

      auto fe = find_string_end(text, pos);
      std::string from = json_unesc(text.substr(pos + 1, fe - pos - 2));
      pos = fe;
      pos = skip_ws(text, pos);
      if (pos < text.size() && text[pos] == ':')
        pos++;

      pos = skip_ws(text, pos);
      if (pos >= text.size() || text[pos] != '"')
        continue;
      auto te = find_string_end(text, pos);
      std::string to = json_unesc(text.substr(pos + 1, te - pos - 2));
      pos = te;
      pos = skip_ws(text, pos);
      if (pos < text.size() && text[pos] == ':')
        pos++;

      auto nv = extract_json_num(text, pos);
      double w = nv.val.empty() ? 1.0 : std::stod(nv.val);
      pos = nv.pos;

      adj_[from][to] = w;
      adj_[to][from] = w;
      nodes_[from].related.push_back(to);
      nodes_[to].related.push_back(from);

      pos = skip_ws(text, pos);
      if (pos < text.size() && text[pos] == ',')
        pos++;
    }

    return true;
  }

private:
  // --- JSON utilities (hand-written, replace with nlohmann/json later) ---
  static std::string json_esc(const std::string &s) {
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
    return "\"" + r + "\"";
  }

  static std::string json_unesc(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        switch (s[i + 1]) {
        case '"':
          r += '"';
          i++;
          break;
        case '\\':
          r += '\\';
          i++;
          break;
        case 'n':
          r += '\n';
          i++;
          break;
        case 't':
          r += '\t';
          i++;
          break;
        case 'r':
          r += '\r';
          i++;
          break;
        default:
          r += s[i];
          break;
        }
      } else {
        r += s[i];
      }
    }
    return r;
  }

  static size_t skip_ws(const std::string &s, size_t pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' ||
                              s[pos] == '\t' || s[pos] == '\r'))
      pos++;
    return pos;
  }

  static size_t find_string_end(const std::string &s, size_t start) {
    if (start >= s.size() || s[start] != '"')
      return start + 1;
    size_t p = start + 1;
    while (p < s.size()) {
      if (s[p] == '\\') {
        p += 2;
      } else if (s[p] == '"') {
        return p + 1;
      } else {
        p++;
      }
    }
    return s.size();
  }

  struct StrResult {
    std::string val;
    size_t pos;
  };
  static StrResult extract_json_str(const std::string &s, size_t pos) {
    pos = skip_ws(s, pos);
    if (pos >= s.size() || s[pos] != '"')
      return {"", pos};
    size_t end = find_string_end(s, pos);
    std::string raw = s.substr(pos + 1, end - pos - 2);
    return {json_unesc(raw), end};
  }

  struct NumResult {
    std::string val;
    size_t pos;
  };
  static NumResult extract_json_num(const std::string &s, size_t pos) {
    pos = skip_ws(s, pos);
    std::string num;
    while (pos < s.size() &&
           (isdigit(s[pos]) || s[pos] == '.' || s[pos] == '-' ||
            s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+'))
      num += s[pos++];
    return {num, pos};
  }

  static size_t skip_json_value(const std::string &s, size_t pos) {
    pos = skip_ws(s, pos);
    if (pos >= s.size())
      return pos;
    char c = s[pos];
    if (c == '"') {
      return find_string_end(s, pos);
    }
    if (c == '{' || c == '[') {
      int depth = 1;
      bool in_str = false;
      pos++;
      while (pos < s.size() && depth > 0) {
        if (in_str) {
          if (s[pos] == '\\')
            pos += 2;
          else if (s[pos] == '"')
            in_str = false;
          pos++;
        } else {
          if (s[pos] == '"')
            in_str = true;
          else if (s[pos] == '{' || s[pos] == '[')
            depth++;
          else if (s[pos] == '}' || s[pos] == ']')
            depth--;
          pos++;
        }
      }
      return pos;
    }
    if (isdigit(c) || c == '-')
      return extract_json_num(s, pos).pos;
    if (s.substr(pos, 4) == "true")
      return pos + 4;
    if (s.substr(pos, 5) == "false")
      return pos + 5;
    if (s.substr(pos, 4) == "null")
      return pos + 4;
    return pos + 1;
  }
};

// ========== stdout JSON output ==========
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

static std::string get_mem_path() {
  // Use MEM_PATH env var if set
  const char *env = getenv("MEM_PATH");
  if (env && env[0])
    return env;

  // Otherwise, derive path from the executable's own location
  // This avoids dependency on the current working directory
#ifdef _WIN32
  char exe_path[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
  std::string exe_str(exe_path);
  // Find the last backslash
  auto pos = exe_str.find_last_of("\\/");
  if (pos != std::string::npos) {
    std::string dir = exe_str.substr(0, pos);
    // If exe is in skills/, memory path is ../memories/main.memgraph
    // Resolve: skills/skill_mem.exe ? ../memories/main.memgraph
    if (dir.size() >= 7 && dir.substr(dir.size() - 7) == "\\skills") {
      return dir.substr(0, dir.size() - 7) + "\\memories\\main.memgraph";
    }
  }
#endif
  return "memories/main.memgraph";
}

// Use wmain to receive proper UTF-16 argv (via CreateProcessW from shell.cpp),
// then convert to UTF-8 internally. This preserves non-ASCII characters
// (?, ?, ?, Chinese, etc.) that would be corrupted by main's ANSI argv.
#ifdef _WIN32
int wmain(int argc, wchar_t *argv[]) {
  SetConsoleOutputCP(CP_UTF8);

  // Convert all argv to UTF-8 strings
  std::vector<std::string> args_utf8;
  for (int i = 0; i < argc; i++) {
    args_utf8.push_back(wide_to_utf8(argv[i]));
  }

  // Build argc/argv-compatible access
  auto get_argv = [&](int i) -> const char * {
    if (i < 0 || i >= (int)args_utf8.size())
      return "";
    return args_utf8[i].c_str();
  };
#else
int main(int argc, char *argv[]) {
#endif

  std::string mem_path = get_mem_path();

  MemGraph g;
  g.load(mem_path);

  if (argc < 2) {
    out_json(false, "Usage: skill_mem.exe <command> [args...]");
    return 1;
  }

#ifdef _WIN32
  std::string cmd = get_argv(1);
#else
  std::string cmd = argv[1];
#endif
  auto toupper = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
  };
  cmd = toupper(cmd);

#ifdef _WIN32
  auto rest = [&]() -> std::string {
    std::string r;
    for (int i = 2; i < argc; i++) {
      if (i > 2)
        r += " ";
      r += get_argv(i);
    }
    return r;
  };
#else
  auto rest = [argc, argv]() -> std::string {
    std::string r;
    for (int i = 2; i < argc; i++) {
      if (i > 2)
        r += " ";
      r += argv[i];
    }
    return r;
  };
#endif

  if (cmd == "STATUS") {
    out_json(true, std::to_string(g.count()) + " nodes",
             "{\"nodes\":" + std::to_string(g.count()) + "}");
  } else if (cmd == "LIST") {
    auto ids = g.all_ids();
    std::string data =
        "{\"count\":" + std::to_string(ids.size()) + ",\"nodes\":[";
    bool first = true;
    for (auto &id : ids) {
      if (!first)
        data += ",";
      first = false;
      auto n = g.get(id);
      data += "{\"id\":\"" + js_esc(id) + "\"";
      if (n)
        data += ",\"preview\":\"" + js_esc(n->content.substr(0, 60)) + "\"";
      data += "}";
    }
    data += "]}";
    out_json(true, std::to_string(ids.size()) + " nodes", data);
  } else if (cmd == "GO" && argc >= 3) {
#ifdef _WIN32
    std::string id = get_argv(2);
#else
    std::string id = argv[2];
#endif
    if (!g.has(id)) {
      out_json(false, "Not found: " + id);
      return 1;
    }
    auto n = g.get(id);
    std::string data = "{\"id\":\"" + js_esc(n->id) + "\",\"content\":\"" +
                       js_esc(n->content) + "\",\"hint\":\"" + js_esc(n->hint) +
                       "\"}";
    out_json(true, "At: " + id, data);
    g.save(mem_path);
  } else if (cmd == "WHERE") {
    if (g.count() == 0) {
      out_json(false, "No nodes");
      return 1;
    }
    auto ids = g.all_ids();
    auto n = g.get(ids[0]);
    if (!n) {
      out_json(false, "No current node");
      return 1;
    }
    std::string data = "{\"id\":\"" + js_esc(n->id) + "\",\"content\":\"" +
                       js_esc(n->content) + "\",\"connections\":[";
    auto adj = g.adjacent(ids[0]);
    bool first = true;
    for (auto &[id, w] : adj) {
      if (!first)
        data += ",";
      first = false;
      data += "{\"id\":\"" + js_esc(id) + "\"}";
    }
    data += "]}";
    out_json(true, ids[0], data);
  } else if (cmd == "REMEMBER" && argc >= 4) {
#ifdef _WIN32
    std::string id = get_argv(2);
    std::string content = rest().substr(id.length() + 1);
#else
    std::string id = argv[2];
    std::string content = rest().substr(id.length() + 1);
#endif
    g.upsert(id, content);
    out_json(true, "Remembered: " + id, "{\"id\":\"" + js_esc(id) + "\"}");
    g.save(mem_path);
  } else if (cmd == "RECALL" && argc >= 3) {
#ifdef _WIN32
    std::string query = rest();
#else
    std::string query = rest();
#endif
    std::vector<std::pair<std::string, double>> scores;
    for (auto &id : g.all_ids()) {
      auto n = g.get(id);
      if (!n)
        continue;
      double s = 0;
      if (id.find(query) != std::string::npos)
        s += 1.0;
      if (n->content.find(query) != std::string::npos)
        s += 0.8;
      if (n->hint.find(query) != std::string::npos)
        s += 0.6;
      if (s > 0.3)
        scores.push_back({id, s});
    }
    std::sort(scores.begin(), scores.end(),
              [](auto &a, auto &b) { return a.second > b.second; });
    std::string data = "{\"query\":\"" + js_esc(query) +
                       "\",\"count\":" + std::to_string(scores.size()) +
                       ",\"matches\":[";
    bool first = true;
    for (auto &[id, s] : scores) {
      if (!first)
        data += ",";
      first = false;
      data +=
          "{\"id\":\"" + js_esc(id) + "\",\"score\":" + std::to_string(s) + "}";
    }
    data += "]}";
    out_json(true, "Found " + std::to_string(scores.size()) + " matches", data);
  } else if ((cmd == "FORGET" || cmd == "RM") && argc >= 3) {
#ifdef _WIN32
    std::string id = get_argv(2);
#else
    std::string id = argv[2];
#endif
    g.del(id);
    out_json(true, "Forgot: " + id);
    g.save(mem_path);
  } else if (cmd == "TIE" && argc >= 4) {
#ifdef _WIN32
    if (!g.relate(get_argv(2), get_argv(3))) {
      out_json(false, "Both nodes must exist: " + std::string(get_argv(2)) +
                          " " + std::string(get_argv(3)));
#else
    if (!g.relate(argv[2], argv[3])) {
      out_json(false, "Both nodes must exist: " + std::string(argv[2]) + " " +
                          std::string(argv[3]));
#endif
      return 1;
    }
#ifdef _WIN32
    out_json(true,
             "Tied: " + std::string(get_argv(2)) + " <-> " +
                 std::string(get_argv(3)),
             "{\"from\":\"" + js_esc(get_argv(2)) + "\",\"to\":\"" +
                 js_esc(get_argv(3)) + "\"}");
#else
    out_json(true,
             "Tied: " + std::string(argv[2]) + " <-> " + std::string(argv[3]),
             "{\"from\":\"" + js_esc(argv[2]) + "\",\"to\":\"" +
                 js_esc(argv[3]) + "\"}");
#endif
    g.save(mem_path);
  } else if (cmd == "AROUND" && argc >= 3) {
#ifdef _WIN32
    std::string id = get_argv(2);
#else
    std::string id = argv[2];
#endif
    auto adj = g.adjacent(id);
    std::string data = "{\"node\":\"" + js_esc(id) +
                       "\",\"count\":" + std::to_string(adj.size()) +
                       ",\"adjacent\":[";
    bool first = true;
    for (auto &[nid, w] : adj) {
      if (!first)
        data += ",";
      first = false;
      data += "{\"id\":\"" + js_esc(nid) + "\"}";
    }
    data += "]}";
    out_json(true, std::to_string(adj.size()) + " connections", data);
  } else if (cmd == "BACK") {
    auto ids = g.all_ids();
    if (ids.empty()) {
      out_json(false, "No history");
      return 1;
    }
    std::string id = ids[0];
    auto adj = g.adjacent(id);
    if (adj.empty()) {
      out_json(false, "No history from entry");
      return 1;
    }
    auto n = g.get(adj[0].first);
    if (!n) {
      out_json(false, "No node");
      return 1;
    }
    std::string data = "{\"id\":\"" + js_esc(n->id) + "\",\"content\":\"" +
                       js_esc(n->content) + "\"}";
    out_json(true, "Back: " + adj[0].first, data);
  } else if (cmd == "CLEAR") {
    g = MemGraph();
    g.upsert("root", "Memory system initialized", "Root node");
    g.save(mem_path);
    out_json(true, "Memory cleared");
  } else {
    out_json(false, "Unknown command: " + cmd);
    return 1;
  }
  return 0;
}