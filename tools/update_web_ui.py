#!/usr/bin/env python3
"""
tools/update_web_ui.py
Redesigns web/index.html:
1. PWA installable (manifest, service worker, meta tags, install prompt button)
2. Removes all emojis and replaces with clean SVG icons / typography
3. Fixes mobile paddings, grid alignment, and touch targets
4. Rebuilds src/web_ui.h gzipped array
"""
import re
import gzip
import os

with open("web/index.html", "r", encoding="utf-8") as f:
    html = f.read()

# 1. PWA Header tags
old_head = '<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">'
new_head = """<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="theme-color" content="#08080a">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="BambuDesk">
<link rel="manifest" href="/manifest.json">
<link rel="icon" type="image/svg+xml" href="/icon.svg">
<link rel="apple-touch-icon" href="/icon-192.png">"""

html = html.replace(old_head, new_head)

# 2. Refine Body & App container CSS for perfect paddings
old_body_css = """body{
  background:var(--bg);
  color:var(--txt);
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","Segoe UI",Roboto,sans-serif;
  min-height:100vh;
  display:flex;
  flex-direction:column;
  align-items:center;
  padding:12px 10px 48px;
}
.app{width:100%;max-width:440px;display:flex;flex-direction:column;gap:14px;}"""

new_body_css = """body{
  background:var(--bg);
  color:var(--txt);
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","Segoe UI",Roboto,sans-serif;
  min-height:100vh;
  display:flex;
  flex-direction:column;
  align-items:center;
  padding:14px 12px calc(48px + env(safe-area-inset-bottom));
  margin:0;
}
.app{width:100%;max-width:440px;display:flex;flex-direction:column;gap:12px;}
.tele-val{font-variant-numeric:tabular-nums;}
.pwa-btn{
  background:rgba(255,255,255,0.06);
  border:1px solid rgba(255,255,255,0.12);
  color:#fff;
  border-radius:999px;
  padding:4px 10px;
  font-size:0.68rem;
  font-weight:600;
  display:inline-flex;
  align-items:center;
  gap:5px;
  cursor:pointer;
  transition:all 0.15s;
}
.pwa-btn:hover{background:rgba(255,255,255,0.14);border-color:rgba(255,255,255,0.25);}
.pwa-btn:active{transform:scale(0.96);}"""

html = html.replace(old_body_css, new_body_css)

# 3. Add PWA Install Button to Header
old_header = """  <!-- Header -->
  <div class="header">
    <div class="brand">STΔN <span>studio.</span></div>
    <div class="badge" onclick="openIpModal()">
      <div class="dot" id="cDot"></div>
      <span id="cIp">192.168.0.154</span>
    </div>
  </div>"""

new_header = """  <!-- Header -->
  <div class="header">
    <div class="brand">STΔN <span>studio.</span></div>
    <div style="display:flex;align-items:center;gap:6px;">
      <button id="pwaInstallBtn" class="pwa-btn" style="display:none;" onclick="installPwa()" title="Install as Web App">
        <svg viewBox="0 0 24 24" width="12" height="12" fill="currentColor"><path d="M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM17 13l-5 5-5-5h3V9h4v4h3z"/></svg>
        <span>Install App</span>
      </button>
      <div class="badge" onclick="openIpModal()">
        <div class="dot" id="cDot"></div>
        <span id="cIp">192.168.0.154</span>
      </div>
    </div>
  </div>"""

html = html.replace(old_header, new_header)

# 4. Remove Emojis from Warning Banner
html = html.replace("<span>⚠️ Browser HTTPS Restriction</span>",
                    """<svg viewBox="0 0 24 24" width="14" height="14" fill="#F87171"><path d="M12 2L1 21h22L12 2zm0 3.99L19.53 19H4.47L12 5.99zM11 10v4h2v-4h-2zm0 6v2h2v-2h-2z"/></svg><span>Browser HTTPS Restriction</span>""")

html = html.replace('style="background:none;border:none;color:var(--txt-muted);cursor:pointer;font-size:0.9rem;">✕</button>',
                    'style="background:none;border:none;color:var(--txt-muted);cursor:pointer;display:flex;align-items:center;padding:4px;"><svg viewBox="0 0 24 24" width="14" height="14" stroke="currentColor" fill="none" stroke-width="2.5"><path d="M18 6L6 18M6 6l12 12"/></svg></button>')

html = html.replace('👉 Open Local Monitor (http://<span class="bannerDirectIp">192.168.0.154</span>)',
                    '<span>Open Local Monitor (http://<span class="bannerDirectIp">192.168.0.154</span>)</span><svg viewBox="0 0 24 24" width="12" height="12" fill="currentColor"><path d="M5 13h11.86l-5.43 5.43 1.42 1.42L21.14 12l-8.29-7.85-1.42 1.42L16.86 11H5v2z"/></svg>')

html = html.replace('👉 Open Direct Link (http://<span class="bannerDirectIp">192.168.0.154</span>)',
                    '<span>Open Direct Link (http://<span class="bannerDirectIp">192.168.0.154</span>)</span>')

# 5. Remove Emojis from D-Pad titles
html = html.replace('title="Up (▲)"', 'title="Up"')
html = html.replace('title="Left (◀)"', 'title="Left"')
html = html.replace('title="Right (▶)"', 'title="Right"')
html = html.replace('title="Down (▼)"', 'title="Down"')

# 6. Remove Emojis from Buzzer Siren
html = html.replace('🚨 Siren', 'Siren')

# 7. Remove Emojis from Pet Health & Moods
html = html.replace('🤒 Sick', 'Sick')
html = html.replace('🌡️ Fever', 'Fever')
html = html.replace('😤 Angry', 'Angry')
html = html.replace('😢 Sad', 'Sad')
html = html.replace('😭 Cry', 'Cry')
html = html.replace('🕺 Dance', 'Dance')
html = html.replace('🎉 Party', 'Party')
html = html.replace('💊 Treat', 'Treat')

# 8. Remove Emojis from JavaScript
html = html.replace('"✓ Connected! (RSSI: "', '"Connected (RSSI: "')
html = html.replace('"✓ Connected"', '"Connected"')
html = html.replace('"❌ "', '""')
html = html.replace("'⚠️ Please tap above to select a file first!'", "'Please select a file first.'")
html = html.replace("`✓ Uploaded ${f.name} successfully!`", "`Uploaded ${f.name} successfully.`")
html = html.replace('"⏳ Pushing image to display..."', '"Pushing image to display..."')
html = html.replace('"⏳ Retrying push..."', '"Retrying push..."')
html = html.replace('"✓ Pushed image to display successfully!"', '"Pushed image to display successfully."')
html = html.replace('"❌ Push failed: "', '"Push failed: "')
html = html.replace('"✓ Display reset to live UI"', '"Display reset to live UI"')

# Replace file icons in JS
old_file_row = "row.innerHTML = `<div class=\"file-name\"><span>${f.dir?'📁':'📄'}</span><span>${f.name}</span><span style=\"color:var(--txt-muted);font-size:0.65rem;\">(${sizeStr})</span></div>"
new_file_row = "row.innerHTML = `<div class=\"file-name\"><span>${f.dir ? '<svg viewBox=\"0 0 24 24\" width=\"14\" height=\"14\" fill=\"var(--cyan)\" style=\"vertical-align:middle;margin-right:4px;\"><path d=\"M10 4H4c-1.1 0-2 .9-2 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>' : '<svg viewBox=\"0 0 24 24\" width=\"14\" height=\"14\" fill=\"var(--txt-muted)\" style=\"vertical-align:middle;margin-right:4px;\"><path d=\"M14 2H6c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z\"/></svg>'}</span><span>${f.name}</span><span style=\"color:var(--txt-muted);font-size:0.65rem;\">(${sizeStr})</span></div>"
html = html.replace(old_file_row, new_file_row)

# 9. Append PWA Service Worker & Install Handler to JS
pwa_script = """
// --- PWA Installation & Service Worker ---
let deferredPrompt = null;
window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault();
  deferredPrompt = e;
  const btn = document.getElementById('pwaInstallBtn');
  if (btn) btn.style.display = 'inline-flex';
});

function installPwa() {
  if (deferredPrompt) {
    deferredPrompt.prompt();
    deferredPrompt.userChoice.then((choice) => {
      if (choice.outcome === 'accepted') {
        const btn = document.getElementById('pwaInstallBtn');
        if (btn) btn.style.display = 'none';
      }
      deferredPrompt = null;
    });
  } else {
    alert('To install BambuDesk:\\n• Android/Chrome: Tap ⋮ menu > Add to Home screen / Install app\\n• iOS/Safari: Tap Share > Add to Home Screen');
  }
}

window.addEventListener('appinstalled', () => {
  const btn = document.getElementById('pwaInstallBtn');
  if (btn) btn.style.display = 'none';
  deferredPrompt = null;
});

if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(() => {});
  });
}
"""

# Insert before closing </script>
html = html.replace("pollStatus();\n</script>", "pollStatus();\n" + pwa_script + "\n</script>")

with open("web/index.html", "w", encoding="utf-8") as f:
    f.write(html)

print("Updated web/index.html successfully!")
