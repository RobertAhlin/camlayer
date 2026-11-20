import { MaskProcessor } from './mask-processor.mjs';
import { FrameTiming } from './frame-timing.mjs';

const params = new URLSearchParams(window.location.search);
const OBS_MODE = params.get('mode') === 'obs';
const OBS_DEBUG = OBS_MODE && params.get('debug') === '1';
document.body.classList.toggle('obs-mode', OBS_MODE);
document.body.classList.toggle('obs-debug', OBS_DEBUG);

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
    <header><div class="brand"><div class="brand-symbol">C</div>CamLayer</div><span class="version">EXPERIMENTAL · v0.3.0</span></header>
    <section class="hero"><div class="eyebrow">AI-powered camera compositing</div><h1>Your camera. Without the background.</h1><p>Real-time person segmentation, processed privately in your browser. Preview a transparent camera layer before we explore OBS integration.</p></section>
    <div class="toolbar">
      <label class="field">Camera <select id="camera"><option value="">Default camera</option></select></label>
      <label class="field">Processing resolution <select id="quality"><option value="640">640 × 360 · Fast</option><option value="1280">1280 × 720 · Quality</option></select></label>
      <label class="field">Mask detail <select id="mask-detail"><option value="balanced">Balanced · 640px</option><option value="full">Full · Original</option><option value="fast">Fast · 480px</option></select></label>
      <button id="start">Start camera</button><button id="stop" class="secondary" disabled>Stop</button>
    </div>
    <div class="obs-message" id="obs-message" role="status" aria-live="polite"></div>
    <div class="views">
      <section class="panel"><div class="panel-header"><strong>Original camera</strong><span class="pill">INPUT</span></div><div class="stage" id="original-stage"><video id="video" autoplay playsinline muted></video><div class="placeholder"><div class="mark">◉</div>Camera preview appears here</div></div></section>
      <section class="panel"><div class="panel-header"><strong>Background removed</strong><span class="pill">ALPHA OUTPUT</span></div><div class="stage checker" id="output-stage"><canvas id="output"></canvas><div class="placeholder"><div class="mark">✦</div>AI output appears here</div></div></section>
    </div>
    <div class="bottom">
      <section class="settings"><h2 class="section-title">Refine your image</h2><div class="sliders"><div class="setting"><label class="slider-label" for="threshold">Mask threshold <output id="threshold-value">0.35</output></label><input id="threshold" type="range" min="0.15" max="0.85" step="0.01" value="0.35"></div><div class="setting"><label class="slider-label" for="softness">Edge softness <output id="softness-value">0.28</output></label><input id="softness" type="range" min="-2.00" max="2.00" step="0.01" value="0.28"></div><div class="setting"><label class="slider-label" for="cleanup">Edge cleanup <output id="cleanup-value">0.30</output></label><input id="cleanup" type="range" min="0" max="1" step="0.05" value="0.30"><p class="setting-help">Reduces mask noise and small artifacts before feathering. High values may remove fine details.</p></div><div class="setting"><label class="slider-label" for="stability">Edge stability <output id="stability-value">0.50</output></label><input id="stability" type="range" min="0" max="2.00" step="0.05" value="0.50"><p class="setting-help">Reduces static edge flicker while preserving faster response to larger movements.</p></div></div><label class="checkbox"><input id="checker" type="checkbox" checked> Show checkerboard behind the transparent output</label><label class="checkbox"><input id="stabilize" type="checkbox" checked> Stabilize mask between frames</label></section>
      <section class="metrics"><h2 class="section-title">Live performance</h2><div class="metric-grid"><div class="metric"><strong id="presented-fps">—</strong><span>Presented FPS</span></div><div class="metric"><strong id="fps">—</strong><span>Processed FPS</span></div><div class="metric"><strong id="latency">—</strong><span>Total processing (ms)</span></div><div class="metric"><strong id="dimensions">—</strong><span>Output size</span></div><div class="timing-breakdown" aria-label="Processing time breakdown"><div class="timing-row"><span>AI inference</span><strong id="inference-time">—</strong></div><div class="timing-row"><span>Mask processing</span><strong id="mask-time">—</strong></div><div class="timing-row"><span>Canvas rendering</span><strong id="canvas-time">—</strong></div><p class="timing-note">Smoothed milliseconds per processed frame. Includes mask extraction in mask processing; totals may include small additional overhead.</p></div><p class="cadence-info" id="cadence-info">Frame synchronization will be detected when the camera starts.</p><div class="stability-panel"><h3>Stability test</h3><div class="stability-actions"><label class="field">Duration <select id="test-duration"><option value="2">2 minutes</option><option value="5">5 minutes</option><option value="10" selected>10 minutes</option></select></label><button id="test-start" class="secondary" disabled>Start test</button><button id="test-stop" class="secondary" disabled>End test</button></div><p id="test-results" class="test-results" aria-live="polite">Start the camera to run a test. Keep this tab visible.</p></div></div><p id="status" class="status"><span class="dot"></span><span id="status-text">Ready to start. Camera permission is requested only after clicking Start.</span></p></section>
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
const detailSelect = element('mask-detail');
const startButton = element('start');
const stopButton = element('stop');
const testStartButton = element('test-start');
const testStopButton = element('test-stop');
const testDurationSelect = element('test-duration');
const testResults = element('test-results');
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
let frameId = null;
let frameMode = 'video';
let lastPlaybackTime = -1;
let lastFallbackAt = 0;
let diagnosticsInterval = null;
let smoothedTimes = null;
const frameTiming = new FrameTiming();
const maskProcessor = new MaskProcessor();
let cachedMaskImage = null;
const SETTINGS_KEY = 'camlayer.settings.v2';
const DEFAULT_SETTINGS = Object.freeze({ camera: '', quality: '640', detail: 'balanced', threshold: 0.35, softness: 0.28, cleanup: 0.30, stability: 0.50, stabilize: true, checker: true });
let starting = false;
const maskCanvas = document.createElement('canvas');
const maskContext = maskCanvas.getContext('2d');
if (!maskContext) throw new Error('Mask canvas is unavailable.');

function readSettings() {
  try {
    const stored = JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}');
    if (!stored || typeof stored !== 'object') return { ...DEFAULT_SETTINGS };
    const settings = { ...DEFAULT_SETTINGS };
    for (const key of ['camera', 'quality', 'detail', 'threshold', 'softness', 'cleanup', 'stability', 'stabilize', 'checker']) {
      if (typeof stored[key] === typeof settings[key]) settings[key] = stored[key];
    }
    if (!['640', '1280'].includes(settings.quality)) settings.quality = '640';
    if (!['full', 'balanced', 'fast'].includes(settings.detail)) settings.detail = 'balanced';
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
detailSelect.value = savedSettings.detail;
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
    detail: detailSelect.value,
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
  if (OBS_MODE) {
    const banner = element('obs-message');
    banner.textContent = message;
    banner.classList.toggle('error', state === 'error');
    banner.classList.toggle('visible', state === 'error' || OBS_DEBUG);
  }
}

function setControls(active) {
  startButton.disabled = active || starting;
  stopButton.disabled = !active && !starting;
  cameraSelect.disabled = active || starting;
  qualitySelect.disabled = active || starting;
  // Mask detail can be changed live without restarting the camera.
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
  const image = maskProcessor.process(mask, width, height, {
    detail: detailSelect.value,
    threshold: Number(thresholdInput.value),
    softness: Number(softnessInput.value),
    cleanup: Number(cleanupInput.value),
    stability: Number(stabilityInput.value),
    stabilize: stabilizeInput.checked
  }, output.width);
  if (!cachedMaskImage || cachedMaskImage.width !== image.width || cachedMaskImage.height !== image.height) {
    maskCanvas.width = image.width;
    maskCanvas.height = image.height;
    cachedMaskImage = new ImageData(image.data, image.width, image.height);
  }
  maskContext.putImageData(cachedMaskImage, 0, 0);
}

function updatePerformanceMetrics(inferenceMs, maskMs, canvasMs, totalMs) {
  const alpha = 0.22;
  if (!smoothedTimes) {
    smoothedTimes = { inferenceMs, maskMs, canvasMs, totalMs };
  } else {
    for (const [key, value] of Object.entries({ inferenceMs, maskMs, canvasMs, totalMs })) {
      smoothedTimes[key] = smoothedTimes[key] * (1 - alpha) + value * alpha;
    }
  }
}

function testStatusText(summary) {
  if (!summary) return 'Start the camera to run a test. Keep this tab visible.';
  const elapsed = Math.floor(summary.elapsedMs / 1000);
  const total = Math.floor(summary.durationMs / 1000);
  const formatTime = (seconds) => `${Math.floor(seconds / 60).toString().padStart(2, '0')}:${(seconds % 60).toString().padStart(2, '0')}`;
  const statusLabel = summary.status === 'running' ? 'Running' : summary.status === 'completed' ? 'Completed' : 'Stopped early';
  const warning = summary.backgrounded ? ' Tab was hidden during the test; results may be affected.' : '';
  return `${statusLabel} · ${formatTime(elapsed)} / ${formatTime(total)} · Avg processed ${summary.avgProcessedFps.toFixed(1)} FPS · Mean processing ${summary.avgProcessingMs.toFixed(1)} ms · Worst ${summary.maxMs.toFixed(1)} ms · Skipped presented frames ${summary.skipped} · Long gaps (>250 ms) ${summary.stalls}.${warning}`;
}

function refreshPerformanceDisplay() {
  const now = performance.now();
  const rates = frameTiming.rates(now);
  element('presented-fps').textContent = rates.presentedFps === null ? '—' : rates.presentedFps.toFixed(1);
  element('fps').textContent = rates.processedFps === null ? '—' : rates.processedFps.toFixed(1);
  if (smoothedTimes) {
    element('latency').textContent = smoothedTimes.totalMs.toFixed(0);
    element('inference-time').textContent = smoothedTimes.inferenceMs.toFixed(1) + ' ms';
    element('mask-time').textContent = smoothedTimes.maskMs.toFixed(1) + ' ms';
    element('canvas-time').textContent = smoothedTimes.canvasMs.toFixed(1) + ' ms';
  }
  const summary = frameTiming.testSummary(now);
  testResults.textContent = summary ? testStatusText(summary) : running ? 'Camera ready. Choose a duration and start a stability test.' : 'Start the camera to run a test. Keep this tab visible.';
  testStartButton.disabled = !running || summary?.status === 'running';
  testStopButton.disabled = !running || summary?.status !== 'running';
}

function renderFrame() {
  if (!running || !segmenter || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) return;
  const started = performance.now();
  const result = segmenter.segmentForVideo(video, started);
  const afterInference = performance.now();
  let afterMask = afterInference;
  let afterCanvas = afterInference;
  try {
    const masks = result.confidenceMasks ?? [];
    // Single-channel selfie models return their foreground mask at index 0.
    const personMask = masks.length > 1 ? masks[1] : masks[0];
    if (!personMask) throw new Error(`The model returned no confidence mask (count: ${masks.length}).`);
    updateMask(personMask.getAsFloat32Array(), personMask.width, personMask.height);
    afterMask = performance.now();

    outputContext.globalCompositeOperation = 'source-over';
    outputContext.clearRect(0, 0, output.width, output.height);
    const featherPixels = Math.abs(Number(softnessInput.value)) * 8;
    outputContext.filter = featherPixels > 0 ? `blur(${featherPixels.toFixed(2)}px)` : 'none';
    outputContext.drawImage(maskCanvas, 0, 0, output.width, output.height);
    outputContext.filter = 'none';
    outputContext.globalCompositeOperation = 'source-in';
    outputContext.drawImage(video, 0, 0, output.width, output.height);
    outputContext.globalCompositeOperation = 'source-over';
    afterCanvas = performance.now();
  } finally {
    result.close();
  }
  const finished = performance.now();
  const totalMs = finished - started;
  updatePerformanceMetrics(afterInference - started, afterMask - afterInference, afterCanvas - afterMask, totalMs);
  frameTiming.onProcessed(finished, totalMs);
}

function handleFrameError(error) {
  console.error('CamLayer frame processing failed:', error);
  stop();
  status(error instanceof Error ? error.message : 'Processing failed.', 'error');
}

function scheduleFrame() {
  if (!running) return;
  if (frameMode === 'video') {
    // Called once when a NEW frame is sent to the compositor.
    frameId = video.requestVideoFrameCallback((now, metadata) => {
      frameId = null;
      if (!running) return;
      try {
        if (frameTiming.onPresented(now, metadata.presentedFrames)) renderFrame();
      } catch (error) {
        handleFrameError(error);
        return;
      }
      scheduleFrame();
    });
  } else {
    frameId = requestAnimationFrame(fallbackFrame);
  }
}

function fallbackFrame(now) {
  frameId = null;
  if (!running) return;
  try {
    if (video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) {
      const count = video.getVideoPlaybackQuality?.().totalVideoFrames;
      if (Number.isSafeInteger(count) && count > 0) {
        if (frameTiming.onPresented(now, count)) renderFrame();
      } else if (now - lastFallbackAt >= 33 && video.currentTime !== lastPlaybackTime) {
        // Best-effort fallback: currentTime alone does not identify unique frames.
        lastFallbackAt = now;
        lastPlaybackTime = video.currentTime;
        if (frameTiming.onPresented(now)) renderFrame();
      }
    }
  } catch (error) {
    handleFrameError(error);
    return;
  }
  scheduleFrame();
}

function cancelScheduledFrame() {
  if (frameId === null) return;
  if (frameMode === 'video') video.cancelVideoFrameCallback(frameId);
  else cancelAnimationFrame(frameId);
  frameId = null;
}

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
    maskProcessor.reset();
    cachedMaskImage = null;
    frameTiming.reset();
    smoothedTimes = null;
    lastPlaybackTime = -1;
    lastFallbackAt = 0;
    frameMode = typeof video.requestVideoFrameCallback === 'function' && typeof video.cancelVideoFrameCallback === 'function' ? 'video' : 'fallback';
    element('cadence-info').textContent = frameMode === 'video'
      ? 'New camera frames only · browser frame callback. Skipped frames count compositor frames not processed.'
      : 'Estimated frame rate · older browser fallback; one-to-one frame processing is not guaranteed.';
    running = true;
    originalStage.classList.add('active');
    outputStage.classList.add('active');
    status('Camera active. Background removal is running locally.', 'running');
    scheduleFrame();
    diagnosticsInterval = window.setInterval(refreshPerformanceDisplay, 500);
    refreshPerformanceDisplay();
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
  cancelScheduledFrame();
  if (diagnosticsInterval !== null) window.clearInterval(diagnosticsInterval);
  diagnosticsInterval = null;
  frameTiming.finishTest(performance.now(), 'interrupted');
  stream?.getTracks().forEach((track) => track.stop());
  stream = null;
  video.pause();
  video.srcObject = null;
  outputContext.clearRect(0, 0, output.width, output.height);
  originalStage.classList.remove('active');
  outputStage.classList.remove('active');
  maskProcessor.reset();
  cachedMaskImage = null;
  element('fps').textContent = '—';
  element('presented-fps').textContent = '—';
  element('latency').textContent = '—';
  element('dimensions').textContent = '—';
  for (const id of ['inference-time', 'mask-time', 'canvas-time']) element(id).textContent = '—';
  smoothedTimes = null;
  setControls(false);
  refreshPerformanceDisplay();
  status('Camera stopped.');
}

startButton.addEventListener('click', () => { void start(); });
stopButton.addEventListener('click', stop);
testStartButton.addEventListener('click', () => {
  if (!running) return;
  frameTiming.startTest(performance.now(), Number(testDurationSelect.value));
  refreshPerformanceDisplay();
});
testStopButton.addEventListener('click', () => {
  frameTiming.finishTest(performance.now(), 'stopped');
  refreshPerformanceDisplay();
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) frameTiming.noteHidden();
});
for (const [input, label] of [[thresholdInput, 'threshold-value'], [softnessInput, 'softness-value'], [cleanupInput, 'cleanup-value'], [stabilityInput, 'stability-value']]) {
  input.addEventListener('input', () => { element(label).value = Number(input.value).toFixed(2); saveSettings(); });
}
for (const control of [cameraSelect, qualitySelect, detailSelect, stabilizeInput, checkerInput]) {
  control.addEventListener('change', saveSettings);
}
detailSelect.addEventListener('change', () => { maskProcessor.reset(); cachedMaskImage = null; });
checkerInput.addEventListener('change', () => outputStage.classList.toggle('checker', checkerInput.checked));
window.addEventListener('pagehide', () => {
  running = false;
  cancelScheduledFrame();
  if (diagnosticsInterval !== null) window.clearInterval(diagnosticsInterval);
  frameTiming.finishTest(performance.now(), 'interrupted');
  stream?.getTracks().forEach((track) => track.stop());
  segmenter?.close();
});
if (navigator.mediaDevices?.enumerateDevices) void listCameras();

if (OBS_MODE) {
  // Browser Source has independent camera permissions and preferences.
  // The source URL can override resolution and detail without relying on Chrome storage.
  const resolution = params.get('resolution');
  if (resolution === '640' || resolution === '1280') qualitySelect.value = resolution;
  const detail = params.get('detail');
  if (detail && ['fast', 'balanced', 'full'].includes(detail)) detailSelect.value = detail;
  checkerInput.checked = false;
  outputStage.classList.remove('checker');
  // OBS may deny access to getUserMedia; the source then shows an error banner.
  // The optional debug mode exposes a manual start button for source interaction.
  void start();
}
