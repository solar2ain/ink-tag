// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest'

beforeEach(() => { sessionStorage.clear(); vi.resetModules() })
afterEach(() => vi.restoreAllMocks())

it('announces once across navigation and a reload in the same tab', async () => {
  const first = await import('./sessionNotice')
  expect(first.claimRestoredSessionNotice()).toBe(true)
  expect(first.claimRestoredSessionNotice()).toBe(false)
  vi.resetModules()
  expect((await import('./sessionNotice')).claimRestoredSessionNotice()).toBe(false)
})

it('announces again in a new tab session and stores no account data', async () => {
  const { claimRestoredSessionNotice } = await import('./sessionNotice')
  claimRestoredSessionNotice()
  expect(sessionStorage.length).toBe(1)
  expect(sessionStorage.getItem(sessionStorage.key(0)!)).toBe('1')
  sessionStorage.clear(); vi.resetModules()
  expect((await import('./sessionNotice')).claimRestoredSessionNotice()).toBe(true)
})

it('still suppresses repeated notices if browser storage is unavailable', async () => {
  vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => { throw Error('blocked') })
  const { claimRestoredSessionNotice } = await import('./sessionNotice')
  expect(claimRestoredSessionNotice()).toBe(true)
  expect(claimRestoredSessionNotice()).toBe(false)
})
