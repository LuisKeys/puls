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

export function send(cmd: Omit<Command, 'version'>): void {
  const msg = { version: BRIDGE_VERSION, ...cmd } as Command;
  window.setState?.('cmd', JSON.stringify(msg));
}

function decode(b64: string): unknown {
  const bin = atob(b64);
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return JSON.parse(new TextDecoder().decode(bytes));
}

window.stateChanged = (key, value) => {
  try {
    const data = decode(value) as { version?: number; schemaVersion?: number; counter?: number };
    if (key === 'state' && data.schemaVersion === 1) {
      // Host-restored persisted state (not a bridge event).
      listeners.forEach((l) => l({ version: 1, type: 'restored', counter: data.counter ?? 0 }));
      return;
    }
    if (data.version !== BRIDGE_VERSION) return;
    if (key === 'evt') listeners.forEach((l) => l(data as NativeEvent));
  } catch (e) {
    console.error('bad native message', e);
  }
};
