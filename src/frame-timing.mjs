/**
 * Frame cadence and long-running stability diagnostics.
 * Uses presentedFrames metadata when available. No camera or DOM dependency.
 */
export class FrameTiming {
  constructor(windowMs = 3000) {
    this.windowMs = windowMs;
    this.reset();
  }

  reset() {
    this.firstAt = null;
    this.lastPresentedId = null;
    this.lastProcessedAt = null;
    this.presentedEvents = [];
    this.processedEvents = [];
    this.totalPresented = 0;
    this.totalProcessed = 0;
    this.totalSkipped = 0;
    this.test = null;
  }

  prune(now) {
    const cutoff = now - this.windowMs;
    while (this.presentedEvents.length && this.presentedEvents[0][0] < cutoff) {
      this.presentedEvents.shift();
    }
    while (this.processedEvents.length && this.processedEvents[0] < cutoff) {
      this.processedEvents.shift();
    }
  }

  onPresented(now, presentedFrames = null) {
    if (this.firstAt === null) this.firstAt = now;
    let delta = 1;
    if (Number.isSafeInteger(presentedFrames) && presentedFrames >= 0) {
      if (this.lastPresentedId !== null) {
        if (presentedFrames <= this.lastPresentedId) return false;
        delta = presentedFrames - this.lastPresentedId;
      }
      this.lastPresentedId = presentedFrames;
    }
    this.totalPresented += delta;
    this.totalSkipped += delta - 1;
    this.presentedEvents.push([now, delta]);
    if (this.test?.status === 'running') {
      this.test.presented += delta;
      this.test.skipped += delta - 1;
    }
    this.prune(now);
    return true;
  }

  onProcessed(now, processingMs) {
    this.totalProcessed++;
    this.processedEvents.push(now);
    if (this.test?.status === 'running') {
      const run = this.test;
      run.processed++;
      run.sumMs += processingMs;
      run.maxMs = Math.max(run.maxMs, processingMs);
      run.minMs = Math.min(run.minMs, processingMs);
      if (this.lastProcessedAt !== null && now - this.lastProcessedAt > 250) run.stalls++;
    }
    this.lastProcessedAt = now;
    this.prune(now);
  }

  rates(now) {
    this.prune(now);
    if (this.firstAt === null) return { presentedFps: null, processedFps: null };
    const seconds = Math.max(0.1, Math.min(now - this.firstAt, this.windowMs) / 1000);
    const presented = this.presentedEvents.reduce((sum, event) => sum + event[1], 0);
    return {
      presentedFps: presented / seconds,
      processedFps: this.processedEvents.length / seconds
    };
  }

  startTest(now, minutes) {
    if (![2, 5, 10].includes(minutes)) throw new RangeError('Unsupported test duration.');
    this.test = {
      startedAt: now,
      durationMs: minutes * 60000,
      status: 'running',
      endedAt: null,
      reason: '',
      backgrounded: false,
      presented: 0,
      processed: 0,
      skipped: 0,
      stalls: 0,
      sumMs: 0,
      minMs: Infinity,
      maxMs: 0
    };
    this.lastProcessedAt = null;
  }

  finishTest(now, reason = 'completed') {
    if (!this.test || this.test.status !== 'running') return;
    this.test.status = reason;
    this.test.reason = reason;
    this.test.endedAt = now;
  }

  noteHidden() {
    if (this.test?.status === 'running') this.test.backgrounded = true;
  }

  testSummary(now) {
    const run = this.test;
    if (!run) return null;
    if (run.status === 'running' && now - run.startedAt >= run.durationMs) {
      this.finishTest(now);
    }
    const end = run.endedAt ?? now;
    const elapsedMs = Math.max(0, end - run.startedAt);
    return {
      ...run,
      elapsedMs,
      avgProcessingMs: run.processed ? run.sumMs / run.processed : 0,
      avgProcessedFps: elapsedMs > 0 ? run.processed / (elapsedMs / 1000) : 0,
      progress: Math.min(1, elapsedMs / run.durationMs)
    };
  }
}
