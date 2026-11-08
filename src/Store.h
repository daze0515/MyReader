#pragma once
#include "Common.h"
#include "Json.h"

struct Settings {
  std::string theme = "sepia";
  std::string customBg = "#f5efe1";
  std::string fontFamily = "";
  double fontSize = 19;
  int fontWeight = 400;
  double lineHeight = 1.9;
  double paraSpacing = 0.9;
  double letterSpacing = 0.02;
  double pageWidth = 40;
  bool indent = true;
  std::string scrollStepUnit = "line";
  double scrollStep = 3;
  bool simplified = true;
  double requestInterval = 1.2;
  double downloadGap = 2.0;   // 离线下载时每章之间的间隔（秒），完全由用户设定
  std::string proxyMode = "system";
  std::string proxyManual = "";

  Json ToJson() const;
  void FromJson(const Json& j);
};

struct BookRecord {
  std::string bookId;
  std::string title;
  std::string author;
  std::string cover;
  std::string url;
  std::string category;
  std::string status;
  int total = 0;
  int chapterIndex = 0;
  double ratio = 0;
  int64_t updatedAt = 0;
  std::string kind = "web";   // web | local（本地 TXT 原地引用）
  std::string localPath;      // 本地 TXT 的绝对路径
  std::string txtEncoding = "auto";
  std::string txtRegex;       // 自定义章节正则

  Json ToJson() const;
  void FromJson(const Json& j);
};

class Store {
public:
  bool Init(const std::wstring& dataDir);

  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }

  std::vector<BookRecord>& books() { return books_; }
  BookRecord* Find(const std::string& bookId);
  BookRecord& Upsert(const std::string& bookId);

  std::wstring BookDir(const std::string& bookId) const;
  std::wstring ChapterPath(const std::string& bookId, int index) const;
  bool ReadChapterCache(const std::string& bookId, int index, std::string& out) const;
  bool WriteChapterCache(const std::string& bookId, int index, const std::string& data) const;
  // 已缓存的章节（按下标升序），带文件大小：下载面板提示 + 清理缓存都要用
  struct CachedChapter { int index = 0; long long bytes = 0; };
  std::vector<CachedChapter> CachedChapters(const std::string& bookId) const;

  // 目录落盘：离线下载后断网也能打开书架里的书
  std::wstring TocPath(const std::string& bookId) const;
  bool ReadToc(const std::string& bookId, std::string& out) const;
  bool WriteToc(const std::string& bookId, const std::string& data) const;

  void SaveSettings();
  void SaveBookshelf();
  std::wstring DataDir() const { return dir_; }

private:
  std::wstring dir_;
  Settings settings_;
  std::vector<BookRecord> books_;
};
