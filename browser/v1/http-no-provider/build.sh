#!/usr/bin/env bash
## BuildEnv: WASIXCC_TARGET=wasm32-wasix
## ProgramName: test
## Env: GREETING=hi
## ExpectedExitCode: 0
set -euo pipefail
wasm-as --all-features main.wat -o main
