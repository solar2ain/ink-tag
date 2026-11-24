import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { createHashRouter, RouterProvider } from 'react-router-dom'
import { App } from './App'
import { initializeLayoutMode } from './layout'
import { initializeTheme } from './theme'
import { initializePreferences } from './preferences'
import './styles.css'
import { consumePasswordQrLocation } from './data/passwordCredentials'

initializeLayoutMode()
initializeTheme()
initializePreferences()
consumePasswordQrLocation()
const router = createHashRouter([{ path: '*', element: <App /> }])

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
)
