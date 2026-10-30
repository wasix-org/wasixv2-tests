#!/usr/bin/env bash
## BuildEnv: WASIXCC_TARGET=wasm32-wasix
## ProgramName: test
## Env: GREETING=hi
## ExpectedExitCode: 0
## ExpectedStdout: child
## MappedDirectory: .:/prog
set -euo pipefail
wasm-as --all-features main.wat -o main
wasm-as --all-features child.wat -o child.wasm
chmod +x child.wasm
