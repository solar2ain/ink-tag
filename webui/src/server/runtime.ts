import { create } from 'zustand'

export type RuntimeMode = 'checking' | 'server' | 'static' | 'standalone'

export const RUNTIME_MODE_LABELS: Record<RuntimeMode, string> = {
  checking: '启动',
  server: '服务',
  static: '静态',
  standalone: '文件',
}

export interface ServerCapabilities {
  apiVersion: number
  serverVersion: string
  features: string[]
  collections: string[]
  limits: { jsonBytes: number; assetBytes: number }
}

type RuntimeFetch = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>

interface RuntimeState {
  mode: RuntimeMode
  capabilities: ServerCapabilities | null
  detail: string
}

const standalone = typeof window !== 'undefined' && window.location.protocol === 'file:'

export const useRuntimeStore = create<RuntimeState>(() => ({
  mode: standalone ? 'standalone' : 'checking',
  capabilities: null,
  detail: standalone ? '本地单文件运行' : '正在检测本地服务',
}))

export async function detectRuntime(href: string, fetcher: RuntimeFetch = fetch): Promise<RuntimeState> {
  const location = new URL(href)
  if (location.protocol === 'file:') {
    return { mode: 'standalone', capabilities: null, detail: '本地单文件运行' }
  }

  const endpoint = new URL('api/v1/capabilities', location)
  endpoint.hash = ''
  const controller = new AbortController()
  const timeout = globalThis.setTimeout(() => controller.abort(), 1500)
  try {
    const response = await fetcher(endpoint, {
      headers: { Accept: 'application/json' },
      cache: 'no-store',
      signal: controller.signal,
    })
    if (!response.ok || !response.headers.get('content-type')?.includes('application/json')) {
      throw new Error('capability endpoint unavailable')
    }
    const capabilities = await response.json() as ServerCapabilities
    if (capabilities.apiVersion !== 1 || !Array.isArray(capabilities.features)) {
      throw new Error('unsupported capability response')
    }
    return {
      mode: 'server',
      capabilities,
      detail: `本地服务 ${capabilities.serverVersion}`,
    }
  } catch {
    return { mode: 'static', capabilities: null, detail: '浏览器本地存储' }
  } finally {
    globalThis.clearTimeout(timeout)
  }
}

let probe: Promise<void> | null = null

export function probeRuntime() {
  if (useRuntimeStore.getState().mode === 'standalone') return Promise.resolve()
  if (!probe) {
    probe = detectRuntime(window.location.href)
      .then((state) => useRuntimeStore.setState(state))
      .finally(() => { probe = null })
  }
  return probe
}
