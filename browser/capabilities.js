export function capabilities(worker = false) {
    const report = {
        secureContext: globalThis.isSecureContext,
        crossOriginIsolated: globalThis.crossOriginIsolated,
        sharedArrayBuffer: typeof SharedArrayBuffer === "function",
        atomics: typeof Atomics === "object",
        sharedMemory: false,
        blockingWait: false,
        jspiSuspending: typeof WebAssembly.Suspending === "function",
        jspiPromising: typeof WebAssembly.promising === "function",
    };
    try {
        const memory = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
        report.sharedMemory = memory.buffer instanceof SharedArrayBuffer;
        if (worker) report.blockingWait = Atomics.wait(new Int32Array(memory.buffer), 0, 0, 0) === "timed-out";
    } catch (error) { report.error = String(error); }
    return report;
}

export function requireLane(page, worker) {
    for (const [actor, report] of [["page", page], ["worker", worker]]) {
        for (const key of ["secureContext", "crossOriginIsolated", "sharedArrayBuffer", "sharedMemory", "atomics"]) {
            if (report[key] !== true) throw new Error(`required capability missing: ${actor}.${key}`);
        }
        if (report.jspiSuspending || report.jspiPromising) throw new Error(`JSPI must be disabled on ${actor}`);
    }
    if (!worker.blockingWait) throw new Error("required capability missing: worker.blockingWait");
}
