#include "Zip.h"
// 只用 mz_ 前缀的接口，关掉 compress / crc32 之类的 zlib 同名宏，免得污染别的代码
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../third_party/miniz/miniz.h"
#include <cstring>

struct ZipReader::Impl {
  mz_zip_archive zip;
};

ZipReader::ZipReader() : impl_(std::make_unique<Impl>()) {
  mz_zip_zero_struct(&impl_->zip);
}

ZipReader::~ZipReader() { Close(); }

void ZipReader::Close() {
  if (open_) mz_zip_reader_end(&impl_->zip);
  open_ = false;
  mz_zip_zero_struct(&impl_->zip);
  data_.clear();
}

bool ZipReader::OpenFile(const std::wstring& path, std::string& err) {
  std::string bytes;
  if (!ReadFileBytes(path, bytes)) {
    err = "无法读取文件（可能已被移动或删除）";
    return false;
  }
  return OpenMemory(std::move(bytes), err);
}

bool ZipReader::OpenMemory(std::string data, std::string& err) {
  Close();
  data_ = std::move(data);
  if (data_.size() < 22) {   // 连 zip 的结尾目录都放不下
    err = "不是有效的 EPUB（文件太小）";
    data_.clear();
    return false;
  }
  if (!mz_zip_reader_init_mem(&impl_->zip, data_.data(), data_.size(), 0)) {
    const char* why = mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip));
    err = std::string("不是有效的 EPUB（zip 解析失败：") + (why ? why : "未知错误") + "）";
    mz_zip_zero_struct(&impl_->zip);
    data_.clear();
    return false;
  }
  open_ = true;
  return true;
}

int ZipReader::Locate(const std::string& name) const {
  if (!open_ || name.empty()) return -1;
  // 不带 MZ_ZIP_FLAG_CASE_SENSITIVE = 不区分大小写
  return mz_zip_reader_locate_file(&impl_->zip, name.c_str(), nullptr, 0);
}

bool ZipReader::Has(const std::string& name) const { return Locate(name) >= 0; }

bool ZipReader::Read(const std::string& name, std::string& out) const {
  out.clear();
  const int idx = Locate(name);
  if (idx < 0) return false;
  mz_zip_archive_file_stat st;
  memset(&st, 0, sizeof(st));
  if (!mz_zip_reader_file_stat(&impl_->zip, (mz_uint)idx, &st)) return false;
  if (st.m_uncomp_size == 0) return true;
  size_t n = 0;
  void* p = mz_zip_reader_extract_to_heap(&impl_->zip, (mz_uint)idx, &n, 0);
  if (!p) return false;
  out.assign(static_cast<const char*>(p), n);
  mz_free(p);
  return true;
}

bool ZipReader::IsEncrypted(const std::string& name) const {
  const int idx = Locate(name);
  return idx >= 0 && mz_zip_reader_is_file_encrypted(&impl_->zip, (mz_uint)idx);
}

std::vector<std::string> ZipReader::Names() const {
  std::vector<std::string> out;
  if (!open_) return out;
  const mz_uint n = mz_zip_reader_get_num_files(&impl_->zip);
  for (mz_uint i = 0; i < n; ++i) {
    char buf[1024] = {};
    if (mz_zip_reader_get_filename(&impl_->zip, i, buf, (mz_uint)sizeof(buf)) > 0) out.push_back(buf);
  }
  return out;
}
