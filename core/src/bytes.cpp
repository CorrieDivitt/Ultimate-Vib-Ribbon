#include "vib/bytes.h"

#include <fstream>

namespace vib {

Bytes read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  Bytes data(size);
  if (size && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
    throw std::runtime_error("cannot read " + path.string());
  return data;
}

void write_file(const std::filesystem::path& path, ByteView data) {
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot create " + path.string());
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!out) throw std::runtime_error("cannot write " + path.string());
}

}  // namespace vib
