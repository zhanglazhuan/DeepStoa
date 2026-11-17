# t_quality

ESP32-S3 DevKit + GDEM0397T81P display and FT6336 touch diagnostic.

The initial screen is solid black. Press BOOT (GPIO0, active low) to cycle
through **black -> white -> 32px grid -> black**. Holding BOOT does not repeat.

On the grid screen, a touch displays the latest raw FT6336 coordinates as
`X:123 Y:456` at the touched position, shifted inward near screen edges to
keep the label visible. The grid and its existing axis labels remain.
Each click uses a full e-ink refresh, then puts the panel into deep sleep;
expect the normal refresh delay. Clicks during a refresh are coalesced to the
latest click. Touches on the solid-color screens only log to the serial port.

With ESP-IDF v5.5.3 activated, run from this directory:

```text
idf.py build
idf.py -p <PORT> flash monitor
```
