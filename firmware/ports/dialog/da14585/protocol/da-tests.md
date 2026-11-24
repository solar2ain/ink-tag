# DA14585 bounded tests

The normal source build advertises `TEST` and implements `LIST_TESTS`,
`RUN_TEST` and `GET_TEST_STATUS`. Public and private builds use the same code;
no debug-only feature switch, signing key or extra resource image is needed.
Tests are explicit device operations, not automatic boot diagnostics.

`LIST_TESTS` takes no payload and replies with `version:u8=1, count:u8, ids[]`:

| ID | Operation | Availability |
| --- | --- | --- |
| `0x30` | TIME_SHIFT | Native calendar/clock worker; requires a valid current time |
| `0x31` | TIME_SET | Native calendar/clock worker |
| `0x20` | ADC_READ | Listed only when the board exposes voltage telemetry; channel 1 is battery millivolts |

No status-light, input simulation, buzzer, locate, raw GPIO, NFC snapshot or
native diagnostic-pattern operation is advertised without an implemented and
qualified backend. Host-rendered screen patterns still use ordinary image
transfer. Debug-session commands remain unsupported.

`RUN_TEST` requires flags zero. All fields are little-endian:

- TIME_SHIFT: `0x30, displayMode:u8, deltaSeconds:i32` (6 bytes). Mode is
  calendar `1` or clock `2`. Delta is nonzero and at most 366 days in magnitude.
- TIME_SET: `0x31, displayMode:u8, utc:u32, timezoneMinutes:i16, weekStart:u8`
  (9 bytes). Week start retains the native Sunday `0` / Monday `1` contract.
- ADC_READ: `0x20, channel:u8` (2 bytes). Channel `1` replies with
  `1, channel, valid:u8, readingMv:u16, 0` and a matching COMPLETE event.
  An unavailable reading has `valid=0`; it never authorizes a write. Other
  defined channels are unsupported; malformed channels/lengths are invalid.

The time operations retain the existing local persistent-anchor bounds:
`1704067200..4102444800`, inclusive, with timezone -840..840 minutes. A date
outside that native range is INVALID_REQUEST before settings/time writes.
This is a firmware storage constraint, not a shared-client year restriction.
No native record format, epoch or layout changes.

Successful time tests persist and publish the new clock, acknowledge with the
ordinary 7-byte time reply, then run one calendar/clock job. Its existing
worker sends COMPLETE with the RUN_TEST opcode and the same transaction ID,
including cancellation, IO and refresh failures. An admitted display failure
does not undo an already acknowledged time change. Synchronize the normal
clock explicitly after testing when the test time should not be retained.

BUSY/screen/power checks precede mutation. Persistence failure leaves RAM time,
timezone and preferences unchanged. A scheduling refusal restores persistence
before reporting BUSY; failed restoration reports PERSISTENCE_ERROR and no
new RAM time or refresh is published. Unknown persistent outcomes are not
reported as success or replayed.

`GET_TEST_STATUS` takes no payload and returns
`1, displayBusy:u8, currentDisplayMode:u8`. It uses the same ordinary display
state rather than a second test queue.

Offline regression, with pytest and GCC or WSL GCC:

```powershell
python -m pytest tests/test_da_bounded_tests.py -q
```

The exported test compiles the production handlers, scheduler and completion
path with inert hardware/storage adapters. It requires neither a vendor SDK
nor private Git history, credentials or ignored build artifacts. It is not a
physical panel or ADC-accuracy qualification.
