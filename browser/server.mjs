import { createServer } from "node:http";
import { readFile, realpath } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";

const headers = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
    "Cross-Origin-Resource-Policy": "same-origin",
    "Cache-Control": "no-store",
};
const mime = { ".wasm": "application/wasm", ".js": "text/javascript", ".mjs": "text/javascript", ".html": "text/html", ".json": "application/json" };

export async function serve(mounts, { isolated = true } = {}) {
    const roots = await Promise.all(Object.entries(mounts).map(async ([prefix, directory]) => [prefix, await realpath(directory)]));
    const server = createServer(async (request, response) => {
        if (isolated) for (const [key, value] of Object.entries(headers)) response.setHeader(key, value);
        try {
            if (request.method !== "GET") { response.writeHead(405).end(); return; }
            const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
            const mount = roots.find(([prefix]) => pathname.startsWith(prefix));
            if (!mount) { response.writeHead(404).end(); return; }
            const [prefix, root] = mount;
            const path = resolve(root, pathname.slice(prefix.length));
            if (!path.startsWith(root + sep)) { response.writeHead(403).end(); return; }
            const actual = await realpath(path);
            if (!actual.startsWith(root + sep)) { response.writeHead(403).end(); return; }
            let bytes = await readFile(actual);
            if ([".js", ".mjs"].includes(extname(actual))) {
                // Apply before *every* module, including coordinator/task/service workers.
                // V8 flags for shipped JSPI have changed across Chromium versions. Probes
                // verify the actual API absence in both agents rather than trusting flags.
                bytes = Buffer.concat([Buffer.from("WebAssembly.Suspending = undefined; WebAssembly.promising = undefined;\n"), bytes]);
            }
            response.writeHead(200, { "Content-Type": mime[extname(actual)] ?? "application/octet-stream" }).end(bytes);
        } catch {
            response.writeHead(404).end();
        }
    });
    await new Promise((resolve, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", resolve); });
    return {
        url: `http://127.0.0.1:${server.address().port}`,
        close: async () => {
            server.closeAllConnections();
            await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
        },
    };
}
