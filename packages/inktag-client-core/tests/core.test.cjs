const assert = require('node:assert/strict')
const test = require('node:test')
const core = require('../dist')

test('miniapp never advertises Find even for a Pro device', () => {
  assert.equal(core.clientNavigationFor('miniapp', true).some((item) => item.key === 'find'), false)
  assert.equal(core.clientNavigationFor('server', true).some((item) => item.key === 'find'), true)
})

test('atomic replacement appends the slot id without changing ordinary append packets', () => {
  const screen = { controller: 0x13, recipe: 0, color: 2, transform: 0,
    width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 }
  const options = { screen, store: true, name: 'image' }
  const encoded = { format: 1, transfers: [{ step: 'bw', data: new Uint8Array([0xaa]) }] }
  const append = core.encodeImageTransferBeginV2(encoded, options)
  const replace = core.encodeImageTransferBeginV2(encoded, { ...options, replaceSlotId: 3 })
  assert.deepEqual(replace.slice(0, -1), append)
  assert.equal(replace.at(-1), 3)
  assert.throws(() => core.encodeImageTransferBeginV2(encoded, { ...options, replaceSlotId: 255 }))
  assert.throws(() => core.encodeImageTransferBeginV2(encoded, { ...options, store: false, replaceSlotId: 3 }))
})

test('generated HELLO command stays aligned with protocol registry', () => {
  assert.equal(core.CommandV2.HELLO, 0x01)
  assert.equal(core.PROTOCOL_V2_VERSION, 0x10)
})

test('encodes the canonical three-byte request header', () => {
  assert.deepEqual([...core.encodeV2Request(core.CommandV2.GET_STATUS, 0x2a)], [0x04, 0x2a, 0x10])
  for (const flags of [0, 1, 2, 3]) {
    assert.equal(core.encodeV2Request(core.CommandV2.GET_STATUS, 0x2a, flags)[2], 0x10 | flags)
  }
  for (const flags of [-1, 4, 0x10, 0x20, 0xff, 1.5, NaN]) {
    assert.throws(() => core.encodeV2Request(core.CommandV2.GET_STATUS, 0x2a, flags), /Unsupported request flags/)
  }
})

test('decodes direct replies and asynchronous events', () => {
  assert.deepEqual(core.decodeV2Frame(Uint8Array.from([0x04, 0x2a, 0x00, 0x01]), core.EventV2.READY), {
    kind: 'response', opcode: 0x04, transactionId: 0x2a, status: 0, payload: Uint8Array.of(1),
  })
  assert.deepEqual(core.decodeV2Frame(Uint8Array.from([0xe2, 0x00, 0x00, 0x09, 0x2a]), core.EventV2.READY), {
    kind: 'event', opcode: 0xe2, transactionId: 0, status: 0,
    originOpcode: 0x09, originTransactionId: 0x2a, payload: new Uint8Array(0),
  })
})

test('transaction IDs wrap, remain nonzero, and skip pending IDs', () => {
  const registry = new core.V2TransactionRegistry()
  const first = registry.allocate()
  registry.set(first, 'pending')
  assert.equal(first, 1)
  assert.equal(registry.allocate(), 2)
  registry.clear()
  assert.equal(registry.allocate(), 1)
})

test('decodes the read-only time and screen payloads', () => {
  const time = Uint8Array.from([0x00, 0xf1, 0x53, 0x65, 0xe0, 0x01, 0x01])
  assert.deepEqual(core.decodeTimeV2(time), { utcTimestamp: 1700000000, timezoneMinutes: 480, weekStart: 1 })
  const screen = Uint8Array.from([1, 0x13, 0, 2, 0, 0x90, 1, 0x2c, 1, 0, 0, 0, 0, 0x90, 1, 0x2c, 1])
  assert.deepEqual(core.decodeScreenProfileV2(screen), {
    schemaVersion: 1, controller: 0x13, recipe: 0, color: 2, transform: 0, nativeUiScale: 0,
    width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300,
  })
  screen[0] = 2
  assert.equal(core.decodeScreenProfileV2(screen), null)
})

test('puts InkTag advertisements before stronger unrelated devices', () => {
  const ranked = core.rankDiscoveredDevices([
    { deviceId: 'speaker', name: 'Speaker', RSSI: -20 },
    { deviceId: 'tag', localName: 'InkTag_GR_1234', RSSI: -70 },
  ])
  assert.deepEqual(ranked.map((item) => item.deviceId), ['tag', 'speaker'])
})

test('shares one deterministic screen and image codec across clients', () => {
  const previews = core.commonImagePreviewResolutions()
  assert.deepEqual(previews.find((item) => item.id === '212x104'), {
    id: '212x104', width: 212, height: 104, sizes: ['2.13'],
  })
  assert.deepEqual(previews.find((item) => item.id === '296x152'), {
    id: '296x152', width: 296, height: 152, sizes: ['2.66', '2.8'],
  })
  assert.ok(previews.every((item) => item.width >= item.height))
  const profile = {
    schemaVersion: 1, controller: 0xfe, recipe: 0, color: 1, transform: 0, nativeUiScale: 0,
    width: 8, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 1,
  }
  const data = new Uint8ClampedArray(8 * 4).fill(0xff)
  const encoded = core.encodeImage({ width: 8, height: 1, data }, profile)
  assert.deepEqual([...encoded.transfers[0].data], [0xff])
  assert.equal(core.imageCrc32(encoded.transfers), 0xff000000)
  assert.deepEqual([...core.encodeScreenProfileV2(profile)], [1, 0xfe, 0, 1, 0, 8, 0, 1, 0, 0, 0, 0, 0, 8, 0, 1, 0])
})

test('shares Osprey E6 wire colors and exact readback with both client profile shapes', () => {
  const colors = [[0, 0, 0], [255, 255, 255], [235, 194, 25], [220, 48, 38], [0, 0, 255], [41, 204, 20]]
  for (const controller of [0x40, 0x41]) {
    const screen = { schemaVersion: 1, controller, recipe: 1, color: 10, transform: 0, nativeUiScale: 0,
      width: 240, height: 240, visibleX: 0, visibleY: 0, visibleWidth: 240, visibleHeight: 240 }
    const data = Uint8ClampedArray.from(Array.from({ length: 240 * 240 }, (_, i) => [...colors[i % 6], 255]).flat())
    const { controller: _, ...config } = screen
    for (const profile of [screen, { ...config, driver: controller }]) {
      const encoded = core.encodeImage({ width: 240, height: 240, data }, profile)
      assert.equal(encoded.format, core.TransferFormat.INDEXED_4BPP)
      assert.equal(encoded.transfers.length, 1)
      const wire = encoded.transfers[0].data
      assert.deepEqual([...wire.slice(0, 3)], [0x01, 0x23, 0x56])
      assert.deepEqual(core.decodeStoredImage(wire, profile, encoded.format, 1).data, data)
      const invalid = wire.slice()
      invalid[0] = 0x41
      assert.throws(() => core.decodeStoredImage(invalid, profile, encoded.format, 1), /E6/)
    }
  }
})

test('encodes local time and decodes resource metadata without a platform API', () => {
  const date = new Date('2026-09-12T03:04:05.000Z')
  const encodedTime = core.encodeLocalTimeV2(date, 1)
  const timeView = new DataView(encodedTime.buffer)
  assert.equal(timeView.getUint32(0, true), Math.floor(date.getTime() / 1000))
  assert.equal(timeView.getInt16(4, true), -date.getTimezoneOffset())
  assert.equal(encodedTime[6], 1)

  const name = new TextEncoder().encode('ink_你好')
  const payload = new Uint8Array(19 + name.length)
  const view = new DataView(payload.buffer)
  payload.set([0, 7, 2, 3, 2, 1, name.length])
  view.setUint16(7, 296, true)
  view.setUint16(9, 128, true)
  view.setUint32(11, 9472, true)
  view.setUint32(15, 1_789_160_000, true)
  payload.set(name, 19)
  assert.deepEqual(core.decodeResourceListItemV2(payload), {
    done: false,
    item: {
      id: 7, contentType: 2, format: 3, segments: 2, compatible: true,
      name: 'ink_你好', width: 296, height: 128, bytes: 9472, createdAt: 1_789_160_000,
    },
  })
})

test('runs one shared V2 image-transfer transaction through a platform port', async () => {
  const requests = []
  const waits = []
  const port = {
    allocateTransaction: () => 9,
    request: async (opcode, payload = new Uint8Array(0), options = {}) => {
      requests.push({ opcode, payload: Uint8Array.from(payload), options })
      if (opcode === core.CommandV2.TRANSFER_BEGIN) return Uint8Array.from([0, 0, 20, 0])
      return new Uint8Array(0)
    },
    waitForCompletion: async (transactionId, opcode) => {
      waits.push({ transactionId, opcode })
      return Uint8Array.of(1)
    },
    cancelCompletion: () => assert.fail('successful transfer must not cancel completion'),
  }
  const profile = {
    schemaVersion: 1, controller: 0xfe, recipe: 0, color: 1, transform: 0, nativeUiScale: 0,
    width: 8, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 1,
  }
  const progress = []
  const result = await core.transferEncodedImageV2(port, {
    format: 1,
    transfers: [{ step: 'bw', data: Uint8Array.from({ length: 25 }, (_, index) => index) }],
  }, {
    screen: profile,
    writePayload: 64,
    onProgress: (value) => progress.push(value),
  })

  assert.equal(result, null)
  assert.deepEqual(requests.map(({ opcode }) => opcode), [
    core.CommandV2.TRANSFER_BEGIN,
    core.CommandV2.TRANSFER_WRITE,
    core.CommandV2.TRANSFER_WRITE,
    core.CommandV2.TRANSFER_COMMIT,
  ])
  assert.ok(requests.every(({ options }) => options.transactionId === 9))
  assert.deepEqual([...requests[1].payload.slice(0, 4)], [0, 0, 0, 0])
  assert.deepEqual([...requests[2].payload.slice(0, 4)], [20, 0, 0, 0])
  assert.deepEqual(progress, [0.8, 1])
  assert.deepEqual(waits, [{ transactionId: 9, opcode: core.CommandV2.TRANSFER_COMMIT }])
})

test('aborts a failed shared image transfer and releases its completion waiter', async () => {
  const requests = []
  let rejectCompletion = () => {}
  let cancelled = false
  const port = {
    allocateTransaction: () => 4,
    request: async (opcode) => {
      requests.push(opcode)
      if (opcode === core.CommandV2.TRANSFER_BEGIN) return Uint8Array.from([0, 0, 20, 0])
      if (opcode === core.CommandV2.TRANSFER_COMMIT) throw new Error('commit failed')
      return new Uint8Array(0)
    },
    waitForCompletion: () => new Promise((resolve, reject) => { rejectCompletion = reject }),
    cancelCompletion: (_transactionId, reason) => { cancelled = true; rejectCompletion(reason) },
  }
  const profile = {
    schemaVersion: 1, controller: 0xfe, recipe: 0, color: 1, transform: 0, nativeUiScale: 0,
    width: 8, height: 1, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 1,
  }
  await assert.rejects(core.transferEncodedImageV2(port, {
    format: 1, transfers: [{ step: 'bw', data: Uint8Array.of(0xff) }],
  }, { screen: profile, writePayload: 20 }), /commit failed/)
  assert.equal(cancelled, true)
  assert.deepEqual(requests, [
    core.CommandV2.TRANSFER_BEGIN, core.CommandV2.TRANSFER_WRITE,
    core.CommandV2.TRANSFER_COMMIT, core.CommandV2.TRANSFER_ABORT,
  ])
})

test('shares product navigation and negotiated command availability across clients', () => {
  assert.deepEqual(core.clientNavigation(false).map((item) => item.key), [
    'overview', 'image', 'create', 'library', 'device',
  ])
  assert.equal(core.clientPageMetadata('image').title, '图片')
  assert.equal(core.clientPageMetadata('overview').icon, 'layout-dashboard')
  assert.equal(core.clientPageMetadata('device').icon, 'monitor-cog')
  assert.equal(core.clientPageMetadata('device').label, '设置')
  assert.deepEqual(core.CLIENT_DEVICE_TABS.map((item) => item.key), [
    'overview', 'screen', 'hardware', 'behavior', 'activation', 'ota', 'logs', 'test', 'preferences', 'about',
  ])
  assert.deepEqual(core.CLIENT_LIBRARY_TABS.map((item) => item.key), ['pages', 'assets', 'drafts'])
  const displayModes = (1 << core.DisplayModeV2.CALENDAR) | (1 << core.DisplayModeV2.CLEAR)
  assert.deepEqual(core.deriveV2ClientAvailability(
    core.CapabilityV2.DIRECT_TRANSFER | core.CapabilityV2.TIME,
    displayModes,
  ), {
    directImage: true, resources: false, time: true,
    calendar: true, clock: false, deviceInfo: false, clear: true,
  })
  assert.equal(core.screenColorCode(3), 'BWRY')
  assert.equal(core.screenColorCode(10), 'KWRYGB')
})

test('keeps portable storage identities and ordering independent of the backing store', () => {
  assert.equal(core.durableBleDeviceKey('AA:BB:CC:DD:EE:FF'), 'ble:AA:BB:CC:DD:EE:FF')
  assert.equal(core.durableBleDeviceKey(''), null)
  const input = [{ id: 'old', at: 1 }, { id: 'new', at: 3 }, { id: 'middle', at: 2 }]
  assert.deepEqual(core.newestFirst(input, (item) => item.at).map((item) => item.id), ['new', 'middle', 'old'])
  assert.deepEqual(input.map((item) => item.id), ['old', 'new', 'middle'])
})

test('builds the shared WebUI and Mini Program template catalog once', () => {
  assert.equal(core.CLIENT_TEMPLATE_SOURCES.length, 12)
  assert.equal(core.buildClientPageTemplates().length, 219)
  assert.equal(core.CLIENT_TEMPLATE_SOURCES.some(item => item.id === 'weather-cards'), false)
  assert.equal(core.CLIENT_TEMPLATE_KIND_LABELS['date-card'], '日期牌')
  assert.deepEqual(core.filterClientPageTemplates(core.clientTemplatesForResolution(400,300), {kind:'date-card'}).map(item=>item.name), ['日期牌','日期牌'])
  assert.equal(core.CLIENT_TEMPLATE_KIND_ORDER.includes('device-info'), false)
  assert.equal(core.buildClientPageTemplates().some(item => item.kind === 'device-info' || item.nativeAction === 'device-info'), false)
  const landscapeCalendars = core.filterClientPageTemplates(core.buildClientPageTemplates(), {
    kind: 'calendar', layout: 'landscape', query: '400x300',
  })
  assert.equal(landscapeCalendars.length, 1)
  assert.deepEqual(core.paginateClientPageTemplates(core.buildClientPageTemplates(), 2).items.length, 6)
  assert.equal(core.clientTemplatesForResolution(128, 250).find((item) => item.kind === 'todo').layout, 'landscape')
  assert.deepEqual(core.COMMON_TEMPLATE_SCREENS.find((item) => item.id === 'square-240'), {
    id: 'square-240', label: '1.54 寸 240 方屏', width: 240, height: 240,
  })
  assert.deepEqual(core.COMMON_TEMPLATE_SCREENS.find((item) => item.id === 'rect-296'), {
    id: 'rect-296', label: '2.66 / 2.9 寸', width: 296, height: 152,
  })
})
