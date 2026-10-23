# t_wifi

Minimal ESP32-S3 Wi-Fi authentication diagnostic. It scans the target AP and
then tries two configurations with the same credentials:

1. DeepStoa settings: WPA2 minimum, PMF capable/optional, SAE H2E both.
2. ESP-IDF default station settings.

The test uses the root DeepStoa 16 MB partition layout, so its app is flashed
at `0x20000`. Its flash clock is reduced to 20 MHz to distinguish a marginal
flash read path from a damaged image. The test prints SSID, scan auth mode,
channel, RSSI, password **length only**,
connection events, and disconnect reason. It never prints the password.

Copy `main/wifi_credentials.example.h` to `main/wifi_credentials.h` and set
your local credentials. The latter file is ignored by Git.

From this directory, with ESP-IDF v5.5.3 activated:

```text
idf.py -B build_slow -DSDKCONFIG=sdkconfig.slow -DIDF_TARGET=esp32s3 build
idf.py -B build_slow -p COM4 flash monitor
```

Flashing this test temporarily replaces the main firmware. Flash the main
project again after testing. Older `build`, `build_s3`, and `build_wpa2`
directories used the default 2 MB factory partition layout; do not flash
those builds onto a device that should retain the DeepStoa partition layout.

## Device result (2026-10-22)

On the COM11 ESP32-S3, the target hotspot scanned at about -32 dBm. With WPA3
enabled, the scan reported `auth=7` (WPA2/WPA3 mixed). Both the DeepStoa
settings profile and default ESP-IDF profile disconnected during authentication
with reason 2. A second build with WPA3 disabled reported `auth=3` (WPA2) and
both profiles still disconnected with reason 2. The main DeepStoa firmware was
flashed back after the tests. These results reproduce the issue without the
Settings UI. Reason 2 can mean an authentication timeout or an AP-supplied
reason, so these logs alone cannot distinguish AP behavior from a board RF
problem.

On a second ESP32-S3 (COM4), the 40 MHz build failed its bootloader image
checksum with different calculated values on successive resets. An independent
full flash read of the app at `0x20000` matched the local binary byte for byte.
The `build_slow` profile above is prepared for a 20 MHz boot test; it has not
yet been flashed as part of this diagnosis.

On an ESP32-S3 DevKitC, the same hotspot and credentials connected with the
DeepStoa profile using WPA3-SAE and received an IP address. The test then
intentionally disconnected (reason 8). Its second, default ESP-IDF profile
failed with reason 49 (invalid PMKID). This confirms the DeepStoa connection
settings and credentials work on the DevKitC. That run used an older 2 MB image
header, which accounts for the boot-time flash-size warning; the current
`build_slow` profile declares 16 MB.
