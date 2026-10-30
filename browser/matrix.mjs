// Each requested engine is a required lane: capability failures remain failures.
export const browserNames = ["chromium", "firefox", "webkit"];

export function selectCases(tests, { repeat = 1, filter } = {}) {
    if (!Number.isSafeInteger(repeat) || repeat < 1 || repeat > 10000) throw new Error("repeat must be an integer from 1 to 10000");
    const selected = filter ? tests.filter(({ name }) => name.includes(filter)) : tests;
    if (selected.length === 0) throw new Error("fixture selection is empty");
    return Array.from({ length: repeat }, (_, iteration) => selected.map((test) => ({ ...test,
        ...(repeat > 1 ? { name: `${test.name} [${iteration + 1}/${repeat}]` } : {}),
    }))).flat();
}

export function browserOptions(name, executablePath) {
    if (!browserNames.includes(name)) throw new Error(`unknown browser: ${name}`);
    return { headless: true, executablePath,
        ...(name === "chromium" ? { args: ["--disable-dev-shm-usage"] } : {}),
    };
}
