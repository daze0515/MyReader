#pragma once
#include "Common.h"

class Json {
public:
  enum class T { Null, Bool, Num, Str, Arr, Obj };

  T t = T::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  Json() = default;
  static Json Bool(bool v);
  static Json Num(double v);
  static Json Str(const std::string& v);
  static Json Arr();
  static Json Obj();

  bool IsNull() const { return t == T::Null; }
  bool IsObj() const { return t == T::Obj; }
  bool IsArr() const { return t == T::Arr; }

  // object access
  Json& operator[](const std::string& key);
  const Json* Get(const std::string& key) const;
  std::string GetStr(const std::string& key, const std::string& def = "") const;
  double GetNum(const std::string& key, double def = 0) const;
  bool GetBool(const std::string& key, bool def = false) const;

  void Push(Json v) { arr.push_back(std::move(v)); }

  std::string Dump() const;
  static bool Parse(const std::string& text, Json& out);
};
