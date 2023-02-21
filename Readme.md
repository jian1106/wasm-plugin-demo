 * system: macOS 13.2 (Intel Chip)
 * kubernetes cluster: docker-desktop 4.16.2 with kubernetes
    * kubectl client version: 1.25.4
    * kubectl server version: 1.24.2
 * istioctl version: 1.15.5
 * pilot image: istio/pilot:1.15.5
 * proxy image: istio/proxyv2:1.15.5
 * envoy version: aed172a7bd0b71db7121840c84b97855fb418381/1.23.4-dev/Clean/RELEASE/BoringSSL (run `envoy --version` inside `istio-proxy` container)

### Build istio/envoy c++ wasm sdk
1. clone [istio/envoy](https://github.com/istio/envoy) from github and checkout branch release-1.15
```bash
git clone git@github.com:istio/envoy.git
cd envoy
git checkout release-1.15
```
2. build wasm c++ sdk as a docker image
```bash
cd api/wasm/cpp/
docker build -t wasmsdk:v2-1.15 -f Dockerfile-sdk .
```

### Build my c++ wasm plugin
1. create workspace folder `wasm-plugin-demo`
```bash
mkdir wasm-plugin-demo
cd wasm-plugin-demo
```
2. create file `plugin.cc`
```cpp
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
   ```
2. create a `makefile` and make wasm  
```makefile
DOCKER_SDK=/sdk
all: myproject.wasm
include ${DOCKER_SDK}/Makefile.base_lite
```
   
3. Build wasm binary file
```bash
docker run --rm -v $PWD:/work -w /work wasmsdk:v2-1.15 /build_wasm.sh
```
4. Build wasm docker image
```dockerfile
FROM scratch
COPY plugin.wasm ./
```
```bash
docker build -t jian1106/wasm-demo:0.0.1 .
docker push jian1106/wasm-demo:0.0.1
```


### Build customized envoy image and run a container for test
1. Build image from istio/proxyv2:1.15.5  
create and build Dockerfile-istio-proxy-it
```dockerfile
FROM istio/proxyv2:1.15.5
ENTRYPOINT ["/bin/bash"]
```
```bash
docker build -t istio/proxyv2:1.15.5-it -f Dockerfile-istio-proxy-it .
```

2. Run a container with istio/proxyv2:1.15.5-it
```
docker run --rm -it -p 10000:10000 -v $PWD:/work -w /work istio/proxyv2:1.15.5-it
```

### Test wasm binary file inside the container with static configuration  
1. Download `envoy-demo.yaml`
```bash
curl -LO https://www.envoyproxy.io/docs/envoy/latest/_downloads/92dcb9714fb6bc288d042029b34c0de4/envoy-demo.yaml
```
2. Add wasm configuration
```yaml
static_resources:
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 10000
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains: ["*"]
              routes:
              - match:
                  prefix: "/"
                route:
                  host_rewrite_literal: www.envoyproxy.io
                  cluster: service_envoyproxy_io
          access_log:
          - name: envoy.access_loggers.stdout
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.access_loggers.stream.v3.StdoutAccessLog
          http_filters:
          - name: envoy.filters.http.wasm
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.wasm.v3.Wasm
              config:
                vm_config:
                  runtime: "envoy.wasm.runtime.v8"
                  code:
                    local:
                      filename: "/work/plugin.wasm"
          - name: envoy.filters.http.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

  clusters:
  - name: service_envoyproxy_io
    type: LOGICAL_DNS
    # Comment out the following line to test on v6 networks
    dns_lookup_family: V4_ONLY
    load_assignment:
      cluster_name: service_envoyproxy_io
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: www.envoyproxy.io
                port_value: 443
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        sni: www.envoyproxy.io

admin:
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 9902                                
```

3. Start envoy
```bash
envoy -c envoy-demo.yaml
```

4. Check request/response headers  
call `localhost:10000` from host, `x-wasm-response: WasmResponseMessage` is in the response headers.
```bash
curl -I localhost:10000
```
```
HTTP/1.1 200 OK
age: 57303
cache-control: public, max-age=0, must-revalidate
content-length: 17304
content-security-policy: frame-ancestors 'self';
content-type: text/html; charset=UTF-8
date: Mon, 20 Feb 2023 12:46:02 GMT
etag: "e47b4cc2d8b8a71bffe7be2b5bff92bf-ssl"
server: envoy
strict-transport-security: max-age=31536000
x-nf-request-id: 01GSS4CW92CNPYHBPPB0RS4PGY
x-envoy-upstream-service-time: 276
x-wasm-response: WasmResponseMessage
```


### Integrate with istio by WasmPlugin
1. Download istio 1.15.5
```bash
curl -L https://istio.io/downloadIstio | ISTIO_VERSION=1.15.5 TARGET_ARCH=x86_64 sh -
```
2. Install istio and enable auto inject in default namespace
```bash
istioctl install --set profile=demo -y
kubectl label namespace default istio-injection=enabled
```
3. Deploy demo apps
Download demo yaml and apply
```bash
curl -LO https://raw.githubusercontent.com/istio/istio/release-1.17/samples/bookinfo/platform/kube/bookinfo.yaml
kubectl apply -f bookinfo.yaml
```
4. Inject WasmPlugin  
create and apply `wasm.yaml`
```yaml
apiVersion: extensions.istio.io/v1alpha1
kind: WasmPlugin
metadata:
  name: example
  namespace: default
spec:
  selector:
    matchLabels:
      app: productpage
  url: jian1106/wasm-demo:0.0.1
  imagePullPolicy: IfNotPresent
```
```bash
kubectl apply -f wasm.yaml
```

5. istio-proxy is crashed  
inside istio-proxy container of Pod productpage-v1-xxxxxxxxx-yyyyy  
```
2023-02-21T04:22:09.098282Z	info	wasm	fetching image jian1106/wasm-demo from registry index.docker.io with tag 0.0.1
2023-02-21T04:22:11.821890Z	critical	envoy backtrace	Caught Segmentation fault, suspect faulting address 0x7f55ace41000
2023-02-21T04:22:11.821931Z	critical	envoy backtrace	Backtrace (use tools/stack_decode.py to get line numbers):
2023-02-21T04:22:11.821935Z	critical	envoy backtrace	Envoy version: aed172a7bd0b71db7121840c84b97855fb418381/1.23.4-dev/Clean/RELEASE/BoringSSL
2023-02-21T04:22:11.828385Z	info	ads	ADS: "@" productpage-v1-66756cddfd-whs2c.default-1 terminated
2023-02-21T04:22:11.828409Z	error	Envoy exited with error: signal: segmentation fault
2023-02-21T04:22:11.828437Z	info	ads	ADS: "@" productpage-v1-66756cddfd-whs2c.default-2 terminated
```