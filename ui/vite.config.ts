import { defineConfig, type Plugin } from 'vite';

// The WebView loads file://.../index.html, where <script type="module"> is blocked as cross-origin.
// Emit one classic inline script + inline CSS in a single index.html.
function inlineAssets(): Plugin {
  return {
    name: 'puls-inline-assets',
    enforce: 'post',
    generateBundle(_opts, bundle) {
      const html = bundle['index.html'];
      if (!html || html.type !== 'asset') return;
      let src = String(html.source);
      for (const [name, chunk] of Object.entries(bundle)) {
        if (chunk.type === 'chunk' && chunk.isEntry) {
          src = src.replace(/<script[^>]*src="[^"]*"[^>]*><\/script>/, '');
          src = src.replace('</body>', () => `<script>${chunk.code}</script></body>`);
          delete bundle[name];
        } else if (chunk.type === 'asset' && name.endsWith('.css')) {
          src = src.replace(/<link[^>]*rel="stylesheet"[^>]*>/, () => `<style>${chunk.source}</style>`);
          delete bundle[name];
        }
      }
      html.source = src;
    },
  };
}

export default defineConfig({
  base: './',
  plugins: [inlineAssets()],
  build: {
    target: 'es2020',
    cssCodeSplit: false,
    modulePreload: false,
    assetsInlineLimit: 100000000,
    rollupOptions: { output: { format: 'iife', inlineDynamicImports: true } },
  },
});
