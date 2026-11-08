#pragma once
#include "Common.h"

struct TocEntry {
  std::string title;
  std::string url;
  std::string words;
  std::string updated;
};

struct BookMeta {
  std::string bookId;
  std::string title;
  std::string author;
  std::string cover;
  std::string category;
  std::string status;
};

struct TocResult {
  bool ok = false;
  std::string error;
  BookMeta meta;
  std::vector<TocEntry> chapters;
};

struct Chapter {
  bool ok = false;
  std::string error;
  std::string title;
  std::string words;
  std::string updated;
  std::vector<std::string> paragraphs;
};
