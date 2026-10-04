# Building and publishing the Smart Life panel

How the MicroESP panel (`panel/`, a Ray Panel MiniApp) goes from the repo to the device page in Smart Life: the Tuya MiniApp IDE on Linux (Windows build under Wine), previewing on the phone, and the upload → review → release → assign flow that finally made the panel show up on the real device.

Tested with Tuya MiniApp IDE 0.10.9 (Windows build), Wine 11, Node 22.23.3 for Windows, `@ray-js/cli` 1.7.83, Smart Life on Android, EU data center.

## 1. The IDE on Linux (Wine)

Tuya only ships the MiniApp IDE for Windows and macOS. The Windows build runs under Wine with three fixes, all automated in [`panel/scripts/ide-wine/`](../panel/scripts/ide-wine/):

| Problem | Fix |
|---|---|
| The NSIS installer fails under Wine | `setup.sh` unpacks the app from the installer payload (`$PLUGINSDIR/app-64.7z`) into `~/.wine-tuya` |
| The IDE asks for Node ≥ 16 and builds with the Windows `node.exe` | `setup.sh` installs Node for Windows (checksum verified) in `C:\Program Files\nodejs` and adds it to the prefix `PATH` |
| The IDE builds by typing `npx ray build` into a PowerShell terminal, and Wine's `powershell.exe` is a stub: the build never starts and the IDE fails with `ENOENT ''` | `patch-ide.mjs` patches `rayBuild()` in `@ark/miniapp-compiler` to spawn `node node_modules/@ray-js/cli/bin/ray build …` directly. Idempotent; keeps `index.js.orig` |

```bash
# Once (download "Tuya MiniApp IDE Setup X.Y.Z.exe" for Windows from the Tuya developer site)
panel/scripts/ide-wine/setup.sh ~/Downloads/"Tuya MiniApp IDE Setup 0.10.9.exe"

# After each npm install in panel/
cd panel
npm install
npm run ide:win-natives   # adds the win32-x64 native packages (esbuild, lightningcss, oxc, tailwind oxide)

# Launch
panel/scripts/ide-wine/run-ide.sh
```

Notes:

- `node_modules` installed on Linux only has the Linux native binaries. The IDE runs Ray with the Windows `node.exe`, which needs the win32 ones. `ide:win-natives` installs them side by side, so the Linux tooling (`npm test`, `npm run build:tuya`) keeps working.
- Updating or re-extracting the IDE drops the patch: run `node panel/scripts/ide-wine/patch-ide.mjs` again. If the IDE version changed and the pattern is not found, the script says so instead of guessing.
- Installing PowerShell 7 in the prefix does not help: it does not run under Wine.
- The Wine log (`~/.wine-tuya/wine-run.log`) can contain session tokens. Do not share it.

## 2. Import the project

1. Log in to the IDE with the Tuya developer account, **in the data center of the product** (EU for MicroESP). A session in the wrong data center (for example China) lets you log in but every upload fails with `USER_SESSION_LOSS` (HTTP 401).
2. **Import** the `panel/` folder. The IDE creates `typings/tuya*` (ignored by git) and may rewrite `project.tuya.json` (`uploadSourceMap`, `compilerOptions`).
3. `project.tuya.json` must have `"baseversion": "2.27.0"` or newer, or the IDE refuses to compile.

## 3. Preview on the phone

1. In the IDE, **Account** (top right) shows a QR code. Scan it with Smart Life (**+ → Scan**) using the account that owns the device.
2. **Plugins → Panel Tools → debug real device** and pick the device.
3. **Preview** shows another QR code. Scan it with Smart Life: the panel opens with the real device, without changing the panel that the product shows.

Power off and Reboot are real commands: to test them, confirm and cancel during the countdown.

## 4. Publish and assign it to the device

What did **not** work: creating the MiniApp only from the MiniApp Developer Platform, uploading and releasing it. The release was fine, but the panel had no associated product, so the product's **Change Panel → Self-Developed** list stayed empty.

What worked:

1. **Create a new panel for the product** on the Tuya Developer Platform, so it is associated with the MicroESP TuyaLink product from the start. It then appears in **App → Manage My Panels** with that product.
2. **Link it in the IDE**: in the project settings, select that panel/MiniApp and the product.
3. **Upload** the version from the IDE (top bar). It appears in the MiniApp version list on the platform.
4. **Validate**: in the version management of the MiniApp, submit the version for review/verification and wait until it is approved.
5. **Release** the approved version as the online version.
6. **Select it on the product**: in the product, **Application Development → Panel Development → Change Panel → Self-Developed**, choose the new panel and confirm.
7. In Smart Life, close and reopen the device page: it loads the new panel (the app may need a refresh of the device list the first time).

To ship a new version later, repeat steps 3–5. The product keeps the panel, so step 6 is not needed again.

## 5. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| IDE: compile error about the base library version | `baseversion` in `project.tuya.json` below 2.27.0 |
| IDE: `ENOENT ''` or the build never starts | Patch missing (re-run `patch-ide.mjs`) or Windows natives missing (`npm run ide:win-natives`) |
| raypack: `.less` files with "no loader" | Folders named `src/devices` or `src/i18n` are reserved by raypack. This project uses `src/device` and `src/strings` |
| Upload: `USER_SESSION_LOSS` (401) | IDE logged in to the wrong data center. Log out, choose the product's data center, log in again |
| Uploaded and released, but not in **Self-Developed** | The MiniApp is not associated with the product. Create the panel from the product side (section 4, step 1) and upload to that one |
| Panel opens empty on the phone | Check the device is online and the dongle is healthy (`!status` on the CDC CLI). The panel reads `getDeviceInfo().dps`, keyed by abilityId (`"101"`…`"114"`) |
