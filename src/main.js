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
    <header><div class="brand"><div class="brand-symbol">C</div>CamLayer</div><span class="version">EXPERIMENTAL · v0.2.5</span></header>
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
      <section class="settings"><h2 class="section-title">Refine your image</h2><div class="sliders"><div class="setting"><label class="slider-label" for="threshold">Mask threshold <output id="threshold-value">0.35</output></label><input id="threshold" type="range" min="0.15" max="0.85" step="0.01" value="0.35"></div><div class="setting"><label class="slider-label" for="softness">Edge softness <output id="softness-value">0.28</output></label><input id="softness" type="range" min="-2.00" max="2.00" step="0.01" value="0.28"></div><div class="setting"><label class="slider-label" for="cleanup">Edge cleanup <output id="cleanup-value">0.30</output></label><input id="cleanup" type="range" min="0" max="1" step="0.05" value="0.30"><p class="setting-help">Reduces mask noise and small artifacts before feathering. High values may remove fine details.</p></div><div class="setting"><label class="slider-label" for="stability">Edge stability <output id="stability-value">0.50</output></label><input id="stability" type="range" min="0" max="2.00" step="0.05" value="0.50"><p class="setting-help">Reduces static edge flicker while preserving faster response to larger movements.</p></div></div><label class="checkbox"><input id="checker" type="checkbox" checked> Show checkerboard behind the transparent output</label><label class="checkbox"><input id="stabilize" type="checkbox" checked> Stabilize mask between frames</label></section>
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
const cleanupInput = element('cleanup');
const stabilityInput = element('stability');
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
let spatialBuffer = null;
let cleanupBuffer = null;
let alphaBuffer = null;
let morphologyRow = null;
let morphologyOutput = null;
const SETTINGS_KEY = 'camlayer.settings.v2';
const DEFAULT_SETTINGS = Object.freeze({ camera: '', quality: '640', threshold: 0.35, softness: 0.28, cleanup: 0.30, stability: 0.50, stabilize: true, checker: true });
let starting = false;
const maskCanvas = document.createElement('canvas');
const maskContext = maskCanvas.getContext('2d');
if (!maskContext) throw new Error('Mask canvas is unavailable.');

function readSettings() {
  try {
    const stored = JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}');
    if (!stored || typeof stored !== 'object') return { ...DEFAULT_SETTINGS };
    const settings = { ...DEFAULT_SETTINGS };
    for (const key of ['camera', 'quality', 'threshold', 'softness', 'cleanup', 'stability', 'stabilize', 'checker']) {
      if (typeof stored[key] === typeof settings[key]) settings[key] = stored[key];
    }
    if (!['640', '1280'].includes(settings.quality)) settings.quality = '640';
    for (const [key, low, high] of [['threshold', 0.15, 0.85], ['softness', -2.0, 2.0], ['cleanup', 0, 1], ['stability', 0, 2.0]]) {
      if (!Number.isFinite(settings[key])) settings[key] = DEFAULT_SETTINGS[key];
      settings[key] = Math.min(high, Math.max(low, settings[key]));
    }
    return settings;
  } catch (error) {
    console.warn('Saved settings could not be restored:', error);
    return { ...DEFAULT_SETTINGS };
  }
}

const savedSettings = readSettings();
cameraSelect.dataset.preferredCamera = savedSettings.camera;
qualitySelect.value = savedSettings.quality;
for (const [input, name] of [[thresholdInput, 'threshold'], [softnessInput, 'softness'], [cleanupInput, 'cleanup'], [stabilityInput, 'stability']]) {
  input.value = String(savedSettings[name]);
  element(name + '-value').value = Number(input.value).toFixed(2);
}
stabilizeInput.checked = savedSettings.stabilize;
checkerInput.checked = savedSettings.checker;
outputStage.classList.toggle('checker', checkerInput.checked);

function saveSettings() {
  const values = {
    camera: cameraSelect.value,
    quality: qualitySelect.value,
    threshold: Number(thresholdInput.value),
    softness: Number(softnessInput.value),
    cleanup: Number(cleanupInput.value),
    stability: Number(stabilityInput.value),
    stabilize: stabilizeInput.checked,
    checker: checkerInput.checked
  };
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(values));
  } catch (error) {
    console.warn('Settings could not be saved:', error);
  }
}

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
    const previous = cameraSelect.value || cameraSelect.dataset.preferredCamera || '';
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
  const size = width * height;
  if (maskCanvas.width !== width || maskCanvas.height !== height || !previousMask || previousMask.length !== size) {
    maskCanvas.width = width;
    maskCanvas.height = height;
    previousMask = new Float32Array(mask);
    spatialBuffer = new Float32Array(size);
    cleanupBuffer = new Float32Array(size);
    alphaBuffer = new Float32Array(size);
    morphologyRow = new Float32Array(size);
    morphologyOutput = new Float32Array(size);
  }

  const threshold = Number(thresholdInput.value);
  const cleanup = Number(cleanupInput.value);
  const stabilize = stabilizeInput.checked;
  const data = maskContext.createImageData(width, height);

  // Use an adaptive temporal filter. Minor confidence fluctuations are
  // smoothed more aggressively, while larger changes (moving silhouette)
  // rapidly reduce history weight to avoid visible trailing.
  // The blend always responds to the current frame; it never freezes pixels.
  const stability = Number(stabilityInput.value);
  // Extend stability to 2.0 without allowing the mask to become frozen.
  // Above 1.0, steady edges are filtered more strongly while movement
  // detection remains sensitive to large, fast changes.
  const quietHistory = stability <= 1
    ? 0.45 + stability * 0.43
    : 0.88 + (stability - 1) * 0.075;
  const motionThreshold = 0.035 + Math.min(stability, 1) * 0.055;
  const motionWindow = 0.12 + Math.min(stability, 1) * 0.08;
  for (let i = 0; i < size; i++) {
    const current = Math.min(1, Math.max(0, mask[i]));
    const previous = previousMask[i];
    const difference = Math.abs(current - previous);
    const movement = Math.min(1, Math.max(0, (difference - motionThreshold) / motionWindow));
    const historyWeight = stabilize ? quietHistory * (1 - movement * movement * (3 - 2 * movement)) : 0;
    const blended = previous * historyWeight + current * (1 - historyWeight);
    spatialBuffer[i] = blended;
    previousMask[i] = blended;
  }

  // Edge cleanup is spatial denoising before alpha conversion.
  // A separable box filter is much cheaper than a full 2D kernel.
  // Radius grows up to 4 native mask pixels at maximum cleanup.
  let values = spatialBuffer;
  if (cleanup > 0.001) {
    const radius = Math.max(1, Math.round(cleanup * 4));
    const windowSize = radius * 2 + 1;
    const horizontal = new Float32Array(size);
    for (let y = 0; y < height; y++) {
      const row = y * width;
      let sum = 0;
      for (let dx = -radius; dx <= radius; dx++) {
        sum += spatialBuffer[row + Math.min(width - 1, Math.max(0, dx))];
      }
      for (let x = 0; x < width; x++) {
        horizontal[row + x] = sum / windowSize;
        const leaving = Math.max(0, x - radius);
        const entering = Math.min(width - 1, x + radius + 1);
        sum += spatialBuffer[row + entering] - spatialBuffer[row + leaving];
      }
    }
    for (let x = 0; x < width; x++) {
      let sum = 0;
      for (let dy = -radius; dy <= radius; dy++) {
        sum += horizontal[Math.min(height - 1, Math.max(0, dy)) * width + x];
      }
      for (let y = 0; y < height; y++) {
        const index = y * width + x;
        const blurred = sum / windowSize;
        cleanupBuffer[index] = spatialBuffer[index] * (1 - cleanup) + blurred * cleanup;
        const leaving = Math.max(0, y - radius);
        const entering = Math.min(height - 1, y + radius + 1);
        sum += horizontal[entering * width + x] - horizontal[leaving * width + x];
      }
    }
    values = cleanupBuffer;
  }

  // Convert confidence to alpha once. Edge softness will change the
  // geometry of this alpha mask instead of approximating it with a threshold.
  const signedSoftness = Number(softnessInput.value);
  const transition = 0.15;
  const low = threshold - transition / 2;
  for (let i = 0; i < size; i++) {
    const t = Math.max(0, Math.min(1, (values[i] - low) / transition));
    alphaBuffer[i] = t * t * (3 - 2 * t);
  }

  // A positive setting dilates (max filter); a negative setting erodes
  // (min filter). Work at model resolution, scaling radius to output pixels.
  // A separable rectangular footprint keeps the per-frame cost manageable.
  const outputWidth = output.width || width;
  const desiredNativeRadius = Math.abs(signedSoftness) * 12 * width / outputWidth;
  const radius = Math.min(8, Math.ceil(desiredNativeRadius));
  const strength = Math.min(1, desiredNativeRadius);
  let finalAlpha = alphaBuffer;
  if (radius > 0 && strength > 0) {
    const expand = signedSoftness > 0;
    for (let y = 0; y < height; y++) {
      const row = y * width;
      for (let x = 0; x < width; x++) {
        let extremum = alphaBuffer[row + x];
        for (let dx = -radius; dx <= radius; dx++) {
          const xx = Math.max(0, Math.min(width - 1, x + dx));
          const value = alphaBuffer[row + xx];
          extremum = expand ? Math.max(extremum, value) : Math.min(extremum, value);
        }
        morphologyRow[row + x] = extremum;
      }
    }
    for (let y = 0; y < height; y++) {
      const row = y * width;
      for (let x = 0; x < width; x++) {
        let extremum = morphologyRow[row + x];
        for (let dy = -radius; dy <= radius; dy++) {
          const yy = Math.max(0, Math.min(height - 1, y + dy));
          const value = morphologyRow[yy * width + x];
          extremum = expand ? Math.max(extremum, value) : Math.min(extremum, value);
        }
        const index = row + x;
        morphologyOutput[index] = alphaBuffer[index] * (1 - strength) + extremum * strength;
      }
    }
    finalAlpha = morphologyOutput;
  }

  for (let i = 0; i < size; i++) {
    const offset = i * 4;
    data.data[offset] = 255;
    data.data[offset + 1] = 255;
    data.data[offset + 2] = 255;
    data.data[offset + 3] = Math.round(finalAlpha[i] * 255);
  }
  maskContext.putImageData(data, 0, 0);
}

function renderFrame() {
  if (!running || !segmenter || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) return;
  const start = performance.now();
  const result = segmenter.segmentForVideo(video, start);
  try {
    const masks = result.confidenceMasks ?? [];
    // Selfie segmentation may return a single foreground confidence mask.
    // Multi-channel variants typically place the person mask at index 1.
    const personMask = masks.length > 1 ? masks[1] : masks[0];
    if (!personMask) throw new Error(`The model returned no confidence mask (count: ${masks.length}).`);
    updateMask(personMask.getAsFloat32Array(), personMask.width, personMask.height);
    outputContext.globalCompositeOperation = 'source-over';
    outputContext.clearRect(0, 0, output.width, output.height);
    // Blur softens the morphologically shifted edge in both directions.
    const featherPixels = Math.abs(Number(softnessInput.value)) * 8;
    outputContext.filter = featherPixels > 0 ? `blur(${featherPixels.toFixed(2)}px)` : 'none';
    outputContext.drawImage(maskCanvas, 0, 0, output.width, output.height);
    outputContext.filter = 'none';
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
    spatialBuffer = null;
    cleanupBuffer = null;
    alphaBuffer = null;
    morphologyRow = null;
    morphologyOutput = null;
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
  spatialBuffer = null;
  cleanupBuffer = null;
  alphaBuffer = null;
  morphologyRow = null;
  morphologyOutput = null;
  element('fps').textContent = '—';
  element('latency').textContent = '—';
  element('dimensions').textContent = '—';
  setControls(false);
  status('Camera stopped.');
}

startButton.addEventListener('click', () => { void start(); });
stopButton.addEventListener('click', stop);
for (const [input, label] of [[thresholdInput, 'threshold-value'], [softnessInput, 'softness-value'], [cleanupInput, 'cleanup-value'], [stabilityInput, 'stability-value']]) {
  input.addEventListener('input', () => { element(label).value = Number(input.value).toFixed(2); saveSettings(); });
}
for (const control of [cameraSelect, qualitySelect, stabilizeInput, checkerInput]) {
  control.addEventListener('change', saveSettings);
}
checkerInput.addEventListener('change', () => outputStage.classList.toggle('checker', checkerInput.checked));
window.addEventListener('pagehide', () => {
  running = false;
  cancelAnimationFrame(frameId);
  stream?.getTracks().forEach((track) => track.stop());
  segmenter?.close();
});
if (navigator.mediaDevices?.enumerateDevices) void listCameras();
