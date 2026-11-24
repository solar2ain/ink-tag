export type BatteryTone = 'normal' | 'warning' | 'critical' | 'unknown'

export interface BatteryVisualState {
  level: number
  tone: BatteryTone
}

const EMPTY_VOLTAGE = 2.4
const FULL_VOLTAGE = 3.3
const WARNING_VOLTAGE = 2.6

export function batteryVisualState(voltage: number | null): BatteryVisualState {
  if (voltage == null || !Number.isFinite(voltage)) return { level: 0, tone: 'unknown' }

  const level = Math.max(0, Math.min(1, (voltage - EMPTY_VOLTAGE) / (FULL_VOLTAGE - EMPTY_VOLTAGE)))
  const tone = voltage <= EMPTY_VOLTAGE
    ? 'critical'
    : voltage <= WARNING_VOLTAGE
      ? 'warning'
      : 'normal'
  return { level, tone }
}

export function VoltageBatteryIcon({ voltage }: { voltage: number | null }) {
  const { level, tone } = batteryVisualState(voltage)
  const fillWidth = 10 * level

  return (
    <svg
      className={`voltage-battery ${tone}`}
      viewBox="0 0 20 14"
      role="img"
      aria-label={voltage == null ? '电池电量未知' : `电池电量约 ${Math.round(level * 100)}%`}
    >
      <rect className="voltage-battery-outline" x="1" y="2" width="15" height="10" rx="2" />
      <path className="voltage-battery-terminal" d="M18 5v4" />
      {fillWidth > 0 && <rect className="voltage-battery-fill" x="3.5" y="4.5" width={fillWidth} height="5" rx=".7" />}
    </svg>
  )
}
