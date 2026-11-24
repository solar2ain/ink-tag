import { CircleHelp } from 'lucide-react'
import { useDeviceStore } from '../store/device'
import { NfcCapabilityV2, NfcCommandV2 } from '../protocol/generatedCommandsV2'
import type { NfcCapabilities } from '../protocol/nfcCommand'

const commands = [
  ['NEXT', NfcCommandV2.NEXT_IMAGE, '显示下一张图片'],
  ['PREVIOUS', NfcCommandV2.PREVIOUS_IMAGE, '显示上一张图片'],
  ['CALENDAR', NfcCommandV2.CALENDAR, '切换到日历'],
  ['CLOCK', NfcCommandV2.CLOCK, '切换到时钟'],
  ['DEVICE', NfcCommandV2.DEVICE_INFO, '显示设备信息'],
  ['SLIDESHOW', NfcCommandV2.SLIDESHOW, '开始图片轮播'],
  ['CLEAR', NfcCommandV2.CLEAR, '清空当前画面'],
  ['SLOT-1', NfcCommandV2.SHOW_SLOT, '显示图槽 1 的图片'],
] as const

export function availableNfcTextCommands(caps: NfcCapabilities | null) {
  return caps && (caps.flags & NfcCapabilityV2.COMMAND) !== 0
    ? commands.filter(([, opcode]) => (caps.commandMask & (1 << opcode)) !== 0)
    : []
}

export function NfcCommandHelp() {
  const caps = useDeviceStore(state => state.nfcCapabilities)
  const pro = useDeviceStore(state => state.activation.phase === 'active' && state.activation.edition === 'advanced')
  const available = availableNfcTextCommands(caps)
  const examples = available
  return <aside className="nfc-command-help" role="note" aria-label="NFC 使用说明" tabIndex={0}>
    <CircleHelp aria-hidden="true" />
    <div><p>用手机 NFC 读取标签可打开设备链接，首次连接仍需浏览器蓝牙授权。</p>
      {!pro && <p>手机写命令需要在设备上激活 Pro。</p>}
      {examples.length > 0 && <p>在手机上打开 NFC Tools，选择“写入 → 添加记录 → 文本”，填入命令，再把手机贴近标签。</p>}
      {examples.length ? <><p>这台设备支持以下命令：</p><dl className="nfc-command-list">{examples.map(([text, , effect]) => <div key={text}><dt><code>{text}</code></dt><dd>{effect}</dd></div>)}</dl></>
        : <p>{caps ? '当前固件未启用手机文本命令。' : '尚未读到 NFC 命令能力。'}</p>}
      {available.some(([, opcode]) => opcode === NfcCommandV2.SHOW_SLOT) && <p>也可以填写设备中已有图片的完整名称，碰触后显示这张图片。每次只写一条命令，不要添加前后空格或换行。</p>}
    </div>
  </aside>
}
