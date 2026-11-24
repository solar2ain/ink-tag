import { defineConfig } from '@playwright/test'

export default defineConfig({
  testDir: './e2e',
  outputDir: '../.local/test-results/playwright',
  timeout: 30_000,
  fullyParallel: true,
  forbidOnly: Boolean(process.env.CI),
  retries: process.env.CI ? 1 : 0,
  reporter: [['line'], ['json', { outputFile: '../.local/test-results/playwright-report.json' }]],
  use: {
    baseURL: 'http://127.0.0.1:43127',
    browserName: 'chromium',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: 'npx vite preview --mode static --host 127.0.0.1 --port 43127 --strictPort',
    url: 'http://127.0.0.1:43127',
    reuseExistingServer: false,
    timeout: 30_000,
  },
})
