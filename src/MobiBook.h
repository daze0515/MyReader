#pragma once
#include "LocalBook.h"

class MobiBook : public LocalBook {
 public:
  bool Load(const std::wstring& path, std::string& error);

  const char* Format() const override { return "mobi"; }
  const std::wstring& PathW() const override { return path_; }
  const std::string& Title() const override { return title_; }
  std::string Category() const override { return "本地 MOBI"; }
  std::string Author() const override { return author_; }
  std::string_view CoverBytes() const override;
  int ChapterCount() const override { return static_cast<int>(chapters_.size()); }
  std::string ChapterTitle(int index) const override;
  bool GetChapter(int index, std::vector<std::string>& paragraphs) const override;
  bool ImageSize(const std::string& path, int& width, int& height) const override;
  bool ReadResource(const std::string& path, std::string& bytes, std::string& mime) const override;

 private:
  struct Chapter {
    std::string title;
    std::vector<std::string> paragraphs;
  };

  bool Parse(std::string& error);
  void AddChapter(std::string html, std::string title, unsigned encoding);
  std::string_view Record(size_t index) const;
  std::string_view Resource(const std::string& path, size_t& index, std::string& mime) const;

  std::wstring path_;
  std::string bytes_;
  std::string title_;
  std::string author_;
  std::vector<size_t> offsets_;
  std::vector<Chapter> chapters_;
  size_t firstImage_ = 0;
  size_t imageEnd_ = 0;
  size_t cover_ = 0;
  bool kf8_ = false;
  mutable std::map<size_t, std::pair<int, int>> imageSizes_;
};
