import './styles.css';
import { onEvent, send } from './bridge';
import { mountGrid } from './grid';
import { applyEvent } from './state';
import { mountToolbar } from './toolbar';

const app = document.getElementById('app')!;
app.innerHTML = '<div id="toolbar"></div><div id="grid"></div>';
mountToolbar(document.getElementById('toolbar')!);
mountGrid(document.getElementById('grid')!);

onEvent(applyEvent);
send({ type: 'hello' }); // DPF pushes restored state before the page is ready, so ask for the model explicitly
