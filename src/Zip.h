#pragma once
#include "Common.h"

// 只读 zip（EPUB 用）：整包读进内存，按名字解压单个文件。底层是 third_party/miniz。
// 名字匹配不区分大小写（EPUB 里 href 的大小写经常和压缩包里对不上）。
class ZipReader {
 public:
  ZipReader();
  ~ZipReader();
  ZipReader(const ZipReader&) = delete;
  ZipReader& operator=(const ZipReader&) = delete;

  bool OpenFile(const std::wstring& path, std::string& err);
  bool OpenMemory(std::string data, std::string& err);
  void Close();
  bool IsOpen() const { return open_; }

  bool Has(const std::string& name) const;
  bool Read(const std::string& name, std::string& out) const;
  bool IsEncrypted(const std::string& name) const;
  std::vector<std::string> Names() const;

 private:
  int Locate(const std::string& name) const;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string data_;   // miniz 直接引用这块内存，打开期间不能改
  bool open_ = false;
};
