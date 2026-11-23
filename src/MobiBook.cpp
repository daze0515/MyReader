#include "MobiBook.h"
#include "EpubBook.h"
#include "Html.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace {
using View = std::string_view;
constexpr uint32_t kMissing = UINT32_MAX;
constexpr size_t kMaxBook = 256 * 1024 * 1024;
constexpr size_t kMaxMarkup = 128 * 1024 * 1024;
constexpr size_t kMaxChapter = 16 * 1024 * 1024;
constexpr size_t kMaxEntries = 100000;

struct InvalidMobi {};

void Require(bool condition) {
  if (!condition) throw InvalidMobi{};
}

View Slice(View data, size_t offset, size_t length) {
  Require(offset <= data.size() && length <= data.size() - offset);
  return data.substr(offset, length);
}

uint8_t Byte(View data, size_t offset) {
  return static_cast<uint8_t>(Slice(data, offset, 1)[0]);
}

uint32_t Big(View data, size_t offset, size_t length = 4) {
  uint32_t value = 0;
  for (unsigned char byte : Slice(data, offset, length)) value = (value << 8) | byte;
  return value;
}

uint32_t VarInt(View data, size_t& offset) {
  uint32_t value = 0;
  for (int count = 0; count < 5; ++count) {
    uint8_t byte = Byte(data, offset++);
    Require(value <= (UINT32_MAX >> 7));
    value = (value << 7) | (byte & 0x7f);
    if (byte & 0x80) return value;
  }
  throw InvalidMobi{};
}

bool Number(View text, uint32_t& value) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool KindleBase32(View text, uint32_t& result) {
  if (text.empty() || text.size() > 7) return false;
  result = 0;
  for (char ch : text) {
    if (ch >= 'a' && ch <= 'v') ch = static_cast<char>(ch - 'a' + 'A');
    unsigned digit = ch >= '0' && ch <= '9' ? ch - '0' :
                     ch >= 'A' && ch <= 'V' ? ch - 'A' + 10 : 32;
    if (digit >= 32 || result > (UINT32_MAX >> 5)) return false;
    result = result * 32 + digit;
  }
  return true;
}

char LowerAscii(char ch) {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool StartsNoCase(View text, View prefix) {
  return text.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), text.begin(),
                    [](char left, char right) { return left == LowerAscii(right); });
}

std::string Decode(View data, unsigned encoding) {
  size_t end = data.find('\0');
  if (end != View::npos) data = data.substr(0, end);
  if (data.empty() || encoding == 65001) return std::string(data);
  const int size = static_cast<int>(data.size());
  const int count = MultiByteToWideChar(1252, 0, data.data(), size, nullptr, 0);
  if (count <= 0) return {};
  std::wstring wide(count, L'\0');
  MultiByteToWideChar(1252, 0, data.data(), size, wide.data(), count);
  return W2U(wide);
}

struct Header {
  uint32_t version = 0;
  uint32_t compression = 0;
  uint32_t encoding = 0;
  uint32_t encryption = 0;
  uint32_t textCount = 0;
  uint32_t textSize = 0;
  uint32_t extraFlags = 0;
  uint32_t firstImage = kMissing;
  uint32_t cover = kMissing;
  uint32_t boundary = kMissing;
  uint32_t ncx = kMissing;
  uint32_t fragments = kMissing;
  uint32_t skeletons = kMissing;
  uint32_t fdst = kMissing;
  uint32_t flowCount = 0;
  std::string title;
  std::string author;
};

Header ReadHeader(View record) {
  Require(Slice(record, 16, 4) == "MOBI");
  const size_t length = Big(record, 20);
  Require(length >= 0x74 && length <= record.size() - 16);
  const size_t headerEnd = length + 16;
  const auto optional = [&](size_t offset, size_t width, uint32_t fallback) {
    return offset + width <= headerEnd ? Big(record, offset, width) : fallback;
  };
  Header head;
  head.compression = Big(record, 0, 2);
  head.textSize = Big(record, 4);
  head.textCount = Big(record, 8, 2);
  head.encryption = Big(record, 12, 2);
  head.encoding = Big(record, 0x1c);
  head.version = Big(record, 0x24);
  head.firstImage = optional(0x6c, 4, kMissing);
  if (optional(0x68, 4, 0) >= 5) head.extraFlags = optional(0xf2, 2, 0);
  head.ncx = optional(0xf4, 4, kMissing);
  if (head.version == 8) {
    head.fragments = optional(0xf8, 4, kMissing);
    head.skeletons = optional(0xfc, 4, kMissing);
    head.fdst = optional(0xc0, 4, kMissing);
    head.flowCount = optional(0xc4, 4, 0);
  }
  if (head.encryption != 0) return head;
  if (optional(0x80, 4, 0) & 0x40) {
    const View exth = Slice(record, headerEnd, Big(record, headerEnd + 4));
    Require(Slice(exth, 0, 4) == "EXTH");
    const uint32_t count = Big(exth, 8);
    Require(count <= (exth.size() - 12) / 8);
    size_t cursor = 12;
    for (uint32_t entry = 0; entry < count; ++entry) {
      const uint32_t kind = Big(exth, cursor);
      const uint32_t size = Big(exth, cursor + 4);
      Require(size >= 8);
      const View field = Slice(exth, cursor + 8, size - 8);
      if (kind == 100 && head.author.empty()) head.author = Decode(field, head.encoding);
      if (kind == 503 && head.title.empty()) head.title = Decode(field, head.encoding);
      if (kind == 121 && field.size() == 4) head.boundary = Big(field, 0);
      if (kind == 201 && field.size() == 4) head.cover = Big(field, 0);
      cursor += size;
    }
  }
  if (head.title.empty()) {
    const uint32_t titleLength = Big(record, 0x58);
    if (titleLength) head.title = Decode(Slice(record, Big(record, 0x54), titleLength), head.encoding);
  }
  return head;
}

View StripTrailer(View record, uint32_t flags) {
  for (uint32_t entries = flags >> 1; entries; entries >>= 1) {
    if (!(entries & 1)) continue;
    uint32_t length = 0;
    size_t bytes = 0;
    bool found = false;
    while (bytes < 4 && bytes < record.size()) {
      const uint8_t byte = Byte(record, record.size() - 1 - bytes);
      length |= static_cast<uint32_t>(byte & 0x7f) << (7 * bytes++);
      if (byte & 0x80) { found = true; break; }
    }
    Require(found && length >= bytes && length <= record.size());
    record.remove_suffix(length);
  }
  if (flags & 1) {
    const size_t length = (Byte(record, record.size() - 1) & 3) + 1;
    Require(length <= record.size());
    record.remove_suffix(length);
  }
  return record;
}

void UnpackPalmDoc(View record, std::string& out) {
  const size_t start = out.size();
  for (size_t cursor = 0; cursor < record.size();) {
    const uint8_t token = Byte(record, cursor++);
    size_t length = 1;
    if (token >= 1 && token <= 8) length = token;
    if (token >= 0x80 && token < 0xc0) {
      const unsigned pair = (token << 8) | Byte(record, cursor++);
      const size_t distance = (pair >> 3) & 0x7ff;
      length = (pair & 7) + 3;
      Require(distance > 0 && distance <= out.size() - start && length <= kMaxMarkup - out.size());
      for (size_t pos = 0; pos < length; ++pos) out.push_back(out[out.size() - distance]);
    } else {
      const size_t growth = token >= 0xc0 ? 2 : length;
      Require(growth <= kMaxMarkup - out.size());
      if (token >= 0xc0) {
        out.push_back(' ');
        out.push_back(static_cast<char>(token ^ 0x80));
      } else if (token >= 1 && token <= 8) {
        out.append(Slice(record, cursor, length));
        cursor += length;
      } else {
        out.push_back(static_cast<char>(token));
      }
    }
  }
}

struct IndexEntry {
  View key;
  std::map<unsigned, std::vector<uint32_t>> fields;

  uint32_t Get(unsigned tag, size_t slot = 0) const {
    const auto it = fields.find(tag);
    return it != fields.end() && slot < it->second.size() ? it->second[slot] : kMissing;
  }
};

struct Index {
  std::vector<IndexEntry> entries;
  std::vector<View> strings;

  View String(uint32_t offset) const {
    const size_t record = offset >> 16;
    if (record >= strings.size()) return {};
    size_t pos = offset & 0xffff;
    const size_t length = VarInt(strings[record], pos);
    return Slice(strings[record], pos, length);
  }
};

void ReadFields(View tags, size_t controlCount, View data, IndexEntry& entry) {
  const View controls = Slice(data, 0, controlCount);
  struct Field {
    unsigned tag;
    size_t count;
    bool byBytes;
  };
  std::vector<Field> fields;
  size_t control = 0;
  size_t cursor = controlCount;
  for (size_t offset = 0; offset < tags.size(); offset += 4) {
    if (Byte(tags, offset + 3) == 1) { ++control; continue; }
    const unsigned mask = Byte(tags, offset + 2);
    const unsigned bits = Byte(controls, control) & mask;
    if (!bits) continue;
    const bool byBytes = bits == mask && std::popcount(mask) > 1;
    const size_t count = byBytes ? VarInt(data, cursor) :
        (bits >> std::countr_zero(mask)) * Byte(tags, offset + 1);
    fields.push_back({Byte(tags, offset), count, byBytes});
  }
  for (const auto& field : fields) {
    auto& values = entry.fields[field.tag];
    Require(field.count <= data.size() - cursor);
    if (field.byBytes) {
      const View encoded = Slice(data, 0, cursor + field.count);
      while (cursor < encoded.size()) values.push_back(VarInt(encoded, cursor));
    } else {
      for (size_t slot = 0; slot < field.count; ++slot) values.push_back(VarInt(data, cursor));
    }
  }
}

template <typename RecordReader>
Index ReadIndex(const RecordReader& record, size_t start) {
  const View main = record(start);
  Require(Slice(main, 0, 4) == "INDX");
  const size_t indexCount = Big(main, 24);
  const size_t stringCount = Big(main, 52);
  Require(indexCount <= UINT16_MAX && stringCount <= UINT16_MAX);
  const size_t tagsAt = Big(main, 4);
  Require(Slice(main, tagsAt, 4) == "TAGX");
  const size_t tagSize = Big(main, tagsAt + 4);
  const size_t controls = Big(main, tagsAt + 8);
  Require(tagSize >= 12 && tagSize <= 1024 && (tagSize - 12) % 4 == 0 && controls <= 64);
  const View tags = Slice(main, tagsAt + 12, tagSize - 12);
  Index result;
  for (size_t part = 0; part < indexCount; ++part) {
    const View data = record(start + 1 + part);
    Require(Slice(data, 0, 4) == "INDX");
    const size_t positions = Big(data, 20);
    const size_t count = Big(data, 24);
    Require(positions >= Big(data, 4) && Slice(data, positions, 4) == "IDXT");
    Require(count <= (data.size() - positions - 4) / 2 && count <= kMaxEntries - result.entries.size());
    for (size_t row = 0; row < count; ++row) {
      const size_t begin = Big(data, positions + 4 + row * 2, 2);
      const size_t end = row + 1 == count ? positions : Big(data, positions + 6 + row * 2, 2);
      Require(begin >= Big(data, 4) && begin < end && end <= positions);
      const View line = Slice(data, begin, end - begin);
      IndexEntry entry;
      entry.key = Slice(line, 1, Byte(line, 0));
      ReadFields(tags, controls, line.substr(1 + entry.key.size()), entry);
      result.entries.push_back(std::move(entry));
    }
  }
  for (size_t part = 0; part < stringCount; ++part) {
    const View text = record(start + 1 + indexCount + part);
    Require(!text.empty());
    result.strings.push_back(text);
  }
  return result;
}

struct Link {
  uint32_t offset;
  uint32_t fragment;
  uint32_t within;
  std::string title;
};

template <typename RecordReader>
std::vector<Link> ReadLinks(const RecordReader& record, size_t start, const Header& head) {
  if (head.ncx == kMissing) return {};
  try {
    std::vector<Link> links;
    const auto index = ReadIndex(record, start + head.ncx);
    for (const auto& entry : index.entries) {
      const uint32_t label = entry.Get(3);
      links.push_back({entry.Get(1), entry.Get(6), entry.Get(6, 1),
                       label == kMissing ? std::string() : Decode(index.String(label), head.encoding)});
    }
    return links;
  } catch (const InvalidMobi&) {
    return {};
  }
}

std::string PictureMime(View data) {
  return ImageMimeType(std::string(data.substr(0, 16)), {});
}

bool TagAt(View html, View name) {
  if (!StartsNoCase(html, name) || html.size() <= name.size()) return false;
  const char next = html[name.size()];
  return next == '>' || next == '/' || next == ' ' || next == '\t' || next == '\n' || next == '\r';
}

std::string ImagePath(const HtmlNode& node, View href, bool kf8) {
  uint32_t image = 0;
  constexpr View prefix = "kindle:embed:";
  if (kf8 && StartsNoCase(href, prefix)) {
    href.remove_prefix(prefix.size());
    if (!KindleBase32(href.substr(0, href.find('?')), image)) return {};
  } else {
    const std::string* record = node.Attr("recindex");
    if (!record || !Number(*record, image)) return {};
  }
  return image > 0 ? "rec/" + std::to_string(image - 1) : std::string();
}

std::map<size_t, std::string> ChapterCuts(View html, const std::vector<Link>& links) {
  std::map<size_t, std::string> cuts;
  for (const auto& link : links) {
    if (link.offset < html.size() && !link.title.empty()) cuts.try_emplace(link.offset, link.title);
  }
  if (cuts.empty()) {
    for (size_t pos = html.find('<'); pos != View::npos; pos = html.find('<', pos + 1)) {
      if (TagAt(html.substr(pos), "<mbp:pagebreak")) cuts.try_emplace(pos);
    }
  }
  cuts.try_emplace(0);
  return cuts;
}

template <typename RecordReader>
View MainFlow(View markup, const RecordReader& record, size_t start, const Header& head) {
  if (head.flowCount <= 1) return markup;
  Require(head.fdst != kMissing);
  const View fdst = record(start + head.fdst);
  Require(Slice(fdst, 0, 4) == "FDST" && Big(fdst, 8) == head.flowCount);
  const size_t offset = Big(fdst, 4);
  Require(offset >= 12 && head.flowCount <= (fdst.size() - std::min(offset, fdst.size())) / 8);
  const size_t begin = Big(fdst, offset);
  const size_t end = Big(fdst, offset + 4);
  Require(end >= begin);
  return Slice(markup, begin, end - begin);
}

std::string LabelForSkeleton(const std::vector<std::pair<size_t, const Link*>>& labels, size_t begin,
                             size_t length) {
  const auto label = std::lower_bound(labels.begin(), labels.end(), begin,
      [](const auto& item, size_t position) { return item.first < position; });
  return label != labels.end() && label->first - begin < length ? label->second->title : std::string();
}

template <typename RecordReader, typename AddChapter>
void ReadKf8(View markup, const RecordReader& record, size_t start, const Header& head,
             const std::vector<Link>& links, AddChapter addChapter) {
  const View main = MainFlow(markup, record, start, head);
  if (head.skeletons == kMissing) {
    Require(head.fragments == kMissing);
    addChapter(std::string(main), {});
    return;
  }
  const auto skeletons = ReadIndex(record, start + head.skeletons);
  const auto fragments = head.fragments == kMissing ? Index{} : ReadIndex(record, start + head.fragments);
  std::vector<std::pair<size_t, const Link*>> labels;
  for (const auto& link : links) {
    if (link.title.empty()) continue;
    size_t position = link.offset;
    if (link.fragment < fragments.entries.size() && link.within != kMissing) {
      uint32_t insertion = 0;
      if (Number(fragments.entries[link.fragment].key, insertion)) {
        position = static_cast<size_t>(insertion) + link.within;
      }
    }
    if (position < main.size()) labels.emplace_back(position, &link);
  }
  std::stable_sort(labels.begin(), labels.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
  size_t nextFragment = 0;
  size_t previousEnd = 0;
  size_t shiftBudget = kMaxMarkup * 8;
  for (const auto& skeleton : skeletons.entries) {
    const size_t begin = skeleton.Get(6);
    const size_t length = skeleton.Get(6, 1);
    const size_t count = skeleton.Get(1);
    Require(begin >= previousEnd && length <= kMaxChapter && count <= fragments.entries.size() - nextFragment);
    std::string part(Slice(main, begin, length));
    size_t fragmentData = begin + length;
    for (size_t number = 0; number < count; ++number) {
      const auto& fragment = fragments.entries[nextFragment++];
      const size_t fragmentLength = fragment.Get(6, 1);
      uint32_t insertion = 0;
      Require(Number(fragment.key, insertion) && insertion >= begin);
      const size_t offset = insertion - begin;
      Require(offset <= part.size() && fragmentLength <= kMaxChapter - part.size());
      const size_t shift = part.size() - offset;
      Require(shift <= shiftBudget);
      shiftBudget -= shift;
      part.insert(offset, Slice(main, fragmentData, fragmentLength));
      fragmentData += fragmentLength;
    }
    previousEnd = fragmentData;
    addChapter(std::move(part), LabelForSkeleton(labels, begin, fragmentData - begin));
  }
  Require(nextFragment == fragments.entries.size());
}
}

bool MobiBook::Load(const std::wstring& path, std::string& error) {
  *this = MobiBook{};
  error.clear();
  try {
    WIN32_FILE_ATTRIBUTE_DATA file{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &file) ||
        (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      error = "无法读取本地电子书文件";
      return false;
    }
    if (file.nFileSizeHigh || file.nFileSizeLow > kMaxBook) {
      error = "MOBI 文件过大（上限 256 MiB）";
      return false;
    }
    if (!ReadFileBytes(path, bytes_)) {
      error = "无法读取本地电子书文件";
      return false;
    }
    path_ = path;
    if (Parse(error)) return true;
  } catch (const InvalidMobi&) {
    error = "MOBI 文件损坏、已截断或包含不支持的索引";
  } catch (const std::bad_alloc&) {
    error = "可用内存不足，无法打开 MOBI";
  } catch (const std::length_error&) {
    error = "MOBI 文件内容过大";
  }
  *this = MobiBook{};
  return false;
}

std::string_view MobiBook::Record(size_t index) const {
  Require(index < offsets_.size() - 1);
  return View(bytes_).substr(offsets_[index], offsets_[index + 1] - offsets_[index]);
}

bool MobiBook::Parse(std::string& error) {
  const View database(bytes_);
  if (database.size() < 78 || database.substr(60, 8) != "BOOKMOBI") {
    error = "不是有效的 MOBI / AZW3 文件";
    return false;
  }
  Require(database.size() <= kMaxBook && Big(database, 72) == 0);
  const size_t count = Big(database, 76, 2);
  const size_t tableEnd = 78 + count * 8;
  Require(count >= 2 && tableEnd <= database.size());
  offsets_.reserve(count + 1);
  size_t previous = tableEnd;
  for (size_t index = 0; index < count; ++index) {
    const size_t offset = Big(database, 78 + index * 8);
    Require(offset >= previous && offset <= database.size());
    offsets_.push_back(offset);
    previous = offset;
  }
  offsets_.push_back(database.size());

  const auto record = [this](size_t index) { return Record(index); };
  const Header legacy = ReadHeader(record(0));
  Header head = legacy;
  size_t start = 0;
  if (legacy.version < 8 && legacy.boundary != kMissing) {
    const size_t boundary = legacy.boundary;
    if (boundary > 0 && boundary < count && record(boundary - 1) == "BOUNDARY") start = boundary;
    else if (boundary + 1 < count && record(boundary) == "BOUNDARY") start = boundary + 1;
    if (start) {
      head = ReadHeader(record(start));
      Require(head.version == 8);
    }
  }
  if (head.encryption || legacy.encryption) {
    error = "这本书带 DRM 加密，无法打开";
    return false;
  }
  if (head.compression == 17480) {
    error = "暂不支持 HUFF/CDIC 压缩的 MOBI";
    return false;
  }
  if ((head.compression != 1 && head.compression != 2) ||
      (head.encoding != 65001 && head.encoding != 1252) || head.version > 8) {
    error = "不支持此 MOBI 版本、编码或压缩方式";
    return false;
  }
  Require(head.textCount > 0 && head.textCount < count - start && head.textSize <= kMaxMarkup);
  kf8_ = head.version == 8;
  title_ = TrimAscii(head.title.empty() ? legacy.title : head.title);
  author_ = TrimAscii(head.author.empty() ? legacy.author : head.author);
  if (title_.empty()) {
    const size_t name = path_.find_last_of(L"/\\");
    title_ = W2U(path_.substr(name == std::wstring::npos ? 0 : name + 1));
    const size_t dot = title_.find_last_of('.');
    if (dot != std::string::npos) title_.resize(dot);
  }

  firstImage_ = imageEnd_ = count;
  const bool shared = start && legacy.firstImage != kMissing && legacy.firstImage > legacy.textCount &&
                      legacy.firstImage < start - 1;
  if (shared) {
    firstImage_ = legacy.firstImage;
    imageEnd_ = start - 1;
  } else if (head.firstImage != kMissing && head.firstImage > head.textCount && head.firstImage < count - start) {
    firstImage_ = start + head.firstImage;
  }
  cover_ = count;
  const size_t cover = head.cover != kMissing ? head.cover : legacy.cover;
  if (cover < imageEnd_ - firstImage_ && PictureMime(record(firstImage_ + cover)) != "application/octet-stream") {
    cover_ = firstImage_ + cover;
  }

  std::string markup;
  markup.reserve(std::min<size_t>(head.textSize, database.size()));
  for (size_t index = 1; index <= head.textCount; ++index) {
    const View payload = StripTrailer(record(start + index), head.extraFlags);
    if (head.compression == 2) UnpackPalmDoc(payload, markup);
    else {
      Require(payload.size() <= kMaxMarkup - markup.size());
      markup.append(payload);
    }
  }
  Require(markup.size() >= head.textSize);
  if (head.textSize > 0) markup.resize(head.textSize);
  if (View(markup).starts_with("%MOP")) {
    error = "暂不支持固定版式的 Kindle Print Replica";
    return false;
  }
  const auto links = ReadLinks(record, start, head);
  if (kf8_) {
    ReadKf8(markup, record, start, head, links, [&](std::string html, std::string title) {
      AddChapter(std::move(html), std::move(title), head.encoding);
    });
  } else {
    const auto cuts = ChapterCuts(markup, links);
    Require(cuts.size() <= kMaxEntries);
    for (auto cut = cuts.begin(); cut != cuts.end(); ++cut) {
      const auto next = std::next(cut);
      const size_t length = (next == cuts.end() ? markup.size() : next->first) - cut->first;
      AddChapter(markup.substr(cut->first, length), cut->second, head.encoding);
    }
  }
  if (!chapters_.empty()) return true;
  error = "这本 MOBI 没有可阅读的章节";
  return false;
}

void MobiBook::AddChapter(std::string html, std::string title, unsigned encoding) {
  Require(html.size() <= kMaxChapter);
  bool hasNonCoverImage = false;
  auto segments = EpubExtractSegments(Decode(html, encoding), {}, nullptr, {},
      [&](const HtmlNode& node, const std::string& href) {
        const std::string path = ImagePath(node, href, kf8_);
        size_t index = 0;
        std::string mime;
        if (Resource(path, index, mime).empty()) return std::string();
        hasNonCoverImage |= index != cover_;
        return path;
      }, 256);
  if (segments.empty()) return;
  auto paragraphs = std::move(segments.front().paragraphs);
  const auto firstText = std::find_if(paragraphs.begin(), paragraphs.end(),
                                     [](const auto& text) { return !IsLocalImagePara(text); });
  const bool hasText = firstText != paragraphs.end();
  if (paragraphs.empty() || (!hasText && !hasNonCoverImage)) return;
  title = TrimAscii(HtmlDecode(title));
  if (title.empty()) title = segments.front().heading;
  if (title.empty()) {
    title = hasText ? "第 " + std::to_string(chapters_.size() + 1) + " 节" : "插图";
  }
  if (hasText && paragraphs.size() > 1 && TrimAscii(*firstText) == title) {
    paragraphs.erase(firstText);
  }
  chapters_.push_back({std::move(title), std::move(paragraphs)});
}

std::string MobiBook::ChapterTitle(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= chapters_.size()) return {};
  return chapters_[index].title;
}

bool MobiBook::GetChapter(int index, std::vector<std::string>& paragraphs) const {
  paragraphs.clear();
  if (index < 0 || static_cast<size_t>(index) >= chapters_.size()) return false;
  paragraphs = chapters_[index].paragraphs;
  return true;
}

std::string_view MobiBook::CoverBytes() const {
  return !offsets_.empty() && cover_ < offsets_.size() - 1 ? Record(cover_) : View{};
}

std::string_view MobiBook::Resource(const std::string& path, size_t& index, std::string& mime) const {
  const View name(path);
  uint32_t number = 0;
  if (!name.starts_with("rec/") || !Number(name.substr(4), number) || number >= imageEnd_ - firstImage_) {
    return {};
  }
  index = firstImage_ + number;
  const View data = Record(index);
  mime = PictureMime(data);
  return mime == "application/octet-stream" ? View{} : data;
}

bool MobiBook::ReadResource(const std::string& path, std::string& bytes, std::string& mime) const {
  size_t index = 0;
  mime.clear();
  const View data = Resource(path, index, mime);
  bytes.assign(data);
  return !bytes.empty();
}

bool MobiBook::ImageSize(const std::string& path, int& width, int& height) const {
  size_t index = 0;
  std::string mime;
  const View data = Resource(path, index, mime);
  if (data.empty()) return false;
  const auto [entry, fresh] = imageSizes_.try_emplace(index, 0, 0);
  if (fresh) {
    int imageWidth = 0;
    int imageHeight = 0;
    if (ImageDimensions(std::string(data), imageWidth, imageHeight)) entry->second = {imageWidth, imageHeight};
  }
  width = entry->second.first;
  height = entry->second.second;
  return width > 0 && height > 0;
}
