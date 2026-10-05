---
created_at: 2026-10-05T21:14:21.368116569Z
updated_at: 2026-10-05T21:14:21.368116569Z
tags:
    - changes
    - panel
    - tuya-quirk
---
# 2026-10-05 — Panel: Restart button pushed out of the card

What changed: `panel/src/pages/home/index.module.less`, `.actionRow .btn { flex: 1 1 0; width: 0; min-width: 0; }`. CHANGELOG `Unreleased` / Fixed.

Why: owner's phone screenshot (Smart Life, panel 0.4.x) showed "Apagar" filling the row and "Reiniciar" clipped past the right edge of the hero card. `.btn { width: 100% }` lost against the MiniApp `button` default style (fixed width, no shrink). Tuya MiniApp quirk worth remembering: give flex children `flex-basis: 0` + `min-width: 0` instead of relying on `width: 100%` for `Button`.

Verification: `npm run typecheck`, `npm test` (14 pass), `npm run build:tuya`; the built `dist/tuya/pages/home/index.css` contains `.actionRow__… .btn__…{flex:1 1 0;width:0;min-width:0}`. Not yet seen on the device: needs IDE upload → review → release ([[panel-publishing.md]]).

Related: [[changes/2026-10-05-s5-wake-evidence.md]].
