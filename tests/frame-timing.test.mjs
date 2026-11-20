import test from 'node:test';
import assert from 'node:assert/strict';
import { FrameTiming } from '../src/frame-timing.mjs';

test('counts one frame per distinct video metadata counter and records gaps', () => {
  const timing = new FrameTiming();
  assert.equal(timing.onPresented(0, 10), true);
  timing.onProcessed(20, 20);
  assert.equal(timing.onPresented(30, 10), false);
  assert.equal(timing.onPresented(66, 13), true);
  timing.onProcessed(80, 14);
  assert.equal(timing.totalPresented, 4);
  assert.equal(timing.totalSkipped, 2);
  assert.equal(timing.totalProcessed, 2);
});

test('rolling rates use elapsed wall time and become zero when frames stop', () => {
  const timing = new FrameTiming(3000);
  for (let i = 0; i < 60; i++) {
    const now = i * 33.333;
    timing.onPresented(now, i + 1);
    timing.onProcessed(now + 15, 15);
  }
  const rates = timing.rates(2000);
  assert.ok(rates.presentedFps > 25 && rates.presentedFps < 35);
  assert.ok(rates.processedFps > 25 && rates.processedFps < 35);
  const idle = timing.rates(8000);
  assert.equal(idle.presentedFps, 0);
  assert.equal(idle.processedFps, 0);
});

test('stability test tracks average, worst time, gaps and missed presentations', () => {
  const timing = new FrameTiming();
  timing.startTest(0, 2);
  timing.onPresented(100, 1);
  timing.onProcessed(120, 30);
  timing.onPresented(500, 4);
  timing.onProcessed(540, 50);
  const report = timing.testSummary(120000);
  assert.equal(report.status, 'completed');
  assert.equal(report.processed, 2);
  assert.equal(report.presented, 4);
  assert.equal(report.skipped, 2);
  assert.equal(report.stalls, 1);
  assert.equal(report.avgProcessingMs, 40);
  assert.equal(report.maxMs, 50);
  assert.equal(report.progress, 1);
});

test('visibility and manual stop qualify stability results', () => {
  const timing = new FrameTiming();
  assert.throws(() => timing.startTest(0, 3), RangeError);
  timing.startTest(1000, 5);
  timing.noteHidden();
  timing.finishTest(61000, 'stopped');
  const report = timing.testSummary(999999);
  assert.equal(report.status, 'stopped');
  assert.equal(report.backgrounded, true);
  assert.equal(report.elapsedMs, 60000);
});

test('legacy metadata-free mode counts callbacks as estimates', () => {
  const timing = new FrameTiming();
  assert.equal(timing.onPresented(100), true);
  assert.equal(timing.onPresented(133), true);
  assert.equal(timing.totalSkipped, 0);
  assert.equal(timing.totalPresented, 2);
});
