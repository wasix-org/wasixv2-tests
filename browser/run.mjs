import { chromium, firefox, webkit } from "playwright";
import { browserOptions, selectCases } from "./matrix.mjs";
import wabtFactory from "wabt";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { resolve, dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { parseArgs } from "node:util";
import { serve } from "./server.mjs";
import { serveHttp } from "./http-server.mjs";
import { validateManifest, checkResult, within, bytes } from "./results.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const MAX_LOG = 1024 * 1024;

async function smokeInput(out) {
    const input = join(out, "input");
    await mkdir(input);
    const wabt = await wabtFactory();
    const source = `(module
        (import "env" "memory" (memory 1 1 shared))
        (func (export "answer") i32.const 0 i32.const 42 i32.atomic.store))`;
    const module = wabt.parseWat("transport.wat", source, { threads: true });
    try { await writeFile(join(input, "main.wasm"), module.toBinary({}).buffer); }
    finally { module.destroy(); }
    await writeFile(join(input, "transport.wat"), source);
    return {
        input,
        manifest: { version: 1, lane: "sab-isolated-no-jspi", tests: [{
            name: "infrastructure/wasm-transport", status: "ready", assets: "smoke", module: "main.wasm",
            transportOnly: true, timeoutMs: 10000,
            expected: { exitCode: 0, mustFail: false, stdout: ["ok"], stderr: [], files: [] },
        }] },
    };
}

export async function run({ manifestPath, runtime, out, smoke = false, b2Smoke = false, executablePath, isolated = true, browserName = "chromium", repeat = 1, filter, launch, freshBrowser = false }) {
    out = resolve(out);
    await mkdir(dirname(out), { recursive: true });
    await mkdir(out); // Never mix a previous run's successful artifacts with this run.
    let manifest;
    let input;
    let browser;
    let server;
    let http;
    const report = { version: 1, lane: "sab-isolated-no-jspi", infrastructureOnly: smoke, tests: [] };
    const evidence = { lane: report.lane, node: process.version, jspiEnforcement: "APIs removed before every served JS module; page and worker probes required", page: null, worker: null };
    try {
        if (smoke) ({ manifest, input } = await smokeInput(out));
        else {
            manifestPath = resolve(manifestPath);
            input = dirname(manifestPath);
            manifest = JSON.parse(await readFile(manifestPath, "utf8"));
            await writeFile(join(out, "manifest.json"), JSON.stringify(manifest, null, 2));
        }
        validateManifest(manifest);
        if (!smoke && manifest.tests.some((test) => test.transportOnly)) throw new Error("transportOnly is reserved for --smoke");
        const tests = selectCases(structuredClone(manifest.tests), { repeat, filter });
        if (tests.some(test=>test.http)) {
            http = await serveHttp();
            for (const test of tests) if (test.http) test.env.push(["HTTP_BASE",http.url]);
        }
        if (b2Smoke) tests.push({ name: "infrastructure/b2-shared-runtime", status: "ready", timeoutMs: 45000, b2: true });
        report.tests = tests.map(({ name }) => ({ name, status: "failed", error: "browser did not start" }));
        const mounts = { "/harness/": here, "/fixtures/": smoke ? out : input };
        // Smoke uses the same relative module layout as fixture exports.
        if (smoke) { await mkdir(join(out, "smoke")); await writeFile(join(out, "smoke/main.wasm"), await readFile(join(input, "main.wasm"))); }
        if (runtime) mounts["/runtime/"] = resolve(runtime);
        server = await serve(mounts, { isolated });
        const launchOptions = browserOptions(browserName, executablePath);
        const launchBrowser = () => (launch ?? ((options) => ({ chromium, firefox, webkit })[browserName].launch(options)))(launchOptions);
        browser = await launchBrowser();
        evidence.isolation = freshBrowser ? "process-per-case" : "context-per-case";
        evidence.browserName = browserName;
        evidence.browser = browser.version();
        evidence.executable = executablePath ?? `Playwright pinned ${browserName}`;
        for (let index = 0; index < tests.length; index++) {
            if (freshBrowser && index > 0) {
                await browser.close();
                browser = await launchBrowser();
            }
            const test = tests[index];
            const result = report.tests[index];
            const prefix = join(out, String(index).padStart(3, "0"));
            await writeFile(prefix + ".stdout", "");
            await writeFile(prefix + ".stderr", "");
            let context;
            let page;
            let log = "";
            let truncated = false;
            const append = (line) => {
                if (log.length < MAX_LOG) log += line.slice(0, MAX_LOG - log.length) + "\n";
                else truncated = true;
            };
            const start = Date.now();
            try {
                await within((async () => {
                    context = await browser.newContext();
                    page = await context.newPage();
                    page.on("console", (message) => append(`[${message.type()}] ${message.text()}`));
                    page.on("pageerror", (error) => append(`[pageerror] ${error.stack}`));
                    page.on("requestfailed", (request) => append(`[requestfailed] ${request.url()} ${request.failure()?.errorText}`));
                    await page.goto(server.url + "/harness/index.html");
                    await page.waitForFunction(() => typeof globalThis.probeLane === "function");
                    try {
                        const cap = await page.evaluate(() => globalThis.probeLane());
                        evidence.page = cap.page; evidence.worker = cap.worker;
                        result.capabilities = cap;
                    } catch (error) {
                        const cap = await page.evaluate(() => globalThis.capabilityReport).catch(() => null);
                        if (cap) { evidence.page = cap.page; evidence.worker = cap.worker; result.capabilities = cap; }
                        throw error;
                    }
                    if (test.status !== "ready") throw new Error(test.error ?? "fixture export failed");
                    if (test.b2) {
                        if (!runtime) throw new Error("B2 smoke requires --runtime with B2 build.sh output");
                        await page.goto(server.url + "/runtime/index.html");
                        await page.waitForFunction(() => globalThis.smokeResults !== undefined);
                        result.runtime = await page.evaluate(() => globalThis.smokeResults);
                        if (result.runtime?.sharedArc !== true || result.runtime?.noJSPI !== true) throw new Error("B2 smoke returned incomplete results");
                    } else {
                        const output = await page.evaluate((test) => globalThis.runCase(test), test);
                        // Keep failing guest stdout/stderr too, before checking expectations.
                        await writeFile(prefix + ".stdout", bytes(output.stdout, "stdout"));
                        await writeFile(prefix + ".stderr", bytes(output.stderr, "stderr"));
                        checkResult(test, output);
                        result.exitCode = output.exitCode;
                    }
                    result.status = "passed";
                    delete result.error;
                })(), test.timeoutMs);
            } catch (error) {
                result.status = "failed";
                result.error = error.stack ?? String(error);
            } finally {
                // Closing the context terminates its dedicated workers, including a hung guest.
                try { await context?.close(); }
                catch (error) {
                    result.status = "failed";
                    result.error = `${result.error ?? ""}\ncontext cleanup failed: ${error}`;
                }
                result.durationMs = Date.now() - start;
                result.logsTruncated = truncated;
                await writeFile(prefix + ".console.log", log + (truncated ? "\n[console log truncated]\n" : ""));
                await writeFile(prefix + ".json", JSON.stringify(result, null, 2));
                console.log(`${result.status}: ${test.name}${result.error ? ": " + result.error.split("\n")[0] : ""}`);
            }
        }
    } catch (error) {
        report.error = error.stack ?? String(error);
        for (const test of report.tests) if (test.status !== "passed") test.error = report.error;
    } finally {
        for (const cleanup of [() => browser?.close(), () => server?.close(), () => http?.close()]) {
            try { await cleanup(); }
            catch (error) { report.error ??= "cleanup failed: " + error; }
        }
        report.passed = report.tests.length > 0 && !report.error && report.tests.every(({ status }) => status === "passed");
        for (let index = 0; index < report.tests.length; index++) {
            await writeFile(join(out, String(index).padStart(3, "0") + ".json"), JSON.stringify(report.tests[index], null, 2));
        }
        await writeFile(join(out, "capabilities.json"), JSON.stringify(evidence, null, 2));
        await writeFile(join(out, "results.json"), JSON.stringify(report, null, 2));
    }
    return report;
}

async function main() {
    const { values } = parseArgs({ options: {
        manifest: { type: "string" }, runtime: { type: "string" }, artifacts: { type: "string", default: "artifacts/run" },
        browser: { type: "string", default: "chromium" }, repeat: { type: "string", default: "1" }, filter: { type: "string" },
        "fresh-browser": { type: "boolean", default: false },
        smoke: { type: "boolean", default: false }, "b2-smoke": { type: "boolean", default: false },
    } });
    if (values.smoke === Boolean(values.manifest)) throw new Error("choose exactly one of --smoke or --manifest PATH");
    const report = await run({ manifestPath: values.manifest, runtime: values.runtime, out: values.artifacts, smoke: values.smoke, b2Smoke: values["b2-smoke"], executablePath: process.env[values.browser === "chromium" ? "CHROME" : values.browser.toUpperCase()], browserName: values.browser, repeat: Number(values.repeat), filter: values.filter, freshBrowser: values["fresh-browser"] });
    if (!report.passed) { console.error(report.error ?? "browser cases failed; see results.json"); process.exitCode = 1; }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    main().catch((error) => { console.error(error); process.exitCode = 2; });
}
