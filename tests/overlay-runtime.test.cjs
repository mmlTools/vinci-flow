const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

// Run the complete lt.js emitted by the C++ regression executable.
const script = fs.readFileSync(process.argv[2], 'utf8');
const flush = () => new Promise(resolve => setImmediate(resolve));

function page(readyState = 'loading') {
  const intervals = [];
  const listeners = [];
  const requests = [];
  const classes = new Set();
  const root = {
    id: 'lt_test', dataset: {}, style: {},
    classList: {
      contains: name => classes.has(name),
      add: name => classes.add(name),
      remove: name => classes.delete(name),
    },
    querySelectorAll: () => [],
  };
  const document = {
    readyState,
    getElementById: () => {
      assert.notEqual(document.readyState, 'loading', 'template ran before DOM was ready');
      return root;
    },
    querySelectorAll: () => [root],
    addEventListener: (name, callback, options) => listeners.push({ name, callback, options }),
  };
  const context = vm.createContext({
    document, console, setTimeout,
    setInterval: (callback, ms) => intervals.push({ callback, ms }),
    fetch: url => new Promise((resolve, reject) => requests.push({ url, resolve, reject })),
  });
  context.window = context;
  return {
    root, intervals, listeners, requests,
    run: () => vm.runInContext(script, context),
    ready: () => {
      document.readyState = 'interactive';
      for (const listener of listeners.splice(0)) {
        assert.equal(listener.name, 'DOMContentLoaded');
        assert.equal(listener.options.once, true);
        listener.callback();
      }
    },
    tick: () => intervals.find(interval => interval.ms === 350).callback(),
  };
}

test('repeated execution before and after DOM readiness initializes once', () => {
  const p = page();
  for (let i = 0; i < 100; ++i) p.run();
  assert.equal(p.listeners.length, 1);
  assert.equal(p.intervals.length, 0);
  assert.equal(p.requests.length, 0);
  p.ready();
  for (let i = 0; i < 100; ++i) p.run();
  assert.equal(p.root.initializations, 1);
  assert.equal(p.intervals.length, 2);
  assert.equal(p.requests.length, 2);
});

for (const state of ['interactive', 'complete']) {
  test(`late script execution initializes in ${state} state`, () => {
    const p = page(state);
    p.run();
    p.run();
    assert.equal(p.root.initializations, 1);
    assert.equal(p.intervals.length, 2);
    assert.equal(p.listeners.length, 0);
  });
}

test('slow visibility reads do not overlap, then show and hide still work', async () => {
  const p = page('complete');
  p.run();
  for (let i = 0; i < 100; ++i) p.tick();
  assert.equal(p.requests.length, 2);
  p.requests[0].resolve({ json: async () => ['lt_test'] });
  p.requests[1].resolve({ ok: true, text: async () => '{}' });
  await flush();
  assert.equal(p.root.style.display, 'block');
  p.tick();
  assert.equal(p.requests.length, 3);
  p.requests[2].resolve({ json: async () => [] });
  await flush();
  assert.equal(p.root.style.display, 'none');
});

test('failed and malformed visibility reads release the polling lock', async () => {
  const p = page('complete');
  p.run();
  p.requests[0].reject(new Error('temporarily unavailable'));
  await flush();
  p.tick();
  assert.equal(p.requests.length, 3);
  p.requests[2].resolve({ json: async () => ({ invalid: true }) });
  await flush();
  p.tick();
  assert.equal(p.requests.length, 4);
  p.requests[3].resolve({ json: async () => ['lt_test'] });
  await flush();
  assert.equal(p.root.style.display, 'block');
});
