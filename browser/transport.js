// Infrastructure-only module transport. The WASIX executor is supplied by B2/B8.
onmessage = async ({ data: { url, memory } }) => {
    try {
        const { instance } = await WebAssembly.instantiateStreaming(fetch(url), { env: { memory } });
        instance.exports.answer();
        postMessage({ exitCode: 0, stdout: Array.from(new TextEncoder().encode("ok\n")), stderr: [], files: [] });
    } catch (error) { postMessage({ error: String(error) }); }
    close();
};
