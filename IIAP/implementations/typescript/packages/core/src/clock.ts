export interface Clock {
  now(): number;
  setTimeout(callback: () => void, delayMs: number): ReturnType<typeof setTimeout>;
  clearTimeout(handle: ReturnType<typeof setTimeout>): void;
}
export const systemClock: Clock = {
  now: () => Date.now(),
  setTimeout: (callback, delayMs) => globalThis.setTimeout(callback, delayMs),
  clearTimeout: (handle) => globalThis.clearTimeout(handle),
};

export function createManualClock(start = 0): Clock & { advanceBy(ms: number): void } {
  let now = start;
  let next = 1;
  const timers = new Map<number, { due: number; callback: () => void }>();
  return {
    now: () => now,
    setTimeout(callback, delayMs) {
      const id = next++;
      timers.set(id, { due: now + Math.max(0, delayMs), callback });
      return id as unknown as ReturnType<typeof setTimeout>;
    },
    clearTimeout(handle) { timers.delete(handle as unknown as number); },
    advanceBy(ms) {
      now += Math.max(0, ms);
      while (true) {
        const ready = [...timers.entries()]
          .filter(([, timer]) => timer.due <= now)
          .sort((a, b) => a[1].due - b[1].due)[0];
        if (!ready) break;
        timers.delete(ready[0]);
        ready[1].callback();
      }
    },
  };
}
