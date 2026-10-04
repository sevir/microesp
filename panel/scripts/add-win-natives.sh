#!/usr/bin/env bash
# Add the Windows x64 builds of the native packages to a node_modules that
# was installed on Linux, so the Tuya MiniApp IDE running under Wine (Windows
# Node) can run `ray build` on this same checkout. Versions are read from the
# installed Linux packages; nothing is written to package.json or the lockfile.
set -euo pipefail
cd "$(dirname "$0")/.."

ver() { node -p "require('./$1/package.json').version"; }

add() { # <target dir> <package> <version>
  local dir=$1 pkg=$2 v=$3 tmp
  [ -d "$dir" ] && [ "$(ver "$dir")" = "$v" ] && { echo "ok   $pkg@$v"; return; }
  tmp=$(mktemp -d)
  (cd "$tmp" && npm pack --silent "$pkg@$v" >/dev/null)
  rm -rf "$dir" && mkdir -p "$dir"
  tar -xzf "$tmp"/*.tgz -C "$dir" --strip-components=1
  rm -rf "$tmp"
  echo "add  $pkg@$v -> $dir"
}

while read -r linux win; do
  for d in $(find node_modules -type d -path "*/$linux" -not -path "*/.cache/*"); do
    add "${d%"$linux"}$win" "$win" "$(ver "$d")"
  done
done <<'LIST'
@esbuild/linux-x64 @esbuild/win32-x64
lightningcss-linux-x64-gnu lightningcss-win32-x64-msvc
@oxc-parser/binding-linux-x64-gnu @oxc-parser/binding-win32-x64-msvc
@tailwindcss/oxide-linux-x64-gnu @tailwindcss/oxide-win32-x64-msvc
LIST
