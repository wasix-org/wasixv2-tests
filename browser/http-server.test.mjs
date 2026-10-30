import test from "node:test";
import assert from "node:assert/strict";
import { serveHttp } from "./http-server.mjs";
test("HTTP mock separates readable failures, CORS denial and transport errors", async()=>{
    const peer=await serveHttp();
    try {
        const success=await fetch(peer.url+"/binary");
        assert.equal(success.status,201); assert.equal(success.headers.get("access-control-allow-origin"),"*");
        assert.deepEqual([...new Uint8Array(await success.arrayBuffer())],[0,128,255]);
        const error=await fetch(peer.url+"/status"); assert.equal(error.status,500);
        assert.equal((await fetch(peer.url+"/cors-denied")).headers.get("access-control-allow-origin"),null);
        await assert.rejects(fetch(peer.url+"/reset"));
        const echo=await fetch(peer.url+"/echo",{method:"POST",body:new Uint8Array([0,128,255])});
        assert.deepEqual([...new Uint8Array(await echo.arrayBuffer())],[0,128,255]);
    } finally { await peer.close(); }
});
