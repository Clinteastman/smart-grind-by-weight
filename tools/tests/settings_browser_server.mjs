// Local-only UI fixture: serves the actual embedded page, never a real grinder.
// Run with Node, then visit http://127.0.0.1:8765/#settings.
// Select ?scenario=failed, busy, pending, or reload-error before #settings.
import {createServer} from 'node:http';
import {readFileSync} from 'node:fs';

const source = readFileSync(new URL('../../src/network/device_page.h', import.meta.url), 'utf8');
const page = source.split('R"WEB(')[1].split(')WEB";')[0];
let scenario = 'saved', submitted = false, polls = 0;
const settings = {
  current_profile: 1, grind_mode: 'weight', profiles: [9, 18, 20].map(weight => ({weight, time: 8})),
  automatic: {start: false, return: false, threshold_g: 50}, swipe_enabled: false,
  logging_enabled: true, purge: {mode: 0, amount_g: 1, freshness_hours: 4},
  motor_latency_ms: 50, coast_ratio: 1,
  display: {brightness: 70, screensaver_brightness: 35, screensaver_startup: true,
    screensaver_sleep: true, display_off_enabled: false, screensaver_idle_timeout_s: 60,
    display_off_delay_s: 300, screensaver_startup_timeout_s: 3,
    gaggimate_host: 'gaggimate.local', has_custom_screensaver: false, screensaver_style: 'orbit'},
  bluetooth_startup: true
};
const server = createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  const json = (body, code = 200) => {
    res.writeHead(code, {'Content-Type': 'application/json', 'Cache-Control': 'no-store'});
    res.end(JSON.stringify(body));
  };
  if (url.pathname === '/') {
    scenario = url.searchParams.get('scenario') || 'saved'; submitted = false; polls = 0;
    res.writeHead(200, {'Content-Type': 'text/html', 'Cache-Control': 'no-store'});
    return res.end(page);
  }
  if (url.pathname === '/api/v1/settings' && req.method === 'POST') {
    let body = ''; for await (const chunk of req) body += chunk;
    submitted = true; polls = 0;
    if (scenario === 'saved' || scenario === 'reload-error') {
      const form = new URLSearchParams(body);
      settings.display.brightness = Number(form.get('brightness_percent'));
      settings.display.screensaver_idle_timeout_s = Number(form.get('screensaver_idle_timeout_s'));
    }
    return json({accepted: true, request_id: 7}, 202);
  }
  if (url.pathname === '/api/v1/settings') {
    if (submitted && scenario === 'reload-error') return json({error: 'Injected reload failure'}, 503);
    return json(settings);
  }
  if (url.pathname === '/api/v1/settings/result') {
    polls++;
    const status = polls < 8 ? 'pending' : scenario === 'reload-error' ? 'saved' : scenario;
    return json({request_id: 7, status});
  }
  if (url.pathname === '/api/v1/status') return json({
    firmware: {version: '99.0.0', build: 'LOCAL UI TEST'},
    device: {model: 'Local fixture (no hardware)', hardware_revision: 'v2', id: 'test-only'},
    network: {ssid: 'Local UI test', hostname: 'localhost', ip: '127.0.0.1'},
    ota: {active: false, preparing: false, progress: 0}
  });
  if (url.pathname === '/api/v1/history') return json({sessions: []});
  return json({error: 'Not supported by local UI fixture'}, 404);
});
server.listen(8765, '127.0.0.1', () => console.log('Local settings fixture: http://127.0.0.1:8765/#settings (no hardware)'));
