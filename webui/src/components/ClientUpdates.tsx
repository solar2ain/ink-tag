import { useEffect, useState } from 'react'
import { INKTAG_WEBUI_VERSION } from '../generated/version'
import { componentUpdate, loadUpdates, type UpdateFeed } from '../protocol/updates'
import { useLanguage, useTranslation } from '../preferences'
declare const __INKTAG_CLIENT_BUILD__: { buildId: string; buildNumber: number }
export const CLIENT_BUILD = { version: INKTAG_WEBUI_VERSION, ...__INKTAG_CLIENT_BUILD__ }
export function ClientUpdates() {
  const language = useLanguage(), t = useTranslation()
  const [request, setRequest] = useState(0)
  const [state, setState] = useState<{ feed?: UpdateFeed; error?: string; checking?: boolean }>({})
  useEffect(() => {
    if (!request) return
    let cancelled = false
    setState({ checking: true })
    void loadUpdates().then(feed => { if (!cancelled) setState({ feed }) })
      .catch(error => {
        if (cancelled) return
        const detail = error instanceof Error ? error.message : '检查更新失败'
        const http = /HTTP (\d{3})/.exec(detail)
        setState({ error: language === 'en' ? http ? `Update check failed (HTTP ${http[1]})` : /超时/.test(detail)
          ? 'Update check timed out. Please try again later.' : 'Update check failed. The feed may be unavailable or invalid.' : detail })
      })
    return () => { cancelled = true }
  }, [request, language])
  const latest = state.feed?.components.webui ?? null
  const result = latest ? componentUpdate(latest, CLIENT_BUILD) : 'not-published'
  return <section className="panel client-updates">
    <div className="panel-heading"><h2>{t('客户端更新')}</h2></div>
    <p className="client-build-id">WebUI {CLIENT_BUILD.version} · {t('构建')} {CLIENT_BUILD.buildId}</p>
    <div className="client-update-controls">
    <button className="button secondary" disabled={state.checking} onClick={() => setRequest(n => n + 1)}>{t(state.checking ? '正在检查…' : '检查客户端更新')}</button></div>
    {state.error && <p role="status">{state.error} · {t('不影响设备连接与其他功能。')}</p>}
    {state.feed && <div role="status"><p>{result === 'available' ? `${t('可更新 WebUI')}：${latest!.version}（${latest!.buildId}）`
      : t(result === 'current' ? '当前 WebUI 无需更新' : '尚无已批准发布的客户端')}</p>
      {(['webui', 'cli', 'localServer'] as const).map(name => {
        const item = state.feed!.components[name]
        return item && <p key={name}>{name === 'localServer' ? t('本地服务') : name === 'cli' ? 'CLI' : 'WebUI'} {item.version}：{item.artifacts.map(a => <a key={a.name} href={a.downloadUrl} target="_blank" rel="noreferrer">{a.name}（{a.bytes} B） </a>)}</p>
      })}<p>{t('下载附件后按手册安装；网页检查不会自动安装或刷写设备。')}</p>
    </div>}
  </section>
}
