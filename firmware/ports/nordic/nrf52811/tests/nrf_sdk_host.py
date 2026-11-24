"""Small host harness helpers for the shipping Nordic SDK regressions."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def _function(source, declaration):
    start = source.index(declaration)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def _compile_run(tmp_path, name, source, *, includes=(), sources=(),
                 sanitizers=False, defines=()):
    compiler = shutil.which('gcc') or shutil.which('cc')
    if not compiler:
        raise RuntimeError('Host GCC is required; run the SDK regression in WSL/Linux')
    unit, executable = tmp_path / (name + '.c'), tmp_path / name
    unit.write_text(source, encoding='utf-8')
    command = [compiler, '-std=c99', '-Wall', '-Wextra', '-Werror']
    if sanitizers:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-fno-sanitize-recover=all']
    command += list(defines)
    command += ['-I' + str(ROOT / inc) for inc in includes]
    command += [str(ROOT / src) for src in sources]
    subprocess.run(command + [str(unit), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
