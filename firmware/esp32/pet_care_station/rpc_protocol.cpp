#include "rpc_protocol.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {
const char kRequestPrefix[] = "v1/devices/me/rpc/request/";
const size_t kMaxPayloadBytes = 256;
const unsigned kMaxNesting = 4;

bool IsDigit(uint8_t c) { return c >= '0' && c <= '9'; }

bool ParseTopic(const char* topic, char* id) {
  if (topic == nullptr ||
      strncmp(topic, kRequestPrefix, sizeof(kRequestPrefix) - 1) != 0) {
    return false;
  }
  const char* suffix = topic + sizeof(kRequestPrefix) - 1;
  uint32_t value = 0;
  size_t count = 0;
  while (suffix[count] != '\0') {
    if (count == 10 || !IsDigit(static_cast<uint8_t>(suffix[count]))) {
      return false;
    }
    const uint32_t digit = static_cast<uint32_t>(suffix[count] - '0');
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
    ++count;
  }
  if (count == 0) return false;
  memcpy(id, suffix, count);
  id[count] = '\0';
  return true;
}

// ArduinoJson intentionally permits some non-JSON syntax (single quotes,
// unquoted keys and relaxed numbers). Validate strict JSON first. Both passes
// are bounded by the same small input and nesting limits.
class StrictJson {
 public:
  StrictJson(const uint8_t* data, size_t length)
      : data_(data), length_(length), position_(0) {}

  bool Valid() {
    if (!Value(0)) return false;
    Whitespace();
    return position_ == length_;
  }

 private:
  void Whitespace() {
    while (position_ < length_) {
      const uint8_t c = data_[position_];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      ++position_;
    }
  }

  bool Consume(uint8_t c) {
    if (position_ >= length_ || data_[position_] != c) return false;
    ++position_;
    return true;
  }

  bool String() {
    if (!Consume('"')) return false;
    while (position_ < length_) {
      const uint8_t c = data_[position_++];
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') continue;
      if (position_ == length_) return false;
      const uint8_t escape = data_[position_++];
      if (escape == 'u') {
        for (unsigned i = 0; i < 4; ++i) {
          if (position_ == length_) return false;
          const uint8_t digit = data_[position_++];
          if (!IsDigit(digit) && !(digit >= 'a' && digit <= 'f') &&
              !(digit >= 'A' && digit <= 'F')) return false;
        }
      } else if (escape != '"' && escape != '\\' && escape != '/' &&
                 escape != 'b' && escape != 'f' && escape != 'n' &&
                 escape != 'r' && escape != 't') {
        return false;
      }
    }
    return false;
  }

  bool Digits() {
    const size_t start = position_;
    while (position_ < length_ && IsDigit(data_[position_])) ++position_;
    return position_ != start;
  }

  bool Number() {
    Consume('-');
    if (!Consume('0') && !Digits()) return false;
    if (Consume('.') && !Digits()) return false;
    if (Consume('e') || Consume('E')) {
      if (!Consume('+')) Consume('-');
      if (!Digits()) return false;
    }
    return true;
  }

  bool Literal(const char* text) {
    while (*text != '\0') {
      if (!Consume(static_cast<uint8_t>(*text++))) return false;
    }
    return true;
  }

  bool Container(unsigned depth, bool object) {
    if (depth >= kMaxNesting) return false;
    ++position_;  // The opening brace/bracket was checked by Value().
    const uint8_t close = object ? '}' : ']';
    Whitespace();
    if (Consume(close)) return true;
    do {
      Whitespace();
      if (object) {
        if (!String()) return false;
        Whitespace();
        if (!Consume(':')) return false;
      }
      if (!Value(depth + 1)) return false;
      Whitespace();
      if (Consume(close)) return true;
    } while (Consume(','));
    return false;
  }

  bool Value(unsigned depth) {
    Whitespace();
    if (position_ == length_) return false;
    switch (data_[position_]) {
      case '{': return Container(depth, true);
      case '[': return Container(depth, false);
      case '"': return String();
      case 't': return Literal("true");
      case 'f': return Literal("false");
      case 'n': return Literal("null");
      default: return Number();
    }
  }

  const uint8_t* data_;
  const size_t length_;
  size_t position_;
};
}  // namespace

RpcParseResult ParseRpcRequest(const char* topic, const uint8_t* payload,
                               size_t length, RpcRequest* out) {
  if (out == nullptr) return kRpcInvalidPayload;
  memset(out, 0, sizeof(*out));
  if (!ParseTopic(topic, out->id)) return kRpcBadTopic;
  if (payload == nullptr || length == 0 || length > kMaxPayloadBytes ||
      memchr(payload, '\0', length) != nullptr ||
      !StrictJson(payload, length).Valid()) {
    return kRpcInvalidPayload;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, payload, length, DeserializationOption::NestingLimit(kMaxNesting));
  if (error || !document.is<JsonObject>()) return kRpcInvalidPayload;
  const JsonVariantConst method = document["method"];
  if (!method.is<const char*>()) return kRpcInvalidPayload;
  const JsonString name = method.as<JsonString>();
  // Comparing a C string alone could accept "getValue\u0000suffix".
  if (name.size() != strlen(name.c_str())) return kRpcInvalidPayload;
  if (strcmp(name.c_str(), "getValue") == 0 ||
      strcmp(name.c_str(), "getLight") == 0) return kRpcValid;
  if (strcmp(name.c_str(), "setValue") != 0 &&
      strcmp(name.c_str(), "setLight") != 0) return kRpcUnsupportedMethod;
  const JsonVariantConst params = document["params"];
  if (!params.is<bool>()) return kRpcInvalidPayload;
  out->set = true;
  out->value = params.as<bool>();
  return kRpcValid;
}
