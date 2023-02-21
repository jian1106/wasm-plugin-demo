#include <string>
#include <string_view>
#include <unordered_map>

#include "proxy_wasm_intrinsics.h"

using namespace std;

class ExampleRootContext : public RootContext {
public:
  explicit ExampleRootContext(uint32_t id, string_view root_id) : RootContext(id, root_id) {}

  bool onConfigure(size_t) override;
};

class ExampleContext : public Context {
public:
  explicit ExampleContext(uint32_t id, RootContext *root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t headers) override;
  FilterHeadersStatus onResponseHeaders(uint32_t headers) override;
};

static RegisterContextFactory register_ExampleContext(CONTEXT_FACTORY(ExampleContext), ROOT_FACTORY(ExampleRootContext));

bool ExampleRootContext::onConfigure(size_t) {
  LOG_INFO("onConfigure");
  return true;
}

FilterHeadersStatus ExampleContext::onRequestHeaders(uint32_t) {
  addResponseHeader("X-Wasm-Request", "WasmRequestMessage");
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus ExampleContext::onResponseHeaders(uint32_t) {
  addResponseHeader("X-Wasm-Response", "WasmResponseMessage");
  return FilterHeadersStatus::Continue;
}
