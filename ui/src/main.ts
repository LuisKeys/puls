import './styles.css';
import { onEvent, send } from './bridge';

const app = document.getElementById('app')!;
app.innerHTML = `
  <div><button id="ping">ping</button> <span id="counter" class="dim">counter: -</span></div>
  <div id="transport" class="dim">transport: -</div>`;

const counter = document.getElementById('counter')!;
const transport = document.getElementById('transport')!;

document.getElementById('ping')!.addEventListener('click', () => send({ type: 'ping' }));
send({ type: 'hello' });

onEvent((ev) => {
  if (ev.type === 'pong' || ev.type === 'restored') counter.textContent = `counter: ${ev.counter}`;
  else if (ev.type === 'transport')
    transport.textContent = `${ev.playing ? 'playing' : 'stopped'}  ${ev.bpm.toFixed(1)} bpm  q=${ev.quarter.toFixed(3)}`;
  else if (ev.type === 'error') transport.textContent = `error: ${ev.message}`;
});
