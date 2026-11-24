// Object URLs also work in embedded browsers that do not download data: links.
export function downloadBlob(blob: Blob, name: string) {
  const url = URL.createObjectURL(blob), anchor = document.createElement('a')
  anchor.href = url
  anchor.download = name.replace(/[\\/:*?"<>|]/g, '_')
  anchor.hidden = true
  document.body.append(anchor)
  try { anchor.click() } finally {
    anchor.remove()
    // Let the browser consume the URL before releasing it (including large exports).
    setTimeout(() => URL.revokeObjectURL(url), 60_000)
  }
}

export async function downloadCanvasPng(canvas: HTMLCanvasElement, name: string) {
  const blob = await new Promise<Blob>((resolve, reject) => {
    canvas.toBlob(value => value ? resolve(value) : reject(new Error('无法生成 PNG 图片，请重试。')), 'image/png')
  })
  downloadBlob(blob, name)
}
