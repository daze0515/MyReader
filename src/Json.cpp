#include "Json.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

Json Json::Bool(bool v) { Json j; j.t = T::Bool; j.b = v; return j; }
Json Json::Num(double v) { Json j; j.t = T::Num; j.num = v; return j; }
Json Json::Str(const std::string& v) { Json j; j.t = T::Str; j.str = v; return j; }
Json Json::Arr() { Json j; j.t = T::Arr; return j; }
Json Json::Obj() { Json j; j.t = T::Obj; return j; }

Json& Json::operator[](const std::string& key) {
  if (t != T::Obj) t = T::Obj;
  for (auto& kv : obj) if (kv.first == key) return kv.second;
  obj.emplace_back(key, Json());
  return obj.back().second;
}

const Json* Json::Get(const std::string& key) const {
  if (t != T::Obj) return nullptr;
  for (auto& kv : obj) if (kv.first == key) return &kv.second;
  return nullptr;
}

std::string Json::GetStr(const std::string& key, const std::string& def) const {
  const Json* v = Get(key);
  return (v && v->t == T::Str) ? v->str : def;
}
double Json::GetNum(const std::string& key, double def) const {
  const Json* v = Get(key);
  return (v && v->t == T::Num) ? v->num : def;
}
bool Json::GetBool(const std::string& key, bool def) const {
  const Json* v = Get(key);
  return (v && v->t == T::Bool) ? v->b : def;
}

static void EscapeTo(std::string& out, const std::string& s) {
  out.push_back('"');
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back((char)c);
        }
    }
  }
  out.push_back('"');
}

static void DumpTo(const Json& j, std::string& out) {
  switch (j.t) {
    case Json::T::Null: out += "null"; break;
    case Json::T::Bool: out += j.b ? "true" : "false"; break;
    case Json::T::Num: {
      char buf[40];
      if (j.num == (double)(long long)j.num) snprintf(buf, sizeof(buf), "%lld", (long long)j.num);
      else snprintf(buf, sizeof(buf), "%.6g", j.num);
      out += buf;
      break;
    }
    case Json::T::Str: EscapeTo(out, j.str); break;
    case Json::T::Arr: {
      out.push_back('[');
      for (size_t i = 0; i < j.arr.size(); ++i) { if (i) out.push_back(','); DumpTo(j.arr[i], out); }
      out.push_back(']');
      break;
    }
    case Json::T::Obj: {
      out.push_back('{');
      for (size_t i = 0; i < j.obj.size(); ++i) {
        if (i) out.push_back(',');
        EscapeTo(out, j.obj[i].first);
        out.push_back(':');
        DumpTo(j.obj[i].second, out);
      }
      out.push_back('}');
      break;
    }
  }
}

std::string Json::Dump() const {
  std::string out;
  out.reserve(256);
  DumpTo(*this, out);
  return out;
}

namespace {
struct Parser {
  const std::string& s;
  size_t i = 0;
  explicit Parser(const std::string& src) : s(src) {}

  void SkipWs() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
  bool Eof() const { return i >= s.size(); }

  bool ParseValue(Json& out) {
    SkipWs();
    if (Eof()) return false;
    char c = s[i];
    if (c == '{') return ParseObj(out);
    if (c == '[') return ParseArr(out);
    if (c == '"') { out.t = Json::T::Str; return ParseStr(out.str); }
    if (c == 't') { if (s.compare(i, 4, "true") == 0) { i += 4; out = Json::Bool(true); return true; } return false; }
    if (c == 'f') { if (s.compare(i, 5, "false") == 0) { i += 5; out = Json::Bool(false); return true; } return false; }
    if (c == 'n') { if (s.compare(i, 4, "null") == 0) { i += 4; out = Json(); return true; } return false; }
    return ParseNum(out);
  }

  bool ParseNum(Json& out) {
    size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) ++i;
    if (i == start) return false;
    out = Json::Num(strtod(s.substr(start, i - start).c_str(), nullptr));
    return true;
  }

  bool ParseStr(std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
      char c = s[i];
      if (c == '"') { ++i; return true; }
      if (c == '\\') {
        ++i;
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case '/': out.push_back('/'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case 'u': {
            if (i + 4 > s.size()) return false;
            unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
              unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
              if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
            }
            AppendUtf8(out, cp);
            break;
          }
          default: out.push_back(e);
        }
      } else {
        out.push_back(c);
        ++i;
      }
    }
    return false;
  }

  bool ParseArr(Json& out) {
    ++i;
    out = Json::Arr();
    SkipWs();
    if (i < s.size() && s[i] == ']') { ++i; return true; }
    while (true) {
      Json v;
      if (!ParseValue(v)) return false;
      out.arr.push_back(std::move(v));
      SkipWs();
      if (i < s.size() && s[i] == ',') { ++i; continue; }
      if (i < s.size() && s[i] == ']') { ++i; return true; }
      return false;
    }
  }

  bool ParseObj(Json& out) {
    ++i;
    out = Json::Obj();
    SkipWs();
    if (i < s.size() && s[i] == '}') { ++i; return true; }
    while (true) {
      SkipWs();
      std::string key;
      if (!ParseStr(key)) return false;
      SkipWs();
      if (i >= s.size() || s[i] != ':') return false;
      ++i;
      Json v;
      if (!ParseValue(v)) return false;
      out.obj.emplace_back(key, std::move(v));
      SkipWs();
      if (i < s.size() && s[i] == ',') { ++i; continue; }
      if (i < s.size() && s[i] == '}') { ++i; return true; }
      return false;
    }
  }
};
}

bool Json::Parse(const std::string& text, Json& out) {
  Parser p(text);
  if (!p.ParseValue(out)) return false;
  p.SkipWs();
  return true;
}
