# BIOS and operating system setup for remote power-on (Lenovo ThinkStation P3 Ultra SFF G2)

Story: MESP-US-0030. Reference machine: **Lenovo ThinkStation P3 Ultra SFF G2** running Linux (Pop!_OS / Ubuntu 24.04). Its wired NIC is `enp128s31f6` (MAC `fc:9d:05:18:ee:32`).

The dongle powers the PC on in two ways:

1. **USB keyboard (HID)**. In suspend (S3) the standard USB *remote wakeup* is enough. In hibernation (S4) and power-off (S5) the BIOS has to keep the USB port powered and monitored.
2. **Wake-on-LAN (WOL)**: a magic packet over the local network to the MACs that the agent reports to the dongle. The default method `hid_then_wol` sends it **always together with the HID** (and repeats it after 20 s if the PC has not booted).

> **Validation status.** This guide is **not yet validated on the machine**. The MESP-US-0002 spike (S3/S4/S5 × port × BIOS matrix) is still pending and `docs/analysis/spike-wake.md` does not exist. The menu names below are the usual ones in Lenovo ThinkStation/ThinkCentre BIOSes (sources at the end), but they **have not been checked on the P3 Ultra SFF G2**. When you run the test, write down the real name of each option in the "Actual name on this machine" column and the result in [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md).

## 1. Enter the BIOS

1. Restart the PC and press **F1** repeatedly when the Lenovo logo appears. This is what the P3 Ultra SFF G2 user guide says ([ManualsLib][ug-g2]).
2. **F10** saves and exits. **F9** loads the defaults: do not use it unless you want to undo all the changes.
3. **Before changing anything, take a photo of every screen of the *Power* menu** so you can go back to the previous state.

## 2. BIOS settings

| # | What to look for (usual Lenovo name) | Likely menu | Recommended value | What it is for | Data reliability | Actual name on this machine |
|---|---|---|---|---|---|---|
| 1 | **Enhanced Power Saving Mode** (ErP LPS mode / "Deep Sleep" / "Energy Star") | Power | **Disabled** | With ErP enabled, in S4/S5 the machine cuts power to the USB ports and the NIC, and only starts with the button, the alarm or after a power outage. Lenovo states that, if it is enabled, Wake on LAN has to be disabled ([Lenovo ErP][erp]). | Option documented by Lenovo; its name on the P3 Ultra G2 still needs confirming | |
| 2 | **Automatic Power On → Wake on LAN** | Power → Automatic Power On | **Primary** or **Enabled** (depending on the options offered) | WOL fallback from S3/S4/S5 | Option documented on ThinkCentre/ThinkStation ([ThinkCentre manual][tc-hmm]) | |
| 3 | **Wake from keyboard / USB** ("Wake Up on USB", "USB Wake Support", "Keyboard Power On") | Power → Automatic Power On, or Devices → USB Setup | **Enabled** | Wake with the dongle's HID keyboard from S4/S5. From S3 it is controlled by the operating system (§3.1) | **Unknown**: no public documentation of this option on the P3 Ultra G2 was found. It may not exist | |
| 4 | **Always On USB** / "USB power in S4/S5" / "Charge in Battery/Off mode" | Devices → USB Setup or Power | **Enabled** | Keeps the USB port's 5 V with the PC off: the dongle stays connected to Wi-Fi and can send the WOL and the *resume* signal. Without this option, in S5 the dongle powers off and **cannot turn anything on** | **Unknown** on this model. Some machines limit it to a specific port (marked with a lightning bolt or a battery) | |
| 5 | **After Power Loss** | Power | **Last State** (recommended) or **Power On** | What the PC does when power returns after an outage. With *Last State* it returns to how it was; *Power Off* requires pressing the button | Option documented ([Lenovo ErP][erp], [Lenovo forum][forum-apl]) | |
| 6 | **Smart Power On** | Power → Smart Power On | **Enabled** | Power on (or wake from hibernation) by pressing **Alt+P** on a keyboard connected to the USB connector that supports this feature. It is the dongle's main HID path from S4/S5: every power-on command sends Alt+P | Documented in the P3 Ultra user guide ("Enable or disable the smart power-on feature": rear USB-A 3.2 Gen 2 connector marked *smart power on*) ([P3 Ultra guide][ug-p3u], [ManualsLib][ug-g2]) | |
| 7 | **Fast Boot / Quick Boot** (Startup → Boot Mode: Quick / Diagnostics) | Startup | **Diagnostics** (or Fast Boot disabled) while you run the tests | With fast boot the BIOS may skip USB initialization. It does not affect wakeup, but it does affect seeing the dongle in the BIOS | Probable; needs confirming | |
| 8 | **Wake Up on Alarm** | Power → Automatic Power On | Disabled (except for the tests in §4.3) | Scheduled power-on. Useful as a safety net during tests | Documented on other Lenovo machines ([Lenovo ErP][erp]) | |

**Windows** (only if the PC boots Windows): disable *Fast Startup* (Control Panel → Power Options → Choose what the power buttons do → "Turn on fast startup"). With it enabled, "Shut down" is actually a hybrid hibernation and WOL and USB wakeup behave differently. In Device Manager, on the "MicroESP" HID keyboard → Power Management, check "Allow this device to wake the computer". On the NIC check "Only allow a magic packet to wake the computer".

### Recommended USB port

Use the **rear USB-A connector marked *smart power on*** (on the P3 Ultra, item 8 of the rear view: "USB-A 3.2 Gen 2 connector (smart power on)"). Only that connector watches for Alt+P with the PC off. If you do not use it, use a **rear motherboard port** (not a hub or the front panel) and, if any is marked as *Always On* or with a charging icon, that one.

> **Effect on the dongle state.** With *Smart Power On* enabled, the BIOS keeps the keyboard enumerated in S5. The dongle then sees a mounted and running USB bus even though the PC is off, and the LED and `pc_state` may show `booting`/`on_no_agent` (amber). That is why the firmware **always sends** the power-on command, without looking at `pc_state`: if the bus is active, it presses Alt+P; if it is suspended, it sends the *resume* signal and Alt+P on resuming; if it is not mounted, a forced *resume*. Power-on is considered successful when the agent connects or the PC enumerates the dongle again. `!status` shows `hid_proto=boot|report` (the BIOS usually asks for the *boot* protocol) to refine detection. It is **not yet known** which ports of the P3 Ultra SFF G2 keep 5 V in S5. To check, power the PC off (S5) with the dongle plugged in: if the dongle's display or LED stay on, the port is powered (you can also measure it with a USB multimeter). Record the result in the E2E plan.

## 3. Linux

### 3.1 Allow the dongle to wake the computer (S3)

The agent installer (`sudo agent/deploy/install.sh`) already installs the rule `/etc/udev/rules.d/99-microesp.rules` ([`agent/deploy/udev/99-microesp.rules`](../../agent/deploy/udev/99-microesp.rules)), which sets `ATTR{power/wakeup}="enabled"` on the USB device `303a:4002`. To check it:

```bash
# Must show "enabled" for the dongle
for d in /sys/bus/usb/devices/*; do
  [ "$(cat "$d/idVendor" 2>/dev/null)" = 303a ] && echo "$d: $(cat "$d/product") wakeup=$(cat "$d/power/wakeup")"
done
# The xHCI controller must also be able to wake the system ("*enabled" column)
grep -i xhc /proc/acpi/wakeup
```

If the `XHC` controller shows as `*disabled`, enable it with `echo XHC | sudo tee /proc/acpi/wakeup`. Note that this command **toggles** the state and does not persist across reboots. If needed, create a systemd unit or a udev rule for the PCI controller (`ATTR{power/wakeup}="enabled"` on the xHCI device).

The firmware reports the `hid_not_armed` fault (bit 2 of DP 114) if the host suspends the bus without arming *remote wakeup*: it means this part is not configured correctly.

### 3.2 Wake-on-LAN on the NIC

View and enable it with `ethtool` (does not persist across reboots):

```bash
sudo ethtool enp128s31f6 | grep -i wake     # "Supports Wake-on: pumbg"  "Wake-on: d" (disabled)
sudo ethtool -s enp128s31f6 wol g           # g = magic packet
sudo ethtool enp128s31f6 | grep -i 'Wake-on:'   # must say "Wake-on: g"
```

Persistent way with NetworkManager (Pop!_OS / Ubuntu):

```bash
nmcli -f NAME,DEVICE connection show                 # name of the enp128s31f6 connection
sudo nmcli connection modify "<connection>" 802-3-ethernet.wake-on-lan magic
sudo nmcli connection up "<connection>"
nmcli -g 802-3-ethernet.wake-on-lan connection show "<connection>"   # → magic
```

With systemd-networkd, use a `.link` file with `WakeOnLan=magic` instead.

The dongle sends the magic packet to the MACs it receives from the agent in the `hello` (at most 4, and only from physical NICs). That is why **the agent must have connected at least once** after pairing. To check it, look at the `agent:` line of `!status` in the dongle CLI (with the agent stopped): `macs=` must be ≥ 1. To test WOL from another machine on the same network: `wakeonlan fc:9d:05:18:ee:32` or `etherwake -i <if> fc:9d:05:18:ee:32`.

> WOL from S5 depends on the BIOS leaving the NIC powered (ErP disabled, Wake on LAN enabled) and on the driver (`e1000e` on Intel I219 NICs) leaving WOL armed at shutdown. Some versions of NetworkManager or the driver disable it at shutdown. If it fails from S5 but works from S3, this is the first place to check.

## 4. How to test it safely (S3 first, then S5)

Always have a recovery path ready: the **physical power button** keeps working in every case. Do not run these tests remotely without someone next to the PC.

### 4.1 Preliminary checks

1. The dongle is plugged into the chosen port and connected to Smart Life (the `pc_state` DP shows `on`).
2. The agent is active and paired: `systemctl status microesp-agent` and `agent_online = true` in the app.
3. The power-on method (DP 109 `wake_method`) is `hid_then_wol` (default value).
4. The dongle has the PC's MACs (the agent connected after pairing).

### 4.2 Suspend (S3)

```bash
# Safety net: the RTC wakes the machine after 180 s if the dongle does not
sudo rtcwake -m mem -s 180
```

While it is suspended (the `pc_state` DP changes to `sleep`), tap **Power on** in the app or **double-press** the dongle button. The PC must wake within a few seconds, well before the 180 s. To test without a safety net, use `systemctl suspend`.

Expected result: `last_result = wake_sent` → `pc_state` changes to `booting`/`on`. If it does not wake, check §3.1 (`power/wakeup`, `/proc/acpi/wakeup`) and the `hid_not_armed` bit.

### 4.3 Hibernation (S4), if configured

```bash
systemctl hibernate
```

Many Pop!_OS/Ubuntu installations do not have hibernation configured (it requires enough swap and `resume=`). If it is not, skip this case and mark it "N/A" in the E2E plan.

### 4.4 Power-off (S5)

1. **WOL only** first: set DP 109 to `wol`, power off with `systemctl poweroff` and, from the app, tap **Power on**. If the dongle powers off together with the PC (port not powered in S5), the WOL cannot leave the dongle: test it first from another machine with `wakeonlan`.
2. Then **HID only**: DP 109 = `hid`, power off and tap **Power on**. The firmware forces the *resume* signal 3 times, 2 s apart. It only works if the BIOS monitors the port in S5 (settings 3 and 4 in the table).
3. Finally, the default method `hid_then_wol`.
4. If after 120 s the USB bus has not mounted, the dongle sets `last_result = wake_failed`. In that case power on with the physical button and review the table in §2.

Optional safety net for S5: `sudo rtcwake -m off -s 300` (needs the BIOS to allow RTC wakeup, setting 8; not verified on this machine).

## 5. What remains to be confirmed

- [ ] Exact name of each option in §2 in the P3 Ultra SFF G2 BIOS and the BIOS version (`sudo dmidecode -s bios-version`).
- [ ] Whether there is any "USB wake"/"keyboard power on" option from S4/S5 (besides *Smart Power On*).
- [ ] That the dongle's Alt+P powers on from S5 on the *smart power on* connector (E2E-13) and which `hid_proto` the BIOS requests in S5.
- [ ] Which USB ports provide 5 V in S5.
- [ ] S3/S4/S5 × HID/WOL results matrix (MESP-US-0002, [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md)).

## Sources

- Lenovo ThinkStation P3 Ultra SFF G2, user guide (enter the BIOS with F1, Power → Smart Power On): [ManualsLib][ug-g2]
- Lenovo ThinkStation P3 Ultra, user guide (rear connector "USB-A 3.2 Gen 2 connector (smart power on)"; "With the smart power-on feature enabled, you can start up or wake up the computer from the hibernation mode by pressing Alt+P"): [PDF][ug-p3u]
- Lenovo, "Enabling or disabling the ErP LPS compliance mode" (Power → Enhanced Power Saving Mode; disable Wake on LAN if enabled; Wake Up on Alarm, After Power Loss): [download.lenovo.com][erp]
- Lenovo ThinkCentre, hardware maintenance manual (Automatic Power On, Wake on LAN): [ManualsLib][tc-hmm]
- Lenovo forum, "After Power Loss": [forums.lenovo.com][forum-apl]
- ThinkStation P3 Ultra SFF Gen 2 specifications (PSREF): [psref.lenovo.com][psref]

[ug-g2]: https://www.manualslib.com/manual/3960124/Lenovo-Thinkstation-P3-Ultra-Sff-G2.html
[ug-p3u]: https://doi-product-assets.s3.amazonaws.com/pub/User-Manual/1077202329.pdf
[erp]: https://download.lenovo.com/pccbbs/pubs/p330_tiny/html_en/en/Enabling_or_disabling_the_ErP_LPS_compliance_mode_(topic)_T0000763260.html
[tc-hmm]: https://www.manualslib.com/manual/701412/Lenovo-Thinkcentre-Edge.html?page=173
[forum-apl]: https://forums.lenovo.com/t5/ThinkCentre-A-E-M-S-Series/After-Power-Loss/m-p/5248695
[psref]: https://psref.lenovo.com/syspool/Sys/PDF/ThinkStation/ThinkStation_P3_Ultra_SFF_Gen_2/ThinkStation_P3_Ultra_SFF_Gen_2_Spec.pdf
