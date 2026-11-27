import {
  decodeNfcImageTarget, INKTAG_NFC_D1, encodeNfcTextRecord, parseInkTagNfcD1,
  nfcApduData, writeInkTagNfcType4Command, type NfcExchange, type NfcImageTarget,
} from '../../../packages/inktag-client-core/src/nfc'
import { formatImageRate } from '../image/transferRate'
import { encodeNfcImage, type EncodedImage } from '../protocol/image'
import { encodeLzo1x, LZO_MAX_DECODED_BLOCK } from '../protocol/lzo1x'
import { colorCode, type RgbaFrame } from '../protocol/screen'
import { isoDepExchange } from './isoDep'
import { Pn532, Type2RfTimeout, type SerialPort } from './pn532'
import { InkTagType2, TYPE2_IMAGE_GROUP_PACKETS } from './type2'

export type SerialOperation =
  | { kind: 'read' }
  | { kind: 'screen' }
  | { kind: 'command'; text: () => string }
  | { kind: 'image'; frame: RgbaFrame; color: number }

export type SerialScreen = { width: number; height: number; color: number }

export interface SerialImageProgress {
  confirmed: number
  submitted?: number
  total: number
  bytesPerSecond: number
  decodedBytesPerSecond?: number
}

export function imagePackets(target: NfcImageTarget, image: EncodedImage, compress: boolean) {
  if (target.packing !== image.format || target.planes !== image.transfers.length ||
      image.transfers.some(transfer => transfer.data.length !== target.planeBytes)) {
    throw new Error('图片编码长度与标签屏幕参数不一致')
  }
  const packets: Uint8Array[] = []
  for (const [plane, { data }] of image.transfers.entries()) {
    if (!compress) {
      for (let offset = 0; offset < data.length; offset += 250) {
        const chunk = data.subarray(offset, offset + 250)
        packets.push(Uint8Array.of(0xf0, 0xd2, plane, (offset / 250) & 255, chunk.length, ...chunk))
      }
      continue
    }
    for (let index = 0; index < data.length; index += LZO_MAX_DECODED_BLOCK) {
      const block = encodeLzo1x(data.subarray(index, index + LZO_MAX_DECODED_BLOCK))
      if (block.length > 2192) throw new Error('LZO 压缩块超出标签缓冲区')
      for (let offset = 0; offset < block.length; offset += 250) {
        const chunk = block.subarray(offset, offset + 250)
        packets.push(Uint8Array.of(0xf0, 0xd3, plane, Number(offset + chunk.length === block.length),
          chunk.length + 2, (index / LZO_MAX_DECODED_BLOCK) & 255, offset / 250, ...chunk))
      }
    }
  }
  return packets
}

const sameUid = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((value, index) => value === b[index])

export async function useSerialTag(port: SerialPort, action: SerialOperation, expectedUid: Uint8Array | undefined,
  signal: AbortSignal, progress: (status: string, image?: SerialImageProgress, waitingForDevice?: boolean) => void,
  onReaderReady?: () => void, t: (text: string) => string = text => text) {
  const reader = new Pn532(port, signal)
  const { result, firmware } = await reader.run(async () => {
    onReaderReady?.()
    progress('请将 PN532 读卡器放到设备上。', undefined, true)
    const { uid, kind, ats } = await reader.select()
    if (expectedUid && !sameUid(expectedUid, uid)) throw new Error('检测到另一张标签，请重新识屏或贴回已选择的设备')
    const tagType = kind === 'type2' ? 'Type 2' : 'Type 4'
    const tagUid = Array.from(uid, byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase()
    progress(t('已检测到设备（{device}），请保持贴近。').replace('{device}', `${tagType} · UID ${tagUid}`))
    await reader.setup(kind)
    const openType2 = () => new InkTagType2(reader.type2Frame, uid, () => reader.reselectType2(uid))
    let type2 = kind === 'type2' ? openType2() : undefined
    let exchange: NfcExchange = type2?.exchange ?? isoDepExchange(ats, (frame, wait) => reader.isoFrame(frame, wait))
    const getD1 = async () => parseInkTagNfcD1(await exchange(INKTAG_NFC_D1), uid)
    if (action.kind === 'command') {
      progress('正在核对设备并发送指令。')
      const makeRecord = () => encodeNfcTextRecord(action.text())
      if (type2) await type2.writeText(makeRecord)
      else {
        await getD1()
        await writeInkTagNfcType4Command(exchange, makeRecord())
      }
      return { uid, tagType, outcome: '指令已发送，请在设备上查看执行结果。' }
    }

    if (action.kind === 'read' && type2) {
      const name = await type2.readName()
      if (!name) throw new Error('未读取到有效的 InkTag NDEF 设备链接')
      return { uid, tagType, outcome: name }
    }
    const getScreen = async () => {
      await type2?.open()
      return getD1()
    }
    progress('正在读取 D1 屏幕参数。')
    const fields = await getScreen().catch(async (error: Error) => {
      if (!type2 || !(error.cause instanceof Type2RfTimeout)) throw error
      // A mirror query may time out before image DATA begins. Reselect the
      // same UID after the volatile mirror has cleared, then use a fresh nonce.
      signal.throwIfAborted()
      progress('正在重新读取屏幕参数，请保持贴近。')
      await reader.reselectType2(uid, 500)
      type2 = openType2()
      exchange = type2.exchange
      return getScreen()
    })
    const target = decodeNfcImageTarget(fields)
    const turned = Boolean(target.transform & 1)
    const width = turned ? target.visibleHeight : target.visibleWidth
    const height = turned ? target.visibleWidth : target.visibleHeight
    const screen = { width, height, color: target.color }
    const description = `${width} × ${height} · ${colorCode(screen.color)}`
    const compression = fields.get(0xd1)
    const compress = compression?.length === 7 && compression[0] === 1
    if (action.kind === 'read' || action.kind === 'screen') return {
      uid, tagType, screen, outcome: `${description} · ${compress ? 'LZO 图传' : 'D2 图传'}`,
    }

    if (action.frame.width !== width || action.frame.height !== height || action.color !== target.color) {
      return {
        uid, tagType, screen,
        outcome: `已将目标屏幕切换为 ${description}。请关闭窗口，确认预览后再次点击「NFC 传图」。`,
      }
    }
    const image = encodeNfcImage(action.frame, target, target.packing)
    const packets = imagePackets(target, image, compress)
    if (type2) {
      progress('正在校验 Type 2 图传通道。')
      await type2.qualifyWrites(fields.get(0xe2))
      progress(`Type 2 图传通道已就绪（${type2.supportsImageGroups ? '支持分组确认' : '逐包确认'}）。`)
    }
    const started = performance.now()
    const codec = compress ? 'LZO' : 'D2'
    let sent = 0
    let decoded = 0
    let confirmedProgress: SerialImageProgress = {
      confirmed: 0, total: packets.length, bytesPerSecond: 0, decodedBytesPerSecond: compress ? 0 : undefined,
    }
    progress(`${codec} 传图 0/${packets.length}，请保持贴近。`, confirmedProgress)
    let firstBlockConfirmed = false
    for (let index = 0; index < packets.length;) {
      signal.throwIfAborted()
      let batchSize = 1
      if (firstBlockConfirmed && type2?.supportsImageGroups) {
        const group = packets.slice(index, index + TYPE2_IMAGE_GROUP_PACKETS)
        await type2.exchangeImageGroup(group, count => {
          const submitted = index + count
          const phase = count === group.length ? '等待本组确认' : '请保持贴近'
          progress(`${codec} 已发送 ${submitted}/${packets.length} 包 · 已确认 ${index} 包，${phase}。`, {
            ...confirmedProgress, submitted,
          })
        })
        batchSize = group.length
      } else {
        // The initial image session must get a real application response.
        nfcApduData(await exchange(packets[index]))
        firstBlockConfirmed = !compress || packets[index][3] === 1
      }
      const end = index + batchSize
      for (; index < end; index++) {
        const packet = packets[index]
        sent += packet.length - (compress ? 7 : 5)
        if (compress && packet[3]) {
          decoded += Math.min(LZO_MAX_DECODED_BLOCK, target.planeBytes - decoded % target.planeBytes)
        }
      }
      const seconds = Math.max((performance.now() - started) / 1000, 0.001)
      confirmedProgress = {
        confirmed: index, total: packets.length,
        bytesPerSecond: sent / seconds, decodedBytesPerSecond: compress ? decoded / seconds : undefined,
      }
      progress(`${codec} 传图 ${index}/${packets.length} · ${sent} 字节，请保持贴近。`, confirmedProgress)
    }
    const seconds = Math.max((performance.now() - started) / 1000, 0.001)
    const averageSpeed = formatImageRate(sent / seconds)
    const equivalent = compress ? ` · 原图等效 ${formatImageRate(decoded / seconds)}` : ''
    signal.throwIfAborted()
    progress('正在请求设备刷新（D4）。')
    try {
      nfcApduData(await exchange(Uint8Array.of(0xf0, 0xd4, 0, 0, 0)))
    } catch (error) {
      throw new Error(`刷新请求未确认，请查看设备。 ${error instanceof Error ? error.message : ''}`)
    }
    return { uid, tagType, outcome: `${description} · ${codec} ${sent} 字节 · DATA ${seconds.toFixed(2)} 秒 · ${compress ? '压缩平均' : '平均'} ${averageSpeed}${equivalent}。设备已接受刷新请求（9000）。` }
  })
  return { ...result, firmware }
}
