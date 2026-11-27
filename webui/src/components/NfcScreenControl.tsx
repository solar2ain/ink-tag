import { Nfc } from 'lucide-react'
import { webSerial } from '../nfc/pn532'
import type { SerialScreen } from '../nfc/serialTag'
import { useNfcOperation } from '../nfc/useNfcOperation'
import { NfcOperationDialog } from './NfcOperationDialog'

export function NfcScreenControl({ disabled, onScreen }: {
  disabled: boolean
  onScreen: (screen: SerialScreen) => void
}) {
  const serialNfc = useNfcOperation()

  const scan = async () => {
    const result = await serialNfc.run('贴卡识屏', { kind: 'screen' })
    if (result?.screen) {
      onScreen(result.screen)
      serialNfc.close()
    }
  }

  return <>
    <div className="image-target-heading">
      <h2>目标屏幕</h2>
      {webSerial() && <button type="button" className="button secondary" disabled={disabled || Boolean(serialNfc.operation)} onClick={() => void scan()}><Nfc />贴卡识屏</button>}
    </div>
    {serialNfc.operation && <NfcOperationDialog operation={serialNfc.operation} close={serialNfc.close} />}
  </>
}
