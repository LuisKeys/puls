import { send } from './bridge';
import { openMenu } from './menu';
import { onChange, state } from './state';
import { BANK_SIZE } from './types';

// 16 pattern slots. Click views a pattern and requests it for playback (quantized by the Pattern switch
// setting while playing); Alt+click switches immediately. A dot marks the slot the sequencer is playing.
export function mountBank(el: HTMLElement): void {
  el.textContent = '';
  const slots: HTMLButtonElement[] = [];
  for (let i = 0; i < BANK_SIZE; i++) {
    const b = document.createElement('button');
    b.className = 'slot';
    b.textContent = String(i + 1);
    b.addEventListener('click', (e) =>
      send(e.altKey ? { type: 'selectPattern', index: i, quantize: 'now' } : { type: 'selectPattern', index: i }),
    );
    b.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      openPatternMenu(e.clientX, e.clientY, i);
    });
    slots.push(b);
    el.appendChild(b);
  }
  const name = document.createElement('span');
  name.className = 'pname dim';
  el.appendChild(name);

  const paint = (): void => {
    slots.forEach((b, i) => {
      const slot = state.bank[i];
      b.classList.toggle('sel', i === state.selectedPattern);
      b.classList.toggle('playing', i === state.playingPattern);
      b.classList.toggle('used', !!slot?.used);
      b.title = `${slot?.name ?? `Pattern ${i + 1}`} — click: switch (${state.settings.patternSwitch}), Alt+click: now, right-click: more`;
    });
    name.textContent = state.pattern.name;
  };
  onChange((c) => {
    if (c.kind === 'bank' || c.kind === 'rebuild' || c.kind === 'settings') paint();
  });
  paint();
}

function openPatternMenu(x: number, y: number, index: number): void {
  const slot = state.bank[index];
  openMenu(x, y, `Pattern ${index + 1}`, [
    { kind: 'text', label: 'Name', maxBytes: 15, value: slot?.name ?? '', commit: (v) => v.trim() && send({ type: 'renamePattern', index, name: v }) },
    { kind: 'action', label: 'Play now', run: () => send({ type: 'selectPattern', index, quantize: 'now' }) },
    { kind: 'action', label: 'Copy', run: () => send({ type: 'copyPattern', index }) },
    { kind: 'action', label: 'Paste (replaces this pattern)', disabled: !state.hasClipboard, run: () => send({ type: 'pastePattern', index }) },
    { kind: 'action', label: 'Duplicate to next free slot', run: () => send({ type: 'duplicatePattern', index }) },
    { kind: 'action', label: 'Clear steps', run: () => send({ type: 'clearPattern', index }) },
  ]);
}
