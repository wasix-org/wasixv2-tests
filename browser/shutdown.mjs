// Both R2 regressions retain mandatory zero-native-worker disposal gates.
import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { runWorkerClose } from "./worker-close.mjs";
import { runLifetime } from "./lifetime.mjs";

export function shutdownPassed(reports, expected) {
    return Number.isSafeInteger(expected) && expected > 0 && reports.length === expected && reports.every(report =>
        report.passed === true && ["no-rust", "normal", "timer"].includes(report.case) &&
        (report.case === "no-rust" || report.liveWorkersAfterStop === 0));
}

export async function runShutdown({ runtime, manifestPath, out, engines = "chromium,firefox", repeat = 3 }) {
    assert(Number.isSafeInteger(repeat) && repeat > 0 && repeat <= 100);
    await mkdir(out, { recursive: true });
    const reports = [];
    const expectedReports = engines.split(",").length * (1 + 2 * repeat);
    try {
        for (const browserName of engines.split(",")) {
            assert(["chromium", "firefox"].includes(browserName));
            const reduction = await runWorkerClose({ runtime, output: join(out, browserName + "-no-rust"), browserName, repeat });
            reports.push({ browserName, case: "no-rust", passed: reduction.passed });
            for (let iteration = 1; iteration <= repeat; ++iteration) {
                for (const workerFault of [undefined, "timer"]) {
                    const name = `${browserName}-${workerFault ?? "normal"}-${iteration}`;
                    const result = await runLifetime({ runtime, manifestPath, out: join(out, name), repeat: 1, browserName,
                        entry: workerFault ? "lifetime_smoke" : "run_fixture", workerFault });
                    reports.push({ browserName, case: workerFault ?? "normal", iteration, passed: result.passed,
                        liveWorkersAfterStop: result.liveWorkersAfterStop });
                    console.log(JSON.stringify(reports.at(-1)));
                }
            }
        }
    } finally {
        await writeFile(join(out, "results.json"), JSON.stringify({ passed: shutdownPassed(reports, expectedReports), reports }, null, 2));
    }
    return { passed: shutdownPassed(reports, expectedReports), reports };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [runtime, manifestPath, out, engines, repeat = "3"] = process.argv.slice(2);
    const report = await runShutdown({ runtime, manifestPath, out, engines, repeat: Number(repeat) });
    process.exitCode = report.passed ? 0 : 1;
}
