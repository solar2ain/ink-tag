/* Shared decoder for the versioned InkTag OTA rejection record. */
(function (global) {
    'use strict';

    const REASONS = [
        'UNSPECIFIED', 'POWER_SAMPLE_INVALID', 'VOLTAGE_BELOW_MIN',
        'IMAGE_INFO_INVALID', 'ADDRESS_OR_LAYOUT_MISMATCH', 'ERASE_FAILED',
        'WRITE_FAILED', 'CHECKSUM_OR_SIGNATURE_FAILED', 'INTERNAL'
    ];
    const STAGES = [
        'UNKNOWN', 'MODE_SWITCH', 'PROGRAM_START', 'ERASE', 'PROGRAM_WRITE',
        'PROGRAM_END', 'IMAGE_VERIFY', 'SIGNATURE_VERIFY', 'METADATA_COMMIT',
        'BOOT_CONFIRM'
    ];

    function describe(operation, response, options = {}) {
        const data = response || new Uint8Array();
        const fallbackOffset = options.fallbackOffset ?? null;
        const fallback = fallbackOffset === null ? '' : `, offset=${fallbackOffset}`;
        const prefix = `target rejected ${operation}`;
        if (data.length < 15 || data[0] !== options.errorStatus ||
                data[1] !== 0x49 || data[2] !== 0x4b) {
            return `${prefix}: malformed rejection response${fallback}`;
        }
        if (data[3] !== 1) {
            return `${prefix}: unsupported rejection extension v${data[3]}${fallback}`;
        }
        const reason = REASONS[data[4]] || `UNKNOWN_0x${data[4].toString(16).padStart(2, '0').toUpperCase()}`;
        const stage = STAGES[data[5]] || `UNKNOWN_0x${data[5].toString(16).padStart(2, '0').toUpperCase()}`;
        const flags = data[6];
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        const detail = view.getUint32(7, true);
        const fields = [`reason=${reason}`, `stage=${stage}`];
        if (flags & 0x04) fields.push(`address=0x${detail.toString(16).padStart(8, '0').toUpperCase()}`);
        else if (flags & 0x08) fields.push(`offset=${detail}`);
        else if (fallbackOffset !== null) fields.push(`offset=${fallbackOffset}`);
        if (flags & 0x01) fields.push(`sampledMv=${view.getUint16(11, true)}`);
        if (flags & 0x02) fields.push(`thresholdMv=${view.getUint16(13, true)}`);
        return `${prefix}: ${fields.join(', ')}`;
    }

    global.InkTagOtaRejection = { describe };
})(typeof window !== 'undefined' ? window : globalThis);
