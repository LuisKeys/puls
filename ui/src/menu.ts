import { send, nextGesture } from './bridge';
import { DEFAULT_STEP, trackById } from './state';
import { MAX_STEPS, RESOLUTIONS, type Resolution, type Step, type TrackProps } from './types';

// A small popover of labelled controls. Each control commits through `commit(value, gesture)`; the gesture id
// stays the same while one control is being dragged/typed in, so the native side records a single undo step.
type Field =
  | { kind: 'range'; label: string; min: number; max: number; value: number; unit?: string; commit: (v: number, g: number) => void }
  | { kind: 'text'; label: string; maxBytes: number; value: string; commit: (v: string, g: number) => void }
  | { kind: 'color'; label: string; value: string; commit: (v: string, g: number) => void }
  | { kind: 'select'; label: string; options: readonly string[]; value: string; commit: (v: string, g: number) => void }
  | { kind: 'check'; label: string; value: boolean; commit: (v: boolean, g: number) => void };

let menu: HTMLElement | null = null;

export function closeMenu(): void {
  menu?.remove();
  menu = null;
  document.removeEventListener('pointerdown', onOutside, true);
  document.removeEventListener('keydown', onKey, true);
}

function onOutside(e: Event): void {
  if (menu && !menu.contains(e.target as Node)) closeMenu();
}
function onKey(e: KeyboardEvent): void {
  if (e.key === 'Escape') closeMenu();
}

function row(label: string, control: HTMLElement[]): HTMLElement {
  const r = document.createElement('label');
  r.className = 'mrow';
  const l = document.createElement('span');
  l.textContent = label;
  r.append(l, ...control);
  return r;
}

function build(f: Field): HTMLElement {
  let g = nextGesture();
  const renew = (): void => void (g = nextGesture());
  switch (f.kind) {
    case 'range': {
      const range = document.createElement('input');
      range.type = 'range';
      const num = document.createElement('input');
      num.type = 'number';
      for (const i of [range, num]) {
        i.min = String(f.min);
        i.max = String(f.max);
        i.value = String(f.value);
      }
      const apply = (src: HTMLInputElement): void => {
        const v = Math.min(f.max, Math.max(f.min, Math.round(Number(src.value))));
        if (!Number.isFinite(v) || src.value === '') return;
        range.value = num.value = String(v);
        f.commit(v, g);
      };
      range.addEventListener('input', () => apply(range));
      num.addEventListener('input', () => apply(num));
      for (const i of [range, num]) i.addEventListener('change', renew);
      return row(f.label, [range, num]);
    }
    case 'text': {
      const t = document.createElement('input');
      t.type = 'text';
      t.value = f.value;
      t.addEventListener('input', () => {
        // The native side limits names to 15 bytes of UTF-8.
        while (new TextEncoder().encode(t.value).length > f.maxBytes) t.value = t.value.slice(0, -1);
      });
      t.addEventListener('change', () => f.commit(t.value, g));
      return row(f.label, [t]);
    }
    case 'color': {
      const c = document.createElement('input');
      c.type = 'color';
      c.value = f.value;
      c.addEventListener('input', () => f.commit(c.value, g));
      c.addEventListener('change', renew);
      return row(f.label, [c]);
    }
    case 'select': {
      const s = document.createElement('select');
      for (const o of f.options) s.add(new Option(o, o));
      s.value = f.value;
      s.addEventListener('change', () => f.commit(s.value, nextGesture()));
      return row(f.label, [s]);
    }
    case 'check': {
      const c = document.createElement('input');
      c.type = 'checkbox';
      c.checked = f.value;
      c.addEventListener('change', () => f.commit(c.checked, nextGesture()));
      return row(f.label, [c]);
    }
  }
}

function open(x: number, y: number, title: string, fields: Field[], footer?: HTMLElement): void {
  closeMenu();
  const el = document.createElement('div');
  el.className = 'menu';
  const h = document.createElement('div');
  h.className = 'mtitle';
  h.textContent = title;
  el.append(h, ...fields.map(build));
  if (footer) el.append(footer);
  document.body.appendChild(el);
  // Keep the popover inside the viewport.
  const r = el.getBoundingClientRect();
  el.style.left = `${Math.max(4, Math.min(x, window.innerWidth - r.width - 4))}px`;
  el.style.top = `${Math.max(4, Math.min(y, window.innerHeight - r.height - 4))}px`;
  menu = el;
  document.addEventListener('pointerdown', onOutside, true);
  document.addEventListener('keydown', onKey, true);
}

export function openStepMenu(x: number, y: number, trackId: number, stepIdx: number): void {
  const t = trackById(trackId);
  if (!t) return;
  const st = t.steps[stepIdx];
  const mod = (props: Partial<Step>, g: number): void =>
    send({ type: 'modifyStep', track: trackId, step: stepIdx, props, gesture: g });
  const reset = document.createElement('button');
  reset.textContent = 'Clear step';
  reset.addEventListener('click', () => {
    send({ type: 'modifyStep', track: trackId, step: stepIdx, props: { ...DEFAULT_STEP } });
    closeMenu();
  });
  open(x, y, `${t.name} · step ${stepIdx + 1}`, [
    { kind: 'check', label: 'Active', value: st.active, commit: (v, g) => mod({ active: v }, g) },
    { kind: 'range', label: 'Velocity', min: 1, max: 127, value: st.velocity, commit: (v, g) => mod({ velocity: v }, g) },
    { kind: 'range', label: 'Probability %', min: 0, max: 100, value: st.probability, commit: (v, g) => mod({ probability: v }, g) },
    { kind: 'range', label: 'Ratchets', min: 1, max: 8, value: st.ratchets, commit: (v, g) => mod({ ratchets: v }, g) },
    { kind: 'range', label: 'Micro-timing %', min: -50, max: 50, value: st.microtiming, commit: (v, g) => mod({ microtiming: v }, g) },
    { kind: 'range', label: 'Gate %', min: 1, max: 100, value: st.gate, commit: (v, g) => mod({ gate: v }, g) },
  ], reset);
}

export function openTrackMenu(x: number, y: number, trackId: number): void {
  const t = trackById(trackId);
  if (!t) return;
  const mod = (props: Partial<TrackProps>, g: number): void => send({ type: 'modifyTrack', track: trackId, props, gesture: g });
  const range = (label: string, key: keyof TrackProps, min: number, max: number, offset = 0): Field => ({
    kind: 'range', label, min: min + offset, max: max + offset, value: (t[key] as number) + offset,
    commit: (v, g) => mod({ [key]: v - offset }, g),
  });
  open(x, y, t.name, [
    { kind: 'text', label: 'Name', maxBytes: 15, value: t.name, commit: (v, g) => v.trim() && mod({ name: v }, g) },
    { kind: 'color', label: 'Color', value: t.color, commit: (v, g) => mod({ color: v }, g) },
    range('MIDI note', 'note', 0, 127),
    range('Channel', 'channel', 0, 15, 1),
    { kind: 'select', label: 'Resolution', options: RESOLUTIONS, value: t.resolution, commit: (v, g) => mod({ resolution: v as Resolution }, g) },
    range('Length', 'length', 1, MAX_STEPS),
    range('Phase', 'phase', 0, MAX_STEPS - 1),
    range('Velocity %', 'velocityPct', 0, 200),
    range('Probability %', 'probabilityPct', 0, 100),
    range('Swing %', 'swing', 0, 75),
    range('Duration %', 'durationPct', 1, 100),
    { kind: 'check', label: 'Mute', value: t.mute, commit: (v, g) => mod({ mute: v }, g) },
    { kind: 'check', label: 'Solo', value: t.solo, commit: (v, g) => mod({ solo: v }, g) },
  ]);
}
