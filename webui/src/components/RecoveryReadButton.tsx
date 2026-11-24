import { FeedbackNotice, useFeedbackMessage } from './FeedbackNotice'
import { LoaderCircle, SearchCheck } from 'lucide-react'
import { useState } from 'react'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'

export function RecoveryReadButton({ onRunningChange, collapsed = false }: { onRunningChange: (running: boolean) => void; collapsed?: boolean }) {
  const { action, connection } = useDeviceStore()
  const [running, setRunning] = useState(false)
  const [result, setResult, resultRevision] = useFeedbackMessage('')
  const [failed, setFailed] = useState(false)
  const expected = inktagClient.getExpectedRecoveryIdentity()
  const nordic = inktagClient.getOtaRecoveryType() === 'nordic'
  const available = nordic || inktagClient.getOtaRecoveryType() === 'telink'
    || /^(?:IKT|InkTag)_TL_[0-9A-F]{4}_OTA$/i.test(expected?.recoveryName || '')
  // connection subscribes this component to the App -> Recovery transition.
  void connection
  if (!available && !running && !result) return null
  const read = async () => {
    setRunning(true); onRunningChange(true); setResult(''); setFailed(false)
    const log = (message: string) => { setResult(message); useDeviceStore.getState().addLog(message) }
    try {
      const ota = await import('../protocol/ota')
      log(await (nordic ? ota.inspectNordicRecovery(log) : ota.inspectTelinkRecovery(log)))
    } catch (reason) {
      const message = reason instanceof Error ? reason.message : String(reason)
      setResult(message); setFailed(true); useDeviceStore.getState().addLog(message, 'error')
    } finally { setRunning(false); onRunningChange(false) }
  }
  const control = <div><button className="button secondary" disabled={!available || running || action !== 'idle'} onClick={() => void read()}>
    {running ? <LoaderCircle className="spin" /> : <SearchCheck />}{running ? '正在读取…' : '读取 Recovery 状态'}
  </button>{result && <FeedbackNotice noticeKey={resultRevision} className={failed ? 'library-message error' : 'field-note'} role={failed ? 'alert' : 'status'}>{result}</FeedbackNotice>}</div>
  return collapsed ? <details className="recovery-read-details"><summary>详情</summary>{control}</details> : control
}
