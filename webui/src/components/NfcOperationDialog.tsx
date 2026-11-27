import { Nfc, X } from 'lucide-react'
import { useEffect, useId, useRef } from 'react'
import { formatImageRate } from '../image/transferRate'
import type { NfcOperationState } from '../nfc/useNfcOperation'
import { useTranslation } from '../preferences'

export function NfcOperationDialog({ operation, close }: { operation: NfcOperationState; close: () => void }) {
  const t = useTranslation()
  const dialog = useRef<HTMLDialogElement>(null)
  const titleId = useId()
  useEffect(() => { dialog.current?.showModal() }, [])
  const image = operation.image
  const confirmed = image ? 100 * image.confirmed / image.total : 0
  const submitted = image ? 100 * (image.submitted ?? image.confirmed) / image.total : 0

  return <dialog ref={dialog} className="nfc-operation-dialog" aria-labelledby={titleId}
    onCancel={event => { event.preventDefault(); close() }}>
    <header><h2 id={titleId}>{t(operation.title)}</h2>
      <button className="icon-button" type="button" aria-label={t('关闭窗口')} onClick={close}><X /></button>
    </header>
    <div className="nfc-operation-body">
      {operation.busy && operation.placement ? (
        <div className="nfc-placement" data-placement={operation.placement} aria-hidden="true">
          {operation.placement === 'reader' ? <>
            <span className="nfc-placement-tag">{t('设备')}</span>
            <span className="nfc-placement-reader"><Nfc />PN532</span>
          </> : <>
            <span className="nfc-placement-tag"><Nfc /></span>
            <span className="nfc-placement-phone" />
          </>}
          <span className="nfc-placement-wave" />
        </div>
      ) : <span className="nfc-operation-icon" aria-hidden="true"><Nfc /></span>}
      <p role={operation.error ? 'alert' : 'status'}>{t(operation.status)}</p>
      {image && <div className="nfc-image-progress">
        <div className="nfc-image-progress-track">
          <div className="nfc-image-meter" role="progressbar" aria-label={t('NFC 传图已确认进度')}
            aria-valuemin={0} aria-valuemax={image.total} aria-valuenow={image.confirmed}>
            <span className="nfc-image-meter-submitted" style={{ width: `${submitted}%` }} />
            <span className="nfc-image-meter-confirmed" style={{ width: `${confirmed}%` }} />
          </div>
          <span>{t('已确认')} {Math.floor(confirmed)}%</span>
        </div>
        <div className="nfc-image-progress-rates">
          <span>{t(image.decodedBytesPerSecond === undefined ? '传输' : '压缩传输')} {formatImageRate(image.bytesPerSecond)}</span>
          {image.decodedBytesPerSecond !== undefined && <span>{t('原图等效')} {formatImageRate(image.decodedBytesPerSecond)}</span>}
        </div>
      </div>}
    </div>
    <footer><button className={`button ${operation.busy ? 'secondary' : 'primary'}`} type="button" onClick={close}>
      {t(operation.busy ? '取消' : '关闭')}
    </button></footer>
  </dialog>
}
