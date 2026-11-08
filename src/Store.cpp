#include "Store.h"
#include <algorithm>

Json Settings::ToJson() const {
  Json j = Json::Obj();
  j["theme"] = Json::Str(theme);
  j["customBg"] = Json::Str(customBg);
  j["fontFamily"] = Json::Str(fontFamily);
  j["fontSize"] = Json::Num(fontSize);
  j["fontWeight"] = Json::Num(fontWeight);
  j["lineHeight"] = Json::Num(lineHeight);
  j["paraSpacing"] = Json::Num(paraSpacing);
  j["letterSpacing"] = Json::Num(letterSpacing);
  j["pageWidth"] = Json::Num(pageWidth);
  j["indent"] = Json::Bool(indent);
  j["scrollStepUnit"] = Json::Str(scrollStepUnit);
  j["scrollStep"] = Json::Num(scrollStep);
  j["simplified"] = Json::Bool(simplified);
  j["requestInterval"] = Json::Num(requestInterval);
  j["downloadGap"] = Json::Num(downloadGap);
  j["proxyMode"] = Json::Str(proxyMode);
  j["proxyManual"] = Json::Str(proxyManual);
  return j;
}

void Settings::FromJson(const Json& j) {
  theme = j.GetStr("theme", theme);
  customBg = j.GetStr("customBg", customBg);
  fontFamily = j.GetStr("fontFamily", fontFamily);
  fontSize = j.GetNum("fontSize", fontSize);
  fontWeight = (int)j.GetNum("fontWeight", fontWeight);
  lineHeight = j.GetNum("lineHeight", lineHeight);
  paraSpacing = j.GetNum("paraSpacing", paraSpacing);
  letterSpacing = j.GetNum("letterSpacing", letterSpacing);
  pageWidth = j.GetNum("pageWidth", pageWidth);
  indent = j.GetBool("indent", indent);
  scrollStepUnit = j.GetStr("scrollStepUnit", scrollStepUnit);
  scrollStep = j.GetNum("scrollStep", scrollStep);
  simplified = j.GetBool("simplified", simplified);
  requestInterval = j.GetNum("requestInterval", requestInterval);
  downloadGap = j.GetNum("downloadGap", downloadGap);
  proxyMode = j.GetStr("proxyMode", proxyMode);
  proxyManual = j.GetStr("proxyManual", proxyManual);
}

Json BookRecord::ToJson() const {
  Json j = Json::Obj();
  j["bookId"] = Json::Str(bookId);
  j["title"] = Json::Str(title);
  j["author"] = Json::Str(author);
  j["cover"] = Json::Str(cover);
  j["url"] = Json::Str(url);
  j["category"] = Json::Str(category);
  j["status"] = Json::Str(status);
  j["total"] = Json::Num(total);
  j["chapterIndex"] = Json::Num(chapterIndex);
  j["ratio"] = Json::Num(ratio);
  j["updatedAt"] = Json::Num((double)updatedAt);
  j["kind"] = Json::Str(kind);
  j["localPath"] = Json::Str(localPath);
  j["txtEncoding"] = Json::Str(txtEncoding);
  j["txtRegex"] = Json::Str(txtRegex);
  return j;
}

void BookRecord::FromJson(const Json& j) {
  bookId = j.GetStr("bookId");
  title = j.GetStr("title");
  author = j.GetStr("author");
  cover = j.GetStr("cover");
  url = j.GetStr("url");
  category = j.GetStr("category");
  status = j.GetStr("status");
  total = (int)j.GetNum("total", 0);
  chapterIndex = (int)j.GetNum("chapterIndex", 0);
  ratio = j.GetNum("ratio", 0);
  updatedAt = (int64_t)j.GetNum("updatedAt", 0);
  kind = j.GetStr("kind", kind);
  if (kind.empty()) kind = "web";
  localPath = j.GetStr("localPath", localPath);
  txtEncoding = j.GetStr("txtEncoding", txtEncoding);
  if (txtEncoding.empty()) txtEncoding = "auto";
  txtRegex = j.GetStr("txtRegex", txtRegex);
}

bool Store::Init(const std::wstring& dataDir) {
  dir_ = dataDir;
  EnsureDir(dir_);
  EnsureDir(dir_ + L"\\books");

  std::string text;
  if (ReadFileBytes(dir_ + L"\\settings.json", text)) {
    Json j;
    if (Json::Parse(text, j) && j.IsObj()) settings_.FromJson(j);
  }
  if (ReadFileBytes(dir_ + L"\\bookshelf.json", text)) {
    Json j;
    if (Json::Parse(text, j) && j.IsObj()) {
      const Json* items = j.Get("books");
      if (items && items->IsArr()) {
        for (auto& it : items->arr) {
          BookRecord b;
          b.FromJson(it);
          if (!b.bookId.empty()) books_.push_back(std::move(b));
        }
      }
    }
  }
  return true;
}

BookRecord* Store::Find(const std::string& bookId) {
  for (auto& b : books_) if (b.bookId == bookId) return &b;
  return nullptr;
}

BookRecord& Store::Upsert(const std::string& bookId) {
  BookRecord* found = Find(bookId);
  if (found) return *found;
  BookRecord b;
  b.bookId = bookId;
  books_.push_back(std::move(b));
  return books_.back();
}

std::wstring Store::BookDir(const std::string& bookId) const {
  return dir_ + L"\\books\\" + U2W(bookId);
}

std::wstring Store::ChapterPath(const std::string& bookId, int index) const {
  wchar_t name[32] = {};
  swprintf_s(name, L"%d.json", index);
  return BookDir(bookId) + L"\\" + name;
}

bool Store::ReadChapterCache(const std::string& bookId, int index, std::string& out) const {
  return ReadFileBytes(ChapterPath(bookId, index), out);
}

bool Store::WriteChapterCache(const std::string& bookId, int index, const std::string& data) const {
  EnsureDir(BookDir(bookId));
  return WriteFileBytes(ChapterPath(bookId, index), data);
}

std::vector<Store::CachedChapter> Store::CachedChapters(const std::string& bookId) const {
  std::vector<CachedChapter> out;
  WIN32_FIND_DATAW fd = {};
  HANDLE h = FindFirstFileW((BookDir(bookId) + L"/*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    const std::wstring name = fd.cFileName;
    if (name.size() < 6) continue;                                  // 至少 "0.json"
    if (name.compare(name.size() - 5, 5, L".json") != 0) continue;
    bool digits = true;
    for (size_t i = 0; i + 5 < name.size(); ++i) {
      if (name[i] < L'0' || name[i] > L'9') { digits = false; break; }
    }
    if (!digits) continue;                                          // 跳过 toc.json
    CachedChapter c;
    for (size_t i = 0; i + 5 < name.size(); ++i) c.index = c.index * 10 + (int)(name[i] - L'0');
    c.bytes = ((long long)fd.nFileSizeHigh << 32) | (long long)fd.nFileSizeLow;
    out.push_back(c);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end(),
            [](const CachedChapter& a, const CachedChapter& b) { return a.index < b.index; });
  out.erase(std::unique(out.begin(), out.end(),
                        [](const CachedChapter& a, const CachedChapter& b) { return a.index == b.index; }),
            out.end());
  return out;
}

std::wstring Store::TocPath(const std::string& bookId) const {
  return BookDir(bookId) + L"\\toc.json";
}

bool Store::ReadToc(const std::string& bookId, std::string& out) const {
  return ReadFileBytes(TocPath(bookId), out);
}

bool Store::WriteToc(const std::string& bookId, const std::string& data) const {
  EnsureDir(BookDir(bookId));
  return WriteFileBytes(TocPath(bookId), data);
}

void Store::SaveSettings() {
  EnsureDir(dir_);
  WriteFileBytes(dir_ + L"\\settings.json", settings_.ToJson().Dump());
}

void Store::SaveBookshelf() {
  EnsureDir(dir_);
  Json j = Json::Obj();
  Json arr = Json::Arr();
  for (auto& b : books_) arr.Push(b.ToJson());
  j["books"] = arr;
  WriteFileBytes(dir_ + L"\\bookshelf.json", j.Dump());
}
