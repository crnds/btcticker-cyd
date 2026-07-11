# Regenerates src/ui/fonts/PriceFont.h — a GFXfont containing only the
# glyphs the price row ever draws (digits, comma, dash for the "-----"
# placeholder). TFT_eSPI's bundled GFXFF set tops out at 24pt; this gives
# the price a monospace face that fills PRICE_Y_H in screen.cpp.
#
# Mono glyphs are naturally wider than proportional ones, so a height-filling
# size would overflow the 320 px row. We render tall, then squeeze each glyph
# horizontally just enough that "999,999" still fits (see MAX_W / H_SCALE).
#
# Requires Pillow (`pip install pillow`) and a bold monospace TTF. Default is
# the vendored JetBrains Mono Bold under fonts/ (OFL) — modern, high x-height,
# easier to read on the small TFT than Courier. Alternatives in fonts/:
#   IBMPlexMono-Bold.ttf, FiraMono-Bold.ttf
import os
from PIL import Image, ImageDraw, ImageFont

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = os.path.join(_SCRIPT_DIR, "..", "fonts", "JetBrainsMono-Bold.ttf")
# Keep in sync with screen.cpp PRICE_Y_H / PRICE_LEFT_PAD
PRICE_Y_H = 104
PRICE_LEFT_PAD = 8
MAX_W = 320 - 2 * PRICE_LEFT_PAD
# leave a few px of vertical pad so glyphs don't clip the fillRect
MAX_GLYPH_H = PRICE_Y_H - 8
# hardest condensation allowed — below this, squeezed stems get uneven
SQUEEZE_FLOOR = 0.78
FIRST, LAST = 0x2C, 0x39   # ',' '-' '.' '/' '0'-'9' (range must stay contiguous)
# The price row only ever draws digits, comma (thousands) and dash (the
# "-----" placeholder). '.' and '/' just fill out the contiguous code range a
# GFXfont requires — but '/' spans corner-to-corner diagonally at this size,
# far taller than any digit, so it must NOT count toward sizing or the
# runtime ascent scan. TFT_eSPI's setFreeFont() computes vertical-centering
# metrics (glyph_ab/glyph_bb, used by ML_DATUM etc.) by scanning every glyph
# in the font's range — an unused outlier glyph silently skews where the
# price digits land. Fix: render '.' and '/' as zero-metric blanks below.
USED = {0x2C, 0x2D} | set(range(0x30, 0x3A))  # ',' '-' '0'-'9'
PAD = 200
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "src", "ui", "fonts", "PriceFont.h")

# Size by the real ink bbox of the glyphs we actually draw, not the font's
# full metrics (JetBrains Mono's generous line gap would waste a third of
# the row) and not the unused '.'/'/' fillers (see USED above).
def glyph_set_height(f):
    img = Image.new("L", (4, 4))
    d = ImageDraw.Draw(img)
    top, bottom = 10**6, -10**6
    for code in USED:
        bbox = d.textbbox((0, 0), chr(code), font=f, anchor="ls")
        if bbox[3] > bbox[1]:
            top, bottom = min(top, bbox[1]), max(bottom, bbox[3])
    return bottom - top

# largest px that fits the row height without squeezing harder than the floor
FONT_PX = 8
for trial in range(8, 240):
    f = ImageFont.truetype(FONT_PATH, trial)
    if glyph_set_height(f) > MAX_GLYPH_H: break
    if MAX_W / f.getlength("999,999") < SQUEEZE_FLOOR: break
    FONT_PX = trial

font = ImageFont.truetype(FONT_PATH, FONT_PX)
natural_w = font.getlength("999,999")
# squeeze horizontally only when the height-filling face is too wide
H_SCALE = min(1.0, MAX_W / natural_w)

canvas = Image.new("L", (PAD * 2, PAD * 2), 0)
draw = ImageDraw.Draw(canvas)

bitmap = bytearray()
glyphs = []  # (offset, width, height, xAdvance, xOffset, yOffset)

for code in range(FIRST, LAST + 1):
    if code not in USED:
        # blank filler glyph — keeps the code range contiguous without
        # polluting sizing or the runtime ascent/descent scan
        glyphs.append((len(bitmap), 0, 0, 0, 0, 0))
        continue
    ch = chr(code)
    bbox = draw.textbbox((PAD, PAD), ch, font=font, anchor="ls")
    left, top, right, bottom = bbox
    width, height = right - left, bottom - top
    adv = round(font.getlength(ch) * H_SCALE)
    xoff = round((left - PAD) * H_SCALE)
    yoff = top - PAD
    offset = len(bitmap)
    if width <= 0 or height <= 0:
        glyphs.append((offset, 0, 0, adv, 0, 0))
        continue
    canvas.paste(0, (0, 0, PAD * 2, PAD * 2))
    draw.text((PAD, PAD), ch, font=font, fill=255, anchor="ls")
    crop = canvas.crop((left, top, right, bottom))
    if H_SCALE < 1.0:
        new_w = max(1, round(width * H_SCALE))
        # LANCZOS + the >=128 threshold below keeps stem widths even under
        # stronger condensation (NEAREST dropped whole columns)
        crop = crop.resize((new_w, height), Image.Resampling.LANCZOS)
        width = new_w
    px = crop.load()
    bits = []
    for y in range(height):
        for x in range(width):
            bits.append(1 if px[x, y] >= 128 else 0)
    nbytes = (len(bits) + 7) // 8
    gbytes = bytearray(nbytes)
    for i, b in enumerate(bits):
        if b:
            gbytes[i // 8] |= (0x80 >> (i % 8))
    bitmap += gbytes
    glyphs.append((offset, width, height, adv, xoff, yoff))

ascent, descent = font.getmetrics()
yAdvance = ascent + descent

names = {0x2C: "','", 0x2D: "'-'", 0x2E: "'.'", 0x2F: "'/'"}
for i in range(10):
    names[0x30 + i] = f"'{i}'"

lines = []
face = os.path.splitext(os.path.basename(FONT_PATH))[0]
lines.append(
    "// Auto-generated bold monospace digit font for the price row "
    "(%s @ %dpx, h-scale %.3f)." % (face, FONT_PX, H_SCALE)
)
lines.append("// Sized to fill PRICE_Y_H; glyphs squeezed horizontally so")
lines.append("// \"999,999\" fits the full row width. Glyph set: ',' '-' '.' '/' '0'-'9'.")
lines.append("// Regenerate with scripts/gen_price_font.py if size/layout changes.")
lines.append("#pragma once")
lines.append("")
lines.append("const uint8_t PriceFontBitmaps[] PROGMEM = {")
for i in range(0, len(bitmap), 12):
    chunk = bitmap[i:i + 12]
    lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
lines.append("};")
lines.append("")
lines.append("const GFXglyph PriceFontGlyphs[] PROGMEM = {")
for idx, (offset, width, height, adv, xoff, yoff) in enumerate(glyphs):
    code = FIRST + idx
    comma = "," if idx < len(glyphs) - 1 else ""
    lines.append(
        f"  {{ {offset:6d}, {width:3d}, {height:3d}, {adv:3d}, {xoff:4d}, {yoff:4d} }}"
        f"{comma}  // 0x{code:02X} {names[code]}"
    )
lines.append("};")
lines.append("")
lines.append("const GFXfont PriceFont PROGMEM = {")
lines.append("  (uint8_t  *)PriceFontBitmaps,")
lines.append("  (GFXglyph *)PriceFontGlyphs,")
lines.append(f"  0x{FIRST:02X}, 0x{LAST:02X}, {yAdvance} }};")
lines.append("")
lines.append(f"// {len(bitmap)} bytes bitmap, {len(glyphs)} glyphs, H_SCALE={H_SCALE:.3f}")

with open(OUT_PATH, "w") as f:
    f.write("\n".join(lines) + "\n")
print(
    f"wrote {OUT_PATH}: FONT_PX={FONT_PX} H_SCALE={H_SCALE:.3f} "
    f"yAdvance={yAdvance} bitmap={len(bitmap)}B naturalW={natural_w:.0f} maxW={MAX_W}"
)
