#!/bin/bash
# ci/apt-install.sh — install Ubuntu packages on a CI runner without letting a
# third-party apt repository fail the job.
#
# `apt-get update` exits non-zero when any configured repository fails, and runner
# images ship several this project never installs from. Source lists that name no
# ubuntu.com host are dropped before updating; Ubuntu's own are kept wherever the
# image puts them.
#
# Usage: ci/apt-install.sh <package>...
#        ci/apt-install.sh --prune-only
# APT_SOURCES_DIR overrides the directory that is pruned.
set -euo pipefail

sources_dir=${APT_SOURCES_DIR:-/etc/apt/sources.list.d}
prune_only=0
if [[ ${1:-} == --prune-only ]]; then
    prune_only=1
    shift
fi

remove() {
    if [[ -w $(dirname "$1") ]]; then
        rm -f "$1"
    else
        sudo rm -f "$1"
    fi
}

if [[ -d $sources_dir ]]; then
    for list in "$sources_dir"/*.list "$sources_dir"/*.sources; do
        [[ -f $list ]] || continue
        if grep -qE '://[^[:space:]]*\.ubuntu\.com' "$list"; then
            echo "apt-install: keeping $(basename "$list")"
        else
            echo "apt-install: dropping $(basename "$list")"
            remove "$list"
        fi
    done
fi

if [[ $prune_only -eq 1 ]]; then
    exit 0
fi
if [[ $# -eq 0 ]]; then
    echo "apt-install: no packages named" >&2
    exit 1
fi

sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$@"
