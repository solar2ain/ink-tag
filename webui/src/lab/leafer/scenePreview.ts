import * as engine from 'leafer-editor'
import { EditorCore, type Document } from '../../../../packages/inktag-editor-core/src/index'
import { createEditionBadgeFrame, editionBadgeGeometry } from '../../device-gui/editionBadgeBitmap'
import { mountScene } from './adapter'
import { nativeRaster } from './nativeBridge'
import { resolveLabText } from './textBridge'
import { richTextRaster } from './richTextRenderer'
import { templateDocument } from './templateLibrary'
import { CanvasImageCache } from './imageRaster'

/** Thumbnails use the editor's document and renderer, including the device-mark placeholder. */
export async function renderTemplatePreview(id: string, width: number, height: number, color?: Document['colorMode']) {
  const doc = templateDocument(id, width, height)
  if (color !== undefined) doc.colorMode = color
  const canvas = new engine.Leafer({ view: document.createElement('canvas'), width, height, pixelRatio: 1,
    move: { disabled: true }, zoom: { disabled: true } })
  const scene = mountScene(engine, canvas, new EditorCore(doc), () => {}, 1, nativeRaster, resolveLabText, richTextRaster,
    (w, h) => ({ ...editionBadgeGeometry(w, h, 'base'), ...createEditionBadgeFrame('base', 2, w, h) }), true,new CanvasImageCache())
  try { return await scene.exportPng(true) }
  finally { scene.destroy(); canvas.destroy() }
}
