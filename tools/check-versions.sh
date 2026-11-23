#!/usr/bin/env bash
# Fails when an app changed but its APP_VERSION didn't: a device only takes
# an update when the version goes up.
#
# Usage: tools/check-versions.sh <base-ref>     (for example origin/main)
# A change to a file in apps/kit counts for every app that uses it: its
# app.cmake or its code names that file (kit/run.cpp and kit/run.h go together).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BASE=$1
version_at() { { git show "$1:apps/$2/$2.pkg" 2>/dev/null || true; } | sed -n 's/^APP_VERSION="\(.*\)"$/\1/p'; }
changed=$(git diff --name-only "$BASE"...HEAD -- apps)
# The kit files changed, as stems: apps/kit/run.cpp -> kit/run.
kit_stems=$(echo "$changed" | sed -n 's|^apps/\(kit/[^/]*\)\.[^./]*$|\1|p' | sort -u)
uses_kit() {   # app id: does it use a changed kit file?
    local stem
    for stem in $kit_stems; do
        grep -rqF "$stem." "apps/$1" && return 0
    done
    return 1
}
fails=0
for pkg in apps/*/*.pkg; do
    id=$(basename "$pkg" .pkg)
    touched=0
    # README.md and tests don't reach the device.
    echo "$changed" | grep "^apps/$id/" | grep -qv -e "^apps/$id/README.md$" -e "^apps/$id/tests/" && touched=1
    [ -n "$kit_stems" ] && uses_kit "$id" && touched=1
    [ "$touched" = 1 ] || continue
    old=$(version_at "$BASE" "$id")
    new=$(sed -n 's/^APP_VERSION="\(.*\)"$/\1/p' "$pkg")
    if [ -z "$old" ]; then
        echo "ok   $id is new ($new)"
    elif dpkg --compare-versions "$new" gt "$old"; then
        echo "ok   $id $old -> $new"
    else
        echo "FAIL $id changed but APP_VERSION is still $new: raise it in apps/$id/$id.pkg"
        fails=$((fails + 1))
    fi
done
[ "$fails" = 0 ] || exit 1
echo "check-versions: all passed"
