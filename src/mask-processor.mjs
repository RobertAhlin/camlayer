/**
 * Reusable, resolution-aware mask postprocessing.
 * The input confidence mask is never modified. RGB remains white, while alpha
 * carries the foreground. No dependencies, DOM APIs, or video uploads.
 */
export class MaskProcessor {
  constructor() {
    this.reset();
  }

  reset() {
    this.width = 0;
    this.height = 0;
    this.previous = null;
    this.values = null;
    this.horizontal = null;
    this.cleaned = null;
    this.alpha = null;
    this.morphRow = null;
    this.morphResult = null;
    this.queue = null;
    this.rgba = null;
  }

  process(source, sourceWidth, sourceHeight, options, outputWidth) {
    if (source.length !== sourceWidth * sourceHeight) {
      throw new Error('The confidence mask has unexpected dimensions.');
    }
    const maxWidth = options.detail === 'full' ? sourceWidth : options.detail === 'fast' ? 480 : 640;
    const width = Math.min(sourceWidth, maxWidth);
    const height = Math.max(1, Math.round(sourceHeight * width / sourceWidth));
    const count = width * height;
    const newSize = this.width !== width || this.height !== height;
    if (newSize) {
      this.width = width;
      this.height = height;
      this.previous = new Float32Array(count);
      this.values = new Float32Array(count);
      this.horizontal = new Float32Array(count);
      this.cleaned = new Float32Array(count);
      this.alpha = new Float32Array(count);
      this.morphRow = new Float32Array(count);
      this.morphResult = new Float32Array(count);
      this.queue = new Int32Array(Math.max(width, height));
      this.rgba = new Uint8ClampedArray(count * 4);
      for (let i = 0; i < this.rgba.length; i += 4) {
        this.rgba[i] = 255;
        this.rgba[i + 1] = 255;
        this.rgba[i + 2] = 255;
      }
    }

    const stability = options.stability;
    const history = stability <= 1 ? 0.45 + stability * 0.43 : 0.88 + (stability - 1) * 0.075;
    const motionThreshold = 0.035 + Math.min(stability, 1) * 0.055;
    const motionWindow = 0.12 + Math.min(stability, 1) * 0.08;
    const scaleX = sourceWidth / width;
    const scaleY = sourceHeight / height;
    const fullSize = width === sourceWidth && height === sourceHeight;

    // Sample a confidence value per work pixel and apply motion-aware temporal smoothing.
    // Bilinear downsampling preserves soft boundaries without upscaling the camera feed.
    for (let y = 0; y < height; y++) {
      const sy = Math.max(0, Math.min(sourceHeight - 1, (y + 0.5) * scaleY - 0.5));
      const y0 = Math.floor(sy);
      const y1 = Math.min(sourceHeight - 1, y0 + 1);
      const fy = sy - y0;
      const row0 = y0 * sourceWidth;
      const row1 = y1 * sourceWidth;
      for (let x = 0; x < width; x++) {
        const i = y * width + x;
        let current;
        if (fullSize) {
          current = source[i];
        } else {
          const sx = Math.max(0, Math.min(sourceWidth - 1, (x + 0.5) * scaleX - 0.5));
          const x0 = Math.floor(sx);
          const x1 = Math.min(sourceWidth - 1, x0 + 1);
          const fx = sx - x0;
          const top = source[row0 + x0] * (1 - fx) + source[row0 + x1] * fx;
          const bottom = source[row1 + x0] * (1 - fx) + source[row1 + x1] * fx;
          current = top * (1 - fy) + bottom * fy;
        }
        current = Math.min(1, Math.max(0, current));
        const prior = newSize ? current : this.previous[i];
        const delta = Math.abs(current - prior);
        const movement = Math.max(0, Math.min(1, (delta - motionThreshold) / motionWindow));
        const smoothMovement = movement * movement * (3 - 2 * movement);
        const historyWeight = options.stabilize ? history * (1 - smoothMovement) : 0;
        const blended = prior * historyWeight + current * (1 - historyWeight);
        this.values[i] = blended;
        this.previous[i] = blended;
      }
    }

    // Optional two-pass box smoothing, keeping scratch arrays between frames.
    let confidence = this.values;
    const cleanup = options.cleanup;
    if (cleanup > 0) {
      const radius = Math.max(1, Math.round(cleanup * 4));
      const diameter = radius * 2 + 1;
      for (let y = 0; y < height; y++) {
        const row = y * width;
        let sum = 0;
        for (let dx = -radius; dx <= radius; dx++) {
          sum += this.values[row + Math.max(0, Math.min(width - 1, dx))];
        }
        for (let x = 0; x < width; x++) {
          this.horizontal[row + x] = sum / diameter;
          sum += this.values[row + Math.min(width - 1, x + radius + 1)] - this.values[row + Math.max(0, x - radius)];
        }
      }
      for (let x = 0; x < width; x++) {
        let sum = 0;
        for (let dy = -radius; dy <= radius; dy++) {
          sum += this.horizontal[Math.max(0, Math.min(height - 1, dy)) * width + x];
        }
        for (let y = 0; y < height; y++) {
          const i = y * width + x;
          const blurred = sum / diameter;
          this.cleaned[i] = this.values[i] * (1 - cleanup) + blurred * cleanup;
          sum += this.horizontal[Math.min(height - 1, y + radius + 1) * width + x] - this.horizontal[Math.max(0, y - radius) * width + x];
        }
      }
      confidence = this.cleaned;
    }

    const low = options.threshold - 0.075;
    for (let i = 0; i < count; i++) {
      const t = Math.min(1, Math.max(0, (confidence[i] - low) / 0.15));
      this.alpha[i] = t * t * (3 - 2 * t);
    }

    const signedSoftness = options.softness;
    const desiredRadius = Math.abs(signedSoftness) * 12 * width / Math.max(1, outputWidth);
    const radius = Math.min(8, Math.ceil(desiredRadius));
    const blend = Math.min(1, desiredRadius);
    let finalAlpha = this.alpha;
    if (radius > 0 && blend > 0) {
      const expand = signedSoftness > 0;
      const queue = this.queue;
      for (let y = 0; y < height; y++) {
        const row = y * width;
        let head = 0, tail = 0, next = 0;
        for (let x = 0; x < width; x++) {
          const right = Math.min(width - 1, x + radius);
          while (next <= right) {
            const value = this.alpha[row + next];
            while (tail > head) {
              const prior = this.alpha[row + queue[tail - 1]];
              if (expand ? prior > value : prior < value) break;
              tail--;
            }
            queue[tail++] = next++;
          }
          const left = x - radius;
          while (head < tail && queue[head] < left) head++;
          this.morphRow[row + x] = this.alpha[row + queue[head]];
        }
      }
      for (let x = 0; x < width; x++) {
        let head = 0, tail = 0, next = 0;
        for (let y = 0; y < height; y++) {
          const bottom = Math.min(height - 1, y + radius);
          while (next <= bottom) {
            const value = this.morphRow[next * width + x];
            while (tail > head) {
              const prior = this.morphRow[queue[tail - 1] * width + x];
              if (expand ? prior > value : prior < value) break;
              tail--;
            }
            queue[tail++] = next++;
          }
          const top = y - radius;
          while (head < tail && queue[head] < top) head++;
          const i = y * width + x;
          const extremum = this.morphRow[queue[head] * width + x];
          this.morphResult[i] = this.alpha[i] * (1 - blend) + extremum * blend;
        }
      }
      finalAlpha = this.morphResult;
    }

    // Set alpha only. Opaque white RGB channels are initialized once per size.
    for (let i = 0; i < count; i++) this.rgba[4 * i + 3] = Math.round(finalAlpha[i] * 255);
    return { width, height, data: this.rgba };
  }
}
