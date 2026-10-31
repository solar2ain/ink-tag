import{B as e}from"./fonts.generated-DtYl4Vl9.js";import{Ct as t,Dt as n,Mt as r,Nt as i,bt as a,wt as o,yt as s}from"./index-1ya5GlFp.js";var c=`/* Nordic Secure DFU transport for InkTag nRF52 boards.
 * Runtime dependency-free: the browser consumes an already signed DFU ZIP.
 */
(function (global) {
    'use strict';

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
            super(\`等待 Secure DFU 0x\${opcode.toString(16)} 响应超时 (\${milliseconds} ms)，结果未知\`);
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

    async function inflateRaw(compressed) {
        if (typeof DecompressionStream === 'undefined') {
            throw new Error('当前浏览器不支持 ZIP 解压，请升级 Chrome 或 Edge');
        }
        const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
        return new Uint8Array(await new Response(stream).arrayBuffer());
    }

    async function readZipEntries(arrayBuffer) {
        const bytes = new Uint8Array(arrayBuffer);
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
        let centralOffset = view.getUint32(eocd + 16, true);
        const decoder = new TextDecoder();
        const entries = new Map();

        for (let index = 0; index < entryCount; index++) {
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

            if (view.getUint32(localOffset, true) !== 0x04034b50) throw new Error(\`ZIP 条目损坏: \${name}\`);
            const localNameLength = view.getUint16(localOffset + 26, true);
            const localExtraLength = view.getUint16(localOffset + 28, true);
            const dataOffset = localOffset + 30 + localNameLength + localExtraLength;
            const compressed = bytes.slice(dataOffset, dataOffset + compressedSize);
            let data;
            if (method === 0) data = compressed;
            else if (method === 8) data = await inflateRaw(compressed);
            else throw new Error(\`ZIP 使用了不支持的压缩方式 \${method}: \${name}\`);
            if (data.length !== uncompressedSize) throw new Error(\`ZIP 条目长度不符: \${name}\`);
            entries.set(name, data);
            centralOffset += 46 + nameLength + extraLength + commentLength;
        }
        return entries;
    }

    async function parsePackage(file) {
        const entries = await readZipEntries(await file.arrayBuffer());
        const manifestBytes = entries.get('manifest.json');
        if (!manifestBytes) throw new Error('OTA ZIP 缺少 manifest.json');
        const manifestJson = JSON.parse(new TextDecoder().decode(manifestBytes));
        const manifest = manifestJson.manifest || manifestJson;
        const application = manifest.application;
        if (!application) throw new Error('只支持 application-only Nordic Secure DFU 包');
        if (manifest.bootloader || manifest.softdevice || manifest.softdevice_bootloader) {
            throw new Error('Web OTA 不允许更新 SoftDevice 或 Bootloader');
        }
        const initPacket = entries.get(application.dat_file);
        const firmware = entries.get(application.bin_file);
        if (!initPacket || !firmware) throw new Error('OTA ZIP 缺少应用 DAT/BIN 文件');
        return {
            fileName: file.name,
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
            const error = Object.assign(new Error(\`Nordic Recovery \${label}超时；本次结果未确认，请只读核对状态，不要盲目重发\`), { code: 'NORDIC_NATIVE_TIMEOUT' });
            onPhase(error.message); return error;
        };
        const step = (label, work, milliseconds = 15000, trace = true, write = false) => {
            if (!current()) return Promise.reject(cancelled());
            if (milliseconds <= 0) return Promise.reject(expiredError(label));
            const started = Date.now();
            if (trace) onPhase(\`Nordic Recovery：\${label}\`);
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
                        if (trace) onPhase(\`Nordic Recovery：\${label}完成（\${Date.now() - started} ms）\`);
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
                const extended = data.length > 3 ? \`, extended=0x\${data[3].toString(16).padStart(2, '0')}\` : '';
                pending.reject(new Error(\`Secure DFU 0x\${data[1].toString(16)} 失败: 0x\${data[2].toString(16)}\${extended}\`));
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
                    this.native.step(\`控制写 0x\${opcode.toString(16)}\`,
                        () => writeWithResponse(this.control, concatBytes(new Uint8Array([opcode]), payload)), waitMs, false, true)
                        .finally(() => { writeSettled = true; }),
                    reply,
                ]);
                return value;
            } catch (error) {
                if (!writeSettled) {
                    this.native.abandon();
                    throw Object.assign(new Error(\`Nordic 控制写 0x\${opcode.toString(16)} 尚未返回，结果未知；不要重发\`), { code: 'NORDIC_NATIVE_TIMEOUT', cause: error });
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
            if (payload.length !== 8) throw new Error(\`\${label}响应长度错误\`);
            const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
            return { offset: view.getUint32(0, true), crc: view.getUint32(4, true) };
        }

        verifyCrc(checkpoint, referenceBytes, expectedOffset, label) {
            const expectedCrc = crc32(referenceBytes.slice(0, expectedOffset));
            if (checkpoint.offset !== expectedOffset || checkpoint.crc !== expectedCrc) {
                throw new Error(
                    \`\${label}校验失败：设备偏移 \${checkpoint.offset}，期望 \${expectedOffset}；\` +
                    \`设备 CRC 0x\${checkpoint.crc.toString(16).toUpperCase().padStart(8, '0')}，\` +
                    \`期望 0x\${expectedCrc.toString(16).toUpperCase().padStart(8, '0')}\`
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
                        /invalid attribute (?:value )?length|exceed[^\\n]*(?:mtu|maximum.*length)/i.test(String(error.message || '')));
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
                throw new Error(\`设备上的 DFU 断点 \${state.offset} 超过当前固件大小，请重启设备后重新 OTA\`);
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
                    throw new Error(\`固件对象 \${offset}-\${nextOffset} 连续 \${MAX_OBJECT_ATTEMPTS} 次传输失败：\${lastError.message}\`);
                }
                // Never retry an EXECUTE blindly: if its response was lost, the device may
                // already have committed this page. Reconnection can safely resume it.
                try {
                    await this.request(OP_EXECUTE, new Uint8Array(), 30000);
                } catch (error) {
                    if (!(error instanceof DfuResponseTimeout) || nextOffset !== firmware.length) {
                        const failure = new Error(\`Secure DFU 数据 EXECUTE 0x04 失败 (\${nextOffset}/\${firmware.length})，不要重发：\${error.message || error}\`);
                        failure.code = 'NORDIC_EXECUTE_FAILED';
                        failure.cause = error;
                        throw failure;
                    }
                    const unknown = new Error(\`最后一个固件对象 CRC 已核对，但 EXECUTE 0x04 ACK 未知 (\${nextOffset}/\${firmware.length})；不要重发，必须核对同设备运行镜像\`);
                    unknown.code = 'NORDIC_FINAL_EXECUTE_ACK_UNKNOWN';
                    unknown.cause = error;
                    throw unknown;
                }
                offset = nextOffset;
                this.onProgress(offset, firmware.length, started);
            }
        }

        async transfer(dfuPackage) {
            try {
                await this.open();
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
        return addressMatch ? \`\${addressMatch[1]}\${addressMatch[2]}\`.toUpperCase() : '';
    }

    function bootloaderName(applicationName, applicationAddress = '') {
        const id4 = stableId4(applicationName, applicationAddress);
        if (!id4) throw new Error('无法从应用广播名或 BLE 地址确定 Nordic 稳定 ID4');
        return \`IKT_NRF_\${id4}_OTA\`;
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
            const result = await timeout(response, 8000, \`Buttonless DFU 0x\${opcode.toString(16)} 响应超时\`);
            if (result[2] !== BUTTONLESS_SUCCESS) {
                throw new Error(\`Buttonless DFU 0x\${opcode.toString(16)} 被拒绝: 0x\${result[2].toString(16)}\`);
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
                onStatus(\`原设备授权的 Recovery 通道未就绪：\${error.message}\`);
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
            throw new Error(\`Nordic Recovery 广播名无效：\${advertisedName || (device && device.id) || '未知'}\`);
        }
        if (expectedName && (!recoveryId4(expectedName) || (id4 && id4 !== recoveryId4(expectedName)))) {
            throw new Error(\`请选择 \${expectedName}，当前选择的是 \${advertisedName || device.id}\`);
        }
        let lastError = null;
        const native = nativeScope(device, onStatus);
        for (let attempt = 0; attempt < DFU_VALIDATE_ATTEMPTS; attempt++) {
            try {
                onStatus(\`Recovery 通道校验 \${attempt + 1}/\${DFU_VALIDATE_ATTEMPTS}\`);
                const server = await native.connect(() => device.gatt.connect());
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
        throw new Error(\`所选设备不是 Nordic Secure DFU Bootloader：\${lastError ? lastError.message : '无法发现 DFU 服务'}\`);
    }

    async function transferDevice(device, dfuPackage, onProgress, options = {}) {
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
                await transport.transfer(dfuPackage);
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
        throw new Error(\`连接 DFU Bootloader 超时：\${lastError ? lastError.message : '设备未出现'}\`);
    }

    async function queryRecovery(server) {
        const transport = new SecureDfuTransport(server);
        try {
            await transport.open(false);
            const version = await transport.request(0x00);
            return {safeResume: version.length === 1 && version[0] === 0x81};
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
            if (proof.length !== 1 || proof[0] !== 0x81) throw new Error('该 Recovery 不支持安全退出');
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
`,l="/* Shared decoder for the versioned InkTag OTA rejection record. */\n(function (global) {\n    'use strict';\n\n    const REASONS = [\n        'UNSPECIFIED', 'POWER_SAMPLE_INVALID', 'VOLTAGE_BELOW_MIN',\n        'IMAGE_INFO_INVALID', 'ADDRESS_OR_LAYOUT_MISMATCH', 'ERASE_FAILED',\n        'WRITE_FAILED', 'CHECKSUM_OR_SIGNATURE_FAILED', 'INTERNAL'\n    ];\n    const STAGES = [\n        'UNKNOWN', 'MODE_SWITCH', 'PROGRAM_START', 'ERASE', 'PROGRAM_WRITE',\n        'PROGRAM_END', 'IMAGE_VERIFY', 'SIGNATURE_VERIFY', 'METADATA_COMMIT',\n        'BOOT_CONFIRM'\n    ];\n\n    function describe(operation, response, options = {}) {\n        const data = response || new Uint8Array();\n        const fallbackOffset = options.fallbackOffset ?? null;\n        const fallback = fallbackOffset === null ? '' : `, offset=${fallbackOffset}`;\n        const prefix = `target rejected ${operation}`;\n        if (data.length < 15 || data[0] !== options.errorStatus ||\n                data[1] !== 0x49 || data[2] !== 0x4b) {\n            return `${prefix}: malformed rejection response${fallback}`;\n        }\n        if (data[3] !== 1) {\n            return `${prefix}: unsupported rejection extension v${data[3]}${fallback}`;\n        }\n        const reason = REASONS[data[4]] || `UNKNOWN_0x${data[4].toString(16).padStart(2, '0').toUpperCase()}`;\n        const stage = STAGES[data[5]] || `UNKNOWN_0x${data[5].toString(16).padStart(2, '0').toUpperCase()}`;\n        const flags = data[6];\n        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);\n        const detail = view.getUint32(7, true);\n        const fields = [`reason=${reason}`, `stage=${stage}`];\n        if (flags & 0x04) fields.push(`address=0x${detail.toString(16).padStart(8, '0').toUpperCase()}`);\n        else if (flags & 0x08) fields.push(`offset=${detail}`);\n        else if (fallbackOffset !== null) fields.push(`offset=${fallbackOffset}`);\n        if (flags & 0x01) fields.push(`sampledMv=${view.getUint16(11, true)}`);\n        if (flags & 0x02) fields.push(`thresholdMv=${view.getUint16(13, true)}`);\n        return `${prefix}: ${fields.join(', ')}`;\n    }\n\n    global.InkTagOtaRejection = { describe };\n})(typeof window !== 'undefined' ? window : globalThis);\n",u=`/* Goodix OTAS v2 transport for InkTag GR5331 and GR5515.
 * Runtime dependency-free: packages are board-locked ZIP_STORED archives.
 */
(function (global) {
    'use strict';

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
        const profile = LAYOUT_PROFILES[\`\${manifest.board}/\${manifest.layout}\`];
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
        assertGrantAvailable(device.id ? \`device:\${device.id}\` : (serverGrants.get(device.gatt) || device.gatt));
    }
    function session(server, onStage = () => {}, deviceId, isCurrent = () => true) {
        deviceId = deviceId || server.device?.id;
        const grant = deviceId ? \`device:\${deviceId}\` : (serverGrants.get(server) || server);
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
            const elapsed = () => \`\${Date.now() - started} ms\`;
            const remaining = budget(timeoutMs);
            const timedOut = () => {
                note(\`Goodix OTA：\${label}超时（\${elapsed()}），已停止本次流程\`);
                return abort(Object.assign(
                    new Error(\`Goodix OTA \${label}超时；已停止本次流程，请只读确认设备状态\`),
                    { code: 'GOODIX_NATIVE_STAGE_TIMEOUT' }));
            };
            if (remaining <= 0) throw timedOut();
            if (!quiet) note(\`Goodix OTA：\${label}开始\`);
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
                    } else { if (!quiet) note(\`Goodix OTA：\${label}完成（\${elapsed()}）\`); resolve(value); }
                }, error => {
                    clearTimeout(timer);
                    if (expired || !current()) disconnect();
                    if (!expired) {
                        if (!quiet) note(\`Goodix OTA：\${label}失败（\${elapsed()}）\`);
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
        const view = new DataView(buffer);
        const decoder = new TextDecoder('utf-8');
        const entries = new Map();
        let offset = 0;
        while (offset + 4 <= bytes.length && view.getUint32(offset, true) === 0x04034b50) {
            if (offset + 30 > bytes.length) throw new Error('ZIP local header is truncated');
            const flags = view.getUint16(offset + 6, true);
            const method = view.getUint16(offset + 8, true);
            const compressedSize = view.getUint32(offset + 18, true);
            const nameLength = view.getUint16(offset + 26, true);
            const extraLength = view.getUint16(offset + 28, true);
            if (flags & 0x08) throw new Error('ZIP data descriptors are not supported');
            if (method !== 0) throw new Error('Goodix OTA package must use ZIP_STORED');
            const nameStart = offset + 30;
            const dataStart = nameStart + nameLength + extraLength;
            const dataEnd = dataStart + compressedSize;
            if (dataEnd > bytes.length) throw new Error('ZIP entry is truncated');
            const name = decoder.decode(bytes.subarray(nameStart, nameStart + nameLength));
            entries.set(name, bytes.slice(dataStart, dataEnd));
            offset = dataEnd;
        }
        return entries;
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
        return btoa(binary).replace(/\\+/g, '-').replace(/\\//g, '_').replace(/=+$/g, '');
    }

    function reversed(bytes) {
        return Uint8Array.from(bytes).reverse();
    }

    async function parsePackage(file) {
        const entries = readStoredZip(await file.arrayBuffer());
        const manifestBytes = entries.get('manifest.json');
        if (!manifestBytes) throw new Error('manifest.json is missing');
        const manifest = JSON.parse(new TextDecoder('utf-8').decode(manifestBytes));
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
        const releaseMatch = /^(\\d+)\\.(\\d+)\\.(\\d+)$/.exec(String(manifest.release || ''));
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
                    reject(this.scope.abort(new Error(\`Goodix command 0x\${command.toString(16)} timed out\`)));
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

    async function queryTarget(transport) {
        const info = await transport.request(CMD_GET_INFO);
        if (info.length < 18 || info[0] !== ACK_SUCCESS || info[17] !== 2) {
            throw new Error('target does not report Goodix OTAS v2');
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
            const target = await queryTarget(record.transport);
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
                throw new Error(\`recovery address mismatch: device 0x\${saveAddress.toString(16)}, package 0x\${programAddress.toString(16)}\`);
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
`,d=`/* OnMicro DFU v3 transport for InkTag OM6626.
 * The application first reboots into a fixed BLE Recovery. Recovery then
 * replaces the single application in place and keeps advertising after an
 * interrupted transfer, so the same package can be sent again.
 */
(function (global) {
    'use strict';

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
        [\`\${SIGNED_BOARD}/\${SIGNED_LAYOUT}\`]: { signature: LAYOUT_SIGNATURE, address: 0x2a000, slot: 0x4e000, board: SIGNED_BOARD, boardSignature: BOARD_SIGNATURE, journalSession: true },
        [\`om6626_hsela/\${SIGNED_LAYOUT}\`]: { signature: 0x3156484f, address: 0x2a000, slot: 0x4e000, board: 'om6626_hsela', boardSignature: 0x53484d4f, journalSession: true }
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
            const error = new Error(\`OM Recovery \${phase}超时；本次流程已停止，结果未确认，请只读检查，勿重复写入\`);
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
                if (owner.lastPhase !== phase) onPhase(\`OM Recovery：\${phase}\`);
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
        const view = new DataView(buffer);
        const decoder = new TextDecoder('utf-8');
        const entries = new Map();
        let offset = 0;
        while (offset + 4 <= bytes.length && view.getUint32(offset, true) === 0x04034b50) {
            if (offset + 30 > bytes.length) throw new Error('ZIP local header is truncated');
            const flags = view.getUint16(offset + 6, true);
            const method = view.getUint16(offset + 8, true);
            const compressedSize = view.getUint32(offset + 18, true);
            const nameLength = view.getUint16(offset + 26, true);
            const extraLength = view.getUint16(offset + 28, true);
            if (flags & 0x08) throw new Error('ZIP data descriptors are not supported');
            if (method !== 0) throw new Error('OnMicro OTA package must use ZIP_STORED');
            const nameStart = offset + 30;
            const dataStart = nameStart + nameLength + extraLength;
            const dataEnd = dataStart + compressedSize;
            if (dataEnd > bytes.length) throw new Error('ZIP entry is truncated');
            const name = decoder.decode(bytes.subarray(nameStart, nameStart + nameLength));
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
        const key = \`\${manifest.board}/\${manifest.layout}\`;
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
        if (bytes.length !== 20 ||
            view.getUint32(0, true) !== 0x50524b49 || bytes[4] !== 1 || bytes[5] !== 0 ||
            view.getUint16(6, true) !== 20 || view.getUint32(8, true) !== profile.signature ||
            view.getUint32(12, true) !== profile.slot - SIGNATURE_TRAILER_SIZE ||
            view.getUint32(16, true) !== profile.slot) {
            throw new Error('Recovery preflight is missing or incompatible; a separately authorized fixed-Recovery Full upgrade is required');
        }
        const signature = pkg.manifest.signature || {};
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

    async function parsePackage(file) {
        const entries = readStoredZip(await file.arrayBuffer());
        const manifestBytes = entries.get('manifest.json');
        if (!manifestBytes) throw new Error('manifest.json is missing');
        const manifest = JSON.parse(new TextDecoder('utf-8').decode(manifestBytes));
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
                })[pending.opcode] || \`opcode 0x\${pending.opcode.toString(16)}\`;
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
            if (!this.scope.usable) return;
            try { await this.scope.step('关闭控制通知', () => this.control.stopNotifications()); }
            catch (error) {
                // A successful EXECUTE may already have rebooted into the App.
                // A settled stop rejection is cleanup only; final App attestation
                // remains authoritative. A pending native stop must still fail.
                if (error.code === 'OM_NATIVE_TIMEOUT') throw error;
                this.scope.onPhase(\`OM Recovery 通知清理未完成：\${String(error.message || error)}\`);
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
                    reject(new Error(\`OnMicro DFU opcode 0x\${opcode.toString(16)} timed out\`));
                }, timeoutMs);
            });
            // A notification may reject while the write itself is still pending.
            response.catch(() => {});
            try {
                await this.scope.step(\`写入控制指令 0x\${opcode.toString(16)}\`, () => this.writeControl(bytes), Math.min(timeoutMs, 10000));
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
                throw new Error(\`OnMicro checksum mismatch at \${offset}/\${expectedOffset}\`);
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
                scope.onPhase(\`OM Recovery 清理未确认（保留原始失败）：\${String(error.message || error)}\`);
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
            if (!server) throw new Error(\`OM6626 fixed BLE Recovery did not become ready\${lastError ? \`: \${lastError.message || lastError}\` : ''}\`);
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
            const profile = Object.values(LAYOUT_PROFILES).find(candidate =>
                view.byteLength === 20 && candidate.signature === view.getUint32(8, true));
            if (!profile || view.byteLength !== 20 || view.getUint32(0, true) !== 0x50524b49 ||
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
                } catch (error) {
                    if (error.code === 'RECOVERY_REFUSED') throw error;
                    const unknown = new Error(\`退出 Recovery 结果未知；请只读确认设备状态，不要重复退出或复位：\${error.message || error}\`);
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
`,f=`/* Dialog/Renesas SDK6 SUOTA transport for InkTag DA14585.
 * InkTag keeps the wire protocol while the receiver writes only the single
 * application slot and protects the fixed Recovery image.
 */
(function (global) {
    'use strict';

    const SERVICE_UUID = '0000fef5-0000-1000-8000-00805f9b34fb';
    const MEM_DEV_UUID = '8082caa8-41a6-4021-91c6-56f9b954cc34';
    const GPIO_MAP_UUID = '724249f0-5ec3-4b5f-8804-42345af08651';
    const MEM_INFO_UUID = '6c53db25-47a1-45fe-a022-7c92fb334fd4';
    const PATCH_LEN_UUID = '9d84b9a3-000c-49d8-9183-855b673fda31';
    const PATCH_DATA_UUID = '457871e8-d516-4ca1-9116-57d0b17b9cb2';
    const STATUS_UUID = '5f78df94-798c-46f5-990a-b3eb6a065c88';
    const VERSION_UUID = '64b4e8b5-0de5-401b-a21d-acc8db3b913a';
    const PD_SIZE_UUID = '42c3dfdd-77be-4d9c-8454-8f875267fb3b';

    const SUOTA_IMAGE_SPI_FLASH = 0x13;
    const SUOTA_SERVICE_EXIT = 0xff;
    const SUOTA_IMAGE_END = 0xfe;
    const SUOTA_REBOOT = 0xfd;
    const SUOTA_IMAGE_STARTED = 0x10;
    const SUOTA_COMPLETE_OK = 0x02;
    const SUOTA_SERVICE_EXITED = 0x03;
    const SPI_GPIO_MAP = 0x05060300;
    const MAX_IMAGE_SIZE = 0x1f000 - 0x4000;
    const DEFAULT_BLOCK_SIZE = 488;
    const DEFAULT_CHUNK_SIZE = 244;
    const STATUS_NAMES = {
        0x01: 'service started',
        0x02: 'complete',
        0x03: 'service exited',
        0x04: 'XOR checksum error',
        0x05: 'patch length error',
        0x06: 'external memory error',
        0x07: 'internal memory error',
        0x08: 'invalid memory type',
        0x09: 'application error',
        0x10: 'image transfer started',
        0x11: 'invalid image bank',
        0x12: 'invalid image header',
        0x13: 'invalid image size',
        0x14: 'invalid product header',
        0x15: 'same image version',
        0x16: 'external memory read error',
        0x17: 'invalid image version',
        0x18: 'image version rollback rejected',
        0x19: 'SUOTA session locked until reboot'
    };

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

    function xorChecksum(bytes) {
        let checksum = 0;
        for (const value of bytes) checksum ^= value;
        return checksum;
    }

    function appendXor(bytes) {
        const output = new Uint8Array(bytes.length + 1);
        output.set(bytes);
        output[bytes.length] = xorChecksum(bytes);
        return output;
    }

    function memoryCommand(command, argument = 0) {
        const output = new Uint8Array(4);
        const value = (((command & 0xff) << 24) | (argument & 0x00ffffff)) >>> 0;
        new DataView(output.buffer).setUint32(0, value, true);
        return output;
    }

    function decodeVersion(bytes) {
        let end = 12;
        while (end < 28 && bytes[end] !== 0) end++;
        for (let offset = 12; offset < end; offset++) {
            if (bytes[offset] < 0x20 || bytes[offset] > 0x7e) {
                throw new Error('pQ image version is not ASCII');
            }
        }
        return new TextDecoder('ascii').decode(bytes.subarray(12, end));
    }

    function parseImage(input, fileName = '') {
        const image = input instanceof Uint8Array ? input.slice() : new Uint8Array(input);
        if (image.length < 64 || image.length > MAX_IMAGE_SIZE) {
            throw new Error(\`SUOTA image size is outside 64..\${MAX_IMAGE_SIZE} bytes\`);
        }
        if (image[0] !== 0x70 || image[1] !== 0x51 || image[2] !== 0xaa) {
            throw new Error('not an SDK6 pQ SUOTA image');
        }
        const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
        const codeSize = view.getUint32(4, true);
        const expectedCrc = view.getUint32(8, true);
        if (codeSize + 64 !== image.length) {
            throw new Error(\`pQ code size mismatch: \${codeSize} + 64 != \${image.length}\`);
        }
        const actualCrc = crc32(image.subarray(64));
        if (actualCrc !== expectedCrc) throw new Error('pQ application CRC32 mismatch');
        return {
            fileName,
            image,
            version: decodeVersion(image),
            codeSize,
            crc32: actualCrc,
            wireLength: image.length + 1
        };
    }

    async function parsePackage(file) {
        return parseImage(await file.arrayBuffer(), file.name);
    }

    function bytesFromValue(value) {
        return new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
    }

    function sleep(milliseconds) {
        return new Promise(resolve => setTimeout(resolve, milliseconds));
    }

    async function writeWithResponse(characteristic, bytes) {
        if (characteristic.writeValueWithResponse) return characteristic.writeValueWithResponse(bytes);
        return characteristic.writeValue(bytes);
    }

    async function writeWithoutResponse(characteristic, bytes) {
        if (characteristic.writeValueWithoutResponse) return characteristic.writeValueWithoutResponse(bytes);
        return characteristic.writeValue(bytes);
    }

    class Transport {
        constructor(service, onPhase = () => {}) {
            this.service = service;
            this.onPhase = onPhase;
            this.statusQueue = [];
            this.statusWaiter = null;
            this.onStatus = this.onStatus.bind(this);
        }

        onStatus(event) {
            const data = bytesFromValue(event.target.value);
            if (!data.length) return;
            const status = data[0];
            if (this.statusWaiter) {
                const waiter = this.statusWaiter;
                this.statusWaiter = null;
                clearTimeout(waiter.timer);
                waiter.resolve(status);
            } else {
                this.statusQueue.push(status);
            }
        }

        async open() {
            this.onPhase('正在读取 SUOTA 必需特征…');
            this.memDev = await this.service.getCharacteristic(MEM_DEV_UUID);
            this.gpioMap = await this.service.getCharacteristic(GPIO_MAP_UUID);
            this.patchLen = await this.service.getCharacteristic(PATCH_LEN_UUID);
            this.patchData = await this.service.getCharacteristic(PATCH_DATA_UUID);
            this.status = await this.service.getCharacteristic(STATUS_UUID);
            this.status.addEventListener('characteristicvaluechanged', this.onStatus);
            this.onPhase('正在开启 SUOTA 状态通知…');
            await this.status.startNotifications();
        }

        async close() {
            if (!this.status) return;
            try { await this.status.stopNotifications(); } catch (_) { }
            finally { this.status.removeEventListener('characteristicvaluechanged', this.onStatus); }
        }

        async nextStatus(timeoutMs) {
            if (this.statusQueue.length) return this.statusQueue.shift();
            if (this.statusWaiter) throw new Error('another SUOTA status wait is already active');
            return new Promise((resolve, reject) => {
                const waiter = { resolve, reject, timer: null };
                waiter.timer = setTimeout(() => {
                    if (this.statusWaiter === waiter) this.statusWaiter = null;
                    reject(new Error('SUOTA status timed out'));
                }, timeoutMs);
                this.statusWaiter = waiter;
            });
        }

        async waitStatus(expected, action, timeoutMs = 30000) {
            const deadline = Date.now() + timeoutMs;
            while (true) {
                const remaining = deadline - Date.now();
                if (remaining <= 0) throw new Error(\`\${action} timed out\`);
                const status = await this.nextStatus(remaining);
                if (status === expected) return status;
                if (status !== 0x01) {
                    const detail = STATUS_NAMES[status] || 'unknown error';
                    throw new Error(\`\${action} failed with SUOTA status 0x\${status.toString(16).padStart(2, '0')}: \${detail}\`);
                }
            }
        }

        async command(command, argument = 0) {
            await writeWithResponse(this.memDev, memoryCommand(command, argument));
        }

        rejectQueuedFailure(action) {
            while (this.statusQueue.length) {
                const status = this.statusQueue.shift();
                // A stale block COMPLETE is neither finalization nor App proof.
                if (status !== 0x01 && status !== SUOTA_COMPLETE_OK) {
                    const detail = STATUS_NAMES[status] || 'unknown error';
                    throw new Error(\`\${action} failed with SUOTA status 0x\${status.toString(16).padStart(2, '0')}: \${detail}\`);
                }
            }
        }

    }

    async function transferServer(server, pkg, onProgress = () => {}, options = {}) {
        const blockSize = options.blockSize || DEFAULT_BLOCK_SIZE;
        const chunkSize = options.chunkSize || DEFAULT_CHUNK_SIZE;
        const packetDelayMs = options.packetDelayMs === undefined ? 0 : options.packetDelayMs;
        if (blockSize < 80 || blockSize > 512) throw new Error('SUOTA block size must be between 80 and 512');
        if (chunkSize < 1 || chunkSize > 244) throw new Error('SUOTA chunk size must be between 1 and 244');

        const onPhase = options.onPhase || (() => {});
        onPhase('正在发现 DA14585 SUOTA 服务…');
        const service = await server.getPrimaryService(SERVICE_UUID);
        const transport = new Transport(service, onPhase);
        const payload = appendXor(pkg.image);
        const started = performance.now();
        let sessionStarted = false;
        let finalizationRequested = false;
        await transport.open();
        try {
            const metadata = {};
            onPhase('正在启动外部 SPI Flash 更新会话…');
            await transport.command(SUOTA_IMAGE_SPI_FLASH, 0);
            await transport.waitStatus(SUOTA_IMAGE_STARTED, 'start');
            sessionStarted = true;
            onPhase('正在配置 DA14585 SPI Flash 引脚…');
            const gpio = new Uint8Array(4);
            new DataView(gpio.buffer).setUint32(0, SPI_GPIO_MAP, true);
            await writeWithResponse(transport.gpioMap, gpio);

            let offset = 0;
            while (offset < payload.length) {
                const block = payload.slice(offset, offset + blockSize);
                const length = new Uint8Array(2);
                new DataView(length.buffer).setUint16(0, block.length, true);
                await writeWithResponse(transport.patchLen, length);
                for (let chunkOffset = 0; chunkOffset < block.length; chunkOffset += chunkSize) {
                    await writeWithoutResponse(transport.patchData, block.slice(chunkOffset, chunkOffset + chunkSize));
                    if (packetDelayMs) await sleep(packetDelayMs);
                }
                await transport.waitStatus(SUOTA_COMPLETE_OK, \`block at offset \${offset}\`);
                offset += block.length;
                onProgress(offset, payload.length, started);
            }

            transport.rejectQueuedFailure('image finalize');
            // FE's ATT response precedes intentional disconnect and offline
            // validation. Never wait for connected final02 or replay FE/FD/FF.
            // The parent must attest the submitted size/CRC in the returned App.
            finalizationRequested = true;
            let finalizationWriteConfirmed = false;
            try {
                await transport.command(SUOTA_IMAGE_END);
                finalizationWriteConfirmed = true;
                onPhase('结束请求已发送，正在断连并等待应用验身…');
            } catch (_) {
                onPhase('结束请求结果未知；不重复发送，只核对返回应用…');
            }
            transport.rejectQueuedFailure('image finalize');
            return { version: pkg.version, metadata, finalizationWriteConfirmed, applicationVerified: false };
        } catch (error) {
            if (sessionStarted && !finalizationRequested) {
                onPhase('SUOTA 失败，正在退出升级会话…');
                try {
                    await transport.command(SUOTA_SERVICE_EXIT);
                    await transport.waitStatus(SUOTA_SERVICE_EXITED, 'service exit', 5000);
                } catch (_) { }
            }
            throw error;
        } finally {
            await transport.close();
            if (finalizationRequested) {
                // Ordinary disconnect, not the old forced FD/reset command.
                try { if (server.connected) server.disconnect(); } catch (_) { }
                transport.rejectQueuedFailure('image finalize');
            }
        }
    }

    async function transferDevice(device, pkg, onProgress, options) {
        let lastError;
        for (let attempt = 0; attempt < 20; attempt++) {
            try {
                const server = device.gatt.connected ? device.gatt : await device.gatt.connect();
                await server.getPrimaryService(SERVICE_UUID);
                return transferServer(server, pkg, onProgress, options);
            } catch (error) {
                lastError = error;
                try { if (device.gatt.connected) device.gatt.disconnect(); } catch (_) { }
                await sleep(attempt === 0 ? 900 : 500);
            }
        }
        throw new Error(\`连接 DA14585 固定 Recovery 超时：\${lastError ? lastError.message : '设备未出现'}\`);
    }

    async function validateDevice(device) {
        const server = device.gatt.connected ? device.gatt : await device.gatt.connect();
        await server.getPrimaryService(SERVICE_UUID);
        return device;
    }

    async function queryRecovery(server) {
        const service = await server.getPrimaryService(SERVICE_UUID);
        const version = await service.getCharacteristic(VERSION_UUID);
        const data = bytesFromValue(await version.readValue());
        return { safeResume: data.length === 5 &&
            data.every((v, i) => v === [13, 73, 75, 1, 1][i]) };
    }

    const unknownExits = new WeakSet();
    async function resumeApplication(server, options = {}) {
        const verifiedName = options.deviceId === server.device?.id ? options.recoveryName : null;
        if (!/^(?:IKT|InkTag)_DA_[0-9a-f]{4}_OTA$/i.test(verifiedName || server.device?.name || '')) {
            throw new Error('请先确认同设备 DA 固定 Recovery 身份');
        }
        if (unknownExits.has(server.device)) {
            throw Object.assign(new Error('退出结果未知，不要重发或强制复位'), {code:'RECOVERY_RESULT_UNKNOWN'});
        }
        if (!(await queryRecovery(server)).safeResume) throw new Error('该 Recovery 不支持安全退出');
        const service = await server.getPrimaryService(SERVICE_UUID);
        const status = await service.getCharacteristic(STATUS_UUID);
        const memDev = await service.getCharacteristic(MEM_DEV_UUID);
        let armed = false, accept, reject, timer;
        const onStatus = event => { if (armed) accept(bytesFromValue(event.target.value)); };
        status.addEventListener('characteristicvaluechanged', onStatus);
        try {
            await status.startNotifications();
            await status.readValue(); // Drain stale attribute/notifications before ownership.
            const reply = new Promise((resolve, fail) => { accept = resolve; reject = fail; });
            timer = setTimeout(() => reject(new Error('fresh DA exit ACK timed out')), options.timeoutMs || 10000);
            armed = true;
            unknownExits.add(server.device);
            let data;
            try {
                // Attach rejection handling before a possibly slow/failed ATT write.
                const pending = reply.then(value => ({value}), error => ({error}));
                await writeWithResponse(memDev, memoryCommand(0xfc));
                const outcome = await pending;
                if (outcome.error) throw outcome.error;
                data = outcome.value;
            } catch (_) {
                throw Object.assign(new Error('退出结果未知；只读确认，不要重发或强制复位'), {code:'RECOVERY_RESULT_UNKNOWN'});
            }
            if (data.length === 1 && data[0] === 2) { unknownExits.delete(server.device); return; }
            if (data.length && data[0] === 6) {
                unknownExits.delete(server.device);
                throw new Error(global.InkTagOtaRejection.describe('SAFE_RESUME_APP', data, {errorStatus:6}));
            }
            if (data.length === 1 && [8,25].includes(data[0])) {
                unknownExits.delete(server.device);
                throw new Error(\`target rejected SAFE_RESUME_APP: SUOTA status \${data[0]}\`);
            }
            throw Object.assign(new Error('退出 ACK 无法确认；不要重发'), {code:'RECOVERY_RESULT_UNKNOWN'});
        } finally {
            armed = false; clearTimeout(timer);
            status.removeEventListener('characteristicvaluechanged', onStatus);
            try { await status.stopNotifications(); } catch (_) { }
        }
    }

    global.DialogSuota = {
        SERVICE_UUID,
        MEM_DEV_UUID,
        GPIO_MAP_UUID,
        MEM_INFO_UUID,
        PATCH_LEN_UUID,
        PATCH_DATA_UUID,
        STATUS_UUID,
        VERSION_UUID,
        PD_SIZE_UUID,
        MAX_IMAGE_SIZE,
        SPI_GPIO_MAP,
        crc32,
        xorChecksum,
        appendXor,
        memoryCommand,
        parseImage,
        parsePackage,
        validateDevice,
        queryRecovery,
        resumeApplication,
        transferServer,
        transferDevice
    };
})(typeof window !== 'undefined' ? window : globalThis);
`,p=`/* Telink TLSR8359 fixed-Recovery transport for InkTag. */
(function (global) {
    'use strict';

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

    function layoutProfile(layout) {
        if (layout === SIGNED_LAYOUT) {
            return { limit: APP_LIMIT, signature: LAYOUT_SIGNATURE };
        }
        throw new Error('not a supported TLSR8359 fixed-Recovery layout');
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
        const expected = signedImageContext(firmware);
        if (limit < 0x1000 || limit > 0x40000 || limit % 0x1000 || firmware.length > limit || epoch > 0xffff ||
            epoch !== expected.epoch || expected.layout !== LAYOUT_SIGNATURE || view.getUint32(12, true) !== expected.layout ||
            view.getUint32(16, true) !== expected.board) {
            throw new Error('TLSR8359 Recovery is incompatible with the signed image; Full is required');
        }
    }

    async function checkRecoveryCompatibility(service, firmware, step = async (_phase, operation) => operation()) {
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
    }

    function readStoredZip(buffer) {
        const bytes = new Uint8Array(buffer);
        const view = new DataView(buffer);
        const decoder = new TextDecoder('utf-8');
        const entries = new Map();
        let offset = 0;
        while (offset + 4 <= bytes.length && view.getUint32(offset, true) === 0x04034b50) {
            if (offset + 30 > bytes.length) throw new Error('ZIP local header is truncated');
            const flags = view.getUint16(offset + 6, true);
            const method = view.getUint16(offset + 8, true);
            const compressedSize = view.getUint32(offset + 18, true);
            const nameLength = view.getUint16(offset + 26, true);
            const extraLength = view.getUint16(offset + 28, true);
            if (flags & 0x08) throw new Error('ZIP data descriptors are not supported');
            if (method !== 0) throw new Error('TLSR8359 OTA package must use ZIP_STORED');
            const nameStart = offset + 30;
            const dataStart = nameStart + nameLength + extraLength;
            const dataEnd = dataStart + compressedSize;
            if (dataEnd > bytes.length) throw new Error('ZIP entry is truncated');
            const name = decoder.decode(bytes.subarray(nameStart, nameStart + nameLength));
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
        const buffer = await file.arrayBuffer();
        let firmware;
        let manifest = null;
        let profile;
        if (buffer.byteLength >= 4 && new DataView(buffer).getUint32(0, true) === 0x04034b50) {
            const entries = readStoredZip(buffer);
            const manifestBytes = entries.get('manifest.json');
            if (!manifestBytes) throw new Error('manifest.json is missing');
            manifest = JSON.parse(new TextDecoder('utf-8').decode(manifestBytes));
            profile = layoutProfile(manifest.layout);
            if (manifest.format !== 'inktag-ota-v1' ||
                manifest.transport !== 'telink-fixed-recovery-v2' ||
                manifest.board !== SIGNED_BOARD) {
                throw new Error('not an InkTag Hanshow TLSR8359 fixed-Recovery package');
            }
            const application = manifest.application || {};
            firmware = entries.get(application.file);
            if (!firmware) throw new Error('application payload is missing');
            const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', firmware));
            if (hex(digest) !== String(application.sha256 || '').toLowerCase()) {
                throw new Error('package SHA-256 verification failed');
            }
        } else throw new Error('TLSR8359 OTA requires a signed manifest-bearing ZIP package');
        if (firmware.length < 32 + SIGNATURE_TRAILER_SIZE || firmware.length > profile.limit) {
            throw new Error(\`TLSR8359 应用大小 \${firmware.length} 超出单槽限制 \${profile.limit}\`);
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
        if (applicationSize + SIGNATURE_TRAILER_SIZE !== firmware.length) {
            throw new Error('Telink 固件声明长度与文件长度不一致');
        }
        const expectedCrc = view.getUint32(declaredSize, false);
        const actualCrc = crc32(firmware.subarray(0, declaredSize));
        if (expectedCrc !== actualCrc) throw new Error('Telink 固件 CRC32 校验失败');
        await verifyPublisherSignature(firmware, manifest, applicationSize, profile);
        if (manifest) {
            const application = manifest.application;
            if (application.size !== applicationSize ||
                application.transfer_size !== firmware.length ||
                application.declared_size !== declaredSize ||
                application.crc32 !== actualCrc ||
                application.active_address !== 0) {
                throw new Error('TLSR8359 application/Recovery layout does not match the manifest');
            }
        }
        return { fileName: file.name, manifest, firmware, declaredSize, crc32: actualCrc };
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
        if (error && error.code === 'TLSR_RECOVERY_DISCOVERY_TIMEOUT') return false;
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
            if (!owner.programming) note(\`TLSR Recovery：\${phase}\`);
            const phaseLimit = phase === '发现升级服务' ? timeoutMs : phase === '建立 GATT 连接' ? 10000 : 5000;
            const remaining = owner.programming ? 10000 : Math.min(phaseLimit, deadline - Date.now());
            const phaseStarted = Date.now();
            if (remaining <= 0) { abort(); throw sessionError('TLSR Recovery 发现超时', 'TLSR_RECOVERY_DISCOVERY_TIMEOUT'); }
            return new Promise((resolve, reject) => {
                let expired = false;
                const timer = setTimeout(() => {
                    expired = true; abort();
                    const error = sessionError(\`TLSR Recovery \${phase}超时；结果未知，未重放\`,
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
                        if (!owner.programming) note(\`TLSR Recovery：\${phase}完成（\${Date.now() - phaseStarted} ms）\`);
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
        verifiedRecoveryStatus(await readStatus(characteristic));
        let reply;
        try {
            // Exactly one guarded request. Never reconnect/replay or fall back
            // to reset. Recovery revalidates the complete signed App and power.
            await writeWithResponse(characteristic, new Uint8Array([0x03, 0xff]));
            const deadline = Date.now() + 15000;
            do {
                await delay(50);
                reply = verifiedRecoveryStatus(await readStatus(characteristic));
                if (reply.status !== STATUS_RECEIVING) break;
            } while (Date.now() < deadline);
        } catch (cause) {
            const error = new Error(\`退出 Recovery 结果未知；请只读确认设备角色，不要重复退出或复位：\${String(cause)}\`);
            error.code = 'RECOVERY_RESULT_UNKNOWN';
            throw error;
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
        throw new Error(\`SAFE_RESUME_APP 拒绝：Recovery 状态 \${hex(reply.raw)}\`);
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
        try {
        if (!prepared) await checkRecoveryCompatibility(characteristic.service, pkg.firmware, discovery.step);
        const useWriteResponse = characteristicSupportsWriteResponse(characteristic);
        let useNotifications = Boolean(characteristic.properties &&
            characteristic.properties.notify);
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
        let status = resume ? await discovery.step('读取升级状态', () => readStatus(characteristic)) : await beginTransfer();
        if (resume && status[0] === STATUS_COMPLETE) {
            if (device.gatt.connected) device.gatt.disconnect();
            return;
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
                await discovery.step('开启升级通知', () => characteristic.startNotifications());
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
                        throw new Error(\`TLSR8359 Recovery 在数据块 \${index}..\${batchEnd - 1} 拒绝传输（\${hex(status)}）\`);
                    }
                    const acknowledged = acknowledgedIndex(status);
                    if (acknowledged === batchEnd) {
                        if (packetNotifications && notificationMisses) {
                            notificationRecoveries++; notificationMisses = 0;
                        }
                        break;
                    }
                    if (acknowledged > batchEnd) {
                        throw new Error(\`TLSR8359 Recovery 返回超前确认 \${acknowledged}，当前数据块结束于 \${batchEnd}（\${hex(status)}）\`);
                    }
                    if (performance.now() >= deadline) {
                        throw new Error(\`TLSR8359 Recovery 未确认数据块 \${index}..\${batchEnd - 1}（\${hex(status)}）\`);
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
            // The remaining phases use explicit reads, not queued notifications.
            detachNotifications();
            await delay(50);
            status = await readStatus(characteristic);
            if (status[0] !== STATUS_RECEIVING || acknowledgedIndex(status) !== blocks) {
                throw new Error('TLSR8359 Recovery 在结束前拒绝传输');
            }
            const end = new Uint8Array(6);
            const endView = new DataView(end.buffer);
            const maximum = blocks - 1;
            end.set([0x02, 0xff]);
            endView.setUint16(2, maximum, true);
            endView.setUint16(4, maximum ^ 0xffff, true);
            if (useWriteResponse) await writeWithResponse(characteristic, end);
            else await writeWithoutResponse(characteristic, end);
            // Recovery hashes committed Flash in cooperative 64-byte slices, then
            // performs one P-256 verification. RECEIVING remains the truthful
            // status until both checks finish.
            const validationDeadline = performance.now() + 10000;
            for (;;) {
                await delay(50);
                status = await readStatus(characteristic);
                if (status[0] === STATUS_COMPLETE) break;
                if (status[0] !== STATUS_RECEIVING) {
                    throw new Error(\`TLSR8359 Recovery 拒绝固件，状态 \${hex(status)}\`);
                }
                if (performance.now() >= validationDeadline) {
                    throw new Error('TLSR8359 Recovery 固件验证超时');
                }
            }
            try { options.onDiagnostics?.({
                payloadBytes: pkg.firmware.length, packets, payloadLimit: fastBlocks * 16,
                windowPackets: 1, notificationFallbacks, notificationRecoveries,
                quarterSeconds: [...quarterSeconds, dataSeconds],
                validationSeconds: Number(((performance.now() - started) / 1000 - dataSeconds).toFixed(3)),
            }); } catch (_) { /* optional timing must not change the result */ }
            if (device.gatt.connected) device.gatt.disconnect();
        } finally {
            detachNotifications();
        }
        } finally {
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
`;async function m(e=async()=>{await a.waitForPasswordAuthorization(),await a.syncTime()}){try{return await e(),null}catch(e){return e instanceof Error?e.message:String(e)}}function h(e,t){if(!e||e.appBytes!==t.codeSize||e.checksum!==t.crc32)throw Error(`DA14585 重连后的应用与升级包不一致（app_bytes=${e?.appBytes??0}, crc32=${e?.checksum??0}；期望 app_bytes=${t.codeSize}, crc32=${t.crc32}）`)}function g(e,t){let n=Number(t.manifest.compatibilityEpoch),r=Number(t.application.size),i=Number(t.application.checksum);if(!e||!Number.isInteger(r)||r<=0||e.version!==n||e.appBytes!==r||e.checksum!==i)throw Error(`GR 重连后的应用与升级包不一致（epoch=${e?.version??-1}, app_bytes=${e?.appBytes??0}, checksum=${e?.checksum??0}；期望 epoch=${n}, app_bytes=${r}, checksum=${i}）`)}var _=!1;function v(){if(!_){for(let e of[l,c,u,d,f,p]){let t=document.createElement(`script`);t.textContent=e,document.head.appendChild(t),t.remove()}_=!0}}function y(e){return e===`nordic`?`已签名的 Nordic application-only OTA ZIP`:e===`goodix`?`与当前 GR 主板匹配并带发布签名的 InkTag OTA ZIP`:e===`onmicro`?`与当前 OM6626 主板匹配并带发布签名的单槽 Recovery OTA ZIP`:e===`dialog`?`DA14585 SDK6 pQ SUOTA .img`:e===`telink`?`TLSR8359 单槽 Recovery OTA ZIP（旧版裸 .bin 仍可用于调试）`:`连接设备后识别更新格式`}function b(e,t,n){let a=r(t),o=i(a),s=e===`goodix`?n===`gr551x_zkc42v`&&[`gr551x_zkc42vm`,`gr551x_zkc42v`].includes(a):n===`om6626_zkcxxb`&&[`om6626_zkc21b`,`om6626_zkc42b`,`om6626_zkc21v`].includes(a);if(!a||(o?o.ota!==e||o.packageTarget!==n:a!==n&&!s))throw Error(`固件与当前设备的固件系列不匹配`)}function x(e,t){let n=r(e);if(n===`tlsr8359_hanshow`){if(t?.board!==`tlsr8359_hanshow`)throw Error(`固件与当前 TLSR8359 固件系列不匹配`);return}if(e&&!new Set([`hs_nowa154`,`hs_nebular420`]).has(n))throw Error(`当前设备 ${e} 不是已识别的 TLSR8359 固件目标`);let i=t?.board,a=(t?.compatible_boards||[]).map(r);if(e&&i&&r(i)!==n&&!(i===`tlsr8359_hanshow`&&a.includes(n)))throw Error(`固件目标为 ${i}，当前设备固件标识为 ${n}`)}async function S(e,t,n){if(v(),e===`none`)throw Error(`当前设备不支持 Web OTA`);if(e===`nordic`){let n=await window.NordicDfu.parsePackage(t);return{type:e,payload:n,summary:`${t.name} · 应用 ${n.firmware.length} bytes · 签名初始化包 ${n.initPacket.length} bytes`}}if(e===`goodix`){let r=await window.GoodixOta.parsePackage(t);return n&&b(`goodix`,n,r.manifest.board),{type:e,payload:r,summary:`${t.name} · ${r.manifest.chip} / ${r.manifest.board} · 版本 ${r.manifest.release} (${r.manifest.releaseVersion}) · 兼容纪元 ${r.manifest.compatibilityEpoch} · 应用 ${r.application.size} bytes · 已验证签名密钥 ${r.manifest.signature.key_id}`}}if(e===`onmicro`){let r=await window.OnMicroOta.parsePackage(t);return n&&b(`onmicro`,n,r.manifest.board),{type:e,payload:r,summary:`${t.name} · ${r.manifest.chip} / ${r.manifest.board} · 应用 ${r.application.size} bytes · 已验证签名密钥 ${r.manifest.signature.key_id}`}}if(e===`telink`){let r=await window.TelinkOta.parsePackage(t);return x(n,r.manifest),{type:e,payload:r,summary:`${t.name} · TLSR8359 单应用 ${r.firmware.length} bytes · CRC32 ${r.crc32.toString(16).padStart(8,`0`).toUpperCase()}`}}if(n&&n!==`da14585_aesl`&&n!==`aesl42_da`&&n!==`aesl29_da`)throw Error(`当前主板 ${n} 不是 DA14585 AESL 目标`);let r=await window.DialogSuota.parsePackage(t);return{type:e,payload:r,summary:`${t.name} · SDK6 pQ ${r.version} · 应用 ${r.codeSize} bytes`}}async function C(e,t,n,r){let i=Date.now()+45e3;for(let o=0;o<t;o++){let s=i-Date.now();if(s<=0)break;await new Promise(e=>setTimeout(e,o===0?1800:900)),r?.before(),n({percent:100,message:`设备正在重启，尝试自动重连 ${o+1}/${t}`});try{if(await a.reconnectDevice(e,Math.min(1e4,s)),r?.after(),a.getOtaRecoveryType()===null)return!0;r?await r.releaseRecovery():await a.disconnectAndWait()}catch{r?.before()}}return!1}async function w(e){v(),await window.GoodixOta.queryRecovery(e)}async function T(e){return v(),await window.TelinkOta.queryRecovery(e)}async function E(e){let t=n.getState();if(t.action!==`idle`)throw Error(`请等待当前操作结束`);let r=a.getDevice(),i=a.getOtaRecoveryType()===`telink`?a.getVerifiedRecoveryName():``,o=a.getExpectedRecoveryIdentity()||(r&&i?{deviceId:r.id,recoveryName:i}:null);if(!o||!/^(?:IKT|InkTag)_TL_[0-9A-F]{4}_OTA$/i.test(o.recoveryName))throw Error(`没有本页已核对的 TLSR Recovery 交接身份；请先通过正常入口进入 Recovery`);let s=a.getRecoveryHandoffDevice(),c=navigator.bluetooth,l=(r?.id===o.deviceId?[r]:s?.id===o.deviceId?[s]:await c?.getDevices?.()||[]).find(e=>e.id===o.deviceId);if(!l)throw Error(`原设备授权不可用，请从正常蓝牙入口只读确认同一设备`);t.setAction(`ota`,0,`正在读取 Recovery 状态`);try{let t=a.getExpectedRecoveryIdentity()||(i?o:null);if(t?.deviceId!==o.deviceId||t.recoveryName!==o.recoveryName)throw Error(`Recovery 交接身份已变化，已停止读取`);if(a.getExpectedRecoveryIdentity()?await a.releaseForRecovery(o):await a.disconnectAndWait(900),a.getDevice())throw Error(`设备连接已变化，已停止读取`);v();let n=await window.TelinkOta.queryRecoveryDevice(l,{onPhase:e});return`Recovery 状态 ${n.status} · ${Array.from(n.raw,e=>e.toString(16).padStart(2,`0`)).join(` `)}`}finally{n.getState().setAction(`idle`)}}async function D(e){let t=n.getState(),r=a.getDevice(),i=a.getGattServer();if(t.action!==`idle`)throw Error(`请等待当前操作结束`);if(!r||!i?.connected||a.getOtaRecoveryType()!==`nordic`||!/^(?:IKT|InkTag)_NRF_[0-9A-F]{4}_OTA$/i.test(a.getVerifiedRecoveryName()))throw Error(`请先从正常蓝牙入口连接并核对同一台 Nordic Recovery`);t.setAction(`ota`,0,`正在读取 Recovery 状态`);try{v();let t=await window.NordicDfu.queryRecoveryState(i,{onPhase:e}),n=e=>`offset=${e.offset} CRC=${e.crc.toString(16).toUpperCase().padStart(8,`0`)} max=${e.maxSize}`;return`Recovery 只读状态 · INIT ${n(t.command)} · DATA ${n(t.data)}`}finally{n.getState().setAction(`idle`)}}async function O(e){return v(),await window.OnMicroOta.queryRecovery(e)}async function k(e){v();try{return await window.DialogSuota.queryRecovery(e)}catch{return{safeResume:!1}}}async function A(e){v();try{return await window.NordicDfu.queryRecovery(e)}catch{return{safeResume:!1}}}var j=new WeakSet;async function M(t,r,i){if(j.has(i)&&a.getExpectedRecoveryIdentity())throw Error(`进入 Recovery 结果尚未确认；请只读重新连接确认，不要重复进入或复位`);j.add(i);let o=n.getState();try{await a.enterOtaRecovery()}catch(t){throw t instanceof s&&[e.UNKNOWN_COMMAND,e.UNSUPPORTED,e.INVALID_REQUEST,e.INVALID_STATE,e.BUSY,e.DENIED,e.LOCKED,e.LOW_POWER].includes(t.status)&&j.delete(i),t}let c=a.getExpectedRecoveryIdentity();if(!c||c.deviceId!==r.id)throw Error(`Recovery 交接身份尚未确认；不要重复进入或复位`);let l=()=>{let e=a.getExpectedRecoveryIdentity();if(!e||e.deviceId!==c.deviceId||e.recoveryName!==c.recoveryName)throw Error(`Recovery 交接已取消或身份已改变；请只读确认设备状态，不要重复进入或复位`)};return l(),t===`goodix`&&(v(),await window.GoodixOta.enterRecovery(i,{isCurrent:()=>{let e=a.getExpectedRecoveryIdentity();return n.getState().action===`ota`&&e?.deviceId===c.deviceId&&e.recoveryName===c.recoveryName},onStage:e=>o.addLog(e)}),l()),o.addLog(`设备已接受进入 Recovery，正在等候角色切换`),await a.releaseForRecovery(c),l(),{expected:c,requireHandoff:l}}async function N(e){let t=n.getState(),r=a.getDevice(),i=a.getGattServer(),o=t.capabilities.ota;if(!i?.connected||!r||t.connection!==`connected`||t.deviceId!==e||r.id!==e||t.firmwareVersion===`Recovery`||t.protocolVersion!==16||o===`none`)throw Error(`请先连接并确认同一台支持 Recovery 的 v1 应用设备`);if(t.action!==`idle`)throw Error(`请等待当前操作结束`);if(j.has(i)&&a.getExpectedRecoveryIdentity())throw Error(`进入 Recovery 结果尚未确认；请只读重新连接确认，不要重复进入或复位`);t.setAction(`ota`,0,`正在进入 Recovery`);try{let{expected:n,requireHandoff:s}=await M(o,r,i),c=Date.now()+45e3,l;for(let i=0;i<9&&Date.now()<c;i++){await new Promise(e=>setTimeout(e,i===0?1800:900)),s();try{if(await a.reconnectDevice(r,Math.min(1e4,Math.max(1,c-Date.now()))),a.getDevice()?.id===e&&a.getOtaRecoveryType()===o&&a.getVerifiedRecoveryName()===n.recoveryName){t.addLog(`同一设备的 Recovery 角色已核对；可更新固件或按设备能力安全退出`,`success`);return}throw Error(`连接已恢复，但没有核对到期望的 Recovery 角色`)}catch(e){l=e}}throw Error(`进入请求已接受，但 Recovery 回连尚未确认；请从附近设备选择 ${n.recoveryName} 只读确认，不要重复进入或复位${l?`：${String(l)}`:``}`)}finally{n.getState().setAction(`idle`)}}var P=new WeakSet;async function F(e){let t=a.getDevice(),i=a.getGattServer();if(!t||!i?.connected||a.getOtaRecoveryType()!==e)throw Error(`请先连接对应的固定 Recovery`);if(n.getState().action!==`idle`)throw Error(`请等待当前操作结束`);if((e===`dialog`||e===`nordic`)&&!a.getRecoverySafeResumeSupported())throw Error(`该 Recovery 不支持安全退出`);if(P.has(i))throw Object.assign(Error(`退出 Recovery 结果未知；请只读确认状态，不要重复退出或复位`),{code:`RECOVERY_RESULT_UNKNOWN`});v();let o=e===`goodix`?window.GoodixOta:e===`telink`?window.TelinkOta:e===`dialog`?window.DialogSuota:e===`nordic`?window.NordicDfu:window.OnMicroOta,s=e===`dialog`||e===`nordic`?a.getRecoveryOriginalImage():null;try{e===`dialog`||e===`nordic`?await o.resumeApplication(i,{deviceId:t.id,recoveryName:a.getVerifiedRecoveryName()}):await o.resumeApplication(i)}catch(e){throw e?.code===`RECOVERY_RESULT_UNKNOWN`&&P.add(i),e}if(n.getState().addLog(`Recovery 已接受安全退出，正在等待原应用`,`info`),await a.disconnectAndWait(),!await C(t,9,()=>void 0))throw Error(`退出请求已接受，但原应用尚未重连；请重新选择设备确认，不要重复退出或复位`);if(e===`dialog`||e===`nordic`){let t=a.getRuntimeImageInfo(),i=r(n.getState().boardId||``),o=e===`dialog`?95648:i===`nrf52832_priceetag`?163840:131072;if(!t||!t.appBytes||t.appBytes>o||s&&(s.appBytes!==t.appBytes||s.checksum!==t.checksum))throw Error(`已重连，但原应用读回尚未核验；不要重复退出或复位`)}n.getState().addLog(`已退出 Recovery，原应用已重连`,`success`)}var I=()=>F(`goodix`),L=()=>F(`telink`),R=()=>F(`onmicro`),z=()=>F(`dialog`),B=()=>F(`nordic`),V=0;async function H(i,c){v();let l=a.getGattServer(),u=a.getDevice();if(!l?.connected||!u)throw Error(`请先连接应用设备`);let d=n.getState();if(i.type===`goodix`&&d.action!==`idle`)throw Error(`请等待当前操作结束`);let f=i.type===`onmicro`&&a.getOtaRecoveryType()===`onmicro`,p=i.type===`dialog`&&a.getOtaRecoveryType()===`dialog`,_=i.type===`telink`&&a.getOtaRecoveryType()===`telink`,y=i.type===`goodix`&&a.getOtaRecoveryType()===`goodix`,x=i.type===`goodix`?++V:0,S,w,T=()=>{if(x!==V||n.getState().action!==`ota`)throw new DOMException(`GR 升级操作已取消或变化`,`AbortError`)},E=()=>{T();let e=a.getExpectedRecoveryIdentity();if(!S||e?.deviceId!==S.deviceId||e.recoveryName!==S.recoveryName)throw new DOMException(`GR 升级交接已取消或身份变化`,`AbortError`)},D=()=>{if(T(),a.getDevice()?.id!==u.id||!a.getGattServer()?.connected||a.getOtaRecoveryType()!==null||a.getConnectionGeneration()!==w)throw new DOMException(`GR 应用连接已取消或变化，未继续验身或校时`,`AbortError`)},O,k=!1,A=async e=>{try{await window.NordicDfu.transferDevice(e,i.payload,N,{maxPacketSize:[`nrf52832_priceetag`,`aesl22_nrf`].includes(r(d.boardId||``))?200:20,onPhase:e=>{let t=n.getState();t.addLog(e),c({percent:(t.actionProgress||0)*100,message:e})}})}catch(e){if(!(e instanceof Error)||e.code!==`NORDIC_FINAL_EXECUTE_ACK_UNKNOWN`)throw e;k=!0}};d.setAction(`ota`,0),d.setOtaPhase(a.getOtaRecoveryType()?`recovery-transfer`:`connecting-recovery`);let N=(e,t,n)=>{let r=t?e*100/t:0,i=Math.max((performance.now()-n)/1e3,.001);d.setAction(`ota`,r/100),e>0&&t>0&&d.setOtaPhase(`recovery-transfer`),c({percent:r,message:`正在传输 ${e}/${t} bytes · ${(e/i/1024).toFixed(1)} KiB/s`})};try{if((i.type===`goodix`&&!y||i.type===`onmicro`&&!f)&&b(i.type,n.getState().boardId,i.payload.manifest.board),i.type===`onmicro`&&!f){c({percent:0,message:`正在读取升级前的 Recovery 状态`});try{let e=await a.queryRuntimeBuild();O=o(e)}catch(e){if(e instanceof Error&&e.message.includes(`固定 Recovery`))throw e;let t=e instanceof Error?e.message:String(e);throw Error(`无法读取升级前的 Recovery 状态：${t}。尚未发送升级包，请先检查连接与设备日志`)}}if(i.type===`goodix`){c({percent:0,message:y?`已连接 GR 固定 Recovery，正在重新发送完整应用`:`正在切换到 Goodix 固定恢复模式`});let e=x,t=y?a.takeGoodixRecoveryForOta(u.id):(await M(`goodix`,u,l)).expected;S=t;let r=a.getConnectionGeneration(),o=await window.GoodixOta.acquireRecoverySession({device:u,generation:e,isCurrent:()=>{let i=a.getExpectedRecoveryIdentity();return e===V&&r===a.getConnectionGeneration()&&n.getState().action===`ota`&&i?.deviceId===t.deviceId&&i.recoveryName===t.recoveryName},onStage:e=>{n.getState().addLog(e),c({percent:(n.getState().actionProgress??0)*100,message:e})}},i.payload),s=!1;try{await window.GoodixOta.transferRecoverySession(o,i.payload,N)}catch(e){throw s=!0,e}finally{try{await window.GoodixOta.releaseRecoverySession(o)}catch(e){if(!s)throw e;d.addLog(`GR Recovery 清理未确认（保留原始失败）：${String(e)}`,`warning`)}}}else if(i.type===`onmicro`)f?c({percent:0,message:`已连接 OM6626 固定 Recovery，正在重新发送完整应用`}):(c({percent:0,message:`正在切换到 OM6626 固定 Recovery`}),await a.enterOtaRecovery(),await a.disconnectAndWait(900)),await window.OnMicroOta.transferDevice(u,i.payload,N,{onPhase:e=>d.addLog(e)});else if(i.type===`dialog`)p?c({percent:0,message:`已连接 DA14585 固定 Recovery，正在重新发送完整应用`}):(c({percent:0,message:`正在切换到 DA14585 固定 Recovery`}),await a.enterOtaRecovery()),await window.DialogSuota.transferDevice(u,i.payload,N,{onPhase:e=>c({percent:0,message:e})});else if(i.type===`telink`){let t;if(_)c({percent:0,message:`已连接 TLSR8359 固定 Recovery，正在重新发送完整应用`}),t=a.takeTelinkRecoveryForOta(u.id);else{if(c({percent:0,message:`正在切换到 TLSR8359 固定 Recovery`}),j.has(l))throw Error(`TLSR 进入结果尚未确认；不要重复进入或复位`);j.add(l);try{await a.enterOtaRecovery()}catch(t){throw t instanceof s&&[e.UNKNOWN_COMMAND,e.UNSUPPORTED,e.INVALID_REQUEST,e.INVALID_STATE,e.BUSY,e.DENIED,e.LOCKED,e.LOW_POWER].includes(t.status)&&j.delete(l),t}t=await a.releaseTelinkAppForOta(u.id)}let r=a.getConnectionGeneration(),o=await window.TelinkOta.acquireRecoverySession({device:u,generation:r,isCurrent:()=>{let e=a.getExpectedRecoveryIdentity();return a.getConnectionGeneration()===r&&e?.deviceId===t.deviceId&&e.recoveryName===t.recoveryName},afterAppEntry:!_,onDiagnostics:e=>n.getState().addLog(`TLSR OTA 统计：${JSON.stringify(e)}`),onStage:e=>{n.getState().addLog(e),c({percent:0,message:e})}},i.payload),d;try{await window.TelinkOta.transferRecoverySession(o,i.payload,N)}catch(e){throw d=e,e}finally{try{await window.TelinkOta.releaseRecoverySession(o)}catch(e){if(!d)throw e}}}else{let e=window.NordicDfu;if(a.getOtaRecoveryType()===`nordic`)c({percent:0,message:`已连接 nRF Secure DFU，正在发送完整应用`}),await A(u);else{let t=n.getState(),r=e.bootloaderName(``,t.bleAddress);c({percent:0,message:`正在切换到 ${r}`}),await a.enterOtaRecovery();let i=a.getExpectedRecoveryIdentity();if(!i||i.deviceId!==u.id||i.recoveryName!==r)throw Error(`无法确认升级前的 nRF 设备身份；不要重复请求进入 Recovery`);let o=await e.findGrantedBootloaderDevice(r,i,u,e=>{n.getState().addLog(e),c({percent:0,message:e})});o||=(n.getState().addLog(`已授权设备未通过 Recovery 通道校验，需要在蓝牙列表重新选择`),c({percent:0,message:`请在设备列表选择 ${r}`}),await e.requestBootloaderDevice(r,i)),await A(o)}}c({percent:100,message:k?`末尾 ACK 未知，不重发；正在核对同设备运行镜像，尚不能判定成功`:i.type===`dialog`?`数据传输结束，正在等待应用验身；断连不代表升级成功`:`升级包已提交，正在等待应用重新启动`}),d.setOtaPhase(`reconnecting-app`);let r=await C(u,i.type===`onmicro`||i.type===`telink`?12:9,c,i.type===`goodix`?{before:E,after:()=>{if(T(),a.getDevice()?.id!==u.id)throw new DOMException(`GR 连接身份已变化`,`AbortError`)},releaseRecovery:()=>a.releaseForRecovery(S)}:void 0);if(i.type===`goodix`&&r&&(w=a.getConnectionGeneration(),D()),r&&(d.setOtaPhase(`attesting-app`),await a.waitForPasswordAuthorization(),i.type===`goodix`&&D()),i.type===`goodix`){if(!r)throw Error(`升级包已传完，但 GR 应用未重新上线，不能判定升级成功`);b(`goodix`,n.getState().boardId,i.payload.manifest.board),c({percent:100,message:`设备已重连，正在核对运行固件`}),g(a.getRuntimeImageInfo(),i.payload),c({percent:100,message:`运行固件与升级包一致，正在恢复设备时间`})}else if(i.type===`onmicro`){if(!r)throw Error(`升级包已传完，但设备未重新上线，不能判定升级成功`);b(`onmicro`,n.getState().boardId,i.payload.manifest.board),c({percent:100,message:`设备已重连，正在核对运行固件与 Recovery 状态`});let e=await a.queryRuntimeBuild();t(e,Number(i.payload.manifest.releaseVersion),Number(i.payload.application.size),O),c({percent:100,message:`单应用试运行已确认，正在恢复设备时间`})}else if(i.type===`telink`){if(!r)throw Error(`升级包已传完，但 TLSR8359 应用未重新上线，不能判定升级成功`);c({percent:100,message:`设备已重连，正在核对 TLSR8359 应用大小与 CRC32`});let e=await a.queryV2ApplicationIdentity();if(e.size!==i.payload.declaredSize||e.checksum!==i.payload.crc32)throw Error(`TLSR8359 应用核验不一致：设备 ${e.size}/${e.checksum.toString(16)}，固件 ${i.payload.declaredSize}/${i.payload.crc32.toString(16)}`);j.delete(l),c({percent:100,message:`单应用试运行已确认，应用大小与 CRC32 一致`})}else if(i.type===`dialog`){if(!r)throw Error(`升级包已传完，但 DA14585 应用未重新上线，不能判定升级成功`);h(a.getRuntimeImageInfo(),i.payload),c({percent:100,message:`应用长度与 CRC32 已核对，试运行已确认`})}else if(i.type===`nordic`){if(!r)throw Error(`升级包已传完，但 nRF 应用未重新上线，不能判定升级成功`);let e=await a.queryV2ApplicationIdentity();if(!i.payload.firmware?.length||e.size!==i.payload.firmware.length||e.checksum!==i.payload.firmwareCrc)throw Error(`nRF 应用核验不一致：设备 ${e.size}/${e.checksum.toString(16)}，固件 ${i.payload.firmware?.length??0}/${i.payload.firmwareCrc?.toString(16)??`unknown`}`);c({percent:100,message:`应用大小与 CRC32 已核对`})}if(r){c({percent:100,message:`固件验身完成，正在校准设备时间`});let e=await m(i.type===`goodix`?async()=>{D(),await a.waitForPasswordAuthorization(),D(),await a.syncTime()}:void 0);i.type===`goodix`&&D(),e?(n.getState().addLog(`OTA 已完成，但设备时间校准失败：${e}`,`warning`),c({percent:100,message:`OTA 已完成；设备时间未校准，可稍后手动同步`})):c({percent:100,message:`OTA 完成，应用已验身、重连并校时`})}else c({percent:100,message:`OTA 已提交，请手动连接设备确认版本并同步时间`})}finally{(i.type!==`goodix`||x===V&&n.getState().action===`ota`&&(!a.getDevice()||a.getDevice()?.id===u.id))&&n.getState().setAction(`idle`)}}export{b as assertApplicationPackageFamily,h as assertDialogOtaAttestation,g as assertGoodixOtaAttestation,x as assertTelinkPackageTarget,N as enterFixedRecovery,z as exitDialogRecovery,I as exitGoodixRecovery,B as exitNordicRecovery,R as exitOnMicroRecovery,L as exitTelinkRecovery,D as inspectNordicRecovery,E as inspectTelinkRecovery,y as otaFileHint,S as parseOtaPackage,m as syncClockAfterVerifiedOta,H as transferOta,k as verifyDialogRecovery,w as verifyGoodixRecovery,A as verifyNordicRecovery,O as verifyOnMicroRecovery,T as verifyTelinkRecovery};