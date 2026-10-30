import test from "node:test";
import assert from "node:assert/strict";
import { checkSamples } from "./background.mjs";

test("background acceptance rejects early sleeps, missing progress and false visibility", () => {
    const samples = [100e6, 120e6, 110e6];
    assert.deepEqual(checkSamples(samples, 3, 100, "hidden", "hidden"), {
        count: 3, requestedMs: 100, visibility: "hidden", minMs: 100, medianMs: 110, maxMs: 120, elapsedMs: 330,
    });
    assert.throws(() => checkSamples([99e6, 100e6, 120e6], 3, 100, "hidden", "hidden"), /before its deadline/);
    assert.throws(() => checkSamples(samples.slice(1), 3, 100, "hidden", "hidden"), /progress/);
    assert.throws(() => checkSamples(samples, 3, 100, "visible", "hidden"), /visibility/);
});
