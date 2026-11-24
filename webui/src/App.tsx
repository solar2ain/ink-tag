import { Navigate, Route, Routes } from 'react-router-dom'
import { AppShell } from './components/AppShell'
import { CreatePage } from './pages/CreatePage'
import { DashboardPage } from './pages/DashboardPage'
import { DevicePage } from './pages/DevicePage'
import { LibraryPage } from './pages/LibraryPage'
import { FindPage } from './pages/FindPage'
import { LocalWebuiHelpPage } from './pages/LocalWebuiHelpPage'
import {ImageToolRoute} from './components/ImageToolRoute'
import { ToolRoute, type ToolLoader } from './components/ToolRoute'

const loadEditor: ToolLoader = () => import('./pages/LeaferEditorPage').then(module => ({ default: module.LeaferEditorPage }))

export function App() {
  return (
    <AppShell>
      <Routes>
        <Route path="/" element={<DashboardPage />} />
        <Route path="/image" element={<ImageToolRoute />} />
        <Route path="/create" element={<CreatePage />} />
        <Route path="/create/canvas/:templateId" element={<ToolRoute name="创作画板" load={loadEditor} />} />
        <Route path="/library" element={<LibraryPage />} />
        <Route path="/device/*" element={<DevicePage />} />
        <Route path="/find" element={<FindPage />} />
        <Route path="/help/local-webui" element={<LocalWebuiHelpPage />} />
        <Route path="/preferences" element={<Navigate to="/device?tab=preferences" replace />} />
        <Route path="/lab" element={<Navigate to="/device?tab=test" replace />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </AppShell>
  )
}
