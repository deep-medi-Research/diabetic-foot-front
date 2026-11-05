#include "hwlab/platform/npy_archive.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

#include "hwlab/platform/shot_folder.h"

namespace hwlab::platform {

namespace {

constexpr uint32_t kLocalHeader = 0x04034b50;
constexpr uint32_t kCentralHeader = 0x02014b50;
constexpr uint32_t kEndOfCentral = 0x06054b50;
constexpr uint32_t kZip64Locator = 0x07064b50;
constexpr uint32_t kZip64EndOfCentral = 0x06064b50;
constexpr uint16_t kZip64ExtraId = 0x0001;
constexpr uint32_t kFull32 = 0xFFFFFFFFu;

// 호스트 엔디언과 무관하게 리틀 엔디언으로 읽습니다.
uint64_t LittleEndian(std::string_view bytes, size_t at, size_t width) {
  uint64_t value = 0;
  for (size_t i = 0; i < width; ++i) value |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
  return value;
}

bool Fits(std::string_view bytes, uint64_t at, uint64_t length) {
  return at <= bytes.size() && length <= bytes.size() - at;
}

uint32_t Crc32(std::string_view bytes) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  uint32_t crc = kFull32;
  for (const char b : bytes) crc = table[(crc ^ static_cast<unsigned char>(b)) & 0xFFu] ^ (crc >> 8);
  return crc ^ kFull32;
}

// numpy 머리는 파이썬 dict 문자 표기입니다. 이 형식에서 키 하나의 값 시작 위치만 찾습니다.
size_t ValueStart(const std::string& header, const char* key) {
  for (const char quote : {'\'', '"'}) {
    const std::string quoted = std::string(1, quote) + key + quote;
    size_t at = header.find(quoted);
    if (at == std::string::npos) continue;
    at = header.find(':', at + quoted.size());
    if (at == std::string::npos) return std::string::npos;
    return header.find_first_not_of(" \t", at + 1);
  }
  return std::string::npos;
}

std::optional<std::string> QuotedValue(const std::string& header, const char* key) {
  const size_t at = ValueStart(header, key);
  if (at == std::string::npos || (header[at] != '\'' && header[at] != '"')) return std::nullopt;
  const size_t end = header.find(header[at], at + 1);
  if (end == std::string::npos) return std::nullopt;
  return header.substr(at + 1, end - at - 1);
}

std::optional<std::vector<int64_t>> ShapeValue(const std::string& header) {
  size_t at = ValueStart(header, "shape");
  if (at == std::string::npos || header[at] != '(') return std::nullopt;
  const size_t end = header.find(')', at);
  if (end == std::string::npos) return std::nullopt;
  std::vector<int64_t> shape;
  int64_t value = 0;
  bool digits = false;
  for (size_t i = at + 1; i <= end; ++i) {
    const char c = header[i];
    if (c >= '0' && c <= '9') {
      if (value > (std::numeric_limits<int64_t>::max() - 9) / 10) return std::nullopt;
      value = value * 10 + (c - '0');
      digits = true;
    } else if (c == ',' || c == ')') {
      if (digits) shape.push_back(value);
      else if (c == ',') return std::nullopt;
      value = 0;
      digits = false;
    } else if (c != ' ' && c != 'L') {
      return std::nullopt;
    }
  }
  return shape;
}

struct Entry {
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc = 0;
  uint64_t size = 0;
  uint64_t local_offset = 0;
};

std::optional<std::vector<Entry>> CentralDirectory(std::string_view bytes, std::string* why) {
  // 끝 레코드 뒤에 주석이 최대 65535바이트까지 붙을 수 있어 뒤에서부터 찾습니다.
  if (bytes.size() < 22) {
    *why = "ZIP 끝 레코드가 없습니다";
    return std::nullopt;
  }
  size_t end = std::string_view::npos;
  const size_t lowest = bytes.size() > 22 + 0xFFFF ? bytes.size() - 22 - 0xFFFF : 0;
  for (size_t at = bytes.size() - 22;; --at) {
    if (LittleEndian(bytes, at, 4) == kEndOfCentral) {
      end = at;
      break;
    }
    if (at == lowest) break;
  }
  if (end == std::string_view::npos) {
    *why = "ZIP 끝 레코드가 없습니다";
    return std::nullopt;
  }
  uint64_t count = LittleEndian(bytes, end + 10, 2);
  uint64_t dir_size = LittleEndian(bytes, end + 12, 4);
  uint64_t dir_offset = LittleEndian(bytes, end + 16, 4);
  if (count == 0xFFFF || dir_size == kFull32 || dir_offset == kFull32) {
    if (end < 20 || LittleEndian(bytes, end - 20, 4) != kZip64Locator) {
      *why = "zip64 위치 레코드가 없습니다";
      return std::nullopt;
    }
    const uint64_t record = LittleEndian(bytes, end - 20 + 8, 8);
    if (!Fits(bytes, record, 56) || LittleEndian(bytes, record, 4) != kZip64EndOfCentral) {
      *why = "zip64 끝 레코드가 없습니다";
      return std::nullopt;
    }
    count = LittleEndian(bytes, record + 32, 8);
    dir_size = LittleEndian(bytes, record + 40, 8);
    dir_offset = LittleEndian(bytes, record + 48, 8);
  }
  if (!Fits(bytes, dir_offset, dir_size)) {
    *why = "중앙 디렉터리가 파일 밖을 가리킵니다";
    return std::nullopt;
  }
  std::vector<Entry> entries;
  uint64_t at = dir_offset;
  for (uint64_t i = 0; i < count; ++i) {
    if (!Fits(bytes, at, 46) || LittleEndian(bytes, at, 4) != kCentralHeader) {
      *why = "중앙 디렉터리 항목 머리가 틀립니다";
      return std::nullopt;
    }
    Entry e;
    e.flags = static_cast<uint16_t>(LittleEndian(bytes, at + 8, 2));
    e.method = static_cast<uint16_t>(LittleEndian(bytes, at + 10, 2));
    e.crc = static_cast<uint32_t>(LittleEndian(bytes, at + 16, 4));
    uint64_t packed = LittleEndian(bytes, at + 20, 4);
    uint64_t unpacked = LittleEndian(bytes, at + 24, 4);
    const uint64_t name_length = LittleEndian(bytes, at + 28, 2);
    const uint64_t extra_length = LittleEndian(bytes, at + 30, 2);
    const uint64_t comment_length = LittleEndian(bytes, at + 32, 2);
    e.local_offset = LittleEndian(bytes, at + 42, 4);
    if (!Fits(bytes, at + 46, name_length + extra_length + comment_length)) {
      *why = "중앙 디렉터리 항목이 파일 밖으로 나갑니다";
      return std::nullopt;
    }
    e.name = std::string(bytes.substr(at + 46, name_length));
    // zip64 추가 필드에는 32비트 칸이 0xFFFFFFFF인 값만 이 순서로 들어 있습니다.
    for (uint64_t x = at + 46 + name_length; x + 4 <= at + 46 + name_length + extra_length;) {
      const uint64_t id = LittleEndian(bytes, x, 2);
      const uint64_t length = LittleEndian(bytes, x + 2, 2);
      if (x + 4 + length > at + 46 + name_length + extra_length) break;
      if (id == kZip64ExtraId) {
        uint64_t field = x + 4;
        for (uint64_t* slot : {&unpacked, &packed, &e.local_offset}) {
          if (*slot != kFull32) continue;
          if (field + 8 > x + 4 + length) {
            *why = "zip64 추가 필드가 짧습니다: " + e.name;
            return std::nullopt;
          }
          *slot = LittleEndian(bytes, field, 8);
          field += 8;
        }
      }
      x += 4 + length;
    }
    if (e.method == 0 && packed != unpacked) {
      *why = "무압축 항목의 크기가 서로 다릅니다: " + e.name;
      return std::nullopt;
    }
    e.size = packed;
    entries.push_back(std::move(e));
    at += 46 + name_length + extra_length + comment_length;
  }
  return entries;
}

double ElementAt(std::string_view data, size_t index, char kind, size_t width) {
  const uint64_t raw = LittleEndian(data, index * width, width);
  if (kind == 'f' && width == 8) {
    double v = 0.0;
    std::memcpy(&v, &raw, sizeof(v));
    return v;
  }
  if (kind == 'f') {
    const auto bits = static_cast<uint32_t>(raw);
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return static_cast<double>(v);
  }
  if (width == 8) return static_cast<double>(static_cast<int64_t>(raw));
  return static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(raw)));
}

}  // namespace

std::optional<NpyArray> ReadNpy(std::string_view bytes, std::string* skip_reason, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<NpyArray> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  const auto skip = [&](const std::string& why) -> std::optional<NpyArray> {
    if (skip_reason != nullptr) *skip_reason = why;
    return std::nullopt;
  };
  if (bytes.size() < 10 || bytes.substr(0, 6) != std::string_view("\x93NUMPY", 6)) {
    return fail(".npy 시작 바이트가 아닙니다");
  }
  const auto major = static_cast<unsigned char>(bytes[6]);
  if (major < 1 || major > 3) return fail(".npy 머리 버전 " + std::to_string(major) + "은 읽지 않습니다");
  const size_t length_width = major == 1 ? 2 : 4;
  if (bytes.size() < 8 + length_width) return fail(".npy 머리 길이가 잘렸습니다");
  const uint64_t header_length = LittleEndian(bytes, 8, length_width);
  const uint64_t data_start = 8 + length_width + header_length;
  if (!Fits(bytes, 8 + length_width, header_length)) return fail(".npy 머리가 잘렸습니다");
  const std::string header(bytes.substr(8 + length_width, header_length));

  const auto descr = QuotedValue(header, "descr");
  if (!descr) {
    // 구조체 dtype은 descr가 문자열이 아니라 목록입니다.
    if (ValueStart(header, "descr") != std::string::npos) return skip("구조체 dtype");
    return fail(".npy 머리에 descr가 없습니다");
  }
  const size_t fortran_at = ValueStart(header, "fortran_order");
  if (fortran_at == std::string::npos) return fail(".npy 머리에 fortran_order가 없습니다");
  const bool fortran = header.compare(fortran_at, 4, "True") == 0;
  if (!fortran && header.compare(fortran_at, 5, "False") != 0) return fail(".npy fortran_order 값이 틀립니다");
  const auto shape = ShapeValue(header);
  if (!shape) return fail(".npy 머리의 shape를 읽지 못했습니다");

  if (*descr != "<f8" && *descr != "<f4" && *descr != "<i8" && *descr != "<i4") {
    return skip("지원하지 않는 dtype " + *descr);
  }
  const char kind = (*descr)[1];
  const size_t width = static_cast<size_t>((*descr)[2] - '0');
  if (fortran && shape->size() > 1) return skip("Fortran 순서 다차원 배열");

  const std::string_view data = bytes.substr(data_start);
  uint64_t count = 1;
  for (const int64_t n : *shape) {
    // 곱이 데이터 바이트 수를 넘으면 그 자리에서 멈춰 넘침을 막습니다.
    if (n != 0 && count > data.size() / static_cast<uint64_t>(n)) return fail(".npy 데이터가 shape보다 짧습니다");
    count *= static_cast<uint64_t>(n);
  }
  if (count * width != data.size()) return fail(".npy 데이터 길이가 shape와 다릅니다");
  NpyArray array;
  array.shape = *shape;
  array.values.reserve(count);
  for (uint64_t i = 0; i < count; ++i) array.values.push_back(ElementAt(data, i, kind, width));
  return array;
}

std::optional<NpzContents> ReadNpzBytes(std::string_view bytes, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<NpzContents> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  std::string why;
  const auto entries = CentralDirectory(bytes, &why);
  if (!entries) return fail(why);
  NpzContents contents;
  for (const Entry& e : *entries) {
    if ((e.flags & 1u) != 0) return fail("암호화된 항목은 읽지 않습니다: " + e.name);
    if (e.method != 0) {
      return fail("압축된 항목은 읽지 않습니다(방식 " + std::to_string(e.method) + "): " + e.name);
    }
    if (!Fits(bytes, e.local_offset, 30) || LittleEndian(bytes, e.local_offset, 4) != kLocalHeader) {
      return fail("항목 머리가 틀립니다: " + e.name);
    }
    // 지역 머리의 크기 칸은 데이터 기술자나 zip64에서 비어 있을 수 있어 중앙 디렉터리 값을 씁니다.
    const uint64_t start = e.local_offset + 30 + LittleEndian(bytes, e.local_offset + 26, 2) +
                           LittleEndian(bytes, e.local_offset + 28, 2);
    if (!Fits(bytes, start, e.size)) return fail("항목 데이터가 파일 밖으로 나갑니다: " + e.name);
    const std::string_view data = bytes.substr(start, e.size);
    if (Crc32(data) != e.crc) return fail("항목 CRC가 다릅니다: " + e.name);

    constexpr std::string_view kSuffix = ".npy";
    if (e.name.size() <= kSuffix.size() || e.name.compare(e.name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
      contents.skipped[e.name] = ".npy 항목이 아님";
      continue;
    }
    const std::string key = e.name.substr(0, e.name.size() - kSuffix.size());
    std::string skip_reason;
    auto array = ReadNpy(data, &skip_reason, &why);
    if (array) {
      contents.arrays[key] = std::move(*array);
    } else if (!skip_reason.empty()) {
      contents.skipped[key] = skip_reason;
    } else {
      return fail(e.name + ": " + why);
    }
  }
  return contents;
}

std::optional<NpzContents> ReadNpz(const std::string& path_utf8, std::string* error) {
  std::FILE* file = OpenFileUtf8(path_utf8, "rb");
  if (file == nullptr) {
    if (error != nullptr) *error = "파일을 열지 못했습니다: " + path_utf8;
    return std::nullopt;
  }
  std::string bytes;
  char buffer[1 << 16];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) bytes.append(buffer, n);
  std::fclose(file);
  return ReadNpzBytes(bytes, error);
}

}  // namespace hwlab::platform
