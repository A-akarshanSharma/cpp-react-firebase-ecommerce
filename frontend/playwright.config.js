import { defineConfig } from '@playwright/test'
export default defineConfig({
  testDir: './tests',
  testMatch: '**/*.spec.js',
  fullyParallel: true,
  use: { baseURL: 'http://127.0.0.1:5174', trace: 'retain-on-failure' },
  webServer: {
    command: 'npm run dev -- --host 127.0.0.1 --port 5174 --strictPort',
    url: 'http://127.0.0.1:5174',
    reuseExistingServer: false,
    env: {
      VITE_API_URL: 'http://127.0.0.1:8089',
      VITE_PREVIEW_MODE: 'true',
      VITE_FIREBASE_API_KEY: 'test-web-key',
      VITE_FIREBASE_AUTH_DOMAIN: 'studio-test.firebaseapp.com',
      VITE_FIREBASE_PROJECT_ID: 'studio-test',
      VITE_FIREBASE_APP_ID: 'test-app',
    },
  },
})
