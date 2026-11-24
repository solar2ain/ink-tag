/* Telink TLSR8359 fixed-Recovery transport for InkTag. */
(function (global) {
    'use strict';

    // Kept local because these adapters are also loaded as standalone raw scripts.
    function parseOtaManifest(bytes) {
        if (bytes.length > 64 * 1024) throw new Error('OTA manifest exceeds size limit');
        const text = new TextDecoder('utf-8', {fatal: true}).decode(bytes);
        const value = JSON.parse(text);
        if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('OTA manifest must be an object');
        const stack = [];
        let key = '';
        for (const match of text.matchAll(/"(?:[^"\\]|\\.)*"|[{}\[\]:]/g)) {
            const token = match[0];
            if (token === '{' || token === '[') {
                stack.push(token === '{' ? new Set() : null);
                if (stack.length > 32) throw new Error('OTA manifest nesting exceeds limit');
            } else if (token === '}' || token === ']') stack.pop();
            else if (token === ':') {
                const keys = stack[stack.length - 1];
                if (keys.has(key)) throw new Error('Duplicate OTA JSON field');
                keys.add(key);
            } else key = JSON.parse(token);
        }
        return value;
    }

    const SERVICE_UUID = '00010203-0405-0607-0809-0a0b0c0d1912';
    const CHARACTERISTIC_UUID = '00010203-0405-0607-0809-0a0b0c0d2b12';
    const APP_LIMIT = 0x3f000;
    const STATUS_RECEIVING = 1;
    const STATUS_COMPLETE = 2;
    const FAST_PACKET_MAX_BLOCKS = 14;
    const FAST_WINDOW_PACKETS = 2;
    const SIGNATURE_MAGIC = 0x31534b49;
    const SIGNATURE_VERSION = 1;
    const SIGNATURE_ALGORITHM_ECDSA_P256 = 1;
    const SIGNATURE_PREFIX_SIZE = 32;
    const SIGNATURE_SIZE = 64;
    const SIGNATURE_TRAILER_SIZE = SIGNATURE_PREFIX_SIZE + SIGNATURE_SIZE;
    const BOARD_SIGNATURE = 0x53484c54;
    const LAYOUT_SIGNATURE = 0x31564c54;
    const VERIFIER_SIGNATURE = 0x314b4c54;
    const SIGNED_BOARD = 'tlsr8359_hanshow';
    const SIGNED_LAYOUT = 'layout-v1';
    const TRUSTED_SIGNING_KEY = {
        trustProfile: 'production',
        algorithm: 'ecdsa-p256-sha256',
        keyId: 'inktag-tlsr8359-ota-prod-v1',
        publicKeyHash: 'f34e6d37e3d2b88454d01d149446dcc5'
    };

    function layoutProfile(layout, start = 0) {
        if (layout !== SIGNED_LAYOUT) throw new Error('not a supported TLSR8359 layout-v1 package');
        if (start === 0 || start === 0x1000) return { limit: APP_LIMIT, signature: LAYOUT_SIGNATURE, start };
        throw new Error('not a supported TLSR8359 fixed-Recovery layout');
    }

    function entryProof(start) {
        const bytes = new Uint8Array(20), view = new DataView(bytes.buffer);
        bytes.set([0x49,0x41,0x42,0x31]);
        [start, APP_LIMIT, 0x40000, 0x11000].forEach((n, i) => view.setUint32(4+i*4,n,true));
        return bytes;
    }

    function signedEntryProfile(layout, firmware) {
        if (nativeImageContext(firmware).layout !== LAYOUT_SIGNATURE) throw new Error('Unsupported native entry context');
        const at = 0x140, record = firmware.subarray(at, at+20);
        if (record[0] === 0x49 && record[1] === 0x41 && record[2] === 0x42 && record[3] === 0x31) {
            const body = new DataView(firmware.buffer,firmware.byteOffset,firmware.byteLength).getUint32(24,true);
            const expected = entryProof(0x1000);
            if (at+20 > body || record.length !== 20 || !record.every((v,i) => v === expected[i])) throw new Error('Invalid signed App entry address');
            return layoutProfile(layout, 0x1000);
        }
        if (unsignedImage(firmware)) throw new Error('Unsigned Telink App requires its native entry-address binding');
        return layoutProfile(layout);
    }

    function unsignedImage(firmware) {
        return firmware.length >= 36 && new DataView(firmware.buffer,firmware.byteOffset,firmware.byteLength).getUint32(24,true)+4 === firmware.length;
    }

    function nativeImageContext(firmware) {
        if (!unsignedImage(firmware)) return signedImageContext(firmware);
        const body=firmware.length-4, view=new DataView(firmware.buffer,firmware.byteOffset,firmware.byteLength);
        if (firmware.length>APP_LIMIT || !firmware.slice(8,12).every((v,i)=>v===[75,78,76,84][i]) ||
            view.getUint32(body,false)!==crc32(firmware.slice(0,body))) throw new Error('Unsigned Telink native CRC mismatch');
        return {epoch:1,board:BOARD_SIGNATURE,layout:LAYOUT_SIGNATURE};
    }

    function signedImageContext(firmware) {
        if (firmware.length < 128) throw new Error('TLSR8359 signed image is too short');
        const view = new DataView(firmware.buffer, firmware.byteOffset, firmware.byteLength);
        const appSize = view.getUint32(24, true) + 4;
        if (appSize + 96 !== firmware.length) throw new Error('TLSR8359 signed image length mismatch');
        return { epoch: view.getUint32(appSize + 12, true),
            board: view.getUint32(appSize + 16, true), layout: view.getUint32(appSize + 20, true) };
    }

    function validateRecoveryContext(raw, firmware) {
        if (raw.length !== 20 || raw[0] !== 0x49 || raw[1] !== 0x54 || raw[2] !== 0x43 || raw[3] !== 0x31) {
            throw new Error('TLSR8359 Recovery compatibility response is invalid');
        }
        const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
        const limit = view.getUint32(4, true), epoch = view.getUint32(8, true);
        const expected = nativeImageContext(firmware);
        if (limit !== APP_LIMIT || firmware.length > limit || epoch > 0xffff ||
            epoch !== expected.epoch || expected.layout !== LAYOUT_SIGNATURE || view.getUint32(12, true) !== expected.layout ||
            view.getUint32(16, true) !== expected.board) {
            throw new Error('TLSR8359 Recovery is incompatible with the signed image; Full is required');
        }
    }

    async function checkRecoveryCompatibility(service, firmware, step = async (_phase, operation) => operation()) {
        if (unsignedImage(firmware)) {
            const revision=await step('读取 Recovery 策略通道',()=>service.getCharacteristic('00010203-0405-0607-0809-0a0b0c0d2b15'));
            if (!revision.properties.read) throw new Error('当前 Recovery 要求发布者签名，未发送未签名固件');
            const value=await step('读取 Recovery 策略',()=>revision.readValue());
            const raw=new Uint8Array(value.buffer,value.byteOffset,value.byteLength);
            if(raw.length!==14 || !raw.slice(0,4).every((v,i)=>v===[73,75,82,86][i]) || !raw[4] || raw[7]!==raw[4] ||
                !raw.slice(8).every((v,i)=>v===[73,75,83,80,1,2][i]))
                throw new Error('当前 Recovery 要求发布者签名，未发送未签名固件');
        }
        let characteristic;
        try {
            characteristic = await step('读取兼容性通道', () => service.getCharacteristic('00010203-0405-0607-0809-0a0b0c0d2b13'));
        } catch (error) {
            if (!error || error.name !== 'NotFoundError') throw error;
            throw new Error('Recovery lacks read-only compatibility information; Full is required');
        }
        if (!characteristic.properties.read) throw new Error('Recovery compatibility characteristic is not readable');
        const value = await step('读取兼容性信息', () => characteristic.readValue());
        validateRecoveryContext(new Uint8Array(value.buffer, value.byteOffset, value.byteLength), firmware);
        const profile = signedEntryProfile(SIGNED_LAYOUT, firmware);
        const expected = entryProof(profile.start);
        let entry;
        try {
            entry = await step('读取启动地址通道', () => service.getCharacteristic('00010203-0405-0607-0809-0a0b0c0d2b14'));
        } catch (error) {
            if (!error || error.name !== 'NotFoundError') throw error;
            if (profile.start !== 0) throw new Error('Recovery lacks compiled entry-address proof; Full is required');
            return;
        }
        if (!entry.properties.read) throw new Error('Recovery entry-address proof is not readable');
        const address = await step('读取启动地址证明', () => entry.readValue());
        const raw = new Uint8Array(address.buffer,address.byteOffset,address.byteLength);
        if (raw.length !== 20 || !raw.every((v,i) => v === expected[i])) throw new Error('Recovery compiled entry address differs; Full is required');
    }

    function readStoredZip(buffer) {
        const bytes = new Uint8Array(buffer);
        if (bytes.length > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds archive limit');
        const view = new DataView(buffer);
        const decoder = new TextDecoder('utf-8', {fatal: true});
        const entries = new Map();
        const names = new Set();
        let totalSize = 0;
        let offset = 0;
        while (offset + 4 <= bytes.length && view.getUint32(offset, true) === 0x04034b50) {
            if (offset + 30 > bytes.length) throw new Error('ZIP local header is truncated');
            const flags = view.getUint16(offset + 6, true);
            const method = view.getUint16(offset + 8, true);
            const compressedSize = view.getUint32(offset + 18, true);
            const uncompressedSize = view.getUint32(offset + 22, true);
            const nameLength = view.getUint16(offset + 26, true);
            const extraLength = view.getUint16(offset + 28, true);
            if (flags & 0x08) throw new Error('ZIP data descriptors are not supported');
            if (method !== 0) throw new Error('TLSR8359 OTA package must use ZIP_STORED');
            const nameStart = offset + 30;
            const dataStart = nameStart + nameLength + extraLength;
            const dataEnd = dataStart + compressedSize;
            if (dataEnd > bytes.length) throw new Error('ZIP entry is truncated');
            const name = decoder.decode(bytes.subarray(nameStart, nameStart + nameLength));
            if (!name || /[\\:\x00]/.test(name) || name.split('/').some(p => !p || p === '.' || p === '..') || names.has(name.toLowerCase())) throw new Error('Unsafe or duplicate OTA ZIP member');
            names.add(name.toLowerCase());
            totalSize += uncompressedSize;
            if (entries.size >= 64 || compressedSize !== uncompressedSize || uncompressedSize > (name === 'manifest.json' ? 64 * 1024 : 4 * 1024 * 1024) || totalSize > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds member/decoded size limit');
            if (flags & 1) throw new Error('Encrypted OTA ZIP is unsupported');
            if (crc32(bytes.subarray(dataStart, dataEnd)) !== view.getUint32(offset + 14, true)) throw new Error('ZIP entry CRC mismatch');
            entries.set(name, bytes.slice(dataStart, dataEnd));
            offset = dataEnd;
        }
        return entries;
    }

    function hex(bytes) {
        return Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('');
    }

    function crc16(bytes) {
        let crc = 0xffff;
        for (const value of bytes) {
            let current = value;
            for (let bit = 0; bit < 8; bit++) {
                crc = (crc >>> 1) ^ (((crc ^ current) & 1) ? 0xa001 : 0);
                current >>>= 1;
            }
        }
        return crc & 0xffff;
    }

    function crc32(bytes) {
        let crc = 0xffffffff;
        for (const value of bytes) {
            crc ^= value;
            for (let bit = 0; bit < 8; bit++) {
                crc = ((crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)) >>> 0;
            }
        }
        return (~crc) >>> 0;
    }

    function dataPacket(index, payload) {
        if (!payload.length || payload.length > FAST_PACKET_MAX_BLOCKS * 16 || payload.length % 16) {
            throw new Error('TLSR8359 OTA payload must contain 1..14 complete blocks');
        }
        const packet = new Uint8Array(payload.length + 4);
        const view = new DataView(packet.buffer);
        view.setUint16(0, index, true);
        packet.set(payload, 2);
        view.setUint16(packet.length - 2, crc16(packet.subarray(0, packet.length - 2)), true);
        return packet;
    }

    async function verifyPublisherSignature(firmware, manifest, applicationSize, profile) {
        const signature = manifest.signature || {};
        const trailer = new DataView(
            firmware.buffer, firmware.byteOffset + applicationSize,
            SIGNATURE_TRAILER_SIZE);
        const publicKeyHex = String(signature.public_key || '').toLowerCase();
        if (!/^[0-9a-f]{130}$/.test(publicKeyHex)) {
            throw new Error('TLSR8359 OTA publisher public key is invalid');
        }
        const publicKey = Uint8Array.from(
            publicKeyHex.match(/../g), value => parseInt(value, 16));
        const publicKeyHash = new Uint8Array(
            await crypto.subtle.digest('SHA-256', publicKey));
        if (trailer.getUint32(0, true) !== SIGNATURE_MAGIC ||
            trailer.getUint8(4) !== SIGNATURE_VERSION ||
            trailer.getUint8(5) !== SIGNATURE_ALGORITHM_ECDSA_P256 ||
            trailer.getUint16(6, true) !== SIGNATURE_TRAILER_SIZE ||
            trailer.getUint32(8, true) !== applicationSize ||
            trailer.getUint32(12, true) !== manifest.compatibilityEpoch ||
            trailer.getUint32(16, true) !== BOARD_SIGNATURE ||
            trailer.getUint32(20, true) !== profile.signature ||
            trailer.getUint32(24, true) !== VERIFIER_SIGNATURE ||
            trailer.getUint32(28, true) !== manifest.releaseVersion ||
            signature.required !== true ||
            signature.trust_profile !== TRUSTED_SIGNING_KEY.trustProfile ||
            signature.algorithm !== TRUSTED_SIGNING_KEY.algorithm ||
            signature.key_id !== TRUSTED_SIGNING_KEY.keyId ||
            String(signature.public_key_hash || '').toLowerCase() !==
                TRUSTED_SIGNING_KEY.publicKeyHash ||
            signature.signed_context_size !== 16 ||
            signature.signed_board !== SIGNED_BOARD ||
            signature.signed_layout !== manifest.layout ||
            signature.board_signature !== BOARD_SIGNATURE ||
            signature.layout_signature !== profile.signature ||
            signature.verifier_signature !== VERIFIER_SIGNATURE ||
            signature.signed_prefix_size !== SIGNATURE_PREFIX_SIZE ||
            signature.signed_trailer_size !== SIGNATURE_TRAILER_SIZE ||
            publicKey[0] !== 0x04 ||
            hex(publicKeyHash.slice(0, 16)) !== TRUSTED_SIGNING_KEY.publicKeyHash) {
            throw new Error('TLSR8359 OTA publisher signature metadata is invalid');
        }
        const key = await crypto.subtle.importKey(
            'raw', publicKey, { name: 'ECDSA', namedCurve: 'P-256' }, false,
            ['verify']);
        const valid = await crypto.subtle.verify(
            { name: 'ECDSA', hash: 'SHA-256' }, key,
            firmware.slice(firmware.length - SIGNATURE_SIZE),
            firmware.slice(0, applicationSize + SIGNATURE_PREFIX_SIZE));
        if (!valid) throw new Error('TLSR8359 OTA publisher signature verification failed');
    }

    async function parsePackage(file) {
        if (file.size > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds archive limit before reading');
        const buffer = await file.arrayBuffer();
        let firmware;
        let manifest = null;
        let profile;
        if (buffer.byteLength >= 4 && new DataView(buffer).getUint32(0, true) === 0x04034b50) {
            const entries = readStoredZip(buffer);
            const manifestBytes = entries.get('manifest.json');
            if (!manifestBytes) throw new Error('manifest.json is missing');
            manifest = parseOtaManifest(manifestBytes);
            profile = layoutProfile(manifest.layout);
            if (manifest.format !== 'inktag-ota-v1' ||
                manifest.transport !== 'telink-fixed-recovery-v2' ||
                manifest.board !== SIGNED_BOARD) {
                throw new Error('not an InkTag Hanshow TLSR8359 fixed-Recovery package');
            }
            const application = manifest.application || {};
            firmware = entries.get(application.file);
            if (!firmware) throw new Error('application payload is missing');
            profile = signedEntryProfile(manifest.layout, firmware);
            const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', firmware));
            if (hex(digest) !== String(application.sha256 || '').toLowerCase()) {
                throw new Error('package SHA-256 verification failed');
            }
        } else throw new Error('TLSR8359 OTA requires a signed manifest-bearing ZIP package');
        const unsigned=manifest.signature?.required===false;
        if (unsigned && (manifest.edition!=='InkTag' || Object.keys(manifest.signature).length!==3 ||
            manifest.signature.algorithm!=='none' || manifest.signature.policy!=='crc-only' ||
            manifest.compatibilityEpoch!==1 || !unsignedImage(firmware))) throw new Error('TLSR 未签名 InkTag 包格式无效');
        if (firmware.length < 32 + (unsigned?4:SIGNATURE_TRAILER_SIZE) || firmware.length > profile.limit) {
            throw new Error(`TLSR8359 应用大小 ${firmware.length} 超出单槽限制 ${profile.limit}`);
        }
        if (firmware[8] !== 0x4b || firmware[9] !== 0x4e ||
            firmware[10] !== 0x4c || firmware[11] !== 0x54) {
            throw new Error('固件缺少 Telink TLNK 启动标记');
        }
        const view = new DataView(firmware.buffer, firmware.byteOffset, firmware.byteLength);
        const declaredSize = view.getUint32(24, true);
        const applicationSize = declaredSize + 4;
        if (!Number.isInteger(manifest.releaseVersion) ||
            manifest.releaseVersion <= 0 || manifest.releaseVersion > 0xffffffff ||
            !Number.isInteger(manifest.compatibilityEpoch) ||
            manifest.compatibilityEpoch < 0 || manifest.compatibilityEpoch > 0xffff) {
            throw new Error('TLSR8359 release version or compatibility epoch is invalid');
        }
        if (applicationSize + (unsigned?0:SIGNATURE_TRAILER_SIZE) !== firmware.length) {
            throw new Error('Telink 固件声明长度与文件长度不一致');
        }
        const expectedCrc = view.getUint32(declaredSize, false);
        const actualCrc = crc32(firmware.subarray(0, declaredSize));
        if (expectedCrc !== actualCrc) throw new Error('Telink 固件 CRC32 校验失败');
        if (!unsigned) await verifyPublisherSignature(firmware, manifest, applicationSize, profile);
        if (manifest) {
            const application = manifest.application;
            if (application.size !== applicationSize ||
                application.transfer_size !== firmware.length ||
                application.declared_size !== declaredSize ||
                application.crc32 !== actualCrc ||
                application.active_address !== profile.start) {
                throw new Error('TLSR8359 application/Recovery layout does not match the manifest');
            }
        }
        return { fileName: file.name, manifest, firmware, declaredSize, crc32: actualCrc, signatureStatus:unsigned?'unsigned':'verified' };
    }

    const delay = (milliseconds) => new Promise(resolve => setTimeout(resolve, milliseconds));
    const FLASH_SECTOR_BLOCKS = 0x1000 / 16;

    function fastBlockLimit(status) {
        if (status.length !== 14 || status[8] !== 0x49 || status[9] !== 0x54 ||
            status[10] !== 1 || status[11] < 2 || status[11] > FAST_PACKET_MAX_BLOCKS) {
            throw new Error('TLSR8359 Recovery 状态格式不是 release-v1，请先通过 SWS 完整烧录当前固件');
        }
        return Math.min(status[11], FAST_PACKET_MAX_BLOCKS);
    }

    function fastPacketEnd(start, total, blockLimit) {
        if (start < 0 || start >= total || blockLimit < 2) {
            throw new Error('TLSR8359 fast OTA packet arguments are outside the image');
        }
        const sectorEnd = (Math.floor(start / FLASH_SECTOR_BLOCKS) + 1) * FLASH_SECTOR_BLOCKS;
        // The first acknowledged packet carries the complete 32-byte Telink
        // header so Recovery can reject marker/size errors before first erase.
        if (start === 0) return Math.min(start + 2, total);
        if (start % FLASH_SECTOR_BLOCKS === 0) return Math.min(start + 1, total);
        return Math.min(start + blockLimit, sectorEnd, total);
    }

    function fastPacketWindow(start, total, blockLimit, packetLimit = FAST_WINDOW_PACKETS) {
        if (packetLimit < 1 || packetLimit > FAST_WINDOW_PACKETS) {
            throw new Error('TLSR8359 OTA packet window limit is invalid');
        }
        const ranges = [];
        let current = start;
        for (let packet = 0; packet < packetLimit; packet++) {
            const end = fastPacketEnd(current, total, blockLimit);
            ranges.push([current, end]);
            current = end;
            if (current >= total || start % FLASH_SECTOR_BLOCKS === 0 ||
                current % FLASH_SECTOR_BLOCKS === 0) break;
        }
        return ranges;
    }

    function packetRequiresDeliveryAck(index) {
        return index % FLASH_SECTOR_BLOCKS === 0;
    }

    function characteristicSupportsWriteResponse(characteristic) {
        return Boolean(characteristic && characteristic.properties &&
            characteristic.properties.write);
    }

    function isTransientGattError(error) {
        if (error && ['TLSR_RECOVERY_DISCOVERY_TIMEOUT', 'TLSR_FINALIZATION_UNKNOWN'].includes(error.code)) return false;
        const name = String(error && error.name || '').toLowerCase();
        const detail = String(error && error.message || error || '').toLowerCase();
        return name === 'networkerror' || name === 'invalidstateerror' ||
            name === 'aborterror' || detail.includes('gatt server is disconnected') ||
            detail.includes('unreachable') || detail.includes('device disconnected') ||
            detail.includes('未确认数据块');
    }

    // One owner per exact browser grant. Native promises remain quarantined
    // after timeout until their real settlement; a UI deadline cannot cancel them.
    const discoveryOwners = new Map();
    const verifiedLinks = new WeakMap();
    const recoverySessions = new WeakMap();
    const deviceKey = device => device.id || device;
    const sessionError = (message, code = 'TLSR_SESSION_INVALID') => Object.assign(new Error(message), { code });

    function assertDeviceAvailable(device) {
        const owner = discoveryOwners.get(deviceKey(device));
        if (owner?.pending || owner?.active) throw sessionError(
            'TLSR 原生操作尚未结束或同设备会话仍被占用；未重试连接',
            owner.pending ? 'TLSR_NATIVE_PENDING' : 'TLSR_SESSION_BUSY');
    }

    function cancelRecoveryDiscovery(device) {
        assertDeviceAvailable(device);
        verifiedLinks.delete(device.gatt);
    }

    async function recoveryCharacteristic(device, onPhase = () => {}, timeoutMs = 45000, context = {}) {
        assertDeviceAvailable(device);
        const key = deviceKey(device), server = device.gatt;
        const isCurrent = context.isCurrent || (() => true);
        const owner = { active: true, finished: false, pending: 0, programming: false };
        discoveryOwners.set(key, owner);
        const deadline = Date.now() + timeoutMs;
        const current = () => discoveryOwners.get(key) === owner && owner.active && isCurrent();
        const disconnectOwned = () => {
            // A cancelled generation may still produce a late native server.
            // Quarantine prevents a new owner until that promise settles. Only
            // this exact map owner may close/delete its link, never a successor.
            if (discoveryOwners.get(key) === owner) {
                if (server.connected) server.disconnect();
                verifiedLinks.delete(server);
            }
        };
        const retire = () => {
            if (owner.finished && !owner.pending && discoveryOwners.get(key) === owner) discoveryOwners.delete(key);
        };
        const finish = () => { owner.active = false; owner.finished = true; retire(); };
        const abort = () => { owner.active = false; disconnectOwned(); };
        const note = text => { try { onPhase(text); } catch (_) { /* diagnostics only */ } };
        const step = async (phase, operation, write = false) => {
            if (!current()) throw sessionError('TLSR 会话已取消；未继续本次操作');
            if (!owner.programming) note(`TLSR Recovery：${phase}`);
            const phaseLimit = phase === '发现升级服务' ? timeoutMs : phase === '建立 GATT 连接' ? 10000 : 5000;
            const remaining = owner.programming ? 10000 : Math.min(phaseLimit, deadline - Date.now());
            const phaseStarted = Date.now();
            if (remaining <= 0) { abort(); throw sessionError('TLSR Recovery 发现超时', 'TLSR_RECOVERY_DISCOVERY_TIMEOUT'); }
            return new Promise((resolve, reject) => {
                let expired = false;
                const timer = setTimeout(() => {
                    expired = true; abort();
                    const error = sessionError(`TLSR Recovery ${phase}超时；结果未知，未重放`,
                        write ? 'TLSR_OTA_RESULT_UNKNOWN' : 'TLSR_RECOVERY_DISCOVERY_TIMEOUT');
                    note(error.message); reject(error);
                }, remaining);
                owner.pending++;
                Promise.resolve().then(() => {
                    if (!current()) throw sessionError('TLSR 会话已取消');
                    return operation();
                }).then(value => {
                    clearTimeout(timer);
                    if (expired || !current()) {
                        disconnectOwned();
                        if (!expired) reject(sessionError('TLSR 会话代际已变化'));
                    } else {
                        if (!owner.programming) note(`TLSR Recovery：${phase}完成（${Date.now() - phaseStarted} ms）`);
                        resolve(value);
                    }
                }, error => {
                    clearTimeout(timer);
                    if (expired || !current()) disconnectOwned();
                    if (!expired) reject(error);
                }).finally(() => { owner.pending--; retire(); });
            });
        };
        const scope = { step, finish, abort, owner, device, disconnectOwned };
        try {
            // Reuse the exact live server/characteristic verified by the ordinary
            // client. Never disconnect or rediscover that already proven link.
            const cached = server.connected && verifiedLinks.get(server);
            if (cached) return { ...scope, characteristic: cached.characteristic, service: cached.service };
            for (let attempt = 0; ; attempt++) {
                try {
                    if (!server.connected) await step('建立 GATT 连接', () => server.connect());
                    const service = await step('发现升级服务', () => server.getPrimaryService(SERVICE_UUID));
                    const characteristic = await step('发现数据通道', () => service.getCharacteristic(CHARACTERISTIC_UUID));
                    return { ...scope, characteristic, service };
                } catch (error) {
                    // WinRT can resolve connect() against the retiring App link,
                    // then explicitly reject discovery when that link drops.
                    // Only this known, settled, pre-write handoff failure gets
                    // one fresh discovery. Native timeouts stay quarantined;
                    // no entry, compatibility check or image write is replayed.
                    if (attempt || !context.afterAppEntry || !current() ||
                            server.connected || !isTransientGattError(error)) throw error;
                    note('TLSR Recovery：App 旧连接已断开，等待后重新发现一次（尚未发送固件）');
                    await delay(900);
                }
            }
        } catch (error) { abort(); finish(); throw error; }
    }

    async function acquireRecoverySession(context, pkg) {
        const { device, generation, isCurrent, onStage } = context;
        if (!device?.id || !device.gatt || !Number.isSafeInteger(generation) || generation < 0 ||
            typeof isCurrent !== 'function' || !isCurrent() || !(pkg?.firmware instanceof Uint8Array)) {
            throw sessionError('TLSR Recovery 缺少精确授权、当前代际或已核验包');
        }
        const handle = Object.freeze({});
        const discovery = await recoveryCharacteristic(device, onStage, 45000, context);
        const record = { discovery, context, original: pkg, firmware: pkg.firmware.slice(), state: 'acquiring', release: null };
        recoverySessions.set(handle, record);
        try {
            await checkRecoveryCompatibility(discovery.service, record.firmware, discovery.step);
            verifiedRecoveryStatus(await discovery.step('读取升级状态', () => readStatus(discovery.characteristic)));
            const native = discovery.characteristic;
            discovery.characteristic = {
                service: native.service, properties: native.properties,
                readValue: () => discovery.step('读取升级状态', () => native.readValue()),
                writeValueWithResponse: bytes => discovery.step('写入升级数据', () => writeWithResponse(native, bytes), true),
                writeValueWithoutResponse: bytes => discovery.step('写入升级数据', () => writeWithoutResponse(native, bytes), true),
                addEventListener: (...args) => native.addEventListener(...args),
                removeEventListener: (...args) => native.removeEventListener(...args),
                startNotifications: () => discovery.step('开启升级通知', () => native.startNotifications()),
                stopNotifications: () => discovery.step('关闭升级通知', () => native.stopNotifications()),
            };
            record.state = 'acquired';
            return handle;
        } catch (error) { discovery.abort(); try { await releaseRecoverySession(handle); } catch (_) { /* preserve cause */ } throw error; }
    }

    async function releaseRecoverySession(handle) {
        const record = recoverySessions.get(handle);
        if (!record) throw sessionError('无效 TLSR 会话');
        if (record.release) return record.release;
        record.release = Promise.resolve().then(() => {
            record.discovery.abort(); record.discovery.finish(); record.state = 'released';
            if (record.discovery.owner.pending) throw sessionError('TLSR 原生调用仍待真实结束；同授权隔离中', 'TLSR_NATIVE_PENDING');
        });
        return record.release;
    }

    async function transferRecoverySession(handle, pkg, onProgress = () => {}) {
        const record = recoverySessions.get(handle);
        if (!record || record.state !== 'acquired') throw sessionError('TLSR 会话无效或已消费');
        record.state = 'transferring';
        let failure;
        try {
            if (!record.context.isCurrent() || pkg !== record.original ||
                pkg.firmware.length !== record.firmware.length ||
                !record.firmware.every((value, index) => value === pkg.firmware[index])) throw sessionError('TLSR 代际或固件包已改变');
            record.discovery.owner.programming = true;
            // A normal explicitly requested update starts the selected package
            // once. A remote RECEIVING index alone cannot identify its prefix.
            return await transferDeviceOnce(record.context.device, { ...pkg, firmware: record.firmware }, onProgress, false,
                { onDiagnostics: record.context.onDiagnostics }, record.discovery);
        } catch (error) { failure = error; throw error; }
        finally {
            try { await releaseRecoverySession(handle); } catch (error) { if (!failure) throw error; }
        }
    }

    async function writeWithoutResponse(characteristic, bytes) {
        if (characteristic.writeValueWithoutResponse) {
            return characteristic.writeValueWithoutResponse(bytes);
        }
        return characteristic.writeValue(bytes);
    }

    async function writeWithResponse(characteristic, bytes) {
        if (characteristic.writeValueWithResponse) {
            return characteristic.writeValueWithResponse(bytes);
        }
        return characteristic.writeValue(bytes);
    }

    async function readStatus(characteristic) {
        const value = await characteristic.readValue();
        const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        return bytes.slice();
    }

    function verifiedRecoveryStatus(raw) {
        if (raw.length !== 14 || raw[8] !== 0x49 || raw[9] !== 0x54 ||
            raw[10] !== 1 || raw[11] < 2 || raw[11] > FAST_PACKET_MAX_BLOCKS) {
            throw new Error('TLSR8359 固定 Recovery 状态/版本无效；需要当前 v1 Recovery');
        }
        // A refusal is readable state, not proof of an unusable BLE link. This
        // wire has no voltage value or sample-validity field; never invent one.
        return { status: raw[0], raw };
    }

    async function queryRecovery(server) {
        if (!server.device) {
            const service = await server.getPrimaryService(SERVICE_UUID);
            const characteristic = await service.getCharacteristic(CHARACTERISTIC_UUID);
            const proof = verifiedRecoveryStatus(await readStatus(characteristic));
            verifiedLinks.set(server, { service, characteristic });
            return proof;
        }
        const scope = await recoveryCharacteristic(server.device);
        try {
            const proof = verifiedRecoveryStatus(await scope.step('读取升级状态', () => readStatus(scope.characteristic)));
            verifiedLinks.set(server, { service: scope.service, characteristic: scope.characteristic });
            return proof;
        } catch (error) { scope.abort(); throw error; }
        finally { scope.finish(); }
    }

    // Read-only diagnostic retains the exact owner until native settlement.
    async function queryRecoveryDevice(device, options = {}) {
        const discovery = await recoveryCharacteristic(device, options.onPhase);
        try { return verifiedRecoveryStatus(await discovery.step('读取升级状态', () => readStatus(discovery.characteristic))); }
        finally { discovery.disconnectOwned(); discovery.finish(); }
    }

    async function resumeApplication(server) {
        const service = await server.getPrimaryService(SERVICE_UUID);
        const characteristic = await service.getCharacteristic(CHARACTERISTIC_UUID);
        const initial = verifiedRecoveryStatus(await readStatus(characteristic));
        if (initial.status === 8) {
            throw new Error('已有上一次退出 Recovery 的终态；请只读确认当前角色，不要重复退出或复位');
        }
        let reply;
        let active = false;
        let subscribed = false;
        let waiting = null;
        const updates = [];
        const onStatus = event => {
            if (!active) return;
            const value = event.target.value;
            const update = new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
            if (waiting) waiting(update);
            else updates.push(update);
        };
        const nextStatus = timeoutMs => {
            if (updates.length) return Promise.resolve(updates.shift());
            return new Promise(resolve => {
                const deliver = value => {
                    clearTimeout(timer);
                    waiting = null;
                    resolve(value);
                };
                const timer = setTimeout(() => deliver(null), Math.max(0, timeoutMs));
                waiting = deliver;
            });
        };
        characteristic.addEventListener('characteristicvaluechanged', onStatus);
        try {
            await characteristic.startNotifications();
            subscribed = true;
            active = true;
            // Exactly one guarded request. Never reconnect/replay or fall back
            // to reset. Recovery revalidates the complete signed App and power.
            await writeWithResponse(characteristic, new Uint8Array([0x03, 0xff]));
            const deadline = Date.now() + 15000;
            while (Date.now() < deadline) {
                const raw = await nextStatus(deadline - Date.now());
                if (!raw) throw new Error('等待本轮终态通知超时');
                reply = verifiedRecoveryStatus(raw);
                // Recovery uses one coalescing pending flag. An intermediate
                // RECEIVING notification is not a required wire phase.
                if (reply.status === STATUS_RECEIVING) continue;
                break;
            }
            if (!reply || reply.status === STATUS_RECEIVING) throw new Error('退出 Recovery 验证超时');
        } catch (cause) {
            const error = new Error(`退出 Recovery 结果未知；请只读确认设备角色，不要重复退出或复位：${String(cause)}`);
            error.code = 'RECOVERY_RESULT_UNKNOWN';
            throw error;
        } finally {
            active = false;
            characteristic.removeEventListener('characteristicvaluechanged', onStatus);
            updates.length = 0;
            if (subscribed && server.connected) {
                try { await characteristic.stopNotifications(); } catch (_) {
                    // Cleanup cannot replace a primary refusal/unknown result.
                    // The caller still performs exact App discovery/attestation.
                }
            }
        }
        if (reply.status === 8) return;
        if (reply.status === 9) {
            throw new Error('SAFE_RESUME_APP 拒绝：LOW_POWER（电压不足或采样无效），Recovery 仍可连接；请检查供电后再处理');
        }
        if (reply.status === 10) {
            throw new Error('无法安全返回原应用，原应用或更新状态不完整（UNSAFE_RESUME）；请重新选择与设备匹配的 OTA 应用文件，并完成 OTA 升级');
        }
        if (reply.status === STATUS_RECEIVING) {
            const error = new Error('退出 Recovery 验证超时，结果未知；请只读确认设备角色，不要重复退出或复位');
            error.code = 'RECOVERY_RESULT_UNKNOWN';
            throw error;
        }
        throw new Error(`SAFE_RESUME_APP 拒绝：Recovery 状态 ${hex(reply.raw)}`);
    }

    function acknowledgedIndex(status) {
        if (status.length < 3) {
            throw new Error('TLSR8359 Recovery 版本过旧，请先使用受保护的首次烧录工具更新 Recovery');
        }
        return status[1] | (status[2] << 8);
    }

    async function transferDeviceOnce(device, pkg, onProgress, resume = false, options = {}, prepared = null) {
        const discovery = prepared || await recoveryCharacteristic(device, options.onPhase);
        const characteristic = discovery.characteristic;
        let notificationsStarted = false;
        let finalDetach = null;
        const stopNotifications = async () => {
            if (notificationsStarted && device.gatt.connected) {
                try { await characteristic.stopNotifications(); } catch (_) {
                    // Keep primary failures; the outer exact App attestation
                    // still decides success if the CCC disconnect races.
                }
            }
            notificationsStarted = false;
        };
        try {
        if (!prepared) await checkRecoveryCompatibility(characteristic.service, pkg.firmware, discovery.step);
        const useWriteResponse = characteristicSupportsWriteResponse(characteristic);
        let useNotifications = Boolean(characteristic.properties &&
            characteristic.properties.notify);
        if (!useNotifications) throw new Error('TLSR8359 Recovery 必须支持终态通知；尚未发送 BEGIN 或固件数据');
        const notificationQueue = [];
        let notificationWaiter = null;
        const onStatus = event => {
            const value = event.target.value;
            const update = new Uint8Array(
                value.buffer, value.byteOffset, value.byteLength,
            ).slice();
            if (notificationWaiter) notificationWaiter(update);
            else notificationQueue.push(update);
        };
        const detachNotifications = () => {
            characteristic.removeEventListener('characteristicvaluechanged', onStatus);
            notificationQueue.length = 0;
        };
        finalDetach = detachNotifications;
        const nextNotification = async timeoutMs => {
            if (notificationQueue.length) return notificationQueue.shift();
            return new Promise(resolve => {
                let timer;
                const deliver = value => {
                    clearTimeout(timer);
                    notificationWaiter = null;
                    resolve(value);
                };
                notificationWaiter = deliver;
                timer = setTimeout(() => {
                    if (notificationWaiter === deliver) notificationWaiter = null;
                    resolve(null);
                }, timeoutMs);
            });
        };
        const beginTransfer = async () => {
            const begin = new Uint8Array([0x01, 0xff]);
            if (useWriteResponse) await writeWithResponse(characteristic, begin);
            else await writeWithoutResponse(characteristic, begin);
            await delay(250);
            const startedStatus = await readStatus(characteristic);
            if (startedStatus[0] !== STATUS_RECEIVING || acknowledgedIndex(startedStatus) !== 0) {
                throw new Error('TLSR8359 Recovery 拒绝开始传输');
            }
            return startedStatus;
        };
        const blocks = Math.ceil(pkg.firmware.length / 16);
        // Every shipping release-v1 Recovery declares NOTIFY. Verify its CCC
        // before any destructive BEGIN; a read-only peer is not qualified for
        // the guarded return path. Observe initial reads without a listener.
        await discovery.step('开启升级通知', () => characteristic.startNotifications());
        notificationsStarted = true;
        let status = resume ? await discovery.step('读取升级状态', () => readStatus(characteristic)) : await beginTransfer();
        if (resume && status[0] === STATUS_COMPLETE) {
            throw sessionError('Recovery 有上一次 COMPLETE 结果；请只读确认应用，不能以缓存读取代替本轮终态通知', 'TLSR_FINALIZATION_UNKNOWN');
        }
        if (resume && (status[0] !== STATUS_RECEIVING ||
            acknowledgedIndex(status) > blocks)) {
            status = await beginTransfer();
        }
        const fastBlocks = fastBlockLimit(status);
        const started = performance.now();
        const quarterSeconds = [];
        let packets = 0, notificationFallbacks = 0, notificationRecoveries = 0, notificationMisses = 0;
        // Web Bluetooth also dispatches characteristicvaluechanged for readValue().
        // Observe the initial/BEGIN state before subscribing so its old IDLE or
        // BEGIN snapshot cannot be consumed as a later data acknowledgement.
        try {
            if (useNotifications) {
                characteristic.addEventListener('characteristicvaluechanged', onStatus);
            }
            for (let index = acknowledgedIndex(status); index < blocks;) {
                // Web Bluetooth/WinRT may drop the second queued no-response
                // write, even on a fresh transfer. Wait for each indexed packet
                // to commit; keep large packets and sector boundaries unchanged.
                const packetRanges = fastPacketWindow(
                    index, blocks, fastBlocks, 1,
                );
                for (const [packetStart, packetEnd] of packetRanges) {
                    const payload = new Uint8Array((packetEnd - packetStart) * 16).fill(0xff);
                    payload.set(pkg.firmware.subarray(
                        packetStart * 16,
                        Math.min(packetEnd * 16, pkg.firmware.length),
                    ));
                    const packet = dataPacket(packetStart, payload);
                    packets++;
                    if (useWriteResponse && packetRequiresDeliveryAck(packetStart)) {
                        await writeWithResponse(characteristic, packet);
                    } else {
                        await writeWithoutResponse(characteristic, packet);
                    }
                }
                const batchEnd = packetRanges[packetRanges.length - 1][1];
                const opensSector = packetRanges[0][0] % FLASH_SECTOR_BLOCKS === 0;
                // A notification carries the exact committed index, including
                // sector erase completion. Only read polling needs the fixed
                // erase allowance after actual controller delivery.
                if (opensSector && !useNotifications) await delay(300);
                const deadline = performance.now() + 3000;
                let packetNotifications = useNotifications;
                for (;;) {
                    if (packetNotifications) {
                        status = await nextNotification(Math.max(0, deadline - performance.now()));
                        if (!status) {
                            notificationFallbacks++;
                            notificationMisses++;
                            packetNotifications = false;
                            // One lost notification is not proof that the whole
                            // subscription failed. Read this committed index,
                            // then allow the next packet's normal notification.
                            // Two consecutive misses retain bounded read fallback.
                            if (notificationMisses >= 2) {
                                useNotifications = false;
                                detachNotifications();
                            }
                            if (opensSector) await delay(300);
                            status = await readStatus(characteristic);
                        }
                    } else status = await readStatus(characteristic);
                    if (status[0] !== STATUS_RECEIVING) {
                        throw new Error(`TLSR8359 Recovery 在数据块 ${index}..${batchEnd - 1} 拒绝传输（${hex(status)}）`);
                    }
                    const acknowledged = acknowledgedIndex(status);
                    if (acknowledged === batchEnd) {
                        if (packetNotifications && notificationMisses) {
                            notificationRecoveries++; notificationMisses = 0;
                        }
                        break;
                    }
                    if (acknowledged > batchEnd) {
                        throw new Error(`TLSR8359 Recovery 返回超前确认 ${acknowledged}，当前数据块结束于 ${batchEnd}（${hex(status)}）`);
                    }
                    if (performance.now() >= deadline) {
                        throw new Error(`TLSR8359 Recovery 未确认数据块 ${index}..${batchEnd - 1}（${hex(status)}）`);
                    }
                    if (!packetNotifications) await delay(5);
                }
                while (quarterSeconds.length < 3 && batchEnd >= blocks * (quarterSeconds.length + 1) / 4) {
                    quarterSeconds.push(Number(((performance.now() - started) / 1000).toFixed(3)));
                }
                if (index === 0 || batchEnd % 64 === 0 || batchEnd === blocks) {
                    onProgress(Math.min(batchEnd * 16, pkg.firmware.length), pkg.firmware.length, started);
                }
                index = batchEnd;
            }
            const dataSeconds = Number(((performance.now() - started) / 1000).toFixed(3));
            // The final data read is made with the listener detached. Web
            // Bluetooth readValue events must not masquerade as notifications.
            detachNotifications();
            await delay(50);
            status = await readStatus(characteristic);
            if (status[0] !== STATUS_RECEIVING || acknowledgedIndex(status) !== blocks) {
                throw new Error('TLSR8359 Recovery 在结束前拒绝传输');
            }
            characteristic.addEventListener('characteristicvaluechanged', onStatus);
            const end = new Uint8Array(6);
            const endView = new DataView(end.buffer);
            const maximum = blocks - 1;
            end.set([0x02, 0xff]);
            endView.setUint16(2, maximum, true);
            endView.setUint16(4, maximum ^ 0xffff, true);
            try {
                if (useWriteResponse) await writeWithResponse(characteristic, end);
                else await writeWithoutResponse(characteristic, end);
            } catch (cause) {
                throw sessionError(`END 结果未知，不能重放 END 或固件：${String(cause)}`, 'TLSR_FINALIZATION_UNKNOWN');
            }
            // Recovery hashes committed Flash in cooperative 64-byte slices, then
            // performs one P-256 verification. RECEIVING remains the truthful
            // status until both checks finish.
            const validationDeadline = performance.now() + 10000;
            for (;;) {
                if (performance.now() >= validationDeadline || (!notificationQueue.length && !device.gatt.connected)) {
                    throw sessionError('TLSR8359 Recovery 终态通知未确认，结果未知', 'TLSR_FINALIZATION_UNKNOWN');
                }
                status = await nextNotification(Math.max(0, validationDeadline - performance.now()));
                if (!status) throw sessionError('TLSR8359 Recovery 固件验证超时，终态结果未知', 'TLSR_FINALIZATION_UNKNOWN');
                verifiedRecoveryStatus(status);
                if (status[0] === STATUS_COMPLETE) {
                    if (acknowledgedIndex(status) !== blocks) throw new Error('TLSR8359 Recovery 终态索引与固件结束不一致');
                    break;
                }
                if (status[0] !== STATUS_RECEIVING) {
                    throw new Error(`TLSR8359 Recovery 拒绝固件，状态 ${hex(status)}`);
                }
            }
            try { options.onDiagnostics?.({
                payloadBytes: pkg.firmware.length, packets, payloadLimit: fastBlocks * 16,
                windowPackets: 1, notificationFallbacks, notificationRecoveries,
                quarterSeconds: [...quarterSeconds, dataSeconds],
                validationSeconds: Number(((performance.now() - started) / 1000 - dataSeconds).toFixed(3)),
            }); } catch (_) { /* optional timing must not change the result */ }
            detachNotifications();
            await stopNotifications();
            if (device.gatt.connected) device.gatt.disconnect();
        } finally {
            detachNotifications();
        }
        } finally {
            finalDetach?.();
            await stopNotifications();
            if (!prepared) discovery.finish();
        }
    }

    async function transferDevice(device, pkg, onProgress, options = {}) {
        const handle = await acquireRecoverySession({ device, generation: 0, isCurrent: () => true, onStage: options.onPhase }, pkg);
        return await transferRecoverySession(handle, pkg, onProgress);
    }

    global.TelinkOta = {
        acquireRecoverySession, transferRecoverySession, releaseRecoverySession, assertDeviceAvailable,
        validateRecoveryContext, checkRecoveryCompatibility,
        queryRecovery, queryRecoveryDevice, resumeApplication,
        parsePackage, transferDevice, crc16, crc32, dataPacket,
        fastBlockLimit, fastPacketEnd, fastPacketWindow, packetRequiresDeliveryAck,
        characteristicSupportsWriteResponse,
        isTransientGattError, acknowledgedIndex, cancelRecoveryDiscovery,
    };
})(window);
