FROM scratch
COPY plugin.wasm ./

# curl -X POST http://localhost:15000/logging?wasm=debug
#