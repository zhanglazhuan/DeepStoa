# t_wifi

The test currently requests a Wi-Fi transmit-power ceiling of **18 dBm** using
`esp_wifi_set_max_tx_power(72)` after Wi-Fi starts and before scanning/connecting.
The startup log reads back both the initial and configured limits in 0.25 dBm
units. This is a diagnostic setting for measuring the COM11 board's power rail.
The ESP-IDF v5.5.3 API documentation maps requests 72-79 to the 72 (18 dBm)
level; actual RF output also depends on PHY/rate/country limits and calibration.
A readback value is not a measurement of actual transmitted power.

Minimal ESP32-S3 Wi-Fi authentication diagnostic. It scans the target AP and
then selects the strongest AP matching `TEST_WIFI_SSID` (the local test hotspot
is `zhanglazhuan`). It logs each scanned AP and connects using the selected
BSSID and channel. If the target is not found or scanning fails, the test stops
without attempting a connection to that network and continues to the next network.
It tests the hotspot first, then the router `CMCC-ayrw`, configured with
`TEST_ROUTER_SSID` and `TEST_ROUTER_PASSWORD` in the ignored local credentials
header. For each network it tries two configurations with the same credentials:

1. DeepStoa settings: WPA2 minimum, PMF capable/optional, SAE H2E both.
2. ESP-IDF default station settings.

After each successful IP assignment, it resolves `www.baidu.com` to IPv4 and
sends four ICMP echo requests (one-second interval and timeout). Logs distinguish
Wi-Fi/IP success, DNS failure, ping replies/timeouts, and packet loss before
disconnecting. No ICMP replies alone do not prove Internet access is unavailable,
since ICMP may be filtered. Ping success requires at least one reply.

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
