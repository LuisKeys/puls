import { send, nextGesture } from './bridge';
import { openStepMenu, openTrackMenu, closeMenu } from './menu';
import { onChange, select, state, trackById, type Change } from './state';
import { MAX_STEPS, type Step, type Track } from './types';

const DRAG_THRESHOLD_PX = 4;
const VELOCITY_PER_PX = 0.8;

let root: HTMLElement;
const rows = new Map<number, { row: HTMLElement; label: HTMLElement; name: HTMLElement; swatch: HTMLElement; len: HTMLInputElement; mute: HTMLElement; solo: HTMLElement; cells: HTMLElement[] }>();
const heads = new Map<number, HTMLElement>(); // track id -> cell currently carrying .play

function rgba(hex: string, a: number): string {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a.toFixed(3)})`;
}

// Velocity is shown as the track color's alpha (hue kept).
function paintCell(cell: HTMLElement, t: Track, s: number): void {
  const st: Step = t.steps[s];
  cell.classList.toggle('on', st.active);
  cell.classList.toggle('out', s >= t.length);
  cell.classList.toggle('rat', st.active && st.ratchets > 1);
  if (st.ratchets > 1) cell.dataset.r = String(st.ratchets);
  cell.classList.toggle('prob', st.active && st.probability < 100);
  cell.classList.toggle('early', st.active && st.microtiming < 0);
  cell.classList.toggle('late', st.active && st.microtiming > 0);
  cell.style.background = st.active ? rgba(t.color, 0.25 + 0.75 * (st.velocity / 127)) : '';
}

function paintRow(t: Track): void {
  const r = rows.get(t.id);
  if (!r) return;
  r.name.textContent = t.name;
  r.swatch.style.background = t.color;
  if (document.activeElement !== r.len) r.len.value = String(t.length);
  r.row.classList.toggle('muted', t.mute);
  r.mute.classList.toggle('active', t.mute);
  r.solo.classList.toggle('active', t.solo);
  for (let s = 0; s < MAX_STEPS; s++) paintCell(r.cells[s], t, s);
}

function paintSelection(): void {
  for (const [id, r] of rows) r.row.classList.toggle('selected', id === state.selected);
}

function rebuild(): void {
  root.textContent = '';
  rows.clear();
  heads.clear();
  closeMenu();
  const frag = document.createDocumentFragment();
  for (const t of state.pattern.tracks) {
    const row = document.createElement('div');
    row.className = 'row';
    row.dataset.t = String(t.id);

    const label = document.createElement('div');
    label.className = 'label';
    const swatch = document.createElement('span');
    swatch.className = 'swatch';
    const name = document.createElement('span');
    name.className = 'name';
    const len = document.createElement('input');
    len.className = 'len';
    len.type = 'number';
    len.min = '1';
    len.max = String(MAX_STEPS);
    len.title = 'Steps';
    len.addEventListener('change', () => {
      const v = Math.min(MAX_STEPS, Math.max(1, Math.round(Number(len.value)) || 1));
      len.value = String(v);
      send({ type: 'modifyTrack', track: t.id, props: { length: v } });
    });
    const mkBtn = (txt: string, title: string, prop: 'mute' | 'solo'): HTMLButtonElement => {
      const b = document.createElement('button');
      b.className = 'ms';
      b.textContent = txt;
      b.title = title;
      b.addEventListener('click', () => {
        const cur = trackById(t.id);
        if (cur) send({ type: 'modifyTrack', track: t.id, props: { [prop]: !cur[prop] } });
      });
      return b;
    };
    const mute = mkBtn('M', 'Mute', 'mute');
    const solo = mkBtn('S', 'Solo', 'solo');
    label.append(swatch, name, mute, solo, len);

    const cellsEl = document.createElement('div');
    cellsEl.className = 'cells';
    const cells: HTMLElement[] = [];
    for (let s = 0; s < MAX_STEPS; s++) {
      const c = document.createElement('div');
      c.className = s % 4 === 0 ? 'cell beat' : 'cell';
      c.dataset.t = String(t.id);
      c.dataset.s = String(s);
      cells.push(c);
      cellsEl.appendChild(c);
    }
    row.append(label, cellsEl);
    frag.appendChild(row);
    rows.set(t.id, { row, label, name, swatch, len, mute, solo, cells });
    paintRow(t);
  }
  root.appendChild(frag);
  paintSelection();
  schedulePlayheads();
}

// ---- playheads: one class toggle per track, applied in a rAF that only runs while an update is pending ----
let headRaf = 0;
function schedulePlayheads(): void {
  if (headRaf) return;
  headRaf = requestAnimationFrame(() => {
    headRaf = 0;
    state.pattern.tracks.forEach((t, i) => {
      const r = rows.get(t.id);
      const pos = state.playheads[i] ?? -1;
      const next = r && pos >= 0 ? r.cells[pos] : undefined;
      const prev = heads.get(t.id);
      if (prev === next) return;
      prev?.classList.remove('play');
      next?.classList.add('play');
      if (next) heads.set(t.id, next);
      else heads.delete(t.id);
    });
  });
}

// ---- pointer gestures: click toggles, vertical drag past a threshold edits velocity ----
interface Gesture {
  id: number;
  step: number;
  pointer: number;
  startY: number;
  startVel: number;
  wasActive: boolean;
  dragging: boolean;
  gestureId: number;
}
let gesture: Gesture | null = null;
let velRaf = 0;
let pendingVel: { id: number; step: number; velocity: number; gesture: number } | null = null;

function flushVelocity(): void {
  velRaf = 0;
  if (!pendingVel) return;
  const p = pendingVel;
  pendingVel = null;
  send({ type: 'setStepVelocity', track: p.id, step: p.step, velocity: p.velocity, gesture: p.gesture });
}

function onPointerDown(e: PointerEvent): void {
  const el = (e.target as HTMLElement).closest<HTMLElement>('.cell');
  if (el) {
    if (e.button !== 0 || e.ctrlKey || gesture) return; // ctrl+click is a context click on macOS
    const id = Number(el.dataset.t);
    const step = Number(el.dataset.s);
    const t = trackById(id);
    if (!t) return;
    select(id);
    gesture = { id, step, pointer: e.pointerId, startY: e.clientY, startVel: t.steps[step].velocity, wasActive: t.steps[step].active, dragging: false, gestureId: nextGesture() };
    root.setPointerCapture(e.pointerId);
    return;
  }
  const label = (e.target as HTMLElement).closest<HTMLElement>('.row');
  if (label && !(e.target as HTMLElement).closest('input,button')) select(Number(label.dataset.t));
}

function onPointerMove(e: PointerEvent): void {
  const g = gesture;
  if (!g || e.pointerId !== g.pointer) return;
  const dy = g.startY - e.clientY; // up = louder
  if (!g.dragging) {
    if (!g.wasActive || Math.abs(dy) <= DRAG_THRESHOLD_PX) return;
    g.dragging = true;
  }
  const t = trackById(g.id);
  if (!t) return;
  const v = Math.min(127, Math.max(1, Math.round(g.startVel + dy * VELOCITY_PER_PX)));
  if (v === t.steps[g.step].velocity) return;
  t.steps[g.step].velocity = v; // optimistic
  paintCell(rows.get(g.id)!.cells[g.step], t, g.step);
  pendingVel = { id: g.id, step: g.step, velocity: v, gesture: g.gestureId };
  if (!velRaf) velRaf = requestAnimationFrame(flushVelocity);
}

function onPointerUp(e: PointerEvent): void {
  const g = gesture;
  if (!g || e.pointerId !== g.pointer) return;
  gesture = null;
  if (velRaf) cancelAnimationFrame(velRaf);
  flushVelocity();
  if (g.dragging) return;
  const t = trackById(g.id);
  if (!t) return;
  t.steps[g.step].active = !t.steps[g.step].active; // optimistic; the stepChanged echo confirms it
  paintCell(rows.get(g.id)!.cells[g.step], t, g.step);
  send({ type: 'toggleStep', track: g.id, step: g.step });
}

function onPointerCancel(e: PointerEvent): void {
  if (gesture && e.pointerId === gesture.pointer) {
    if (velRaf) cancelAnimationFrame(velRaf);
    flushVelocity();
    gesture = null;
  }
}

export function mountGrid(el: HTMLElement): void {
  root = el;
  root.addEventListener('pointerdown', onPointerDown);
  root.addEventListener('pointermove', onPointerMove);
  root.addEventListener('pointerup', onPointerUp);
  root.addEventListener('pointercancel', onPointerCancel);
  root.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const cell = (e.target as HTMLElement).closest<HTMLElement>('.cell');
    const row = (e.target as HTMLElement).closest<HTMLElement>('.row');
    if (!row) return;
    const id = Number(row.dataset.t);
    select(id);
    if (cell) openStepMenu(e.clientX, e.clientY, id, Number(cell.dataset.s));
    else openTrackMenu(e.clientX, e.clientY, id);
  });
  onChange((c: Change) => {
    switch (c.kind) {
      case 'rebuild': rebuild(); break;
      case 'track': { const t = trackById(c.id); if (t) paintRow(t); break; }
      case 'step': {
        // Don't let an echo of an older value fight a drag that is still in progress on this cell.
        if (gesture?.dragging && gesture.id === c.id && gesture.step === c.step) break;
        const t = trackById(c.id);
        const r = rows.get(c.id);
        if (t && r) paintCell(r.cells[c.step], t, c.step);
        break;
      }
      case 'selection': paintSelection(); break;
      case 'playheads': schedulePlayheads(); break;
    }
  });
}
