import { vi } from 'vitest'
import source from '../../ota-drivers/goodix_ota.js?raw'
import rejection from '../../ota-drivers/ota_rejection.js?raw'

function adapter() {
  const scope: any = {}
  new Function('window', rejection)(scope)
  new Function('window', source)(scope)
  return scope.GoodixOta
}
export const never = () => new Promise<any>(() => {})
export function deferred() {
  let resolve!: (value: any) => void
  const promise = new Promise<any>(done => { resolve = done })
  return { promise, resolve }
}
export function goodixFixture(recoveryInfo?: Uint8Array) {
  const ota = adapter()
  const profile = ota.layoutProfile({ board: 'gr551x_zkc42v', layout: 'layout-v1', compatibilityEpoch: 1 })
  const target = new Uint8Array(54)
  target[0] = 1
  const v = new DataView(target.buffer)
  v.setUint32(1, profile.activeAddress, true)
  v.setUint16(6, 0x4744, true)
  v.setUint16(8, 1, true)
  v.setUint32(10, 64, true)
  v.setUint32(18, profile.activeAddress, true)
  v.setUint32(22, profile.activeAddress, true)
  v.setUint32(46, profile.layoutMarker, true)
  v.setUint32(50, ~profile.layoutMarker >>> 0, true)
  const pkg = { manifest: { board: 'gr551x_zkc42v', layout: 'layout-v1', compatibilityEpoch: 1 },
    application: { active_address: profile.activeAddress }, firmwareType: 1,
    imageInfo: new Uint8Array(48), firmware: new Uint8Array(1300) }
  let listener: ((event: any) => void) | undefined
  const commands: number[] = []
  const parser = new ota.FrameParser()
  const tx = { startNotifications: vi.fn(async () => tx), stopNotifications: vi.fn(async () => undefined),
    addEventListener: vi.fn((_type, callback) => { listener = callback }),
    removeEventListener: vi.fn((_type, callback) => { if (listener === callback) listener = undefined }) }
  const receive = (bytes: Uint8Array) => {
    const [request] = parser.feed(bytes)
    if (!request) return
    commands.push(request.command)
    let reply = new Uint8Array([1])
    if (request.command === 1) { reply = new Uint8Array(18); reply[0] = 1; reply[17] = 2; if(recoveryInfo) reply=new Uint8Array(recoveryInfo) }
    else if (request.command === 0x42) reply = target
    else if (request.command === 0x23 && target[5] === 1) throw new Error('PROGRAM in App')
    queueMicrotask(() => {
      const data = ota.frame(request.command, reply)
      listener?.({ target: { value: new DataView(data.buffer) } })
    })
  }
  const rx = { writeValueWithoutResponse: vi.fn(async (bytes: Uint8Array) => receive(bytes)) }
  const ctrl = { writeValueWithoutResponse: vi.fn(async () => undefined) }
  const service = { getCharacteristic: vi.fn(async (uuid: string) => uuid.includes('0402') ? tx : uuid.includes('0403') ? rx : ctrl) }
  const server = { connected: true, disconnect: vi.fn(() => { server.connected = false }),
    connect: vi.fn(async () => { server.connected = true; return server }), getPrimaryService: vi.fn(async (_uuid: string) => service) }
  const device = Object.assign(new EventTarget(), { id: 'exact-fc34-grant', name: 'IKT_GR_FC34', gatt: server })
  Object.defineProperty(server, 'device', { value: device })
  return { ota, target, pkg, tx, rx, ctrl, service, server, commands, receive, device }
}
