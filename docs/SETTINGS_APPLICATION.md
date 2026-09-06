# Reliable settings application

This branch is work in progress and is not a released fix.

## Storage failures

Web profile updates check all eight stored values (selected profile, mode,
three weights and three times). A failed write returns failure and reloads
the stored profile state into memory. These separate writes are not an atomic
transaction: some values can be saved before another write fails. Reloading
keeps the running values consistent with the next reboot; it does not undo
successful writes.

The host regression test compiles the complete production profile controller.
It injects a failure at each write, compares the live state with a new
controller loaded from the resulting store, checks a subsequent successful
retry, and verifies that invalid requests do not write anything. Concurrent
snapshot tests also remain enabled.

The web settings path also checks namespace opens and individual writes for
purging, automation, logging, swipe, Bluetooth startup, brightness and screensaver
settings. It retains the timing/settings-service failure results. Motor latency
and coast ratio only change their running value after a successful storage write.
The cached settings and touchscreen runtime refresh run even after partial writes.

## Save completion API

`POST /api/v1/settings` still returns HTTP 202 for a queued save, now with
`request_id` alongside `accepted`. Acceptance is not completion.

Poll `GET /api/v1/settings/result?id=<request_id>` for `status`:

- `pending`: queued, being saved, or waiting for UI runtime refresh.
- `saved`: all checked writes/setup succeeded and UI refresh has finished.
- `failed`: storage or runtime setup failed; some values may have changed.
- `busy`: another motor/update operation prevented application; retry when idle.
- `unknown` (HTTP 404): result is no longer available; do not infer success.

Only one save may be pending at once. The last four results are retained in RAM;
IDs start from a random value on boot. Responses are not cached. A full command
queue fails the reservation and returns HTTP 503, rather than leaving it pending.
The shared operation reservation excludes motor tests, tuning, grinding and OTA
through persistence, cached-state refresh and the UI refresh completion callback.

Validation includes production persistence methods with each direct write/open
failure injected, plus dependency failures. Result tests execute the production
result methods and APPLY_SETTINGS dispatch branch with peripheral stubs. They
check busy/refused saves, partial-save refresh, completion ordering, bounded result
retention, ID wrap and concurrent requests. They do not exercise a real HTTP
server or rendered UI. All 18 host tests and three standalone policies pass.
Native WSL V1/V2 builds passed in 39.128s/39.734s (local build 1).
The embedded web page now polls the result instead of assuming a queued save
has finished. It blocks duplicate save submissions, distinguishes busy and
partial-write failure from success, and reloads the stored values. Unknown,
mismatched or timed-out results do not automatically resubmit the save. A saved
result followed by a reload failure explicitly says the save succeeded but the
values could not be reloaded. Initial save requests and settings reloads have
15-second network deadlines as well as bounded result polling. The settings
form is temporarily inert during the save/reload, so edits cannot be made and
then silently replaced by the refreshed values. Interaction is restored on
success and failure.

`node tools/tests/settings_web_test.mjs` syntax-checks the complete embedded
JavaScript and executes the production save handler with API/DOM doubles. It
passes pending/saved/failed/busy, unknown results, ID mismatch, polling timeout,
duplicate submission and reload failure cases. This is not rendered-browser
or hardware verification. The test runs in firmware CI.

No device flashing or physical acceptance test has been performed.

After the web timeout/interaction changes, all 18 host tests passed again
(17.484 seconds), along with the JavaScript tests including a real abortable
pending request. Native WSL V1/V2 rebuilds passed in 31.194/29.872 seconds,
local build 1. These remain local validation results, not device acceptance.

## Remaining before this fix can ship

Local browser checks started with `node tools/tests/settings_browser_server.mjs`
and the actual embedded page at `http://127.0.0.1:8765/#settings`. In-app browser
click-through confirmed pending saves block interaction, successful saves show
the confirmed success message, and `?scenario=failed#settings` shows partial-save
failure with controls restored. This fixture has no real motor/device connection
and does not implement WebSocket telemetry. It is single-session test tooling.

The accessibility tree also exposed a remaining issue: making the entire form
inert hides its pending status from assistive technology. Keep an accessible
pending announcement outside the inert subtree before claiming UI completion.
Chrome, explicit mobile/desktop viewports, remaining failure scenarios and
console/network inspection have not yet been completed.

- Test the HTTP request/result lifecycle and the rendered web settings workflow,
  including runtime application failures and settings reload errors.
- Complete V1/V2 builds, PR review and appropriate device acceptance before
  merge/release.
