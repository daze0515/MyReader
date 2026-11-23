#pragma once
#include "LocalBook.h"
#include "Zip.h"

// EPUB 正文里的一段内容：段落文字 + 本段第一个 h1–h6 标题
struct EpubSegment {
  std::vector<std::string> paragraphs;
  std::string heading;
};

struct HtmlNode;
using LocalImageResolver = std::function<std::string(const HtmlNode&, const std::string&)>;

// 把一份 XHTML 正文拆成段落。插图（<img> / SVG 里的 <image>）单独成段，见 MakeLocalImagePara；
// 夹在文字中间、alt 只有一两个字的小图（生僻字图片）把 alt 嵌回原处。
// anchors 非空时，遇到 id（或 <a name>）等于 anchors[k] 的元素就切到第 k+1 段，
// 一共返回 anchors.size()+1 段，第 0 段是第一个锚点之前的内容。
// docTitle 非空时顺便取出 <title> 的文字；baseDir 是这份正文在包内的目录，用来解析图片路径。
std::vector<EpubSegment> EpubExtractSegments(const std::string& xhtml,
                                             const std::vector<std::string>& anchors,
                                             std::string* docTitle = nullptr,
                                             const std::string& baseDir = std::string(),
                                             const LocalImageResolver& resolveImage = {},
                                             size_t maxDepth = 0);

// 只看文件头取图片宽高（PNG / JPEG / GIF / WebP / BMP），失败返回 false
bool ImageDimensions(const std::string& bytes, int& w, int& h);
// 按文件头识别图片类型，认不出再看扩展名
std::string ImageMimeType(const std::string& bytes, const std::string& path);
// 包内路径 <-> URL 路径（%XX 转义，'/' 保留）
std::string EpubUrlEncodePath(const std::string& path);
std::string EpubUrlDecode(const std::string& s);

// zip 内相对路径拼接 + 规范化：去掉 #锚点 / ?参数，解 %XX 转义，处理 ./ 和 ../
std::string EpubResolvePath(const std::string& baseDir, const std::string& href);

// 本地 EPUB：原地引用，打开时整包读进内存，章节正文与插图按需解压、解析。
// 章节顺序以 spine 为准，标题取自目录（EPUB3 nav.xhtml / EPUB2 toc.ncx）；
// 一个文件里有多个带锚点的目录项时，按锚点把它切成多章。
class EpubBook : public LocalBook {
 public:
  bool Load(const std::wstring& path, std::string& err);

  const char* Format() const override { return "epub"; }
  const std::wstring& PathW() const override { return pathW_; }
  const std::string& Title() const override { return title_; }
  int ChapterCount() const override { return (int)chapters_.size(); }
  std::string ChapterTitle(int index) const override;
  bool GetChapter(int index, std::vector<std::string>& paragraphs) const override;

  std::string Category() const override { return "本地 EPUB"; }
  std::string Author() const override { return author_; }
  const std::string& Version() const { return version_; }
  std::string_view CoverBytes() const override { return cover_; }
  const std::string& CoverMime() const { return coverMime_; }
  std::string ChapterFile(int index) const;
  // 插图：宽高（有缓存）与原始字节。只认图片，别的文件一律不给
  bool ImageSize(const std::string& path, int& w, int& h) const override;
  bool ReadResource(const std::string& path, std::string& bytes, std::string& mime) const override;

 private:
  struct Item {
    std::string file;   // zip 内路径
    int segFrom = 0;    // 该章覆盖这个文件的第 segFrom..segTo 段
    int segTo = 0;
    std::string title;
  };

  bool Parse(std::string& err);
  bool ReadText(const std::string& name, std::string& out) const;
  const std::vector<EpubSegment>& Segments(const std::string& file) const;

  std::wstring pathW_;
  std::string title_;
  std::string author_;
  std::string version_;
  std::string cover_;
  std::string coverMime_;
  std::vector<Item> chapters_;
  std::map<std::string, std::vector<std::string>> anchors_;   // 需要按锚点切分的文件 -> 锚点
  ZipReader zip_;

  // 最近解析过的那个文件（同一文件切出来的多章连着读时不用重复解析）
  mutable std::string segFile_;
  mutable std::vector<EpubSegment> segCache_;
  mutable std::map<std::string, std::pair<int, int>> sizes_;   // 小写路径 -> 宽高（0 = 读不出）
};
