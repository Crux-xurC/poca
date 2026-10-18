# Poca

Poca is the CEF shell for local applications. This initial launcher starts Tezcatl, waits for its page server, then opens the page in CEF. Authentication and other security controls are not implemented here.

## Build the CEF sample

Download and extract the CEF binary distribution `cef_binary_152.0.14+g8a0b5b9+chromium-152.0.7977.167_linux64` into `vendor/` (the SDK is intentionally not tracked in Git because it expands to several gigabytes). The distribution includes the `cefsimple` sample and its CMake configuration. Configure/build it (CMake 3.21+, C++20 toolchain, and the Linux development libraries required by CEF):

```sh
CEF=vendor/cef_binary_152.0.14+g8a0b5b9+chromium-152.0.7977.167_linux64
cmake -S "$CEF" -B build -DUSE_SANDBOX=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cefsimple -j
```

The resulting executable is `build/Release/cefsimple`. Then run `./run`. The launcher invokes `nix develop ~/my_app/tezcatl --command ~/my_app/tezcatl/nix/t -d --no-browser`, waits up to 20 seconds for `http://127.0.0.1:4001/main.html`, and launches CEF with Alloy style. Override `TEZCATL_DIR`, `POCA_CEF_APP`, or `POCA_URL` for local development.

The local CEF SDK under `vendor/` and generated output under `build/` are ignored by Git. Review the SDK `LICENSE.txt` and redistribution terms before publishing builds.
