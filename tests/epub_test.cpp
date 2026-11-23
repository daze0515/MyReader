// EPUB 单元测试：路径拼接 / 正文段落提取 / 两个样本书（EPUB3 nav + EPUB2 ncx）的完整解析。
//   epub_test.exe --selftest        跑全部断言
//   epub_test.exe some.epub         打印任意一本 EPUB 的解析结果（排查真实书用）
#include "../src/Common.h"
#include "../src/EpubBook.h"
#include <cstdio>
#include <cstring>

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

static void CheckParas(const EpubBook& b, int index, const std::vector<std::string>& want, const std::string& tag) {
  std::vector<std::string> got;
  Check(b.GetChapter(index, got), tag + " getChapter");
  CheckInt((long long)got.size(), (long long)want.size(), tag + " paragraph count");
  for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
    CheckEq(got[i], want[i], tag + " p[" + std::to_string(i) + "]");
  }
}

static int SelfTest() {
  // ---- 路径 ----
  CheckEq(EpubResolvePath("OEBPS/", "Text/ch1.xhtml"), "OEBPS/Text/ch1.xhtml", "path relative");
  CheckEq(EpubResolvePath("OEBPS/Text/", "../Images/a.jpg"), "OEBPS/Images/a.jpg", "path ..");
  CheckEq(EpubResolvePath("OEBPS/", "Text/chapter%20two.xhtml#p1"), "OEBPS/Text/chapter two.xhtml", "path %20 + #frag");
  CheckEq(EpubResolvePath("a/b/", "./c.html?x=1"), "a/b/c.html", "path ./ + ?query");
  CheckEq(EpubResolvePath("OEBPS/", "/root.html"), "root.html", "path absolute");
  CheckEq(EpubResolvePath("", "../../x.html"), "x.html", "path .. above root");
  CheckEq(EpubResolvePath("OEBPS/", "https://example.com/a.html"), "", "path external link ignored");

  // ---- 段落提取 ----
  {
    const std::string x =
      "<html><head><title/><script>var s='<p>no</p>';</script></head><body>"
      "<h1>第一章<br/>开始</h1>"
      "<p>　　清晨，<span>阳光</span>\n   洒进窗户。</p>"
      "<p>他说：&ldquo;你好&rdquo;&#xFF0C;<ruby>笑<rt>xiao</rt></ruby>了。</p>"
      "<p>甲<br/>乙</p><p><img src='a.png'/></p><p>Hello\n  world.</p>"
      "</body></html>";
    std::string docTitle = "?";
    std::vector<EpubSegment> segs = EpubExtractSegments(x, {}, &docTitle);
    CheckInt((long long)segs.size(), 1, "extract: one segment without anchors");
    CheckEq(docTitle, "", "extract: self-closing <title/> does not swallow body");
    if (!segs.empty()) {
      const auto& p = segs[0].paragraphs;
      CheckInt((long long)p.size(), 7, "extract: paragraph count");
      if (p.size() == 7) {
        CheckEq(p[0], "第一章 开始", "extract: <br> inside heading -> space");
        CheckEq(p[1], "清晨，阳光洒进窗户。", "extract: newline between CJK dropped, indent trimmed");
        CheckEq(p[2], "他说：“你好”，笑了。", "extract: entities + ruby <rt> skipped");
        CheckEq(p[3], "甲", "extract: <br> splits paragraph (1)");
        CheckEq(p[4], "乙", "extract: <br> splits paragraph (2)");
        CheckEq(p[5], MakeLocalImagePara("a.png", ""), "extract: image-only <p> -> image paragraph");
        CheckEq(p[6], "Hello world.", "extract: newline between latin -> space");
      }
      CheckEq(segs[0].heading, "第一章 开始", "extract: heading captured");
    }
  }
  {
    const std::string x =
      "<body><p>前言</p><h2 id='a1'>甲章</h2><p>甲正文</p>"
      "<p><a name='a2'></a>乙章</p><p>乙正文</p></body>";
    std::vector<EpubSegment> segs = EpubExtractSegments(x, { "a1", "a2" });
    CheckInt((long long)segs.size(), 3, "anchors: 3 segments");
    if (segs.size() == 3) {
      CheckInt((long long)segs[0].paragraphs.size(), 1, "anchors: seg0 size");
      CheckInt((long long)segs[1].paragraphs.size(), 2, "anchors: seg1 size (id on heading)");
      CheckInt((long long)segs[2].paragraphs.size(), 2, "anchors: seg2 size (<a name>)");
      CheckEq(segs[1].heading, "甲章", "anchors: seg1 heading");
    }
  }

  // ---- 插图提取 ----
  {
    const std::string x =
      "<body>"
      "<p>风起<img src='../Images/g1.png' alt='叒'/>云涌。</p>"                  // 生僻字图片：alt 嵌回原处
      "<p>完<img src='../Images/g1.png' alt='装饰花纹'/>。</p>"                  // alt 太长：丢掉
      "<div class='pic'><img src='../Images/a%20b.jpg' alt='图 &amp; 注'/></div>"  // 单独成段，路径解码，alt 解码
      "<p><img src='a.png'/><img src='b.png'/></p>"                              // 一块里两张图
      "<svg xmlns:xlink='http://www.w3.org/1999/xlink'><image xlink:href='../Images/s.jpg'/>"
      "<text>svg 文字</text></svg>"                                               // SVG 只取 <image>
      "<p><img src='data:image/png;base64,AAAA'/></p>"                           // data: 跳过
      "<p><img src='https://example.com/x.png'/></p>"                            // 外链跳过
      "</body>";
    std::vector<EpubSegment> segs = EpubExtractSegments(x, {}, nullptr, "OEBPS/Text/");
    const auto& p = segs[0].paragraphs;
    CheckInt((long long)p.size(), 6, "images: paragraph count");
    if (p.size() == 6) {
      CheckEq(p[0], "风起叒云涌。", "images: gaiji alt inlined");
      CheckEq(p[1], "完。", "images: long alt inside text dropped");
      CheckEq(p[2], MakeLocalImagePara("OEBPS/Images/a b.jpg", "图 & 注"), "images: block image + baseDir + %20 + alt");
      CheckEq(p[3], MakeLocalImagePara("OEBPS/Text/a.png", ""), "images: two in one block (1)");
      CheckEq(p[4], MakeLocalImagePara("OEBPS/Text/b.png", ""), "images: two in one block (2)");
      CheckEq(p[5], MakeLocalImagePara("OEBPS/Images/s.jpg", ""), "images: svg <image xlink:href>");
    }
    std::string path, alt;
    Check(ParseLocalImagePara(MakeLocalImagePara("x/y.png", "注"), &path, &alt) && path == "x/y.png" && alt == "注",
          "images: make/parse round trip");
    Check(!IsLocalImagePara("普通段落"), "images: plain text is not image");
  }

  // ---- 图片宽高 / 类型 / URL 转义 ----
  {
    int w = 0, h = 0;
    const std::string gif = std::string("GIF89a") + std::string("\x2C\x01\xC8\x00", 4) + std::string(8, '\0');
    Check(ImageDimensions(gif, w, h) && w == 300 && h == 200, "dims: gif");
    std::string bmp(30, '\0');
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[18] = '\x40'; bmp[19] = '\x01';                                   // 320
    bmp[22] = '\x10'; bmp[23] = '\xFF'; bmp[24] = '\xFF'; bmp[25] = '\xFF';  // -240
    Check(ImageDimensions(bmp, w, h) && w == 320 && h == 240, "dims: bmp (top-down, negative height)");
    std::string vp8x = std::string("RIFF") + std::string(4, '\0') + "WEBPVP8X" + std::string(18, '\0');
    vp8x[24] = '\x7F'; vp8x[25] = '\x02';                                 // 640-1
    vp8x[27] = '\xDF'; vp8x[28] = '\x01';                                 // 480-1
    Check(ImageDimensions(vp8x, w, h) && w == 640 && h == 480, "dims: webp VP8X");
    Check(!ImageDimensions("not an image at all", w, h), "dims: garbage rejected");
    CheckEq(ImageMimeType(gif, "x.bin"), "image/gif", "mime: sniff gif");
    CheckEq(ImageMimeType("<svg/>", "a/B.SVG"), "image/svg+xml", "mime: svg by extension");
    CheckEq(ImageMimeType("<html/>", "a/b.xhtml"), "application/octet-stream", "mime: xhtml is not an image");
    CheckEq(EpubUrlEncodePath("OEBPS/Images/插图 1.jpg"), "OEBPS/Images/%E6%8F%92%E5%9B%BE%201.jpg", "url: encode");
    CheckEq(EpubUrlDecode(EpubUrlEncodePath("a/插 图#1.png")), "a/插 图#1.png", "url: round trip");
  }

  // ---- EPUB3 样本：nav.xhtml 目录、封面页/目录页跳过、文件名带空格、插图（SVG 插图页 / 题图 / 生僻字图） ----
  {
    EpubBook b;
    std::string err;
    const bool ok = b.Load(U2W("tests/sample_epub3.epub"), err);
    Check(ok, "epub3 load err=" + err);
    if (ok) {
      CheckEq(b.Format(), "epub", "epub3 format");
      CheckEq(b.Version(), "3.0", "epub3 version");
      CheckEq(b.Title(), "测试小说 & 番外", "epub3 title (entity decoded)");
      CheckEq(b.Author(), "张三", "epub3 author");
      Check(b.CoverBytes().size() > 64, "epub3 cover-image found");
      CheckEq(b.CoverMime(), "image/png", "epub3 cover mime");
      CheckInt(b.ChapterCount(), 4, "epub3 chapters (cover + nav skipped, svg illustration kept)");
      if (b.ChapterCount() == 4) {
        CheckEq(b.ChapterTitle(0), "第一章 开始", "epub3 title[0] from nav");
        CheckEq(b.ChapterTitle(1), "插图", "epub3 title[1] image-only page");
        CheckEq(b.ChapterTitle(2), "第二章　转折", "epub3 title[2] from nav");
        CheckEq(b.ChapterTitle(3), "第三章 结局", "epub3 title[3] from <h2> (not in toc)");
        CheckEq(b.ChapterFile(2), "OEBPS/Text/chapter two.xhtml", "epub3 %20 href resolved");
        CheckParas(b, 0, { "清晨，阳光洒进窗户。", "他说：“你好”，然后笑了。", "第一行", "第二行",
                           MakeLocalImagePara("OEBPS/Images/cover.png", "插图"),
                           "English words wrap across lines." }, "epub3 ch0");
        CheckParas(b, 1, { MakeLocalImagePara("OEBPS/Images/illus.jpg", "") }, "epub3 ch1 (svg page)");
        CheckParas(b, 2, { "风起叒云涌。", "引用的一段话。" }, "epub3 ch2 (heading dropped, gaiji)");
        CheckParas(b, 3, { MakeLocalImagePara("OEBPS/Images/illus.jpg", "题图 & 装饰"), "完。" },
                   "epub3 ch3 (head image kept, heading after it dropped)");
        std::vector<std::string> again;
        b.GetChapter(0, again);
        CheckInt((long long)again.size(), 6, "epub3 re-read chapter 0 after another file");

        int w = 0, h = 0;
        Check(b.ImageSize("OEBPS/Images/illus.jpg", w, h) && w == 120 && h == 80, "epub3 jpeg size 120x80");
        Check(b.ImageSize("OEBPS/Images/cover.png", w, h) && w == 60 && h == 90, "epub3 png size 60x90");
        Check(b.ImageSize("oebps/images/G1.PNG", w, h) && w == 16 && h == 16, "epub3 size lookup ignores case");
        Check(!b.ImageSize("OEBPS/Images/missing.png", w, h), "epub3 missing image -> no size");
        std::string bytes, mime;
        Check(b.ReadResource("OEBPS/Images/illus.jpg", bytes, mime) && mime == "image/jpeg" && bytes.size() > 100,
              "epub3 resource: jpeg served");
        Check(!b.ReadResource("OEBPS/Text/ch1.xhtml", bytes, mime), "epub3 resource: xhtml refused");
        Check(!b.ReadResource("OEBPS/Images/missing.png", bytes, mime), "epub3 resource: missing refused");
      }
      std::vector<std::string> none;
      Check(!b.GetChapter(4, none), "epub3 out of range");
    }
  }

  // ---- EPUB2 样本：toc.ncx（含嵌套）、一个文件按锚点切成多章、GBK 正文、带 BOM 的 UTF-8 ----
  {
    EpubBook b;
    std::string err;
    const bool ok = b.Load(U2W("tests/sample_epub2.epub"), err);
    Check(ok, "epub2 load err=" + err);
    if (ok) {
      CheckEq(b.Version(), "2.0", "epub2 version");
      CheckEq(b.Title(), "旧版测试", "epub2 title");
      CheckEq(b.Author(), "李四", "epub2 author");
      Check(b.CoverBytes().size() > 64, "epub2 <meta name=cover> found");
      CheckInt(b.ChapterCount(), 4, "epub2 chapters (anchor split)");
      if (b.ChapterCount() == 4) {
        CheckEq(b.ChapterTitle(0), "序", "epub2 title[0]");
        CheckEq(b.ChapterTitle(1), "第1章 起", "epub2 title[1]");
        CheckEq(b.ChapterTitle(2), "第2章 承", "epub2 title[2] (nested navPoint)");
        CheckEq(b.ChapterTitle(3), "后记", "epub2 title[3]");
        CheckParas(b, 0, { "这是序言。" }, "epub2 ch0 (content before first anchor)");
        CheckParas(b, 1, { "第一章正文。" }, "epub2 ch1 (GBK decoded, heading dropped)");
        CheckParas(b, 2, { "第二章正文。" }, "epub2 ch2 (<a name> anchor)");
        CheckParas(b, 3, { "后记内容。" }, "epub2 ch3 (UTF-8 BOM)");
      }
    }
  }

  // ---- 坏文件 ----
  {
    EpubBook b;
    std::string err;
    Check(!b.Load(U2W("tests/no_such_file.epub"), err), "missing file rejected");
    Check(!err.empty(), "missing file has error text");
    err.clear();
    Check(!b.Load(U2W("tests/sample_utf8.txt"), err), "non-zip rejected");
    Check(err.find("EPUB") != std::string::npos, "non-zip error mentions EPUB: " + err);
  }

  // ---- 扩展名判断 ----
  Check(IsEpubPath(L"D:/a/B.EPUB"), "IsEpubPath uppercase");
  Check(!IsEpubPath(L"D:/a/b.txt"), "IsEpubPath txt");
  Check(IsSupportedLocalPath(L"x.Txt") && IsSupportedLocalPath(L"x.epub"), "IsSupportedLocalPath");
  Check(!IsSupportedLocalPath(L"x.pdf"), "IsSupportedLocalPath pdf");

  printf("PASS=%d FAIL=%d\n", gPass, gFail);
  return gFail == 0 ? 0 : 1;
}

static int Dump(const char* file) {
  EpubBook b;
  std::string err;
  if (!b.Load(U2W(file), err)) {
    printf("file=%s\nerror=%s\n", file, err.c_str());
    return 1;
  }
  printf("file=%s\nversion=%s\ntitle=%s\nauthor=%s\ncover=%d bytes (%s)\nchapters=%d\n", file,
         b.Version().c_str(), b.Title().c_str(), b.Author().c_str(), (int)b.CoverBytes().size(),
         b.CoverMime().c_str(), b.ChapterCount());
  for (int i = 0; i < b.ChapterCount(); ++i) {
    std::vector<std::string> paras;
    b.GetChapter(i, paras);
    printf("  [%d] %s  (%d paragraphs, %s)\n", i, b.ChapterTitle(i).c_str(), (int)paras.size(),
           b.ChapterFile(i).c_str());
    if (i < 2) {
      for (size_t k = 0; k < paras.size() && k < 3; ++k) {
        std::string img, alt;
        if (ParseLocalImagePara(paras[k], &img, &alt)) printf("      [插图] %s %s\n", img.c_str(), alt.c_str());
        else printf("      %s\n", paras[k].c_str());
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc >= 2 && strcmp(argv[1], "--selftest") != 0) return Dump(argv[1]);
  return SelfTest();
}
