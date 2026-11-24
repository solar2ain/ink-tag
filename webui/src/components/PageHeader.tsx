import type { ReactNode } from 'react'
import { useTranslation } from '../preferences'

export function PageHeader({ title, actions }: { title: string; actions?: ReactNode }) {
  const t = useTranslation()
  return (
    <div className="page-header">
      <div>
        <h1>{t(title)}</h1>
      </div>
      {actions && <div className="page-actions">{actions}</div>}
    </div>
  )
}
