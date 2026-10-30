// Actual foreground/background Host sleeps; no injected scheduling delays.
import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { dirname, resolve, join } from "node:path";
import { fileURLToPath } from "node:url";
import { serve } from "./server.mjs";
import { nativeTabs, openNativeTab } from "./native-tabs.mjs";
import { within } from "./results.mjs";
import { liveWorkerCount } from "./lifetime.mjs";

export function checkSamples(elapsedNs, count, delayMs, visibility, expectedVisibility) {
    assert.equal(visibility, expectedVisibility, "measurement tab has the wrong visibility");
    assert(Array.isArray(elapsedNs) && elapsedNs.length === count, "timer progress did not complete every sample");
    assert(elapsedNs.every(ns => Number.isSafeInteger(ns) && ns >= delayMs * 1e6), "sleep completed before its deadline");
    const sorted = elapsedNs.map(ns => ns / 1e6).sort((a, b) => a - b);
    return { count, requestedMs: delayMs, visibility, minMs: sorted[0], medianMs: sorted[Math.floor(count / 2)],
        maxMs: sorted.at(-1), elapsedMs: sorted.reduce((sum, value) => sum + value, 0) };
}

export async function runBackground({ runtime, out, browserName = "chromium", headed = true }) {
    await mkdir(out, { recursive: true });
    const report = { browserName, headed, lane: "sab-isolated-no-jspi", samples: [] };
    const server = await serve({ "/runtime/": resolve(runtime), "/harness/": dirname(fileURLToPath(import.meta.url)) });
    let browser;
    let native;
    try {
        assert(headed, "real background measurement requires a headed native tab");
        native = await nativeTabs(browserName);
        browser = native.browser;
        report.version = browser.version();
        const cdp = browserName === "chromium" ? await browser.newBrowserCDPSession() : null;
        const context = native.context;
        const page = context.pages()[0] ?? await context.newPage();
        await page.goto(server.url + "/harness/index.html");
        const foreground = await openNativeTab(page, context, "WASIX-background-timer-measurement");
        await foreground.goto(server.url + "/harness/index.html");
        report.realFocusPolicy = true;
        await page.bringToFront();
        await within(page.evaluate(async () => {
            const { startCoordinator } = await import("/runtime/bootstrap.js");
            const { default: descriptor } = await import("/runtime/limits.js");
            const module = await WebAssembly.compile(await (await fetch("/runtime/runtime_bg.wasm")).arrayBuffer());
            const memory = new WebAssembly.Memory(descriptor);
            const messages = [];
            const waiters = new Set();
            let fatal;
            globalThis.until = async type => {
                for (;;) {
                    if (fatal) throw fatal;
                    const index = messages.findIndex(data => data.type === type);
                    if (index >= 0) return messages.splice(index, 1)[0];
                    await new Promise(resolve => waiters.add(resolve));
                }
            };
            globalThis.visibility = [];
            document.addEventListener("visibilitychange", () => visibility.push({ state: document.visibilityState, time: performance.now() }));
            globalThis.coordinator = await startCoordinator({ module, memory, disableJSPI: true,
                bindingsUrl: new URL("/runtime/runtime.js", location.href).href,
                workerUrl: new URL("/runtime/worker.js", location.href).href,
                coordinatorUrl: new URL("/runtime/coordinator.js", location.href).href,
                memoryCopyUrl: new URL("/runtime/memory/copy-worker.js", location.href).href,
                configure: "configure_browser", taskLimit: 8, serviceLimit: 8, prewarm: 0, servicePrewarm: 0,
                stackSize: 2097152, admissionTimeout: 10000,
            }, data => {
                if (data.type === "fatal") fatal = new Error(data.error);
                else messages.push(data);
                for (const wake of waiters) wake();
                waiters.clear();
            });
        }), 30000);
        const measure = async (expectedVisibility, count, delayMs) => {
            await page.waitForFunction(state => document.visibilityState === state, expectedVisibility, { timeout: 5000, polling: 100 });
            const measurement = await within(page.evaluate(async ({ count, delayMs }) => {
                const initial = document.visibilityState;
                const eventCount = visibility.length;
                coordinator.postMessage({ type: "entry", entry: "background_sleep_smoke", args: [count, delayMs] });
                const result = JSON.parse((await until("result")).result);
                return { result, initial, final: document.visibilityState, transitions: visibility.slice(eventCount) };
            }, { count, delayMs }), 120000);
            assert.equal(measurement.final, expectedVisibility);
            assert.equal(measurement.transitions.length, 0, "visibility changed during the sleep window");
            const sample = checkSamples(measurement.result, count, delayMs, measurement.initial, expectedVisibility);
            report.samples.push(sample);
            console.log(JSON.stringify(sample));
        };
        await measure("visible", 10, 100);
        await foreground.bringToFront();
        await measure("hidden", 80, 500);
        await page.bringToFront();
        await measure("visible", 10, 100);
        report.visibility = await page.evaluate(() => visibility);
        await page.evaluate(async () => { coordinator.postMessage({ type: "shutdown" }); await until("stopped"); });
        await within((async () => { while (await liveWorkerCount(page, cdp)) await new Promise(resolve => setTimeout(resolve, 10)); })(), 5000);
        report.liveWorkersAfterStop = await liveWorkerCount(page, cdp);
        report.passed = true;
    } catch (error) {
        report.error = error.stack ?? String(error);
        report.passed = false;
    } finally {
        await native?.close();
        await server.close();
        await writeFile(join(out, "results.json"), JSON.stringify(report, null, 2));
    }
    return report;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [runtime, out, browserName, headed] = process.argv.slice(2);
    const report = await runBackground({ runtime, out, browserName, headed: headed !== "headless" });
    console.log(JSON.stringify({ passed: report.passed, error: report.error?.split("\n")[0] }));
    process.exitCode = report.passed ? 0 : 1;
}
