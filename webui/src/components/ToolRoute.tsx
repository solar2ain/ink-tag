import { Component, lazy, Suspense, useMemo, useState, type ComponentType, type ReactNode, type ErrorInfo } from 'react'
import { useDeviceStore } from '../store/device'

export type ToolLoader = () => Promise<{ default: ComponentType }>
const resourceFailure = (error: unknown) => error instanceof Error && /dynamically imported module|Importing a module script failed|Unable to preload CSS|Loading chunk/i.test(error.message)

class ToolBoundary extends Component<{ children: ReactNode; name: string; retry: () => void }, { failed: boolean; resource: boolean }> {
  state = { failed: false, resource: false }
  static getDerivedStateFromError(error: unknown) { return { failed: true, resource: resourceFailure(error) } }
  componentDidCatch(error: Error, info: ErrorInfo) {
    // Local diagnostics only; no identity, credentials or telemetry upload.
    console.error(`${this.props.name}加载失败`, error, info.componentStack)
    useDeviceStore.getState().addLog(`${this.props.name}${this.state.resource ? '资源加载失败' : '运行失败'}：${error.message}`, 'error')
  }
  render() {
    if (!this.state.failed) return this.props.children
    return <div className="page">
      <section className="panel tool-route-error" role="alert">
        <h2>{this.props.name}未能打开</h2>
        <p>{this.state.resource ? `${this.props.name}的资源未能加载，请先重试。若仍失败，请在设备操作结束后刷新网页。` : `${this.props.name}发生了错误。可以重试，或先切换到其他页面。`}</p>
        <button className="button secondary" onClick={this.props.retry}>重试打开{this.props.name}</button>
        <p className="muted">重试不会刷新网页，也不会断开蓝牙连接。</p>
      </section>
    </div>
  }
}

/** A route failure must never replace the shell or its active BLE session. */
export function ToolRoute({ name, load }: { name: string; load: ToolLoader }) {
  const [attempt, setAttempt] = useState(0)
  const Tool = useMemo(() => lazy(load), [load, attempt])
  return <ToolBoundary key={attempt} name={name} retry={() => setAttempt(value => value + 1)}>
    <Suspense fallback={<div className="route-loading" role="status">正在打开{name}</div>}><Tool /></Suspense>
  </ToolBoundary>
}
