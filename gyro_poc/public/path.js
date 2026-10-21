export const DURATION = 18;
export function positionAt(seconds) {
  const t = Math.max(0, Math.min(1, seconds / DURATION));
  const angle = (-50 + 100 * t) * Math.PI / 180;
  return [8 * Math.sin(angle), 2.3 + 0.35 * Math.sin(Math.PI * t), 8 * Math.cos(angle)];
}
export function playbackTime(state, now) {
  return Math.max(0, Math.min(state.end ?? DURATION, state.offset + (state.playing ? (now - state.startedAt) / 1000 : 0)));
}
