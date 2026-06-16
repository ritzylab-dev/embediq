# EmbedIQ GPIO Blink Demo

Demonstrates fb_gpio — the EmbedIQ GPIO Platform Driver FB — on a Raspberry Pi.

An LED blinks at 1 Hz (on for 1 s, off for 1 s) on the configured GPIO pin
for the specified duration, then the program exits.

## Hardware

- Raspberry Pi (any model with 40-pin header)
- 1× LED
- 1× 330 Ω resistor

## Wiring

```
GPIO17 (Pin 11) ──── 330Ω ──── LED (+) ──── LED (−) ──── GND (Pin 9)
```

BCM GPIO17 is Pin 11 on the 40-pin header. GND is any GND pin (e.g. Pin 9).

## Build

```sh
cmake -B build -DEMBEDIQ_PLATFORM=host   # host build (writes to /tmp)
# or for Raspberry Pi:
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/rpi.cmake
cmake --build build
```

## Run on Raspberry Pi

GPIO access requires root or membership in the `gpio` group:

```sh
# Add your user to the gpio group (once):
sudo usermod -aG gpio $USER
# Then log out and back in.

# Run the demo (default: GPIO 17, 10 seconds):
./build/examples/gpio_blink/embediq_gpio_blink

# Custom pin and duration:
./build/examples/gpio_blink/embediq_gpio_blink --pin 17 --duration 30
```

## Run on Host (CI / development)

On a Linux or macOS host, the HAL writes to /tmp files instead of sysfs.
The LED state is visible in /tmp/embediq_gpio17_value (reads "0" or "1").

```sh
./build/examples/gpio_blink/embediq_gpio_blink --pin 17 --duration 2
```

## Platform Portability

The demo uses logical gpio_id=0 throughout the application layer.
The physical BCM pin number (from --pin) is set only in main.c's board config.
To port to another board or pin, change only main.c:

```c
s_gpio_pins[0].gpio_id = 0u;      /* keep as 0 */
s_gpio_pins[0].pin  = NEW_PIN;    /* change this */
```

fb_blink_led.c requires zero changes between hardware platforms.

## Message Flow

```
fb_timer (Phase 1)
  └─ MSG_TIMER_1SEC ──► fb_blink_led (Phase 3)
                           └─ MSG_GPIO_SET_REQUEST(gpio_id=0) ──► fb_gpio (Phase 1)
                                                                     └─ maps gpio_id=0 → physical pin
                                                                     └─ sysfs /sys/class/gpio/gpio{N}/value
```
