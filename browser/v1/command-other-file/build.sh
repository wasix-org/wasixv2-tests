#!/usr/bin/env bash
## BuildEnv: WASIXCC_TARGET=wasm32-wasix
## MappedDirectory: .:/usr/bin
## CurrentDirectory: /usr/bin
## BrowserCommandDefaults: [{"name":"other","argv0":"shown","args":["--main"],"env":["A=default","B=default","ignored"]}]
## ExpectedStdout: show user
## ExpectedStdout: A=caller
set -euo pipefail
wasm-as --all-features main.wat -o main
wasm-as --all-features show.wat -o show
cp show other
chmod +x show other
