# RFID Switch - Core2 GDTouchKeyboard

This example combines the RFID relay and Shelly BLE runtime paths in one sketch. It uses the M5Stack Core2 display and `GDTouchKeyboard` for configuration, so it does not start the Wi-Fi Web Config portal.

## Compile-time variant

The sketch selects the Shelly variant by default. To build the relay variant, define `RFID_SWITCH_VARIANT_RELAY` as a compiler flag:

```text
arduino-cli compile --fqbn esp32:esp32:m5stack_core2 \
  --build-property build.extra_flags=-DRFID_SWITCH_VARIANT_RELAY \
  examples/rfid-switch-core2-gdtouchkeyboard
```

Alternatively, change the default `RFID_SWITCH_VARIANT_SHELLY` definition at the top of the sketch to `RFID_SWITCH_VARIANT_RELAY`. Recompile after changing the variant. Defining both variants is a compile-time error.

The example requires an **M5Stack Core2** with these connections:

| Function | Core2 GPIO |
| --- | ---: |
| RFID reader RX | 13 |
| RFID reader TX | 14 |
| Relay input | 32 / Port A |

The relay output is used only by the relay build. The Shelly build requires the `ShellyBleRpc` library and controls Shelly Switch 0 over BLE.

## Configuration controls

On first boot, or when Button A is pressed during the first three seconds after reset, the configuration overview appears.

- Button A selects the previous field.
- Button C selects the next field.
- Button B edits the selected field.
- Touch `Prev`, `Edit`, or `Next` in the overview to perform that action.
- Navigate to the `Save` entry and press Button B or touch `Edit` to start the RFID runtime.
- In the keyboard, Button A deletes, Button B accepts, and Button C changes keyboard mode.

The overview status `Ready` means that the current values pass validation; it does not save them or indicate that the RFID runtime has started. Select the `Save` entry and press `Edit`/Button B to save the values and leave configuration. The display backlight is on while configuring and is switched off after configuration closes.

Both variants configure EPC, TID, password, and token. EPC and TID are required even-length hexadecimal values. Password and token are optional eight-character hexadecimal values.

The Shelly variant additionally accepts either a colon-separated BLE address or a case-sensitive name filter. A direct BLE address takes precedence when both are configured. Settings use the same `shelly-ble` NVS namespace and keys as the original Web Config examples.

In Shelly mode, the Core2 power LED shows the last confirmed Shelly relay state across deep sleep: off means relay off and full brightness means relay on. A three-blink sequence indicates that the Shelly state is currently unavailable, for example after a BLE connection or RPC failure. The failure indication is shown before sleep; a later wake that successfully reads the Shelly state restores the normal relay-state indication.

## Dependencies

Install the ESP32 Arduino core, `M5Unified`, `GDTouchKeyboard`, `M5Unit-UHF-RFID`, and `esp32-shelly-ble-rpc`. `NimBLE-Arduino` is required by the Shelly BLE dependency.
