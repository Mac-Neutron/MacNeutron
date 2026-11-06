#!/bin/sh
# Proton's Windows-named fonts for the guest's Wine (Proton's Makefile.in "Fonts", its Liberation part): Arial, Arial
# Bold, Times New Roman, Georgia, Courier New and Courier New Bold, each a Liberation font under Proton's names, with
# Proton's patch for Liberation Mono. Not Microsoft's fonts: metric-compatible ones under the names programs ask for
# (Chromium 49's renderer, 3DMark's UI, died with no font of these). Runs in the builder (guest-stages.sh fonts) from
# ~/mn-fonts, this folder with the pinned tarball beside it, and writes ~/mn-fonts-out, the fonts and their licence,
# which image/build.sh puts in wine.erofs's share/fonts.
set -eu
H=$(cd "$(dirname "$0")" && pwd)
. "$H/pins"
rpm -q fontforge patch > /dev/null || sudo dnf -y -q install fontforge patch
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
tar -xzf "$H/liberation-fonts-$LIBERATION_COMMIT.tar.gz" -C "$W"
S=$W/liberation-fonts-$LIBERATION_COMMIT
patch -s "$S/src/LiberationMono-Regular.sfd" "$H/LiberationMono-Regular.patch"
O=$HOME/mn-fonts-out
rm -rf "$O"
mkdir -p "$O"
gen() {  # gen <out> <Liberation source> <PostScript name> <family> <full name>: Proton's NAMES for <out>
  fontforge -quiet -script "$H/generatefont.pe" "$S/src/$2.sfd" "$3" "$4" "$5" "$O/$1.ttf" 2> "$W/$1.log" ||
    { cat "$W/$1.log" >&2; exit 1; }
}
gen arial LiberationSans-Regular Arial Arial Arial
gen arialbd LiberationSans-Bold Arial-Bold Arial "Arial Bold"
gen times LiberationSerif-Regular TimesNewRoman "Times New Roman" "Times New Roman"
gen georgia LiberationSerif-Regular Georgia Georgia Georgia
gen cour LiberationMono-Regular CourierNew "Courier New" "Courier New"
gen courbd LiberationMono-Bold CourierNewPS-BoldMT "Courier New" "Courier New Bold"
cp "$S/LICENSE" "$O/LICENSE"  # the SIL OFL 1.1, which each copy of the fonts carries
echo "fonts: $(cd "$O" && echo *)"
