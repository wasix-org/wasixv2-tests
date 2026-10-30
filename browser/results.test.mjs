import test from "node:test";
import assert from "node:assert/strict";
import { checkResult, validateManifest, within } from "./results.mjs";
import { requireLane } from "./capabilities.js";

const fixture = () => ({ name: "case/default", status: "ready", assets: "cases/0", module: "main.wasm", timeoutMs: 100,
    expected: { exitCode: 42, mustFail: false, stdout: ["main", "atexit"], stderr: [], files: [] } });
const output = () => ({ exitCode: 42, stdout: Array.from(Buffer.from(" \nmain\natexit\n")), stderr: [], files: [] });

test("exit and output expectations match the native fixture checker", () => {
    checkResult(fixture(), output());
    for (const result of [ { ...output(), exitCode: 0 }, { ...output(), stdout: [] }, { ...output(), truncated: true }, { ...output(), stderr: [-1] } ]) {
        assert.throws(() => checkResult(fixture(), result));
    }
    const config = fixture(); config.expected.mustFail = true; config.expected.stdout = [];
    checkResult(config, { ...output(), exitCode: -1 });
    assert.throws(() => checkResult(config, { ...output(), exitCode: 0 }));
});

test("expected files require exact bytes and cannot silently disappear", () => {
    const config = fixture(); config.expected.files = [{ path: "/t/file", data: [0, 255] }];
    assert.throws(() => checkResult(config, output()));
    checkResult(config, { ...output(), files: [{ path: "/t/file", data: [0, 255] }] });
    assert.throws(() => checkResult(config, { ...output(), files: [{ path: "/t/file", data: [0] }] }));
});

test("manifest rejects empty, duplicate, stale-schema and unknown statuses", () => {
    const manifest = { version: 1, lane: "sab-isolated-no-jspi", tests: [fixture()] };
    validateManifest(manifest);
    for (const value of [ { ...manifest, version: 2 }, { ...manifest, unknown: true }, { ...manifest, tests: [{ ...fixture(), b2: true }] }, { ...manifest, tests: [] }, { ...manifest, tests: [fixture(), fixture()] }, { ...manifest, tests: [{ ...fixture(), status: "skipped" }] }, { ...manifest, tests: [{ ...fixture(), assets: "../escape" }] } ]) {
        assert.throws(() => validateManifest(value));
    }
});

test("SAB/no-JSPI lane fails for any missing capability on either agent", () => {
    const ready = { secureContext: true, crossOriginIsolated: true, sharedArrayBuffer: true, sharedMemory: true, atomics: true, blockingWait: true, jspiSuspending: false, jspiPromising: false };
    requireLane(ready, ready);
    for (const key of ["secureContext", "crossOriginIsolated", "sharedArrayBuffer", "sharedMemory", "atomics", "blockingWait"]) {
        assert.throws(() => requireLane(ready, { ...ready, [key]: false }));
        if (key !== "blockingWait") assert.throws(() => requireLane({ ...ready, [key]: false }, ready));
    }
    assert.throws(() => requireLane({ ...ready, jspiPromising: true }, ready));
    assert.throws(() => requireLane(ready, { ...ready, jspiSuspending: true }));
});

test("a hung guest fails on its deadline", async () => {
    await assert.rejects(within(new Promise(() => {}), 5), /timeout/);
    assert.equal(await within(Promise.resolve(42), 1000), 42);
});

test("v1 capabilities and command defaults are typed metadata",()=>{
    const manifest={version:1,lane:"sab-isolated-no-jspi",tests:[{...fixture(),console:true,http:true,commandDefaults:[{name:"show",argv0:"shown",args:["--main"],env:["B=default"]}]}]};
    validateManifest(manifest);
    for(const changes of [{http:"yes"},{console:1},{commandDefaults:[{name:"show",argv0:null,args:[],env:[],unknown:true}]},{commandDefaults:[{name:"show",argv0:1,args:[],env:[]}]}]) {
        assert.throws(()=>validateManifest({...manifest,tests:[{...manifest.tests[0],...changes}]}));
    }
});

test("network profiles and local relay URLs are explicit typed metadata",()=>{
    for(const network of [null,"classic","fidelity","absent","refused","disconnect","overflow","ingress","peer","switch4","switch6","ephemeral"]){
        validateManifest({version:1,lane:"sab-isolated-no-jspi",tests:[{...fixture(),network}]});
    }
    for(const network of [true,{},"wisp","skip"]){assert.throws(()=>validateManifest({version:1,lane:"sab-isolated-no-jspi",tests:[{...fixture(),network}]}));}
    for(const relayUrl of [1,"http://127.0.0.1:1","ws://example.com:1"]){assert.throws(()=>validateManifest({version:1,lane:"sab-isolated-no-jspi",tests:[{...fixture(),relayUrl}]}));}
});
