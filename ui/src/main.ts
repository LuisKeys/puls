import './styles.css';
import { onEvent, send } from './bridge';
import { mountBank } from './bank';
import { mountGrid } from './grid';
import { applyEvent } from './state';
import { mountToolbar } from './toolbar';

const app = document.getElementById('app')!;
app.innerHTML = '<div id="toolbar"></div><div id="bank"></div><div id="grid"></div>';
mountToolbar(document.getElementById('toolbar')!);
mountBank(document.getElementById('bank')!);
mountGrid(document.getElementById('grid')!);

// The page never takes part in native drag and drop; a stray drag would show the "+" (copy) cursor.
for (const type of ['dragstart', 'dragenter', 'dragover', 'drop'] as const)
  document.addEventListener(type, (e) => {
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'none';
  });

onEvent(applyEvent);

// Undo/redo shortcuts (only reach us while the WebView has keyboard focus).
window.addEventListener('keydown', (e) => {
  if (!(e.metaKey || e.ctrlKey) || e.altKey || (e.target as HTMLElement).tagName === 'INPUT') return;
  const k = e.key.toLowerCase();
  if (k === 'z') send({ type: e.shiftKey ? 'redo' : 'undo' });
  else if (k === 'y') send({ type: 'redo' });
  else return;
  e.preventDefault();
});
send({ type: 'hello' }); // DPF pushes restored state before the page is ready, so ask for the model explicitly
