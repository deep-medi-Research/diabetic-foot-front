// platform 내부 헤더입니다. 관류 분석 프로그램 출력 줄, 요약 JSON, 매니페스트, 사이드카에서 값 몇 개만 꺼내는 JSON 읽기입니다.
#ifndef HWLAB_PLATFORM_SRC_JSON_READER_H_
#define HWLAB_PLATFORM_SRC_JSON_READER_H_

#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hwlab::platform {

inline void AppendCodePoint(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

/** JSON 스칼라 값의 종류입니다. */
enum class JsonKind : uint8_t {
  kString,
  kNumber,
  kTrue,
  kFalse,
  kNull,
  kNonFinite,  // 파이썬 json.dump가 쓰는 NaN, Infinity, -Infinity
};

/** JSON 스칼라 값 하나입니다. text는 문자열이면 이스케이프를 푼 UTF-8, 숫자이면 원문입니다. */
struct JsonScalar {
  JsonKind kind = JsonKind::kNull;
  std::string text;
};

// 값 몇 개만 꺼내므로, 값을 모두 담는 JSON 트리는 만들지 않습니다.
class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : text_(text) {}

  /** 키와 값(문자열은 이스케이프를 푼 것, 참거짓은 "true"/"false")을 읽습니다. */
  std::optional<std::map<std::string, std::string>> Read() {
    std::map<std::string, std::string> members;
    SkipSpace();
    if (!Take('{')) return std::nullopt;
    SkipSpace();
    if (Take('}')) return Finish(members);
    for (;;) {
      SkipSpace();
      std::string key;
      if (!ReadString(&key)) return std::nullopt;
      SkipSpace();
      if (!Take(':')) return std::nullopt;
      SkipSpace();
      std::string value;
      if (!ReadScalar(&value, nullptr)) return std::nullopt;
      members[key] = value;
      SkipSpace();
      if (Take(',')) continue;
      if (Take('}')) return Finish(members);
      return std::nullopt;
    }
  }

  /** 중첩된 JSON 문서 전체를 읽으며 키 경로가 가리키는 문자열 값을 찾습니다. */
  bool FindString(const std::vector<std::string>& path, std::string* out) {
    const auto found = FindScalar(path);
    if (!found || found->kind != JsonKind::kString) return false;
    *out = found->text;
    return true;
  }

  /** 문서 전체를 읽으며 키 경로가 가리키는 스칼라 값을 찾습니다. 문법이 틀리거나 경로가 배열, 객체이면 nullopt입니다. */
  std::optional<JsonScalar> FindScalar(const std::vector<std::string>& path) {
    Found found;
    if (!Walk(path, 0, true, &found, 0) || !AtEnd() || !found.scalar) return std::nullopt;
    return found.scalar;
  }

  /** 문서 전체를 읽으며 키 경로가 가리키는 배열의 스칼라 원소를 찾습니다. 원소에 배열이나 객체가 있으면 nullopt입니다. */
  std::optional<std::vector<JsonScalar>> FindArray(const std::vector<std::string>& path) {
    Found found;
    if (!Walk(path, 0, true, &found, 0) || !AtEnd() || !found.array) return std::nullopt;
    return found.array;
  }

 private:
  struct Found {
    std::optional<JsonScalar> scalar;
    std::optional<std::vector<JsonScalar>> array;
  };

  bool AtEnd() {
    SkipSpace();
    return pos_ == text_.size();
  }

  bool Walk(const std::vector<std::string>& path, size_t level, bool matching, Found* found, int depth) {
    if (depth > 64) return false;
    SkipSpace();
    if (pos_ >= text_.size()) return false;
    const bool target = matching && level == path.size();
    if (Take('{')) {
      SkipSpace();
      if (Take('}')) return true;
      for (;;) {
        SkipSpace();
        std::string key;
        if (!ReadString(&key)) return false;
        SkipSpace();
        if (!Take(':')) return false;
        const bool next = matching && level < path.size() && key == path[level];
        if (!Walk(path, level + 1, next, found, depth + 1)) return false;
        SkipSpace();
        if (Take(',')) continue;
        return Take('}');
      }
    }
    if (Take('[')) {
      // 찾는 배열이면 원소가 모두 스칼라일 때만 담습니다. 경로의 다음 이름이 "0", "1" 같은 번호이면 그 번호의 원소로
      // 들어갑니다(객체 배열의 값 하나를 꺼낼 때).
      std::vector<JsonScalar> items;
      bool flat = true;
      size_t index = 0;
      SkipSpace();
      if (!Take(']')) {
        for (;; ++index) {
          SkipSpace();
          const bool element = matching && level < path.size() && path[level] == std::to_string(index);
          if (pos_ < text_.size() && (text_[pos_] == '{' || text_[pos_] == '[')) {
            flat = false;
            if (!Walk(path, level + 1, element, found, depth + 1)) return false;
          } else {
            JsonScalar item;
            if (!ReadScalar(&item.text, &item.kind)) return false;
            if (element && level + 1 == path.size()) found->scalar = item;
            if (target) items.push_back(std::move(item));
          }
          SkipSpace();
          if (Take(',')) continue;
          if (!Take(']')) return false;
          break;
        }
      }
      if (target && flat) found->array = std::move(items);
      return true;
    }
    JsonScalar value;
    if (!ReadScalar(&value.text, &value.kind)) return false;
    if (target) found->scalar = std::move(value);
    return true;
  }

  std::optional<std::map<std::string, std::string>> Finish(std::map<std::string, std::string>& members) {
    SkipSpace();
    if (pos_ != text_.size()) return std::nullopt;
    return members;
  }

  void SkipSpace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Take(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool TakeWord(const char* word) {
    const std::string w(word);
    if (text_.compare(pos_, w.size(), w) != 0) return false;
    pos_ += w.size();
    return true;
  }

  bool ReadHex4(uint32_t* value) {
    if (pos_ + 4 > text_.size()) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    *value = v;
    return true;
  }

  bool ReadString(std::string* out) {
    if (!Take('"')) return false;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out->push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) return false;
      const char e = text_[pos_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out->push_back(e);
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'u': {
          uint32_t cp = 0;
          if (!ReadHex4(&cp)) return false;
          // ensure_ascii로 찍힌 BMP 밖 글자는 대리쌍 두 개로 옵니다.
          if (cp >= 0xD800 && cp <= 0xDBFF && TakeWord("\\u")) {
            uint32_t low = 0;
            if (!ReadHex4(&low) || low < 0xDC00 || low > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          AppendCodePoint(*out, cp);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool ReadScalar(std::string* out, JsonKind* kind) {
    JsonKind k = JsonKind::kNumber;
    if (pos_ < text_.size() && text_[pos_] == '"') {
      k = JsonKind::kString;
      if (!ReadString(out)) return false;
    } else if (TakeWord("true")) {
      k = JsonKind::kTrue;
      *out = "true";
    } else if (TakeWord("false")) {
      k = JsonKind::kFalse;
      *out = "false";
    } else if (TakeWord("NaN") || TakeWord("Infinity") || TakeWord("-Infinity")) {
      // 파이썬 json.dump는 NaN과 무한대를 이 낱말로 씁니다(요약 JSON에 나옴).
      k = JsonKind::kNonFinite;
      out->clear();
    } else if (TakeWord("null")) {
      k = JsonKind::kNull;
      out->clear();
    } else {
      const size_t begin = pos_;
      while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '-' ||
                                     text_[pos_] == '+' || text_[pos_] == '.' || text_[pos_] == 'e' ||
                                     text_[pos_] == 'E')) {
        ++pos_;
      }
      if (pos_ == begin) return false;
      *out = text_.substr(begin, pos_ - begin);
    }
    if (kind != nullptr) *kind = k;
    return true;
  }

  const std::string& text_;
  size_t pos_ = 0;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_SRC_JSON_READER_H_
