// Compare tiny/real compiled Modules with the same coordinator and no Rust execution.
import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { dirname, resolve, join } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium, firefox } from "playwright";
import { serve } from "./server.mjs";
import { browserOptions } from "./matrix.mjs";
import { within } from "./results.mjs";

export async function runWorkerClose({ runtime, output, browserName = "chromium", repeat = 5 }) {
    assert(Number.isSafeInteger(repeat) && repeat > 0 && repeat <= 100);
    const server = await serve({ "/runtime/": resolve(runtime), "/harness/": dirname(fileURLToPath(import.meta.url)) });
    const report = { browserName, runtimeInstantiated: false, cases: [] };
    await mkdir(output, { recursive: true });
    try {
        for (const large of Array.from({ length: repeat }, () => [false, true]).flat()) {
            const browser = await ({ chromium, firefox })[browserName].launch(browserOptions(browserName,
                process.env[browserName === "chromium" ? "CHROME" : "FIREFOX"]));
            try {
                report.version = browser.version();
                const cdp = browserName === "chromium" ? await browser.newBrowserCDPSession() : null;
                const page = await browser.newPage();
                await page.goto(server.url + "/harness/index.html");
                await within(page.evaluate(async large => {
                    const { startCoordinator } = await import("/runtime/bootstrap.js");
                    const module = large
                        ? await WebAssembly.compile(await (await fetch("/runtime/runtime_bg.wasm")).arrayBuffer())
                        : new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]));
                    const memory = new WebAssembly.Memory({ initial: 65, maximum: 4096, shared: true });
                    let finish, fail;
                    const stopped = new Promise((resolve, reject) => { finish = resolve; fail = reject; });
                    const coordinator = await startCoordinator({ module, memory,
                        bindingsUrl: new URL("/runtime/runtime.js", location.href).href,
                        workerUrl: new URL("/harness/retire-worker.js", location.href).href,
                        coordinatorUrl: new URL("/runtime/coordinator.js", location.href).href,
                        taskLimit: 2, serviceLimit: 2, prewarm: 2, servicePrewarm: 2,
                    }, data => { if (data.type === "stopped") finish(); if (data.type === "fatal") fail(new Error(data.error)); });
                    coordinator.postMessage({ type: "shutdown" });
                    await stopped;
                    coordinator.terminate();
                }, large), 15000);
                const live = async () => cdp
                    ? (await cdp.send("Target.getTargets")).targetInfos.filter(({ type }) => type === "worker").map(({ url }) => url)
                    : page.workers().map(worker => worker.url());
                let remaining;
                try { await within((async () => { while ((await live()).length) await new Promise(resolve => setTimeout(resolve, 20)); })(), 5000); }
                catch { /* Record the failed zero-worker condition below. */ }
                remaining = await live();
                const result = { iteration: report.cases.length + 1, large, passed: remaining.length === 0, remaining: remaining.map(url => url.split("/").at(-1)) };
                report.cases.push(result);
                console.log(JSON.stringify(result));
            } finally { await browser.close(); }
        }
        report.passed = report.cases.every(test => test.passed);
        assert.equal(report.cases.length, 2 * repeat);
    } finally {
        await server.close();
        await writeFile(join(output, "results.json"), JSON.stringify(report, null, 2));
    }

    return report;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [runtime, output, browserName, repeat = "5"] = process.argv.slice(2);
    const report = await runWorkerClose({ runtime, output, browserName, repeat: Number(repeat) });
    process.exitCode = report.passed ? 0 : 1;
}
