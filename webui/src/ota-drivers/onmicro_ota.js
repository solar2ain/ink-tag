/* OnMicro DFU v3 transport for InkTag OM6626.
 * The application first reboots into a fixed BLE Recovery. Recovery then
 * replaces the single application in place and keeps advertising after an
 * interrupted transfer, so the same package can be sent again.
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

    const SERVICE_UUID = '000001bf-5f00-0000-0000-4f4d2d4f5441';
    const CONTROL_UUID = '000001bf-5f01-0000-0000-4f4d2d4f5441';
    const DATA_UUID = '000001bf-5f02-0000-0000-4f4d2d4f5441';
    const RECOVERY_PROFILE_UUID = '000001bf-5f03-0000-0000-4f4d2d4f5441';
    const IMAGE_TYPE_CUSTOM = 0x10;
    const RESPONSE = 0x60;
    const SUCCESS = 0x01;
    const DFU_OPERATION_FAILED = 0x0a;
    const CREATE = 0x01;
    const SET_PRN = 0x02;
    const CHECKSUM = 0x03;
    const EXECUTE = 0x04;
    const SELECT = 0x06;
    const SAFE_RESUME_APP = 0x07;
    const OBJECT_COMMAND = 0x01;
    const OBJECT_DATA = 0x02;
    const CHECKPOINT_PACKETS = 8;
    const SIGNATURE_MAGIC = 0x31534b49;
    const SIGNATURE_FORMAT_VERSION = 1;
    const SIGNATURE_PREFIX_SIZE = 32;
    const SIGNATURE_SIZE = 64;
    const SIGNATURE_TRAILER_SIZE = 96;
    const BOARD_SIGNATURE = 0x42584d4f;
    const LAYOUT_SIGNATURE = 0x31565a4f;
    const VERIFIER_SIGNATURE = 0x314b4d4f;
    const SIGNED_BOARD = 'om6626_zkcxxb';
    const SIGNED_LAYOUT = 'layout-v1';
    const LAYOUT_PROFILES = {
        [`${SIGNED_BOARD}/${SIGNED_LAYOUT}`]: { signature: LAYOUT_SIGNATURE, address: 0x2a000, slot: 0x4e000, board: SIGNED_BOARD, boardSignature: BOARD_SIGNATURE, journalSession: true },
        [`om6626_hsela/${SIGNED_LAYOUT}`]: { signature: 0x3156484f, address: 0x2a000, slot: 0x4e000, board: 'om6626_hsela', boardSignature: 0x53484d4f, journalSession: true }
    };
    const TRUSTED_SIGNING_KEY = {
        trustProfile: 'production',
        algorithm: 'ecdsa-p256-sha256',
        keyId: 'inktag-om6626-ota-v1',
        publicKeyHash: 'fc0e79e61ce05df76dcff00bf122e86e'
    };

    // A JS deadline cannot cancel an OS Bluetooth promise. Retain the grant
    // until that promise settles; a late result never continues this operation
    // or disconnects a newer owner's link.
    const nativeOwners = new Map();
    const serverKeys = new WeakMap();
    function nativeScope(server, device, onPhase = () => {}) {
        const peer = device || server.device;
        const key = peer ? (peer.id || peer.gatt || server) : (serverKeys.get(server) || server);
        if (nativeOwners.has(key)) {
            const error = new Error('OM Recovery 原生蓝牙操作仍未结束；请等待并只读确认设备，不能重试写入');
            error.code = 'OM_NATIVE_PENDING';
            throw error;
        }
        if (device) serverKeys.set(device.gatt, key);
        serverKeys.set(server, key);
        const owner = { pending: 0, abandoned: false, finished: false };
        nativeOwners.set(key, owner);
        const discoveryDeadline = performance.now() + 45000;
        const release = () => {
            if (owner.finished && !owner.pending && nativeOwners.get(key) === owner) nativeOwners.delete(key);
        };
        const timedOut = phase => {
            owner.abandoned = true;
            const error = new Error(`OM Recovery ${phase}超时；本次流程已停止，结果未确认，请只读检查，勿重复写入`);
            error.code = 'OM_NATIVE_TIMEOUT';
            onPhase(error.message);
            return error;
        };
        return {
            onPhase,
            discover(phase, operation, maximum = 45000) {
                return this.step(phase, operation, Math.min(maximum, discoveryDeadline - performance.now()));
            },
            get usable() { return !owner.abandoned && !owner.pending; },
            finish() { owner.finished = true; release(); },
            async step(phase, operation, milliseconds = 5000) {
                if (owner.abandoned || owner.finished || nativeOwners.get(key) !== owner) {
                    throw new Error('OM Recovery 本次流程已停止；未继续写入');
                }
                if (owner.lastPhase !== phase) onPhase(`OM Recovery：${phase}`);
                owner.lastPhase = phase;
                if (milliseconds <= 0) throw timedOut(phase);
                owner.pending++;
                let timer;
                let expired = false;
                const native = Promise.resolve().then(operation);
                const settled = native.then(value => {
                    if (expired || owner.abandoned) throw new Error('OM Recovery 已忽略过期的蓝牙结果');
                    return value;
                }).finally(() => { owner.pending--; release(); });
                // Consume even a late rejection after the caller's deadline.
                settled.catch(() => {});
                try {
                    return await Promise.race([settled, new Promise((_, reject) => {
                        timer = setTimeout(() => {
                            expired = true;
                            reject(timedOut(phase));
                        }, milliseconds);
                    })]);
                } finally { clearTimeout(timer); }
            }
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
            if (method !== 0) throw new Error('OnMicro OTA package must use ZIP_STORED');
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

    function fromHex(value) {
        if (typeof value !== 'string' || value.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(value)) {
            throw new Error('OnMicro OTA publisher public key is malformed');
        }
        const output = new Uint8Array(value.length / 2);
        for (let i = 0; i < output.length; i++) output[i] = parseInt(value.slice(i * 2, i * 2 + 2), 16);
        return output;
    }

    function crc32(bytes, initial = 0) {
        let crc = (~initial) >>> 0;
        for (const value of bytes) {
            crc ^= value;
            for (let bit = 0; bit < 8; bit++) {
                crc = ((crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)) >>> 0;
            }
        }
        return (~crc) >>> 0;
    }

    function packageProfile(manifest) {
        const key = `${manifest.board}/${manifest.layout}`;
        const profile = Object.prototype.hasOwnProperty.call(LAYOUT_PROFILES, key) ? LAYOUT_PROFILES[key] : null;
        const application = manifest.application || {};
        if (!profile || manifest.board !== profile.board ||
            application.image_type !== IMAGE_TYPE_CUSTOM ||
            application.active_address !== profile.address || application.slot_size !== profile.slot) {
            throw new Error('OnMicro application/Recovery layout does not match this firmware');
        }
        if (!Number.isInteger(manifest.compatibilityEpoch) || manifest.compatibilityEpoch < 0 || manifest.compatibilityEpoch > 0xffff) {
            throw new Error('OnMicro compatibility epoch must be an explicit uint16');
        }
        if (manifest.chip !== 'OM6626' ||
            JSON.stringify(application.recovery_state_addresses) !== JSON.stringify([0x82000, 0x83000]) ||
            Object.prototype.hasOwnProperty.call(application, 'recovery_request_address') || application.recovery_request_storage !== 'transaction-journal') {
            throw new Error('OnMicro Recovery context does not match');
        }
        return profile;
    }

    function validateRecoveryPreflight(value, pkg) {
        const bytes = value instanceof DataView
            ? new Uint8Array(value.buffer, value.byteOffset, value.byteLength) : new Uint8Array(value);
        const profile = packageProfile(pkg.manifest);
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const validTail = bytes.length === 26 && bytes.slice(20,25).every((v,i) => v === [73,75,83,80,1][i]) && [0,1,2].includes(bytes[25]);
        if ((bytes.length !== 20 && !validTail) ||
            view.getUint32(0, true) !== 0x50524b49 || bytes[4] !== 1 || bytes[5] !== 0 ||
            view.getUint16(6, true) !== 20 || view.getUint32(8, true) !== profile.signature ||
            view.getUint32(12, true) !== profile.slot - SIGNATURE_TRAILER_SIZE ||
            view.getUint32(16, true) !== profile.slot) {
            throw new Error('Recovery preflight is missing or incompatible; a separately authorized fixed-Recovery Full upgrade is required');
        }
        const signature = pkg.manifest.signature || {};
        if (signature.required === false) {
            if (!validTail || bytes[25] !== 2) throw new Error('当前 Recovery 要求发布者签名，未发送未签名固件');
            verifyBasePayload(pkg.manifest, pkg.firmware);
            return;
        }
        if (signature.signed_board !== profile.board || signature.signed_layout !== pkg.manifest.layout ||
            signature.board_signature !== profile.boardSignature || signature.layout_signature !== profile.signature ||
            signature.verifier_signature !== VERIFIER_SIGNATURE) {
            throw new Error('Recovery preflight package context does not match');
        }
        const application = pkg.manifest.application;
        if (!Number.isInteger(application.size) || application.size <= 0 ||
            application.size > profile.slot - SIGNATURE_TRAILER_SIZE ||
            pkg.firmware.length !== application.size + SIGNATURE_TRAILER_SIZE ||
            application.transfer_size !== pkg.firmware.length) {
            throw new Error('Recovery preflight package exceeds the signed profile');
        }
    }

    function verifyBasePayload(manifest, firmware) {
        const profile = packageProfile(manifest), app = manifest.application;
        const signature = manifest.signature || {};
        if (manifest.sourceProfile !== 'base' || Object.keys(signature).length !== 3 ||
            signature.required !== false || signature.algorithm !== 'none' || signature.policy !== 'crc-only' ||
            !Number.isInteger(app.size) || app.size <= 40 || app.size > profile.slot - SIGNATURE_TRAILER_SIZE ||
            app.size !== firmware.length || app.transfer_size !== firmware.length ||
            manifest.compatibilityEpoch !== 1 || !Number.isInteger(manifest.releaseVersion) ||
            manifest.releaseVersion < 0 || manifest.releaseVersion > 0xffffffff) {
            throw new Error('OnMicro 未签名 InkTag 包格式或上下文无效');
        }
        return verifyIntegrityPayload(manifest, firmware, profile);
    }

    function verifyIntegrityPayload(manifest, firmware, profile) {
        const at = firmware.length - 40, d = firmware.slice(at), view = new DataView(d.buffer);
        const u = offset => view.getUint32(offset, true);
        if (u(0) !== 0x49434b49 || d[4] !== 1 || d[5] !== 40 || view.getUint16(6,true) !== 0 ||
            u(8) !== at || u(12) !== crc32(firmware.slice(0,at)) || u(16) !== profile.boardSignature ||
            u(20) !== profile.signature || u(24) !== 1 || u(28) !== manifest.releaseVersion ||
            u(32) !== 0 || u(36) !== crc32(d.slice(0,36))) {
            throw new Error('OnMicro App CRC／长度／主板上下文不匹配');
        }
        return at;
    }

    // Only after package verification: runtime excludes IKCI and publisher trailer.
    function applicationBodySize(pkg) {
        const { manifest, firmware } = pkg;
        if (manifest.signature?.required === false) return verifyBasePayload(manifest, firmware);
        const size = manifest.application.size;
        if (size > 40 && firmware.slice(size-40,size-36).every((v,i) => v === [73,75,67,73][i]) && firmware.length >= size) {
            return verifyIntegrityPayload(manifest, firmware.slice(0,size), packageProfile(manifest));
        }
        return size;
    }

    async function parsePackage(file) {
        if (file.size > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds archive limit before reading');
        const entries = readStoredZip(await file.arrayBuffer());
        const manifestBytes = entries.get('manifest.json');
        if (!manifestBytes) throw new Error('manifest.json is missing');
        const manifest = parseOtaManifest(manifestBytes);
        if (manifest.format !== 'inktag-ota-v1' || manifest.transport !== 'onmicro-dfu-v3') {
            throw new Error('not an InkTag OnMicro DFU v3 package');
        }
        const profile = packageProfile(manifest);
        const application = manifest.application || {};
        const firmware = entries.get(application.file);
        if (!firmware || firmware.length !== application.transfer_size) {
            throw new Error('application payload size does not match manifest');
        }
        if (firmware.length > profile.slot) {
            throw new Error('OnMicro application/Recovery layout does not match this firmware');
        }
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', firmware));
        if (hex(digest) !== String(application.sha256 || '').toLowerCase()) {
            throw new Error('package SHA-256 verification failed');
        }
        const signing = manifest.signature || {};
        if (signing.required === false) {
            verifyBasePayload(manifest, firmware);
            return { fileName: file.name, manifest, application, firmware, signatureStatus: 'unsigned' };
        }
        if (signing.required !== true) throw new Error('OnMicro OTA package must require a publisher signature');
        if (signing.trust_profile !== TRUSTED_SIGNING_KEY.trustProfile ||
            signing.algorithm !== TRUSTED_SIGNING_KEY.algorithm ||
            signing.key_id !== TRUSTED_SIGNING_KEY.keyId ||
            String(signing.public_key_hash || '').toLowerCase() !== TRUSTED_SIGNING_KEY.publicKeyHash) {
            throw new Error('OnMicro OTA publisher signing key is not trusted');
        }
        if (signing.signed_context_size !== 16 ||
            signing.signed_board !== profile.board ||
            signing.signed_layout !== manifest.layout ||
            signing.board_signature !== profile.boardSignature ||
            signing.layout_signature !== profile.signature ||
            signing.verifier_signature !== VERIFIER_SIGNATURE ||
            signing.signed_prefix_size !== SIGNATURE_PREFIX_SIZE ||
            signing.signed_trailer_size !== SIGNATURE_TRAILER_SIZE ||
            application.size + SIGNATURE_TRAILER_SIZE !== application.transfer_size) {
            throw new Error('OnMicro OTA publisher signature trailer is invalid');
        }
        const publicKey = fromHex(signing.public_key || '');
        const publicKeyDigest = new Uint8Array(await crypto.subtle.digest('SHA-256', publicKey));
        if (publicKey.length !== 65 || publicKey[0] !== 0x04 ||
            hex(publicKeyDigest.slice(0, 16)) !== TRUSTED_SIGNING_KEY.publicKeyHash) {
            throw new Error('OnMicro OTA publisher public key is not trusted');
        }
        const trailer = firmware.slice(application.size);
        const trailerView = new DataView(trailer.buffer, trailer.byteOffset, trailer.byteLength);
        const releaseVersion = manifest.releaseVersion;
        const compatibilityEpoch = manifest.compatibilityEpoch;
        if (!Number.isInteger(releaseVersion) || releaseVersion < 0 || releaseVersion > 0xffffffff ||
            !Number.isInteger(compatibilityEpoch) || compatibilityEpoch < 0 || compatibilityEpoch > 0xffff) {
            throw new Error('OnMicro release version or compatibility epoch is invalid');
        }
        if (trailerView.getUint32(0, true) !== SIGNATURE_MAGIC ||
            trailer[4] !== SIGNATURE_FORMAT_VERSION ||
            trailer[5] !== 1 || trailerView.getUint16(6, true) !== SIGNATURE_TRAILER_SIZE ||
            trailerView.getUint32(8, true) !== application.size ||
            trailerView.getUint32(12, true) !== compatibilityEpoch ||
            trailerView.getUint32(16, true) !== profile.boardSignature ||
            trailerView.getUint32(20, true) !== profile.signature ||
            trailerView.getUint32(24, true) !== VERIFIER_SIGNATURE ||
            trailerView.getUint32(28, true) !== releaseVersion) {
            throw new Error('OnMicro OTA signed trailer does not match the manifest');
        }
        const rawSignature = trailer.slice(SIGNATURE_PREFIX_SIZE);
        if (rawSignature.length !== SIGNATURE_SIZE || !rawSignature.some(value => value !== 0)) {
            throw new Error('OnMicro OTA ECDSA signature is missing');
        }
        const verificationKey = await crypto.subtle.importKey(
            'raw', publicKey, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']
        );
        const signatureValid = await crypto.subtle.verify(
            { name: 'ECDSA', hash: 'SHA-256' },
            verificationKey,
            rawSignature,
            firmware.slice(0, application.size + SIGNATURE_PREFIX_SIZE)
        );
        if (!signatureValid) throw new Error('OnMicro OTA ECDSA P-256 signature verification failed');
        applicationBodySize({ manifest, firmware });
        return { fileName: file.name, manifest, application, firmware };
    }

    function commandObject(pkg) {
        const output = new Uint8Array(24);
        const view = new DataView(output.buffer);
        view.setUint32(0, 0x01bfdf55, true);
        view.setUint32(4, 3, true);
        view.setUint16(8, 0, true);
        view.setUint16(10, 1, true);
        view.setUint32(12, IMAGE_TYPE_CUSTOM, true);
        view.setUint32(16, pkg.manifest.releaseVersion >>> 0, true);
        view.setUint32(20, pkg.firmware.length, true);
        return output;
    }

    function createCommand(type, size) {
        const output = new Uint8Array(6);
        const view = new DataView(output.buffer);
        output[0] = CREATE;
        output[1] = type;
        view.setUint32(2, size, true);
        return output;
    }

    function describeRejection(operation, response) {
        if (!global.InkTagOtaRejection) throw new Error('InkTag OTA rejection decoder is not loaded');
        return global.InkTagOtaRejection.describe(operation, response, {
            errorStatus: DFU_OPERATION_FAILED
        });
    }

    class Transport {
        constructor(service, scope) {
            this.scope = scope;
            this.service = service;
            this.pending = [];
            this.resetAcknowledged = false;
            this.onNotification = this.onNotification.bind(this);
        }

        onNotification(event) {
            const value = event.target.value;
            const data = new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
            const pending = this.pending.shift();
            if (!pending) return;
            if (data.length < 3 || data[0] !== RESPONSE || data[1] !== pending.opcode) {
                pending.reject(new Error('unexpected OnMicro DFU response'));
            } else if (data[2] !== SUCCESS) {
                const operation = ({
                    [CREATE]: 'CREATE',
                    [CHECKSUM]: 'CHECKSUM',
                    [EXECUTE]: 'EXECUTE',
                    [SELECT]: 'SELECT',
                    [SAFE_RESUME_APP]: 'SAFE_RESUME_APP'
                })[pending.opcode] || `opcode 0x${pending.opcode.toString(16)}`;
                const error = new Error(describeRejection(operation, data.slice(2)));
                error.code = 'RECOVERY_REFUSED';
                pending.reject(error);
            } else {
                pending.resolve(data);
            }
        }

        async open() {
            this.control = await this.scope.discover('发现控制通道', () => this.service.getCharacteristic(CONTROL_UUID));
            this.data = await this.scope.discover('发现数据通道', () => this.service.getCharacteristic(DATA_UUID));
            await this.scope.discover('开启控制通知', () => this.control.startNotifications());
            this.control.addEventListener('characteristicvaluechanged', this.onNotification);
        }

        async close() {
            if (!this.control) return;
            this.control.removeEventListener('characteristicvaluechanged', this.onNotification);
            // A validated terminal ACK schedules a device-native reset. Do not
            // issue another remote CCC write against the departing Recovery.
            // The caller still has to reconnect and attest the actual App.
            if (this.resetAcknowledged) return;
            if (!this.scope.usable) return;
            try { await this.scope.step('关闭控制通知', () => this.control.stopNotifications()); }
            catch (error) {
                // Failed/nonterminal operations retain bounded native cleanup;
                // an unsettled OS request must continue holding its grant.
                if (error.code === 'OM_NATIVE_TIMEOUT') throw error;
                this.scope.onPhase(`OM Recovery 通知清理未完成：${String(error.message || error)}`);
            }
        }

        async writeControl(bytes) {
            if (this.control.writeValueWithResponse) return this.control.writeValueWithResponse(bytes);
            return this.control.writeValue(bytes);
        }

        async request(bytes, timeoutMs = 15000) {
            const opcode = bytes[0];
            let pending;
            const response = new Promise((resolve, reject) => {
                pending = { opcode, resolve, reject };
                this.pending.push(pending);
                pending.timer = setTimeout(() => {
                    const index = this.pending.indexOf(pending);
                    if (index >= 0) this.pending.splice(index, 1);
                    reject(new Error(`OnMicro DFU opcode 0x${opcode.toString(16)} timed out`));
                }, timeoutMs);
            });
            // A notification may reject while the write itself is still pending.
            response.catch(() => {});
            try {
                await this.scope.step(`写入控制指令 0x${opcode.toString(16)}`, () => this.writeControl(bytes), Math.min(timeoutMs, 10000));
                return await response;
            } catch (error) {
                const index = this.pending.indexOf(pending);
                if (index >= 0) this.pending.splice(index, 1);
                throw error;
            } finally {
                clearTimeout(pending.timer);
            }
        }

        async writeData(bytes) {
            if (this.data.writeValueWithoutResponse) return this.data.writeValueWithoutResponse(bytes);
            return this.data.writeValue(bytes);
        }

        async checksum(expectedOffset, expectedCrc) {
            const response = await this.request(new Uint8Array([CHECKSUM]), 30000);
            if (response.length < 11) throw new Error('OnMicro checksum response is truncated');
            const view = new DataView(response.buffer, response.byteOffset, response.byteLength);
            const offset = view.getUint32(3, true);
            const crc = view.getUint32(7, true);
            if (offset !== expectedOffset || crc !== expectedCrc) {
                throw new Error(`OnMicro checksum mismatch at ${offset}/${expectedOffset}`);
            }
        }
    }

    async function sendObject(transport, type, bytes, onProgress, totalBase, totalSize) {
        await transport.request(new Uint8Array([SELECT, type]));
        await transport.request(createCommand(type, bytes.length));
        await transport.request(new Uint8Array([SET_PRN, 0, 0]));
        let offset = 0;
        let crc = 0;
        const packetSize = 200;
        while (offset < bytes.length) {
            const chunk = bytes.slice(offset, offset + packetSize);
            await transport.scope.step('写入应用数据', () => transport.writeData(chunk), 15000);
            crc = crc32(chunk, crc);
            offset += chunk.length;
            if (offset === bytes.length || offset % (packetSize * CHECKPOINT_PACKETS) === 0) {
                await transport.checksum(offset, crc);
            }
            onProgress(totalBase + offset, totalSize);
        }
        await transport.request(new Uint8Array([EXECUTE]), type === OBJECT_DATA ? 45000 : 15000);
        if (type === OBJECT_DATA) transport.resetAcknowledged = true;
    }

    async function readRecoveryProfile(server, scope) {
        const service = await scope.discover('发现升级服务', () => server.getPrimaryService(SERVICE_UUID));
        const characteristic = await scope.discover('发现 Recovery 状态通道', () => service.getCharacteristic(RECOVERY_PROFILE_UUID));
        return scope.discover('读取 Recovery 状态', () => characteristic.readValue());
    }

    async function withTransport(server, scope, operation) {
        const service = await scope.discover('发现升级服务', () => server.getPrimaryService(SERVICE_UUID));
        const transport = new Transport(service, scope);
        let failure;
        try {
            await transport.open();
            return await operation(transport);
        } catch (error) {
            failure = error;
            throw error;
        } finally {
            try { await transport.close(); }
            catch (error) {
                if (!failure) throw error;
                scope.onPhase(`OM Recovery 清理未确认（保留原始失败）：${String(error.message || error)}`);
            }
        }
    }

    async function transferServer(server, pkg, onProgress = () => {}, options = {}) {
        const scope = options.scope || nativeScope(server, null, options.onPhase);
        try {
            validateRecoveryPreflight(await readRecoveryProfile(server, scope), pkg);
            const command = commandObject(pkg);
            const total = command.length + pkg.firmware.length;
            const started = performance.now();
            return await withTransport(server, scope, async transport => {
                await sendObject(transport, OBJECT_COMMAND, command,
                    done => onProgress(done, total, started), 0, total);
                await sendObject(transport, OBJECT_DATA, pkg.firmware,
                    done => onProgress(done, total, started), command.length, total);
                return { activeAddress: pkg.application.active_address };
            });
        } finally { if (!options.scope) scope.finish(); }
    }

    async function transferDevice(device, pkg, onProgress, options = {}) {
        const scope = nativeScope(device.gatt, device, options.onPhase);
        let lastError;
        let server;
        let profile;
        try {
            for (let attempt = 0; attempt < 18; attempt++) {
                await new Promise(resolve => setTimeout(resolve, attempt === 0 ? 900 : 650));
                try {
                    server = device.gatt.connected ? device.gatt
                        : await scope.discover('建立 GATT 连接', () => device.gatt.connect(), 10000);
                    profile = await readRecoveryProfile(server, scope);
                    break;
                } catch (error) {
                    lastError = error;
                    server = null;
                    // A still-pending native call cannot be cancelled or retried.
                    if (!scope.usable || error.name === 'SecurityError' || error.name === 'NotAllowedError') throw error;
                    try { if (device.gatt.connected) device.gatt.disconnect(); } catch (_) { }
                }
            }
            if (!server) throw new Error(`OM6626 fixed BLE Recovery did not become ready${lastError ? `: ${lastError.message || lastError}` : ''}`);
            validateRecoveryPreflight(profile, pkg);
            // Only pre-transfer link establishment is retried. Unknown CREATE,
            // data or EXECUTE outcomes must never replay any part of the image.
            return await transferServer(server, pkg, onProgress, { scope });
        } finally { scope.finish(); }
    }

    async function validateDevice(device) {
        const scope = nativeScope(device.gatt, device);
        try {
            const server = device.gatt.connected ? device.gatt
                : await scope.discover('建立 GATT 连接', () => device.gatt.connect(), 10000);
            await scope.discover('发现升级服务', () => server.getPrimaryService(SERVICE_UUID));
            return device;
        } finally { scope.finish(); }
    }

    async function queryRecovery(server, options = {}) {
        const scope = options.scope || nativeScope(server, null, options.onPhase);
        try {
            const value = await readRecoveryProfile(server, scope);
            const view = new DataView(value.buffer, value.byteOffset, value.byteLength);
            const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
            const validTail = bytes.length === 26 && bytes.slice(20,25).every((v,i) => v === [73,75,83,80,1][i]) && [0,1,2].includes(bytes[25]);
            const validLength = view.byteLength === 20 || validTail;
            const profile = Object.values(LAYOUT_PROFILES).find(candidate =>
                validLength && candidate.signature === view.getUint32(8, true));
            if (!profile || !validLength || view.getUint32(0, true) !== 0x50524b49 ||
                view.getUint8(4) !== 1 || view.getUint8(5) !== 0 ||
                view.getUint16(6, true) !== 20 || view.getUint32(8, true) !== profile.signature ||
                view.getUint32(12, true) !== profile.slot - SIGNATURE_TRAILER_SIZE ||
                view.getUint32(16, true) !== profile.slot) {
                throw new Error('OM fixed Recovery profile is missing or incompatible');
            }
            return view;
        } finally { if (!options.scope) scope.finish(); }
    }

    async function resumeApplication(server, options = {}) {
        const scope = nativeScope(server, null, options.onPhase);
        try {
            await queryRecovery(server, { scope });
            return await withTransport(server, scope, async transport => {
                try {
                    await transport.request(new Uint8Array([SAFE_RESUME_APP]));
                    transport.resetAcknowledged = true;
                } catch (error) {
                    if (error.code === 'RECOVERY_REFUSED') throw error;
                    const unknown = new Error(`退出 Recovery 结果未知；请只读确认设备状态，不要重复退出或复位：${error.message || error}`);
                    unknown.code = 'RECOVERY_RESULT_UNKNOWN';
                    throw unknown;
                }
            });
        } finally { scope.finish(); }
    }

    global.OnMicroOta = {
        SERVICE_UUID,
        CONTROL_UUID,
        DATA_UUID,
        RECOVERY_PROFILE_UUID,
        validateRecoveryPreflight,
        applicationBodySize,
        crc32,
        commandObject,
        parsePackage,
        transferServer,
        transferDevice,
        validateDevice,
        queryRecovery,
        resumeApplication,
        describeRejection
    };
})(typeof window !== 'undefined' ? window : globalThis);
