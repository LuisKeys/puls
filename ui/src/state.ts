import { MAX_STEPS, type BankSlot, type NativeEvent, type Pattern, type Settings, type Step, type Track, type WirePattern, type WireStep, type WireTrack } from './types';

export type Change =
  | { kind: 'rebuild' } // init / patternChanged: structure may differ
  | { kind: 'track'; id: number }
  | { kind: 'step'; id: number; step: number }
  | { kind: 'settings' }
  | { kind: 'bank' }
  | { kind: 'history' }
  | { kind: 'selection' }
  | { kind: 'transport' }
  | { kind: 'playheads' }
  | { kind: 'error'; message: string };

export const DEFAULT_STEP: Step = { active: false, velocity: 100, probability: 100, ratchets: 1, microtiming: 0, gate: 50 };

export const normalizeStep = (w: WireStep | undefined): Step => ({ ...DEFAULT_STEP, ...w });

function normalizeTrack(w: WireTrack): Track {
  const steps: Step[] = [];
  for (let i = 0; i < MAX_STEPS; i++) steps.push(normalizeStep(w.steps?.[i]));
  return { ...w, steps };
}

function normalizePattern(w: WirePattern): Pattern {
  return { name: w.name, seed: w.seed, tracks: w.tracks.map(normalizeTrack) };
}

export const state = {
  pattern: { name: '', seed: 1, tracks: [] } as Pattern,
  settings: { anchorMode: 'restart', enabled: true, patternSwitch: 'bar' } as Settings,
  selected: 0, // track id
  bank: [] as BankSlot[],
  selectedPattern: 0, // bank slot being viewed/edited
  playingPattern: 0, // bank slot the sequencer plays (a switch may still be queued)
  hasClipboard: false,
  loaded: false,
  canUndo: false,
  canRedo: false,
  playing: false,
  bpm: 0,
  quarter: 0,
  playheads: [] as number[], // per track index
};

type Listener = (c: Change) => void;
const listeners: Listener[] = [];
export const onChange = (fn: Listener): void => void listeners.push(fn);
const emit = (c: Change): void => listeners.forEach((l) => l(c));

export const trackById = (id: number): Track | undefined => state.pattern.tracks.find((t) => t.id === id);
export const selectedTrack = (): Track | undefined => trackById(state.selected) ?? state.pattern.tracks[0];

export function select(id: number): void {
  if (state.selected === id) return;
  state.selected = id;
  emit({ kind: 'selection' });
}

function keepSelection(): void {
  if (!trackById(state.selected)) state.selected = state.pattern.tracks[0]?.id ?? 0;
}

export function applyEvent(ev: NativeEvent): void {
  switch (ev.type) {
    case 'init':
      state.pattern = normalizePattern(ev.pattern);
      state.settings = ev.settings;
      state.bank = ev.bank;
      state.selectedPattern = ev.selected;
      state.hasClipboard = ev.hasClipboard;
      state.loaded = true;
      state.canUndo = ev.canUndo;
      state.canRedo = ev.canRedo;
      keepSelection();
      emit({ kind: 'rebuild' });
      emit({ kind: 'settings' });
      emit({ kind: 'bank' });
      break;
    case 'patternChanged':
      state.pattern = normalizePattern(ev.pattern);
      state.selectedPattern = ev.selected;
      keepSelection();
      emit({ kind: 'rebuild' });
      emit({ kind: 'bank' });
      break;
    case 'bankChanged':
      state.bank = ev.bank;
      emit({ kind: 'bank' });
      break;
    case 'clipboard':
      state.hasClipboard = ev.hasPattern;
      emit({ kind: 'bank' });
      break;
    case 'stepsChanged':
      for (const c of ev.cells) {
        const t = trackById(c.track);
        if (!t) continue;
        t.steps[c.step] = normalizeStep(c.data);
        emit({ kind: 'step', id: t.id, step: c.step });
      }
      break;
    case 'trackChanged': {
      const t = trackById(ev.track.id);
      if (!t) return;
      Object.assign(t, ev.track);
      emit({ kind: 'track', id: t.id });
      break;
    }
    case 'stepChanged': {
      const t = trackById(ev.track);
      if (!t) return;
      t.steps[ev.step] = normalizeStep(ev.data);
      emit({ kind: 'step', id: t.id, step: ev.step });
      break;
    }
    case 'settings':
      state.settings = ev.settings;
      emit({ kind: 'settings' });
      break;
    case 'history':
      state.canUndo = ev.canUndo;
      state.canRedo = ev.canRedo;
      emit({ kind: 'history' });
      break;
    case 'transport':
      state.playing = ev.playing;
      state.bpm = ev.bpm;
      state.quarter = ev.quarter;
      emit({ kind: 'transport' });
      break;
    case 'playheads':
      state.playheads = ev.steps;
      emit({ kind: 'playheads' });
      if (state.playingPattern !== ev.pattern) {
        state.playingPattern = ev.pattern;
        emit({ kind: 'bank' });
      }
      break;
    case 'error':
      emit({ kind: 'error', message: ev.message });
      break;
  }
}
