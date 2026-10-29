// In-plugin help overlay. Plain DOM; closes with Esc, a click outside the panel, or the close button.
const SECTIONS: { title: string; rows: [string, string][] }[] = [
  {
    title: 'Steps',
    rows: [
      ['Click', 'Toggle a step'],
      ['Drag up / down on an active step', 'Set its velocity (brighter = louder)'],
      ['Right-click or Ctrl+click', 'Step menu: probability, ratchets, micro-timing, gate, clear'],
      ['Markers', 'Number = ratchets, dashed top = probability < 100 %, side bar = early / late'],
    ],
  },
  {
    title: 'Tracks',
    rows: [
      ['Click a label', 'Select the track (toolbar Res / Swing act on it)'],
      ['M / S', 'Mute / solo'],
      ['Length field', 'Steps before the track loops (1-64); different lengths make polyrhythms'],
      ['Right-click a label', 'Name, color, MIDI note, channel, resolution, length, phase, swing, scaling'],
      ['Toolbar + / −', 'Add a track / remove the selected one (asks first)'],
    ],
  },
  {
    title: 'Patterns',
    rows: [
      ['Click a slot (1-16)', 'View and play it; while playing the switch waits for the next bar (see ⚙)'],
      ['Alt+click a slot', 'Switch immediately'],
      ['Right-click a slot', 'Rename, play now, copy, paste, duplicate, clear'],
      ['Green dot', 'The pattern that is actually playing'],
    ],
  },
  {
    title: 'Keyboard (when the plugin window has focus)',
    rows: [
      ['Arrows / Shift+arrows', 'Move the cursor / extend the selection (Shift+click also extends)'],
      ['Space or Enter', 'Toggle the selected steps'],
      ['Delete or Backspace', 'Clear the selected steps'],
      ['Ctrl/Cmd + C, X, V', 'Copy, cut, paste a block of steps at the selection'],
      ['Ctrl/Cmd + Z, Shift+Z (or Y)', 'Undo / redo'],
      ['Esc / ?', 'Collapse the selection / open or close this help'],
    ],
  },
  {
    title: 'Tips',
    rows: [
      ['Where to put it', 'On a track before an instrument (note FX); it only makes notes while the host plays'],
      ['Polyrhythm', 'Give tracks different lengths or resolutions (e.g. 16 steps vs 12 steps vs 1/8T)'],
      ['Groove', 'Swing delays odd steps; micro-timing nudges single steps by up to ±50 %'],
      ['Phase', 'Phase shifts where in its loop a track starts'],
      ['Closing the window', 'Never interrupts playback; the pattern is saved with the project'],
    ],
  },
];

let overlay: HTMLElement | null = null;

export const isHelpOpen = (): boolean => overlay !== null;

export function closeHelp(): void {
  overlay?.remove();
  overlay = null;
  document.removeEventListener('keydown', onKey, true);
}

function onKey(e: KeyboardEvent): void {
  if (e.key === 'Escape') {
    e.stopPropagation();
    closeHelp();
  }
}

export function toggleHelp(): void {
  if (overlay) return closeHelp();
  overlay = document.createElement('div');
  overlay.className = 'help';
  overlay.addEventListener('pointerdown', (e) => {
    if (e.target === overlay) closeHelp();
  });

  const panel = document.createElement('div');
  panel.className = 'helppanel';
  const head = document.createElement('div');
  head.className = 'helphead';
  const title = document.createElement('span');
  title.textContent = 'Puls help';
  const close = document.createElement('button');
  close.textContent = '×';
  close.title = 'Close (Esc)';
  close.addEventListener('click', closeHelp);
  head.append(title, close);
  panel.appendChild(head);

  for (const sec of SECTIONS) {
    const h = document.createElement('div');
    h.className = 'helph';
    h.textContent = sec.title;
    panel.appendChild(h);
    for (const [k, v] of sec.rows) {
      const row = document.createElement('div');
      row.className = 'helprow';
      const key = document.createElement('span');
      key.className = 'helpkey';
      key.textContent = k;
      const val = document.createElement('span');
      val.textContent = v;
      row.append(key, val);
      panel.appendChild(row);
    }
  }
  overlay.appendChild(panel);
  document.body.appendChild(overlay);
  document.addEventListener('keydown', onKey, true);
}
