"""Presentation metadata after the driver has verified a Recovery identity."""


def legacy_recovery_version() -> dict:
    """The human-defined version of a confirmed unversioned old Recovery."""
    return {"recoveryVersion": "1.0.0", "recoveryVersionSource": "legacy-default"}


def reported_recovery_version(version: str) -> dict:
    return {"recoveryVersion": version, "recoveryVersionSource": "device"}


def decode_ikrv(record: bytes) -> dict:
    """A present but malformed identity is an error, never a legacy default."""
    if len(record) != 8 or record[:4] != b"IKRV":
        raise ValueError("Malformed Recovery software version identity")
    major, minor, patch, epoch = record[4:]
    if major == 0 or epoch != major:
        raise ValueError("Invalid Recovery software version/epoch identity")
    return {**reported_recovery_version(f"{major}.{minor}.{patch}"), "recoveryEpoch": epoch}
