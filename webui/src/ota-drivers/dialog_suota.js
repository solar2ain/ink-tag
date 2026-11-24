/* Dialog/Renesas SDK6 SUOTA transport for InkTag DA14585.
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
            throw new Error(`SUOTA image size is outside 64..${MAX_IMAGE_SIZE} bytes`);
        }
        if (image[0] !== 0x70 || image[1] !== 0x51 || image[2] !== 0xaa) {
            throw new Error('not an SDK6 pQ SUOTA image');
        }
        const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
        const codeSize = view.getUint32(4, true);
        const expectedCrc = view.getUint32(8, true);
        if (codeSize + 64 !== image.length) {
            throw new Error(`pQ code size mismatch: ${codeSize} + 64 != ${image.length}`);
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
        const parsed = parseImage(await file.arrayBuffer(), file.name);
        if (!global.InkTagSignedOta) throw new Error('固件验签组件未加载');
        parsed.verified = await global.InkTagSignedOta.inspectDialog(parsed.image);
        return parsed;
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
                if (remaining <= 0) throw new Error(`${action} timed out`);
                const status = await this.nextStatus(remaining);
                if (status === expected) return status;
                if (status !== 0x01) {
                    const detail = STATUS_NAMES[status] || 'unknown error';
                    throw new Error(`${action} failed with SUOTA status 0x${status.toString(16).padStart(2, '0')}: ${detail}`);
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
                    throw new Error(`${action} failed with SUOTA status 0x${status.toString(16).padStart(2, '0')}: ${detail}`);
                }
            }
        }

    }

    async function transferServer(server, pkg, onProgress = () => {}, options = {}) {
        if (!global.InkTagSignedOta) throw new Error('固件验签组件未加载');
        const verified = await global.InkTagSignedOta.inspectDialog(pkg.image);
        const blockSize = options.blockSize || DEFAULT_BLOCK_SIZE;
        const chunkSize = options.chunkSize || DEFAULT_CHUNK_SIZE;
        const packetDelayMs = options.packetDelayMs === undefined ? 0 : options.packetDelayMs;
        if (blockSize < 80 || blockSize > 512) throw new Error('SUOTA block size must be between 80 and 512');
        if (chunkSize < 1 || chunkSize > 244) throw new Error('SUOTA chunk size must be between 1 and 244');

        const onPhase = options.onPhase || (() => {});
        onPhase('正在发现 DA14585 SUOTA 服务…');
        const service = await server.getPrimaryService(SERVICE_UUID);
        if (!verified.publisherVerified) {
            const version = await service.getCharacteristic(VERSION_UUID);
            global.InkTagSignedOta.requireRecoverySignature(verified, 'dialog', bytesFromValue(await version.readValue()));
        }
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
                await transport.waitStatus(SUOTA_COMPLETE_OK, `block at offset ${offset}`);
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
        throw new Error(`连接 DA14585 固定 Recovery 超时：${lastError ? lastError.message : '设备未出现'}`);
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
        const old = data.length === 5 && data.every((v, i) => v === [13, 73, 75, 1, 1][i]);
        const policy = data.length === 9 && data.slice(0,8).every((v,i) => v === [13,73,75,1,1,83,80,1][i]) && [0,1,2].includes(data[8]);
        return { safeResume: old || policy, signaturePolicy: policy ? ['optional','required','crc-only'][data[8]] : 'required' };
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
                throw new Error(`target rejected SAFE_RESUME_APP: SUOTA status ${data[0]}`);
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
