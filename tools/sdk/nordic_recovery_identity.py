"""Read the native Recovery software identity; no package/signing dependency."""
import json
import re


def recovery_identity(recovery, repo):
    version = json.loads((repo/'version.json').read_text()).get('recoveryVersion')
    if not isinstance(version, str) or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid Recovery release version')
    parts = [int(p) for p in version.split('.')]
    if parts[0] != 1 or any(p > 255 for p in parts):
        raise ValueError('Current Nordic Recovery requires epoch1 byte-sized version fields')
    expected = b'IKRV'+bytes(parts+[parts[0]])
    if recovery.count(b'IKRV') != 1 or recovery.count(expected) != 1:
        raise ValueError('Recovery load image lacks one matching release identity')
    return version, parts[0], recovery.index(expected)
