import test from "node:test";
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";
import { restoreChromiumFocus } from "./native-tabs.mjs";

const exec = promisify(execFile);
const command = 'promises2.push(this._client.send("Emulation.setFocusEmulationEnabled", { enabled: true }));';
test("Chromium background driver removes exactly its automation focus override", () => {
    assert.equal(restoreChromiumFocus("before\n" + command + "\nafter"), "before\npromises2.push(Promise.resolve());\nafter");
    assert.throws(() => restoreChromiumFocus("unknown driver"), /unsupported/);
    assert.throws(() => restoreChromiumFocus(command + command), /unsupported/);
});

test("Firefox background preparation preserves installed bytes and all non-focus archive contents", async () => {
    const root = await mkdtemp(join(tmpdir(), "wasix-focus-unit-"));
    try {
        const script = fileURLToPath(new URL("./prepare-background-firefox.py", import.meta.url));
        await exec("python3", ["-B", "-c", `
import importlib.util, pathlib, sys, zipfile
root = pathlib.Path(sys.argv[1]); source = root / 'source'; source.mkdir()
(source / 'firefox').write_bytes(b'fake browser executable')
name = 'chrome/juggler/content/content/main.js'
a = b'  docShell.overrideHasFocus = true;\\n'
b = b'  docShell.forceActiveState = true;\\n'
text = b'before\\n' + a + b + b'after\\n'
with zipfile.ZipFile(source / 'omni.ja', 'w') as z:
    z.writestr(name, text); z.writestr('unrelated.js', b'unchanged timer/visibility engine data')
original = (source / 'omni.ja').read_bytes()
spec = importlib.util.spec_from_file_location('prepare', sys.argv[2]); module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
output = root / 'copy'; assert module.prepare(source / 'firefox', output) == output / 'firefox'
assert (source / 'omni.ja').read_bytes() == original
assert (output / 'firefox').read_bytes() == (source / 'firefox').read_bytes()
with zipfile.ZipFile(output / 'omni.ja') as z:
    assert z.read(name) == b'before\\nafter\\n'
    assert z.read('unrelated.js') == b'unchanged timer/visibility engine data'
try: module.prepare(source / 'firefox', source / 'nested')
except ValueError: pass
else: raise AssertionError('must refuse modifying source installation')
with zipfile.ZipFile(source / 'omni.ja', 'w') as z: z.writestr(name, b'unknown layout')
try: module.prepare(source / 'firefox', root / 'unsupported')
except ValueError: pass
else: raise AssertionError('must refuse unknown browser source layout')
assert not (root / 'unsupported').exists()
`, root, script]);
    } finally { await rm(root, { recursive: true, force: true }); }
});
