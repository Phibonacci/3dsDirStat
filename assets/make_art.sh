#!/bin/sh
# Generates the home-menu art for 3dsDirStat with ImageMagick:
#   icon.png   48x48   (SMDH icon)
#   banner.png 256x128 (3D banner image, needs bannertool to package)
# Same visual language as the app: a white rounded panel with a small treemap
# in the app's category colours. The banner has a transparent background.
set -e
cd "$(dirname "$0")"
TITLE_FONT=/usr/share/fonts/gsfonts/NimbusSans-Bold.otf
TEXT_FONT=/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf

# Treemap blocks: "x0,y0 x1,y1 colour" in a 0..100 x 0..100 unit square.
BLOCKS="0,0 54,100 #3B82F6
54,0 100,46 #8B5CF6
54,46 78,100 #22C55E
78,46 100,72 #EC4899
78,72 100,100 #F97316"

# treemap <x> <y> <w> <h> <out.png>: blocks with a soft top-to-bottom shade.
treemap() {
    X=$1; Y=$2; W=$3; H=$4; OUT=$5
    DRAW=""
    echo "$BLOCKS" | while read -r P0 P1 C; do
        X0=$(echo "$P0" | cut -d, -f1); Y0=$(echo "$P0" | cut -d, -f2)
        X1=$(echo "$P1" | cut -d, -f1); Y1=$(echo "$P1" | cut -d, -f2)
        # 1 px white gap between neighbouring blocks (like the app's map).
        PX0=$(( X0 * W / 100 )); PY0=$(( Y0 * H / 100 ))
        [ "$X0" -gt 0 ] && PX0=$(( PX0 + 1 ))
        [ "$Y0" -gt 0 ] && PY0=$(( PY0 + 1 ))
        PX1=$(( X1 * W / 100 - 1 )); PY1=$(( Y1 * H / 100 - 1 ))
        [ "$X1" -eq 100 ] && PX1=$(( W - 1 ))
        [ "$Y1" -eq 100 ] && PY1=$(( H - 1 ))
        echo "fill '$C' rectangle $PX0,$PY0 $PX1,$PY1"
    done > /tmp/.tm_draw.$$
    # Blocks on a transparent canvas; the shading is applied "atop" so it only
    # touches the blocks and the gaps stay clear (white once placed on a panel).
    magick -size "${W}x${H}" xc:none -draw "@/tmp/.tm_draw.$$" \
        \( -size "${W}x${H}" gradient:'rgba(255,255,255,0.35)-rgba(0,0,0,0.18)' \) \
        -compose atop -composite "$OUT"
    rm -f /tmp/.tm_draw.$$
}

# ---- icon (48x48) ----
# Opaque white over the whole 48x48: the HOME Menu draws its own frame, and
# any transparent pixels would show up black.
treemap 0 0 36 36 /tmp/.tm_icon.$$.png
magick -size 48x48 xc:white \
    /tmp/.tm_icon.$$.png -geometry +6+6 -compose over -composite \
    -alpha off icon.png
rm -f /tmp/.tm_icon.$$.png

# ---- banner (256x128) ----
treemap 0 0 78 78 /tmp/.tm_ban.$$.png
# Transparent canvas: only the rounded panel (and its soft shadow) is drawn.
magick -size 256x128 xc:none \
    -fill '#00000028' -draw 'roundrectangle 8,11 247,121 12,12' \
    -fill '#BCBCBC'   -draw 'roundrectangle 8,8 247,118 12,12' \
    -fill white       -draw 'roundrectangle 9,9 246,117 11,11' \
    /tmp/.tm_ban.$$.png -geometry +21+24 -compose over -composite \
    -font "$TITLE_FONT" -pointsize 23 -fill '#3A3A3A' -annotate +110+60 '3dsDirStat' \
    -font "$TEXT_FONT"  -pointsize 12 -fill '#7C7C7C' -annotate +111+80 'SD card disk usage' \
    -fill '#2563EB' -draw 'roundrectangle 111,90 232,94 2,2' \
    banner.png
rm -f /tmp/.tm_ban.$$.png
echo "icon.png banner.png written"
