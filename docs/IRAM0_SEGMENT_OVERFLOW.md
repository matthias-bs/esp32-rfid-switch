# IRAM0 Segment Overflow

## Summary

Some ESP32 builds of the RFID switch examples can fail at the link stage with an IRAM0 overflow. The observed failure was produced while compiling the Shelly Web Config example for an M5Stack Core2 with ESP32 Arduino core 3.3.11.

The important error was:

```text
section `.iram0.text' will not fit in region `iram0_0_seg'
IRAM0 segment data does not fit.
region `iram0_0_seg' overflowed by 2364 bytes
```

This is a linker error, not a flash-partition-size error. Changing the partition scheme does not increase the available IRAM0 region.

## Reproduce the Build

Compile the Shelly example for the M5Stack Core2 with:

```bash
cd /home/mp/pCloudDrive/rfid-switch/esp32-rfid-switch
arduino-cli compile \
  --fqbn esp32:esp32:m5stack_core2 \
  examples/rfid-switch-shelly
```

## Cause of the IRAM Overflow

The linker map showed that the largest application-level IRAM contributor was:

```text
M5GFX/.../Panel_CVBS.cpp.o   approximately 2220 bytes
```

`Panel_CVBS.cpp` contains CVBS video conversion and interrupt-handling code. Several of its functions are deliberately marked `IRAM_ATTR` because CVBS output must continue to work while the flash cache is unavailable. Removing `IRAM_ATTR` is not a safe general fix; it can cause runtime crashes when the interrupt handler executes.

The standalone Shelly Web Config example does not use the Core2 display or CVBS output. It does use the built-in power LED, but only through a small direct-I2C helper under `src/`, which detects the installed AXP192 or AXP2101-family PMIC. Including M5Unified for this sketch would pull in M5GFX and its display-related code unnecessarily, causing the IRAM0 overflow even though the sketch does not need graphics.

## Applied Fix for the Shelly Example

The Shelly Core2 path now:

- avoids including `M5Unified`;
- uses RFID reader-disconnect recovery to enter configuration mode, because the virtual Core2 Button A is provided by M5Unified and is unavailable in this lightweight sketch;
- controls the built-in power LED through the direct PMIC I2C helper without linking M5GFX;
- retains GPIO 13 and GPIO 14 for the RFID reader;
- keeps the Web Config portal and Shelly BLE functionality unchanged.

## Verified Result

The fixed Shelly example was compiled with:

```bash
arduino-cli compile \
  --fqbn esp32:esp32:m5stack_core2 \
  examples/rfid-switch-shelly
```

The build completed successfully:

```text
Sketch uses 1,230,288 bytes (18%) of program storage space.
Global variables use 70,668 bytes (1%) of dynamic memory.
```

The successful build no longer reports:

- `iram0_0_seg` overflow;
- `IRAM0 segment data does not fit`.

## M5GFX and the Touchscreen Example

M5GFX can be retained when the display is genuinely required, as in the `rfid-switch-core2-gdtouchkeyboard` example. That example uses the Core2 display and touchscreen for local configuration, so removing M5Unified is not equivalent.

There is no general documented M5GFX switch that safely moves the CVBS interrupt code out of IRAM. Do not modify `IRAM_ATTR` in `Panel_CVBS.cpp` unless CVBS support is removed and the resulting library is tested for the target hardware.

For a sketch that does not use graphics, the preferred reduction is to avoid linking M5Unified/M5GFX entirely. For a sketch that needs graphics, investigate the linker map and remove unused features or libraries before changing interrupt placement.

## Diagnosis Checklist

1. Confirm the selected FQBN. For Core2 it must be `esp32:esp32:m5stack_core2`.
2. Check whether the failure is a link error or a compile error.
3. Look for a generated `.map` file in the Arduino build directory.
4. Inspect the `.iram0.text` section and identify the largest input objects.
5. Remove unused display, graphics, Bluetooth, or other feature libraries where the application does not need them.
6. Keep interrupt handlers and functions required while the flash cache is disabled in IRAM.
7. Rebuild and verify that the final output contains a memory summary and no `iram0_0_seg` diagnostic.

## Related Examples

- `examples/rfid-switch-shelly`: lightweight Web Config portal and Shelly BLE output; does not include M5Unified because it would pull in M5GFX and overflow IRAM0. Core2 LED control uses the direct PMIC I2C helper, and configuration recovery uses RFID reader-disconnect detection.
- `examples/rfid-switch-relay`: Web Config portal and local relay output; may use M5Unified for Core2 display and LED indications.
- `examples/rfid-switch-core2-gdtouchkeyboard`: local Core2 display and touchscreen configuration; requires M5Unified and M5GFX.
