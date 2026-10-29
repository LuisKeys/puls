import { BRIDGE_VERSION, type Command, type NativeEvent } from './types';

// DPF injects global setState(key, value) into the page and calls window.stateChanged(key, value).
declare global {
  interface Window {
    setState?: (key: string, value: string) => void;
    stateChanged?: (key: string, value: string) => void;
  }
}

type Listener = (ev: NativeEvent) => void;
const listeners: Listener[] = [];

export function onEvent(fn: Listener): void {
  listeners.push(fn);
}

type Body<C> = C extends { version: 1 } ? Omit<C, 'version'> : never;

export function send(cmd: Body<Command>): void {
  const msg = { version: BRIDGE_VERSION, ...cmd } as Command;
  window.setState?.('cmd', JSON.stringify(msg));
}

// Ids that tag a continuous edit (slider drag, cell drag) so the native side records one undo step for it.
let gestureSeq = 0;
export const nextGesture = (): number => ++gestureSeq;

function decode(b64: string): unknown {
  const bin = atob(b64);
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return JSON.parse(new TextDecoder().decode(bytes));
}

// Every native->page payload is base64 JSON (DPF does not escape what it injects).
window.stateChanged = (key, value) => {
  if (key !== 'evt') return;
  try {
    const data = decode(value) as { version?: number };
    if (data.version === BRIDGE_VERSION) listeners.forEach((l) => l(data as NativeEvent));
  } catch (e) {
    console.error('bad native message', e);
  }
};
