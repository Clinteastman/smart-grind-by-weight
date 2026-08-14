# Wi-Fi, Web and Home Assistant Architecture

This document records the intended Smart Grind network behaviour before the
public interfaces are built. GaggiMate is used as mature behavioural prior art
because it runs a real-time coffee appliance on closely related ESP32 hardware.
Smart Grind's implementation is written independently and kept proportionate to
the grinder's smaller feature set.

## User journey

1. Try saved station credentials without blocking weight sampling, motor control,
   touch, or rendering.
2. If credentials are absent or cannot connect, start a secured setup access
   point with a generated, persisted password.
3. Show the setup SSID, password and Wi-Fi QR code on the grinder display.
4. Serve a captive setup page and support Improv Wi-Fi provisioning over USB
   serial as an alternative.
5. Reboot or transition cleanly into station mode after credentials are saved.
6. Resolve the grinder at its `.local` hostname and advertise versioned HTTP and
   Smart Grind services over mDNS.

## Reliability rules learned from mature ESP32 deployments

- Network connection and reconnection must be asynchronous. No network operation
  may delay the real-time grind-control tasks.
- Wi-Fi event callbacks must do minimal work. State changes are acted on from a
  normal application loop/task where stack and locking behaviour are controlled.
- Tear down an existing mDNS responder before restarting it and stop it on link
  loss, otherwise stale records can accumulate across reconnects.
- The HTTP listener should survive transient station reconnects. Repeatedly
  closing and immediately rebinding an asynchronous server can race socket
  teardown and leave port 80 unavailable.
- WebSocket clients that cannot consume updates must be disconnected rather than
  allowed to accumulate unbounded send queues and exhaust internal RAM.
- Keep LVGL's object heap in PSRAM on hardware so the Wi-Fi/lwIP stack retains
  enough internal RAM to accept TCP connections under a fully constructed UI.
- Captive portal probes used by Android, Apple, Windows and Firefox need explicit
  responses so setup opens reliably on common phones and laptops.
- OTA must be a controlled appliance state: require a short physical arming
  window, refuse updates while grinding, stop the motor, validate size/image
  metadata and internal-heap headroom, incrementally write only the inactive
  application partition, reboot, and verify the expected version after boot.
- Do not pre-erase the complete OTA partition or suspend a task while it can hold
  a flash lock. Both patterns can stall an upload or deadlock recovery; use the
  platform Update API's sector-at-a-time path and keep the file task schedulable.

## Service boundaries

- `NetworkManager`: credentials, station/AP state, reconnect policy and hostname.
- `ProvisioningService`: secured setup AP, captive DNS/HTTP, static setup/device
  pages and (when implemented) Improv serial.
- `DeviceWebServer`: versioned status endpoints and guarded full-image OTA.
- `DeviceApi`: one versioned state/control contract shared by the web UI and the
  native Home Assistant integration.
- `DiscoveryService`: `.local`, `_http._tcp` and `_smartgrind._tcp` records with
  firmware/API metadata.

## Delivery sequence

1. Buildable non-blocking station lifecycle and persisted settings.
2. Secured AP, captive provisioning, Improv serial, and on-device QR/details.
3. HTTP status surface and safe full-image OTA.
4. Versioned WebSocket state/control API with backpressure.
5. Live web UI and native Home Assistant integration on the same API.

## Improv serial provisioning

USB provisioning implements Improv Serial v1 using the pinned official C++
protocol library. Incoming data is consumed in small bounded batches from the
normal application loop, partial frames expire after 100 ms, and malformed
credential frames are rejected before the protocol parser sees them. Wi-Fi
connection attempts are asynchronous and report `Provisioned` plus the local
HTTP URL on success, or return to `Authorized` with `Unable to connect` after a
bounded attempt. Credentials are never written to the serial log.

The supported RPCs are Wi-Fi settings, current state, device information and
network state. Network scanning, hostname changes and device-name changes are
optional Improv extensions and currently return `Unknown RPC command`.

## WebSocket API v1

The device serves `/ws` and publishes at most one state message every 100 ms.
It accepts no more than four clients and disconnects a client whose outbound
queue cannot keep up. Browser handshakes must have the same HTTP origin as the
device page; native clients without an `Origin` header remain supported.

State messages have this shape:

```json
{
  "api": "v1",
  "type": "state",
  "seq": 42,
  "timestamp_ms": 123456,
  "grind": {
    "active": true,
    "phase": "GRINDING",
    "mode": "weight",
    "progress": 63,
    "target_weight": 18.0,
    "target_time_ms": 0
  },
  "scale": { "weight": 11.34, "flow": 2.17 },
  "motor": { "running": true },
  "system": { "free_heap": 118240 }
}
```

Clients may request only actions that reduce risk or clear a finished result:

```json
{"type":"command","action":"stop"}
{"type":"command","action":"dismiss"}
```

Each request receives a v1 acknowledgement containing `action`, `accepted` and
`reason`. There is deliberately no network command that starts the motor. The
network callback only queues requests; the normal UI/control task decides
whether to execute them.

`GET /api/v1/status` also exposes a stable 12-character device identifier,
model and hardware revision. The same identifier is advertised as the `id` TXT
property on `_smartgrind._tcp`; integrations use it as the config-entry and
entity identity so DHCP address changes do not create duplicate devices.
