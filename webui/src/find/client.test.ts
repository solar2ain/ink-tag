import { afterEach, describe, expect, it, vi } from 'vitest'
import { findClient } from './client'

afterEach(() => vi.unstubAllGlobals())

describe('Find owner-action client', () => {
  it('keeps authenticated-without-query-permission distinct from query ready', async () => {
    const state = { ok: true, loggedIn: true, queryReady: false,
      queryError: '未签发 Find 查询令牌', needs2fa: false, identities: [] }
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => state })
    vi.stubGlobal('fetch', fetchMock)
    const result = await findClient.status()
    expect(result.loggedIn).toBe(true)
    expect(result.queryReady).toBe(false)
    expect(result.queryError).toBe(state.queryError)
    expect(fetchMock).toHaveBeenCalledOnce()
  })
  it('preserves SMS choices alongside a trusted-device challenge', async () => {
    const state = { ok: true, loggedIn: false, needs2fa: true, activeMethodIndex: null,
      secondFactorWarning: null, methods: [
        { index: 0, kind: 'trusted_device', label: '受信任的 Apple 设备' },
        { index: 1, kind: 'sms', label: '短信 fixture-number' },
      ] }
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => state })
    vi.stubGlobal('fetch', fetchMock)
    expect((await findClient.login('fixture@example.test', 'fixture-password', true)).methods).toEqual(state.methods)
    expect(fetchMock).toHaveBeenCalledOnce()
  })

  it('does not replay a code submission when its result is unconfirmed', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: false, status: 502,
      json: async () => ({ ok: false, error: '验证短信验证码超时，操作结果尚未确认。' }) })
    vi.stubGlobal('fetch', fetchMock)
    await expect(findClient.submitSecondFactor(1, '123456')).rejects.toThrow('结果尚未确认')
    expect(fetchMock).toHaveBeenCalledOnce()
  })

  it('reads expired-session status without querying or changing an identity', async () => {
    const state = { ok: true, loggedIn: false, needs2fa: false, sessionRestored: false, sessionError: '请重新登录', identities: [] }
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => state })
    vi.stubGlobal('fetch', fetchMock)
    expect(await findClient.status()).toEqual(state)
    expect(fetchMock).toHaveBeenCalledOnce()
    expect(fetchMock).toHaveBeenCalledWith('api/v1/find/status', expect.objectContaining({ cache: 'no-store' }))
    expect(fetchMock.mock.calls[0][1].method).toBeUndefined() // fetch defaults to GET.
    expect(fetchMock.mock.calls[0][1].body).toBeUndefined()
  })
  it('posts a typed proof request to the canonical Find service', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, proofHex: 'ab'.repeat(32), fingerprintHex: 'cd'.repeat(8) }),
    })
    vi.stubGlobal('fetch', fetchMock)

    const result = await findClient.proveOwnerAction('fingerprint', '00112233', 'aabb', 'ccdd')

    expect(result.proofHex).toBe('ab'.repeat(32))
    expect(fetchMock).toHaveBeenCalledWith('api/v1/find/identity/owner-action/prove', expect.objectContaining({
      method: 'POST',
      body: JSON.stringify({ fingerprint: 'fingerprint', deviceIdHex: '00112233', nonceHex: 'aabb', actionHashHex: 'ccdd' }),
    }))
  })
})
