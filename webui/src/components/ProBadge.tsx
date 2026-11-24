export function ProBadge({ className = '' }: { className?: string }) {
  return <span className={`pro-badge${className ? ` ${className}` : ''}`} title="Pro 功能" aria-label="Pro 功能">Pro</span>
}
