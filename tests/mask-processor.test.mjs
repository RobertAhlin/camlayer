import test from 'node:test';
import assert from 'node:assert/strict';
import { MaskProcessor } from '../src/mask-processor.mjs';

const settings = (changes = {}) => ({
  detail: 'full', threshold: 0.5, softness: 0, cleanup: 0,
  stability: 1.75, stabilize: false, ...changes
});
const alpha = (result, x, y) => result.data[(y * result.width + x) * 4 + 3];
function rectangle(w, h, left, top, right, bottom) {
  const data = new Float32Array(w * h);
  for (let y = top; y < bottom; y++) {
    for (let x = left; x < right; x++) data[y * w + x] = 1;
  }
  return data;
}

test('negative values erode and positive values dilate', () => {
  const source = rectangle(20, 20, 6, 6, 14, 14);
  const processor = new MaskProcessor();
  const neutral = processor.process(source, 20, 20, settings(), 20);
  assert.equal(alpha(neutral, 6, 10), 255);
  assert.equal(alpha(neutral, 5, 10), 0);
  const inward = processor.process(source, 20, 20, settings({ softness: -1 }), 20);
  assert.equal(alpha(inward, 6, 10), 0);
  const outward = processor.process(source, 20, 20, settings({ softness: 1 }), 20);
  assert.equal(alpha(outward, 5, 10), 255);
});

test('zero cleanup and zero softness preserve a binary mask', () => {
  const source = rectangle(24, 12, 3, 2, 19, 10);
  const result = new MaskProcessor().process(source, 24, 12, settings(), 24);
  for (let i = 0; i < source.length; i++) {
    assert.equal(result.data[i * 4 + 3], source[i] * 255);
  }
});

test('detail levels and reusable RGBA buffers', () => {
  const source = rectangle(1280, 720, 250, 100, 1000, 600);
  const p = new MaskProcessor();
  const balanced = p.process(source, 1280, 720, settings({ detail: 'balanced' }), 1280);
  assert.deepEqual([balanced.width, balanced.height], [640, 360]);
  const rgba = balanced.data;
  assert.equal(p.process(source, 1280, 720, settings({ detail: 'balanced' }), 1280).data, rgba);
  const fast = p.process(source, 1280, 720, settings({ detail: 'fast' }), 1280);
  assert.deepEqual([fast.width, fast.height], [480, 270]);
  const full = p.process(source, 1280, 720, settings(), 1280);
  assert.deepEqual([full.width, full.height], [1280, 720]);
});

test('fast movement remains responsive with stabilization enabled', () => {
  const p = new MaskProcessor();
  const original = rectangle(20, 10, 2, 2, 12, 8);
  const copy = original.slice();
  p.process(original, 20, 10, settings({ stabilize: true }), 20);
  const moved = rectangle(20, 10, 6, 2, 16, 8);
  const output = p.process(moved, 20, 10, settings({ stabilize: true, stability: 2 }), 20);
  assert.ok(alpha(output, 13, 5) > 200);
  assert.deepEqual(original, copy);
});

test('cleanup and small signed softness work at mask boundaries', () => {
  const p = new MaskProcessor();
  const source = new Float32Array(21 * 11).fill(0.4);
  source[0] = 1;
  for (const softness of [-0.01, 0, 0.01, -2, 2]) {
    const result = p.process(source, 21, 11, settings({ softness, cleanup: 1 }), 21);
    assert.ok(result.data.every(Number.isFinite));
  }
});
