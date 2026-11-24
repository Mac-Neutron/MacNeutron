#!/bin/sh
# Full 15-file / 18-face Proton Windows-named font suite (Mac-Neutron/Proton's Makefile.in "Fonts"):
#   - Liberation (6): arial.ttf, arialbd.ttf, times.ttf, georgia.ttf, cour.ttf, courbd.ttf
#   - Source Han Sans (4): msyh.ttf, msyhbd.ttf, simsun.ttc (SimSun + NSimSun), malgun.ttf
#   - UmeFont (1): msgothic.ttc (MS Gothic + MS PGothic + MS UI Gothic)
#   - Noto merged (4): micross.ttf, nirmala.ttf, lucon.ttf, alt/arial.ttf
# Runs both in the Fedora builder VM (defaults: PROTON_FONTS_DIR=$H/proton-fonts, FONTS_OUT=$HOME/mn-fonts-out)
# and on macOS (wine-arm64/build.sh passes PROTON_FONTS_DIR=$SRC/lsteamclient/fonts, FONTS_OUT=$SRC/fonts-out).
set -eu
H=$(cd "$(dirname "$0")" && pwd)
. "$H/pins"

PROTON_FONTS_DIR="${PROTON_FONTS_DIR:-$H/proton-fonts}"
FONTS_OUT="${FONTS_OUT:-$HOME/mn-fonts-out}"
LIBERATION_TARBALL="${LIBERATION_TARBALL:-$H/liberation-fonts-$LIBERATION_COMMIT.tar.gz}"
[ -z "${SOURCE_DATE_EPOCH:-}" ] || export SOURCE_DATE_EPOCH

PF=$PROTON_FONTS_DIR
O=$FONTS_OUT

# Platform tool setup.
OS=$(uname -s)
if [ "$OS" = Linux ]; then
  missing_rpms=""
  for pkg in fontforge patch python3-fonttools; do
    rpm -q "$pkg" > /dev/null 2>&1 || missing_rpms="$missing_rpms $pkg"
  done
  [ -z "$missing_rpms" ] || sudo dnf -y -q install $missing_rpms
  if ! command -v makeotf > /dev/null 2>&1; then
    VENV="$HOME/mn-fonts-venv"
    if [ ! -x "$VENV/bin/makeotf" ] && [ ! -x "$VENV/bin/afdko" ]; then
      if sudo dnf -y -q install afdko > /dev/null 2>&1; then
        :
      else
        missing_afdko_rpms=""
        for pkg in python3-pip python3-devel cmake gcc-c++; do
          rpm -q "$pkg" > /dev/null 2>&1 || missing_afdko_rpms="$missing_afdko_rpms $pkg"
        done
        [ -z "$missing_afdko_rpms" ] || sudo dnf -y -q install $missing_afdko_rpms
        rm -rf "$VENV"
        python3 -m venv "$VENV"
        "$VENV/bin/pip" install -q afdko
      fi
    fi
    [ ! -d "$VENV/bin" ] || PATH="$VENV/bin:$PATH"
  fi
elif [ "$OS" = Darwin ]; then
  ROOT_DEFAULT=$(cd "$H/../.." && pwd)
  B_DIR="${BUILD_DIR:-$ROOT_DEFAULT/build}"
  [ ! -d "$B_DIR/fonts-venv/bin" ] || PATH="$B_DIR/fonts-venv/bin:$PATH"
fi
export PATH

for cmd in fontforge patch ttx python3; do
  command -v "$cmd" > /dev/null 2>&1 || { echo "vm/fonts/build.sh: missing required tool: $cmd" >&2; exit 1; }
done
python3 -c 'import fontTools' 2> /dev/null || { echo "vm/fonts/build.sh: python3 lacks fontTools" >&2; exit 1; }

if command -v makeotf > /dev/null 2>&1 && command -v tx > /dev/null 2>&1 \
  && command -v sfntedit > /dev/null 2>&1 && command -v otf2ttf > /dev/null 2>&1 \
  && command -v otf2otc > /dev/null 2>&1; then
  afdko_run() { "$@"; }
elif command -v afdko > /dev/null 2>&1; then
  afdko_run() { afdko "$@"; }
else
  echo "vm/fonts/build.sh: missing afdko (makeotf, tx, sfntedit, otf2ttf, otf2otc)" >&2
  exit 1
fi

[ -f "$LIBERATION_TARBALL" ] || { echo "vm/fonts/build.sh: missing tarball: $LIBERATION_TARBALL" >&2; exit 1; }
[ -d "$PF/source-han-sans" ] && [ -d "$PF/ume" ] && [ -d "$PF/noto" ] \
  && [ -d "$PF/patches" ] && [ -d "$PF/ranges" ] && [ -f "$PF/scripts/merge.py" ] \
  || { echo "vm/fonts/build.sh: incomplete Proton fonts dir at $PF" >&2; exit 1; }

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

OUT_TMP="$W/out"
SHS="$W/source-han"
UME="$W/ume-gothic"
mkdir -p "$OUT_TMP/alt" "$OUT_TMP/licenses" "$SHS" "$UME"

# 1. Liberation (6 files).
tar -xzf "$LIBERATION_TARBALL" -C "$W"
S="$W/liberation-fonts-$LIBERATION_COMMIT"
patch -s "$S/src/LiberationMono-Regular.sfd" "$H/LiberationMono-Regular.patch"
gen() {  # gen <out> <Liberation source> <PostScript name> <family> <full name>
  fontforge -quiet -script "$H/generatefont.pe" "$S/src/$2.sfd" "$3" "$4" "$5" "$OUT_TMP/$1.ttf" \
    > "$W/$1.log" 2>&1 || { cat "$W/$1.log" >&2; exit 1; }
}
gen arial LiberationSans-Regular Arial Arial Arial
gen arialbd LiberationSans-Bold Arial-Bold Arial "Arial Bold"
gen times LiberationSerif-Regular TimesNewRoman "Times New Roman" "Times New Roman"
gen georgia LiberationSerif-Regular Georgia Georgia Georgia
gen cour LiberationMono-Regular CourierNew "Courier New" "Courier New"
gen courbd LiberationMono-Bold CourierNewPS-BoldMT "Courier New" "Courier New Bold"

# 2. Source Han Sans (4 files: msyh.ttf, msyhbd.ttf, simsun.ttc, malgun.ttf).
SHS_DIR="$PF/source-han-sans"
build_shs() {  # build_shs <out> <cidfont> <features> <sequences> <unisource> <menunamedb> <to_ttf:0|1>
  _bs_out=$1 _bs_cid=$2 _bs_feat=$3 _bs_seq=$4 _bs_uni=$5 _bs_mdb=$6 _bs_ttf=$7
  _bs_name=$(basename "$_bs_out")
  _bs_log="$W/$_bs_name.log"
  {
    # makeotf writes current.fpr in cwd; isolate it in a dedicated temp dir.
    (
      _mo_td=$(mktemp -d) &&
      trap 'rm -rf "$_mo_td"' EXIT &&
      cd "$_mo_td" &&
      afdko_run makeotf -f "$_bs_cid" -omitMacNames -ff "$_bs_feat" \
        -lic ADOBE -mf "$_bs_mdb" -r -nS -cs 25 -ch "$_bs_uni" \
        -ci "$_bs_seq" -o "$_bs_out.tmp"
    ) &&
    afdko_run tx -cff +S -no_futile "$_bs_cid" "$_bs_out.cff" &&
    # sfntedit uses a hardcoded temporary file in cwd; isolate it in a dedicated temp dir.
    (
      _sf_td=$(mktemp -d) &&
      trap 'rm -rf "$_sf_td"' EXIT &&
      cd "$_sf_td" &&
      afdko_run sfntedit -a "CFF=$_bs_out.cff" "$_bs_out.tmp"
    ) &&
    rm -f "$_bs_out.cff" &&
    { [ "$_bs_ttf" != 1 ] || {
      afdko_run otf2ttf -o "$_bs_out.tmp.ttf" "$_bs_out.tmp" &&
      mv -f "$_bs_out.tmp.ttf" "$_bs_out.tmp"
    }; } &&
    mv "$_bs_out.tmp" "$_bs_out"
  } > "$_bs_log" 2>&1 || { cat "$_bs_log" >&2; exit 1; }
}
build_shs "$OUT_TMP/msyh.ttf" \
  "$SHS_DIR/Regular/cidfont.ps.OTC.SC" "$SHS_DIR/Regular/features.OTC.SC" \
  "$SHS_DIR/SourceHanSans_CN_sequences.txt" "$SHS_DIR/UniSourceHanSansCN-UTF32-H" \
  "$PF/patches/YaHei-FontMenuNameDB" 1
build_shs "$OUT_TMP/msyhbd.ttf" \
  "$SHS_DIR/Bold/cidfont.ps.OTC.SC" "$SHS_DIR/Bold/features.OTC.SC" \
  "$SHS_DIR/SourceHanSans_CN_sequences.txt" "$SHS_DIR/UniSourceHanSansCN-UTF32-H" \
  "$PF/patches/YaHeiBold-FontMenuNameDB" 1
build_shs "$SHS/simsun.ttf" \
  "$SHS_DIR/Regular/cidfont.ps.OTC.SC" "$SHS_DIR/Regular/features.OTC.SC" \
  "$SHS_DIR/SourceHanSans_CN_sequences.txt" "$SHS_DIR/UniSourceHanSansCN-UTF32-H" \
  "$PF/patches/SimSun-FontMenuNameDB" 0
build_shs "$SHS/nsimsun.ttf" \
  "$SHS_DIR/Regular/cidfont.ps.OTC.SC" "$SHS_DIR/Regular/features.OTC.SC" \
  "$SHS_DIR/SourceHanSans_CN_sequences.txt" "$SHS_DIR/UniSourceHanSansCN-UTF32-H" \
  "$PF/patches/NSimSun-FontMenuNameDB" 0
{
  afdko_run otf2otc -o "$OUT_TMP/simsun.ttc.tmp" "$SHS/simsun.ttf" "$SHS/nsimsun.ttf" &&
  afdko_run otf2ttf -o "$OUT_TMP/simsun.ttc" "$OUT_TMP/simsun.ttc.tmp" &&
  rm -f "$OUT_TMP/simsun.ttc.tmp"
} > "$W/simsun.ttc.log" 2>&1 || { cat "$W/simsun.ttc.log" >&2; exit 1; }
build_shs "$OUT_TMP/malgun.ttf" \
  "$SHS_DIR/Regular/cidfont.ps.OTC.K" "$SHS_DIR/Regular/features.OTC.K" \
  "$SHS_DIR/SourceHanSans_KR_sequences.txt" "$SHS_DIR/UniSourceHanSansKR-UTF32-H" \
  "$PF/patches/Malgun-FontMenuNameDB" 0

# 3. UmeFont (1 file: msgothic.ttc).
build_ume() {  # build_ume <out_ttf> <src_ttf> <patch_name>
  _bu_out=$1 _bu_src=$2 _bu_patch=$3
  _bu_ttx="$UME/$_bu_patch.ttx"
  _bu_log="$W/$(basename "$_bu_out").log"
  {
    ttx -q -o "$_bu_ttx" -t name "$PF/ume/$_bu_src" &&
    patch -s "$_bu_ttx" "$PF/patches/$_bu_patch.patch" &&
    ttx -q -o "$_bu_out" -m "$PF/ume/$_bu_src" "$_bu_ttx"
  } > "$_bu_log" 2>&1 || { cat "$_bu_log" >&2; exit 1; }
}
build_ume "$UME/msgothic.ttf" ume-tgo4.ttf UmeGothic-nametable
build_ume "$UME/mspgothic.ttf" ume-pgo4.ttf UmePGothic-nametable
build_ume "$UME/msuigothic.ttf" ume-ugo4.ttf UmeUIGothic-nametable
afdko_run otf2otc -o "$OUT_TMP/msgothic.ttc" "$UME/msgothic.ttf" "$UME/mspgothic.ttf" "$UME/msuigothic.ttf" \
  > "$W/msgothic.ttc.log" 2>&1 || { cat "$W/msgothic.ttc.log" >&2; exit 1; }

# 4. Noto merged (4 files: micross.ttf, nirmala.ttf, alt/arial.ttf, lucon.ttf).
N="$PF/noto"
merge_noto() {  # merge_noto <log_name> <args...>
  _mn_log="$W/$1.log"; shift
  python3 "$PF/scripts/merge.py" "$@" > "$_mn_log" 2>&1 || { cat "$_mn_log" >&2; exit 1; }
}
merge_noto micross \
  "$N/NotoSans-Regular.ttf" "$N/NotoSansArabic-Regular.ttf" "$N/NotoSansArmenian-Regular.ttf" \
  "$N/NotoSansBengali-Regular.ttf" "$N/NotoSansCoptic-Regular.ttf" "$N/NotoSansGeorgian-Regular.ttf" \
  "$N/NotoSansGujarati-Regular.ttf" "$N/NotoSansHebrew-Regular.ttf" "$N/NotoSansKhmer-Regular.ttf" \
  "$N/NotoSansTamil-Regular.ttf" "$N/NotoSansThaana-Regular.ttf" "$N/NotoSansThai-Regular.ttf" \
  "$N/NotoSansMath-Regular.ttf" "$N/NotoSansSymbols-Regular.ttf" "$N/NotoSansMono-Regular.ttf" \
  "MicrosoftSansSerif" "Microsoft Sans Serif" "Regular" "$PF/ranges/micross" "$OUT_TMP/micross.ttf"

merge_noto nirmala \
  "$N/NotoSans-Regular.ttf" "$N/NotoSansChakma-Regular.ttf" "$N/NotoSansBengaliUI-Regular.ttf" \
  "$N/NotoSansDevanagariUI-Regular.ttf" "$N/NotoSansGujaratiUI-Regular.ttf" "$N/NotoSansGurmukhiUI-Regular.ttf" \
  "$N/NotoSansKannadaUI-Regular.ttf" "$N/NotoSansMalayalamUI-Regular.ttf" "$N/NotoSansMeeteiMayek-Regular.ttf" \
  "$N/NotoSansOlChiki-Regular.ttf" "$N/NotoSansOriyaUI-Regular.ttf" "$N/NotoSansSinhalaUI-Regular.ttf" \
  "$N/NotoSansSoraSompeng-Regular.ttf" "$N/NotoSansTamilUI-Regular.ttf" "$N/NotoSansTeluguUI-Regular.ttf" \
  "$N/NotoSerifTibetan-Regular.ttf" "$N/NotoSansMath-Regular.ttf" "$N/NotoSansSymbols2-Regular.ttf" \
  "NirmalaUI" "Nirmala UI" "Regular" "$PF/ranges/nirmala" "$OUT_TMP/nirmala.ttf"

merge_noto alt-arial \
  "$N/NotoSans-Regular.ttf" "$N/NotoSansArabic-Regular.ttf" "$N/NotoSansHebrew-Regular.ttf" \
  "$N/NotoSansArmenian-Regular.ttf" "$N/NotoSansCoptic-Regular.ttf" "$N/NotoSansMath-Regular.ttf" \
  "$N/NotoSansSymbols-Regular.ttf" "$N/NotoSansSymbols2-Regular.ttf" "$N/NotoSansMono-Regular.ttf" \
  "Arial" "Arial" "Regular" "$PF/ranges/arial" "$OUT_TMP/alt/arial.ttf"

merge_noto lucon \
  "$N/NotoSansMono-Regular.ttf" "$N/NotoSansMath-Regular.ttf" "$N/NotoSansSymbols-Regular.ttf" \
  "$N/NotoSansSymbols2-Regular.ttf" \
  "LucidaConsole" "Lucida Console" "Regular" "$PF/ranges/lucon" "$OUT_TMP/lucon.ttf"

# 5. Licenses: all 5 texts under licenses/ plus top-level LICENSE (Liberation OFL) for backward compatibility.
cp "$S/LICENSE" "$OUT_TMP/LICENSE"
cp "$S/LICENSE" "$OUT_TMP/licenses/liberation-OFL.txt"
cp "$PF/noto/LICENSE" "$OUT_TMP/licenses/noto-OFL.txt"
cp "$PF/source-han-sans/LICENSE.txt" "$OUT_TMP/licenses/source-han-sans-OFL.txt"
cp "$PF/ume/license.html" "$OUT_TMP/licenses/ume-license.html"
cp "$H/LICENSE.proton" "$OUT_TMP/licenses/proton-fonts-LICENSE.txt"

# 6. Validate every output before staging atomically into $O.
for f in arial.ttf arialbd.ttf times.ttf georgia.ttf cour.ttf courbd.ttf \
  msyh.ttf msyhbd.ttf simsun.ttc malgun.ttf msgothic.ttc micross.ttf nirmala.ttf lucon.ttf alt/arial.ttf \
  LICENSE licenses/liberation-OFL.txt licenses/noto-OFL.txt licenses/source-han-sans-OFL.txt \
  licenses/ume-license.html licenses/proton-fonts-LICENSE.txt; do
  [ -s "$OUT_TMP/$f" ] || { echo "vm/fonts/build.sh: missing or empty output: $f" >&2; exit 1; }
done

mkdir -p "$(dirname "$O")"
rm -rf "$O"
mv "$OUT_TMP" "$O"
echo "fonts: $(cd "$O" && echo *.ttf *.ttc alt/arial.ttf)"
