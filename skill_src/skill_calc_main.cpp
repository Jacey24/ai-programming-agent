// skill_calc.exe — Standalone calculator tool
// Build: cl /EHsc /std:c++20 skill_calc_main.cpp /Fe:../skills/skill_calc.exe
// Protocol: first arg = command, rest = expression
// Output: JSON { "ok": true/false, "msg": "...", "data": {...} }
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ========== JSON escape ==========
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
  return r;
}

static void out_json(bool ok, const std::string &msg,
                     const std::string &data = "{}") {
  std::string result = "{\"ok\":" + std::string(ok ? "true" : "false") +
                       ",\"msg\":\"" + json_esc(msg) + "\",\"data\":" + data +
                       "}\n";
  std::cout << result << std::flush;
}

// ========== Expression evaluator ==========
// Simple recursive descent parser for basic arithmetic
// Supports: + - * / ( ) ^ (power) and decimal numbers

class Calculator {
public:
  explicit Calculator(const std::string &expr) : input_(expr), pos_(0) {}

  double parse() {
    double result = parse_add_sub();
    if (pos_ < input_.size()) {
      error_pos_ = pos_;
      throw std::runtime_error("Unexpected character at position " +
                               std::to_string(pos_));
    }
    return result;
  }

private:
  std::string input_;
  size_t pos_;
  size_t error_pos_ = 0;

  void skip_ws() {
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t'))
      pos_++;
  }

  double parse_add_sub() {
    double left = parse_mul_div();
    skip_ws();
    while (pos_ < input_.size() &&
           (input_[pos_] == '+' || input_[pos_] == '-')) {
      char op = input_[pos_++];
      double right = parse_mul_div();
      if (op == '+')
        left += right;
      else
        left -= right;
      skip_ws();
    }
    return left;
  }

  double parse_mul_div() {
    double left = parse_power();
    skip_ws();
    while (pos_ < input_.size() &&
           (input_[pos_] == '*' || input_[pos_] == '/')) {
      char op = input_[pos_++];
      double right = parse_power();
      if (op == '*')
        left *= right;
      else if (right == 0.0)
        throw std::runtime_error("Division by zero");
      else
        left /= right;
      skip_ws();
    }
    return left;
  }

  double parse_power() {
    double base = parse_unary();
    skip_ws();
    if (pos_ < input_.size() && input_[pos_] == '^') {
      pos_++;
      double exp = parse_unary();
      return std::pow(base, exp);
    }
    return base;
  }

  double parse_unary() {
    skip_ws();
    if (pos_ < input_.size() && input_[pos_] == '-') {
      pos_++;
      return -parse_primary();
    }
    if (pos_ < input_.size() && input_[pos_] == '+') {
      pos_++;
      return parse_primary();
    }
    return parse_primary();
  }

  double parse_primary() {
    skip_ws();
    if (pos_ >= input_.size())
      throw std::runtime_error("Unexpected end of expression");

    // Parentheses
    if (input_[pos_] == '(') {
      pos_++;
      double val = parse_add_sub();
      skip_ws();
      if (pos_ >= input_.size() || input_[pos_] != ')')
        throw std::runtime_error("Missing closing parenthesis");
      pos_++;
      return val;
    }

    // Number
    if (std::isdigit(input_[pos_]) || input_[pos_] == '.') {
      size_t start = pos_;
      while (pos_ < input_.size() &&
             (std::isdigit(input_[pos_]) || input_[pos_] == '.'))
        pos_++;
      std::string num_str = input_.substr(start, pos_ - start);
      char *end = nullptr;
      double val = std::strtod(num_str.c_str(), &end);
      if (end != num_str.c_str() + num_str.size())
        throw std::runtime_error("Invalid number: " + num_str);
      return val;
    }

    // Functions: sqrt, sin, cos, abs
    std::string func;
    while (pos_ < input_.size() && std::isalpha(input_[pos_]))
      func += input_[pos_++];
    if (!func.empty()) {
      skip_ws();
      if (pos_ < input_.size() && input_[pos_] == '(') {
        pos_++;
        double arg = parse_add_sub();
        skip_ws();
        if (pos_ >= input_.size() || input_[pos_] != ')')
          throw std::runtime_error("Missing parenthesis after function");
        pos_++;
        if (func == "sqrt")
          return std::sqrt(arg);
        if (func == "sin")
          return std::sin(arg);
        if (func == "cos")
          return std::cos(arg);
        if (func == "abs")
          return std::abs(arg);
        if (func == "round")
          return std::round(arg);
        throw std::runtime_error("Unknown function: " + func);
      }
      throw std::runtime_error("Expected ( after function: " + func);
    }

    throw std::runtime_error("Unexpected character: " +
                             std::string(1, input_[pos_]));
  }
};

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif

  if (argc < 3) {
    out_json(false, "Usage: skill_calc.exe CALC <expression>");
    return 1;
  }

  // Parse command (must be CALC)
  std::string cmd = argv[1];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

  if (cmd != "CALC") {
    out_json(false, "Unknown command: " + cmd);
    return 1;
  }

  // Build expression from remaining args
  std::string expr;
  for (int i = 2; i < argc; i++) {
    if (i > 2)
      expr += " ";
    expr += argv[i];
  }

  if (expr.empty()) {
    out_json(false, "No expression provided");
    return 1;
  }

  try {
    Calculator calc(expr);
    double result = calc.parse();

    // Format result
    std::string result_str;
    if (result == std::floor(result) && !std::isinf(result) &&
        std::abs(result) < 1e15) {
      // Integer — no decimal places
      result_str = std::to_string((long long)result);
    } else {
      result_str = std::to_string(result);
      // Remove trailing zeros
      auto dot = result_str.find('.');
      if (dot != std::string::npos) {
        auto end = result_str.find_last_not_of('0');
        if (end > dot) {
          result_str = result_str.substr(0, end + 1);
        } else {
          result_str = result_str.substr(0, dot + 2);
        }
      }
    }

    std::string data = "{\"expression\":\"" + json_esc(expr) +
                       "\",\"result\":\"" + json_esc(result_str) + "\"}";
    out_json(true, expr + " = " + result_str, data);
  } catch (const std::exception &e) {
    out_json(false, "计算错误: " + std::string(e.what()));
    return 1;
  }

  return 0;
}