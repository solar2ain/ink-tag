# Platform HAL direction

Future ports should converge on a small platform boundary rather than letting SDK APIs leak through common code.

The first useful boundary is:

- GPIO: configure, read, write, toggle, default/uninit
- SPI: init, uninit, write, half-duplex read when the panel bus needs it
- time: millisecond delay and an always-running monotonic counter
- power: system sleep/reset hooks
- storage: read/write/erase EPD configuration
- BLE: one combined writable/notifiable command-status characteristic and one read-only app-version characteristic

The current Web Bluetooth contract is intentionally MCU-neutral:

- service UUID: `62750001-d828-918d-fb46-b6c11c675aec`
- command/status characteristic UUID: `62750002-d828-918d-fb46-b6c11c675aec`
- app version characteristic UUID: `62750003-d828-918d-fb46-b6c11c675aec`

The nRF5 port predates this boundary and still uses Nordic APIs directly. GR5331, GR5515, OM6626, and DA14585 ports should start by implementing this layer, then moving common EPD code off direct SDK calls.

Wall-clock code must derive elapsed seconds from that monotonic counter through
`inktag_elapsed_clock.h`. `inktag_timebase.h` defines the MCU-independent clock
source, quality and qualification vocabulary; the vendor adapter owns source
selection/counter access and the exact board owns its fitted-source facts. A
repeating callback is only a sampling opportunity:
one callback must never be assumed to mean exactly one elapsed second, because
EPD work, Flash critical sections, cooperative BLE scheduling, or masked
interrupts can coalesce timer expirations. The product scheduler must also
detect crossed minute/day boundaries when a sample advances by more than one
second. TLSR8359, OM6626 and Goodix use the common accumulator; TLSR's vendor
adapter supplies the SDK system counter after its selected 32 kHz recovery
handler has accounted for suspend. DA14585 follows an
equivalent hardware-tick model and should converge on the same common profile
type during its next migration. New ports must do so before advertising
clock/calendar or slideshow behavior. Nordic remains an explicit migration
item and requires a representative-board wall-clock soak before qualification.

Porting order I recommend:

1. BLE custom service + device advertising name.
2. SPI panel reset/busy/DC/CS smoke test.
3. EPD model table and image write path.
4. Flash/NVDS settings storage.
5. Low-power sleep and wake behavior.
6. Vendor ISP/OTA packaging.
