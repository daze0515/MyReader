#include "../src/MobiBook.h"
#include <algorithm>
#include <cstdio>

namespace {
int passed = 0;
int failed = 0;

void Check(bool ok, const char* label) {
  if (ok) ++passed;
  else {
    ++failed;
    std::fprintf(stderr, "FAIL %s\n", label);
  }
}

size_t RecordOffset(const std::string& bytes, size_t index) {
  const size_t offset = 78 + 8 * index;
  size_t value = 0;
  for (size_t slot = 0; slot < 4; ++slot) {
    value = (value << 8) | static_cast<unsigned char>(bytes.at(offset + slot));
  }
  return value;
}

void CheckAltered(MobiBook& book, const std::string& bytes, const std::string& expected, const char* label) {
  wchar_t tempDir[MAX_PATH]{};
  wchar_t path[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, tempDir) || !GetTempFileNameW(tempDir, L"mbr", 0, path)) {
    Check(false, "create temp file");
    return;
  }
  std::string error;
  const bool wrote = WriteFileBytes(path, bytes);
  const bool loaded = wrote && book.Load(path, error);
  DeleteFileW(path);
  Check(wrote && (expected.empty() ? loaded : !loaded && error.find(expected) != std::string::npos), label);
}

bool Has(const std::vector<std::string>& values, const char* needle) {
  return std::any_of(values.begin(), values.end(), [needle](const auto& value) {
    return value.find(needle) != std::string::npos;
  });
}

void CheckImage(const MobiBook& book, const std::vector<std::string>& paragraphs,
                const char* expectedPath, const char* expectedAlt, int width, int height) {
  const auto picture = std::find_if(paragraphs.begin(), paragraphs.end(), IsLocalImagePara);
  std::string path, alt, bytes, mime;
  Check(picture != paragraphs.end() && ParseLocalImagePara(*picture, &path, &alt), "chapter has image");
  Check(path == expectedPath && alt == expectedAlt, "image path and alt");
  Check(book.ReadResource(path, bytes, mime) && mime == "image/png" && !bytes.empty(), "image resource");
  int actualWidth = 0, actualHeight = 0;
  Check(book.ImageSize(path, actualWidth, actualHeight) && actualWidth == width && actualHeight == height,
        "image size");
}

int SelfTest() {
  MobiBook book;
  std::string error, bytes, mime;
  std::vector<std::string> paragraphs;
  Check(book.Load(L"tests/sample_mobi6.mobi", error), "load MOBI6");
  Check(book.Title() == "测试 MOBI6" && book.Author() == "测试作者", "EXTH metadata");
  Check(book.CoverBytes().size() > 64, "EXTH cover");
  Check(book.ChapterCount() == 2, "MOBI6 chapter count");
  Check(book.ChapterTitle(0) == "目录第一节" && book.ChapterTitle(1) == "目录第二节", "MOBI6 NCX labels");
  Check(book.GetChapter(0, paragraphs) && Has(paragraphs, "中文正文 café"), "PalmDOC UTF-8 with trailer");
  Check(Has(paragraphs, "AAAAB A"), "PalmDOC back reference and space token");
  CheckImage(book, paragraphs, "rec/1", "湖面", 11, 13);
  Check(book.GetChapter(1, paragraphs) && Has(paragraphs, "Second chapter"), "second record follows trailer");
  Check(!book.ReadResource("rec/2", bytes, mime), "non-image resource rejected");
  Check(!book.ReadResource("rec/9999999", bytes, mime), "outside image resource rejected");
  Check(!book.ReadResource("rec/../0", bytes, mime), "invalid image path rejected");
  Check(!book.GetChapter(2, paragraphs) && paragraphs.empty(), "out of range chapter clears output");

  Check(ReadFileBytes(L"tests/sample_mobi6.mobi", bytes), "read fixture for negative cases");
  const size_t header = RecordOffset(bytes, 0);
  const size_t body = RecordOffset(bytes, 1);
  std::string changed = bytes;
  changed.replace(header + 0xf4, 4, 4, '\xff');
  CheckAltered(book, changed, {}, "pagebreak fallback loads");
  Check(book.ChapterCount() == 2 && book.ChapterTitle(1) == "第二节", "pagebreak fallback and heading");
  changed = bytes;
  changed[header + 13] = 2;
  CheckAltered(book, changed, "DRM", "encryption rejected");
  changed = bytes;
  changed[header] = 0x44;
  changed[header + 1] = 0x48;
  CheckAltered(book, changed, "HUFF/CDIC", "HUFF/CDIC rejected");
  CheckAltered(book, bytes.substr(0, body + 5), "截断", "truncated record table rejected");
  changed = bytes;
  changed[body] = '\x80';
  changed[body + 1] = 0;
  CheckAltered(book, changed, "损坏", "invalid PalmDOC back reference");
  changed = bytes;
  changed[RecordOffset(bytes, 2) - 1] = '\xff';
  CheckAltered(book, changed, "损坏", "invalid trailer length");
  Check(!book.Load(L"tests/sample_utf8.txt", error) && error.find("不是有效") != std::string::npos,
        "non-MOBI rejected");

  Check(book.Load(L"tests/sample_azw3.azw3", error), "load AZW3");
  Check(book.Title() == "测试 AZW3" && book.Author() == "测试作者", "KF8 metadata");
  Check(book.ChapterCount() == 2, "KF8 splits skeletons into chapters");
  Check(book.ChapterTitle(0) == "KF8 目录章一" && book.ChapterTitle(1) == "KF8 目录章二",
        "KF8 NCX titles from fragment identifiers");
  Check(book.GetChapter(0, paragraphs) && Has(paragraphs, "第一片正文") && Has(paragraphs, "第二片正文"),
        "KF8 multiple fragments in same skeleton");
  Check(!Has(paragraphs, "第二章正文") && !Has(paragraphs, "color:green"), "KF8 chapters and CSS flow separate");
  CheckImage(book, paragraphs, "rec/10", "远山", 17, 19);
  Check(book.GetChapter(1, paragraphs) && Has(paragraphs, "第二章正文"), "KF8 second skeleton");

  Check(book.Load(L"tests/sample_hybrid.mobi", error), "load hybrid");
  Check(book.Title() == "测试 AZW3" && book.ChapterTitle(0) == "KF8 目录章一", "hybrid prefers KF8");
  Check(book.CoverBytes().size() > 64, "hybrid legacy cover");
  Check(book.GetChapter(0, paragraphs) && Has(paragraphs, "第二片正文"), "hybrid KF8 content");
  CheckImage(book, paragraphs, "rec/10", "远山", 17, 19);
  std::printf("MOBI SELFTEST %d passed, %d failed\n", passed, failed);
  return failed != 0;
}
}

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc == 2 && std::wstring_view(argv[1]) == L"--selftest") return SelfTest();
  if (argc != 2) {
    std::fprintf(stderr, "usage: mobi_test --selftest | <book>\n");
    return 2;
  }
  MobiBook book;
  std::string error;
  if (!book.Load(argv[1], error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  std::printf("%s / %s / %d chapters / %zu cover bytes\n",
              book.Title().c_str(), book.Author().c_str(), book.ChapterCount(), book.CoverBytes().size());
  for (int index = 0; index < std::min(book.ChapterCount(), 8); ++index) {
    std::vector<std::string> paragraphs;
    book.GetChapter(index, paragraphs);
    const size_t images = std::count_if(paragraphs.begin(), paragraphs.end(), IsLocalImagePara);
    std::printf("%d. %s (%zu paragraphs, %zu images)\n", index + 1, book.ChapterTitle(index).c_str(),
                paragraphs.size(), images);
  }
  return 0;
}
