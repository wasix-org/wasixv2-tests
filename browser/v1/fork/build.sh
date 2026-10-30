#!/usr/bin/env bash
## BuildEnv: WASIXCC_TARGET=wasm32-wasix
## ProgramName: test
## Env: GREETING=hi
## ExpectedExitCode: 0
set -euo pipefail
wasm-as --all-features main.wat -o main
wasm-opt --all-features --asyncify --pass-arg=asyncify-imports@wasix_32v1.proc_fork main -o main.async
mv main.async main
