// Application-level bridge schema (versioned). Not a DPF API. Mirrors bridge/Bridge.hpp.
export const BRIDGE_VERSION = 1;
export const MAX_STEPS = 64;
export const MAX_TRACKS = 16;

export const RESOLUTIONS = ['1/4', '1/8', '1/16', '1/32', '1/64', '1/8T', '1/16T', '1/32T'] as const;
export type Resolution = (typeof RESOLUTIONS)[number];

export interface Step {
  active: boolean;
  velocity: number; // 1..127
  probability: number; // 0..100
  ratchets: number; // 1..8
  microtiming: number; // -50..50
  gate: number; // 1..100
}

export interface TrackProps {
  name: string;
  color: string; // #rrggbb
  note: number;
  channel: number;
  length: number; // 1..64
  resolution: Resolution;
  phase: number;
  velocityPct: number;
  probabilityPct: number;
  swing: number; // 0..75
  durationPct: number;
  mute: boolean;
  solo: boolean;
}

export interface Track extends TrackProps {
  id: number;
  steps: Step[]; // always MAX_STEPS long
}

export interface Pattern {
  name: string;
  seed: number;
  tracks: Track[];
}

export interface Settings {
  anchorMode: 'restart' | 'host';
  enabled: boolean;
}

// Wire form: steps are sparse (defaults omitted, trailing defaults dropped).
export type WireStep = Partial<Step>;
export type WireTrack = { id: number } & TrackProps & { steps?: WireStep[] };
export interface WirePattern {
  name: string;
  seed: number;
  tracks: WireTrack[];
}

export type Command =
  | { version: 1; type: 'hello' }
  | { version: 1; type: 'toggleStep'; track: number; step: number }
  | { version: 1; type: 'setStepVelocity'; track: number; step: number; velocity: number; gesture?: number }
  | { version: 1; type: 'modifyStep'; track: number; step: number; props: Partial<Step>; gesture?: number }
  | { version: 1; type: 'modifyTrack'; track: number; props: Partial<TrackProps>; gesture?: number }
  | { version: 1; type: 'addTrack'; props?: Partial<TrackProps> }
  | { version: 1; type: 'removeTrack'; track: number }
  | { version: 1; type: 'setSettings'; settings: Partial<Settings> }
  | { version: 1; type: 'undo' }
  | { version: 1; type: 'redo' };

export type NativeEvent =
  | { version: 1; type: 'init'; pattern: WirePattern; settings: Settings; canUndo: boolean; canRedo: boolean }
  | { version: 1; type: 'patternChanged'; pattern: WirePattern }
  | { version: 1; type: 'trackChanged'; track: { id: number } & TrackProps }
  | { version: 1; type: 'stepChanged'; track: number; step: number; data: WireStep }
  | { version: 1; type: 'settings'; settings: Settings }
  | { version: 1; type: 'history'; canUndo: boolean; canRedo: boolean }
  | { version: 1; type: 'transport'; playing: boolean; bpm: number; quarter: number }
  | { version: 1; type: 'playheads'; steps: number[] }
  | { version: 1; type: 'error'; command?: string; message: string };
