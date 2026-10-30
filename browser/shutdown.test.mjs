import test from "node:test";
import assert from "node:assert/strict";
import { shutdownPassed } from "./shutdown.mjs";

test("shutdown acceptance rejects incomplete evidence, retained workers and failed controls", () => {
    const reports = [{ case: "no-rust", passed: true }, { case: "normal", passed: true, liveWorkersAfterStop: 0 },
        { case: "timer", passed: true, liveWorkersAfterStop: 0 }];
    assert.equal(shutdownPassed(reports, 3), true);
    assert.equal(shutdownPassed([], 0), false);
    assert.equal(shutdownPassed(reports.slice(1), 3), false);
    for (const patch of [{ passed: false }, { liveWorkersAfterStop: 1 }, { liveWorkersAfterStop: undefined }, { case: "unknown" }]) {
        assert.equal(shutdownPassed([reports[0], reports[1], { ...reports[2], ...patch }], 3), false);
    }
    assert.equal(shutdownPassed([{ ...reports[0], passed: false }, ...reports.slice(1)], 3), false);
});
