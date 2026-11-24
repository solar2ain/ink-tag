"""Resolve this independent repository's persistent primary checkout/SDK.

Read only the current checkout's Git pointer and commondir, never search other
projects or create SDK junctions. Windows worktrees also work from WSL.
"""
import argparse
import os
from pathlib import Path
import re


def native_path(text):
    if os.name != 'nt' and re.match(r'^[A-Za-z]:[\\/]', text):
        return Path('/mnt/' + text[0].lower() + '/' + text[3:].replace('\\', '/'))
    return Path(text)


def primary_checkout(root):
    root = Path(root).resolve()
    pointer = root / '.git'
    if pointer.is_dir() or not pointer.exists():
        return root  # Primary clone or an explicit source archive.
    text = pointer.read_text(encoding='utf-8').strip()
    if not text.startswith('gitdir: '):
        raise ValueError('Invalid checkout Git pointer')
    metadata = native_path(text[8:])
    if not metadata.is_absolute():
        metadata = root / metadata
    metadata = metadata.resolve()
    common = native_path((metadata / 'commondir').read_text(encoding='utf-8').strip())
    if not common.is_absolute():
        common = metadata / common
    common = common.resolve()
    if common.name != '.git' or not common.is_dir():
        raise ValueError('Checkout has no persistent primary .git directory')
    return common.parent


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--sdk', action='store_true')
    args = parser.parse_args()
    path = primary_checkout(args.repo)
    if args.sdk:
        path /= '.sdk/nordic/nRF5_SDK_17.1.0_ddde560'
    print(path.as_posix())
