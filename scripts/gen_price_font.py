# Regenerates src/ui/fonts/PriceFont.h — a GFXfont containing only the
# glyphs the price row ever draws (digits, comma, dash for the "-----"
# placeholder). TFT_eSPI's bundled GFXFF set tops out at 24pt; this gives
# the price a noticeably bigger, still-legible size tuned to just fit the
# space left of the F&G/change column (see SIDE_COL_X0 in screen.cpp).
#
# Requires Pillow (`pip install pillow`) and a bold sans TTF — defaults to
# macOS's bundled Arial Bold; point FONT_PATH at another bold sans TTF on
# other platforms.
import os
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
FONT_PX = 66
FIRST, LAST = 0x2C, 0x39   # ',' '-' '.' '/' '0'-'9'
PAD = 200
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "src", "ui", "fonts", "PriceFont.h")

font = ImageFont.truetype(FONT_PATH, FONT_PX)
canvas = Image.new("L", (PAD*2, PAD*2), 0)
draw = ImageDraw.Draw(canvas)

bitmap = bytearray()
glyphs = []  # (offset, width, height, xAdvance, xOffset, yOffset)

for code in range(FIRST, LAST + 1):
    ch = chr(code)
    bbox = draw.textbbox((PAD, PAD), ch, font=font, anchor="ls")
    left, top, right, bottom = bbox
    width, height = right - left, bottom - top
    adv = round(font.getlength(ch))
    offset = len(bitmap)
    if width <= 0 or height <= 0:
        glyphs.append((offset, 0, 0, adv, 0, 0))
        continue
    canvas.paste(0, (0, 0, PAD*2, PAD*2))
    draw.text((PAD, PAD), ch, font=font, fill=255, anchor="ls")
    crop = canvas.crop((left, top, right, bottom))
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
    glyphs.append((offset, width, height, adv, left - PAD, top - PAD))

ascent, descent = font.getmetrics()
yAdvance = ascent + descent

names = {0x2C: "','", 0x2D: "'-'", 0x2E: "'.'", 0x2F: "'/'"}
for i in range(10):
    names[0x30 + i] = f"'{i}'"

lines = []
lines.append("// Auto-generated bold digit font for the price row (Arial Bold @ %dpx)." % FONT_PX)
lines.append("// Glyph set: ',' '-' '.' '/' '0'-'9' — the only characters the price")
lines.append("// display ever needs. Regenerate with scripts/gen_price_font.py if the")
lines.append("// glyph set or size ever changes.")
lines.append("#pragma once")
lines.append("")
lines.append("const uint8_t PriceFontBitmaps[] PROGMEM = {")
for i in range(0, len(bitmap), 12):
    chunk = bitmap[i:i+12]
    lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
lines.append("};")
lines.append("")
lines.append("const GFXglyph PriceFontGlyphs[] PROGMEM = {")
for idx, (offset, width, height, adv, xoff, yoff) in enumerate(glyphs):
    code = FIRST + idx
    comma = "," if idx < len(glyphs) - 1 else ""
    lines.append(f"  {{ {offset:6d}, {width:3d}, {height:3d}, {adv:3d}, {xoff:4d}, {yoff:4d} }}{comma}  // 0x{code:02X} {names[code]}")
lines.append("};")
lines.append("")
lines.append(f"const GFXfont PriceFont PROGMEM = {{")
lines.append("  (uint8_t  *)PriceFontBitmaps,")
lines.append("  (GFXglyph *)PriceFontGlyphs,")
lines.append(f"  0x{FIRST:02X}, 0x{LAST:02X}, {yAdvance} }};")
lines.append("")
lines.append(f"// {len(bitmap)} bytes bitmap, {len(glyphs)} glyphs")

with open(OUT_PATH, "w") as f:
    f.write("\n".join(lines) + "\n")
print(f"wrote {OUT_PATH}: {len(bitmap)} bytes bitmap, {len(glyphs)} glyphs, yAdvance={yAdvance}")
