"""Configure an existing SWD session without resetting its debug power state."""
import ctypes


def configure_preserving_debug_state(link):
    # PyLink 1.7.0 ignores perform_tif_init=False in its SWD branch. On the
    # pinned DLL/probe, its empty Configure string clears DP power requests
    # and releases a previously halted CPU. Pass the supported SDK option
    # explicitly instead; never fall back to interface reinitialization.
    result = link._dll.JLINKARM_CORESIGHT_Configure(b'PerformTIFInit=0')
    if result != 0:
        raise RuntimeError(f'Preserving CoreSight configuration failed: {result}; no fallback')
    # Perform only the ADIv5 line synchronization: >=50 HIGH clocks followed
    # by >=2 idle LOW clocks. Unlike the DLL's general interface setup, this
    # does not write CTRL/STAT or remove debug-power requests. The caller's
    # next transaction must be its ordinary first DPIDR read. Never invoke
    # this to replay a failed AP/CPU/Flash operation inside a live transaction.
    direction = (ctypes.c_uint8 * 9)(*([0xFF] * 9))
    data = (ctypes.c_uint8 * 9)(*([0xFF] * 8 + [0]))
    result = link._dll.JLINK_SWD_StoreRaw(direction, data, 72)
    if result < 0:
        raise RuntimeError(f'SWD line synchronization failed: {result}; no fallback')
    link._dll.JLINK_SWD_SyncBits()
    link._coresight_configured = True
