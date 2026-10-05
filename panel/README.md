# MicroESP panel (Ray)

Panel MiniApp for Smart Life built with [Ray](https://developer.tuya.com/en/miniapp/develop/ray/guide/start/quick-start). It replaces the product's standard panel with the "Panel MicroESP" design: PC state, power on, shut down and restart with confirmation and a cancellable countdown, cloud timers, telemetry and settings.

It is the released panel of the MicroESP product. Building it in the Tuya MiniApp IDE (also on Linux, under Wine), previewing it on the phone and publishing new versions: [`docs/panel-publishing.md`](../docs/panel-publishing.md).

## How it talks to the device

The product is **TuyaLink**, so the panel uses the thing model rather than classic DPs:

| What | Ray API | Detail |
|---|---|---|
| Initial values | `getDeviceInfo` | `dpCodes` (by code) and `dps` (by abilityId 101–114) |
| Changes | `subscribeReceivedThingModelMessage` + `onReceivedThingModelMessage` | Property messages (`type: 0`), plain values or `{value, time}` |
| Changes (fallback) | `onDpDataChange` | In case the app also delivers them as DPs |
| Writes | `publishThingModelMessage` | `type: 0`, `payload: {<code>: value}` = `thing/property/set` |
| Online | `getDeviceInfo().isOnline`, `onDeviceOnlineStatusUpdate` | Cloud state of the dongle |
| Timers | `addTimer`, `syncTimerTask`, `removeTimer`, `onTimerUpdate` | Cloud timers (DeviceKit), one category per command |

Codes, abilityIds, ranges and defaults come from [`firmware/schema/dp.json`](../firmware/schema/dp.json). `npm test` checks that [`src/device/model.ts`](src/device/model.ts) matches that schema.

Semantics the panel follows (see `dp.json`):

- `power_on` is a push button: the panel sends `true` and the dongle sets it back to `false`.
- `power_off` / `reboot` set to `true` start the `cmd_countdown` countdown. Sending `false` while it counts cancels it. The dongle does not report the time left, so the panel estimates it locally from when it sees the `true`.
- Power on, shut down and restart are always enabled, whatever `pc_state` says: the BIOS enumerates the dongle even with the PC off, so the detected state is only a hint. Shut down and restart always ask for confirmation first. They are run by the PC agent, so with `agent_online = false` the dongle answers `agent_offline`; the panel warns about it but still sends them.
- Telemetry arrives in tenths of a percent (`184` → 18.4 %).
- `fault` is a bit mask. Each active bit shows a warning, except `hid_not_armed`, which is informational (a failed wake shows up as `wake_failed`).

## Cloud timers

The Tuya cloud runs the timers and writes the property at the set time, as a Smart Life automation would, so the dongle needs no timer support. A timer only ever writes `true` to one of `power_on`, `power_off` or `reboot` (each in its own category: `mesp_power_on`, `mesp_power_off`, `mesp_reboot`), so a scheduled shut down or restart still runs the cancellable countdown.

- **Run in X h Y min**: the panel adds a one-shot timer (`loops: "0000000"`) at the clock time now + delay, rounded to the minute. A one-shot timer only has a clock time, so the delay is at most 23 h 59 min.
- **At a time**: a clock time (`Picker mode="time"`) and weekday chips that set `loops` (Sunday first). No day selected runs once at the next such time; all seven run every day.
- **Scheduled**: lists the enabled timers of the three categories, soonest first, with a button to remove each one. It also refreshes on `onTimerUpdate`.

Tuya's generic timer page (`openTimerPage`) did not open for this TuyaLink product in the IDE tester, so the panel builds its own timers instead.

## Structure

```
panel/
├── project.tuya.json        # MiniApp kits and base library version (devMode: ray)
├── src/
│   ├── app.tsx
│   ├── routes.config.ts     # A single page: pages/home
│   ├── device/
│   │   ├── model.ts         # Thing model: types, normalization, rules (no dependencies)
│   │   ├── useMicroEsp.ts   # Hook: live state and writes over TuyaLink
│   │   └── useTimers.ts     # Hook: cloud timers of the three commands
│   ├── strings/index.ts     # es/en texts, chosen by the app language
│   ├── components/          # Icon (SVG as data URI), ConfirmSheet
│   ├── pages/home/          # The panel and its Schedule section
│   └── variables.less       # Palette
├── scripts/
│   ├── add-win-natives.sh   # npm run ide:win-natives
│   └── ide-wine/            # Tuya MiniApp IDE under Wine: setup.sh, patch-ide.mjs, run-ide.sh
└── test/model.test.mjs      # Model tests against dp.json
```

> raypack compiles `src/devices` and `src/i18n` separately with a different module resolution, and the build fails. That is why they are called `device` and `strings` here.

## Development

Requirements: Node ≥ 22.6, the Tuya MiniApp IDE and a Tuya Developer Platform account. `project.tuya.json` needs `baseversion` ≥ 2.27.0 or the IDE does not compile.

```bash
cd panel
npm install
npm test                  # model against firmware/schema/dp.json
npm run typecheck
npm run build:tuya        # writes dist/tuya
npm run ide:win-natives   # only for the IDE under Wine, after each npm install
```

The `typings/tuya*` folders are generated by the IDE when it imports the project and are not versioned.

Checked with the real device: `getDeviceInfo` delivers the properties in `dps` keyed by abilityId (`"101"`…`"114"`), which the panel merges with `mergeDps`.

Still to check:

- whether `getDeviceInfo` also fills `dpCodes` for TuyaLink products;
- the exact `payload` shape of `onReceivedThingModelMessage`. The model accepts plain values and `{value, time}`;
- the cloud timers on the real device: that `addTimer` with a single DP value per category runs, once and repeating.
