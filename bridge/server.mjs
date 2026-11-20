import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { WebSocketServer, WebSocket } from 'ws';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const host = '127.0.0.1';
const port = Number(process.env.CAMLAYER_PORT || 8765);
if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error('CAMLAYER_PORT must be between 1024 and 65535.');

const secret = randomBytes(24).toString('hex');
const origin = `http://${host}:${port}`;
const mime = new Map([
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.mjs', 'text/javascript; charset=utf-8'],
  ['.css', 'text/css; charset=utf-8']
]);
const paths = new Map([
  ['/', 'index.html'],
  ['/index.html', 'index.html'],
  ['/src/main.js', 'src/main.js'],
  ['/bridge/sender.mjs', 'bridge/sender.mjs'],
  ['/src/styles.css', 'src/styles.css'],
  ['/src/mask-processor.mjs', 'src/mask-processor.mjs'],
  ['/src/frame-timing.mjs', 'src/frame-timing.mjs'],
  ['/bridge/output.html', 'bridge/output.html'],
  ['/bridge/output.js', 'bridge/output.js']
]);

const headers = {
  'Cache-Control': 'no-store',
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'Content-Security-Policy': "default-src 'self' https://cdn.jsdelivr.net https://unpkg.com https://storage.googleapis.com; script-src 'self' https://cdn.jsdelivr.net https://unpkg.com 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; font-src https://fonts.gstatic.com; connect-src 'self' ws://127.0.0.1:* https://cdn.jsdelivr.net https://unpkg.com https://storage.googleapis.com; img-src 'self' blob: data:; media-src 'self' blob:; worker-src 'self' blob:; object-src 'none'; base-uri 'none'"
};

const server = http.createServer(async (req, res) => {
  const pathname = new URL(req.url || '/', origin).pathname;
  const file = paths.get(pathname);
  if (!file || req.method !== 'GET') {
    res.writeHead(404, headers);
    res.end('Not found');
    return;
  }
  try {
    const content = await readFile(resolve(root, file));
    const type = mime.get(file.slice(file.lastIndexOf('.'))) || 'application/octet-stream';
    res.writeHead(200, { ...headers, 'Content-Type': type });
    res.end(content);
  } catch (error) {
    console.error('File error:', error);
    res.writeHead(500, headers);
    res.end('Failed to read application file');
  }
});

const wss = new WebSocketServer({ noServer: true, maxPayload: 3 * 1024 * 1024 });
let sender = null;
const receivers = new Set();
let lastFrame = null;
let lastFrameAt = 0;
let frameCount = 0;
const MAX_RECEIVER_BACKLOG = 2 * 1024 * 1024;

server.on('upgrade', (req, socket, head) => {
  const url = new URL(req.url || '/', origin);
  const expectedOrigin = req.headers.origin;
  const authorized = url.pathname === '/ws' &&
    url.searchParams.get('token') === secret &&
    ['sender', 'receiver'].includes(url.searchParams.get('role')) &&
    expectedOrigin === origin;
  if (!authorized) {
    socket.write('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
    socket.destroy();
    return;
  }
  wss.handleUpgrade(req, socket, head, (ws) => {
    ws.role = url.searchParams.get('role');
    wss.emit('connection', ws);
  });
});

function broadcastState() {
  const message = JSON.stringify({
    type: 'state',
    senderConnected: Boolean(sender && sender.readyState === WebSocket.OPEN),
    receiverCount: receivers.size
  });
  for (const ws of receivers) if (ws.readyState === WebSocket.OPEN) ws.send(message);
  if (sender?.readyState === WebSocket.OPEN) sender.send(message);
}
wss.on('connection', (ws) => {
  if (ws.role === 'sender') {
    // Only one sender owns the stream at a time. Disconnect the old producer.
    if (sender && sender !== ws) sender.close(1000, 'New sender connected');
    sender = ws;
    lastFrame = null;
    frameCount = 0;
  } else {
    receivers.add(ws);
    if (lastFrame && Date.now() - lastFrameAt < 2000) ws.send(lastFrame, { binary: true });
  }
  broadcastState();

  ws.on('message', (data, isBinary) => {
    if (ws !== sender) return;
    if (!isBinary) {
      try {
        const control = JSON.parse(data.toString());
        if (control?.type === 'clear') {
          lastFrame = null;
          for (const receiver of receivers) {
            if (receiver.readyState === WebSocket.OPEN) receiver.send(JSON.stringify({ type: 'clear' }));
          }
        }
      } catch {}
      return;
    }
    // Only forward a real encoded WebP or PNG image, never arbitrary messages.
    const webp = data.length >= 12 && data.subarray(0, 4).toString() === 'RIFF' && data.subarray(8, 12).toString() === 'WEBP';
    const png = data.length >= 8 && data.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]));
    if (!webp && !png) return;
    frameCount++;
    lastFrame = data;
    lastFrameAt = Date.now();
    for (const receiver of receivers) {
      if (receiver.readyState === WebSocket.OPEN && receiver.bufferedAmount < MAX_RECEIVER_BACKLOG) {
        receiver.send(data, { binary: true });
      }
    }
  });
  ws.on('close', () => {
    if (ws === sender) {
      sender = null;
      lastFrame = null;
    } else {
      receivers.delete(ws);
    }
    broadcastState();
  });
  ws.on('error', (error) => console.warn('Bridge connection:', error.message));
});

server.listen(port, host, () => {
  console.log('\nCamLayer Local OBS Bridge');
  console.log('------------------------------------');
  console.log('1. Chrome sender:');
  console.log(`   ${origin}/?mode=bridge&token=${secret}`);
  console.log('2. OBS Browser Source (1280 x 720):');
  console.log(`   ${origin}/bridge/output.html?token=${secret}`);
  console.log('------------------------------------');
  console.log('Keep this terminal and the Chrome sender open.');
  console.log('All webcam frames stay on this PC. Press Ctrl+C to stop.\n');
});

function shutdown() {
  for (const ws of wss.clients) ws.terminate();
  wss.close();
  server.close();
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
