#!/usr/bin/env bash
## BuildEnv: WASIXCC_TARGET=wasm32-wasix
## ExpectedExitCode: 132
set -euo pipefail
wasm-as --all-features main.wat -o main
