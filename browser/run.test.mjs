import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, writeFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { run } from "./run.mjs";

const browserName = process.env.BROWSER ?? "chromium";
const executablePath = process.env[browserName === "chromium" ? "CHROME" : browserName.toUpperCase()];
const runLane = (options) => run({ browserName, ...options });

test("real isolated browser executes source-built wasm, reports capabilities and fails missing isolation", async () => {
    const artifactRoot = process.env.BROWSER_TEST_ARTIFACTS;
    if (artifactRoot) await mkdir(resolve(artifactRoot), { recursive: true });
    const dir = await mkdtemp(join(artifactRoot ? resolve(artifactRoot) : tmpdir(), "wasix-browser-run-"));
    try {
        const smoke = await runLane({ smoke: true, out: join(dir, "smoke"), executablePath });
        assert.equal(smoke.passed, true, JSON.stringify(smoke));
        assert.equal(smoke.infrastructureOnly, true);
        const cap = JSON.parse(await readFile(join(dir, "smoke/capabilities.json")));
        assert.equal(cap.worker.blockingWait, true);
        assert.equal(cap.page.jspiSuspending, false);
        assert.equal(cap.worker.jspiPromising, false);
        assert.equal(await readFile(join(dir, "smoke/000.stdout"), "utf8"), "ok\n");
        const absent = await runLane({ smoke: true, out: join(dir, "absent"), executablePath, isolated: false });
        assert.equal(absent.passed, false);
        assert.equal(absent.tests.length, 1);
        assert.match(absent.tests[0].error, /capability missing/);
        const missing = JSON.parse(await readFile(join(dir, "absent/capabilities.json")));
        assert.equal(missing.page.crossOriginIsolated, false);
        assert.equal(missing.worker.crossOriginIsolated, false);

        // Reuse only the source-built transport module to test the suite adapter boundary.
        // This fake result provider is test-only; it supplies no WASIX executor.
        const runtime = join(dir, "runtime");
        await mkdir(runtime);
        const fixture = { name: "adapter/default", status: "ready", assets: "smoke", module: "main.wasm", timeoutMs: 10000,
            args: ["alpha"], env: [["EQ", "a=b"]], stdin: [], mounts: [],
            expected: { exitCode: 7, mustFail: false, stdout: ["fixture"], stderr: [], files: [] } };
        const manifestPath = join(dir, "smoke/manifest.json");
        await writeFile(manifestPath, JSON.stringify({ version: 1, lane: "sab-isolated-no-jspi", tests: [fixture] }));
        await writeFile(join(runtime, "fixture-adapter.js"), `export async function runFixture(test, assets) {
            if (test.args[0] !== 'alpha' || test.env[0][1] !== 'a=b' || !assets.endsWith('/smoke/')) throw new Error('lost fixture metadata');
            return { exitCode: 7, stdout: [102,105,120,116,117,114,101,10], stderr: [], files: [] };
        }`);
        const adapted = await runLane({ manifestPath, runtime, out: join(dir, "adapted"), executablePath });
        assert.equal(adapted.passed, true, JSON.stringify(adapted));
        const blocked = await runLane({ manifestPath, out: join(dir, "blocked"), executablePath });
        assert.equal(blocked.passed, false);
        assert.match(blocked.tests[0].error, /B8 merge point/);
        await writeFile(join(runtime, "fixture-adapter.js"), "export function runFixture() { return new Promise(() => {}); }");
        const hung = await runLane({ manifestPath, runtime, out: join(dir, "hung"), executablePath });
        assert.equal(hung.passed, false);
        assert.match(hung.tests[0].error, /timeout/);
        assert(hung.tests[0].durationMs < 15000, "hung browser context was not reclaimed promptly");
        const failedStartup = await runLane({ manifestPath, out: join(dir, "startup"), executablePath: join(dir, "missing-chromium") });
        assert.equal(failedStartup.passed, false);
        assert.equal(failedStartup.tests[0].status, "failed");
        assert.match(failedStartup.tests[0].error, /executable|launch/);
    } finally { if (!artifactRoot) await rm(dir, { recursive: true, force: true }); }
});
