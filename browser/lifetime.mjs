// Persistent B8 facade acceptance using B11's capabilities, server and expectations.
import assert from "node:assert/strict";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { resolve, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium, firefox } from "playwright";
import { serve } from "./server.mjs";
import { browserOptions } from "./matrix.mjs";
import { checkResult, validateManifest, within } from "./results.mjs";

export function checkLifetimeBounds(samples, chromium) {
    const tail = samples.filter(({ iteration }) => iteration >= 25);
    assert(tail.length >= 2, "need warmup and tail samples");
    const range = key => Math.max(...tail.map(s => s[key])) - Math.min(...tail.map(s => s[key]));
    const peakGrowth = key => Math.max(...tail.map(s => s[key])) - Math.max(...tail.filter(s => s.iteration <= 50).map(s => s[key]));
    assert(range("heapBytes") <= 8 * 1024 * 1024, "shared runtime heap did not plateau");
    assert(tail.every(s => s.workers === tail[0].workers), "worker pool grew after warmup");
    if (chromium) {
        assert(peakGrowth("rendererRssBytes") <= 256 * 1024 * 1024, "renderer RSS peak did not stay bounded");
        assert(range("rendererVirtualBytes") <= 128 * 1024 ** 3, "address reservations did not stay bounded");
    }
    return { maxHeapBytes: Math.max(...tail.map(s => s.heapBytes)), workers: tail[0].workers,
        ...(chromium ? { maxRssBytes: Math.max(...tail.map(s => s.rendererRssBytes)), rssPeakGrowthBytes: peakGrowth("rendererRssBytes"), rssRangeBytes: range("rendererRssBytes"), virtualRangeBytes: range("rendererVirtualBytes") } : {}) };
}

export async function runLifetime({ manifestPath, runtime, out, repeat = 500, browserName = "chromium", filter = "proc-fork/default", entry = "run_fixture", workerFault }) {
    assert(Number.isSafeInteger(repeat) && repeat > 0);
    const manifest = validateManifest(JSON.parse(await readFile(manifestPath, "utf8")));
    const fixture = manifest.tests.find(({ name }) => name === filter);
    assert(fixture, "missing exact fixture " + filter);
    out = resolve(out);
    await mkdir(out, { recursive: true });
    const report = { browserName, lane: "sab-isolated-no-jspi", repeat, entry, workerFault, completed: 0, samples: [] };
    const server = await serve({ "/harness/": dirname(fileURLToPath(import.meta.url)), "/runtime/": resolve(runtime), "/fixtures/": dirname(resolve(manifestPath)) });
    let browser;
    let cdp;
    let page;
    try {
        browser = await ({ chromium, firefox })[browserName].launch(browserOptions(browserName, process.env[browserName === "chromium" ? "CHROME" : "FIREFOX"]));
        report.version = browser.version();
        if (browserName === "chromium") cdp = await browser.newBrowserCDPSession();
        page = await browser.newPage();
        await page.goto(server.url + "/harness/index.html");
        report.capabilities = await page.evaluate(() => globalThis.probeLane());
        await page.exposeFunction("recordLifetime", async ({ result, heapBytes, stats, heartbeat }) => {
            if (entry === "run_fixture") checkResult(fixture, result);
            else if (entry === "lifetime_smoke") assert.deepEqual(result, { cancelledWaits: 2048, timerCancels: 1000, wakeBeforeWait: 1000, deadlineKills: 16 });
            else if (entry === "admission_smoke") assert.deepEqual(result, { admissionRejected: true, droppedOnce: true });
            else throw new Error("unknown lifetime entry " + entry);
            ++report.completed;
            if (report.completed === 1 || report.completed % 25 === 0 || report.completed === repeat) {
                const sample = { iteration: report.completed, heapBytes, workers: stats.workers.length, copyRotations: stats.copyRotations, heartbeat };
                if (cdp) {
                    let rss = 0, virtual = 0;
                    const { processInfo } = await cdp.send("SystemInfo.getProcessInfo");
                    for (const process of processInfo.filter(({ type }) => type === "renderer")) {
                        const status = await readFile(`/proc/${process.id}/status`, "utf8").catch(() => "");
                        rss += Number(status.match(/^VmRSS:\s+(\d+)/m)?.[1] ?? 0) * 1024;
                        virtual += Number(status.match(/^VmSize:\s+(\d+)/m)?.[1] ?? 0) * 1024;
                    }
                    Object.assign(sample, { rendererRssBytes: rss, rendererVirtualBytes: virtual });
                }
                report.samples.push(sample);
                await writeFile(join(out, "results.json"), JSON.stringify(report, null, 2));
                console.log(JSON.stringify(sample));
            }
        });
        report.runtime = await within(page.evaluate(async ({ fixture, repeat, entry, workerFault }) => {
            const baseUrl = new URL("/runtime/", location.href);
            const { startCoordinator } = await import("/runtime/bootstrap.js");
            const { default: descriptor } = await import("/runtime/limits.js");
            const [module, bytes] = await Promise.all([
                fetch(new URL("runtime_bg.wasm", baseUrl)).then(r => r.arrayBuffer()).then(WebAssembly.compile),
                fetch(`/fixtures/${fixture.assets}/${fixture.module}`).then(r => r.arrayBuffer()).then(b => new Uint8Array(b)),
            ]);
            const memory = new WebAssembly.Memory(descriptor);
            const messages = [];
            const waiters = new Set();
            let fatal;
            let heartbeat = 0;
            const beat = setInterval(() => ++heartbeat, 10);
            function receive(data) {
                if (data.type === "fatal") fatal = new Error(data.error);
                else if (data.type !== "idle" && data.type !== "retired") messages.push(data);
                for (const wake of waiters) wake();
                waiters.clear();
            }
            async function until(type) {
                for (;;) {
                    if (fatal) throw fatal;
                    const i = messages.findIndex(message => message.type === type);
                    if (i >= 0) return messages.splice(i, 1)[0];
                    await new Promise(resolve => waiters.add(resolve));
                }
            }
            let coordinator;
            try {
                coordinator = await startCoordinator({ module, memory, disableJSPI: true,
                    bindingsUrl: new URL("runtime.js", baseUrl).href,
                    workerUrl: workerFault ? new URL(`/harness/failure-worker.js?fault=${workerFault}`, location.href).href : new URL("worker.js", baseUrl).href,
                    coordinatorUrl: new URL("coordinator.js", baseUrl).href,
                    memoryCopyUrl: new URL("memory/copy-worker.js", baseUrl).href,
                    ioModuleUrl: new URL("io-driver.js", baseUrl).href,
                    configure: "configure_browser", taskLimit: entry === "admission_smoke" ? 1 : 8,
                    serviceLimit: 8, prewarm: entry === "admission_smoke" ? 1 : 8, servicePrewarm: 4,
                    stackSize: 2097152, admissionTimeout: 10000,
                }, receive);
                for (let iteration = 0; iteration < repeat; ++iteration) {
                    const before = heartbeat;
                    coordinator.postMessage({ type: "entry", entry, args: entry === "run_fixture" ? [JSON.stringify(fixture), bytes] : [] });
                    const result = JSON.parse((await until("result")).result);
                    if (entry === "lifetime_smoke" && heartbeat <= before) throw new Error("UI heartbeat stalled while guests and timers ran");
                    let stats;
                    for (;;) {
                        coordinator.postMessage({ type: "stats" });
                        stats = await until("stats");
                        if (!stats.admissions && stats.workers.every(({ status }) => status === "idle")) break;
                        await new Promise(resolve => setTimeout(resolve, 1));
                    }
                    await globalThis.recordLifetime({ result, heapBytes: memory.buffer.byteLength, stats, heartbeat });
                }
                coordinator.postMessage({ type: "shutdown" });
                await until("stopped");
                return { stopped: true, heartbeat };
            } finally { clearInterval(beat); coordinator?.terminate(); }
        }, { fixture, repeat, entry, workerFault }), Math.max(60000, repeat * fixture.timeoutMs));
        assert.equal(report.completed, repeat);
        assert(report.runtime.stopped && report.runtime.heartbeat > 0);
        // After warmup, bounds include runtime allocator high-water space and browser
        // GC/JIT variation. RSS and address reservations are separate measurements.
        if (repeat >= 50) {
            report.bounds = checkLifetimeBounds(report.samples, Boolean(cdp));
        }
        await within((async () => {
            while (page.workers().length) await new Promise(resolve => setTimeout(resolve, 10));
        })(), 5000);
        report.liveWorkersAfterStop = page.workers().length;
        report.passed = true;
    } catch (error) {
        report.error = error.stack ?? String(error);
        report.passed = Boolean(workerFault && /B13 injected/.test(report.error));
        if (report.passed) {
            try {
                await within((async () => {
                    while (page.workers().length) await new Promise(resolve => setTimeout(resolve, 10));
                })(), 5000);
                report.liveWorkersAfterStop = page.workers().length;
            } catch (cleanup) { report.passed = false; report.error += "\n" + cleanup; }
        }
    } finally {
        await browser?.close();
        await server.close();
        await writeFile(join(out, "results.json"), JSON.stringify(report, null, 2));
    }
    return report;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [runtime, manifestPath, out, repeat = "500", browserName = "chromium", filter = "proc-fork/default", entry = "run_fixture", workerFault] = process.argv.slice(2);
    const report = await runLifetime({ runtime, manifestPath, out, repeat: Number(repeat), browserName, filter, entry, workerFault });
    console.log(JSON.stringify({ passed: report.passed, completed: report.completed, error: report.error?.split("\n")[0] }));
    process.exitCode = report.passed ? 0 : 1;
}
