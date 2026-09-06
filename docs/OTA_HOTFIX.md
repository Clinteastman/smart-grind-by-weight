# GitHub firmware update hotfix — 1.5.9

## What went wrong

An HTTPS response can deliver its headers before its firmware body. The Arduino
3.3.2 TLS client can return immediately from `readBytes` when no body byte is
available. The updater treated this as an invalid ESP32 image and stopped at 0%,
even though the published firmware was valid. The web page then silently enabled
the install button again.

The downloader now waits up to 15 seconds for the first byte, ending earlier on
disconnection. It still validates the ESP32 header before opening the inactive
partition. The web status exposes failed updates, and the page checks the running
version before announcing a successful GitHub installation.

## Updating an affected grinder

If **Install update** stays at 0% on older firmware:

1. Download the matching application `.bin` from the
   [1.5.9 release](https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.9)
   once published. V2 uses the file ending in `-waveshare-164-v2.bin`; V1 uses
   `smart-grind-by-weight-v1.5.9.bin`.
2. Open the grinder's web page, then **Settings → System & updates**.
3. Under **Manual firmware file**, choose that file and select **Upload firmware**.
4. Leave power connected until it restarts. Refresh and check the firmware version.

Do not upload a bootloader, partition table, patch or archive. This is a one-time
Wi-Fi recovery; it does not require USB, erasing settings or opening the grinder.

## Validation

- Both V1 and V2 native-WSL firmware builds passed.
- All 28 host regression tests passed, including production first-byte header
  checks with delayed data, disconnects, timeouts, invalid magic and timer wrap.
- Embedded JavaScript tests passed for failure feedback, preparation locking,
  progress, failed recovery and installed-version confirmation.
- On physical V2 hardware, the repaired candidate downloaded the published
  1.5.8 V2 image through the real GitHub-mirror updater. Progress reached 97%,
  then it restarted into release build 12. All exposed settings matched before
  and after. No motor was operated during the update test.
- V1 hardware testing remains unverified. Host fault simulations are not a claim
  of physically interrupting power or Wi-Fi during a flash.

Final 1.5.9 publication and web-button acceptance are checked separately after
review and release packaging.
