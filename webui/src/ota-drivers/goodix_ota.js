/* Goodix OTAS v2 transport for InkTag GR5331 and GR5515.
 * Runtime dependency-free: packages are board-locked ZIP_STORED archives.
 */
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

    const SERVICE_UUID = 'a6ed0401-d344-460a-8075-b9e8ec90d71b';
    const TX_UUID = 'a6ed0402-d344-460a-8075-b9e8ec90d71b';
    const RX_UUID = 'a6ed0403-d344-460a-8075-b9e8ec90d71b';
    const CTRL_UUID = 'a6ed0404-d344-460a-8075-b9e8ec90d71b';
    // OTAS_CTRL_PT_OP_DFU_ENTER is decoded by Goodix with le32toh(), so the
    // wire order of 0x474F4F44 is 44 4F 4F 47 ("DOOG").
    const ENTER_OTA = new Uint8Array([0x44, 0x4f, 0x4f, 0x47]);

    const CMD_GET_INFO = 0x01;
    const CMD_PROGRAM_START = 0x23;
    const CMD_PROGRAM_FLASH = 0x24;
    const CMD_PROGRAM_END = 0x25;
    const CMD_DFU_MODE_SET = 0x41;
    const CMD_DFU_FW_INFO_GET = 0x42;
    const ACK_SUCCESS = 0x01;
    const ACK_ERROR = 0x02;
    const NON_COPY_UPGRADE = 0x02;
    const IMAGE_INFO_PATTERN = 0x4744;
    const IMAGE_INFO_SIZE = 48;
    const IMAGE_INFO_STRUCT_SIZE = 40;
    const IMAGE_TRAILER_SIZE = 48;
    const SIGNATURE_EXTENSION_SIZE = 856;
    const SIGNED_TRAILER_SIZE = IMAGE_TRAILER_SIZE + SIGNATURE_EXTENSION_SIZE;
    const SIGNATURE_PATTERN = 0xdeadbeef;
    const SIGNED_FIRMWARE_TYPE = 0x10;
    const RSA_PUBLIC_KEY_OFFSET = 128;
    const RSA_PUBLIC_KEY_SIZE = 520;
    const RSA_SIGNATURE_OFFSET = 648;
    const RSA_SIGNATURE_SIZE = 256;
    const ECDSA_PUBLIC_KEY_SIZE = 65;
    const ECDSA_SIGNATURE_SIZE = 64;
    const TRUSTED_SIGNING_KEYS = {
        gr5331_zkc21v: {
            trustProfile: 'production',
            keyId: 'inktag-gr5331-ota-v1',
            algorithm: 'ecdsa-p256-sha256',
            publicKeyHash: '3d96ce5cf20e58cca5f5e291ca2bfd81',
            chip: 'GR5331'
        },
        gr551x_zkc42v: {
            trustProfile: 'production',
            keyId: 'inktag-gr551x-ota-v1',
            algorithm: 'rsa-pss-sha256',
            publicKeyHash: 'a410a4f0ce51dd2c89ba8cca1dade9b9',
            chip: 'GR551x'
        }
    };
    // Same publisher/ABI, not the same executable or storage layout.
    TRUSTED_SIGNING_KEYS.gr5513_zkel_internal = TRUSTED_SIGNING_KEYS.gr551x_zkc42v;
    // A layout is not a signing key or an epoch. Cross-layout OTA is forbidden.
    const LAYOUT_PROFILES = {
        // Pilot packages still require exact live signed-marker attestation.
        // Recognizing their format never authorizes an old-to-new layout OTA.
        'gr5331_zkc21v/layout-v1': {
            activeAddress: 0x00220000,
            slotSize: 0x00040000,
            layoutMarker: 0x31563347,
            imageStoreStart: 0x0026a000,
            imageStoreMaxSize: 0x00014000,
            recoveryStateAddresses: [0x00268000, 0x00269000]
        },
        'gr551x_zkc42v/layout-v1': {
            activeAddress: 0x01020000,
            slotSize: 0x00052000,
            layoutMarker: 0x31563547,
            imageStoreStart: 0x0107c000,
            imageStoreMaxSize: 0x00080000,
            recoveryStateAddresses: [0x0107a000, 0x0107b000]
        },
        'gr5513_zkel_internal/layout-v1': {
            activeAddress: 0x01020000,
            slotSize: 0x00044000,
            layoutMarker: 0x31564947,
            imageStoreStart: 0x0106e000,
            imageStoreMaxSize: 0x00010000,
            recoveryStateAddresses: [0x0106c000, 0x0106d000]
        }
    };
    function layoutProfile(manifest) {
        const profile = LAYOUT_PROFILES[`${manifest.board}/${manifest.layout}`];
        if (!profile || !Number.isInteger(manifest.compatibilityEpoch) || manifest.compatibilityEpoch < 0 || manifest.compatibilityEpoch > 0xffff) {
            throw new Error('Goodix OTA package requires the board-qualified Full-migrated layout');
        }
        return profile;
    }
    const DEFAULT_PACKET_SIZE = 240;
    const MODE_SETTLE_MS = 150;

    const OTA_REJECT_MAGIC = [0x49, 0x4b];
    const OTA_REJECT_VERSION = 1;
    const OTA_REJECT_WIRE_SIZE = 15;
    const OTA_REJECT_REASONS = [
        'UNSPECIFIED', 'POWER_SAMPLE_INVALID', 'VOLTAGE_BELOW_MIN',
        'IMAGE_INFO_INVALID', 'ADDRESS_OR_LAYOUT_MISMATCH', 'ERASE_FAILED',
        'WRITE_FAILED', 'CHECKSUM_OR_SIGNATURE_FAILED', 'INTERNAL'
    ];
    const OTA_REJECT_STAGES = [
        'UNKNOWN', 'MODE_SWITCH', 'PROGRAM_START', 'ERASE', 'PROGRAM_WRITE',
        'PROGRAM_END', 'IMAGE_VERIFY', 'SIGNATURE_VERIFY', 'METADATA_COMMIT',
        'BOOT_CONFIRM'
    ];

    function describeRejection(operation, response, fallbackOffset = null) {
        if (!global.InkTagOtaRejection) throw new Error('InkTag OTA rejection decoder is not loaded');
        return global.InkTagOtaRejection.describe(operation, response, {
            errorStatus: ACK_ERROR,
            fallbackOffset
        });
    }

    const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

    // A native GATT promise cannot be cancelled. Keep its exact grant owner
    // until it settles so an abandoned callback cannot write or close a newer
    // operation using the same browser device.
    const sessionOwners = new Map();
    const serverGrants = new WeakMap();
    function assertGrantAvailable(grant) {
        if (sessionOwners.get(grant)?.pendingWrites) {
            throw Object.assign(new Error('Goodix 原生写入或通知开关仍未返回；请等待其结束，不要重试升级'),
                { code: 'GOODIX_NATIVE_WRITE_PENDING' });
        }
        const previous = sessionOwners.get(grant);
        if (previous?.pending || previous?.active) {
            throw Object.assign(new Error('Goodix 原生连接操作仍未结束，或已有会话持有此设备；未重试升级'),
                { code: previous.pending ? 'GOODIX_NATIVE_PENDING' : 'GOODIX_SESSION_BUSY' });
        }
    }
    function assertDeviceAvailable(device) {
        assertGrantAvailable(device.id ? `device:${device.id}` : (serverGrants.get(device.gatt) || device.gatt));
    }
    function session(server, onStage = () => {}, deviceId, isCurrent = () => true) {
        deviceId = deviceId || server.device?.id;
        const grant = deviceId ? `device:${deviceId}` : (serverGrants.get(server) || server);
        if (deviceId) serverGrants.set(server, grant);
        assertGrantAvailable(grant);
        const owner = { active: true, pending: 0, pendingWrites: 0, finished: false };
        sessionOwners.set(grant, owner);
        const discoveryDeadline = Date.now() + 45000;
        let commandIssued = false;
        let programming = false;
        const current = () => sessionOwners.get(grant) === owner && owner.active && isCurrent();
        const note = message => { try { onStage(message); } catch (_) { /* diagnostic only */ } };
        const disconnect = () => {
            if (sessionOwners.get(grant) === owner && isCurrent() && server.connected) {
                try { server.disconnect(); } catch (_) { /* already disconnected */ }
            }
        };
        const assertCurrent = () => {
            if (!current()) throw Object.assign(new Error('Goodix OTA 会话已结束；未继续发送'), { code: 'GOODIX_SESSION_ABANDONED' });
        };
        const retire = () => {
            if (owner.finished && !owner.pending && sessionOwners.get(grant) === owner) sessionOwners.delete(grant);
        };
        const abort = error => { owner.active = false; disconnect(); return error; };
        const budget = milliseconds => programming ? milliseconds : Math.min(milliseconds, discoveryDeadline - Date.now());
        const step = async (label, operation, timeoutMs = 5000, connection = false, quiet = false, writes = false) => {
            assertCurrent();
            const started = Date.now();
            const elapsed = () => `${Date.now() - started} ms`;
            const remaining = budget(timeoutMs);
            const timedOut = () => {
                note(`Goodix OTA：${label}超时（${elapsed()}），已停止本次流程`);
                return abort(Object.assign(
                    new Error(`Goodix OTA ${label}超时；已停止本次流程，请只读确认设备状态`),
                    { code: 'GOODIX_NATIVE_STAGE_TIMEOUT' }));
            };
            if (remaining <= 0) throw timedOut();
            if (!quiet) note(`Goodix OTA：${label}开始`);
            return new Promise((resolve, reject) => {
                let expired = false;
                const timer = setTimeout(() => { expired = true; reject(timedOut()); }, remaining);
                owner.pending++;
                if (writes) owner.pendingWrites++;
                Promise.resolve().then(() => { assertCurrent(); return operation(); }).then(value => {
                    clearTimeout(timer);
                    if (expired || !current()) {
                        disconnect();
                        if (!expired) reject(Object.assign(new Error('Goodix OTA 会话已被替换'), { code: 'GOODIX_SESSION_ABANDONED' }));
                    } else { if (!quiet) note(`Goodix OTA：${label}完成（${elapsed()}）`); resolve(value); }
                }, error => {
                    clearTimeout(timer);
                    if (expired || !current()) disconnect();
                    if (!expired) {
                        if (!quiet) note(`Goodix OTA：${label}失败（${elapsed()}）`);
                        if (current() && connection && !commandIssued && !programming &&
                            ['NetworkError', 'NotFoundError'].includes(error?.name)) {
                            error.goodixConnectionRefused = true;
                        }
                        reject(error);
                    }
                }).finally(() => { owner.pending--; if (writes) owner.pendingWrites--; retire(); });
            });
        };
        return {
            step, current, assertCurrent, disconnect, abort, note, budget,
            command: () => { assertCurrent(); commandIssued = true; },
            programming: () => { assertCurrent(); programming = true; },
            finish: () => { owner.active = false; owner.finished = true; retire(); },
        };
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
            if (method !== 0) throw new Error('Goodix OTA package must use ZIP_STORED');
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
            if (zipCrc32(bytes.subarray(dataStart, dataEnd)) !== view.getUint32(offset + 14, true)) throw new Error('ZIP entry CRC mismatch');
            entries.set(name, bytes.slice(dataStart, dataEnd));
            offset = dataEnd;
        }
        return entries;
    }

    function zipCrc32(bytes) {
        let crc = 0xffffffff;
        for (const byte of bytes) {
            crc ^= byte;
            for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
        return (crc ^ 0xffffffff) >>> 0;
    }

    function hex(bytes) {
        return Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('');
    }

    function byteSum(bytes) {
        let sum = 0;
        for (const value of bytes) sum = (sum + value) >>> 0;
        return sum;
    }

    function containsBytes(haystack, needle) {
        outer: for (let offset = 0; offset + needle.length <= haystack.length; offset++) {
            for (let index = 0; index < needle.length; index++) {
                if (haystack[offset + index] !== needle[index]) continue outer;
            }
            return true;
        }
        return false;
    }

    function base64Url(bytes) {
        let binary = '';
        for (const value of bytes) binary += String.fromCharCode(value);
        return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
    }

    function reversed(bytes) {
        return Uint8Array.from(bytes).reverse();
    }

    async function verifyBasePayload(manifest, firmware, profile) {
        const signature=manifest.signature, app=manifest.application, size=app.size;
        if(manifest.sourceProfile!=='base' || Object.keys(signature).length!==3 || signature.required!==false ||
            signature.algorithm!=='none' || signature.policy!=='crc-only' || manifest.compatibilityEpoch!==1 ||
            !Number.isInteger(size) || size<=40 || size+48!==firmware.length || firmware.length>profile.slotSize)
            throw new Error('Goodix 未签名 InkTag 包格式无效');
        const crc32=data=>{let crc=0xffffffff;for(const b of data){crc^=b;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (~crc)>>>0;};
        const body=size-40,d=firmware.slice(body,size),dv=new DataView(d.buffer),u=offset=>dv.getUint32(offset,true);
        const board=manifest.board==='gr5331_zkc21v'?2:0;
        if(u(0)!==0x49434b49 || d[4]!==1 || d[5]!==40 || dv.getUint16(6,true)!==0 || u(8)!==body ||
            u(12)!==crc32(firmware.slice(0,body)) || u(16)!==board || u(20)!==profile.layoutMarker ||
            u(24)!==1 || u(28)!==manifest.releaseVersion || u(32)!==0 || u(36)!==crc32(d.slice(0,36)))
            throw new Error('Goodix App CRC／长度／主板上下文不匹配');
        const imageInfo=firmware.slice(size),iv=new DataView(imageInfo.buffer);
        const checksum=byteSum(firmware.slice(0,size));
        const comment={'gr5331_zkc21v':'inktag5331','gr551x_zkc42v':'inktag551x','gr5513_zkel_internal':'inktag5513i'}[manifest.board];
        const expectedComment=new Uint8Array(12);expectedComment.set(new TextEncoder().encode(comment));
        if(iv.getUint16(0,true)!==IMAGE_INFO_PATTERN || iv.getUint16(2,true)!==1 || iv.getUint32(4,true)!==size ||
            iv.getUint32(8,true)!==checksum || app.checksum!==checksum || iv.getUint32(12,true)!==profile.activeAddress ||
            iv.getUint32(16,true)!==profile.activeAddress || iv.getUint32(20,true)!==3 || iv.getUint32(24,true)!==0x744 ||
            !imageInfo.slice(28,40).every((v,i)=>v===expectedComment[i]) || iv.getUint32(40,true)!==profile.layoutMarker ||
            iv.getUint32(44,true)!==(~profile.layoutMarker>>>0)) throw new Error('Goodix 原生 image-info 不匹配');
        const bodyHash=hex(new Uint8Array(await crypto.subtle.digest('SHA-256',firmware.slice(0,body))));
        if(app.linked_size!==body || app.linked_sha256!==bodyHash)throw new Error('Goodix linked App 绑定不匹配');
        return imageInfo;
    }

    async function parsePackage(file) {
        if (file.size > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds archive limit before reading');
        const entries = readStoredZip(await file.arrayBuffer());
        const manifestBytes = entries.get('manifest.json');
        if (!manifestBytes) throw new Error('manifest.json is missing');
        const manifest = parseOtaManifest(manifestBytes);
        if (manifest.format !== 'inktag-ota-v1' || manifest.transport !== 'goodix-otas-v2') {
            throw new Error('not an InkTag Goodix OTAS v2 package');
        }
        const application = manifest.application || {};
        const firmware = entries.get(application.file);
        if (!firmware) throw new Error('application payload is missing');
        if (firmware.length !== application.transfer_size) throw new Error('transfer size does not match manifest');
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', firmware));
        if (hex(digest) !== String(application.sha256 || '').toLowerCase()) {
            throw new Error('package SHA-256 verification failed');
        }
        const trustedKey = TRUSTED_SIGNING_KEYS[manifest.board];
        if (!trustedKey) throw new Error('Goodix OTA target board is not trusted');
        if (Object.prototype.hasOwnProperty.call(manifest, 'version') ||
            !Number.isInteger(manifest.releaseVersion) ||
            !Number.isInteger(manifest.compatibilityEpoch)) {
            throw new Error('Goodix OTA package uses the retired version-floor format');
        }
        const profile = layoutProfile(manifest);
        if (manifest.chip !== trustedKey.chip || application.active_address !== profile.activeAddress ||
            application.slot_size !== profile.slotSize ||
            application.image_store_start !== profile.imageStoreStart ||
            application.image_store_max_size !== profile.imageStoreMaxSize ||
            !Array.isArray(application.recovery_state_addresses) ||
            application.recovery_state_addresses.length !== profile.recoveryStateAddresses.length ||
            application.recovery_state_addresses.some((value, index) => value !== profile.recoveryStateAddresses[index])) {
            throw new Error('Goodix OTA board/chip/address policy does not match');
        }
        const releaseMatch = /^(\d+)\.(\d+)\.(\d+)$/.exec(String(manifest.release || ''));
        if (!releaseMatch) throw new Error('Goodix OTA release must be MAJOR.MINOR.PATCH');
        const releaseParts = releaseMatch.slice(1).map(Number);
        if (!Number.isInteger(manifest.protocol) || manifest.protocol < 0 || manifest.protocol > 0xff ||
            releaseParts.some(value => value > 0xff)) {
            throw new Error('Goodix OTA release/protocol bytes are out of range');
        }
        const numericRelease = releaseParts[0] * 10000 + releaseParts[1] * 100 + releaseParts[2];
        if (manifest.releaseVersion !== numericRelease) {
            throw new Error('Goodix OTA releaseVersion does not match release');
        }
        if (manifest.compatibilityEpoch < 0 || manifest.compatibilityEpoch > 0xffff) {
            throw new Error('Goodix OTA compatibilityEpoch is out of range');
        }
        if (!containsBytes(firmware.subarray(0, application.size),
            new Uint8Array([manifest.protocol, ...releaseParts]))) {
            throw new Error('Goodix signed application does not contain the declared GATT release');
        }
        const signature = manifest.signature || {};
        if (signature.required === false) {
            const imageInfo = await verifyBasePayload(manifest, firmware, profile);
            return {fileName:file.name,manifest,application,firmware,imageInfo,firmwareType:0,signatureStatus:'unsigned'};
        }
        if (signature.required !== true) throw new Error('Goodix OTA package must require a publisher signature');
        if (signature.trust_profile !== trustedKey.trustProfile) throw new Error('Goodix OTA trust profile is not production');
        if (signature.algorithm !== trustedKey.algorithm) throw new Error('Goodix OTA signature algorithm is not trusted');
        if (signature.key_id !== trustedKey.keyId) throw new Error('Goodix OTA signing key ID is not trusted');
        if (String(signature.public_key_hash || '').toLowerCase() !== trustedKey.publicKeyHash) {
            throw new Error('Goodix OTA signing-key hash does not match the trusted key');
        }
        if (signature.signature_extension_size !== SIGNATURE_EXTENSION_SIZE ||
            signature.signed_trailer_size !== SIGNED_TRAILER_SIZE) {
            throw new Error('Goodix OTA signature container size is invalid');
        }
        const firmwareType = SIGNED_FIRMWARE_TYPE;
        const expectedTrailerSize = SIGNED_TRAILER_SIZE;
        if (application.size + expectedTrailerSize !== firmware.length) {
            throw new Error('Goodix package trailer size does not match its board policy');
        }
        if (firmware.length > profile.slotSize) {
            throw new Error('Goodix signed application exceeds its board-qualified slot');
        }
        const imageInfo = firmware.slice(application.size, application.size + IMAGE_INFO_SIZE);
        const info = new DataView(imageInfo.buffer, imageInfo.byteOffset, imageInfo.byteLength);
        const signedEpoch = info.getUint16(2, true);
        const size = info.getUint32(4, true);
        const checksum = info.getUint32(8, true);
        const loadAddress = info.getUint32(12, true);
        const runAddress = info.getUint32(16, true);
        if (info.getUint16(0, true) !== IMAGE_INFO_PATTERN || size !== application.size ||
            loadAddress !== application.active_address || runAddress !== loadAddress) {
            throw new Error('Goodix image-info does not match manifest');
        }
        if (signedEpoch !== manifest.compatibilityEpoch) {
            throw new Error('Goodix signed compatibility epoch does not match manifest');
        }
        const marker = info.getUint32(IMAGE_INFO_STRUCT_SIZE, true);
        const inverse = info.getUint32(IMAGE_INFO_STRUCT_SIZE + 4, true);
        if (marker !== profile.layoutMarker || inverse !== (~marker >>> 0)) {
            throw new Error('Goodix signed image-info has the wrong board-layout marker');
        }
        if (checksum !== application.checksum) {
            throw new Error('Goodix image-info checksum does not match manifest');
        }
        if (byteSum(firmware.subarray(0, application.size)) !== checksum) {
            throw new Error('Goodix application checksum failed');
        }
        if (trustedKey) {
            const signedView = new DataView(firmware.buffer, firmware.byteOffset, firmware.byteLength);
            if (signedView.getUint32(application.size + IMAGE_TRAILER_SIZE, true) !== SIGNATURE_PATTERN) {
                throw new Error('Goodix OTA signature-extension pattern is invalid');
            }
            if (new TextDecoder('ascii').decode(firmware.subarray(application.size + 120, application.size + 124)) !== 'SIGN') {
                throw new Error('Goodix OTA package is not a sign-only container');
            }
            const publicKeySize = trustedKey.algorithm === 'rsa-pss-sha256'
                ? RSA_PUBLIC_KEY_SIZE : ECDSA_PUBLIC_KEY_SIZE;
            const publicKey = firmware.subarray(
                application.size + RSA_PUBLIC_KEY_OFFSET,
                application.size + RSA_PUBLIC_KEY_OFFSET + publicKeySize
            );
            const publicKeyDigest = new Uint8Array(await crypto.subtle.digest('SHA-256', publicKey));
            if (hex(publicKeyDigest.slice(0, 16)) !== trustedKey.publicKeyHash) {
                throw new Error('Goodix OTA embedded public key is not trusted');
            }
            if (trustedKey.algorithm === 'rsa-pss-sha256') {
                const rsaSignature = firmware.subarray(
                    application.size + RSA_SIGNATURE_OFFSET,
                    application.size + RSA_SIGNATURE_OFFSET + RSA_SIGNATURE_SIZE
                );
                if (rsaSignature.length !== RSA_SIGNATURE_SIZE || !rsaSignature.some(value => value !== 0)) {
                    throw new Error('Goodix OTA RSA signature is missing');
                }
                const modulus = reversed(publicKey.subarray(0, 256));
                let exponent = reversed(publicKey.subarray(256, 260));
                while (exponent.length > 1 && exponent[0] === 0) exponent = exponent.slice(1);
                const verificationKey = await crypto.subtle.importKey(
                    'jwk',
                    { kty: 'RSA', n: base64Url(modulus), e: base64Url(exponent), ext: true },
                    { name: 'RSA-PSS', hash: 'SHA-256' },
                    false,
                    ['verify']
                );
                const signatureValid = await crypto.subtle.verify(
                    { name: 'RSA-PSS', saltLength: 32 },
                    verificationKey,
                    reversed(rsaSignature),
                    firmware.subarray(0, application.size + RSA_SIGNATURE_OFFSET)
                );
                if (!signatureValid) throw new Error('Goodix OTA RSA-PSS signature verification failed');
            } else {
                const ecdsaSignature = firmware.subarray(
                    application.size + RSA_SIGNATURE_OFFSET,
                    application.size + RSA_SIGNATURE_OFFSET + ECDSA_SIGNATURE_SIZE
                );
                if (ecdsaSignature.length !== ECDSA_SIGNATURE_SIZE || !ecdsaSignature.some(value => value !== 0)) {
                    throw new Error('Goodix OTA ECDSA signature is missing');
                }
                const verificationKey = await crypto.subtle.importKey(
                    'raw', publicKey, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']
                );
                const signatureValid = await crypto.subtle.verify(
                    { name: 'ECDSA', hash: 'SHA-256' },
                    verificationKey,
                    ecdsaSignature,
                    firmware.subarray(0, application.size + RSA_SIGNATURE_OFFSET)
                );
                if (!signatureValid) throw new Error('Goodix OTA ECDSA P-256 signature verification failed');
            }
        }
        return { fileName: file.name, manifest, application, firmware, imageInfo, firmwareType };
    }

    function concatBytes(...parts) {
        const size = parts.reduce((total, part) => total + part.length, 0);
        const output = new Uint8Array(size);
        let offset = 0;
        for (const part of parts) {
            output.set(part, offset);
            offset += part.length;
        }
        return output;
    }

    function uint16LE(value) {
        const bytes = new Uint8Array(2);
        new DataView(bytes.buffer).setUint16(0, value, true);
        return bytes;
    }

    function uint32LE(value) {
        const bytes = new Uint8Array(4);
        new DataView(bytes.buffer).setUint32(0, value >>> 0, true);
        return bytes;
    }

    function frame(command, payload = new Uint8Array()) {
        const body = concatBytes(uint16LE(command), uint16LE(payload.length), payload);
        const checksum = body.reduce((sum, value) => (sum + value) & 0xffff, 0);
        return concatBytes(new Uint8Array([0x44, 0x47]), body, uint16LE(checksum));
    }

    class FrameParser {
        constructor() { this.buffer = new Uint8Array(); }

        feed(chunk) {
            this.buffer = concatBytes(this.buffer, chunk);
            const output = [];
            while (this.buffer.length >= 8) {
                let start = -1;
                for (let i = 0; i + 1 < this.buffer.length; i++) {
                    if (this.buffer[i] === 0x44 && this.buffer[i + 1] === 0x47) { start = i; break; }
                }
                if (start < 0) { this.buffer = new Uint8Array(); break; }
                if (start) this.buffer = this.buffer.slice(start);
                if (this.buffer.length < 8) break;
                const view = new DataView(this.buffer.buffer, this.buffer.byteOffset, this.buffer.byteLength);
                const command = view.getUint16(2, true);
                const length = view.getUint16(4, true);
                const total = length + 8;
                if (this.buffer.length < total) break;
                const raw = this.buffer.slice(0, total);
                const expected = new DataView(raw.buffer).getUint16(total - 2, true);
                let actual = 0;
                for (let i = 2; i < total - 2; i++) actual = (actual + raw[i]) & 0xffff;
                if (actual !== expected) throw new Error('Goodix response checksum failed');
                output.push({ command, data: raw.slice(6, 6 + length) });
                this.buffer = this.buffer.slice(total);
            }
            return output;
        }
    }

    async function writeWithoutResponse(characteristic, data) {
        if (characteristic.writeValueWithoutResponse) return characteristic.writeValueWithoutResponse(data);
        return characteristic.writeValue(data);
    }

    class Transport {
        constructor(service, scope, packetSize = DEFAULT_PACKET_SIZE) {
            this.service = service;
            this.scope = scope;
            this.packetSize = packetSize;
            this.pending = [];
            this.responses = [];
            this.parser = new FrameParser();
            this.onNotification = this.onNotification.bind(this);
        }

        onNotification(event) {
            if (!this.scope.current()) return;
            const value = event.target.value;
            const chunk = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
            for (const response of this.parser.feed(chunk)) {
                const waiter = this.pending.find(item => item.command === response.command);
                if (waiter) {
                    this.pending.splice(this.pending.indexOf(waiter), 1);
                    waiter.resolve(response.data);
                } else {
                    this.responses.push(response);
                }
            }
        }

        async open() {
            this.tx = await this.scope.step('发现应答通道', () => this.service.getCharacteristic(TX_UUID), 5000, true);
            this.rx = await this.scope.step('发现数据通道', () => this.service.getCharacteristic(RX_UUID), 5000, true);
            this.ctrl = await this.scope.step('发现控制通道', () => this.service.getCharacteristic(CTRL_UUID), 5000, true);
            this.tx.addEventListener('characteristicvaluechanged', this.onNotification);
            await this.scope.step('开启升级通知', () => this.tx.startNotifications(), 5000, true, false, true);
            this.scope.command();
            await this.scope.step('开启升级通道', () => writeWithoutResponse(this.ctrl, ENTER_OTA), 8000, false, false, true);
            await sleep(100);
            this.scope.assertCurrent();
        }

        async close() {
            if (!this.tx) return;
            this.tx.removeEventListener('characteristicvaluechanged', this.onNotification);
            if (this.scope.current()) {
                await this.scope.step('关闭升级通知', () => this.tx.stopNotifications(), 3000, false, false, true);
            }
        }

        async sendRaw(data) {
            for (let offset = 0; offset < data.length; offset += this.packetSize) {
                if (this.assertLink) this.assertLink();
                this.scope.command();
                await this.scope.step('发送升级数据', () => writeWithoutResponse(this.rx, data.slice(offset, offset + this.packetSize)), 8000, false, true, true);
            }
        }

        async request(command, payload = new Uint8Array(), timeoutMs = 30000) {
            this.scope.assertCurrent();
            const queued = this.responses.find(response => response.command === command);
            if (queued) {
                this.responses.splice(this.responses.indexOf(queued), 1);
                return queued.data;
            }
            let timer;
            let pending;
            const response = new Promise((resolve, reject) => {
                pending = { command, resolve, reject };
                this.pending.push(pending);
                timer = setTimeout(() => {
                    const index = this.pending.indexOf(pending);
                    if (index >= 0) this.pending.splice(index, 1);
                    reject(this.scope.abort(new Error(`Goodix command 0x${command.toString(16)} timed out`)));
                }, Math.max(1, this.scope.budget(timeoutMs)));
            });
            // A native write may still be pending when its command timer ends.
            // Observe that rejection while sendRaw unwinds; never continue data.
            response.catch(() => {});
            try {
                await this.sendRaw(frame(command, payload));
                const result = await response;
                this.scope.assertCurrent();
                return result;
            } catch (error) {
                const index = this.pending.indexOf(pending);
                if (index >= 0) this.pending.splice(index, 1);
                throw error;
            } finally {
                clearTimeout(timer);
            }
        }
    }

    function attestTarget(target, pkg) {
        if (target.length < 54 || target[0] !== ACK_SUCCESS) {
            throw new Error('target rejected firmware-info query');
        }
        const view = new DataView(target.buffer, target.byteOffset, target.byteLength);
        const pattern = view.getUint16(6, true);
        const activeEpoch = view.getUint16(8, true);
        const activeLoad = view.getUint32(18, true);
        const activeRun = view.getUint32(22, true);
        const marker = view.getUint32(6 + IMAGE_INFO_STRUCT_SIZE, true);
        const inverse = view.getUint32(10 + IMAGE_INFO_STRUCT_SIZE, true);
        const profile = layoutProfile(pkg.manifest);
        const saveAddress = view.getUint32(1, true);
        const activeSize = view.getUint32(10, true);
        if (target[5] !== 0 && target[5] !== 1) throw new Error('target reports an unknown Goodix OTA role');
        if (pattern !== IMAGE_INFO_PATTERN || activeEpoch !== pkg.manifest.compatibilityEpoch ||
            marker !== profile.layoutMarker || inverse !== (~marker >>> 0) ||
            activeLoad !== pkg.application.active_address || activeRun !== activeLoad ||
            activeLoad !== profile.activeAddress || saveAddress !== activeLoad ||
            activeSize <= 0 || activeSize > profile.slotSize - SIGNED_TRAILER_SIZE) {
            throw new Error('target is not on the board-qualified Goodix layout; install the chip-qualified Full image first');
        }
        return { saveAddress, applicationMode: target[5] === 1 };
    }

    async function queryTarget(transport, pkg = null) {
        const info = await transport.request(CMD_GET_INFO);
        if (info.length < 18 || info[0] !== ACK_SUCCESS || info[17] !== 2) {
            throw new Error('target does not report Goodix OTAS v2');
        }
        if (pkg?.firmwareType === 0 && (info.length !== 34 ||
            !info.slice(20,24).every((v,i)=>v===[73,75,82,86][i]) || !info[24] || info[27]!==info[24] ||
            !info.slice(28).every((v,i)=>v===[73,75,83,80,1,2][i]))) {
            throw new Error('当前 Recovery 要求发布者签名，未发送未签名固件');
        }
        const target = await transport.request(CMD_DFU_FW_INFO_GET);
        if (target.length < 54 || target[0] !== ACK_SUCCESS) {
            throw new Error('target rejected DFU firmware-info query');
        }
        if (target[5] !== 0 && target[5] !== 1) throw new Error('target reports an unknown Goodix OTA role');
        return target;
    }

    async function recoverySession(server, resume) {
        const scope = session(server);
        let transport;
        let failed = false;
        try {
            const service = await scope.step('发现升级服务', () => server.getPrimaryService(SERVICE_UUID), 45000, true);
            transport = new Transport(service, scope);
            await transport.open();
            const target = await queryTarget(transport);
            if (target[5] !== 0) throw new Error('当前设备仍是 GR 应用，不是固定 Recovery');
            if (resume) {
                // Empty DFU_MODE_SET is the existing guarded SAFE_RESUME_APP,
                // not the one-byte command that enters OTA or a raw reset.
                const reply = await transport.request(CMD_DFU_MODE_SET, new Uint8Array(), 15000);
                if (!reply.length || reply[0] !== ACK_SUCCESS) {
                    throw new Error(describeRejection('SAFE_RESUME_APP', reply));
                }
            }
        } catch (error) { failed = true; throw error; }
        finally {
            try { if (transport) await transport.close(); }
            catch (error) { if (!failed) throw error; }
            finally { scope.finish(); }
        }
    }

    // SYSTEM_ACTION authorizes the App handoff; the vendor mode command commits
    // it. This path never opens an image transaction or sends firmware data.
    async function enterRecovery(server, { isCurrent = () => true, onStage } = {}) {
        const scope = session(server, onStage, undefined, isCurrent);
        let transport;
        let failed = false;
        let switching = false;
        let modeWriteSettled = false;
        try {
            const service = await scope.step('发现升级服务', () => server.getPrimaryService(SERVICE_UUID), 45000, true);
            transport = new Transport(service, scope);
            await transport.open();
            const target = await queryTarget(transport);
            if (target[5] !== 1) throw new Error('当前设备不是 GR 应用，未发送进入命令');
            switching = true;
            await transport.sendRaw(frame(CMD_DFU_MODE_SET, new Uint8Array([NON_COPY_UPGRADE])));
            modeWriteSettled = true; // Native completion, NOT a device commit ACK.
            await sleep(MODE_SETTLE_MS);
        } catch (error) { failed = true; throw error; }
        finally {
            // GR waits for the central to leave before resetting.
            try { if (transport) await transport.close(); }
            catch (error) {
                // The role change can disconnect before CCCD cleanup. This
                // permits only a fresh live Recovery acquisition, never replay
                // of mode or proof that its metadata commit succeeded.
                if (!failed && !(modeWriteSettled && !server.connected && error?.name === 'NetworkError')) throw error;
            }
            finally { if (switching) scope.disconnect(); scope.finish(); }
        }
    }

    // Opaque handles never expose a reusable GATT transport or owner scope.
    const recoverySessions = new WeakMap();
    function sessionError(message) {
        return Object.assign(new Error(message), { code: 'GOODIX_SESSION_INVALID' });
    }
    function snapshotPackage(pkg) {
        return { ...pkg, manifest: JSON.parse(JSON.stringify(pkg.manifest)),
            application: JSON.parse(JSON.stringify(pkg.application)),
            firmware: pkg.firmware.slice(), imageInfo: pkg.imageInfo.slice() };
    }
    function sameBytes(a, b) {
        return a?.length === b.length && b.every((value, index) => a[index] === value);
    }
    function packageUnchanged(record, pkg) {
        const frozen = record.pkg;
        return pkg === record.originalPackage && pkg.firmwareType === frozen.firmwareType &&
            JSON.stringify(pkg.manifest) === JSON.stringify(frozen.manifest) &&
            JSON.stringify(pkg.application) === JSON.stringify(frozen.application) &&
            sameBytes(pkg.firmware, frozen.firmware) && sameBytes(pkg.imageInfo, frozen.imageInfo);
    }
    function assertSessionLink(record) {
        record.scope.assertCurrent();
        if (record.lostConnection || !record.server.connected ||
            record.device.id !== record.deviceId || record.device.gatt !== record.server ||
            (record.server.device?.id && record.server.device.id !== record.deviceId)) {
            throw sessionError('Goodix Recovery 连接已变化；未继续发送');
        }
    }
    async function releaseRecoverySession(handle) {
        const record = recoverySessions.get(handle);
        if (!record) throw sessionError('无效的 Goodix Recovery 会话');
        if (record.releasePromise) return record.releasePromise;
        if (record.state === 'transferring') record.scope.abort(sessionError('Goodix Recovery 会话已取消'));
        record.state = 'released';
        record.releasePromise = (async () => {
            try {
                if (record.transport) await record.transport.close();
            } catch (error) {
                if (record.endAcknowledged && record.scope.current() &&
                    !record.server.connected && error?.name === 'NetworkError') {
                    record.scope.note('Goodix OTA：PROGRAM_END 已确认，设备已断开，继续应用验身');
                } else throw error;
            } finally {
                record.device.removeEventListener?.('gattserverdisconnected', record.onDisconnect);
                record.scope.disconnect();
                record.scope.finish();
            }
        })();
        return record.releasePromise;
    }
    async function acquireRecoverySession(context, pkg) {
        const { device, generation, isCurrent, onStage } = context;
        if (typeof device?.id !== 'string' || !device.id || !device.gatt || !Number.isSafeInteger(generation) || generation < 0 ||
            typeof isCurrent !== 'function' || !isCurrent()) {
            throw sessionError('Goodix Recovery 缺少当前设备授权或操作代际');
        }
        const server = device.gatt;
        const deviceId = device.id;
        if (server.device?.id && server.device.id !== deviceId) throw sessionError('Goodix GATT 与设备授权不一致');
        const scope = session(server, onStage, deviceId, isCurrent);
        const handle = Object.freeze(Object.create(null));
        const record = { device, deviceId, generation, server, scope, state: 'acquiring',
            originalPackage: pkg, pkg: null, transport: null, lostConnection: false,
            onDisconnect: () => { record.lostConnection = true; } };
        recoverySessions.set(handle, record);
        device.addEventListener?.('gattserverdisconnected', record.onDisconnect);
        try {
            record.pkg = snapshotPackage(pkg);
            let service;
            for (let attempt = 0; attempt < 3; attempt++) {
                try {
                    if (!server.connected) {
                        const connected = await scope.step('建立 GATT 连接', () => server.connect(), 10000, true);
                        if (connected !== server) throw sessionError('Goodix 连接返回了其他 GATT 会话');
                    }
                    service = await scope.step('发现升级服务', () => server.getPrimaryService(SERVICE_UUID), 45000, true);
                    break;
                } catch (error) {
                    if (!scope.current() || !error.goodixConnectionRefused || attempt === 2) throw error;
                    scope.disconnect();
                    await sleep(700);
                    record.lostConnection = false;
                }
            }
            record.transport = new Transport(service, scope);
            await record.transport.open();
            const target = await queryTarget(record.transport, record.pkg);
            const { saveAddress, applicationMode } = attestTarget(target, record.pkg);
            if (applicationMode) throw sessionError('当前设备仍是 GR 应用，不是 Recovery；未发送进入或烧写命令');
            record.saveAddress = saveAddress;
            record.target = target.slice();
            assertSessionLink(record);
            record.transport.assertLink = () => assertSessionLink(record);
            record.state = 'acquired';
            return handle;
        } catch (error) {
            scope.abort(error);
            try { await releaseRecoverySession(handle); } catch (_) { /* preserve acquire failure */ }
            throw error;
        }
    }
    async function transferRecoverySession(handle, candidate, onProgress = () => {}) {
        const record = recoverySessions.get(handle);
        if (!record || record.state !== 'acquired') throw sessionError('Goodix Recovery 会话无效或已消费');
        record.state = 'transferring';
        let failure;
        try {
            assertSessionLink(record);
            if (!packageUnchanged(record, candidate)) throw sessionError('Goodix 升级包已变化，不能复用已核对会话');
            const { pkg, saveAddress, transport, scope } = record;
            const programAddress = pkg.application.active_address;
            if (saveAddress !== programAddress) {
                throw new Error(`recovery address mismatch: device 0x${saveAddress.toString(16)}, package 0x${programAddress.toString(16)}`);
            }

            const startPayload = concatBytes(new Uint8Array([pkg.firmwareType]), pkg.imageInfo);
            scope.programming(); // Set BEFORE the first possibly mutating write.
            scope.note('Goodix OTA：PROGRAM_START 开始');
            const started = await transport.request(CMD_PROGRAM_START, startPayload, 45000);
            if (!started.length || started[0] !== ACK_SUCCESS) throw new Error(describeRejection('PROGRAM_START', started));
            scope.note('Goodix OTA：PROGRAM_START 完成，开始传输应用');

            const startTime = performance.now();
            let sent = 0;
            while (sent < pkg.firmware.length) {
                const chunk = pkg.firmware.slice(sent, sent + 1024);
                const payload = concatBytes(new Uint8Array([1]), uint32LE(programAddress + sent), uint16LE(chunk.length), chunk);
                const response = await transport.request(CMD_PROGRAM_FLASH, payload, 30000);
                if (!response.length || response[0] !== ACK_SUCCESS) throw new Error(describeRejection('PROGRAM_FLASH', response, sent));
                sent += chunk.length;
                onProgress(sent, pkg.firmware.length, startTime);
            }

            scope.note('Goodix OTA：PROGRAM_END 开始');
            const ended = await transport.request(
                CMD_PROGRAM_END,
                concatBytes(new Uint8Array([1]), uint32LE(byteSum(pkg.firmware))),
                30000
            );
            if (!ended.length || ended[0] !== ACK_SUCCESS) throw new Error(describeRejection('PROGRAM_END', ended));
            record.endAcknowledged = true;
            scope.note('Goodix OTA：PROGRAM_END 已确认');
            return { applicationMode: false, programAddress, requiresRecovery: false, endAcknowledged: true };
        } catch (error) {
            failure = error;
            record.scope.abort(error);
            throw error;
        } finally {
            if (record.state === 'transferring') record.state = 'settling';
            try { await releaseRecoverySession(handle); }
            catch (error) { if (!failure) throw error; }
        }
    }
    // Compatibility wrappers only accept an already-Recovery target. Shared
    // orchestration owns App prepare/commit/release; no second handoff algorithm.
    async function transferDevice(device, pkg, onProgress = () => {}, onStage = () => {}) {
        const handle = await acquireRecoverySession({ device, generation: 0, isCurrent: () => true, onStage }, pkg);
        return await transferRecoverySession(handle, pkg, onProgress);
    }
    async function transferServer(server, pkg, onProgress = () => {}, onStage = () => {}) {
        if (!server.device || server.device.gatt !== server) {
            // Check the shared quarantine before reporting an invalid wrapper.
            const scope = session(server, onStage);
            scope.finish();
            throw sessionError('Goodix Recovery 缺少原设备授权');
        }
        return await transferDevice(server.device, pkg, onProgress, onStage);
    }

    async function validateDevice(device) {
        const scope = session(device.gatt, undefined, device.id);
        try {
            const server = device.gatt.connected ? device.gatt :
                await scope.step('建立 GATT 连接', () => device.gatt.connect(), 10000, true);
            await scope.step('发现升级服务', () => server.getPrimaryService(SERVICE_UUID), 45000, true);
            return device;
        } finally { scope.finish(); }
    }

    global.GoodixOta = {
        SERVICE_UUID,
        layoutProfile,
        enterOtaBytes: () => ENTER_OTA.slice(),
        parsePackage,
        acquireRecoverySession,
        transferRecoverySession,
        releaseRecoverySession,
        assertDeviceAvailable,
        transferServer,
        transferDevice,
        validateDevice,
        enterRecovery,
        queryRecovery: (server) => recoverySession(server, false),
        resumeApplication: (server) => recoverySession(server, true),
        frame,
        FrameParser,
        byteSum,
        attestTarget,
        describeRejection
    };
})(typeof window !== 'undefined' ? window : globalThis);
