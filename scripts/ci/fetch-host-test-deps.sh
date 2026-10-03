#!/usr/bin/env bash
# Fetch ONLY the third-party sources needed by the firmware host tests
# (firmware/test/host/Makefile), pinned to the same commits used by the
# TuyaOpen v1.9.0 (b80932d) / ESP-IDF v5.4 checkout, without any ESP toolchain:
#   Unity   -> $DEST/esp-idf/components/unity/unity      (ThrowTheSwitch/Unity)
#   mbedTLS -> $DEST/esp-idf/components/mbedtls/mbedtls  (espressif/mbedtls)
#   cJSON   -> $DEST/TuyaOpen/src/libcjson/cJSON         (DaveGamble/cJSON)
# Then run:  TOS=$DEST/TuyaOpen IDF=$DEST/esp-idf firmware/build.sh test
# Usage: scripts/ci/fetch-host-test-deps.sh [DEST]   (default: .ci-deps)
set -euo pipefail
DEST="${1:-.ci-deps}"

UNITY_SHA=bf560290f6020737eafaa8b5cbd2177c3956c03f   # esp-idf v5.4 components/unity/unity
MBEDTLS_SHA=98fcfd6d2cea90d306e8fde8e5bffd6087c9cda8 # esp-idf v5.4 components/mbedtls/mbedtls
CJSON_SHA=f66cbab4bfb3926ffd4c5e13f9fb6d506ee0241d   # TuyaOpen b80932d src/libcjson/cJSON

# fetch_at URL SHA DIR: shallow checkout of a single commit (idempotent).
fetch_at() {
	local url=$1 sha=$2 dir=$3
	if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" = "$sha" ]; then
		echo "ok  $dir ($sha)"
		return
	fi
	rm -rf "$dir"
	mkdir -p "$dir"
	git -C "$dir" init -q
	git -C "$dir" remote add origin "$url"
	git -C "$dir" fetch -q --depth 1 origin "$sha"
	git -C "$dir" checkout -q FETCH_HEAD
	echo "got $dir ($sha)"
}

fetch_at https://github.com/ThrowTheSwitch/Unity.git "$UNITY_SHA" "$DEST/esp-idf/components/unity/unity"
fetch_at https://github.com/espressif/mbedtls.git "$MBEDTLS_SHA" "$DEST/esp-idf/components/mbedtls/mbedtls"
fetch_at https://github.com/DaveGamble/cJSON.git "$CJSON_SHA" "$DEST/TuyaOpen/src/libcjson/cJSON"
