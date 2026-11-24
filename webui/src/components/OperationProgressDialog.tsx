import { CircleAlert, CircleStop, LoaderCircle, RefreshCw, X } from 'lucide-react'
import { useEffect, useState } from 'react'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'

const actionLabels = {
  'device-info': '正在绘制设备信息页',
  clear: '正在清屏',
  calendar: '正在绘制日历',
  clock: '正在绘制时钟',
  picture: '正在更新图片',
  'slideshow-starting': '正在启动图片轮播',
} as const

export function OperationProgressDialog() {
  const { action, actionDetail, actionProgress, actionStartedAt, operationFailure, clearOperationFailure } = useDeviceStore()
  const [visible, setVisible] = useState(false)
  const [now, setNow] = useState(Date.now())
  const [stopping, setStopping] = useState(false)
  const [stopError, setStopError] = useState('')
  const activeTracked = action === 'device-info' || action === 'clear' || action === 'calendar' || action === 'clock' || action === 'picture' || action === 'slideshow-starting'
  const tracked = activeTracked || operationFailure !== null

  useEffect(() => {
    setVisible(false)
    setStopping(false)
    setStopError('')
    if (!tracked) return
    if (operationFailure) {
      setVisible(true)
      return
    }
    const timer = window.setTimeout(() => setVisible(true), 600)
    return () => window.clearTimeout(timer)
  }, [tracked, actionStartedAt, operationFailure])

  useEffect(() => {
    if (!visible) return
    setNow(Date.now())
    const timer = window.setInterval(() => setNow(Date.now()), 1000)
    return () => window.clearInterval(timer)
  }, [visible])

  if (!tracked || !visible) return null
  const failed = operationFailure !== null
  const displayedAction = operationFailure?.action || action
  const progress = actionProgress == null ? null : Math.max(0, Math.min(1, actionProgress))
  const startedAt = operationFailure?.startedAt || actionStartedAt
  const elapsedEnd = operationFailure?.failedAt || now
  const elapsed = startedAt ? Math.max(0, Math.floor((elapsedEnd - startedAt) / 1000)) : 0
  const busyConflict = actionDetail.startsWith('设备正在执行之前的屏幕任务')
  const defaultTitle = actionLabels[displayedAction as keyof typeof actionLabels] || '正在执行设备操作'
  const phaseTitle = actionDetail.includes('正在刷新墨水屏') || actionDetail.includes('清空并刷新墨水屏')
    ? '正在刷新屏幕'
    : actionDetail.includes('绘制')
      ? '正在绘制布局'
      : defaultTitle

  const stop = async () => {
    setStopping(true)
    setStopError('')
    try {
      if (busyConflict) await inktagClient.replaceBusyDisplayWork()
      else await inktagClient.stop()
    } catch (error) {
      setStopError(error instanceof Error ? error.message : String(error))
      setStopping(false)
    }
  }

  const retry = async () => {
    const run = operationFailure?.retry
    clearOperationFailure()
    if (!run) return
    await run().catch(() => undefined)
  }

  return (
    <div className="operation-backdrop">
      <section className={`operation-dialog ${failed ? 'failed' : ''}`} role="dialog" aria-modal="true" aria-live="polite" aria-labelledby="operation-title">
        <header>
          {failed ? <CircleAlert /> : <LoaderCircle className="spin" />}
          <div><strong id="operation-title">{failed ? `${phaseTitle.replace(/^正在/, '')}失败` : stopping ? '正在取消操作' : busyConflict ? '设备正忙' : phaseTitle}</strong><span>{failed ? operationFailure.detail : stopError || (stopping ? '正在等待设备安全停止屏幕任务。' : actionDetail || '设备正在处理，请保持连接。')}</span></div>
        </header>
        <div
          className={`operation-progress ${failed ? 'failed' : progress == null ? 'indeterminate' : ''}`}
          role="progressbar"
          aria-label={failed ? '操作失败' : progress == null ? '设备处理中' : '图片传输进度'}
          aria-valuemin={failed || progress != null ? 0 : undefined}
          aria-valuemax={failed || progress != null ? 100 : undefined}
          aria-valuenow={failed ? 100 : progress == null ? undefined : Math.round(progress * 100)}
        >
          <i style={failed ? { width: '100%' } : progress == null ? undefined : { width: `${progress * 100}%` }} />
        </div>
        <footer>
          <span>{failed ? `失败前已运行 ${elapsed} 秒` : `已用时 ${elapsed} 秒${progress == null ? '' : ` · ${Math.round(progress * 100)}%`}`}</span>
          {failed ? <div className="operation-dialog-actions">
            <button className="button secondary" type="button" onClick={clearOperationFailure}><X />关闭</button>
            {operationFailure.retry && <button className="button primary" type="button" onClick={() => void retry()}><RefreshCw />重试</button>}
          </div> : <div className="operation-dialog-actions">
            {busyConflict && <button className="button secondary" type="button" disabled={stopping} onClick={() => inktagClient.keepCurrentDisplayWork()}>保持当前任务</button>}
            <button className={busyConflict ? 'button primary' : 'button secondary'} type="button" disabled={stopping} onClick={() => void stop()}>{stopping ? <LoaderCircle className="spin" /> : busyConflict ? <RefreshCw /> : <CircleStop />}{stopping ? (busyConflict ? '替换中' : '取消中') : busyConflict ? '替换为新任务' : '取消'}</button>
          </div>}
        </footer>
      </section>
    </div>
  )
}
