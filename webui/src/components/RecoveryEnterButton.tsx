import { LoaderCircle, LogIn } from 'lucide-react'
import { useState } from 'react'
import { useDeviceStore } from '../store/device'
import { PROTOCOL_V2_VERSION } from '../protocol/generatedCommandsV2'
import { useConfirmationDialog } from './ConfirmationDialog'

export function RecoveryEnterButton({ onError, onRunningChange, alwaysVisible = false, disabled = false }: {
  onError: (message: string) => void
  onRunningChange: (running: boolean) => void
  alwaysVisible?: boolean
  disabled?: boolean
}) {
  const { connection, deviceId, firmwareVersion, protocolVersion, capabilities, action } = useDeviceStore()
  const [running, setRunning] = useState(false)
  const { confirm, dialog } = useConfirmationDialog()
  const available = connection === 'connected' && firmwareVersion !== 'Recovery' && protocolVersion === PROTOCOL_V2_VERSION && capabilities.ota !== 'none'
  if (!available && !running && !alwaysVisible) return null
  const enter = async () => {
    const expectedId = deviceId
    setRunning(true); onRunningChange(true); onError('')
    try {
      if (!await confirm({ title: '进入 Recovery', confirmLabel: '确认进入', message: '将同一设备切换到固件维护模式，不发送升级包或清除用户数据。设备密码仍须验证。\n\nRecovery 模式下 MCU 不会休眠，十分耗电，请不要长期停留在 Recovery 模式。' })) return
      const ota = await import('../protocol/ota')
      await ota.enterFixedRecovery(expectedId)
    } catch (reason) {
      const message = reason instanceof Error ? reason.message : String(reason)
      onError(message)
      useDeviceStore.getState().addLog(`进入 Recovery 未完成：${message}`, 'error')
    } finally { setRunning(false); onRunningChange(false) }
  }
  return <><button className="button secondary" type="button" disabled={disabled || !available || running || action !== 'idle'} onClick={() => void enter()}>
    {running ? <LoaderCircle className="spin" /> : <LogIn />}{running ? '正在进入…' : '进入 Recovery'}
  </button>{dialog}</>
}
