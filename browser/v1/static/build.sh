#!/usr/bin/env bash
## BuildEnv: WASIXCC_TARGET=wasm32-wasix
## ProgramName: test
## Env: GREETING=hi
## ExpectedExitCode: 7
## ExpectedStdout: hello world
set -euo pipefail
wasm-as --all-features main.wat -o main
