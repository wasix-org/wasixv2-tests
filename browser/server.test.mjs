import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile, symlink, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { serve } from "./server.mjs";

test("server isolates all responses, serves wasm MIME and confines assets", async () => {
    const dir = await mkdtemp(join(tmpdir(), "wasix-browser-server-"));
    let server;
    try {
        await writeFile(join(dir, "module.wasm"), "source-built-at-runtime");
        await symlink("/etc/passwd", join(dir, "escape"));
        server = await serve({ "/assets/": dir });
        const response = await fetch(server.url + "/assets/module.wasm");
        assert.equal(await response.text(), "source-built-at-runtime");
        assert.equal(response.headers.get("content-type"), "application/wasm");
        for (const path of ["/missing", "/assets/escape", "/assets/%2e%2e%2fetc/passwd"]) {
            const response = await fetch(server.url + path);
            assert(response.status >= 400);
            assert.equal(response.headers.get("cross-origin-opener-policy"), "same-origin");
            assert.equal(response.headers.get("cross-origin-embedder-policy"), "require-corp");
            assert.equal(response.headers.get("cross-origin-resource-policy"), "same-origin");
        }
    } finally { await server?.close(); await rm(dir, { recursive: true, force: true }); }
});
