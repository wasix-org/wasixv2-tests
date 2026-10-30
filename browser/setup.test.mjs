import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { run } from "./run.mjs";

test("context and page startup failures are recorded per case and reclaim partial contexts", async () => {
    const dir = await mkdtemp(join(tmpdir(), "browser-setup-"));
    let attempts = 0, contextsClosed = 0, browsersClosed = 0;
    try {
        const result = await run({ smoke: true, repeat: 3, out: join(dir, "run"),
            launch: async () => ({ version: () => "test", close: async () => { browsersClosed++; },
                newContext: async () => {
                    if (++attempts === 1) throw new Error("context setup failed");
                    return { newPage: async () => { throw new Error("page setup failed"); },
                        close: async () => { contextsClosed++; },
                    };
                },
            }),
        });
        assert.equal(result.passed, false);
        assert.equal(result.error, undefined);
        assert.equal(attempts, 3);
        assert.equal(contextsClosed, 2);
        assert.equal(browsersClosed, 1);
        for (let index = 0; index < 3; index++) {
            const saved = JSON.parse(await readFile(join(dir, "run", String(index).padStart(3, "0") + ".json")));
            assert.equal(saved.status, "failed");
            assert.match(saved.error, /setup failed/);
            assert(Number.isFinite(saved.durationMs));
            assert.equal(await readFile(join(dir, "run", String(index).padStart(3, "0") + ".console.log"), "utf8"), "");
        }
    } finally { await rm(dir, { recursive: true, force: true }); }
});


test("process isolation launches and closes a distinct browser for each requested case", async () => {
    const dir = await mkdtemp(join(tmpdir(), "browser-process-"));
    let launched = 0, closed = 0;
    try {
        const result = await run({ smoke: true, repeat: 3, freshBrowser: true, out: join(dir, "run"),
            launch: async () => {
                launched++;
                return { version: () => "test", close: async () => { closed++; },
                    newContext: async () => { throw new Error("case reached"); },
                };
            },
        });
        assert.equal(launched, 3);
        assert.equal(closed, 3);
        assert(result.tests.every(({ error }) => error.includes("case reached")));
        const evidence = JSON.parse(await readFile(join(dir, "run/capabilities.json")));
        assert.equal(evidence.isolation, "process-per-case");
    } finally { await rm(dir, { recursive: true, force: true }); }
});
