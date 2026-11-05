#include "hwlab/platform/npy_archive.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using hwlab::platform::NpzContents;
using hwlab::platform::ReadNpy;
using hwlab::platform::ReadNpz;
using hwlab::platform::ReadNpzBytes;

namespace {

void PutLe(std::string& out, uint64_t value, int width) {
  for (int i = 0; i < width; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
}

// np.save와 같은 배치로 머리를 64바이트 경계까지 공백으로 채우고 줄바꿈으로 끝냅니다.
std::string Npy(const std::string& descr, const std::string& shape, const std::string& data, int major = 1,
                bool fortran = false) {
  std::string header = "{'descr': '" + descr + "', 'fortran_order': " + (fortran ? "True" : "False") +
                       ", 'shape': " + shape + ", }";
  const size_t prefix = major == 1 ? 10 : 12;
  while ((prefix + header.size() + 1) % 64 != 0) header.push_back(' ');
  header.push_back('\n');
  std::string out("\x93NUMPY", 6);
  out.push_back(static_cast<char>(major));
  out.push_back('\0');
  PutLe(out, header.size(), major == 1 ? 2 : 4);
  return out + header + data;
}

std::string F8(const std::vector<double>& values) {
  std::string out;
  for (const double v : values) {
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    PutLe(out, bits, 8);
  }
  return out;
}

uint32_t Crc32(const std::string& bytes) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const char b : bytes) {
    crc ^= static_cast<unsigned char>(b);
    for (int k = 0; k < 8; ++k) crc = (crc & 1u) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return crc ^ 0xFFFFFFFFu;
}

struct ZipItem {
  std::string name;
  std::string data;
  uint16_t method = 0;
  // 지역 머리의 CRC와 크기를 0으로 두고 데이터 뒤에 데이터 기술자를 붙입니다(스트림으로 쓴 ZIP).
  bool descriptor = false;
  // 중앙 디렉터리의 크기와 위치를 0xFFFFFFFF로 두고 zip64 추가 필드에 적습니다.
  bool zip64 = false;
};

std::string Zip(const std::vector<ZipItem>& items, bool zip64_end = false) {
  std::string out;
  std::string central;
  for (const ZipItem& item : items) {
    const uint64_t offset = out.size();
    const uint32_t crc = Crc32(item.data);
    const uint16_t flags = item.descriptor ? 0x0008 : 0;
    PutLe(out, 0x04034b50, 4);
    PutLe(out, 20, 2);
    PutLe(out, flags, 2);
    PutLe(out, item.method, 2);
    PutLe(out, 0, 4);  // 시각, 날짜
    PutLe(out, item.descriptor ? 0 : crc, 4);
    PutLe(out, item.descriptor ? 0 : item.data.size(), 4);
    PutLe(out, item.descriptor ? 0 : item.data.size(), 4);
    PutLe(out, item.name.size(), 2);
    PutLe(out, 0, 2);
    out += item.name + item.data;
    if (item.descriptor) {
      PutLe(out, 0x08074b50, 4);
      PutLe(out, crc, 4);
      PutLe(out, item.data.size(), 4);
      PutLe(out, item.data.size(), 4);
    }
    std::string extra;
    if (item.zip64) {
      PutLe(extra, 0x0001, 2);
      PutLe(extra, 24, 2);
      PutLe(extra, item.data.size(), 8);
      PutLe(extra, item.data.size(), 8);
      PutLe(extra, offset, 8);
    }
    const uint64_t small = item.zip64 ? 0xFFFFFFFFu : 0;
    PutLe(central, 0x02014b50, 4);
    PutLe(central, 45, 2);
    PutLe(central, 45, 2);
    PutLe(central, flags, 2);
    PutLe(central, item.method, 2);
    PutLe(central, 0, 4);
    PutLe(central, crc, 4);
    PutLe(central, item.zip64 ? small : item.data.size(), 4);
    PutLe(central, item.zip64 ? small : item.data.size(), 4);
    PutLe(central, item.name.size(), 2);
    PutLe(central, extra.size(), 2);
    PutLe(central, 0, 2);   // 주석
    PutLe(central, 0, 2);   // 디스크
    PutLe(central, 0, 2);   // 내부 속성
    PutLe(central, 0, 4);   // 외부 속성
    PutLe(central, item.zip64 ? small : offset, 4);
    central += item.name + extra;
  }
  const uint64_t dir_offset = out.size();
  out += central;
  if (zip64_end) {
    const uint64_t record = out.size();
    PutLe(out, 0x06064b50, 4);
    PutLe(out, 44, 8);
    PutLe(out, 45, 2);
    PutLe(out, 45, 2);
    PutLe(out, 0, 4);
    PutLe(out, 0, 4);
    PutLe(out, items.size(), 8);
    PutLe(out, items.size(), 8);
    PutLe(out, central.size(), 8);
    PutLe(out, dir_offset, 8);
    PutLe(out, 0x07064b50, 4);
    PutLe(out, 0, 4);
    PutLe(out, record, 8);
    PutLe(out, 1, 4);
  }
  PutLe(out, 0x06054b50, 4);
  PutLe(out, 0, 2);
  PutLe(out, 0, 2);
  PutLe(out, zip64_end ? 0xFFFF : items.size(), 2);
  PutLe(out, zip64_end ? 0xFFFF : items.size(), 2);
  PutLe(out, zip64_end ? 0xFFFFFFFFu : central.size(), 4);
  PutLe(out, zip64_end ? 0xFFFFFFFFu : dir_offset, 4);
  PutLe(out, 0, 2);
  return out;
}

TEST(ReadNpy, ReadsFloat64Vector) {
  std::string skip, error;
  const auto array = ReadNpy(Npy("<f8", "(3,)", F8({1.5, -2.0, 7.25})), &skip, &error);
  ASSERT_TRUE(array) << error;
  EXPECT_EQ(array->shape, (std::vector<int64_t>{3}));
  EXPECT_EQ(array->values, (std::vector<double>{1.5, -2.0, 7.25}));
}

TEST(ReadNpy, ReadsZeroDimensionalScalar) {
  const auto array = ReadNpy(Npy("<f8", "()", F8({30.0})), nullptr);
  ASSERT_TRUE(array);
  EXPECT_TRUE(array->shape.empty());
  EXPECT_EQ(array->values, (std::vector<double>{30.0}));
}

TEST(ReadNpy, ReadsMultiDimensionalInCOrder) {
  const auto array = ReadNpy(Npy("<f8", "(2, 3)", F8({0, 1, 2, 10, 11, 12})), nullptr);
  ASSERT_TRUE(array);
  EXPECT_EQ(array->shape, (std::vector<int64_t>{2, 3}));
  EXPECT_EQ(array->values[4], 11.0);
}

TEST(ReadNpy, ConvertsIntegersAndFloat32) {
  std::string i8;
  PutLe(i8, static_cast<uint64_t>(int64_t{-3}), 8);
  PutLe(i8, 6, 8);
  auto array = ReadNpy(Npy("<i8", "(2,)", i8), nullptr);
  ASSERT_TRUE(array);
  EXPECT_EQ(array->values, (std::vector<double>{-3.0, 6.0}));

  std::string i4;
  PutLe(i4, static_cast<uint32_t>(int32_t{-7}), 4);
  array = ReadNpy(Npy("<i4", "(1,)", i4), nullptr);
  ASSERT_TRUE(array);
  EXPECT_EQ(array->values, (std::vector<double>{-7.0}));

  const float f = 0.5f;
  uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(bits));
  std::string f4;
  PutLe(f4, bits, 4);
  array = ReadNpy(Npy("<f4", "(1,)", f4), nullptr);
  ASSERT_TRUE(array);
  EXPECT_EQ(array->values, (std::vector<double>{0.5}));
}

TEST(ReadNpy, ReadsHeaderVersion2And3) {
  for (const int major : {2, 3}) {
    const auto array = ReadNpy(Npy("<f8", "(2,)", F8({4.0, 5.0}), major), nullptr);
    ASSERT_TRUE(array) << major;
    EXPECT_EQ(array->values, (std::vector<double>{4.0, 5.0}));
  }
}

TEST(ReadNpy, SkipsUnicodeAndFortranArrays) {
  std::string skip, error;
  EXPECT_FALSE(ReadNpy(Npy("<U4", "()", std::string(16, '\0')), &skip, &error));
  EXPECT_NE(skip.find("<U4"), std::string::npos);
  EXPECT_TRUE(error.empty());

  skip.clear();
  EXPECT_FALSE(ReadNpy(Npy("<f8", "(2, 2)", F8({1, 2, 3, 4}), 1, true), &skip, &error));
  EXPECT_FALSE(skip.empty());
  EXPECT_TRUE(error.empty());
}

TEST(ReadNpy, RejectsBadBytes) {
  std::string skip, error;
  EXPECT_FALSE(ReadNpy("not numpy", &skip, &error));
  EXPECT_FALSE(error.empty());
  error.clear();
  // shape보다 데이터가 짧습니다.
  EXPECT_FALSE(ReadNpy(Npy("<f8", "(4,)", F8({1, 2})), &skip, &error));
  EXPECT_FALSE(error.empty());
  error.clear();
  // 곱이 넘치는 shape도 길이 검사에서 거부합니다.
  EXPECT_FALSE(ReadNpy(Npy("<f8", "(4294967296, 4294967296, 16)", F8({1})), &skip, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_TRUE(skip.empty());
}

TEST(ReadNpzBytes, ReadsStoredEntriesAndListsSkipped) {
  const std::string zip = Zip({{"time_s.npy", Npy("<f8", "(2,)", F8({5.0, 7.0}))},
                               {"sides.npy", Npy("<U5", "(2,)", std::string(40, '\0'))},
                               {"notes.txt", "hello"}});
  std::string error;
  const auto contents = ReadNpzBytes(zip, &error);
  ASSERT_TRUE(contents) << error;
  ASSERT_EQ(contents->arrays.count("time_s"), 1u);
  EXPECT_EQ(contents->arrays.at("time_s").values, (std::vector<double>{5.0, 7.0}));
  EXPECT_EQ(contents->skipped.count("sides"), 1u);
  EXPECT_EQ(contents->skipped.count("notes.txt"), 1u);
}

TEST(ReadNpzBytes, ReadsDataDescriptorAndZip64) {
  ZipItem item{"a.npy", Npy("<f8", "(1,)", F8({3.0}))};
  item.descriptor = true;
  item.zip64 = true;
  ZipItem plain{"b.npy", Npy("<f8", "()", F8({9.0}))};
  std::string error;
  const auto contents = ReadNpzBytes(Zip({item, plain}, true), &error);
  ASSERT_TRUE(contents) << error;
  EXPECT_EQ(contents->arrays.at("a").values, (std::vector<double>{3.0}));
  EXPECT_EQ(contents->arrays.at("b").values, (std::vector<double>{9.0}));
}

TEST(ReadNpzBytes, RejectsDeflatedEntry) {
  ZipItem item{"a.npy", Npy("<f8", "(1,)", F8({3.0}))};
  item.method = 8;
  std::string error;
  EXPECT_FALSE(ReadNpzBytes(Zip({item}), &error));
  EXPECT_NE(error.find("압축"), std::string::npos) << error;
}

TEST(ReadNpzBytes, RejectsCorruptArchive) {
  std::string zip = Zip({{"a.npy", Npy("<f8", "(1,)", F8({3.0}))}});
  std::string error;
  EXPECT_FALSE(ReadNpzBytes(zip.substr(0, zip.size() / 2), &error));
  EXPECT_FALSE(error.empty());

  // 데이터 한 바이트를 바꾸면 CRC가 맞지 않습니다.
  zip[zip.find("\x93NUMPY") + 130] ^= 1;
  error.clear();
  EXPECT_FALSE(ReadNpzBytes(zip, &error));
  EXPECT_NE(error.find("CRC"), std::string::npos) << error;
  EXPECT_FALSE(ReadNpzBytes("", &error));
}

TEST(ReadNpz, ReadsAnalyzerOutputFile) {
  std::string error;
  const auto contents = ReadNpz(std::string(HWLAB_TEST_DATA_DIR) + "/154007_perfusion.npz", &error);
  ASSERT_TRUE(contents) << error;
  EXPECT_EQ(contents->arrays.size(), 25u);
  EXPECT_EQ(contents->arrays.at("maps").shape, (std::vector<int64_t>{2, 11, 32, 16}));
  EXPECT_EQ(contents->arrays.at("n_light_ref").values, std::vector<double>(11, 6.0));
  EXPECT_EQ(contents->arrays.at("fps").values, (std::vector<double>{30.0}));
  EXPECT_EQ(contents->skipped.size(), 2u);
  EXPECT_EQ(contents->skipped.count("sides"), 1u);
  EXPECT_EQ(contents->skipped.count("cuff_side"), 1u);
  EXPECT_FALSE(ReadNpz(std::string(HWLAB_TEST_DATA_DIR) + "/없는_파일.npz", &error));
}

}  // namespace
