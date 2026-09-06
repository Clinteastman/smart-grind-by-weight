// Execute the embedded production save handler with controlled API/DOM doubles.
// This checks behaviour, not browser layout or real firmware HTTP routing.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const page = readFileSync(new URL('../../src/network/device_page.h', import.meta.url), 'utf8');
const script = page.split('<script>')[1].split('</script>')[0];
new vm.Script(script); // Syntax-check the complete embedded application.
const save = script.slice(script.indexOf('let settingsSavePending=false;'),
  script.indexOf("$('chooseScreensaver').onclick"));
assert.ok(save.includes('waitForSettingsSave'));

function harness(results, reloadError = false) {
  const elements = new Map();
  const messages = [], calls = [];
  let now = 0, reloads = 0;
  const get = id => {
    if (!elements.has(id)) elements.set(id, {
      disabled: false, textContent: '', attributes: {},
      setAttribute(k, v) { this.attributes[k] = v; },
      removeAttribute(k) { delete this.attributes[k]; }
    });
    return elements.get(id);
  };
  const context = vm.createContext({
    $: get, AbortController, performance: {now: () => now},
    // Advance only polling delays; request watchdogs are tested separately.
    setTimeout(fn, delay) { if (delay <= 250) { now += delay; queueMicrotask(fn); } return 1; },
    clearTimeout() {}, settingsBody: () => 'brightness_percent=50',
    toast: (message, error = false) => messages.push({message, error}),
    async loadSettings() { reloads++; if (reloadError) throw Error('reload failed'); },
    async api(url, options) {
      calls.push({url, options});
      if (options?.method === 'POST') return {accepted: true, request_id: 7};
      const result = results.length > 1 ? results.shift() : results[0];
      if (result instanceof Error) throw result;
      return typeof result === 'string' ? {request_id: 7, status: result} : result;
    }
  });
  vm.runInContext(save, context);
  return {context, get, messages, calls, reloads: () => reloads,
    submit: () => get('settingsForm').onsubmit({preventDefault() {}})};
}

for (const status of ['saved', 'failed', 'busy']) {
  const h = harness(['pending', status]);
  const pending = h.submit();
  assert.equal(h.get('saveSettings').disabled, true);
  assert.equal(h.get('settingsForm').inert, true, 'prevent edits while reloading saved values');
  assert.equal(h.get('settingsPending').textContent, 'Saving on the grinder…');
  assert.equal(h.messages.length, 0, 'must not announce success on acceptance');
  await h.submit(); // Duplicate dispatch must not create another POST.
  await pending;
  assert.equal(h.calls.filter(c => c.options?.method === 'POST').length, 1);
  assert.equal(h.get('saveSettings').disabled, false);
  assert.equal(h.get('reloadSettings').disabled, false);
  assert.equal(h.get('settingsForm').inert, false);
  assert.equal(h.get('settingsPending').textContent, '');
  assert.equal(h.get('settingsForm').attributes['aria-busy'], undefined);
  assert.equal(h.messages.at(-1).error, status !== 'saved');
  assert.equal(h.reloads(), status === 'busy' ? 0 : 1);
}

for (const result of [Error('HTTP 404'), {request_id: 8, status: 'saved'},
    {request_id: 7, status: 'unexpected'}, 'pending']) {
  const h = harness([result]);
  await h.submit();
  assert.equal(h.messages.at(-1).error, true);
  assert.equal(h.reloads(), 0);
  assert.equal(h.get('saveSettings').disabled, false);
  assert.equal(h.calls.filter(c => c.options?.method === 'POST').length, 1,
    'an uncertain save must never automatically resubmit');
}

for (const status of ['saved', 'failed']) {
  const h = harness([status], true);
  await h.submit();
  assert.equal(h.messages.at(-1).error, true);
  assert.match(h.messages.at(-1).message, status === 'saved' ? /were saved/ : /may have changed/);
}

const h = harness(['saved']);
await assert.rejects(vm.runInContext('waitForSettingsSave(undefined)', h.context), /cannot confirm/);
await assert.rejects(vm.runInContext('waitForSettingsSave(0)', h.context), /cannot confirm/);

// Use a real abortable pending request to test the production request deadline.
const stalled = harness(['saved']);
stalled.context.setTimeout = setTimeout;
stalled.context.clearTimeout = clearTimeout;
stalled.context.api = (_url, {signal}) => new Promise((_resolve, reject) => {
  signal.addEventListener('abort', () => reject(Object.assign(Error('aborted'), {name: 'AbortError'})));
});
await assert.rejects(vm.runInContext("settingsApi('/api/v1/settings', {}, 5)", stalled.context), /timed out.*may have changed/);
// A lost POST response must not leave the form locked or announce success.
stalled.context.api = async () => { throw Object.assign(Error('aborted'), {name: 'AbortError'}); };
await stalled.submit();
assert.equal(stalled.get('settingsForm').inert, false);
assert.equal(stalled.get('saveSettings').disabled, false);
assert.equal(stalled.messages.at(-1).error, true);
assert.match(stalled.messages.at(-1).message, /may have changed/);
console.log('Settings web save tests passed (production handler; API/DOM doubles).');
