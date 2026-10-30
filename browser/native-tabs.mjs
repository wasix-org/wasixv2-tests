// Restore browser focus policy in isolated automation copies. Installed/shared
// tools remain unchanged; native Ctrl+T selects a real tab in the same window.
import { execFile } from "node:child_process";
import { cp, readFile, writeFile, mkdtemp, rm } from "node:fs/promises";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";
import { promisify } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";
import { firefox } from "playwright";

const exec = promisify(execFile);
const focusCommand = 'promises2.push(this._client.send("Emulation.setFocusEmulationEnabled", { enabled: true }));';

export function restoreChromiumFocus(source) {
    if (source.split(focusCommand).length !== 2) throw new Error("unsupported Playwright focus override layout");
    return source.replace(focusCommand, "promises2.push(Promise.resolve());");
}

export async function nativeTabs(browserName) {
    const directory = await mkdtemp(join(tmpdir(), "wasix-background-"));
    let context;
    const close = async () => { try { await context?.close(); } finally { await rm(directory, { recursive: true, force: true }); } };
    try {
        if (browserName === "chromium") {
            const modules = join(directory, "node_modules");
            for (const name of ["playwright", "playwright-core"]) {
                await cp(dirname(fileURLToPath(import.meta.resolve(name))), join(modules, name), { recursive: true });
            }
            const bundle = join(modules, "playwright-core/lib/coreBundle.js");
            await writeFile(bundle, restoreChromiumFocus(await readFile(bundle, "utf8")));
            const { chromium } = await import(pathToFileURL(join(modules, "playwright/index.mjs")));
            context = await chromium.launchPersistentContext(join(directory, "profile"), {
                headless: false, executablePath: process.env.CHROME,
                ignoreDefaultArgs: ["--disable-background-timer-throttling", "--disable-backgrounding-occluded-windows", "--disable-renderer-backgrounding"],
            });
        } else if (browserName === "firefox") {
            const { stdout } = await exec("python3", ["-B", fileURLToPath(new URL("./prepare-background-firefox.py", import.meta.url)),
                process.env.FIREFOX ?? firefox.executablePath(), join(directory, "browser")]);
            context = await firefox.launchPersistentContext(join(directory, "profile"), { headless: false, executablePath: stdout.trim(),
                firefoxUserPrefs: { "focusmanager.testmode": false } });
        } else throw new Error("unsupported background browser " + browserName);
        return { browser: context.browser(), context, close };
    } catch (error) { await close(); throw error; }
}

export async function openNativeTab(page, context, title) {
    await page.evaluate(title => { document.title = title; }, title);
    await page.bringToFront();
    const { stdout } = await exec("xdotool", ["search", "--sync", "--onlyvisible", "--name", "^" + title], { timeout: 5000 });
    await exec("xdotool", ["windowfocus", stdout.trim().split("\n")[0]]);
    const opened = context.waitForEvent("page", { timeout: 10000 });
    await exec("xdotool", ["key", "--clearmodifiers", "ctrl+t"]);
    return opened;
}
