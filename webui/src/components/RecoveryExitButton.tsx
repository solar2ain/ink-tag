import { LoaderCircle, LogOut } from 'lucide-react'
import { useEffect, useState } from 'react'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'

export function RecoveryExitButton({ onError, onRunningChange, alwaysVisible = false, disabled = false }: {
  onError: (message: string) => void
  onRunningChange?: (running: boolean) => void
  alwaysVisible?: boolean
  disabled?: boolean
}) {
  const { connection, deviceId, firmwareVersion, action } = useDeviceStore()
  const [running, setRunning] = useState(false)
  const [phase, setPhase] = useState('正在退出…')
  const [unconfirmed, setUnconfirmed] = useState(false)
  useEffect(() => { setUnconfirmed(false) }, [connection, deviceId])
  const recoveryType = inktagClient.getOtaRecoveryType()
  const available = connection === 'connected' && firmwareVersion === 'Recovery' &&
      inktagClient.getRecoverySafeResumeSupported()
  if (!available && !alwaysVisible) return null

  const exit = async () => {
    setRunning(true)
    onRunningChange?.(true)
    onError('')
    try {
      const ota = await import('../protocol/ota')
      const options = { onPhase: setPhase }
      if (recoveryType === 'telink') await ota.exitTelinkRecovery(options)
      else if (recoveryType === 'onmicro') await ota.exitOnMicroRecovery(options)
      else if (recoveryType === 'dialog') await ota.exitDialogRecovery(options)
      else if (recoveryType === 'nordic') await ota.exitNordicRecovery(options)
      else await ota.exitGoodixRecovery(options)
    } catch (reason) {
      if ((reason as { code?: string })?.code === 'RECOVERY_RESULT_UNKNOWN') setUnconfirmed(true)
      const message = reason instanceof Error ? reason.message : String(reason)
      onError(message)
      useDeviceStore.getState().addLog(`退出 Recovery 未完成：${message}`, 'error')
    } finally {
      setRunning(false)
      onRunningChange?.(false)
    }
  }
  return <button className="button secondary" disabled={disabled || !available || running || unconfirmed || action !== 'idle'} onClick={() => void exit()}>
    {running ? <LoaderCircle className="spin" /> : <LogOut />}{running ? phase : '退出 Recovery'}
  </button>
}
