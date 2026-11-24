"""Compile selected production C handlers with inert host adapters."""
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "compat/dialog/src/inktag_da_protocol_v2.inc"


def compile_run(tmp_path, args, sources, sanitize=False, capture=False):
    flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Icompat/common/ble", "-Icompat/common/protocol", *args]
    if sanitize:
        flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if shutil.which("gcc"):
        binary = tmp_path / "handlers.exe"
        subprocess.run(["gcc", *flags, *map(str, sources), "-o", str(binary)], cwd=ROOT, check=True)
        if capture:
            return subprocess.check_output([str(binary)], text=True)
        subprocess.run([str(binary)], check=True)
    else:
        def wsl(p):
            return subprocess.check_output(["wsl", "--exec", "wslpath", "-a", str(p)], text=True).strip()
        binary = wsl(tmp_path / "handlers")
        subprocess.run(["wsl", "--cd", wsl(ROOT), "--exec", "gcc", *flags,
                        *[wsl(p) if Path(p).is_absolute() else str(p) for p in sources],
                        "-o", binary], check=True)
        if capture:
            return subprocess.check_output(["wsl", "--exec", binary], text=True)
        subprocess.run(["wsl", "--exec", binary], check=True)


def function(text, name):
    # Match a definition, not an earlier forward declaration or call site.
    identifier = name.split("(", 1)[0].strip()
    match = re.search(r'(?m)^(?:__attribute__\(\([^\n]*\)\)\s+)?static [^\n]*\b' +
                      re.escape(identifier) + r'\s*\([^;{]*\)\s*\{', text)
    assert match, f"missing static definition: {identifier}"
    start = match.start()
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

