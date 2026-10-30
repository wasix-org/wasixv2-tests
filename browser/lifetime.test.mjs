import test from "node:test";
import assert from "node:assert/strict";
import { checkLifetimeBounds } from "./lifetime.mjs";

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
    ["heapBytes", 9 * 1024 ** 2, /heap/], ["workers", 1, /worker pool/],
    ["rendererRssBytes", 257 * 1024 ** 2, /RSS/], ["rendererVirtualBytes", 129 * 1024 ** 3, /reservations/],
]) {
    test(`${field} growth fails persistent-runtime acceptance`, () => {
        const data = samples();
        data.at(-1)[field] += increase;
        assert.throws(() => checkLifetimeBounds(data, true), message);
    });
}
