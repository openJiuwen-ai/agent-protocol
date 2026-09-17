import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    host: true,
    proxy: {
      '/api': 'http://127.0.0.1:8000',
      '/api/search/ws': {
        target: 'ws://127.0.0.1:8000',
        ws: true,
      },
    },
  },
})
