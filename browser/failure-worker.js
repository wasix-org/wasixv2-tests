// Failure/throttling fixture around the real version-matched B8 worker, never a
// replacement kernel. The coordinator must discard every uncertain runtime heap.
import "/runtime/worker.js";
const receive = globalThis.onmessage;
const fault = new URL(location.href).searchParams.get("fault");
let role;
globalThis.onmessage = event => {
    const data = event.data;
    if (data.type === "boot") {
        role = data.role;
        if (fault === "bootstrap" && role === "root") throw new Error("B13 injected bootstrap death");
        if (fault === "throttled" && role === "timer") {
            const schedule = globalThis.setTimeout;
            globalThis.setTimeout = (callback, delay, ...args) => schedule(callback, delay + 50, ...args);
        }
    }
    if (fault === "task" && data.type === "run" && role === "task") throw new Error("B13 injected accepted task death");
    if (fault === "timer" && data.type === "timer-rearm" && role === "timer") throw new Error("B13 injected timer driver death");
    return receive(event);
};
