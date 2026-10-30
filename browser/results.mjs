import assert from "node:assert/strict";

const MAX_STREAM_BYTES = 8 * 1024 * 1024;
export function bytes(value, label) {
    assert(Array.isArray(value) && value.length <= MAX_STREAM_BYTES, `${label}: expected a bounded byte array`);
    assert(value.every((byte) => Number.isInteger(byte) && byte >= 0 && byte <= 255), `${label}: invalid byte`);
    return Buffer.from(value);
}

function knownKeys(value, keys, label) {
    assert(value && typeof value === "object" && !Array.isArray(value), `${label}: expected object`);
    for (const key of Object.keys(value)) assert(keys.includes(key), `${label}: unknown metadata ${key}`);
}

export function validateManifest(manifest) {
    knownKeys(manifest, ["version", "lane", "tests"], "manifest");
    assert.equal(manifest.version, 1, "unknown manifest version");
    assert.equal(manifest.lane, "sab-isolated-no-jspi", "unknown required lane");
    assert(Array.isArray(manifest.tests) && manifest.tests.length > 0, "empty test selection");
    const names = new Set();
    for (const test of manifest.tests) {
        knownKeys(test, ["name", "status", "assets", "module", "timeoutMs", "args", "env", "stdin", "cwd", "user", "programName", "buildEnv", "mounts", "expected", "error", "transportOnly", "console", "http", "commandDefaults", "acceptance", "network", "relayUrl"], "test");
        for (const key of ["console", "http"]) if (key in test) assert.equal(typeof test[key], "boolean", key + ": expected boolean");
        if (test.acceptance != null) {
            knownKeys(test.acceptance, ["packageVersion", "command", "actions", "liveVfs"], "acceptance");
            if ("packageVersion" in test.acceptance) assert([2, 3].includes(test.acceptance.packageVersion), "invalid package version");
            if ("command" in test.acceptance) assert(typeof test.acceptance.command === "string", "invalid package command");
            if ("liveVfs" in test.acceptance) assert.equal(typeof test.acceptance.liveVfs, "boolean", "invalid liveVfs");
            if ("actions" in test.acceptance) {
                assert(Array.isArray(test.acceptance.actions), "actions: expected array");
                for (const action of test.acceptance.actions) {
                    knownKeys(action, ["after", "input", "resize", "hangup", "edit", "closeInput"], "acceptance action");
                    assert(typeof action.after === "string" && action.after.length > 0, "action needs output marker");
                    for (const key of ["hangup", "edit", "closeInput"]) if (key in action) assert.equal(typeof action[key], "boolean", "invalid action " + key);
                    if ("input" in action) bytes(action.input, "action input");
                    if ("resize" in action) assert(Array.isArray(action.resize) && action.resize.length === 4 && action.resize.every(n => Number.isInteger(n) && n >= 0 && n <= 65535), "invalid resize");
                }
            }
        }
        if (test.network != null) assert(["classic","fidelity","absent","refused","disconnect","overflow","ingress","peer"].includes(test.network), "unknown network profile");
        if ("relayUrl" in test) assert(typeof test.relayUrl === "string" && /^ws:\/\/127\.0\.0\.1:[0-9]+$/.test(test.relayUrl), "invalid local relay URL");
        if ("commandDefaults" in test) {
            assert(Array.isArray(test.commandDefaults), "commandDefaults: expected array");
            for (const command of test.commandDefaults) {
                knownKeys(command, ["name", "argv0", "args", "env"], "command defaults");
                assert(typeof command.name === "string" && command.name.length, "missing command name");
                assert(command.argv0 === null || typeof command.argv0 === "string", "invalid command argv0");
                for (const key of ["args", "env"]) assert(Array.isArray(command[key]) && command[key].every(value=>typeof value === "string"), "invalid command " + key);
            }
        }
        knownKeys(test.expected, ["exitCode", "mustFail", "stdout", "stderr", "files"], "expectations");
        assert(typeof test.name === "string" && test.name.length > 0 && !names.has(test.name), "missing/duplicate test name");
        names.add(test.name);
        assert(["ready", "export-failed"].includes(test.status), "unknown export status");
        assert(Number.isSafeInteger(test.timeoutMs) && test.timeoutMs > 0, "invalid per-test timeout");
        assert(/^(?:[a-zA-Z0-9_-]+\/)*[a-zA-Z0-9_-]+$/.test(test.assets), "invalid assets path");
        assert.equal(test.module, "main.wasm", "unknown module path");
        assert(test.expected && Array.isArray(test.expected.stdout) && Array.isArray(test.expected.stderr) && Array.isArray(test.expected.files), "missing expectations");
        assert(Number.isInteger(test.expected.exitCode) && typeof test.expected.mustFail === "boolean", "invalid exit expectation");
    }
    return manifest;
}

export function checkResult(test, result) {
    assert(result && Number.isInteger(result.exitCode), "runtime did not return an exit code");
    assert(!result.error && !result.truncated, "runtime error or truncated output: " + result.error);
    // Linux kernel/exit.c:do_exit carries the status; fs/exec.c:begin_new_exec replaces the
    // image. The adapter must report the final process status, never worker completion.
    if (test.expected.mustFail) assert.notEqual(result.exitCode, 0, "expected nonzero exit");
    else assert.equal(result.exitCode, test.expected.exitCode, "exit status mismatch");
    const stdout = bytes(result.stdout, "stdout");
    const stderr = bytes(result.stderr, "stderr");
    // Match run.rs:check: no output assertion when unspecified, otherwise trimmed lines.
    for (const [stream, data] of [["stdout", stdout], ["stderr", stderr]]) {
        if (test.expected[stream].length) {
            const text = data.toString("utf8").trim();
            assert.deepEqual(text ? text.split(/\r?\n/) : [], test.expected[stream], stream + " mismatch");
        }
    }
    assert(Array.isArray(result.files), "runtime did not return inspected files");
    for (const expected of test.expected.files) {
        const file = result.files.find(({ path }) => path === expected.path);
        assert(file, "missing expected file: " + expected.path);
        assert.deepEqual(bytes(file.data, expected.path), bytes(expected.data, "expected file"), "file mismatch: " + expected.path);
    }
    return { stdout, stderr };
}

export async function within(promise, milliseconds) {
    let timer;
    try {
        return await Promise.race([promise, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(`timeout after ${milliseconds}ms`)), milliseconds); })]);
    } finally { clearTimeout(timer); }
}
