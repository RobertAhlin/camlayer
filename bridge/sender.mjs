export class BridgeSender {
  constructor(token, onState = () => {}) {
    this.token = token;
    this.onState = onState;
    this.socket = null;
    this.connected = false;
    this.receiverCount = 0;
    this.encoding = false;
    this.lastFrameAt = 0;
    this.closed = false;
    this.generation = 0;
  }

  connect() {
    if (this.closed) return;
    const address = new URL('/ws', window.location.origin);
    address.protocol = address.protocol === 'https:' ? 'wss:' : 'ws:';
    address.searchParams.set('role', 'sender');
    address.searchParams.set('token', this.token);
    const socket = new WebSocket(address);
    this.socket = socket;
    socket.onopen = () => {
      this.connected = true;
      this.onState('Bridge connected. Start the camera and add the OBS source.');
    };
    socket.onmessage = (event) => {
      if (typeof event.data !== 'string') return;
      try {
        const message = JSON.parse(event.data);
        if (message.type === 'state') {
          this.receiverCount = message.receiverCount;
          this.onState(`Bridge connected · ${this.receiverCount} OBS viewer(s).`);
        }
      } catch {}
    };
    socket.onclose = () => {
      this.connected = false;
      this.receiverCount = 0;
      if (!this.closed) {
        this.onState('Local bridge disconnected. Waiting to reconnect...');
        setTimeout(() => this.connect(), 1500);
      }
    };
    socket.onerror = () => this.onState('Local bridge connection error.');
  }

  sendFrame(canvas, now) {
    const socket = this.socket;
    if (!this.connected || !socket || socket.readyState !== WebSocket.OPEN ||
        this.receiverCount === 0 || this.encoding || socket.bufferedAmount > 1200000 ||
        now - this.lastFrameAt < 33) return;

    this.encoding = true;
    this.lastFrameAt = now;
    const generation = this.generation;
    // WebP supports transparent alpha. Fall back to PNG if WebP encoding is unavailable.
    canvas.toBlob((blob) => {
      this.encoding = false;
      if (blob && generation === this.generation && this.socket === socket && socket.readyState === WebSocket.OPEN &&
          socket.bufferedAmount < 1200000) socket.send(blob);
    }, 'image/webp', 0.92);
  }

  clear() {
    this.generation++;
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify({ type: 'clear' }));
    }
  }

  close() {
    this.closed = true;
    this.generation++;
    this.socket?.close();
    this.socket = null;
  }
}
