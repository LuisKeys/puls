// Application-level bridge schema (versioned). Not a DPF API.
export const BRIDGE_VERSION = 1;

export type Command = { version: 1; type: 'ping' } | { version: 1; type: 'hello' };

export type NativeEvent =
  | { version: 1; type: 'transport'; playing: boolean; bpm: number; quarter: number }
  | { version: 1; type: 'error'; message: string }
  | { version: 1; type: 'pong' | 'restored'; counter: number };
