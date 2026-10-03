#!/usr/bin/env python3
"""Select the MicroESP build flavour (release / dev) before tos.py runs.

TuyaOpen generates .build/cache/using.config from app_default.config only once, and
the header .build/include/tuya_kconfig.h only when it is missing. This script
regenerates using.config from app_default.config (+ CONFIG_MESP_DEV_CLI=y for dev)
and deletes the derived files so the next cmake configure rebuilds them. It does
nothing if the flavour did not change (stamp file).

usage: set_build_mode.py <release|dev> <app_dir> <tuyaopen_root>
"""
import os
import sys
import tempfile


def main():
    mode, app, tos = sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
    if mode not in ("release", "dev"):
        sys.exit("mode must be release or dev")
    cache = os.path.join(app, ".build", "cache")
    stamp = os.path.join(cache, "mesp_build_mode")
    using = os.path.join(cache, "using.config")
    try:
        with open(stamp) as f:
            if f.read().strip() == mode and os.path.exists(using):
                return
    except OSError:
        pass
    sys.path.insert(0, os.path.join(tos, "tools", "kconfiglib"))
    sys.path.insert(0, tos)
    from kconfiglib import Kconfig  # noqa: E402
    from set_catalog_config import set_catalog_config  # noqa: E402

    os.makedirs(cache, exist_ok=True)
    catalog = os.path.join(cache, "CatalogKconfig")
    set_catalog_config(os.path.join(tos, "boards"), os.path.join(tos, "src"), app, catalog)
    with open(os.path.join(app, "app_default.config")) as f:
        defcfg = f.read()
    defcfg += "\nCONFIG_MESP_DEV_CLI=%s\n" % ("y" if mode == "dev" else "n")
    with tempfile.NamedTemporaryFile("w", suffix=".config", delete=False) as t:
        t.write(defcfg)
        tmp = t.name
    try:
        os.environ["KCONFIG_CONFIG"] = using
        kconf = Kconfig(catalog, suppress_traceback=True, warn_to_stderr=False)
        kconf.load_config(tmp)
        kconf.write_config(using)
    finally:
        os.unlink(tmp)
    for derived in (os.path.join(cache, "using.cmake"), os.path.join(app, ".build", "include", "tuya_kconfig.h")):
        if os.path.exists(derived):
            os.unlink(derived)
    with open(stamp, "w") as f:
        f.write(mode + "\n")
    print("build mode: %s (MESP_DEV_CLI=%s)" % (mode, "y" if mode == "dev" else "n"))


if __name__ == "__main__":
    main()
