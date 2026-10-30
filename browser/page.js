import { capabilities, requireLane } from "./capabilities.js";

async function message(worker, data) {
    try {
        return await new Promise((resolve, reject) => {
            worker.onmessage = ({ data }) => data.error ? reject(new Error(data.error)) : resolve(data);
            worker.onerror = (event) => reject(new Error(event.message));
            worker.onmessageerror = () => reject(new Error("worker message decoding failed"));
            if (data) worker.postMessage(data);
        });
    } finally { worker.terminate(); }
}

globalThis.probeLane = async () => {
    const report = { page: capabilities(), worker: (await message(new Worker(new URL("./probe.js", import.meta.url), { type: "module" }))).capabilities };
    // Publish evidence even when the required lane fails.
    globalThis.capabilityReport = report;
    requireLane(report.page, report.worker);
    return report;
};

globalThis.runCase = async (test) => {
    if (test.transportOnly) {
        const memory = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
        const output = await message(new Worker(new URL("./transport.js", import.meta.url), { type: "module" }), {
            url: new URL(test.module, location.origin + "/fixtures/" + test.assets + "/").href, memory,
        });
        if (Atomics.load(new Int32Array(memory.buffer), 0) !== 42) throw new Error("transport wasm result missing from shared guest memory");
        return output;
    }
    const assets = new URL("/fixtures/" + test.assets + "/", location.origin).href;
    // Validate the guest's real Wasm feature requirements before calling the runtime adapter.
    await WebAssembly.compileStreaming(fetch(new URL(test.module, assets)));
    // B8 supplies this module next to B2's runtime.js/coordinator.js/worker.js. The adapter
    // delegates to that executor and returns only after process exit and output drain.
    const adapter = await import("/runtime/fixture-adapter.js").catch((error) => {
        throw new Error("B8 merge point: runtime/fixture-adapter.js is unavailable: " + error);
    });
    if (typeof adapter.runFixture !== "function") throw new Error("B8 merge point: runFixture export is missing");
    return await adapter.runFixture(test, assets);
};
