"""Exact source selection for the base implementation and private Pro increments.

Build-time only. Existing object basenames/TU boundaries can be preserved by
materializers; no runtime dispatch, directory probing or private fallback.
"""
import json
import hashlib
from pathlib import Path, PurePosixPath


class SourceProfileError(ValueError):
    pass


def _manifest(repo):
    manifest = json.loads((Path(repo) / 'firmware/source-profiles.json').read_text(encoding='utf-8'))
    if manifest.get('schemaVersion') != 1:
        raise SourceProfileError('Unsupported source profile manifest')
    return manifest


def source_path(repo, relative, *, profile):
    """Return a selected source Path, or None for a Pro-only base omission."""
    if profile not in ('base', 'pro'):
        raise SourceProfileError('Explicit base or pro source profile required')
    name = PurePosixPath(str(relative).replace('\\', '/'))
    if name.is_absolute() or '..' in name.parts or ':' in str(name):
        raise SourceProfileError('Repository-relative common source required')
    if name.parts[:2] != ('firmware', 'common') or len(name.parts) < 4:
        raise SourceProfileError('Only logical firmware/common sources may be selected')
    manifest = _manifest(repo)
    matches = [entry for entry in manifest['overrides'] if entry['logicalPath'] == name.as_posix()]
    if len(matches) > 1:
        raise SourceProfileError('Duplicate logical source mapping')
    selected = matches[0][profile] if matches else name.as_posix()
    if selected is None:
        return None
    selected_parts = PurePosixPath(selected).parts
    if (selected_parts[:2] not in (('firmware', 'common'), ('firmware', 'common_pro'))
            or '..' in selected_parts):
        raise SourceProfileError('Profile source must stay in common/common_pro')
    root = Path(repo).resolve()
    result = (root / selected).resolve()
    if not result.is_relative_to(root) or not result.is_file():
        raise SourceProfileError(f'Missing or invalid {profile} source: {selected}')
    if profile == 'base' and 'common_pro' in result.relative_to(root).parts:
        raise SourceProfileError('base source cannot depend on common_pro')
    return result


def source_paths(repo, relatives, *, profile):
    selected = [source_path(repo, relative, profile=profile) for relative in relatives]
    return [path for path in selected if path is not None]


def profile_defines(repo, *, profile):
    if profile not in ('base', 'pro'):
        raise SourceProfileError('Explicit base or pro source profile required')
    return dict(_manifest(repo)['profiles'][profile])


def source_selection(repo, relatives, *, profile):
    """Content-bound selection for materializer receipts and profile caches."""
    root = Path(repo).resolve()
    files = []
    basenames = {}
    for logical in relatives:
        path = source_path(root, logical, profile=profile)
        if path is None:
            continue
        relative = path.relative_to(root).as_posix()
        if path.name in basenames and basenames[path.name] != relative:
            raise SourceProfileError('Flat materialization would overwrite a source basename')
        basenames[path.name] = relative
        files.append(dict(logicalPath=str(logical).replace('\\', '/'), source=relative,
                          sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    return dict(schemaVersion=1, sourceProfile=profile,
                defines=profile_defines(root, profile=profile), files=files)
