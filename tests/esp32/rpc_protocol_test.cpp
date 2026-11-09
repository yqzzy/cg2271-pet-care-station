#include "rpc_protocol.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
const char kTopic[] = "v1/devices/me/rpc/request/00042";

RpcParseResult Parse(const char* json, RpcRequest* request) {
  return ParseRpcRequest(kTopic, reinterpret_cast<const uint8_t*>(json),
                         std::strlen(json), request);
}

void TestAliasesAndBooleanValues() {
  const char* get_requests[] = {"{\"method\":\"getValue\"}",
                                "{\"method\":\"getLight\",\"params\":null}"};
  for (const char* json : get_requests) {
    RpcRequest request = {};
    assert(Parse(json, &request) == kRpcValid);
    assert(std::strcmp(request.id, "00042") == 0);
    assert(!request.set);
  }
  RpcRequest request = {};
  assert(Parse("{\"method\":\"setValue\",\"params\":true}", &request) == kRpcValid);
  assert(request.set && request.value);
  assert(Parse("{\"method\":\"setLight\",\"params\":false}", &request) == kRpcValid);
  assert(request.set && !request.value);
}

void TestTopicsAreExactAndIdsFitUint32() {
  const char* valid_topics[] = {"v1/devices/me/rpc/request/0",
                                "v1/devices/me/rpc/request/4294967295",
                                "v1/devices/me/rpc/request/0000000001"};
  const uint8_t payload[] = "{\"method\":\"getValue\"}";
  for (const char* topic : valid_topics) {
    RpcRequest request = {};
    assert(ParseRpcRequest(topic, payload, sizeof(payload) - 1, &request) == kRpcValid);
    assert(std::strcmp(request.id, std::strrchr(topic, '/') + 1) == 0);
  }
  const char* invalid_topics[] = {
      "", "v1/devices/me/rpc/request/", "v1/devices/me/rpc/request/-1",
      "v1/devices/me/rpc/request/+1", "v1/devices/me/rpc/request/1x",
      "v1/devices/me/rpc/request/1/", "v1/devices/me/rpc/request/1/2",
      "v1/devices/me/rpc/request/4294967296", "v1/devices/me/rpc/request/00000000001",
      "v1/devices/me/rpc/request/ 1", "v1/devices/me/rpc/request/1 ",
      "prefix/v1/devices/me/rpc/request/1", "v1/devices/me/rpc/response/1"};
  for (const char* topic : invalid_topics) {
    RpcRequest request = {};
    std::strcpy(request.id, "old");
    assert(ParseRpcRequest(topic, payload, sizeof(payload) - 1, &request) == kRpcBadTopic);
    assert(request.id[0] == '\0');
    assert(!request.set && !request.value);
  }
  RpcRequest request = {};
  assert(ParseRpcRequest(nullptr, payload, sizeof(payload) - 1, &request) == kRpcBadTopic);
}

void TestBadPayloadsKeepIdAndNeverSet() {
  const char* invalid[] = {
      "", "{", "null", "true", "[]", "42", "\"getValue\"", "{}",
      "{\"method\":null}", "{\"method\":false}", "{\"method\":1}",
      "{\"method\":[]}", "{\"method\":{}}", "{\"method\":\"setValue\"}",
      "{\"method\":\"setValue\",\"params\":null}",
      "{\"method\":\"setValue\",\"params\":0}",
      "{\"method\":\"setValue\",\"params\":1}",
      "{\"method\":\"setValue\",\"params\":\"true\"}",
      "{\"method\":\"setLight\",\"params\":[]}",
      "{\"method\":\"setLight\",\"params\":{\"value\":true}}",
      "{\"method\":\"getValue\"}garbage", "{\"method\":\"getValue\"}{}",
      "{\"method\":\"getValue\"}true", "{\"method\":\"getValue\"}0",
      "{'method':'getValue'}", "{method:\"getValue\"}",
      "{\"method\":\"getValue\",\"extra\":01}",
      "{\"method\":\"getValue\",\"extra\":+1}",
      "{\"method\":\"getValue\",\"extra\":.1}",
      "{\"method\":\"getValue\",\"extra\":1.}",
      "{\"method\":\"getValue\",\"extra\":1e}",
      "{\"method\":\"getValue\",\"extra\":True}",
      "{\"method\":\"getValue\",}",
      "{\"method\":\"getValue\\u0000suffix\"}",
      "{\"method\":\"getValue\",\"extra\":\"bad\nstring\"}"};
  for (const char* json : invalid) {
    RpcRequest request = {};
    request.set = true;
    request.value = true;
    assert(Parse(json, &request) == kRpcInvalidPayload);
    assert(std::strcmp(request.id, "00042") == 0);
    assert(!request.set && !request.value);
  }
  RpcRequest request = {};
  assert(ParseRpcRequest(kTopic, nullptr, 1, &request) == kRpcInvalidPayload);
  assert(ParseRpcRequest(kTopic, nullptr, 0, &request) == kRpcInvalidPayload);
  assert(ParseRpcRequest(kTopic, reinterpret_cast<const uint8_t*>("{}"), 2,
                         nullptr) == kRpcInvalidPayload);
  const uint8_t embedded_nul[] = {'{', '}', '\0', ' '};
  assert(ParseRpcRequest(kTopic, embedded_nul, sizeof(embedded_nul), &request) ==
         kRpcInvalidPayload);
}

void TestUnsupportedMethodsAreDistinct() {
  const char* unsupported[] = {"{\"method\":\"toggle\",\"params\":true}",
                                "{\"method\":\"setvalue\",\"params\":true}",
                                "{\"method\":\"\"}"};
  for (const char* json : unsupported) {
    RpcRequest request = {};
    assert(Parse(json, &request) == kRpcUnsupportedMethod);
    assert(std::strcmp(request.id, "00042") == 0);
    assert(!request.set && !request.value);
  }
}

void TestBoundsAndExactLength() {
  RpcRequest request = {};
  std::string payload = "{\"method\":\"getValue\"}";
  payload.resize(256, ' ');
  assert(ParseRpcRequest(kTopic, reinterpret_cast<const uint8_t*>(payload.data()),
                         payload.size(), &request) == kRpcValid);
  payload.push_back(' ');
  assert(ParseRpcRequest(kTopic, reinterpret_cast<const uint8_t*>(payload.data()),
                         payload.size(), &request) == kRpcInvalidPayload);
  const uint8_t no_terminator[] = {'{', '"', 'm', 'e', 't', 'h', 'o', 'd', '"',
                                  ':', '"', 'g', 'e', 't', 'V', 'a', 'l', 'u',
                                  'e', '"', '}', 'x'};
  assert(ParseRpcRequest(kTopic, no_terminator, sizeof(no_terminator) - 1,
                         &request) == kRpcValid);
  assert(ParseRpcRequest(kTopic, no_terminator, sizeof(no_terminator),
                         &request) == kRpcInvalidPayload);
  assert(Parse(" \t\r\n{\"method\":\"getValue\"}\r\n\t ", &request) == kRpcValid);
  assert(Parse("{\"method\":\"getValue\",\"extra\":[[[0]]]}", &request) == kRpcValid);
  assert(Parse("{\"method\":\"getValue\",\"extra\":[[[[0]]]]}", &request) == kRpcInvalidPayload);
  assert(Parse("{\"method\":\"getValue\",\"extra\":{\"a\":{\"b\":{\"c\":0}}}}",
               &request) == kRpcValid);
  assert(Parse("{\"method\":\"getValue\",\"extra\":{\"a\":{\"b\":{\"c\":{\"d\":0}}}}}",
               &request) == kRpcInvalidPayload);
  assert(Parse("{\"method\":\"getValue\",\"extra\":[true,false,null,-0,1.25,1e+2,\"a\\\"b\\u0041\"]}",
               &request) == kRpcValid);
}
}  // namespace

int main() {
  TestAliasesAndBooleanValues();
  TestTopicsAreExactAndIdsFitUint32();
  TestBadPayloadsKeepIdAndNeverSet();
  TestUnsupportedMethodsAreDistinct();
  TestBoundsAndExactLength();
  std::puts("PASS: strict bounded RPC topics, methods, booleans, JSON and limits");
}
