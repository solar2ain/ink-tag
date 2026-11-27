import type { NfcExchange } from '../../../packages/inktag-client-core/src/nfc'

export function isoDepExchange(
  ats: Uint8Array,
  transmit: (frame: Uint8Array, waitMs: number) => Promise<Uint8Array>,
): NfcExchange {
  if (!ats.length || ats.length > 20 || ats[0] !== ats.length) throw new Error('Type 4 ATS 长度无效')
  const flags = ats.length > 1 ? ats[1] : 2
  const fsc = [16, 24, 32, 40, 48, 64, 96, 128, 256][flags & 15]
  const tbIndex = 2 + Number(Boolean(flags & 0x10))
  const interfaceBytes = 2 + Number(Boolean(flags & 0x10)) + Number(Boolean(flags & 0x20)) + Number(Boolean(flags & 0x40))
  if (!fsc || (flags & 0x80) || (ats.length > 1 && interfaceBytes > ats.length)) throw new Error('Type 4 ATS 接口参数无效')
  const fwi = flags & 0x20 ? ats[tbIndex] >>> 4 : 4
  if (fwi === 15) throw new Error('Type 4 ATS 等待时间无效')
  const fwt = (4096 / 13560) * 2 ** fwi
  const limit = Math.min(fsc - 3, 240)
  const baseWait = Math.max(100, fwt * 1.1 + 20)
  let number = 0

  // Match the CLI's raw ISO-DEP path, including small FSC and periodic WTX.
  return async apdu => {
    let offset = 0
    let more = false
    const command = () => {
      const part = apdu.subarray(offset, offset + limit)
      offset += part.length
      more = offset < apdu.length
      return Uint8Array.of(2 | number | (more ? 0x10 : 0), ...part)
    }
    let frame: Uint8Array = command()
    let wait = baseWait
    const data: number[] = []
    const deadline = performance.now() + 30_000
    for (let count = 0; count < 256 && performance.now() + wait < deadline; count++) {
      const reply = await transmit(frame, wait)
      if (!reply.length || reply.length + 2 > fsc) throw new Error('Type 4 数据块长度无效')
      const pcb = reply[0]
      if (pcb === 0xf2) {
        const factor = reply[1] & 63
        if (reply.length !== 2 || factor < 1 || factor > 59) throw new Error('Type 4 等待扩展无效')
        wait = Math.max(baseWait, fwt * factor * 1.1 + 20)
        frame = reply
        continue
      }
      wait = baseWait
      if (more) {
        if (reply.length !== 1 || pcb !== (0xa2 | number)) throw new Error('Type 4 数据分片未确认')
        number ^= 1
        frame = command()
        continue
      }
      if ((pcb & 0xef) !== (2 | number)) throw new Error('Type 4 响应序号或类型无效')
      data.push(...reply.subarray(1))
      if (data.length > 4096) throw new Error('Type 4 响应超过长度上限')
      number ^= 1
      if (!(pcb & 0x10)) return Uint8Array.from(data)
      frame = Uint8Array.of(0xa2 | number)
    }
    throw new Error('Type 4 响应超时，请查看设备状态')
  }
}
