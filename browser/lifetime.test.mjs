import test from "node:test";
import assert from "node:assert/strict";
import { checkLifetimeBounds, checkLifetimeResult, liveWorkerCount } from "./lifetime.mjs";

test("relay lifetime acceptance requires exact success and empty process outputs", () => {
    const result = { exitCode: 0, stdout: [], stderr: [], files: [], relayTcp: true };
    assert.doesNotThrow(() => checkLifetimeResult("relay_smoke", null, result));
    for (const patch of [{ relayTcp: false }, { exitCode: 1 }, { stdout: [1] }, { stderr: [1] }, { files: [1] }]) {
        assert.throws(() => checkLifetimeResult("relay_smoke", null, { ...result, ...patch }));
    }
    assert.throws(() => checkLifetimeResult("unknown", null, result), /unknown lifetime entry/);
});

test("worker inventory counts unattached native targets after debugger detachment", async () => {
    const page = { workers: () => [] };
    const targetInfos = [{ type: "page" }, { type: "worker", attached: false }, { type: "worker", attached: true }];
    const cdp = { async send(method) { assert.equal(method, "Target.getTargets"); return { targetInfos }; } };
    assert.equal(await liveWorkerCount(page, cdp), 2);
    targetInfos.splice(1);
    assert.equal(await liveWorkerCount(page, cdp), 0);
});

test("non-CDP browser inventory retains its worker lifecycle check", async () => {
    assert.equal(await liveWorkerCount({ workers: () => [1, 2, 3] }), 3);
});

function samples() {
    return [1, 25, 50, 500].map(iteration => ({ iteration, heapBytes: 32 * 1024 ** 2,
        workers: 10, rendererRssBytes: 150 * 1024 ** 2, rendererVirtualBytes: 2 * 1024 ** 4 }));
}
test("lifetime resource accounting ignores cold-start allocation and bounds warm samples", () => {
    const data = samples();
    data[0].heapBytes = 1;
    assert.deepEqual(checkLifetimeBounds(data, true), { maxHeapBytes: 32 * 1024 ** 2,
        workers: 10, maxRssBytes: 150 * 1024 ** 2, rssPeakGrowthBytes: 0, rssRangeBytes: 0, virtualRangeBytes: 0 });
    assert.deepEqual(checkLifetimeBounds(data, false), { maxHeapBytes: 32 * 1024 ** 2, workers: 10 });
    assert.throws(() => checkLifetimeBounds(data.slice(0, 1), true), /warmup/);
});
test("RSS collection dips are reported without mistaking them for peak growth", () => {
    const data = samples();
    data[1].rendererRssBytes += 400 * 1024 ** 2;
    const bounds = checkLifetimeBounds(data, true);
    assert.equal(bounds.rssRangeBytes, 400 * 1024 ** 2);
    assert.equal(bounds.rssPeakGrowthBytes, 0);
});
for (const [field, increase, message] of [
    ["heapBytes", 17 * 1024 ** 2, /heap/], ["workers", 1, /worker pool/],
    ["rendererRssBytes", 513 * 1024 ** 2, /RSS/], ["rendererVirtualBytes", 17 * 1024 ** 3, /reservations/],
]) {
    test(`${field} growth fails persistent-runtime acceptance`, () => {
        const data = samples();
        data.at(-1)[field] += increase;
        assert.throws(() => checkLifetimeBounds(data, true), message);
    });
}
test("debug-artifact budgets include staging high-water but bound total RSS", () => {
    const data = samples();
    data.at(-1).heapBytes += 16 * 1024 ** 2;
    data.at(-1).rendererRssBytes += 512 * 1024 ** 2;
    data.at(-1).rendererVirtualBytes += 16 * 1024 ** 3;
    assert.doesNotThrow(() => checkLifetimeBounds(data, true));
    for (const sample of data) sample.rendererRssBytes = 2 * 1024 ** 3 + 1;
    assert.throws(() => checkLifetimeBounds(data, true), /RSS exceeded/);
});
