import { send } from './bridge';
import { onChange, selectedTrack, state } from './state';
import { MAX_TRACKS, RESOLUTIONS, type Resolution, type SwitchQuantize } from './types';

const $ = <T extends HTMLElement>(root: HTMLElement, sel: string): T => root.querySelector<T>(sel)!;

export function mountToolbar(el: HTMLElement): void {
  el.innerHTML = `
    <button class="undo" title="Undo (Ctrl/Cmd+Z)">↶</button>
    <button class="redo" title="Redo (Shift+Ctrl/Cmd+Z)">↷</button>
    <span class="sep"></span>
    <button class="enable" title="Enable / disable the sequencer">On</button>
    <span class="pattern dim"></span>
    <span class="sep"></span>
    <span class="sel dim"></span>
    <label>Res <select class="res"></select></label>
    <label>Swing <input class="swing" type="range" min="0" max="75" step="1"><output class="swingv">0</output></label>
    <span class="sep"></span>
    <span class="tracks">Tracks <b class="count">0</b></span>
    <button class="rm" title="Remove selected track">−</button>
    <button class="add" title="Add track">+</button>
    <span class="confirm" hidden><span class="confirmmsg"></span> <button class="danger yes">Remove</button> <button class="no">Cancel</button></span>
    <span class="sep"></span>
    <button class="gear" title="Settings">⚙</button>
    <span class="spacer"></span>
    <span class="transport dim">stopped</span>
    <div class="settings" hidden>
      <label>Step grid anchor
        <select class="anchor">
          <option value="restart">Restart where playback starts</option>
          <option value="host">Follow song position</option>
        </select>
      </label>
      <label>Pattern switch
        <select class="pswitch">
          <option value="bar">At the next bar</option>
          <option value="beat">At the next beat</option>
          <option value="now">Immediately</option>
        </select>
      </label>
    </div>
    <div class="status" hidden></div>`;

  const res = $<HTMLSelectElement>(el, '.res');
  for (const r of RESOLUTIONS) res.add(new Option(r, r));
  const swing = $<HTMLInputElement>(el, '.swing');
  const swingv = $<HTMLOutputElement>(el, '.swingv');
  const settings = $(el, '.settings');
  const status = $(el, '.status');
  const confirmEl = $(el, '.confirm');
  let confirmTrack = 0;
  let confirmTimer = 0;
  const hideConfirm = (): void => {
    confirmTrack = 0;
    clearTimeout(confirmTimer);
    confirmEl.hidden = true;
  };

  $(el, '.undo').addEventListener('click', () => send({ type: 'undo' }));
  $(el, '.redo').addEventListener('click', () => send({ type: 'redo' }));

  $(el, '.enable').addEventListener('click', () => send({ type: 'setSettings', settings: { enabled: !state.settings.enabled } }));
  $(el, '.add').addEventListener('click', () => send({ type: 'addTrack' }));
  $(el, '.rm').addEventListener('click', () => {
    // window.confirm() is unreliable in embedded WebViews, so confirm inline.
    const t = selectedTrack();
    if (!t) return;
    confirmTrack = t.id;
    $(el, '.confirmmsg').textContent = `Remove "${t.name}"?`;
    confirmEl.hidden = false;
    clearTimeout(confirmTimer);
    confirmTimer = window.setTimeout(hideConfirm, 6000);
  });
  $(el, '.yes').addEventListener('click', () => {
    if (confirmTrack) send({ type: 'removeTrack', track: confirmTrack });
    hideConfirm();
  });
  $(el, '.no').addEventListener('click', hideConfirm);
  $(el, '.gear').addEventListener('click', () => (settings.hidden = !settings.hidden));
  res.addEventListener('change', () => {
    const t = selectedTrack();
    if (t) send({ type: 'modifyTrack', track: t.id, props: { resolution: res.value as Resolution } });
  });
  swing.addEventListener('input', () => (swingv.textContent = swing.value));
  swing.addEventListener('change', () => {
    const t = selectedTrack();
    if (t) send({ type: 'modifyTrack', track: t.id, props: { swing: Number(swing.value) } });
  });
  $<HTMLSelectElement>(el, '.anchor').addEventListener('change', (e) =>
    send({ type: 'setSettings', settings: { anchorMode: (e.target as HTMLSelectElement).value as 'restart' | 'host' } }),
  );

  $<HTMLSelectElement>(el, '.pswitch').addEventListener('change', (e) =>
    send({ type: 'setSettings', settings: { patternSwitch: (e.target as HTMLSelectElement).value as SwitchQuantize } }),
  );

  let statusTimer = 0;
  const refresh = (): void => {
    const t = selectedTrack();
    const n = state.pattern.tracks.length;
    ($(el, '.undo') as HTMLButtonElement).disabled = !state.canUndo;
    ($(el, '.redo') as HTMLButtonElement).disabled = !state.canRedo;
    if (confirmTrack && (!t || t.id !== confirmTrack)) hideConfirm();
    $(el, '.enable').classList.toggle('active', state.settings.enabled);
    $(el, '.pattern').textContent = state.pattern.name;
    $(el, '.count').textContent = String(n);
    ($(el, '.add') as HTMLButtonElement).disabled = n >= MAX_TRACKS;
    ($(el, '.rm') as HTMLButtonElement).disabled = n <= 1;
    $(el, '.sel').textContent = t ? t.name : '';
    if (t) {
      res.value = t.resolution;
      if (document.activeElement !== swing) swing.value = String(t.swing);
      swingv.textContent = String(t.swing);
    }
    $<HTMLSelectElement>(el, '.anchor').value = state.settings.anchorMode;
    $<HTMLSelectElement>(el, '.pswitch').value = state.settings.patternSwitch;
  };

  onChange((c) => {
    if (c.kind === 'transport') {
      $(el, '.transport').textContent = `${state.playing ? 'playing' : 'stopped'}  ${state.bpm.toFixed(1)} bpm`;
    } else if (c.kind === 'error') {
      status.textContent = c.message;
      status.hidden = false;
      clearTimeout(statusTimer);
      statusTimer = window.setTimeout(() => (status.hidden = true), 4000);
      send({ type: 'hello' }); // the page may have diverged from the model: resync
    } else {
      refresh();
    }
  });
}
