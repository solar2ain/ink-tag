import { CalendarDays, ChevronLeft, ChevronRight, Clock3, Contact, Eraser, Images, Pencil, Play, Send } from 'lucide-react'
import { useState } from 'react'
import { nfcPhoneCommand } from '../../../packages/inktag-client-core/src/nfc'
import { useNfcOperation } from '../nfc/useNfcOperation'
import { useTranslation } from '../preferences'
import { useConfirmationDialog } from './ConfirmationDialog'
import { NfcOperationDialog } from './NfcOperationDialog'

const presets = [
  ['calendar', '日历', CalendarDays], ['clock', '时钟', Clock3], ['device', '设备', Contact], ['clear', '清屏', Eraser],
  ['slot', '图槽', Images], ['slideshow', '轮播', Play], ['next', '下一张', ChevronRight], ['previous', '上一张', ChevronLeft],
] as const

export function NfcCommandControls({ disabled = false }: { disabled?: boolean }) {
  const t = useTranslation()
  const [input, setInput] = useState<'slot' | 'custom' | null>(null)
  const [slot, setSlot] = useState('1')
  const [custom, setCustom] = useState('')
  const { operation, run, close } = useNfcOperation()
  const { confirm, dialog } = useConfirmationDialog()
  const busy = disabled || Boolean(operation || dialog)
  const validSlot = /^\d{1,3}$/.test(slot) && Number(slot) >= 1 && Number(slot) <= 255
  const inputCommand = input === 'slot' && validSlot ? `slot-${Number(slot)}` : input === 'custom' ? custom.trim() : ''
  const send = (label: string, text: () => string) => void run(`NFC · ${label}`, { kind: 'command', text })

  return <div className="nfc-commands">
    <div className="nfc-command-grid" role="group" aria-label={t('NFC 命令')}>
      {presets.map(([command, label, Icon]) => <button key={command} type="button" className="button secondary"
        aria-pressed={command === 'slot' ? input === 'slot' : undefined} disabled={busy} onClick={async () => {
          if (command === 'slot') { setInput('slot'); return }
          if (command === 'clock' && !await confirm({
            title: '进入时钟页？',
            message: '时钟会频繁刷新，增加耗电。设备不支持局部刷新，或温度、屏幕条件不满足时，可能每分钟全屏刷新并闪烁，不建议长时间停留。',
            confirmLabel: '仍然进入',
          })) return
          setInput(null)
          send(t(label), () => nfcPhoneCommand(command, new Date(), true))
        }}><Icon />{t(label)}</button>)}
    </div>
    <div className="nfc-command-grid nfc-command-input-row">
      <button type="button" className="button secondary" aria-pressed={input === 'custom'} disabled={busy} onClick={() => setInput('custom')}><Pencil />{t('自定义')}</button>
      {input === 'slot' && <input aria-label={t('图槽编号')} placeholder={t('图槽编号')} inputMode="numeric" value={slot} maxLength={3}
        disabled={busy} onChange={event => setSlot(event.target.value)} />}
      {input === 'custom' && <input aria-label={t('图片别名或命令')} placeholder={t('图片别名或命令')} value={custom} maxLength={54}
        disabled={busy} onChange={event => setCustom(event.target.value)} />}
      <button type="button" className="button secondary nfc-command-send" disabled={busy || !inputCommand}
        onClick={() => send(input === 'slot' ? `${t('图槽')} ${Number(slot)}` : t('自定义'), () => inputCommand)}><Send />{t('发送指令')}</button>
    </div>
    {dialog}
    {operation && <NfcOperationDialog operation={operation} close={close} />}
  </div>
}
