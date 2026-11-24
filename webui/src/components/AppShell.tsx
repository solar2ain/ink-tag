import { useEffect, type PropsWithChildren } from 'react'
import { Database, FileArchive, GitFork, HardDrive, Image, Images, LayoutDashboard, LoaderCircle, MapPinned, MonitorCog, PanelsTopLeft } from 'lucide-react'
import { NavLink } from 'react-router-dom'
import { INKTAG_WEBUI_VERSION } from '../generated/version'
import { probeRuntime, RUNTIME_MODE_LABELS, useRuntimeStore } from '../server/runtime'
import { DeviceBar } from './DeviceBar'
import { DeviceLinkConnection } from './DeviceLinkConnection'
import { OperationProgressDialog } from './OperationProgressDialog'
import { LowBatteryDialog } from './LowBatteryDialog'
import { FirmwareUpdateDialog } from './FirmwareUpdateDialog'
import { CrashAlertDialog } from './CrashAlertDialog'
import { ProBadge } from './ProBadge'
import { DevicePasswordPrompt } from './DevicePassword'
import { RecoveryDialog } from './RecoveryDialog'
import { CLIENT_NAVIGATION } from '../../../packages/inktag-client-core/src/application'
import { FirstVisitGuide } from './GettingStarted'
import { useTranslation } from '../preferences'

const navigationIcons = { 'layout-dashboard': LayoutDashboard, image: Image,
  'panels-top-left': PanelsTopLeft, images: Images, 'map-pinned': MapPinned, 'monitor-cog': MonitorCog }
export const navigation = CLIENT_NAVIGATION.map((item) => ({
  to: item.path, label: item.label, icon: navigationIcons[item.icon], end: item.path === '/', pro: item.proOnly,
}))

export function visibleNavigation() {
  return navigation
}

export function AppShell({ children }: PropsWithChildren) {
  const t = useTranslation()
  const runtime = useRuntimeStore()
  useEffect(() => { void probeRuntime() }, [])
  const visibleItems = visibleNavigation()
  const RuntimeIcon = runtime.mode === 'server' ? Database
    : runtime.mode === 'standalone' ? FileArchive
      : runtime.mode === 'checking' ? LoaderCircle : HardDrive
  const runtimeLabel = RUNTIME_MODE_LABELS[runtime.mode]

  return (
    <div className="app-shell">
      <DevicePasswordPrompt />
      <DeviceLinkConnection />
      <aside className="side-nav">
        <div className="brand" aria-label="InkTag">
          <span className="brand-mark" aria-hidden="true"><i /><i /><i /></span>
          <span>InkTag</span>
        </div>
        <nav aria-label={t('主导航')}>
          {visibleItems.map(({ to, label, icon: Icon, end, pro }) => (
            <NavLink key={to} to={to} end={end} className={({ isActive }) => isActive ? 'active' : ''}>
              <span className="nav-icon-wrap"><Icon aria-hidden="true" />{pro && <ProBadge />}</span>
              <span>{t(label)}</span>
            </NavLink>
          ))}
        </nav>
        <div className="side-footer">
          <span className={`runtime-badge ${runtime.mode}`} title={runtime.detail}>
            <RuntimeIcon className={runtime.mode === 'checking' ? 'spin' : ''} />
            <span>{runtimeLabel}</span>
          </span>
          <div className="side-release"><a href="https://github.com/solar2ain/ink-tag" target="_blank" rel="noreferrer" title="打开 ink-tag"><GitFork /></a><span>v{INKTAG_WEBUI_VERSION}</span></div>
        </div>
      </aside>
      <div className="workspace">
        <DeviceBar />
        <main>{children}</main>
      </div>
      <nav className="bottom-nav" aria-label={t('移动端主导航')}>
        {visibleItems.map(({ to, label, icon: Icon, end, pro }) => (
          <NavLink key={to} to={to} end={end} className={({ isActive }) => isActive ? 'active' : ''}>
            <span className="nav-icon-wrap"><Icon aria-hidden="true" />{pro && <ProBadge />}</span>
            <span>{t(label)}</span>
          </NavLink>
        ))}
      </nav>
      <OperationProgressDialog />
      <LowBatteryDialog />
      <CrashAlertDialog />
      <FirmwareUpdateDialog />
      <RecoveryDialog />
      <FirstVisitGuide />
    </div>
  )
}
