import { ImagePlus, X } from 'lucide-react'
import { useEffect, useState } from 'react'
import { createPortal } from 'react-dom'
import { Link } from 'react-router-dom'
import { db, type Asset } from '../data/db'

/** Insert a copy of a local asset; editing the page never changes the original. */
export function ImageAssetPicker({ choose, close }: { choose: (asset: Asset) => void; close: () => void }) {
  const [assets, setAssets] = useState<Asset[]>([])
  const [status, setStatus] = useState('正在读取图片素材…')
  useEffect(() => {
    let cancelled = false
    void db.assets.orderBy('createdAt').reverse().toArray().then(records => {
      if (cancelled) return
      setAssets(records.filter(asset => ['image/png', 'image/jpeg', 'image/webp'].includes(asset.type)))
      setStatus('')
    }).catch(() => { if (!cancelled) setStatus('无法读取素材库，请重试。') })
    return () => { cancelled = true }
  }, [])
  useEffect(() => {
    const key = (event: KeyboardEvent) => { if (event.key === 'Escape') { event.stopPropagation(); close() } }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [close])
  return createPortal(<div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) close() }}>
    <section className="confirmation-dialog image-asset-dialog" role="dialog" aria-modal="true" aria-labelledby="image-asset-title">
      <header><div><ImagePlus /><h2 id="image-asset-title">插入图片素材</h2></div><button className="icon-button" aria-label="关闭" onClick={close}><X /></button></header>
      {status ? <p role="status">{status}</p> : assets.length ? <div className="image-asset-grid">{assets.map(asset => <AssetButton key={asset.id} asset={asset} choose={() => { choose(asset); close() }} />)}</div>
        : <p>还没有可用的图片素材。可先在资源中导入 PNG、JPEG 或 WebP 图片。</p>}
      <footer><Link className="button secondary" to="/library?tab=assets" onClick={close}>管理图片素材</Link><button className="button secondary" onClick={close}>取消</button></footer>
    </section>
  </div>, document.body)
}

function AssetButton({ asset, choose }: { asset: Asset; choose: () => void }) {
  const [url, setUrl] = useState('')
  useEffect(() => {
    const next = URL.createObjectURL(asset.blob)
    setUrl(next)
    return () => URL.revokeObjectURL(next)
  }, [asset.blob])
  return <button title={asset.name} onClick={choose}>{url && <img src={url} alt="" />}<span>{asset.name}</span></button>
}
