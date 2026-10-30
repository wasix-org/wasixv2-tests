import test from "node:test";
import assert from "node:assert/strict";
import { browserNames, browserOptions, selectCases } from "./matrix.mjs";

test("explicit browser lanes reject unknown engines and keep Chromium flags scoped", () => {
    for (const name of browserNames) {
        const options = browserOptions(name, `/bin/${name}`);
        assert.equal(options.executablePath, `/bin/${name}`);
        assert.equal(options.headless, true);
        assert.equal(Boolean(options.args), name === "chromium");
    }
    assert.throws(() => browserOptions("all"), /unknown browser/);
});

test("stress selection repeats real fixture metadata without changing the source", () => {
    const tests = [{ name: "proc-fork/default", module: "main.wasm", args: ["x"] }, { name: "fs/default" }];
    const selected = selectCases(tests, { repeat: 3, filter: "proc-fork/" });
    assert.equal(selected.length, 3);
    assert.deepEqual(selected.map(({ name }) => name), [1, 2, 3].map((i) => `proc-fork/default [${i}/3]`));
    assert(selected.every(({ module, args }) => module === "main.wasm" && args[0] === "x"));
    assert.equal(tests[0].name, "proc-fork/default");
    assert.deepEqual(selectCases(tests), tests);
    for (const repeat of [0, -1, 1.5, NaN, 10001]) assert.throws(() => selectCases(tests, { repeat }), /repeat/);
    assert.throws(() => selectCases(tests, { filter: "missing" }), /empty/);
});
