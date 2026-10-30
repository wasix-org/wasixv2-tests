#!/usr/bin/env python3
"""Restore real tab focus in an isolated copy of Playwright's Firefox.

Juggler forces every document focused/active in content/main.js. Remove only
those two automation overrides; leave browser timer and visibility code intact.
Never rewrite the installed/shared browser cache.
"""
import pathlib
import shutil
import sys
import zipfile


def prepare(executable, output):
    source = pathlib.Path(executable).resolve().parent
    output = pathlib.Path(output).resolve()
    if source == output or source in output.parents:
        raise ValueError("browser copy must be outside its source installation")
    archive = source / "omni.ja"
    name = "chrome/juggler/content/content/main.js"
    with zipfile.ZipFile(archive) as original:
        text = original.read(name)
        for override in [b"  docShell.overrideHasFocus = true;\n", b"  docShell.forceActiveState = true;\n"]:
            if text.count(override) != 1:
                raise ValueError("unsupported Juggler focus override layout")
            text = text.replace(override, b"")
    shutil.copytree(source, output)
    replacement = output / "omni-background.ja"
    with zipfile.ZipFile(archive) as original, zipfile.ZipFile(replacement, "w") as patched:
        for info in original.infolist():
            patched.writestr(info, text if info.filename == name else original.read(info.filename))
    replacement.replace(output / "omni.ja")
    return output / pathlib.Path(executable).name


if __name__ == "__main__":
    print(prepare(*sys.argv[1:]))
