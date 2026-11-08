#include "../src/Common.h"
#include "../src/Extract.h"
#include "../src/Export.h"
#include "../src/S2T.h"
#include "../src/TxtBook.h"
#include <cstdio>

static int gPass = 0, gFail = 0;

static void Check(bool ok, const std::string& what) {
  if (ok) { ++gPass; return; }
  ++gFail;
  printf("FAIL: %s\n", what.c_str());
}
static void CheckEq(const std::string& got, const std::string& want, const std::string& what) {
  if (got == want) { ++gPass; return; }
  ++gFail;
  printf("FAIL: %s\n  got =[%s]\n  want=[%s]\n", what.c_str(), got.c_str(), want.c_str());
}
static void CheckInt(long long got, long long want, const std::string& what) {
  if (got == want) { ++gPass; return; }
  ++gFail;
  printf("FAIL: %s (got %lld want %lld)\n", what.c_str(), got, want);
}

static int SelfTest() {
  // ---- 章节标题判定 ----
  Check(TxtLineLooksLikeHeading("第一章 开始"), "heading 第一章 开始");
  Check(TxtLineLooksLikeHeading("第1章"), "heading 第1章");
  Check(TxtLineLooksLikeHeading("第十二回 风起"), "heading 第十二回");
  Check(TxtLineLooksLikeHeading("Chapter 3 The End"), "heading Chapter 3");
  Check(TxtLineLooksLikeHeading("12、序"), "heading 12、序");
  Check(TxtLineLooksLikeHeading("楔子"), "heading 楔子");
  Check(TxtLineLooksLikeHeading("【第二卷 长安】"), "heading 【第二卷】");
  Check(!TxtLineLooksLikeHeading("第二章的正文内容。"), "not-heading 以句号结尾");
  Check(!TxtLineLooksLikeHeading("他说：第三章我会回来的，然后就走出了门，再也没有回来过。"), "not-heading 长句");
  Check(!TxtLineLooksLikeHeading(""), "not-heading 空行");
  Check(!TxtLineLooksLikeHeading("   "), "not-heading 空白行");

  // ---- 编码探测与解码 ----
  {
    const std::string utf8 = "中文abc";
    CheckEq(TxtBook::DetectEncoding(utf8), "UTF-8", "detect utf8");
    CheckEq(TxtBook::DetectEncoding(std::string("\xEF\xBB\xBF") + utf8), "UTF-8", "detect utf8 bom");
    const std::string gbk = std::string("\xD6\xD0\xCE\xC4");
    CheckEq(TxtBook::DetectEncoding(gbk), "GBK", "detect gbk");
    const std::string u16 = std::string("\xFF\xFE", 2) + std::string("\x2D\x4E", 2);
    CheckEq(TxtBook::DetectEncoding(u16), "UTF-16LE", "detect utf16le");
    std::string dec;
    Check(TxtBook::DecodeToUtf8(gbk, "GBK", dec), "decode gbk ok");
    CheckEq(dec, "中文", "decode gbk text");
    Check(TxtBook::DecodeToUtf8(u16, "UTF-16LE", dec), "decode utf16 ok");
    CheckEq(dec, "中", "decode utf16 text");
  }

  // ---- 简繁 ----
  CheckEq(ToSimplifiedUtf8("繁體字測試"), "繁体字测试", "t2s");
  CheckEq(ToTraditionalUtf8("繁体字测试"), "繁體字測試", "s2t");

  // ---- 载入三种编码的样例 ----
  struct Case { const char* file; const char* enc; };
  const Case cases[] = {
    { "tests/sample_utf8.txt", "UTF-8" },
    { "tests/sample_gbk.txt", "GBK" },
    { "tests/sample_utf16le.txt", "UTF-16LE" },
  };
  for (const Case& c : cases) {
    TxtBook b;
    std::string err;
    const bool ok = b.Load(U2W(c.file), TxtSplitRule{}, err);
    Check(ok, std::string("load ") + c.file + " err=" + err);
    if (!ok) continue;
    const std::string tag = c.file;
    CheckEq(b.Encoding(), c.enc, "encoding " + tag);
    CheckInt((long long)b.Chapters().size(), 4, "chapters " + tag);
    if (b.Chapters().size() < 4) continue;
    CheckEq(b.Chapters()[0].title, "前言", "title[0] " + tag);
    CheckEq(b.Chapters()[1].title, "第一章 开始", "title[1] " + tag);
    CheckEq(b.Chapters()[2].title, "第二章 转折", "title[2] " + tag);
    CheckEq(b.Chapters()[3].title, "Chapter 3 The End", "title[3] " + tag);
    std::vector<std::string> paras;
    Check(b.GetChapter(1, paras), "getChapter(1) " + tag);
    CheckInt((long long)paras.size(), 2, "paragraphs " + tag);
    if (!paras.empty()) CheckEq(paras[0], "这是第一章的第一段。", "para[0] " + tag);
  }

  // ---- 自定义正则（填了就只用用户的规则） ----
  {
    TxtBook b;
    std::string err;
    TxtSplitRule r;
    r.regex = "^第(.*)章[ ]";
    Check(b.Load(U2W("tests/sample_utf8.txt"), r, err), std::string("load with regex err=") + err);
    CheckInt((long long)b.Chapters().size(), 3, "custom regex replaces builtin");
    if (b.Chapters().size() >= 3) {
      CheckEq(b.Chapters()[1].title, "一", "custom regex capture title[1]");
      CheckEq(b.Chapters()[2].title, "二", "custom regex capture title[2]");
    }
  }
  {
    TxtBook b;
    std::string err;
    TxtSplitRule r;
    r.regex = "^Chapter +([0-9]+)";
    Check(b.Load(U2W("tests/sample_utf8.txt"), r, err), std::string("load with group regex err=") + err);
    CheckInt((long long)b.Chapters().size(), 2, "capture regex chapters");
    if (b.Chapters().size() >= 2) CheckEq(b.Chapters()[1].title, "3", "capture group used as title");
  }
  {
    TxtBook b;
    std::string err;
    TxtSplitRule r;
    r.regex = "((((";
    Check(b.Load(U2W("tests/sample_utf8.txt"), r, err), std::string("bad regex still loads err=") + err);
    CheckInt((long long)b.Chapters().size(), 4, "bad regex falls back to builtin");
  }

  // ---- 导出 ----
  {
    std::vector<ExportChapter> chs;
    chs.push_back(ExportChapter{ "第一章", { "繁體段落", "第二段" } });
    chs.push_back(ExportChapter{ "第二章", { "简体段落" } });
    const std::string merged = BuildMergedText(chs, true, false);
    CheckEq(merged, "第一章\n\n繁體段落\n第二段\n\n第二章\n\n简体段落\n", "merged text");
    const std::string simp = BuildMergedText(chs, true, true);
    Check(simp.find("繁体段落") != std::string::npos, "merged simplified");
    Check(simp.find("简体段落") != std::string::npos, "merged keeps simplified");
    const std::string noTitle = BuildMergedText(chs, false, false);
    Check(noTitle.find("第一章") == std::string::npos, "merged without titles");
    CheckEq(EncodeText("中文abc", TextEncoding::Gbk), std::string("\xD6\xD0\xCE\xC4") + "abc", "gbk encode");
    const std::string bom = EncodeText("中文", TextEncoding::Utf8Bom);
    CheckInt((long long)bom.size(), 9, "utf8 bom size");
    Check(bom.size() > 3 && (unsigned char)bom[0] == 0xEF && (unsigned char)bom[2] == 0xBF, "utf8 bom prefix");
    CheckEq(EncodeText("中文", TextEncoding::Utf8), "中文", "utf8 no bom");
  }

  printf("PASS=%d FAIL=%d\n", gPass, gFail);
  return gFail ? 1 : 0;
}

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc >= 2 && std::string(argv[1]) == "--selftest") return SelfTest();
  if (argc < 2) {
    printf("usage: txt_test <file.txt> [--enc X] [--regex R]\n       txt_test --selftest\n");
    return 2;
  }
  TxtSplitRule rule;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--enc" && i + 1 < argc) rule.encoding = argv[++i];
    else if (a == "--regex" && i + 1 < argc) rule.regex = argv[++i];
  }
  TxtBook b;
  std::string err;
  if (!b.Load(U2W(std::string(argv[1])), rule, err)) {
    printf("load failed: %s\n", err.c_str());
    return 1;
  }
  printf("file=%s\nencoding=%s\nrule=%s\ntitle=%s\nchapters=%zu\n",
         b.Path().c_str(), b.Encoding().c_str(), b.Rule().c_str(), b.Title().c_str(),
         b.Chapters().size());
  const auto& chs = b.Chapters();
  for (size_t i = 0; i < chs.size() && i < 5; ++i) printf("  [%zu] %s\n", i, chs[i].title.c_str());
  std::vector<std::string> paras;
  if (b.GetChapter(1, paras)) {
    printf("chapter[1] paragraphs=%zu\n", paras.size());
    for (size_t i = 0; i < paras.size() && i < 3; ++i) printf("    %s\n", paras[i].c_str());
  }
  return 0;
}
