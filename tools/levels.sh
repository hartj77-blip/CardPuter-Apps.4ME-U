#!/usr/bin/env bash
# Which ZeroOS versions a release of the apps is built for (README.md,
# "Compatibility"): the newest ZeroOS release, and ZeroOS's main branch when
# its app level is different (the next release's level, so its apps are
# ready before it comes out).
#
# Prints three lines:
#   release=v0.3.4
#   levels=3,4
#   matrix=[{"abi":"3","ref":"v0.3.4"},{"abi":"4","ref":"main"}]
# Env: ZEROOS_REPO (default hartj77-blip/CardPuter-OS.4ME-U). Needs gh, signed in.
set -euo pipefail
REPO=${ZEROOS_REPO:-hartj77-blip/CardPuter-OS.4ME-U}
abi_of() {
    gh api -H "Accept: application/vnd.github.raw" "repos/$REPO/contents/shell/src/ui/app.h?ref=$1" |
        sed -n 's/^#define ZS_APP_ABI \([0-9]*\).*/\1/p'
}
release=$(gh api "repos/$REPO/releases/latest" --jq .tag_name)
[[ $release =~ ^v[0-9][0-9A-Za-z.-]*$ ]] || { echo "error: no ZeroOS release found in $REPO" >&2; exit 1; }
rel_abi=$(abi_of "$release")
main_abi=$(abi_of main)
[[ $rel_abi =~ ^[0-9]+$ && $main_abi =~ ^[0-9]+$ ]] || { echo "error: could not read ZS_APP_ABI from $REPO" >&2; exit 1; }
echo "release=$release"
if [ "$main_abi" = "$rel_abi" ]; then
    echo "levels=$rel_abi"
    echo "matrix=[{\"abi\":\"$rel_abi\",\"ref\":\"$release\"}]"
else
    echo "levels=$rel_abi,$main_abi"
    echo "matrix=[{\"abi\":\"$rel_abi\",\"ref\":\"$release\"},{\"abi\":\"$main_abi\",\"ref\":\"main\"}]"
fi
