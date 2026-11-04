const MEDIAPIPE_MODULE_URLS = [
  'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.21/vision_bundle.mjs',
  'https://unpkg.com/@mediapipe/tasks-vision@0.10.21/vision_bundle.mjs'
];

const MODEL_URL = 'https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter_landscape/float16/1/selfie_segmenter_landscape.tflite';
const WASM_URL = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.21/wasm';

const app = document.querySelector('#app');
if (!app) throw new Error('Application root is missing.');

app.innerHTML = `
  <div class="shell">
    <header><div class="brand"><div class="brand-symbol">C</div>CamLayer</div><span class="version">EXPERIMENTAL · v0.1</span></header>
    <section class="hero"><div class="eyebrow">AI-powered camera compositing</div><h1>Your camera. Without the background.</h1><p>Real-time person segmentation, processed privately in your browser. Preview a transparent camera layer before we explore OBS integration.</p></section>
    <div class="toolbar">
      <label class="field">Camera <select id="camera"><option value="">Default camera</option></select></label>
      <label class="field">Processing resolution <select id="quality"><option value="640">640 × 360 · Fast</option><option value="1280">1280 × 720 · Quality</option></select></label>
      <button id="start">Start camera</button><button id="stop" class="secondary" disabled>Stop</button>
    </div>
    <div class="views">
      <section class="panel"><div class="panel-header"><strong>Original camera</strong><span class="pill">INPUT</span></div><div class="stage" id="original-stage"><video id="video" autoplay playsinline muted></video><div class="placeholder"><div class="mark">◉</div>Camera preview appears here</div></div></section>
      <section class="panel"><div class="panel-header"><strong>Background removed</strong><span class="pill">ALPHA OUTPUT</span></div><div class="stage checker" id="output-stage"><canvas id="output"></canvas><div class="placeholder"><div class="mark">✦</div>AI output appears here</div></div></section>
    </div>
    <div class="bottom">
      <section class="settings"><h2 class="section-title">Refine your image</h2><div class="sliders"><div class="setting"><label class="slider-label" for="threshold">Mask threshold <output id="threshold-value">0.50</output></label><input id="threshold" type="range" min="0.15" max="0.85" step="0.01" value="0.50"></div><div class="setting"><label class="slider-label" for="softness">Edge softness <output id="softness-value">0.20</output></label><input id="softness" type="range" min="0.02" max="0.50" step="0.01" value="0.20"></div></div><label class="checkbox"><input id="checker" type="checkbox" checked> Show checkerboard behind the transparent output</label><label class="checkbox"><input id="stabilize" type="checkbox" checked> Stabilize mask between frames</label></section>
      <section class="metrics"><h2 class="section-title">Live performance</h2><div class="metric-grid"><div class="metric"><strong id="fps">—</strong><span>Processing FPS</span></div><div class="metric"><strong id="latency">—</strong><span>Inference + render (ms)</span></div><div class="metric"><strong id="dimensions">—</strong><span>Output size</span></div></div><p id="status" class="status"><span class="dot"></span><span id="status-text">Ready to start. Camera permission is requested only after clicking Start.</span></p></section>
    </div><p class="privacy">Privacy first: webcam frames stay in this browser tab. Model and runtime files are downloaded from external hosts on first use. No recording, upload, or OBS integration is included in this build.</p>
  </div>`;

function element(id) {
  const node = document.getElementById(id);
  if (!node) throw new Error(`Missing element: ${id}`);
  return node;
}
const video = element('video');
const output = element('output');
const cameraSelect = element('camera');
const qualitySelect = element('quality');
const startButton = element('start');
const stopButton = element('stop');
const thresholdInput = element('threshold');
const softnessInput = element('softness');
const stabilizeInput = element('stabilize');
const checkerInput = element('checker');
const originalStage = element('original-stage');
const outputStage = element('output-stage');

const outputContext = output.getContext('2d', { alpha: true });
if (!outputContext) throw new Error('Canvas 2D is unavailable.');

let stream = null;
let segmenter = null;
let running = false;
let frameId = 0;
let lastFrameTime = 0;
let averageFps = 0;
let previousMask = null;
let starting = false;
const maskCanvas = document.createElement('canvas');
const maskContext = maskCanvas.getContext('2d');
if (!maskContext) throw new Error('Mask canvas is unavailable.');

function status(message, state = 'idle') {
  element('status').className = `status ${state}`;
  element('status-text').textContent = message;
}

function setControls(active) {
  startButton.disabled = active || starting;
  stopButton.disabled = !active && !starting;
  cameraSelect.disabled = active || starting;
  qualitySelect.disabled = active || starting;
}

async function listCameras() {
  try {
    const devices = await navigator.mediaDevices.enumerateDevices();
    const cameras = devices.filter((device) => device.kind === 'videoinput');
    const previous = cameraSelect.value;
    cameraSelect.replaceChildren(new Option('Default camera', ''));
    cameras.forEach((device, index) => cameraSelect.add(new Option(device.label || `Camera ${index + 1}`, device.deviceId)));
    if ([...cameraSelect.options].some((option) => option.value === previous)) cameraSelect.value = previous;
  } catch {
    // The default camera remains available if device enumeration is blocked.
  }
}

async function getSegmenter() {
  if (segmenter) return segmenter;
  let mediaPipe = null;
  const failures = [];
  for (const url of MEDIAPIPE_MODULE_URLS) {
    try {
      mediaPipe = await import(/* @vite-ignore */ url);
      break;
    } catch (error) {
      console.error('MediaPipe module import failed:', url, error);
      failures.push(url.includes('jsdelivr') ? 'jsDelivr' : 'unpkg');
    }
  }
  if (!mediaPipe) {
    throw new Error(`AI library download failed from ${failures.join(' and ')}. Open the browser Console (F12) for details.`);
  }
  const vision = await mediaPipe.FilesetResolver.forVisionTasks(WASM_URL);
  segmenter = await mediaPipe.ImageSegmenter.createFromOptions(vision, {
    baseOptions: { modelAssetPath: MODEL_URL, delegate: 'CPU' },
    runningMode: 'VIDEO',
    outputConfidenceMasks: true,
    outputCategoryMask: false
  });
  return segmenter;
}

function updateMask(mask, width, height) {
  if (maskCanvas.width !== width || maskCanvas.height !== height) {
    maskCanvas.width = width;
    maskCanvas.height = height;
    previousMask = null;
  }
  const data = maskContext.createImageData(width, height);
  const threshold = Number(thresholdInput.value);
  const softness = Number(softnessInput.value);
  const low = threshold - softness / 2;
  const high = threshold + softness / 2;
  if (!previousMask || previousMask.length !== mask.length) previousMask = new Float32Array(mask);
  const stabilize = stabilizeInput.checked;
  for (let i = 0; i < mask.length; i++) {
    const confidence = Math.max(0, Math.min(1, mask[i]));
    const smoothed = stabilize ? previousMask[i] * 0.55 + confidence * 0.45 : confidence;
    previousMask[i] = smoothed;
    const t = Math.max(0, Math.min(1, (smoothed - low) / Math.max(0.001, high - low)));
    const alpha = t * t * (3 - 2 * t);
    const offset = i * 4;
    data.data[offset] = 255;
    data.data[offset + 1] = 255;
    data.data[offset + 2] = 255;
    data.data[offset + 3] = Math.round(alpha * 255);
  }
  maskContext.putImageData(data, 0, 0);
}

function renderFrame() {
  if (!running || !segmenter || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) return;
  const start = performance.now();
  const result = segmenter.segmentForVideo(video, start);
  try {
    const personMask = result.confidenceMasks?.[1];
    if (!personMask) throw new Error('The model did not return a person confidence mask.');
    updateMask(personMask.getAsFloat32Array(), personMask.width, personMask.height);
    outputContext.globalCompositeOperation = 'source-over';
    outputContext.clearRect(0, 0, output.width, output.height);
    outputContext.drawImage(maskCanvas, 0, 0, output.width, output.height);
    outputContext.globalCompositeOperation = 'source-in';
    outputContext.drawImage(video, 0, 0, output.width, output.height);
    outputContext.globalCompositeOperation = 'source-over';
  } finally {
    result.close();
  }
  const finished = performance.now();
  const delta = lastFrameTime ? finished - lastFrameTime : 0;
  lastFrameTime = finished;
  if (delta > 0) averageFps = averageFps === 0 ? 1000 / delta : averageFps * 0.8 + (1000 / delta) * 0.2;
  element('fps').textContent = averageFps ? averageFps.toFixed(1) : '—';
  element('latency').textContent = (finished - start).toFixed(0);
}

function loop() {
  if (!running) return;
  try {
    if (video.currentTime !== lastVideoTime) {
      lastVideoTime = video.currentTime;
      renderFrame();
    }
  } catch (error) {
    stop();
    status(error instanceof Error ? error.message : 'Processing failed.', 'error');
    return;
  }
  frameId = requestAnimationFrame(loop);
}
let lastVideoTime = -1;

async function start() {
  if (running || starting) return;
  starting = true;
  setControls(false);
  status('Requesting camera permission…');
  try {
    if (!navigator.mediaDevices?.getUserMedia) throw new Error('Camera access requires HTTPS or localhost and a supported browser.');
    const width = Number(qualitySelect.value);
    const height = Math.round(width * 9 / 16);
    stream = await navigator.mediaDevices.getUserMedia({
      audio: false,
      video: {
        ...(cameraSelect.value ? { deviceId: { ideal: cameraSelect.value } } : {}),
        width: { ideal: width },
        height: { ideal: height },
        frameRate: { ideal: 30 }
      }
    });
    video.srcObject = stream;
    await video.play();
    await listCameras();
    status('Loading the AI model… This may take longer the first time.');
    await getSegmenter();
    if (video.videoWidth === 0 || video.videoHeight === 0) throw new Error('The camera did not provide a video frame.');
    output.width = video.videoWidth;
    output.height = video.videoHeight;
    element('dimensions').textContent = `${output.width}×${output.height}`;
    previousMask = null;
    averageFps = 0;
    lastFrameTime = 0;
    lastVideoTime = -1;
    running = true;
    originalStage.classList.add('active');
    outputStage.classList.add('active');
    status('Camera active. Background removal is running locally.', 'running');
    frameId = requestAnimationFrame(loop);
  } catch (error) {
    console.error('CamLayer startup failed:', error);
    stop();
    const message = error instanceof Error ? error.message : 'Unable to start the camera.';
    status(message, 'error');
  } finally {
    starting = false;
    setControls(running);
  }
}

function stop() {
  running = false;
  cancelAnimationFrame(frameId);
  stream?.getTracks().forEach((track) => track.stop());
  stream = null;
  video.pause();
  video.srcObject = null;
  outputContext.clearRect(0, 0, output.width, output.height);
  originalStage.classList.remove('active');
  outputStage.classList.remove('active');
  previousMask = null;
  element('fps').textContent = '—';
  element('latency').textContent = '—';
  element('dimensions').textContent = '—';
  setControls(false);
  status('Camera stopped.');
}

startButton.addEventListener('click', () => { void start(); });
stopButton.addEventListener('click', stop);
for (const [input, label] of [[thresholdInput, 'threshold-value'], [softnessInput, 'softness-value']]) {
  input.addEventListener('input', () => { element(label).value = Number(input.value).toFixed(2); });
}
checkerInput.addEventListener('change', () => outputStage.classList.toggle('checker', checkerInput.checked));
window.addEventListener('pagehide', () => {
  running = false;
  cancelAnimationFrame(frameId);
  stream?.getTracks().forEach((track) => track.stop());
  segmenter?.close();
});
if (navigator.mediaDevices?.enumerateDevices) void listCameras();
