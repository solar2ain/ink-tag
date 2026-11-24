"""Named views of the OM ZKEL family description.

SDK consumers retain their fixed header filenames and include order. Exporting
one reviewed section avoids instantiating sibling/archived tables in each TU.
"""
from pathlib import Path


def compile_view(board: str) -> tuple[str, str]:
    # Registry membership alone must not grant a guessed electrical backend.
    views={
        'om6626_zkc':('om6626_zkel.h','app'),
        'om6626_zkcxxb':('om6626_zkel.h','app'),
        'om6626_zkc21b':('om6626_zkel.h','zkc21b'),
        'om6626_zkc42b':('om6626_zkel.h','zkc42b'),
        'om6626_zkc21v':('om6626_zkel.h','zkc21v'),
    }
    if board not in views:
        raise ValueError('unknown OM compile selector; no board backend')
    return views[board]


def section(path: Path, name: str) -> str:
    content = path.read_text(encoding="utf-8")
    begin, end = f"/* OM SECTION {name} BEGIN */\n", f"/* OM SECTION {name} END */"
    if (content.count(begin) != 1 or content.count(end) != 1 or
            content.index(begin) >= content.index(end)):
        raise ValueError(f"missing/ambiguous OM family section {name}: {path}")
    result = content.split(begin, 1)[1].split(end, 1)[0]
    if not result.strip():
        raise ValueError(f"empty OM family section {name}: {path}")
    return result
