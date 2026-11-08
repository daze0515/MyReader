#include "../src/Common.h"
#include "../src/Extract.h"
#include "../src/SiteAdapter.h"
#include <cstdio>

static void PrintMeta(const BookMeta& m) {
  printf("bookId=%s\n", m.bookId.c_str());
  printf("title=%s\n", m.title.c_str());
  printf("author=%s\n", m.author.c_str());
  printf("category=%s status=%s\n", m.category.c_str(), m.status.c_str());
  printf("cover=%s\n", m.cover.c_str());
}

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 3) { printf("usage: extract_test <file.html> <toc|chapter> [baseUrl] [sitesDir]\n"); return 2; }
  std::string html;
  if (!ReadFileBytes(U2W(argv[1]), html)) { printf("read failed\n"); return 1; }

  const std::string base = argc > 3 ? argv[3] : "https://twkan.com/book/67581/index.html";
  SiteRegistry reg;
  reg.Load(argc > 4 ? U2W(argv[4]) : std::wstring());
  const SiteAdapter* a = reg.Match(base);
  printf("sites=%d matched=%s\n", reg.count(), a ? a->id.c_str() : "(none)");
  if (!a) a = &reg.Fallback();
  printf("adapter=%s bytes=%zu challenge=%d\n", a->id.c_str(), html.size(),
         (int)SiteLooksLikeChallenge(*a, html));

  const std::string mode = argv[2];
  if (mode == "toc" || mode == "list") {
    TocResult r = SiteParseToc(*a, html, base);
    printf("ok=%d error=%s\n", (int)r.ok, r.error.c_str());
    PrintMeta(r.meta);
    printf("chapters=%zu\n", r.chapters.size());
    for (size_t i = 0; i < r.chapters.size() && i < 3; ++i)
      printf("  [%zu] %s | words=%s | upd=%s | %s\n", i, r.chapters[i].title.c_str(),
             r.chapters[i].words.c_str(), r.chapters[i].updated.c_str(), r.chapters[i].url.c_str());
    if (!r.chapters.empty()) {
      const size_t n = r.chapters.size();
      printf("  last: %s -> %s\n", r.chapters[n - 1].title.c_str(), r.chapters[n - 1].url.c_str());
    }
  } else {
    BookMeta m;
    if (SiteParseLastRead(*a, html, m)) { printf("--- lastread ---\n"); PrintMeta(m); }
    Chapter c = SiteParseChapter(*a, html);
    printf("ok=%d error=%s\n", (int)c.ok, c.error.c_str());
    printf("title=%s\n", c.title.c_str());
    printf("paragraphs=%zu\n", c.paragraphs.size());
    for (size_t i = 0; i < c.paragraphs.size() && i < 8; ++i)
      printf("  [%zu] %s\n", i, c.paragraphs[i].c_str());
    if (c.paragraphs.size() > 8) {
      printf("  ...\n");
      for (size_t i = c.paragraphs.size() - 2; i < c.paragraphs.size(); ++i)
        printf("  [%zu] %s\n", i, c.paragraphs[i].c_str());
    }
  }
  return 0;
}
