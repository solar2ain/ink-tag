import { create } from 'zustand'
import type { FirmwareUpdateInfo } from '../protocol/firmwareUpdate'

export type FirmwareUpdateStatus = 'idle' | 'checking' | 'available' | 'current' | 'not-published' | 'unavailable'

interface FirmwareUpdateState {
  status: FirmwareUpdateStatus
  update: FirmwareUpdateInfo | null
  latestVersion: string
  error: string
  requestId: number
  setChecking: () => void
  setResult: (status: 'available' | 'current' | 'not-published', latestVersion: string, update: FirmwareUpdateInfo | null) => void
  setUnavailable: (error: string) => void
  clear: () => void
  requestCheck: () => void
}

export const useFirmwareUpdateStore = create<FirmwareUpdateState>((set) => ({
  status: 'idle',
  update: null,
  latestVersion: '',
  error: '',
  requestId: 0,
  setChecking: () => set({ status: 'checking', update: null, latestVersion: '', error: '' }),
  setResult: (status, latestVersion, update) => set({ status, latestVersion, update, error: '' }),
  setUnavailable: (error) => set({ status: 'unavailable', update: null, latestVersion: '', error }),
  clear: () => set({ status: 'idle', update: null, latestVersion: '', error: '' }),
  requestCheck: () => set((state) => ({ requestId: state.requestId + 1 })),
}))
