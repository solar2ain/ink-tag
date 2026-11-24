# Low power and battery life

[简体中文](../zh-CN/power.md)

An e-paper panel retains its image between refreshes. For calendars and photo displays that rarely refresh, sleep and Bluetooth advertising account for most of the power use. Bluetooth connections, image transfers and frequent refreshes shorten battery life.

## Current measurements and estimates

| Operating state | Reference current |
| --- | --- |
| Sleep between advertising events | About 2–3 µA on most measured boards, excluding advertising pulses |
| One ordinary BLE advertisement per second | About 30–40 µA average on most low-power boards, including advertising pulses while disconnected |
| Find My advertising also enabled | Estimated additional average of about 15–20 µA |
| Clock with a partial refresh every minute | About 5–10 mA for 2–3 seconds per refresh. Estimate the average from the time spent refreshing and idle |

Selected measurements from a calendar or idle state are shown below. The board, panel and firmware can affect the result.

| Measured example | Current |
| --- | --- |
| Goodix GR5513 / GR5515 | About 3 µA between advertisements and about 35 / 37 µA average with ordinary BLE advertising |
| OnMicro OM6626 (ZKC42B) | About 3 µA between advertisements and about 31 µA average with ordinary BLE advertising |
| Telink TLSR8359 with a JD79665AA panel | About 3 µA with ordinary BLE advertising off and about 30 µA average with it on |

Use the reference ranges for boards without recorded figures. For a device that only supports ordinary advertising, use the corresponding ordinary BLE row. On PRO devices with delayed advertising shutoff, Find My can continue after ordinary advertising stops.

## Calendar and clock battery life

These examples assume **200 mAh** or **500 mAh** of usable capacity within the device's operating voltage range. The calendar refreshes once per day. An illustrative refresh at **10 mA for 15 seconds** uses about **0.042 mAh** per day, equivalent to about **1.7 µA** averaged over the day. The calendar estimates include that refresh. Use your panel's measurements for a more representative estimate.

| Usage | Estimated device average | 200 mAh | 500 mAh |
| --- | ---: | ---: | ---: |
| Calendar, both kinds of advertising off | 3.7–4.7 µA | About 4.8–6.1 years | About 12–15 years |
| Calendar, Find My advertising only | 18.7–24.7 µA | About 11–15 months | About 2.3–3.0 years |
| Calendar, ordinary BLE advertising once per second | 31.7–41.7 µA | About 6.6–8.6 months | About 16–22 months |
| Calendar, ordinary BLE every second plus Find My | 46.7–61.7 µA | About 4.4–5.9 months | About 11–15 months |
| Clock, partial refresh every minute, ordinary Bluetooth advertising on | About 196–538 µA | About 15–43 days | About 39–106 days |

The multi-year figures are theoretical upper bounds based on capacity and current. Self-discharge, temperature, the cutoff voltage and voltage sag during refresh can reduce actual runtime. Each minute includes 2–3 seconds of refreshing at 5–10 mA and 57–58 seconds of ordinary Bluetooth advertising at 30–40 µA on average. Calculate `(refresh current × refresh seconds + average idle current × remaining seconds) ÷ 60` to get about 196–538 µA. Include the energy used by any periodic full refreshes separately.

Calculate `runtime (days) = usable capacity (mAh) × 1000 ÷ average current (µA) ÷ 24`. When measuring standby and refresh separately, use `daily charge (mAh) = average standby current (mA) × 24 + extra charge per refresh (mAh) × refreshes per day`. To measure the advertising average, disconnect Bluetooth and the debugger, let the screen finish refreshing and sample across multiple advertising intervals.
