#pragma once
#include "Common.h"

struct HtmlNode {
  std::string tag;   // lowercase tag name; empty for text nodes
  std::string text;  // raw text for text nodes
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<std::unique_ptr<HtmlNode>> children;
  HtmlNode* parent = nullptr;

  bool IsText() const { return tag.empty(); }
  const std::string* Attr(const std::string& name) const;
  std::string Id() const;
  std::string Class() const;
  bool HasClass(const std::string& c) const;
};

class HtmlDoc {
public:
  std::unique_ptr<HtmlNode> root;

  static HtmlDoc Parse(const std::string& html);
  HtmlNode* FindById(const std::string& id) const;
  void FindByClass(const std::string& cls, std::vector<HtmlNode*>& out) const;
  HtmlNode* FindFirstTag(const std::string& tag) const;
};

// Decode HTML entities in a UTF-8 string.
std::string HtmlDecode(const std::string& s);

// Collect decoded text of a subtree; <br> becomes '\n' when withBreaks.
std::string NodeText(const HtmlNode* n, bool withBreaks);
