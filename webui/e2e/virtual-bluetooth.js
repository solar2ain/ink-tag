(() => {
  localStorage.setItem('inktag-getting-started-v1', 'seen')
  const SERVICE_UUID = '62750001-d828-918d-fb46-b6c11c675aec'
  const COMMAND_UUID = '62750002-d828-918d-fb46-b6c11c675aec'
  const VERSION_UUID = '62750003-d828-918d-fb46-b6c11c675aec'
  const RECOVERY_UUID = 'a6ed0401-d344-460a-8075-b9e8ec90d71b'
  const recoveryFixture = new URLSearchParams(location.search).get('fixture')
  const telinkRecovery = Boolean(recoveryFixture?.startsWith('tl-recovery'))
  const omRecovery = Boolean(recoveryFixture?.startsWith('om-recovery'))
  const daRecovery = Boolean(recoveryFixture?.startsWith('da-recovery'))
  const nrfRecovery = Boolean(recoveryFixture?.startsWith('nrf-recovery'))
  const nrfAppSameMac = Boolean(recoveryFixture?.startsWith('nrf-app-same-mac'))
  const nrfAttestation = Boolean(recoveryFixture?.startsWith('nrf-app-same-mac-attest'))
  let inRecovery = telinkRecovery || omRecovery || daRecovery || nrfRecovery || Boolean(recoveryFixture?.startsWith('gr-recovery'))
  window.__recoveryCommands = []
  const encoder = new TextEncoder()
  const imageFixture = new URLSearchParams(location.search).has('image-fixture')
  const emptyImageFixture = new URLSearchParams(location.search).get('image-fixture') === 'empty'
  const screenSaveFixture = new URLSearchParams(location.search).get('screen-save')
  const screenFixture = new URLSearchParams(location.search).get('screen-fixture')
  const daOriginal = screenFixture === 'da-original'
  const fontFixture = new URLSearchParams(location.search).has('font-fixture')
  const passwordFixture = new URLSearchParams(location.search).get('password-fixture')
  const activationFixture = new URLSearchParams(location.search).has('activation-fixture')
  const compactNrf = new URLSearchParams(location.search).get('compact-nrf-temperature')
  const timeDisplayFixture = new URLSearchParams(location.search).has('time-display-busy')
  window.__timeDisplayFixture = { armed: false, synced: false, attempts: 0, statusReads: 0, completed: false }
  let hasStoredImage = !emptyImageFixture
  let nativeFinalized = false
  window.__postNativeImageCommands = []
  const fixedSlots = new URLSearchParams(location.search).has('fixed-slots')
  const fixedCount = new URLSearchParams(location.search).get('fixed-slots') === '255' ? 255 : 15
  const rememberedFixture = new URLSearchParams(location.search).has('remembered-fixture')
  let needsRediscovery = rememberedFixture && sessionStorage.getItem('virtual-ble-granted') === 'yes'
  window.__bluetoothFixture = { chooserCalls: 0, connectCalls: 0, watchCalls: 0, watchStopped: false }
  window.__inktagImageCommands = []
  window.__screenRequests = []

  const littleEndian = (length, writes) => {
    const bytes = new Uint8Array(length)
    const view = new DataView(bytes.buffer)
    writes(bytes, view)
    return bytes
  }

  const hello = new Uint8Array([1, 0x10, 244, 0, 240, 0])
  const capabilities = littleEndian(17, (bytes, view) => {
    bytes[0] = 1
    view.setUint32(1, (imageFixture ? 0x1000000b : 0x00000003) | (fontFixture ? 0x800000 : 0) | (passwordFixture ? 0x8002000 : 0) | (activationFixture ? 0x2000 : 0) | (nrfAppSameMac ? 0x20000 : 0), true)
    view.setUint16(5, 0x000f, true)
    bytes[13] = 18
    bytes[14] = 32
  })
  const board = encoder.encode(daOriginal ? 'aesl29_da' : nrfAppSameMac ? 'nrf52832_priceetag' : compactNrf ? 'aesl22_nrf' : new URLSearchParams(location.search).get('board-fixture') || 'gr551x_zkc42vm')
  const name = encoder.encode(nrfAppSameMac ? 'IKT_NRF_EE02' : compactNrf ? 'IKT_NRF_37DE' : 'InkTag_GR_E2E')
  const label = encoder.encode('E2E Fixture')
  const deviceInfo = littleEndian(23 + board.length + name.length + label.length, (bytes, view) => {
    bytes.set([1, board.length, name.length, label.length, 1], 0)
    bytes.set([0x02, 0x00, 0x00, 0x00, 0xee, 0x02], 5)
    view.setUint32(11, 0x00010200, true)
    view.setUint32(15, daRecovery ? 80676 : nrfRecovery ? 109596 : 184808, true)
    view.setUint32(19, 0x12345678, true)
    bytes.set(board, 23)
    bytes.set(name, 23 + board.length)
    bytes.set(label, 23 + board.length + name.length)
  })
  const screen = littleEndian(17, (bytes, view) => {
    bytes.set([1, screenFixture === 'zkc42v' ? 0x15 : 0x24, 1, 2, 0], 0)
    view.setUint16(5, 400, true)
    view.setUint16(7, 300, true)
    view.setUint16(13, 400, true)
    view.setUint16(15, 300, true)
    if (daOriginal) {
      bytes.set([1, 0x20, 0, 2, 1], 0)
      view.setUint16(5, 128, true); view.setUint16(7, 296, true)
      view.setUint16(13, 128, true); view.setUint16(15, 296, true)
    }
    if (screenFixture?.startsWith('huawei')) {
      bytes.set([1, 0x31, 1, 3, screenFixture === 'huawei-rotated' ? 0x29 : 0x22], 0)
      view.setUint16(5, 768, true); view.setUint16(7, 552, true)
      view.setUint16(13, screenFixture === 'huawei-custom' ? 760 : 768, true)
      view.setUint16(15, 552, true)
    }
  })
  const deviceTime = littleEndian(7, (bytes, view) => {
    view.setUint32(0, 1788547200, true)
    view.setInt16(4, 480, true)
    bytes[6] = 1
  })
  const panelNrf = compactNrf === 'panel' || compactNrf === 'expired'
  const status = littleEndian(panelNrf ? 25 : 16, (bytes, view) => {
    bytes[0] = 1
    bytes[2] = daOriginal || compactNrf === 'invalid' ? 0x04 : 0x0c
    bytes[7] = compactNrf === 'expired' ? 25 : compactNrf ? 22 : 23
    view.setUint16(8, 3010, true)
    if (panelNrf) {
      bytes.set([0x49, 0x54, 0x54, 1, compactNrf === 'panel' ? 3 : 1], 16)
      view.setInt16(21, 25 * 16, true)
      view.setInt16(23, compactNrf === 'panel' ? 22.5 * 16 : 0, true)
    }
  })
  const replies = new Map([
    [0x01, hello],
    [0x02, capabilities],
    [0x03, deviceInfo],
    [0x04, status],
    [0x05, deviceTime],
    [0x06, new Uint8Array(0)],
    [0x07, screen],
    [0x0e, new Uint8Array(0)],
    ...(daOriginal || timeDisplayFixture ? [[0x09, new Uint8Array(0)], [0x71, new Uint8Array(0)]] : []),
    [0x15, littleEndian(20, (bytes, view) => {
      bytes.set([1, 1, 0xff, 0])
      view.setUint32(12, 16384, true)
    })],
  ])
  if (passwordFixture) {
    // Synthetic Pro/password replies for presentation tests; never a real BLE device.
    replies.set(0x40, littleEndian(24, (bytes) => {
      bytes.set([2, 2, 1, 0, 1])
      bytes.set([2, 0, 0, 0, 0, 0, 0xee, 2], 16)
    }))
    const passwordStatus = new Uint8Array(12)
    passwordStatus[0] = 1
    if (passwordFixture === 'locked') { passwordStatus[1] = 1; passwordStatus[8] = 1 }
    replies.set(0x46, passwordStatus)
    replies.set(0x47, new Uint8Array([1, 0, ...new Array(16).fill(1), 1, 0, 0, 0]))
    replies.set(0x48, new Uint8Array([1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0]))
  }
  if (activationFixture) {
    // Synthetic Free activation identity/challenge; never an installed device or signed token.
    replies.set(0x40, littleEndian(24, (bytes) => {
      bytes.set([2, 1, 1, 0, 1])
      bytes.set([2, 0, 0, 0, 0, 0, 0xee, 2], 16)
    }))
    replies.set(0x43, littleEndian(32, (bytes) => {
      bytes.set([1, 1, 1])
      bytes.set([2, 0, 0, 0, 0, 0, 0xee, 2], 16)
      bytes.fill(1, 24)
    }))
  }
  const resourceName = encoder.encode('课程表')
  const resource = littleEndian(19 + resourceName.length, (bytes, view) => {
    bytes.set([0, 7, 1, 1, 1, 1, resourceName.length])
    view.setUint16(7, 400, true)
    view.setUint16(9, 300, true)
    view.setUint32(11, 15000, true)
    view.setUint32(15, 100, true)
    bytes.set(resourceName, 19)
  })
  const resourceDone = fixedSlots ? new Uint8Array(2 + Math.ceil(fixedCount / 8)) : new Uint8Array([1, 0])
  if (fixedSlots) { resourceDone.set([1, 0xff]); resourceDone[2] = 1 << 7 }
  if (imageFixture) {
    replies.set(0x18, littleEndian(fixedSlots ? 34 : 32, (bytes, view) => {
      bytes[0] = 1
      view.setUint16(1, 400, true)
      view.setUint16(3, 300, true)
      view.setUint32(5, 15000, true)
      const count = fixedSlots ? fixedCount : 15
      for (const [offset, value] of [[9, 4], [11, count * 4], [13, (count - 1) * 4], [15, (count - 1) * 4], [17, 1], [19, 1], [30, count - 1]]) {
        view.setUint16(offset, value, true)
      }
      if (fixedSlots) view.setUint16(32, count, true)
      if (emptyImageFixture) for (const offset of [17, 19, 21, 23]) view.setUint16(offset, 0, true)
    }))
  }

  class VirtualCharacteristic extends EventTarget {
    constructor(uuid) {
      super()
      this.uuid = uuid
      this.value = null
    }

    async readValue() {
      return new DataView(new Uint8Array([0x10, 0, 1, 2]).buffer)
    }

    async startNotifications() { return this }
    async stopNotifications() { return this }

    async writeValueWithResponse(value) {
      const request = new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
      if (request.length < 3 || (request[2] & 0xfc) !== 0x10) throw new Error('virtual device requires release-v1 request framing')
      window.__inktagImageCommands.push(request[0])
      if (nativeFinalized) window.__postNativeImageCommands.push(request[0])
      let responseStatus = 0
      const timeWork = window.__timeDisplayFixture
      if (timeDisplayFixture && timeWork.armed) {
        if (request[0] === 0x06) timeWork.synced = true
        if (request[0] === 0x09 && timeWork.synced) {
          timeWork.attempts++
          if (timeWork.attempts === 1) { responseStatus = 5; status[2] |= 1; timeWork.oldTransaction = request[1] }
        }
        if (request[0] === 0x04 && timeWork.attempts === 1 && ++timeWork.statusReads === 2) status[2] &= ~1
      }
      if (screenSaveFixture && request[0] === 0x1c) {
        if (screenSaveFixture === 'clear-fail') responseStatus = 0x0a
        else {
          hasStoredImage = false
          resourceDone[0] = 1
          if (fixedSlots) resourceDone.fill(0, 2)
          const info = replies.get(0x18)
          const view = new DataView(info.buffer)
          view.setUint16(17, 0, true)
          view.setUint16(19, 0, true)
        }
      }
      if (screenSaveFixture && request[0] === 0x08) {
        // Shipping fixed-slot behavior rejects changed geometry while old images remain.
        const requested = request.subarray(3)
        window.__screenRequests.push(Array.from(requested))
        const unchanged = requested.length === screen.length && requested.every((byte, index) => byte === screen[index])
        if ((hasStoredImage && !unchanged) || screenSaveFixture === 'screen-fail') responseStatus = 0x0a
        else screen.set(requested)
      }
      const payload = screenSaveFixture && request[0] === 0x08 ? screen
        : screenSaveFixture && request[0] === 0x1c ? new Uint8Array(0)
        : imageFixture && request[0] === 0x19
        ? request[3] === 0xff && hasStoredImage ? resource : resourceDone
        : imageFixture && request[0] === 0x1a
          ? littleEndian(9 + request[8], (bytes, view) => {
            bytes[0] = request[3]
            const offset = new DataView(request.buffer, request.byteOffset, request.byteLength).getUint32(4, true)
            view.setUint32(1, offset, true)
            view.setUint32(5, 15000, true)
            // Synthetic mono readback fixture, not a real device operation.
            for (let index = 9; index < bytes.length; index++) {
              const position = offset + index - 9
              const row = Math.floor(position / 50)
              bytes[index] = row < 4 || row >= 296 || position % 50 < 2 || position % 50 >= 48
                ? 0 : row % 48 < 8 && position % 50 > 10 && position % 50 < 40 ? 0xaa : 0xff
            }
          })
        : replies.get(request[0])
      if (!payload) throw new Error(`virtual device has no reply for opcode 0x${request[0].toString(16)}`)
      const response = new Uint8Array(3 + payload.length)
      response.set([request[0], request[1], responseStatus], 0)
      response.set(payload, 3)
      queueMicrotask(() => {
        this.value = new DataView(response.buffer)
        this.dispatchEvent(new Event('characteristicvaluechanged'))
        if (timeDisplayFixture && timeWork.armed && request[0] === 0x04 && timeWork.statusReads === 2) {
          const old = new Uint8Array([0xe2, 0, 0, 0x09, timeWork.oldTransaction])
          this.value = new DataView(old.buffer)
          this.dispatchEvent(new Event('characteristicvaluechanged'))
        }
        if (timeDisplayFixture && request[0] === 0x09 && responseStatus === 0) {
          setTimeout(() => {
            const complete = new Uint8Array([0xe2, 0, 0, request[0], request[1]])
            this.value = new DataView(complete.buffer)
            this.dispatchEvent(new Event('characteristicvaluechanged'))
            timeWork.completed = true
          }, 750)
        }
        if (daOriginal && (request[0] === 0x09 || request[0] === 0x71)) {
          status[2] |= 0x08; status[7] = 23
          const complete = new Uint8Array([0xe2, 0, 0, request[0], request[1]])
          this.value = new DataView(complete.buffer)
          this.dispatchEvent(new Event('characteristicvaluechanged'))
        }
        if (nrfAppSameMac && request[0] === 0x0e && request[3] === 4) {
          // Same browser grant/MAC, but BluetoothDevice.name remains the cached App name.
          inRecovery = true
          device.gatt.disconnect()
        }
      })
    }

    async writeValueWithoutResponse(value) { return this.writeValueWithResponse(value) }
  }

  const command = new VirtualCharacteristic(COMMAND_UUID)
  const version = new VirtualCharacteristic(VERSION_UUID)
  const service = {
    uuid: SERVICE_UUID,
    async getCharacteristic(uuid) {
      if (String(uuid).toLowerCase() === COMMAND_UUID) return command
      if (String(uuid).toLowerCase() === VERSION_UUID) return version
      throw new Error(`unknown virtual characteristic ${uuid}`)
    },
  }
  const recoveryTx = new VirtualCharacteristic('a6ed0402-d344-460a-8075-b9e8ec90d71b')
  const recoveryRx = {
    async writeValueWithoutResponse(value) {
      const request = new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
      const requestView = new DataView(request.buffer, request.byteOffset, request.byteLength)
      const opcode = requestView.getUint16(2, true)
      const length = requestView.getUint16(4, true)
      const payload = Array.from(request.slice(6, 6 + length))
      window.__recoveryCommands.push({ opcode, payload })
      let reply
      if (opcode === 1) {
        reply = new Uint8Array(18)
        reply[0] = 1
        reply[17] = 2
      } else if (opcode === 0x42) {
        reply = new Uint8Array(54)
        reply[0] = 1
        reply[5] = recoveryFixture === 'gr-recovery-wrong-role' ? 1 : 0
      } else if (opcode === 0x41 && length === 0) {
        if (recoveryFixture === 'gr-recovery-reject') {
          reply = new Uint8Array([2, 0x49, 0x4b, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        } else {
          reply = new Uint8Array([1])
          inRecovery = false
        }
      } else {
        throw new Error(`unexpected Recovery command 0x${opcode.toString(16)}`)
      }
      const response = new Uint8Array(8 + reply.length)
      response.set([0x44, 0x47, opcode & 0xff, opcode >> 8, reply.length & 0xff, reply.length >> 8])
      response.set(reply, 6)
      new DataView(response.buffer).setUint16(response.length - 2,
        response.slice(2, -2).reduce((sum, byte) => sum + byte, 0) & 0xffff, true)
      queueMicrotask(() => {
        recoveryTx.value = new DataView(response.buffer)
        recoveryTx.dispatchEvent(new Event('characteristicvaluechanged'))
      })
    },
  }
  const recoveryService = {
    async getCharacteristic(uuid) {
      const suffix = String(uuid).slice(0, 8)
      if (suffix === 'a6ed0402') return recoveryTx
      if (suffix === 'a6ed0403') return recoveryRx
      if (suffix === 'a6ed0404') return { async writeValueWithoutResponse() {} }
      throw new Error(`unknown virtual Recovery characteristic ${uuid}`)
    },
  }
  let telinkStatus = recoveryFixture === 'tl-recovery-low-power' ? 9 : 0
  const telinkCharacteristic = {
    async readValue() {
      const value = new Uint8Array([telinkStatus, 1, 6, 1, 4, 40, 3, 0, 0x49, 0x54, 1, 14, 0, 0])
      if (recoveryFixture === 'tl-recovery-wrong-role') value[8] = 0
      return new DataView(value.buffer)
    },
    async writeValueWithResponse(value) {
      const bytes = Array.from(new Uint8Array(value.buffer, value.byteOffset, value.byteLength))
      window.__recoveryCommands.push({ bytes })
      if (bytes.join(',') !== '3,255') throw new Error('unexpected TLSR maintenance write')
      if (recoveryFixture === 'tl-recovery-unknown') throw new DOMException('lost ACK', 'NetworkError')
      telinkStatus = recoveryFixture === 'tl-recovery-low-power' ? 9
        : recoveryFixture === 'tl-recovery-partial' ? 10 : 8
      if (telinkStatus === 8) inRecovery = false
    },
  }
  const telinkService = {
    async getCharacteristic(uuid) {
      if (String(uuid).toLowerCase() !== '00010203-0405-0607-0809-0a0b0c0d2b12') throw new Error('wrong TLSR characteristic')
      return telinkCharacteristic
    },
  }
  const omControl = new EventTarget()
  omControl.startNotifications = async () => omControl
  omControl.stopNotifications = async () => omControl
  omControl.writeValueWithResponse = async (value) => {
    const bytes = Array.from(value)
    window.__recoveryCommands.push({ bytes })
    if (bytes.join(',') !== '7') throw new Error('unexpected OM maintenance write')
    if (recoveryFixture === 'om-recovery-unknown') throw new DOMException('lost ACK', 'NetworkError')
    const rejected = recoveryFixture === 'om-recovery-reject'
    const response = rejected ? new Uint8Array([0x60, 7, 10, 0x49, 0x4b, 1, 2, 1, 1, 0, 0, 0, 0, 0x28, 0x0a, 0x28, 0x0a]) : new Uint8Array([0x60, 7, 1])
    omControl.value = new DataView(response.buffer)
    omControl.dispatchEvent(new Event('characteristicvaluechanged'))
    if (!rejected) inRecovery = false
  }
  const omService = {
    async getCharacteristic(uuid) {
      if (String(uuid).includes('5f03')) return { async readValue() {
        const raw = littleEndian(20, (bytes, view) => {
          view.setUint32(0, 0x50524b49, true)
          bytes[4] = 1
          view.setUint16(6, 20, true)
          view.setUint32(8, 0x31565a4f, true)
          view.setUint32(12, 0x4e000 - 96, true)
          view.setUint32(16, 0x4e000, true)
        })
        if (recoveryFixture === 'om-recovery-wrong-role') raw[0] = 0
        return new DataView(raw.buffer)
      } }
      return omControl
    },
  }
  const nrfControl = new EventTarget()
  nrfControl.startNotifications = async () => nrfControl
  nrfControl.stopNotifications = async () => nrfControl
  let nativeObject = 1
  const nativeObjects = { 1: [], 2: [] }
  const nativeCrc = bytes => {
    let crc = 0xffffffff
    for (const byte of bytes) {
      crc ^= byte
      for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)
    }
    return (crc ^ 0xffffffff) >>> 0
  }
  const nrfPacket = { async writeValueWithoutResponse(value) { nativeObjects[nativeObject].push(...value) } }
  nrfControl.writeValueWithResponse = async value => {
    const bytes = Array.from(value)
    window.__recoveryCommands.push({bytes})
    if (nrfAttestation && [1, 2, 3, 4, 6].includes(bytes[0])) {
      // Synthetic DFU peer for the actual shipping JS transport/parser, NOT
      // firmware signature validation or a physical successful OTA claim.
      let payload = new Uint8Array(0)
      if (bytes[0] === 6) payload = littleEndian(12, (_bytes, view) => view.setUint32(0, 4096, true))
      if (bytes[0] === 1) { nativeObject = bytes[1]; nativeObjects[nativeObject] = [] }
      if (bytes[0] === 3) payload = littleEndian(8, (_bytes, view) => {
        view.setUint32(0, nativeObjects[nativeObject].length, true)
        view.setUint32(4, nativeCrc(nativeObjects[nativeObject]), true)
      })
      const lostFinalAck = bytes[0] === 4 && nativeObject === 2 && recoveryFixture.includes('lost-final')
      if (!lostFinalAck) {
        nrfControl.value = new DataView(Uint8Array.from([96, bytes[0], 1, ...payload]).buffer)
        nrfControl.dispatchEvent(new Event('characteristicvaluechanged'))
      }
      if (bytes[0] === 4 && nativeObject === 2) {
        const view = new DataView(deviceInfo.buffer)
        view.setUint32(15, nativeObjects[2].length + (recoveryFixture.endsWith('size-mismatch') ? 1 : 0), true)
        view.setUint32(19, nativeCrc(nativeObjects[2]) ^ (recoveryFixture.endsWith('crc-mismatch') ? 1 : 0), true)
        inRecovery = false; nativeFinalized = true
        queueMicrotask(() => device.gatt.disconnect())
      }
      return
    }
    if (nrfAppSameMac && bytes[0] === 2) {
      // Deliberate native refusal before CREATE/DATA, not a signed OTA success fixture.
      nrfControl.value = new DataView(new Uint8Array([96, 2, 8]).buffer)
      nrfControl.dispatchEvent(new Event('characteristicvaluechanged'))
      return
    }
    if (bytes.length !== 1 || ![0,12].includes(bytes[0])) throw new Error('unexpected Nordic maintenance command')
    if (bytes[0] === 12 && recoveryFixture === 'nrf-recovery-safe-unknown') throw new DOMException('lost ACK', 'NetworkError')
    const rejected = recoveryFixture === 'nrf-recovery-safe-reject'
    const reply = bytes[0] === 0 ? [96,0,1,recoveryFixture.startsWith('nrf-recovery-safe') || recoveryFixture === 'nrf-app-same-mac-maintenance' ? 129 : 1]
      : [96,12,rejected ? 8 : 1]
    nrfControl.value = new DataView(Uint8Array.from(reply).buffer)
    nrfControl.dispatchEvent(new Event('characteristicvaluechanged'))
    if (bytes[0] === 12 && !rejected) inRecovery = false
  }
  const nrfService = { async getCharacteristic(uuid) {
    if (recoveryFixture === 'nrf-app-same-mac-missing-packet' && String(uuid) === '8ec90002-f315-4f60-9fb8-838830daea50') throw new DOMException('missing Secure DFU packet', 'NotFoundError')
    if (recoveryFixture === 'nrf-recovery-wrong-role' || !['8ec90001-f315-4f60-9fb8-838830daea50', '8ec90002-f315-4f60-9fb8-838830daea50'].includes(String(uuid))) throw new Error('missing Secure DFU channel')
    return String(uuid) === '8ec90001-f315-4f60-9fb8-838830daea50' ? nrfControl : nrfAttestation ? nrfPacket : {}
  } }
  let daExitAccepted = false
  const daStatus = new EventTarget()
  daStatus.startNotifications = async () => daStatus
  daStatus.stopNotifications = async () => daStatus
  daStatus.readValue = async () => {
    // An old attribute and its read-induced notification are not a fresh ACK.
    daStatus.value = new DataView(new Uint8Array([2]).buffer)
    daStatus.dispatchEvent(new Event('characteristicvaluechanged'))
    return daStatus.value
  }
  const daService = { async getCharacteristic(uuid) {
    if (uuid === '64b4e8b5-0de5-401b-a21d-acc8db3b913a') return {async readValue() {
      return new DataView(new Uint8Array(recoveryFixture.startsWith('da-recovery-safe') ? [13,73,75,1,1] : [13]).buffer)
    }}
    if (uuid === '5f78df94-798c-46f5-990a-b3eb6a065c88') return daStatus
    if (uuid === '8082caa8-41a6-4021-91c6-56f9b954cc34') return {async writeValueWithResponse(value) {
      const bytes = Array.from(value)
      window.__recoveryCommands.push({bytes})
      if (bytes.join(',') !== '0,0,0,252') throw new Error('unexpected DA maintenance write')
      if (recoveryFixture === 'da-recovery-safe-unknown') throw new DOMException('lost ACK', 'NetworkError')
      const rejected = recoveryFixture === 'da-recovery-safe-reject'
      daStatus.value = new DataView(new Uint8Array(rejected ? [25] : [2]).buffer)
      daStatus.dispatchEvent(new Event('characteristicvaluechanged'))
      daExitAccepted = !rejected
    }}
    throw new DOMException('unknown SUOTA characteristic', 'NotFoundError')
  }}
  class VirtualDevice extends EventTarget {
    constructor() {
      super()
      this.id = '02:00:00:00:EE:02'
      this.name = inRecovery ? `IKT_${telinkRecovery ? 'TL' : omRecovery ? 'OM' : daRecovery ? 'DA' : nrfRecovery ? 'NRF' : 'GR'}_EE02_OTA` : nrfAppSameMac ? 'IKT_NRF_EE02' : 'InkTag_GR_E2E'
      this.gatt = {
        device: this,
        connected: false,
        connect: async () => {
          window.__bluetoothFixture.connectCalls++
          if (needsRediscovery) throw new DOMException('Bluetooth Device is no longer in range.', 'NetworkError')
          this.gatt.connected = true
          return this.gatt
        },
        disconnect: () => {
          if (!this.gatt.connected) return
          this.gatt.connected = false
          if (daExitAccepted) inRecovery = false
          this.dispatchEvent(new Event('gattserverdisconnected'))
        },
        getPrimaryService: async (uuid) => {
          if (inRecovery && telinkRecovery && String(uuid).toLowerCase() === '00010203-0405-0607-0809-0a0b0c0d1912') return telinkService
          if (inRecovery && omRecovery && String(uuid).toLowerCase() === '000001bf-5f00-0000-0000-4f4d2d4f5441') return omService
          if (inRecovery && daRecovery && String(uuid).toLowerCase() === '0000fef5-0000-1000-8000-00805f9b34fb') return daService
          if (inRecovery && (nrfRecovery || nrfAppSameMac) && String(uuid).toLowerCase() === '0000fe59-0000-1000-8000-00805f9b34fb') return nrfService
          if (inRecovery && String(uuid).toLowerCase() === RECOVERY_UUID) return recoveryService
          if (inRecovery) throw new DOMException('not found in Recovery', 'NotFoundError')
          if (String(uuid).toLowerCase() !== SERVICE_UUID) throw new Error(`unknown virtual service ${uuid}`)
          return service
        },
      }
    }

    async watchAdvertisements({ signal }) {
      window.__bluetoothFixture.watchCalls++
      const timer = setTimeout(() => {
        needsRediscovery = false
        this.dispatchEvent(new Event('advertisementreceived'))
      }, 50)
      signal.addEventListener('abort', () => {
        clearTimeout(timer)
        window.__bluetoothFixture.watchStopped = true
      }, { once: true })
    }
  }
  const device = new VirtualDevice()
  Object.defineProperty(navigator, 'bluetooth', {
    configurable: true,
    value: {
      requestDevice: async () => {
        window.__bluetoothFixture.chooserCalls++
        if (rememberedFixture) sessionStorage.setItem('virtual-ble-granted', 'yes')
        return device
      },
      getDevices: async () => rememberedFixture && sessionStorage.getItem('virtual-ble-granted') === 'yes' ? [device] : [],
    },
  })
})()
