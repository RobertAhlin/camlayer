const params = new URLSearchParams(window.location.search);
const token = params.get('token');
const debug = params.get('debug') === '1';
const canvas = document.querySelector('canvas');
const ctx = canvas.getContext('2d', { alpha: true });
const status = document.querySelector('#status');
let ws = null;
let decoding = false;
let senderConnected = false;
let reconnectTimer = null;
let latestBlob = null;
const size = { width: 1280, height: 720 };

function message(text) {
  status.textContent = text;
  status.hidden = !debug;
}
function clear() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
}
async function displayFrame(data) {
  if (decoding) return; // Drop when decoding cannot keep up.
  decoding = true;
  try {
    const blob = data instanceof Blob ? data : new Blob([data], { type: 'image/webp' });
    const bitmap = await createImageBitmap(blob);
    if (bitmap.width !== size.width || bitmap.height !== size.height) {
      size.width = bitmap.width;
      size.height = bitmap.height;
      canvas.width = bitmap.width;
      canvas.height = bitmap.height;
    }
    clear();
    ctx.drawImage(bitmap, 0, 0);
    bitmap.close();
    message('Receiving transparent frames.');
  } catch (error) {
    console.warn('CamLayer frame decode:', error);
    message('Frame decoding failed. See browser source logs.');
  } finally {
    decoding = false;
  }
}
function connect() {
  if (!token) {
    message('No bridge token in this OBS source URL.');
    return;
  }
  const url = new URL('/ws', location.href);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  url.searchParams.set('role', 'receiver');
  url.searchParams.set('token', token);
  ws = new WebSocket(url);
  ws.binaryType = 'blob';
  ws.onopen = () => message('Connected to local bridge; waiting for Chrome camera.');
  ws.onmessage = (event) => {
    if (typeof event.data === 'string') {
      try {
        const payload = JSON.parse(event.data);
        if (payload.type === 'clear') {
          clear();
          return;
        }
        if (payload.type === 'state') {
          senderConnected = payload.senderConnected;
          if (!senderConnected) {
            clear();
            message('Waiting for Chrome CamLayer sender.');
          }
        }
      } catch {}
    } else {
      void displayFrame(event.data);
    }
  };
  ws.onclose = () => {
    clear();
    message('Bridge disconnected. Retrying...');
    if (!reconnectTimer) reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 1500);
  };
  ws.onerror = () => message('Could not connect to local bridge.');
}
connect();
window.addEventListener('pagehide', () => {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  ws?.close();
});
