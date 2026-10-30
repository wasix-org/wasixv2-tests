// Shutdown reduction only: receive the Module but never instantiate runtime Wasm.
globalThis.onmessage = ({ data }) => {
    if (data.type === "boot") postMessage({ type: "booted" });
    else if (data.type === "collect") postMessage({ type: "collected" });
    else if (data.type === "retire") { postMessage({ type: "retired" }); close(); }
    else throw new Error("unexpected reduction worker request");
};
