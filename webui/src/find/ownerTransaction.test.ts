import { describe, expect, it, vi } from 'vitest'
import { runFindOwnerTransaction } from './ownerTransaction'

describe('Find owner commit boundary', () => {
  it.each(['prepare', 'stage', 'commit', 'proof', 'finalize'])('retains pending private material after %s failure', async (failure) => {
    const error = new Error(failure)
    const prepared = { transactionId: 'pending', fingerprintHex: 'NEW' }
    const proof = { fingerprintHex: failure === 'proof' ? 'OLD' : 'NEW' }
    const steps = {
      prepare: vi.fn(async () => {
        if (failure === 'prepare') throw error
        return prepared
      }),
      stage: vi.fn(async () => { if (failure === 'stage') throw error }),
      commit: vi.fn(async () => {
        if (failure === 'commit') throw error
        return proof
      }),
      finalize: vi.fn(async (owner, result) => {
        if (owner.fingerprintHex !== result.fingerprintHex || failure === 'finalize') throw error
        return 'verified'
      }),
      abortDevice: vi.fn().mockResolvedValue(undefined),
      abortLocal: vi.fn().mockResolvedValue(undefined),
    }
    await expect(runFindOwnerTransaction(steps)).rejects.toBe(error)
    const precommit = failure === 'prepare' || failure === 'stage'
    expect(steps.abortDevice).toHaveBeenCalledTimes(precommit ? 1 : 0)
    expect(steps.abortLocal).toHaveBeenCalledTimes(failure === 'stage' ? 1 : 0)
    expect(steps.commit).toHaveBeenCalledTimes(precommit ? 0 : 1)
    if (failure === 'stage') expect(steps.abortLocal).toHaveBeenCalledWith(prepared)
  })

  it('ignores failed pre-commit abort acknowledgements without replacing the original error', async () => {
    const original = new Error('stage')
    const abort = new Error('abort')
    const commit = vi.fn()
    await expect(runFindOwnerTransaction({
      prepare: async () => 'prepared',
      stage: async () => { throw original },
      commit, finalize: vi.fn(),
      abortDevice: async () => { throw abort },
      abortLocal: async () => { throw abort },
    })).rejects.toBe(original)
    expect(commit).not.toHaveBeenCalled()
  })

  it.each([false, true])('recovers only against the prepared owner after an uncertain commit: %s', async (matches) => {
    const prepared = { fingerprintHex: 'NEW' }
    const lost = new Error('reply lost')
    const recover = vi.fn(async (owner) => {
      expect(owner).toBe(prepared)
      return matches ? 'verified-current-owner' : undefined
    })
    const abortDevice = vi.fn(), abortLocal = vi.fn()
    const result = runFindOwnerTransaction({
      prepare: async () => prepared, stage: async () => undefined,
      commit: async () => { throw lost }, finalize: vi.fn(),
      abortDevice, abortLocal, recoverAfterCommit: recover,
    })
    if (matches) await expect(result).resolves.toBe('verified-current-owner')
    else await expect(result).rejects.toBe(lost)
    expect(recover).toHaveBeenCalledOnce()
    expect(abortDevice).not.toHaveBeenCalled()
    expect(abortLocal).not.toHaveBeenCalled()
  })

  it('does not start recovery after a prepare failure', async () => {
    const recover = vi.fn()
    await expect(runFindOwnerTransaction({
      prepare: async () => { throw new Error('offline') },
      stage: vi.fn(), commit: vi.fn(), finalize: vi.fn(),
      abortDevice: vi.fn().mockResolvedValue(undefined), abortLocal: vi.fn(),
      recoverAfterCommit: recover,
    })).rejects.toThrow('offline')
    expect(recover).not.toHaveBeenCalled()
  })

  it('completes one prepare/stage/commit/finalize without abort', async () => {
    const calls: string[] = []
    const result = await runFindOwnerTransaction({
      prepare: async () => { calls.push('prepare'); return 'prepared' },
      stage: async (prepared) => { expect(prepared).toBe('prepared'); calls.push('stage') },
      commit: async () => { calls.push('commit'); return 'proof' },
      finalize: async (prepared, proof) => {
        expect([prepared, proof]).toEqual(['prepared', 'proof'])
        calls.push('finalize'); return 'verified'
      },
      abortDevice: vi.fn(), abortLocal: vi.fn(),
    })
    expect(result).toBe('verified')
    expect(calls).toEqual(['prepare', 'stage', 'commit', 'finalize'])
  })
})
