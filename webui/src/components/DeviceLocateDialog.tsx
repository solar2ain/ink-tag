import { Lightbulb, LoaderCircle, MapPinned, Square, X } from 'lucide-react'
import { useCallback, useEffect, useRef, useState } from 'react'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'

const LOCATE_DURATION_SECONDS = 72
const LOCATE_DURATION_MS = LOCATE_DURATION_SECONDS * 1000

interface DeviceLocateDialogProps {
  onClose: () => void
}

export function DeviceLocateDialog({ onClose }: DeviceLocateDialogProps) {
  const state = useDeviceStore()
  const [phase, setPhase] = useState<'ready' | 'starting' | 'running' | 'stopping'>('ready')
  const [remainingSeconds, setRemainingSeconds] = useState(LOCATE_DURATION_SECONDS)
  const [message, setMessage] = useState('')
  const closedRef = useRef(false)
  const hasLight = state.capabilities.led !== 'none'
  const supported = hasLight
  const running = phase === 'starting' || phase === 'running' || phase === 'stopping'

  const finish = useCallback(() => {
    if (closedRef.current) return
    closedRef.current = true
    onClose()
  }, [onClose])

  const stop = useCallback(async () => {
    if (phase !== 'running') return
    setPhase('stopping')
    setMessage('')
    try {
      await inktagClient.stopLocateDevice()
      useDeviceStore.getState().addLog('已停止寻找设备提示。', 'success')
      finish()
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
      setPhase('running')
    }
  }, [finish, phase])

  useEffect(() => {
    if (phase !== 'running') return
    const startedAt = Date.now()
    const interval = globalThis.setInterval(() => {
      const remaining = Math.max(0, LOCATE_DURATION_MS - (Date.now() - startedAt))
      setRemainingSeconds(Math.ceil(remaining / 1000))
      if (remaining === 0) {
        globalThis.clearInterval(interval)
        finish()
      }
    }, 250)
    return () => globalThis.clearInterval(interval)
  }, [finish, phase])

  useEffect(() => {
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return
      if (phase === 'running') void stop()
      else if (!running) finish()
    }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [finish, phase, running, stop])

  const start = async () => {
    if (!supported || state.connection !== 'connected') return
    setPhase('starting')
    setMessage('')
    setRemainingSeconds(LOCATE_DURATION_SECONDS)
    try {
      await inktagClient.locateDevice()
      useDeviceStore.getState().addLog('设备已开始本地灯光提示。', 'success')
      setPhase('running')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
      setPhase('ready')
    }
  }

  return <div className="modal-backdrop" onMouseDown={(event) => {
    if (event.target !== event.currentTarget) return
    if (phase === 'running') void stop()
    else if (!running) finish()
  }}>
    <section className="locate-dialog" role="dialog" aria-modal="true" aria-labelledby="locate-dialog-title">
      <header><div><MapPinned /><span><h2 id="locate-dialog-title">寻找设备</h2><p>这是本地灯光提示，不会启用或查询 Apple Find 网络。</p></span></div><button className="icon-button" title={phase === 'running' ? '停止并关闭' : '关闭'} aria-label={phase === 'running' ? '停止并关闭' : '关闭'} disabled={phase === 'starting' || phase === 'stopping'} onClick={() => phase === 'running' ? void stop() : finish()}><X /></button></header>
      {supported ? <div className="locate-behavior">
        <div className={hasLight ? '' : 'unavailable'}><Lightbulb /><span><strong>灯光</strong>{hasLight ? '红、绿、蓝每色 240 ms 依次轮换，共 100 轮（约 72 秒）。' : '当前设备没有可用状态灯。'}</span></div>
      </div> : <div className="identity-dialog-message warning">当前设备没有可用状态灯，不支持本地寻找。</div>}
      {phase === 'running' && <div className="locate-progress"><span>正在寻找</span><strong>{remainingSeconds} 秒</strong><progress max={LOCATE_DURATION_SECONDS} value={LOCATE_DURATION_SECONDS - remainingSeconds} /></div>}
      {message && <div className="identity-dialog-message error">{message}</div>}
      <footer><button className="button secondary" disabled={phase === 'starting' || phase === 'stopping'} onClick={() => phase === 'running' ? void stop() : finish()}>{phase === 'running' ? <Square /> : null}{phase === 'running' ? '停止并关闭' : '关闭'}</button>{phase !== 'running' && <button className="button primary" disabled={!supported || state.connection !== 'connected' || phase === 'starting'} onClick={() => void start()}>{phase === 'starting' ? <LoaderCircle className="spin" /> : <MapPinned />}{phase === 'starting' ? '正在启动' : '开始寻找'}</button>}</footer>
    </section>
  </div>
}
