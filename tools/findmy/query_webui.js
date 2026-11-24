let csrfToken = ''
let latestCoordinates = ''
let latestReport = null
let activeMapProvider = 'osm'
let mapUrls = { baidu: '', amap: '', osm: '' }
let mapOpenUrls = { baidu: '', amap: '', osm: '' }
let identities = []
let trackingIntervalMinutes = 60
let trackFingerprint = ''
let trackView = 'plot'
let trackPoints = []
let trackMapKeys = { baidu: '', amap: '', osm: '' }
let trackMapResizeObserver = null

const byId = (id) => document.getElementById(id)
const loginPanel = byId('login-panel')
const twoFactorPanel = byId('two-factor-panel')
const queryPanel = byId('query-panel')
const message = byId('message')
const mapModal = byId('map-modal')
const trackModal = byId('track-modal')
const pageConfirmModal = byId('page-confirm-modal')
let pageConfirmResolver = null

function setBusy(busy) {
  for (const element of document.querySelectorAll('button:not(#close-map):not(#close-track), input, select')) {
    if (busy) {
      element.dataset.disabledBeforeBusy = String(element.disabled)
      element.disabled = true
    } else if ('disabledBeforeBusy' in element.dataset) {
      element.disabled = element.dataset.disabledBeforeBusy === 'true'
      delete element.dataset.disabledBeforeBusy
    }
  }
  document.body.dataset.busy = busy ? 'true' : 'false'
}

function showMessage(text, kind = 'info') {
  message.textContent = text
  message.dataset.kind = kind
}

function setQueryDisabled(element, disabled) {
  // A response received while busy must replace, not restore, stale availability.
  if ('disabledBeforeBusy' in element.dataset) element.dataset.disabledBeforeBusy = String(disabled)
  element.disabled = disabled || document.body.dataset.busy === 'true'
}

function closePageConfirm(value = null) {
  pageConfirmModal.hidden = true
  pageConfirmResolver?.(value)
  pageConfirmResolver = null
  if (mapModal.hidden && trackModal.hidden) document.body.style.removeProperty('overflow')
}

function openPageConfirm({ title, message: detail, confirmLabel = '确认', danger = false, inputLabel = '', inputValue = '' }) {
  pageConfirmResolver?.(null)
  byId('page-confirm-title').textContent = title
  byId('page-confirm-message').textContent = detail
  byId('accept-page-confirm').textContent = confirmLabel
  byId('page-confirm-form').dataset.danger = String(danger)
  const label = byId('page-confirm-input-label')
  const input = byId('page-confirm-input')
  label.hidden = !inputLabel
  byId('page-confirm-input-caption').textContent = inputLabel
  input.required = Boolean(inputLabel)
  input.value = inputValue
  pageConfirmModal.hidden = false
  document.body.style.overflow = 'hidden'
  queueMicrotask(() => (inputLabel ? input : byId('accept-page-confirm')).focus())
  return new Promise((resolve) => { pageConfirmResolver = resolve })
}

byId('page-confirm-form').addEventListener('submit', (event) => {
  event.preventDefault()
  const input = byId('page-confirm-input')
  closePageConfirm(byId('page-confirm-input-label').hidden ? true : input.value.trim())
})
byId('cancel-page-confirm').addEventListener('click', () => closePageConfirm())
byId('close-page-confirm').addEventListener('click', () => closePageConfirm())
pageConfirmModal.addEventListener('mousedown', (event) => { if (event.target === pageConfirmModal) closePageConfirm() })

function formatAge(timestamp) {
  const reportTime = new Date(timestamp).getTime()
  if (!Number.isFinite(reportTime)) return '--'
  const deltaMs = Date.now() - reportTime
  const future = deltaMs < 0
  const totalMinutes = Math.max(0, Math.round(Math.abs(deltaMs) / 60000))
  if (totalMinutes < 1) return future ? '不到 1 分钟后' : '不到 1 分钟前'
  const days = Math.floor(totalMinutes / 1440)
  const hours = Math.floor((totalMinutes % 1440) / 60)
  const minutes = totalMinutes % 60
  const parts = []
  if (days) parts.push(`${days} 天`)
  if (hours) parts.push(`${hours} 小时`)
  if (minutes && !days) parts.push(`${minutes} 分钟`)
  return `${parts.join(' ')}${future ? '后' : '前'}`
}

function setTrackView(view) {
  trackView = view === 'list' ? 'list' : 'plot'
  byId('track-plot').hidden = trackView !== 'plot'
  byId('track-list').hidden = trackView !== 'list'
  byId('track-map-config').hidden = trackView !== 'plot'
  byId('show-track-plot').setAttribute('aria-pressed', String(trackView === 'plot'))
  byId('show-track-list').setAttribute('aria-pressed', String(trackView === 'list'))
}

function renderTrackPlot(points) {
  trackPoints = points
  const shell = byId('track-plot')
  if (!points.length) {
    const empty = document.createElement('p')
    empty.className = 'track-empty'
    empty.textContent = '最近 24 小时还没有轨迹点。'
    shell.replaceChildren(empty)
    return
  }
  if (activeMapProvider === 'osm' || !trackMapKeys[activeMapProvider].trim()) {
    renderOsmTrack(points, shell)
    if (activeMapProvider !== 'osm') {
      const fallback = document.createElement('p')
      fallback.className = 'track-map-fallback'
      fallback.textContent = `未填写 ${trackMapKeyLabel(activeMapProvider)}，已自动使用 OSMap。`
      shell.append(fallback)
    }
    return
  }
  const empty = document.createElement('p')
  empty.className = 'track-empty'
  empty.textContent = `点击“绘制轨迹”，通过 ${mapProviderName(activeMapProvider)} 显示最近 24 小时的历史点。`
  shell.replaceChildren(empty)
}

function trackMapKeyLabel(provider) {
  return provider === 'baidu' ? '百度地图 AK' : provider === 'amap' ? '高德地图 Key' : ''
}

function normalizedOsmWorld(latitude, longitude) {
  const boundedLatitude = Math.max(-85.05112878, Math.min(85.05112878, Number(latitude)))
  const radians = boundedLatitude * Math.PI / 180
  return {
    x: (Number(longitude) + 180) / 360,
    y: (1 - Math.log(Math.tan(radians) + (1 / Math.cos(radians))) / Math.PI) / 2,
  }
}

function renderOsmTrack(points, shell) {
  trackMapResizeObserver?.disconnect()
  const frame = document.createElement('div')
  frame.className = 'track-map-frame'
  const map = document.createElement('div')
  map.className = 'osm-track-map'
  frame.append(map)
  shell.replaceChildren(frame)
  const render = () => {
    const width = map.clientWidth
    const height = map.clientHeight
    if (!width || !height || !points.length) return
    const tileSize = 256
    const padding = 34
    const normalized = points.map((point) => normalizedOsmWorld(point.latitude, point.longitude))
    const minX = Math.min(...normalized.map((point) => point.x))
    const maxX = Math.max(...normalized.map((point) => point.x))
    const minY = Math.min(...normalized.map((point) => point.y))
    const maxY = Math.max(...normalized.map((point) => point.y))
    const xSpan = Math.max(maxX - minX, 1 / (1 << 19))
    const ySpan = Math.max(maxY - minY, 1 / (1 << 19))
    const fitted = Math.floor(Math.log2(Math.min(
      Math.max(tileSize, width - padding * 2) / (xSpan * tileSize),
      Math.max(tileSize, height - padding * 2) / (ySpan * tileSize),
    )))
    const zoom = Math.max(2, Math.min(19, Number.isFinite(fitted) ? fitted : 19))
    const tileCount = 1 << zoom
    const scale = tileSize * tileCount
    const centerX = (minX + maxX) / 2 * scale
    const centerY = (minY + maxY) / 2 * scale
    const originX = centerX - width / 2
    const originY = centerY - height / 2
    const elements = []
    for (let y = Math.max(0, Math.floor(originY / tileSize)); y <= Math.min(tileCount - 1, Math.floor((originY + height) / tileSize)); y += 1) {
      for (let x = Math.floor(originX / tileSize); x <= Math.floor((originX + width) / tileSize); x += 1) {
        const image = document.createElement('img')
        const wrappedX = ((x % tileCount) + tileCount) % tileCount
        image.src = `https://tile.openstreetmap.org/${zoom}/${wrappedX}/${y}.png`
        image.alt = ''
        image.draggable = false
        image.referrerPolicy = 'strict-origin-when-cross-origin'
        image.style.left = `${x * tileSize - originX}px`
        image.style.top = `${y * tileSize - originY}px`
        elements.push(image)
      }
    }
    const canvas = document.createElement('canvas')
    canvas.setAttribute('aria-label', 'OpenStreetMap 最近 24 小时轨迹')
    const ratio = window.devicePixelRatio || 1
    canvas.width = Math.round(width * ratio)
    canvas.height = Math.round(height * ratio)
    const context = canvas.getContext('2d')
    context.setTransform(ratio, 0, 0, ratio, 0, 0)
    const projected = normalized.map((point) => ({ x: point.x * scale - originX, y: point.y * scale - originY }))
    context.lineCap = 'round'
    context.lineJoin = 'round'
    context.strokeStyle = 'rgba(255,255,255,.94)'
    context.lineWidth = 9
    context.beginPath()
    projected.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y))
    context.stroke()
    context.strokeStyle = '#087f5b'
    context.lineWidth = 5
    context.beginPath()
    projected.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y))
    context.stroke()
    for (const point of projected) {
      context.beginPath()
      context.arc(point.x, point.y, 2.5, 0, Math.PI * 2)
      context.fillStyle = '#087f5b'
      context.fill()
    }
    const drawMarker = (point, fill, label) => {
      context.beginPath()
      context.arc(point.x, point.y, 7, 0, Math.PI * 2)
      context.fillStyle = fill
      context.fill()
      context.lineWidth = 2
      context.strokeStyle = '#fff'
      context.stroke()
      context.fillStyle = '#fff'
      context.font = 'bold 8px sans-serif'
      context.textAlign = 'center'
      context.textBaseline = 'middle'
      context.fillText(label, point.x, point.y + 0.5)
    }
    drawMarker(projected[0], '#17201c', 'A')
    if (projected.length > 1) drawMarker(projected[projected.length - 1], '#bd4138', 'Z')
    const attribution = document.createElement('a')
    attribution.className = 'osm-attribution'
    attribution.href = 'https://www.openstreetmap.org/copyright'
    attribution.target = '_blank'
    attribution.rel = 'noreferrer'
    attribution.textContent = '© OpenStreetMap contributors'
    map.replaceChildren(...elements, canvas, attribution)
  }
  requestAnimationFrame(render)
  trackMapResizeObserver = new ResizeObserver(render)
  trackMapResizeObserver.observe(map)
}

function setTrackMapProvider(provider) {
  activeMapProvider = provider
  for (const button of document.querySelectorAll('[data-track-map-provider]')) {
    button.setAttribute('aria-pressed', String(button.dataset.trackMapProvider === provider))
  }
  byId('track-map-key-caption').textContent = trackMapKeyLabel(provider)
  byId('track-map-key').value = trackMapKeys[provider]
  byId('track-map-key-label').hidden = provider === 'osm'
  byId('draw-track-map').hidden = provider === 'osm' || !trackMapKeys[provider].trim()
  byId('show-track-plot').disabled = false
  byId('show-track-list').disabled = false
  renderTrackPlot(trackPoints)
  renderTrackList(trackPoints)
}

async function drawTrackMap() {
  const apiKey = trackMapKeys[activeMapProvider].trim()
  renderTrackPlot(trackPoints)
  if (activeMapProvider === 'osm' || !apiKey || !trackPoints.length || !trackFingerprint) return
  const shell = byId('track-plot')
  const loading = document.createElement('p')
  loading.className = 'track-empty'
  loading.textContent = `正在通过 ${mapProviderName(activeMapProvider)} 绘制最近 24 小时轨迹…`
  shell.replaceChildren(loading)
  setBusy(true)
  try {
    const result = await api('/api/tracking/map', { fingerprint: trackFingerprint, provider: activeMapProvider, apiKey })
    const frame = document.createElement('div')
    frame.className = 'track-map-frame'
    const image = document.createElement('img')
    image.src = result.imageDataUrl
    image.alt = `${mapProviderName(activeMapProvider)}地图中的最近 24 小时轨迹`
    frame.append(image)
    shell.replaceChildren(frame)
  } catch (error) {
    const failure = document.createElement('p')
    failure.className = 'track-map-error'
    failure.textContent = error.message
    shell.replaceChildren(failure)
  } finally {
    setBusy(false)
  }
}

function renderTrackList(points) {
  const list = byId('track-list')
  if (!points.length) {
    const empty = document.createElement('p')
    empty.textContent = '最近 24 小时还没有轨迹点。'
    list.replaceChildren(empty)
    return
  }
  list.replaceChildren(...points.map((point) => {
    const row = document.createElement('article')
    const copy = document.createElement('div')
    const time = document.createElement('strong')
    time.textContent = new Date(point.timestamp).toLocaleString()
    const coordinates = document.createElement('span')
    coordinates.textContent = `${Number(point.latitude).toFixed(7)}, ${Number(point.longitude).toFixed(7)} · 精度约 ${point.horizontalAccuracy} m`
    const view = document.createElement('button')
    view.type = 'button'
    view.className = 'secondary'
    view.textContent = '查看位置'
    view.addEventListener('click', () => {
      updateMap(point)
      openMap()
    })
    copy.append(time, coordinates)
    row.append(copy, view)
    return row
  }))
}

function compactBluetoothName(value) {
  return String(value || '').trim().replace(/^(?:IKT_|InkTag_)/i, '')
}

function identityName(identity) {
  return String(identity.findNote || '').trim()
    || String(identity.deviceName || '').trim()
    || compactBluetoothName(identity.bluetoothName)
    || `Find ${identity.keyFingerprint.slice(0, 4).toUpperCase()}`
}

function deviceName(items) {
  return items.find((identity) => String(identity.findNote || '').trim())?.findNote.trim()
    || items.find((identity) => String(identity.deviceName || '').trim())?.deviceName.trim()
    || compactBluetoothName(items.find((identity) => String(identity.bluetoothName || '').trim())?.bluetoothName)
    || '未命名设备'
}

function selectedDeviceName() {
  const identity = identities.find((item) => item.selected)
  if (!identity) return '未命名 InkTag'
  return String(identity.deviceName || '').trim()
    || compactBluetoothName(identity.bluetoothName)
    || String(identity.findNote || '').trim()
    || `Find ${identity.keyFingerprint.slice(0, 4).toUpperCase()}`
}

function distinctDeviceNames(items, heading) {
  const values = []
  const add = (value) => {
    const text = String(value || '').trim()
    if (!text || text.localeCompare(heading, undefined, { sensitivity: 'accent' }) === 0) return
    if (!values.some((entry) => entry.localeCompare(text, undefined, { sensitivity: 'accent' }) === 0)) values.push(text)
  }
  add(items.find((identity) => String(identity.deviceName || '').trim())?.deviceName)
  const bluetoothName = compactBluetoothName(items.find((identity) => String(identity.bluetoothName || '').trim())?.bluetoothName)
  if (bluetoothName && bluetoothName.localeCompare(heading, undefined, { sensitivity: 'accent' }) !== 0) add(`[${bluetoothName}]`)
  return values
}

function groupIdentities(items) {
  const groups = new Map()
  for (const identity of items) {
    const id = identity.deviceId || `key:${identity.keyFingerprint}`
    const group = groups.get(id) || { id, identities: [], selected: false }
    group.identities.push(identity)
    group.selected ||= identity.selected === true
    groups.set(id, group)
  }
  return [...groups.values()]
    .map((group) => ({ ...group, identities: group.identities.sort((left, right) => Date.parse(right.createdAt) - Date.parse(left.createdAt)) }))
    .sort((left, right) => Number(right.selected) - Number(left.selected) || Date.parse(right.identities[0].createdAt) - Date.parse(left.identities[0].createdAt))
}

function renderIdentityItem(identity, heading) {
  const item = document.createElement('article')
  item.className = 'identity-item'
  item.dataset.selected = String(identity.selected === true)
  const main = document.createElement('div')
  main.className = 'identity-main'
  const label = document.createElement('strong')
  const note = String(identity.findNote || '').trim()
  label.textContent = note && note.localeCompare(heading, undefined, { sensitivity: 'accent' }) !== 0
    ? note
    : identity.selected ? '当前查询身份' : `Find 身份 ${identity.keyFingerprint.slice(0, 4).toUpperCase()}`
  const address = document.createElement('span')
  address.textContent = `${identity.address} · `
  const fingerprint = document.createElement('code')
  fingerprint.textContent = identity.keyFingerprint
  address.append(fingerprint)
  main.append(label, address)
  const meta = document.createElement('div')
  meta.className = 'identity-meta'
  const queried = document.createElement('span')
  queried.textContent = `查询 ${identity.lastQueriedAt ? new Date(identity.lastQueriedAt).toLocaleString() : '尚未查询'}`
  meta.append(queried)
  const recorded = document.createElement('span')
  recorded.textContent = `最新记录 ${identity.lastReportAt ? new Date(identity.lastReportAt).toLocaleString() : '尚无记录'}`
  meta.append(recorded)
  const tracking = document.createElement('span')
  tracking.textContent = identity.trackingEnabled ? `轨迹记录中 · ${identity.trackPointCount} 点` : `轨迹 ${identity.trackPointCount} 点`
  meta.append(tracking)
  const derivation = document.createElement('span')
  derivation.textContent = identity.derivationVersion === 2
    ? `Find v2 · 每 ${Math.round(Number(identity.periodSeconds) / 60)} 分钟轮换`
    : '旧版 · 仅本地历史'
  meta.append(derivation)
  if (identity.lastAutoError) {
    const error = document.createElement('span')
    error.className = 'error-text'
    error.textContent = identity.lastAutoError
    meta.append(error)
  }
  const actions = document.createElement('div')
  actions.className = 'button-row'
  const select = document.createElement('button')
  select.type = 'button'
  select.className = identity.selected ? 'primary' : 'secondary'
  select.dataset.action = 'select-identity'
  select.dataset.fingerprint = identity.keyFingerprint
  select.disabled = identity.selected === true || identity.derivationVersion !== 2
  select.textContent = identity.selected ? '当前查询设备' : '选择查询'
  const rename = document.createElement('button')
  rename.type = 'button'
  rename.className = 'quiet-button'
  rename.dataset.action = 'edit-find-note'
  rename.dataset.fingerprint = identity.keyFingerprint
  rename.textContent = '修改备注'
  const remove = document.createElement('button')
  remove.type = 'button'
  remove.className = 'quiet-button danger-text'
  remove.dataset.action = 'delete-identity'
  remove.dataset.fingerprint = identity.keyFingerprint
  remove.title = '删除本机身份和对应轨迹'
  remove.textContent = '删除'
  const toggleTrack = document.createElement('button')
  toggleTrack.type = 'button'
  toggleTrack.className = 'secondary'
  toggleTrack.dataset.action = 'toggle-tracking'
  toggleTrack.dataset.fingerprint = identity.keyFingerprint
  toggleTrack.textContent = identity.trackingEnabled ? '停止记录' : '定期记录'
  toggleTrack.disabled = identity.derivationVersion !== 2
  const history = document.createElement('button')
  history.type = 'button'
  history.className = 'quiet-button'
  history.dataset.action = 'view-tracking'
  history.dataset.fingerprint = identity.keyFingerprint
  history.textContent = `轨迹${identity.trackPointCount ? ` ${identity.trackPointCount}` : ''}`
  actions.append(select, toggleTrack, history, rename, remove)
  const editor = document.createElement('form')
  editor.className = 'identity-note-editor'
  editor.dataset.fingerprint = identity.keyFingerprint
  editor.hidden = true
  const editorLabel = document.createElement('label')
  editorLabel.textContent = 'Find 备注'
  const editorInput = document.createElement('input')
  editorInput.name = 'findNote'
  editorInput.maxLength = 64
  editorInput.required = true
  editorInput.value = identity.findNote
  const save = document.createElement('button')
  save.type = 'submit'
  save.className = 'primary'
  save.textContent = '保存'
  const cancel = document.createElement('button')
  cancel.type = 'button'
  cancel.className = 'quiet-button'
  cancel.dataset.action = 'cancel-edit-note'
  cancel.dataset.fingerprint = identity.keyFingerprint
  cancel.textContent = '取消'
  editorLabel.append(editorInput)
  editor.append(editorLabel, save, cancel)
  const deleteConfirm = document.createElement('div')
  deleteConfirm.className = 'identity-delete-confirm'
  deleteConfirm.hidden = true
  const deleteCopy = document.createElement('span')
  deleteCopy.textContent = `删除本机派生种子和 ${identity.trackPointCount} 个轨迹点？不会修改标签中的种子。`
  const keep = document.createElement('button')
  keep.type = 'button'
  keep.className = 'secondary'
  keep.dataset.action = 'cancel-delete-identity'
  keep.dataset.fingerprint = identity.keyFingerprint
  keep.textContent = '取消'
  const confirmDelete = document.createElement('button')
  confirmDelete.type = 'button'
  confirmDelete.className = 'danger-text'
  confirmDelete.dataset.action = 'confirm-delete-identity'
  confirmDelete.dataset.fingerprint = identity.keyFingerprint
  confirmDelete.textContent = '确认删除'
  deleteConfirm.append(deleteCopy, keep, confirmDelete)
  item.append(main, meta, actions, editor, deleteConfirm)
  return item
}

function mapUrl(base, params) {
  const url = new URL(base)
  for (const [name, value] of Object.entries(params)) url.searchParams.set(name, value)
  return url.toString().replace(/\+/g, '%20')
}

function mapProviderName(provider) {
  return provider === 'baidu' ? '百度' : provider === 'amap' ? '高德' : 'OSMap'
}

function clearMap() {
  latestReport = null
  mapUrls = { baidu: '', amap: '', osm: '' }
  mapOpenUrls = { baidu: '', amap: '', osm: '' }
  byId('embedded-map').removeAttribute('src')
  byId('enlarged-map').removeAttribute('src')
  byId('map-card').hidden = true
  closeMap()
}

function reloadMapFrame(frame, url) {
  frame.removeAttribute('src')
  requestAnimationFrame(() => { frame.src = url })
}

function setMapProvider(provider, forceReload = false) {
  const url = mapUrls[provider]
  if (!url) return
  activeMapProvider = provider
  const preview = byId('embedded-map')
  if (forceReload || preview.src !== url) reloadMapFrame(preview, url)
  preview.title = `${mapProviderName(provider)}地图中的 Find 报告位置`
  if (!mapModal.hidden) {
    const enlarged = byId('enlarged-map')
    if (forceReload || enlarged.src !== url) reloadMapFrame(enlarged, url)
    enlarged.title = `${mapProviderName(provider)}地图中的放大 Find 报告位置`
  }
  for (const id of ['show-baidu-map', 'modal-baidu-map']) byId(id).setAttribute('aria-pressed', String(provider === 'baidu'))
  for (const id of ['show-amap-map', 'modal-amap-map']) byId(id).setAttribute('aria-pressed', String(provider === 'amap'))
  for (const id of ['show-osm-map', 'modal-osm-map']) byId(id).setAttribute('aria-pressed', String(provider === 'osm'))
  byId('map-info-card').hidden = false
  byId('modal-map-info-card').hidden = false
  for (const id of ['open-map-provider', 'open-modal-map-provider']) {
    byId(id).href = mapOpenUrls[provider] || url
    byId(id).textContent = `在${mapProviderName(provider)}打开`
  }
}

function updateMap(payload) {
  const latitude = Number(payload.latitude).toFixed(7)
  const longitude = Number(payload.longitude).toFixed(7)
  const accuracy = Number(payload.horizontalAccuracy)
  const reportTime = new Date(payload.timestamp).toLocaleString()
  const accuracyText = Number.isFinite(accuracy) ? `水平精度约 ${accuracy} 米` : '水平精度未知'
  const address = byId('device-address').textContent || '--'
  const mapDeviceName = selectedDeviceName()
  const markerTitle = Number.isFinite(accuracy) ? `${mapDeviceName} · 约 ${accuracy} m` : `${mapDeviceName} 位置`
  latestReport = { address, reportTime, accuracyText, coordinates: `${latitude}, ${longitude}` }
  byId('map-coordinates').textContent = latestReport.coordinates
  byId('map-explanation').textContent = Number.isFinite(accuracy)
    ? `中心点是报告坐标；实际位置存在约 ${accuracy} 米的水平不确定范围，不能据此认定具体门牌或房间。`
    : '中心点是报告坐标；本条报告没有提供可用的水平精度。'
  mapUrls.baidu = mapUrl('https://api.map.baidu.com/marker', {
    location: `${latitude},${longitude}`,
    title: mapDeviceName,
    content: `GPS坐标：${latitude},${longitude}`,
    output: 'html',
    coord_type: 'wgs84',
    zoom: '18',
    src: 'webapp.InkTag.Find',
  })
  mapUrls.amap = mapUrl('https://uri.amap.com/marker', {
    position: `${longitude},${latitude}`,
    name: mapDeviceName,
    src: 'InkTag',
    coordinate: 'wgs84',
    callnative: '0',
  })
  mapUrls.osm = mapUrl('https://www.openstreetmap.org/export/embed.html', {
    bbox: `${(Number(longitude) - 0.005).toFixed(7)},${(Number(latitude) - 0.003).toFixed(7)},${(Number(longitude) + 0.005).toFixed(7)},${(Number(latitude) + 0.003).toFixed(7)}`,
    layer: 'mapnik',
    marker: `${latitude},${longitude}`,
  })
  mapOpenUrls.baidu = mapUrls.baidu
  mapOpenUrls.amap = mapUrls.amap
  mapOpenUrls.osm = `https://www.openstreetmap.org/?mlat=${encodeURIComponent(latitude)}&mlon=${encodeURIComponent(longitude)}#map=18/${latitude}/${longitude}`
  byId('modal-map-address').textContent = address
  byId('modal-map-time').textContent = reportTime
  byId('modal-map-coordinates').textContent = latestReport.coordinates
  byId('modal-map-meta').textContent = `${reportTime} · ${accuracyText}`
  byId('modal-map-accuracy').textContent = accuracyText
  byId('map-info-title').textContent = markerTitle
  byId('modal-map-info-title').textContent = markerTitle
  byId('map-info-detail').textContent = `${address} · ${reportTime} · ${accuracyText}`
  byId('modal-map-info-detail').textContent = `${address} · ${reportTime} · ${accuracyText}`
  byId('map-card').hidden = false
  setMapProvider(activeMapProvider)
}

function openMap() {
  if (!latestReport || !mapUrls[activeMapProvider]) return
  mapModal.hidden = false
  document.body.style.overflow = 'hidden'
  setMapProvider(activeMapProvider)
  byId('close-map').focus()
}

function closeMap() {
  if (!mapModal) return
  mapModal.hidden = true
  document.body.style.removeProperty('overflow')
}

function closeTrack() {
  trackModal.hidden = true
  trackFingerprint = ''
  document.body.style.removeProperty('overflow')
}

function activeTrackIdentity() {
  return identities.find((identity) => identity.keyFingerprint === trackFingerprint) || null
}

function updateTrackControls() {
  const identity = activeTrackIdentity()
  if (!identity) return
  byId('track-dialog-title').textContent = identityName(identity)
  byId('track-interval').value = String(trackingIntervalMinutes)
  byId('toggle-track').textContent = identity.trackingEnabled ? '停止定期记录' : '启用定期记录'
  byId('toggle-track').className = identity.trackingEnabled ? 'secondary' : 'primary'
  byId('toggle-track').disabled = identity.derivationVersion !== 2
  byId('track-interval').disabled = identity.derivationVersion !== 2
}

async function loadTrackHistory(fingerprint) {
  const payload = await api('/api/tracking/history', { fingerprint, limit: 250, hours: 24 })
  const identity = identities.find((item) => item.keyFingerprint === fingerprint)
  byId('track-dialog-meta').textContent = `${payload.total} 个位置点 · 最近 ${payload.windowHours || 24} 小时 · 本机保留 ${payload.retentionDays || 90} 天`
  renderTrackPlot(payload.points)
  renderTrackList(payload.points)
  if (identity) updateTrackControls()
}

async function openTrack(identity) {
  trackFingerprint = identity.keyFingerprint
  trackModal.hidden = false
  document.body.style.overflow = 'hidden'
  setTrackView('plot')
  setTrackMapProvider(activeMapProvider)
  updateTrackControls()
  byId('track-plot').textContent = '正在读取历史轨迹…'
  byId('track-list').textContent = '正在读取轨迹…'
  await loadTrackHistory(identity.keyFingerprint)
  byId('close-track').focus()
}

async function api(path, body) {
  const options = body === undefined
    ? { cache: 'no-store', credentials: 'same-origin' }
    : {
        method: 'POST',
        cache: 'no-store',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json', 'X-InkTag-CSRF': csrfToken },
        body: JSON.stringify(body),
      }
  const response = await fetch(path, options)
  const payload = await response.json()
  if (!response.ok || payload.ok === false) throw new Error(payload.error || `请求失败（HTTP ${response.status}）`)
  return payload
}

function updateState(payload) {
  if (payload.loggedIn) byId('password').value = ''
  csrfToken = payload.csrfToken || csrfToken
  byId('device-address').textContent = payload.address || '--'
  byId('key-fingerprint').textContent = payload.keyFingerprint || '--'
  byId('apple-account-username').textContent = payload.appleAccountUsername || '--'
  if (payload.appleAccountUsername && !byId('email').value) byId('email').value = payload.appleAccountUsername
  loginPanel.hidden = payload.loggedIn || payload.needs2fa
  twoFactorPanel.hidden = !payload.needs2fa
  queryPanel.hidden = !payload.loggedIn
  byId('session-state').textContent = payload.loggedIn
    ? !payload.queryReady ? '账号已验证 · 未取得查询权限' : payload.sessionRestored ? '已加载查询会话' : '已取得查询权限'
    : payload.needs2fa ? '账号已验证 · 等待 2FA' : '未登录'
  identities = Array.isArray(payload.identities) ? payload.identities : []
  byId('query-warning').hidden = !payload.queryError
  byId('query-warning').textContent = payload.queryError || ''
  const querySupported = payload.queryReady && identities.some((identity) => identity.selected && identity.derivationVersion === 2)
  setQueryDisabled(byId('update-track'), !querySupported)
  setQueryDisabled(byId('query-report'), !querySupported)
  trackingIntervalMinutes = Number(payload.trackingIntervalMinutes) || trackingIntervalMinutes
  const list = byId('identity-list')
  list.replaceChildren(...groupIdentities(identities).map((group, index) => {
    const device = document.createElement('details')
    device.className = 'identity-device-group'
    device.open = group.selected || index === 0
    const summary = document.createElement('summary')
    const title = document.createElement('strong')
    const heading = deviceName(group.identities)
    title.textContent = heading
    const meta = document.createElement('span')
    meta.textContent = `${group.identities.length} 个 Find 身份${distinctDeviceNames(group.identities, heading).map((value) => ` · ${value}`).join('')}`
    summary.append(title, meta)
    const entries = document.createElement('div')
    entries.className = 'identity-device-entries'
    const deviceActions = document.createElement('div')
    deviceActions.className = 'identity-device-actions'
    const queryWindow = document.createElement('button')
    queryWindow.type = 'button'
    queryWindow.className = 'secondary identity-device-query-all'
    queryWindow.dataset.action = 'query-derived-window'
    queryWindow.dataset.fingerprint = group.identities[0].keyFingerprint
    queryWindow.textContent = '查询近期报告'
    queryWindow.disabled = !payload.queryReady || group.identities[0].derivationVersion !== 2
    deviceActions.append(queryWindow)
    entries.append(deviceActions)
    entries.append(...group.identities.map((identity) => renderIdentityItem(identity, heading)))
    device.append(summary, entries)
    return device
  }))
  if (payload.needs2fa) {
    byId('two-factor-warning').hidden = !payload.secondFactorWarning
    byId('two-factor-warning').textContent = payload.secondFactorWarning || ''
    const select = byId('two-factor-method')
    const previousValue = select.value
    select.replaceChildren(...payload.methods.map((method) => {
      const option = document.createElement('option')
      option.value = String(method.index)
      option.textContent = method.label
      return option
    }))
    const preferredValue = payload.activeMethodIndex === null || payload.activeMethodIndex === undefined
      ? previousValue
      : String(payload.activeMethodIndex)
    if ([...select.options].some((option) => option.value === preferredValue)) select.value = preferredValue
  }
  if (!trackModal.hidden) updateTrackControls()
}

byId('login-form').addEventListener('submit', async (event) => {
  event.preventDefault()
  setBusy(true)
  showMessage('正在建立本地 Anisette 会话并登录…')
  const passwordInput = byId('password')
  const body = { email: byId('email').value.trim(), password: passwordInput.value, consent: byId('consent').checked }
  try {
    const payload = await api('/api/login', body)
    body.password = ''
    updateState(payload)
    showMessage(payload.queryError || (payload.needs2fa ? '账号密码验证通过，请继续完成双重认证。' : '已取得 Find 查询权限。'), payload.queryError ? 'error' : 'success')
  } catch (error) {
    body.password = ''
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
})

byId('request-code').addEventListener('click', async () => {
  const select = byId('two-factor-method')
  const index = Number(select.value)
  const methodLabel = select.selectedOptions[0]?.textContent || '所选方式'
  setBusy(true)
  showMessage(`正在通过“${methodLabel}”请求验证码…`)
  try {
    const payload = await api('/api/2fa/request', { index })
    updateState(payload)
    showMessage(`验证码已通过“${methodLabel}”请求，请在对应设备上查看。`, 'success')
  } catch (error) {
    showMessage(`账号密码已验证；“${methodLabel}”发送验证码失败：${error.message}`, 'error')
  } finally {
    setBusy(false)
  }
})

byId('code-form').addEventListener('submit', async (event) => {
  event.preventDefault()
  const select = byId('two-factor-method')
  const index = Number(select.value)
  const methodLabel = select.selectedOptions[0]?.textContent || '所选方式'
  setBusy(true)
  showMessage(`正在按“${methodLabel}”验证…`)
  const codeInput = byId('code')
  const code = codeInput.value.trim()
  codeInput.value = ''
  try {
    const payload = await api('/api/2fa/submit', { index, code })
    updateState(payload)
    showMessage(payload.queryError || '双重认证完成，已取得 Find 查询权限。', payload.queryReady ? 'success' : 'error')
  } catch (error) {
    try { updateState(await api('/api/status')) } catch { /* Keep the original operation error. */ }
    showMessage(`账号密码已验证；“${methodLabel}”提交验证码失败：${error.message}`, 'error')
  } finally {
    setBusy(false)
  }
})

byId('query-report').addEventListener('click', async () => {
  setBusy(true)
  showMessage('正在查询并在本机解密最新报告…')
  try {
    const payload = await api('/api/query', {})
    const report = byId('report')
    report.hidden = !payload.found
    if (!payload.found) {
      latestCoordinates = ''
      clearMap()
      showMessage('尚未查到这把 Find 广播密钥的报告。可让 iPhone 继续靠近一段时间后重试。')
      return
    }
    latestCoordinates = `${payload.latitude}, ${payload.longitude}`
    byId('report-time').textContent = new Date(payload.timestamp).toLocaleString()
    byId('report-age').textContent = formatAge(payload.timestamp)
    byId('report-latitude').textContent = Number(payload.latitude).toFixed(7)
    byId('report-longitude').textContent = Number(payload.longitude).toFixed(7)
    byId('report-accuracy').textContent = `${payload.horizontalAccuracy} m`
    byId('report-detail').textContent = `${payload.confidence} / 0x${Number(payload.status).toString(16).padStart(2, '0')}`
    updateMap(payload)
    showMessage('已查询并解密最新报告。', 'success')
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    try { updateState(await api('/api/status')) } catch (_error) { /* Keep the query result visible. */ }
    setBusy(false)
  }
})

byId('update-track').addEventListener('click', async () => {
  const identity = identities.find((item) => item.selected)
  if (!identity) return
  setBusy(true)
  showMessage('正在查询最新位置，并与本机历史轨迹去重合并…')
  let queryError = ''
  try {
    const result = await api('/api/query', { fingerprint: identity.keyFingerprint, saveHistory: true })
    if (!result.found) queryError = '本次没有查到新的位置报告。'
  } catch (error) {
    queryError = `最新位置查询失败：${error.message}`
  }
  try {
    await openTrack(identity)
    showMessage(queryError ? `${queryError} 已打开本机保存的历史轨迹。` : '最新位置已去重写入，并与本机历史轨迹一起显示。', queryError ? 'error' : 'success')
  } catch (error) {
    closeTrack()
    showMessage(queryError || error.message, 'error')
  } finally {
    try { updateState(await api('/api/status')) } catch (_error) { /* The history view remains usable. */ }
    setBusy(false)
  }
})

for (const [id, provider] of [['show-baidu-map', 'baidu'], ['modal-baidu-map', 'baidu'], ['show-amap-map', 'amap'], ['modal-amap-map', 'amap'], ['show-osm-map', 'osm'], ['modal-osm-map', 'osm']]) {
  byId(id).addEventListener('click', () => setMapProvider(provider, true))
}
for (const button of document.querySelectorAll('[data-track-map-provider]')) {
  button.addEventListener('click', () => setTrackMapProvider(button.dataset.trackMapProvider))
}
byId('track-map-key').addEventListener('input', (event) => {
  trackMapKeys[activeMapProvider] = event.target.value
  byId('draw-track-map').hidden = !event.target.value.trim()
  renderTrackPlot(trackPoints)
  renderTrackList(trackPoints)
})
byId('draw-track-map').addEventListener('click', () => { void drawTrackMap() })
byId('reload-map').addEventListener('click', () => setMapProvider(activeMapProvider, true))
byId('reload-modal-map').addEventListener('click', () => setMapProvider(activeMapProvider, true))
byId('open-map').addEventListener('click', openMap)
byId('close-map').addEventListener('click', closeMap)
mapModal.addEventListener('mousedown', (event) => { if (event.target === mapModal) closeMap() })
byId('close-track').addEventListener('click', closeTrack)
trackModal.addEventListener('mousedown', (event) => { if (event.target === trackModal) closeTrack() })
document.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape') return
  if (!pageConfirmModal.hidden) {
    closePageConfirm()
    return
  }
  if (!mapModal.hidden) closeMap()
  if (!trackModal.hidden) closeTrack()
})

byId('copy-coordinates').addEventListener('click', async () => {
  if (!latestCoordinates) return
  try {
    await navigator.clipboard.writeText(latestCoordinates)
    showMessage('坐标已复制。', 'success')
  } catch (_error) {
    showMessage('浏览器未允许复制，请手动复制页面中的坐标。', 'error')
  }
})

byId('identity-list').addEventListener('submit', async (event) => {
  const form = event.target.closest?.('form.identity-note-editor')
  if (!form) return
  event.preventDefault()
  const fingerprint = form.dataset.fingerprint
  const identity = identities.find((item) => item.keyFingerprint === fingerprint)
  const findNote = form.elements.findNote.value.trim()
  if (!identity || !findNote) return
  if (findNote === identity.findNote) {
    form.hidden = true
    return
  }
  setBusy(true)
  try {
    updateState(await api('/api/identity/update', { fingerprint, findNote }))
    showMessage(`Find 备注已更新为“${findNote}”。`, 'success')
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
})

byId('identity-list').addEventListener('click', async (event) => {
  if (!(event.target instanceof Element)) return
  const button = event.target.closest('button[data-action]')
  if (!button) return
  const fingerprint = button.dataset.fingerprint
  const identity = identities.find((item) => item.keyFingerprint === fingerprint)
  if (!identity) return
  if (button.dataset.action === 'query-derived-window') {
    const label = identityName(identity)
    setBusy(true)
    showMessage(`正在查询“${label}”的近期位置，查询范围较大，预计需要约半分钟，请勿关闭页面。`)
    try {
      const result = await api('/api/query', { fingerprint, saveHistory: true, queryWindow: 'recent' })
      updateState(await api('/api/status'))
      showMessage(result.found
        ? `查询完成：派生 ${result.derivedSlotCount} 个时间片，新增 ${Number(result.savedPointCount || 0)} 个去重轨迹点。`
        : `查询完成：派生 ${result.derivedSlotCount} 个时间片，暂未返回位置报告。`, result.found ? 'success' : undefined)
    } catch (error) {
      showMessage(error.message, 'error')
    } finally {
      setBusy(false)
    }
    return
  }
  if (button.dataset.action === 'edit-find-note') {
    const item = button.closest('.identity-item')
    const editor = item?.querySelector('.identity-note-editor')
    if (!editor) return
    editor.hidden = false
    editor.elements.findNote.focus()
    editor.elements.findNote.select()
    return
  }
  if (button.dataset.action === 'cancel-edit-note') {
    const editor = button.closest('.identity-note-editor')
    if (editor) editor.hidden = true
    return
  }
  if (button.dataset.action === 'delete-identity') {
    const confirmation = button.closest('.identity-item')?.querySelector('.identity-delete-confirm')
    if (confirmation) confirmation.hidden = false
    return
  }
  if (button.dataset.action === 'cancel-delete-identity') {
    const confirmation = button.closest('.identity-delete-confirm')
    if (confirmation) confirmation.hidden = true
    return
  }
  if (button.dataset.action === 'confirm-delete-identity') {
    setBusy(true)
    try {
      const wasSelected = identity.selected
      const payload = await api('/api/identity/delete', { fingerprint, confirm: true })
      updateState(payload)
      byId('report').hidden = true
      latestCoordinates = ''
      clearMap()
      showMessage(wasSelected && payload.identities.length > 0 ? '本机身份及轨迹已删除，并已选择另一个保留身份。' : '本机身份及轨迹已删除。', 'success')
    } catch (error) {
      showMessage(error.message, 'error')
    } finally {
      setBusy(false)
    }
    return
  }
  if (button.dataset.action === 'view-tracking') {
    setBusy(true)
    try {
      await openTrack(identity)
    } catch (error) {
      closeTrack()
      showMessage(error.message, 'error')
    } finally {
      setBusy(false)
    }
    return
  }
  if (button.dataset.action === 'toggle-tracking') {
    const enabled = !identity.trackingEnabled
    if (enabled && !await openPageConfirm({
      title: '启用定期轨迹记录',
      message: `将为“${identityName(identity)}”启用本机轨迹记录。\n\n服务运行且 Apple 会话有效时，每 ${trackingIntervalMinutes} 分钟查询一次；坐标保存在本机 SQLite 数据库中。`,
      confirmLabel: '启用记录',
    })) return
    setBusy(true)
    try {
      const payload = await api('/api/tracking/configure', { fingerprint, enabled, intervalMinutes: trackingIntervalMinutes })
      updateState(payload)
      showMessage(enabled ? `已启用轨迹记录，每 ${trackingIntervalMinutes} 分钟查询一次。` : '已停止定期查询；已有轨迹仍保留在本机。', 'success')
    } catch (error) {
      showMessage(error.message, 'error')
    } finally {
      setBusy(false)
    }
    return
  }
  setBusy(true)
  try {
    const payload = await api('/api/identity/select', { fingerprint })
    updateState(payload)
    byId('report').hidden = true
    latestCoordinates = ''
    clearMap()
    showMessage(`已选择“${identity.findNote || identity.deviceName || identity.bluetoothName || fingerprint}”，现在可以查询该标签。`, 'success')
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
})

byId('toggle-track').addEventListener('click', async () => {
  const identity = activeTrackIdentity()
  if (!identity) return
  const enabled = !identity.trackingEnabled
  if (enabled && !await openPageConfirm({
    title: '启用定期轨迹记录',
    message: `将为“${identityName(identity)}”启用本机轨迹记录。\n\n坐标保存在本机 SQLite 数据库中。`,
    confirmLabel: '启用记录',
  })) return
  setBusy(true)
  try {
    updateState(await api('/api/tracking/configure', { fingerprint: identity.keyFingerprint, enabled, intervalMinutes: trackingIntervalMinutes }))
    await loadTrackHistory(identity.keyFingerprint)
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
})

byId('show-track-plot').addEventListener('click', () => setTrackView('plot'))
byId('show-track-list').addEventListener('click', () => setTrackView('list'))

byId('track-interval').addEventListener('change', async (event) => {
  const identity = activeTrackIdentity()
  if (!identity) return
  const intervalMinutes = Number(event.target.value)
  setBusy(true)
  try {
    updateState(await api('/api/tracking/configure', { fingerprint: identity.keyFingerprint, enabled: identity.trackingEnabled, intervalMinutes }))
    showMessage(`所有已启用身份的定期查询间隔已改为 ${intervalMinutes} 分钟。`, 'success')
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
})

byId('clear-track').addEventListener('click', async () => {
  const identity = activeTrackIdentity()
  if (!identity || !await openPageConfirm({
    title: '清除历史轨迹',
    message: `将清除“${identityName(identity)}”保存在本机的全部轨迹点。此操作无法撤销。`,
    confirmLabel: '清除轨迹',
    danger: true,
  })) return
  setBusy(true)
  try {
    updateState(await api('/api/tracking/clear', { fingerprint: identity.keyFingerprint, confirm: true }))
    await loadTrackHistory(identity.keyFingerprint)
    showMessage('本机轨迹记录已清除。', 'success')
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
})

async function logout(messageText) {
  setBusy(true)
  try {
    const payload = await api('/api/logout', {})
    updateState(payload)
    byId('report').hidden = true
    latestCoordinates = ''
    clearMap()
    showMessage(messageText, 'success')
  } catch (error) {
    showMessage(error.message, 'error')
  } finally {
    setBusy(false)
  }
}

byId('logout').addEventListener('click', () => void logout('本机保存的 Apple 会话已清除；账号名仍保存在本机，密码从不保存。'))
byId('restart-login').addEventListener('click', () => void logout('已清除当前登录流程，可以更换账号。'))

api('/api/status').then((payload) => {
  updateState(payload)
  if (payload.sessionError) showMessage(payload.sessionError, 'error')
  else if (payload.loggedIn) showMessage(payload.queryError || (payload.sessionRestored ? '已加载保存的 Apple 会话，查询时将验证是否仍有效。' : '已取得 Find 查询权限。'), payload.queryError ? 'error' : 'info')
  else if (payload.needs2fa) showMessage('账号密码验证通过，请继续完成双重认证。', 'success')
}).catch((error) => showMessage(error.message, 'error'))
