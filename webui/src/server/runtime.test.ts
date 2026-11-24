import { describe, expect, it } from 'vitest'
import { detectRuntime, RUNTIME_MODE_LABELS } from './runtime'

function jsonResponse(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'content-type': 'application/json' },
  })
}

describe('deployment runtime detection', () => {
  it('uses two-character labels for every menu runtime mode', () => {
    expect(RUNTIME_MODE_LABELS).toEqual({
      checking: '启动',
      server: '服务',
      static: '静态',
      standalone: '文件',
    })
    expect(Object.values(RUNTIME_MODE_LABELS).every((label) => [...label].length === 2)).toBe(true)
  })

  it('does not make network requests from the standalone file build', async () => {
    let requests = 0
    const state = await detectRuntime('file:///tmp/InkTag-WebUI.html', async () => {
      requests += 1
      return jsonResponse({})
    })
    expect(state.mode).toBe('standalone')
    expect(requests).toBe(0)
  })

  it('recognizes the same-origin local server', async () => {
    const state = await detectRuntime('https://inktag.local:8443/#/device', async (input) => {
      expect(String(input)).toBe('https://inktag.local:8443/api/v1/capabilities')
      return jsonResponse({
        apiVersion: 1,
        serverVersion: '1.0.0',
        features: ['records'],
        collections: ['pages'],
        limits: { jsonBytes: 1, assetBytes: 1 },
      })
    })
    expect(state.mode).toBe('server')
    expect(state.capabilities?.serverVersion).toBe('1.0.0')
  })

  it('falls back to browser storage on a static host', async () => {
    const state = await detectRuntime('https://example.test/ink-tag/#/', async () => new Response('<html />', {
      status: 200,
      headers: { 'content-type': 'text/html' },
    }))
    expect(state.mode).toBe('static')
    expect(state.capabilities).toBeNull()
  })
})
