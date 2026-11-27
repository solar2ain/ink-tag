/* Nordic Secure DFU transport for InkTag nRF52 boards.
 * Runtime dependency-free: the browser consumes an already signed DFU ZIP.
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

    const SERVICE_UUID = '0000fe59-0000-1000-8000-00805f9b34fb';
    const CONTROL_UUID = '8ec90001-f315-4f60-9fb8-838830daea50';
    const PACKET_UUID = '8ec90002-f315-4f60-9fb8-838830daea50';
    const BUTTONLESS_UUID = '8ec90003-f315-4f60-9fb8-838830daea50';

    const OP_CREATE = 0x01;
    const OP_SET_PRN = 0x02;
    const OP_CRC = 0x03;
    const OP_EXECUTE = 0x04;
    const OP_SELECT = 0x06;
    const OP_ABORT = 0x0c;
    const OP_RESPONSE = 0x60;
    const RESULT_SUCCESS = 0x01;
    const OBJECT_COMMAND = 0x01;
    const OBJECT_DATA = 0x02;
    const DEFAULT_PRN = 4;
    const MAX_OBJECT_ATTEMPTS = 3;
    // Signed INIT validation takes ~16 s on qualified 52811 Recovery. Match
    // the shared CLI's budget only here; ordinary controls/CRC stay at 15 s.
    const SIGNED_INIT_EXECUTE_WAIT_MS = 60000;
    const BUTTONLESS_ENTER = 0x01;
    const BUTTONLESS_RESPONSE = 0x20;
    const BUTTONLESS_SUCCESS = 0x01;
    const DFU_CONNECT_ATTEMPTS = 20;
    const DFU_VALIDATE_ATTEMPTS = 6;
    const DFU_CONNECT_RETRY_MS = 500;
    const grantedBootloaderDevices = new Map();

    const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

    function isRetryableGattError(error) {
        if (error && ['NORDIC_FINAL_EXECUTE_ACK_UNKNOWN', 'NORDIC_EXECUTE_FAILED', 'NORDIC_NATIVE_TIMEOUT', 'NORDIC_NATIVE_CANCELLED', 'NORDIC_NATIVE_WRITE_PENDING'].includes(error.code)) return false;
        const message = String(error && error.message ? error.message : error).toLowerCase();
        return message.includes('gatt') ||
            message.includes('disconnect') ||
            message.includes('cannot retrieve services') ||
            message.includes('networkerror') ||
            message.includes('connect');
    }

    function rememberBootloaderDevice(device, verifiedName = '') {
        const id4 = recoveryId4(verifiedName || (device && device.name));
        if (id4) grantedBootloaderDevices.set(id4, device);
        return device;
    }

    class DfuResponseTimeout extends Error {
        constructor(opcode, milliseconds) {
            super(`等待 Secure DFU 0x${opcode.toString(16)} 响应超时 (${milliseconds} ms)，结果未知`);
            this.name = 'DfuResponseTimeout';
        }
    }

    function timeout(promise, milliseconds, message, error = new Error(message)) {
        let timer;
        return Promise.race([
            promise,
            new Promise((_, reject) => {
                timer = setTimeout(() => reject(error), milliseconds);
            })
        ]).finally(() => clearTimeout(timer));
    }

    function uint32LE(value) {
        const bytes = new Uint8Array(4);
        new DataView(bytes.buffer).setUint32(0, value, true);
        return bytes;
    }

    function concatBytes(...parts) {
        const size = parts.reduce((sum, part) => sum + part.length, 0);
        const output = new Uint8Array(size);
        let offset = 0;
        for (const part of parts) {
            output.set(part, offset);
            offset += part.length;
        }
        return output;
    }

    let crcTable;
    function crc32(bytes) {
        if (!crcTable) {
            crcTable = new Uint32Array(256);
            for (let n = 0; n < 256; n++) {
                let value = n;
                for (let bit = 0; bit < 8; bit++) {
                    value = (value & 1) ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1);
                }
                crcTable[n] = value >>> 0;
            }
        }
        let crc = 0xffffffff;
        for (const byte of bytes) crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
        return (crc ^ 0xffffffff) >>> 0;
    }

    async function inflateRaw(compressed, expectedSize) {
        if (typeof DecompressionStream === 'undefined') {
            throw new Error('当前浏览器不支持 ZIP 解压，请升级 Chrome 或 Edge');
        }
        const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
        const reader = stream.getReader();
        const parts = [];
        let total = 0;
        try {
            while (true) {
                const {done, value} = await reader.read();
                if (done) break;
                total += value.length;
                if (total > expectedSize || total > 4 * 1024 * 1024) {
                    await reader.cancel();
                    throw new Error('OTA ZIP decoded size exceeds limit');
                }
                parts.push(value);
            }
        } finally { reader.releaseLock(); }
        const result = new Uint8Array(total);
        let offset = 0;
        for (const part of parts) { result.set(part, offset); offset += part.length; }
        return result;
    }

    async function readZipEntries(arrayBuffer) {
        const bytes = new Uint8Array(arrayBuffer);
        if (bytes.length > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds archive limit');
        const view = new DataView(arrayBuffer);
        let eocd = -1;
        const lowerBound = Math.max(0, bytes.length - 0xffff - 22);
        for (let offset = bytes.length - 22; offset >= lowerBound; offset--) {
            if (view.getUint32(offset, true) === 0x06054b50) {
                eocd = offset;
                break;
            }
        }
        if (eocd < 0) throw new Error('不是有效的 ZIP 文件');

        const entryCount = view.getUint16(eocd + 10, true);
        if (entryCount > 64) throw new Error('OTA ZIP exceeds member limit');
        let centralOffset = view.getUint32(eocd + 16, true);
        const decoder = new TextDecoder('utf-8', {fatal: true});
        const entries = new Map();
        const names = new Set();
        let totalSize = 0;

        for (let index = 0; index < entryCount; index++) {
            if (centralOffset + 46 > bytes.length) throw new Error('ZIP central header is truncated');
            if (view.getUint32(centralOffset, true) !== 0x02014b50) {
                throw new Error('ZIP 中央目录损坏');
            }
            const method = view.getUint16(centralOffset + 10, true);
            const compressedSize = view.getUint32(centralOffset + 20, true);
            const uncompressedSize = view.getUint32(centralOffset + 24, true);
            const nameLength = view.getUint16(centralOffset + 28, true);
            const extraLength = view.getUint16(centralOffset + 30, true);
            const commentLength = view.getUint16(centralOffset + 32, true);
            const localOffset = view.getUint32(centralOffset + 42, true);
            const name = decoder.decode(bytes.slice(centralOffset + 46, centralOffset + 46 + nameLength));
            if (!name || /[\\:\x00]/.test(name) || name.split('/').some(p => !p || p === '.' || p === '..') || names.has(name.toLowerCase())) throw new Error('Unsafe or duplicate OTA ZIP member');
            names.add(name.toLowerCase());
            const limit = name === 'manifest.json' ? 64 * 1024 : 4 * 1024 * 1024;
            totalSize += uncompressedSize;
            if (uncompressedSize > limit || totalSize > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds decoded size limit');
            if (centralOffset + 46 + nameLength + extraLength + commentLength > bytes.length || localOffset + 30 > bytes.length) throw new Error('ZIP header is truncated');

            if (view.getUint32(localOffset, true) !== 0x04034b50) throw new Error(`ZIP 条目损坏: ${name}`);
            const localNameLength = view.getUint16(localOffset + 26, true);
            const localExtraLength = view.getUint16(localOffset + 28, true);
            const dataOffset = localOffset + 30 + localNameLength + localExtraLength;
            if (dataOffset + compressedSize > bytes.length) throw new Error('ZIP entry is truncated');
            if (view.getUint16(centralOffset + 8, true) & 1) throw new Error('Encrypted OTA ZIP is unsupported');
            const compressed = bytes.slice(dataOffset, dataOffset + compressedSize);
            let data;
            if (method === 0) data = compressed;
            else if (method === 8) data = await inflateRaw(compressed, uncompressedSize);
            else throw new Error(`ZIP 使用了不支持的压缩方式 ${method}: ${name}`);
            if (data.length !== uncompressedSize) throw new Error(`ZIP 条目长度不符: ${name}`);
            if (crc32(data) !== view.getUint32(centralOffset + 16, true)) throw new Error('ZIP entry CRC mismatch');
            entries.set(name, data);
            centralOffset += 46 + nameLength + extraLength + commentLength;
        }
        return entries;
    }

    async function parsePackage(file) {
        if (file.size > 16 * 1024 * 1024) throw new Error('OTA ZIP exceeds archive limit before reading');
        const entries = await readZipEntries(await file.arrayBuffer());
        const manifestBytes = entries.get('manifest.json');
        if (!manifestBytes) throw new Error('OTA ZIP 缺少 manifest.json');
        const manifestJson = parseOtaManifest(manifestBytes);
        const manifest = manifestJson.manifest || manifestJson;
        const application = manifest.application;
        if (!application) throw new Error('只支持 application-only Nordic Secure DFU 包');
        if (manifest.bootloader || manifest.softdevice || manifest.softdevice_bootloader) {
            throw new Error('Web OTA 不允许更新 SoftDevice 或 Bootloader');
        }
        const initPacket = entries.get(application.dat_file);
        const firmware = entries.get(application.bin_file);
        if (!initPacket || !firmware) throw new Error('OTA ZIP 缺少应用 DAT/BIN 文件');
        if (!global.InkTagSignedOta) throw new Error('固件验签组件未加载');
        const verified = await global.InkTagSignedOta.inspectNordic(firmware, initPacket);
        return {
            fileName: file.name,
            verified,
            initPacket,
            firmware,
            firmwareCrc: crc32(firmware)
        };
    }

    async function writeWithResponse(characteristic, data) {
        if (characteristic.writeValueWithResponse) return characteristic.writeValueWithResponse(data);
        return characteristic.writeValue(data);
    }

    async function writeWithoutResponse(characteristic, data) {
        if (characteristic.writeValueWithoutResponse) return characteristic.writeValueWithoutResponse(data);
        return characteristic.writeValue(data);
    }

    // A timeout must cover the native promise too, not only a notification
    // installed after that promise returns. Late native completions never
    // advance the transfer or disconnect a newer owner of the same grant.
    const nativeOwners = new Map();
    const nativeWriteOwners = new Map();
    function nativeScope(device, onPhase = () => {}) {
        const key = device && (device.id || device) || {};
        if (nativeWriteOwners.has(key)) {
            throw Object.assign(new Error('上一笔 Nordic 原生写入尚未返回，结果未知；本页不能开始新的 Recovery 操作'), { code: 'NORDIC_NATIVE_WRITE_PENDING' });
        }
        const owner = { active: true, pending: 0, writes: 0, finished: false };
        nativeOwners.set(key, owner);
        const deadline = Date.now() + 45000;
        const current = () => nativeOwners.get(key) === owner && owner.active;
        const disconnect = () => {
            if (nativeOwners.get(key) === owner && device?.gatt?.connected) device.gatt.disconnect();
        };
        const finish = () => {
            owner.finished = true;
            if (!owner.pending && nativeOwners.get(key) === owner) nativeOwners.delete(key);
        };
        const cancelled = () => Object.assign(new Error('Nordic 原生操作已取消；未继续传输'), { code: 'NORDIC_NATIVE_CANCELLED' });
        const expiredError = label => {
            owner.active = false; disconnect();
            const error = Object.assign(new Error(`Nordic Recovery ${label}超时；本次结果未确认，请只读核对状态，不要盲目重发`), { code: 'NORDIC_NATIVE_TIMEOUT' });
            onPhase(error.message); return error;
        };
        const step = (label, work, milliseconds = 15000, trace = true, write = false) => {
            if (!current()) return Promise.reject(cancelled());
            if (milliseconds <= 0) return Promise.reject(expiredError(label));
            const started = Date.now();
            if (trace) onPhase(`Nordic Recovery：${label}`);
            return new Promise((resolve, reject) => {
                let expired = false;
                const timer = setTimeout(() => {
                    expired = true; reject(expiredError(label));
                }, Math.max(1, milliseconds));
                owner.pending++;
                if (write) { owner.writes++; nativeWriteOwners.set(key, owner); }
                Promise.resolve().then(() => {
                    if (!current()) throw cancelled();
                    return work();
                }).then(value => {
                    clearTimeout(timer);
                    if (expired || !current()) {
                        disconnect();
                        if (!expired) reject(cancelled());
                    } else {
                        if (trace) onPhase(`Nordic Recovery：${label}完成（${Date.now() - started} ms）`);
                        resolve(value);
                    }
                }, error => {
                    clearTimeout(timer);
                    if (expired || !current()) disconnect();
                    if (!expired) reject(error);
                }).finally(() => {
                    owner.pending--;
                    if (write && --owner.writes === 0 && nativeWriteOwners.get(key) === owner) nativeWriteOwners.delete(key);
                    if (owner.finished) finish();
                });
            });
        };
        return { step, current, disconnect, finish,
            abandon: () => { owner.active = false; disconnect(); },
            connect: work => step('建立 GATT 连接', work, Math.min(10000, deadline - Date.now())),
            discovery: (label, work, write = false) => step(label, work, deadline - Date.now(), true, write) };
    }

    class SecureDfuTransport {
        constructor(server, onProgress, options = {}) {
            this.server = server;
            this.ownsNativeScope = !options.nativeScope;
            this.native = options.nativeScope || nativeScope(server?.device, options.onPhase);
            this.onProgress = onProgress || (() => {});
            this.pendingResponse = null;
            this.pendingReceipt = null;
            // Web Bluetooth has no negotiated-MTU getter. Only the named
            // nRF52832 pilot opts into a larger first packet; a known length
            // rejection can fall back ONLY after CRC proves zero acceptance.
            this.packetSize = options.maxPacketSize === 200 ? 200 : 20;
            this.packetSizeConfirmed = this.packetSize === 20;
            this.prn = DEFAULT_PRN;
            this.handleNotification = this.handleNotification.bind(this);
        }

        async open(configurePrn = true) {
            const service = await this.native.discovery('发现升级服务', () => this.server.getPrimaryService(SERVICE_UUID));
            this.control = await this.native.discovery('发现控制通道', () => service.getCharacteristic(CONTROL_UUID));
            this.packet = await this.native.discovery('发现数据通道', () => service.getCharacteristic(PACKET_UUID));
            await this.native.discovery('开启控制通知', () => this.control.startNotifications(), true);
            this.control.addEventListener('characteristicvaluechanged', this.handleNotification);
            if (configurePrn) await this.request(OP_SET_PRN, new Uint8Array([this.prn & 0xff, this.prn >>> 8]));
        }

        async close() {
            if (this.control) {
                this.control.removeEventListener('characteristicvaluechanged', this.handleNotification);
                if (this.native.current()) {
                    try { await this.native.step('关闭控制通知', () => this.control.stopNotifications(), 3000, true, true); }
                    catch (_) { /* Cleanup must not hide the original outcome. */ }
                }
            }
            if (this.ownsNativeScope) this.native.finish();
        }

        handleNotification(event) {
            const view = event.target.value;
            const data = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
            if (data.length < 3 || data[0] !== OP_RESPONSE) return;

            let pending = null;
            if (this.pendingResponse && data[1] === this.pendingResponse.opcode) {
                pending = this.pendingResponse;
                this.pendingResponse = null;
            } else if (data[1] === OP_CRC && this.pendingReceipt) {
                pending = this.pendingReceipt;
                this.pendingReceipt = null;
            }
            if (!pending) return;
            if (data[2] !== RESULT_SUCCESS) {
                const extended = data.length > 3 ? `, extended=0x${data[3].toString(16).padStart(2, '0')}` : '';
                pending.reject(new Error(`Secure DFU 0x${data[1].toString(16)} 失败: 0x${data[2].toString(16)}${extended}`));
                return;
            }
            pending.resolve(data.slice(3));
        }

        async request(opcode, payload = new Uint8Array(), waitMs = 15000) {
            if (this.pendingResponse) throw new Error('Secure DFU 控制请求重叠');
            const response = new Promise((resolve, reject) => {
                this.pendingResponse = { opcode, resolve, reject };
            });
            // Start the response clock before issuing the native write. An ACK
            // arriving while that write never resolves must not hang forever.
            const reply = timeout(response, waitMs, '', new DfuResponseTimeout(opcode, waitMs));
            let writeSettled = false;
            try {
                const [, value] = await Promise.all([
                    this.native.step(`控制写 0x${opcode.toString(16)}`,
                        () => writeWithResponse(this.control, concatBytes(new Uint8Array([opcode]), payload)), waitMs, false, true)
                        .finally(() => { writeSettled = true; }),
                    reply,
                ]);
                return value;
            } catch (error) {
                if (!writeSettled) {
                    this.native.abandon();
                    throw Object.assign(new Error(`Nordic 控制写 0x${opcode.toString(16)} 尚未返回，结果未知；不要重发`), { code: 'NORDIC_NATIVE_TIMEOUT', cause: error });
                }
                throw error;
            } finally {
                this.pendingResponse = null;
            }
        }

        async select(objectType) {
            const response = await this.request(OP_SELECT, new Uint8Array([objectType]));
            if (response.length !== 12) throw new Error('Secure DFU SELECT 响应长度错误');
            const view = new DataView(response.buffer, response.byteOffset, response.byteLength);
            return {
                maxSize: view.getUint32(0, true),
                offset: view.getUint32(4, true),
                crc: view.getUint32(8, true)
            };
        }

        async calculateCrc() {
            const response = await this.request(OP_CRC);
            if (response.length !== 8) throw new Error('Secure DFU CRC 响应长度错误');
            const view = new DataView(response.buffer, response.byteOffset, response.byteLength);
            return { offset: view.getUint32(0, true), crc: view.getUint32(4, true) };
        }

        expectReceipt(waitMs = 15000) {
            if (this.pendingReceipt) throw new Error('Secure DFU PRN 回执重叠');
            let pending;
            const response = new Promise((resolve, reject) => {
                pending = { resolve, reject };
                this.pendingReceipt = pending;
            });
            return timeout(response, waitMs, '等待 Secure DFU PRN 回执超时').finally(() => {
                if (this.pendingReceipt === pending) this.pendingReceipt = null;
            });
        }

        parseCrcPayload(payload, label) {
            if (payload.length !== 8) throw new Error(`${label}响应长度错误`);
            const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
            return { offset: view.getUint32(0, true), crc: view.getUint32(4, true) };
        }

        verifyCrc(checkpoint, referenceBytes, expectedOffset, label) {
            const expectedCrc = crc32(referenceBytes.slice(0, expectedOffset));
            if (checkpoint.offset !== expectedOffset || checkpoint.crc !== expectedCrc) {
                throw new Error(
                    `${label}校验失败：设备偏移 ${checkpoint.offset}，期望 ${expectedOffset}；` +
                    `设备 CRC 0x${checkpoint.crc.toString(16).toUpperCase().padStart(8, '0')}，` +
                    `期望 0x${expectedCrc.toString(16).toUpperCase().padStart(8, '0')}`
                );
            }
        }

        async writePackets(bytes, referenceBytes = bytes, baseOffset = 0, onVerified = null) {
            let packetCount = 0;
            for (let offset = 0; offset < bytes.length;) {
                const packet = bytes.slice(offset, offset + this.packetSize);
                const waitForReceipt = this.prn > 0 && (packetCount + 1) % this.prn === 0;
                const receipt = waitForReceipt ? this.expectReceipt() : null;
                // The native packet write can fail before we reach await receipt.
                // Observe that timer now; the normal await still checks its value.
                if (receipt) receipt.catch(() => {});
                try {
                    await this.native.step('发送数据帧', () => writeWithoutResponse(this.packet, packet), 15000, false, true);
                } catch (error) {
                    const lengthRejected = error && (error.name === 'InvalidModificationError' ||
                        /invalid attribute (?:value )?length|exceed[^\n]*(?:mtu|maximum.*length)/i.test(String(error.message || '')));
                    if (!lengthRejected || this.packetSizeConfirmed || packetCount !== 0 || receipt) throw error;
                    // Do not replay a write with unknown or partial effect.
                    // An explicit native CRC must still be exactly the prefix
                    // preceding this first packet before trying 20-byte writes.
                    const untouched = await this.calculateCrc();
                    this.verifyCrc(untouched, referenceBytes, baseOffset + offset, 'MTU 回退前 ');
                    this.packetSize = 20;
                    this.packetSizeConfirmed = true;
                    continue;
                }
                if (packet.length === this.packetSize) this.packetSizeConfirmed = true;
                packetCount++;
                if (receipt) {
                    const expectedOffset = baseOffset + offset + packet.length;
                    const checkpoint = this.parseCrcPayload(await receipt, 'Secure DFU PRN ');
                    this.verifyCrc(checkpoint, referenceBytes, expectedOffset, 'Secure DFU PRN ');
                    if (onVerified) onVerified(expectedOffset);
                }
                offset += packet.length;
            }
        }

        async prepareResume(firmware) {
            const state = await this.select(OBJECT_DATA);
            if (!state.maxSize) throw new Error('Secure DFU 返回的数据对象大小为 0');
            if (state.offset > firmware.length) {
                throw new Error(`设备上的 DFU 断点 ${state.offset} 超过当前固件大小，请重启设备后重新 OTA`);
            }

            const committedOffset = state.offset - (state.offset % state.maxSize);
            const crcMatches = state.crc === crc32(firmware.slice(0, state.offset));
            if (!crcMatches && state.offset === committedOffset && state.offset !== 0) {
                throw new Error('设备中保存的是另一份固件的 DFU 断点，请重启设备后重新进入 OTA');
            }

            // CREATE rewinds an unfinished object to the last executed page.
            // Re-sending that page is safer than appending after a dropped BLE packet.
            return { maxSize: state.maxSize, offset: committedOffset };
        }

        async sendCommand(initPacket) {
            let state = await this.select(OBJECT_COMMAND);
            if (state.offset === initPacket.length && state.crc === crc32(initPacket)) {
                await this.request(OP_EXECUTE, new Uint8Array(), SIGNED_INIT_EXECUTE_WAIT_MS);
                return;
            }
            if (initPacket.length > state.maxSize) throw new Error('DFU 初始化包超过设备限制');
            // CREATE replaces partial init; ABORT is guarded exit, not cleanup.
            await this.request(OP_CREATE, concatBytes(new Uint8Array([OBJECT_COMMAND]), uint32LE(initPacket.length)));
            await this.writePackets(initPacket, initPacket, 0);
            const checked = await this.calculateCrc();
            if (checked.offset !== initPacket.length || checked.crc !== crc32(initPacket)) {
                throw new Error('DFU 初始化包 CRC 校验失败');
            }
            await this.request(OP_EXECUTE, new Uint8Array(), SIGNED_INIT_EXECUTE_WAIT_MS);
        }

        async sendFirmware(firmware, state) {
            let offset = state.offset;
            const started = performance.now();
            this.onProgress(offset, firmware.length, started);
            while (offset < firmware.length) {
                const objectSize = Math.min(state.maxSize, firmware.length - offset);
                const nextOffset = offset + objectSize;
                let lastError = null;
                for (let attempt = 1; attempt <= MAX_OBJECT_ATTEMPTS; attempt++) {
                    try {
                        await this.request(OP_CREATE, concatBytes(new Uint8Array([OBJECT_DATA]), uint32LE(objectSize)));
                        await this.writePackets(
                            firmware.slice(offset, nextOffset),
                            firmware,
                            offset,
                            verified => this.onProgress(verified, firmware.length, started)
                        );
                        const checked = await this.calculateCrc();
                        this.verifyCrc(checked, firmware, nextOffset, '固件对象 ');
                        lastError = null;
                        break;
                    } catch (error) {
                        if (error && ['NORDIC_NATIVE_TIMEOUT', 'NORDIC_NATIVE_CANCELLED'].includes(error.code)) throw error;
                        lastError = error;
                        this.onProgress(offset, firmware.length, started);
                        if (attempt < MAX_OBJECT_ATTEMPTS) await sleep(200);
                    }
                }
                if (lastError) {
                    throw new Error(`固件对象 ${offset}-${nextOffset} 连续 ${MAX_OBJECT_ATTEMPTS} 次传输失败：${lastError.message}`);
                }
                // Never retry an EXECUTE blindly: if its response was lost, the device may
                // already have committed this page. Reconnection can safely resume it.
                try {
                    await this.request(OP_EXECUTE, new Uint8Array(), 30000);
                } catch (error) {
                    if (!(error instanceof DfuResponseTimeout) || nextOffset !== firmware.length) {
                        const failure = new Error(`Secure DFU 数据 EXECUTE 0x04 失败 (${nextOffset}/${firmware.length})，不要重发：${error.message || error}`);
                        failure.code = 'NORDIC_EXECUTE_FAILED';
                        failure.cause = error;
                        throw failure;
                    }
                    const unknown = new Error(`最后一个固件对象 CRC 已核对，但 EXECUTE 0x04 ACK 未知 (${nextOffset}/${firmware.length})；不要重发，必须核对同设备运行镜像`);
                    unknown.code = 'NORDIC_FINAL_EXECUTE_ACK_UNKNOWN';
                    unknown.cause = error;
                    throw unknown;
                }
                offset = nextOffset;
                this.onProgress(offset, firmware.length, started);
            }
        }

        async transfer(dfuPackage, verified) {
            try {
                if (verified && !verified.publisherVerified) {
                    await this.open(false);
                    global.InkTagSignedOta.requireRecoverySignature(verified, 'nordic', await this.request(0x00));
                    await this.request(OP_SET_PRN, new Uint8Array([this.prn & 0xff, this.prn >>> 8]));
                } else await this.open();
                await this.sendCommand(dfuPackage.initPacket);
                const resumeState = await this.prepareResume(dfuPackage.firmware);
                await this.sendFirmware(dfuPackage.firmware, resumeState);
            } finally {
                await this.close();
            }
        }
    }

    function stableId4(applicationName, applicationAddress = '') {
        const name = String(applicationName || '').trim();
        const match = /^(?:IKT|InkTag)_NRF_([0-9A-F]{4})(?:_OTA)?$/i.exec(name);
        if (match) return match[1].toUpperCase();
        const address = String(applicationAddress || '').trim();
        const addressMatch = /^(?:[0-9A-F]{2}:){4}([0-9A-F]{2}):([0-9A-F]{2})$/i.exec(address);
        return addressMatch ? `${addressMatch[1]}${addressMatch[2]}`.toUpperCase() : '';
    }

    function bootloaderName(applicationName, applicationAddress = '') {
        const id4 = stableId4(applicationName, applicationAddress);
        if (!id4) throw new Error('无法从应用广播名或 BLE 地址确定 Nordic 稳定 ID4');
        return `IKT_NRF_${id4}_OTA`;
    }

    function recoveryId4(name) {
        const match = /^(?:IKT|InkTag)_NRF_([0-9A-F]{4})_OTA$/i.exec(String(name || ''));
        return match ? match[1].toUpperCase() : '';
    }

    async function buttonlessRequest(characteristic, opcode, payload = new Uint8Array()) {
        let handler;
        const response = new Promise(resolve => {
            handler = event => {
                const view = event.target.value;
                const data = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
                if (data.length >= 3 && data[0] === BUTTONLESS_RESPONSE && data[1] === opcode) resolve(data);
            };
            characteristic.addEventListener('characteristicvaluechanged', handler);
        });
        try {
            await writeWithResponse(characteristic, concatBytes(new Uint8Array([opcode]), payload));
            const result = await timeout(response, 8000, `Buttonless DFU 0x${opcode.toString(16)} 响应超时`);
            if (result[2] !== BUTTONLESS_SUCCESS) {
                throw new Error(`Buttonless DFU 0x${opcode.toString(16)} 被拒绝: 0x${result[2].toString(16)}`);
            }
        } finally {
            characteristic.removeEventListener('characteristicvaluechanged', handler);
        }
    }

    async function enterButtonless(server, expectedName) {
        if (!recoveryId4(expectedName)) {
            throw new Error('Nordic Recovery 名称必须是 IKT_NRF_<ID4>_OTA（兼容 InkTag_ 前缀）');
        }
        const service = await server.getPrimaryService(SERVICE_UUID);
        const characteristic = await service.getCharacteristic(BUTTONLESS_UUID);
        await characteristic.startNotifications();
        await buttonlessRequest(characteristic, BUTTONLESS_ENTER);
        return expectedName;
    }

    function sameApplicationGrant(device, expectedName, handoff) {
        return Boolean(device && device.id && handoff && handoff.deviceId === device.id &&
            recoveryId4(expectedName) && recoveryId4(handoff.recoveryName) === recoveryId4(expectedName));
    }

    async function findGrantedBootloaderDevice(expectedName, handoff = {}, applicationDevice = null, onStatus = () => {}) {
        const id4 = recoveryId4(expectedName);
        if (!id4) return null;
        // Reuse the exact pre-entry grant, not an inferred MAC neighbour or a
        // matching mutable App label. The live DFU channels are still mandatory.
        if (sameApplicationGrant(applicationDevice, expectedName, handoff)) {
            try {
                onStatus('正在复用原设备授权并重新发现 Recovery 通道');
                await validateBootloaderDevice(applicationDevice, expectedName, handoff, onStatus);
                return applicationDevice;
            } catch (error) {
                onStatus(`原设备授权的 Recovery 通道未就绪：${error.message}`);
                if (error && ['NORDIC_NATIVE_TIMEOUT', 'NORDIC_NATIVE_CANCELLED', 'NORDIC_NATIVE_WRITE_PENDING'].includes(error.code)) throw error;
                console.warn('原设备授权尚未发现 DFU 服务:', error);
            }
        }
        const cached = grantedBootloaderDevices.get(id4);
        if (cached && (!applicationDevice || cached.id !== applicationDevice.id)) {
            await validateBootloaderDevice(cached, expectedName, handoff);
            return cached;
        }
        if (!navigator.bluetooth.getDevices) return null;
        try {
            const devices = await navigator.bluetooth.getDevices();
            const device = devices.find(candidate => (!applicationDevice || candidate.id !== applicationDevice.id) &&
                (recoveryId4(candidate.name) === id4 || sameApplicationGrant(candidate, expectedName, handoff))) || null;
            if (!device) return null;
            await validateBootloaderDevice(device, expectedName, handoff);
            return rememberBootloaderDevice(device, expectedName);
        } catch (error) {
            // file:// pages and browsers without persistent Bluetooth grants
            // may reject getDevices(). Keep the user-gesture chooser fallback.
            if (error && ['NORDIC_NATIVE_TIMEOUT', 'NORDIC_NATIVE_CANCELLED', 'NORDIC_NATIVE_WRITE_PENDING'].includes(error.code)) throw error;
            console.warn('无法读取已授权的 DFU 设备:', error);
            return null;
        }
    }

    async function requestBootloaderDevice(expectedName, handoff = {}) {
        if (!recoveryId4(expectedName)) throw new Error('无法确定 Nordic Recovery 稳定身份');
        const device = await navigator.bluetooth.requestDevice({
            // Windows/Chrome may omit either FE59 or the complete local name
            // from its filtered chooser even though an unfiltered scan shows
            // the bootloader. Show the chooser without advertisement filters,
            // then validate both the selected name and FE59 after connecting.
            acceptAllDevices: true,
            optionalServices: [SERVICE_UUID]
        });
        await validateBootloaderDevice(device, expectedName, handoff);
        return device;
    }

    async function requestRecoveryDevice() {
        const device = await navigator.bluetooth.requestDevice({
            acceptAllDevices: true,
            optionalServices: [SERVICE_UUID]
        });
        await validateBootloaderDevice(device);
        return device;
    }

    async function validateBootloaderDevice(device, expectedName = '', handoff = {}, onStatus = () => {}) {
        const advertisedName = String(device && device.name || '');
        const id4 = recoveryId4(advertisedName);
        const cachedSameDevice = sameApplicationGrant(device, expectedName, handoff);
        if (!id4 && !cachedSameDevice) {
            throw new Error(`Nordic Recovery 广播名无效：${advertisedName || (device && device.id) || '未知'}`);
        }
        if (expectedName && (!recoveryId4(expectedName) || (id4 && id4 !== recoveryId4(expectedName)))) {
            throw new Error(`请选择 ${expectedName}，当前选择的是 ${advertisedName || device.id}`);
        }
        let lastError = null;
        const native = nativeScope(device, onStatus);
        for (let attempt = 0; attempt < DFU_VALIDATE_ATTEMPTS; attempt++) {
            try {
                onStatus(`Recovery 通道校验 ${attempt + 1}/${DFU_VALIDATE_ATTEMPTS}`);
                const server = device.gatt.connected ? device.gatt
                    : await native.connect(() => device.gatt.connect());
                const service = await native.discovery('校验升级服务', () => server.getPrimaryService(SERVICE_UUID));
                await native.discovery('校验控制通道', () => service.getCharacteristic(CONTROL_UUID));
                await native.discovery('校验数据通道', () => service.getCharacteristic(PACKET_UUID));
                // Keep this just-validated connection. Disconnect followed by
                // immediate reconnect races WinRT's asynchronous teardown.
                native.finish();
                rememberBootloaderDevice(device, expectedName || advertisedName);
                onStatus('已确认原设备的 Recovery 控制与数据通道');
                return true;
            } catch (error) {
                lastError = error;
                native.disconnect();
                if (String(error?.code || '').startsWith('NORDIC_NATIVE_')) { native.finish(); throw error; }
                // During App -> Recovery the shared FE59 service may still
                // contain only the old buttonless characteristic. A genuine
                // missing-service/characteristic discovery error is retryable
                // here only; never add it to transfer/EXECUTE replay rules.
                const missingChannel = error && (error.name === 'NotFoundError' ||
                    /No (?:Characteristics|Services) matching UUID/i.test(String(error.message)));
                if (!isRetryableGattError(error) && !missingChannel) break;
                if (attempt + 1 < DFU_VALIDATE_ATTEMPTS) {
                    onStatus('Recovery 服务仍在切换，断开后重新发现通道');
                    await sleep(DFU_CONNECT_RETRY_MS);
                }
            }
        }
        native.finish();
        throw new Error(`所选设备不是 Nordic Secure DFU Bootloader：${lastError ? lastError.message : '无法发现 DFU 服务'}`);
    }

    async function transferDevice(device, dfuPackage, onProgress, options = {}) {
        if (!global.InkTagSignedOta) throw new Error('固件验签组件未加载');
        const verified = await global.InkTagSignedOta.inspectNordic(dfuPackage.firmware, dfuPackage.initPacket);
        if (options.expectedBoard && options.expectedBoard !== verified.board)
            throw new Error('固件与当前 Nordic 主板不匹配，请选择对应主板的 OTA 包');
        let lastError = null;
        for (let attempt = 0; attempt < DFU_CONNECT_ATTEMPTS; attempt++) {
            let transport = null;
            const native = nativeScope(device, options.onPhase);
            try {
                const server = device.gatt.connected ? device.gatt
                    : await native.connect(() => device.gatt.connect());
                transport = new SecureDfuTransport(server, onProgress, { ...options, nativeScope: native });
                // Retry the complete connect + service-discovery + resumable
                // transfer path. On Windows, gatt.connect() may resolve and
                // the server can disconnect before getPrimaryService().
                await transport.transfer(dfuPackage, verified);
                native.disconnect(); native.finish();
                return;
            } catch (error) {
                lastError = error;
                if (transport) {
                    try { await transport.close(); } catch (_) { /* stale GATT object */ }
                }
                native.disconnect();
                native.finish();
                if (!isRetryableGattError(error)) throw error;
                await sleep(DFU_CONNECT_RETRY_MS);
            }
        }
        throw new Error(`连接 DFU Bootloader 超时：${lastError ? lastError.message : '设备未出现'}`);
    }

    async function queryRecovery(server) {
        const transport = new SecureDfuTransport(server);
        try {
            await transport.open(false);
            const version = await transport.request(0x00);
            return {safeResume: global.InkTagSignedOta.recoverySafeResume('nordic', version),
                signaturePolicy: global.InkTagSignedOta.recoveryPolicy('nordic', version)};
        } finally { await transport.close(); }
    }

    // Native SELECT only reads the current object size/offset/CRC. This path
    // never sets PRN, creates/executes/aborts an object or sends packet data.
    async function queryRecoveryState(server, options = {}) {
        const transport = new SecureDfuTransport(server, null, options);
        try {
            await transport.open(false);
            return { command: await transport.select(OBJECT_COMMAND), data: await transport.select(OBJECT_DATA) };
        } finally { await transport.close(); }
    }

    const unknownExits = new WeakSet();
    async function resumeApplication(server, options = {}) {
        const name = options.deviceId === server.device?.id ? options.recoveryName : server.device?.name;
        if (!/^(?:IKT|InkTag)_NRF_[0-9a-f]{4}_OTA$/i.test(name || '')) throw new Error('请先确认同设备 nRF 固定 Recovery 身份');
        if (unknownExits.has(server.device)) throw Object.assign(new Error('退出结果未知，不要重发或强制复位'), {code:'RECOVERY_RESULT_UNKNOWN'});
        const transport = new SecureDfuTransport(server);
        try {
            await transport.open(false);
            const proof = await transport.request(0x00);
            if (!global.InkTagSignedOta.recoverySafeResume('nordic', proof)) throw new Error('该 Recovery 不支持安全退出');
            unknownExits.add(server.device);
            let reply;
            try { reply = await transport.request(OP_ABORT, new Uint8Array(), options.timeoutMs || 10000); }
            catch (error) {
                if (error.message === 'Secure DFU 0xc 失败: 0x8') {
                    unknownExits.delete(server.device);
                    throw new Error('target rejected SAFE_RESUME_APP: operation not permitted');
                }
                throw Object.assign(new Error('退出结果未知；只读确认，不要重发或强制复位'), {code:'RECOVERY_RESULT_UNKNOWN'});
            }
            if (reply.length) throw Object.assign(new Error('退出 ACK 无法确认；不要重发'), {code:'RECOVERY_RESULT_UNKNOWN'});
            unknownExits.delete(server.device);
        } finally { await transport.close(); }
    }

    global.NordicDfu = {
        SERVICE_UUID,
        CONTROL_UUID,
        PACKET_UUID,
        BUTTONLESS_UUID,
        crc32,
        parsePackage,
        stableId4,
        bootloaderName,
        enterButtonless,
        findGrantedBootloaderDevice,
        requestBootloaderDevice,
        requestRecoveryDevice,
        validateBootloaderDevice,
        transferDevice,
        queryRecovery,
        queryRecoveryState,
        resumeApplication,
        SecureDfuTransport
    };
})(window);
