#!/bin/sh
# published.sh <fork clone> <commit>: fails unless <commit> is on the fork's published `main` branch.
# MacNeutron ships DXMT under the LGPL, so the exact source of what it ships must be public (DXMT fork spec §9).
# release/release.sh runs it on build/wine-arm64-src/dxmt.
set -eu
clone=$1 commit=$2
git -C "$clone" fetch -q origin main || { echo "dxmt: can't reach the fork to check that $commit is published" >&2; exit 1; }
git -C "$clone" merge-base --is-ancestor "$commit" FETCH_HEAD 2> /dev/null \
  || { echo "dxmt: $commit isn't on the fork's main branch; push it before shipping (LGPL)" >&2; exit 1; }
