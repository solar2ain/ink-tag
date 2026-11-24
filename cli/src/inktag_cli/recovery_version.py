"""Presentation metadata after the driver has verified a Recovery identity."""


def legacy_recovery_version() -> dict:
    """The human-defined version of a confirmed unversioned old Recovery."""
    return {"recoveryVersion": "1.0.0", "recoveryVersionSource": "legacy-default"}


def reported_recovery_version(version: str) -> dict:
    return {"recoveryVersion": version, "recoveryVersionSource": "device"}


def decode_ikrv(record: bytes) -> dict:
    """A present but malformed identity is an error, never a legacy default."""
    if len(record) not in (8, 14) or record[:4] != b"IKRV":
        raise ValueError("Malformed Recovery software version identity")
    major, minor, patch, epoch = record[4:8]
    if major == 0 or epoch != major:
        raise ValueError("Invalid Recovery software version/epoch identity")
    result = {**reported_recovery_version(f"{major}.{minor}.{patch}"), "recoveryEpoch": epoch}
    if len(record) == 14:
        result['signaturePolicy'] = decode_policy_extension(record[8:])
    return result


def decode_policy_extension(record: bytes) -> str:
    """Strict IKSP tail, at a known native boundary; no magic scanning."""
    policies = {0: 'optional', 1: 'required', 2: 'crc-only'}
    if len(record) != 6 or record[:5] != b'IKSP\x01' or record[5] not in policies:
        raise ValueError('Malformed Recovery integrity policy identity')
    return policies[record[5]]
