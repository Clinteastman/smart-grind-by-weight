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
values could not be reloaded.

`node tools/tests/settings_web_test.mjs` syntax-checks the complete embedded
JavaScript and executes the production save handler with API/DOM doubles. It
passes pending/saved/failed/busy, unknown results, ID mismatch, polling timeout,
duplicate submission and reload failure cases. This is not rendered-browser
or hardware verification. The test runs in firmware CI.

No device flashing or physical acceptance test has been performed.

## Remaining before this fix can ship

- Bound the initial POST and reload network waits as well as result polling;
  check that editing during a pending save cannot silently lose later edits.
- Test the HTTP request/result lifecycle and the rendered web settings workflow,
  including runtime application failures and settings reload errors.
- Complete V1/V2 builds, PR review and appropriate device acceptance before
  merge/release.
