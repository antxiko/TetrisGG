"""Genera tetrisgg/font_tiles.h a partir de la fuente PNG (rejilla ASCII 16 col,
glifos 8x8 empezando en el espacio, 2 colores: fondo negro + glifo).

Uso: python tools/gen_font.py ["01 - AccessDenied Mono D.png"]

Salida (tiles 4bpp planares del VDP, 32 bytes por tile):
  font_normal[]   : '0'..'9', 'A'..'Z', '-'   texto sobre panel (color 3),
                    degradado azul claro (15) filas 0-3 -> azul (10) filas 4-7
  font_inverted[] : 'A'..'Z'                  texto dentro de las cápsulas,
                    subido 1 px (filas 0-6; la 7 es margen inferior),
                    sobre crema (4), azul (10) filas 0-2 -> marino (0) 3-6
"""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "01 - AccessDenied Mono D.png"
OUT = ROOT / "tetrisgg" / "font_tiles.h"

NORMAL_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-"
INVERTED_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
SPLIT_ROW = 4            # filas < SPLIT_ROW usan el color de arriba

img = Image.open(SRC).convert("RGB")
BG = img.getpixel((0, 0))


def glyph_rows(ch, shift=0):
    c = ord(ch) - 32
    gx, gy = (c % 16) * 8, (c // 16) * 8
    rows = []
    for y in range(8):
        m = 0
        for x in range(8):
            if img.getpixel((gx + x, gy + y)) != BG:
                m |= 0x80 >> x
        rows.append(m)
    # shift>0 sube el glifo (las mayúsculas usan filas 1-7)
    return rows[shift:] + [0] * shift


def tile(rows, fg_top, fg_bottom, bg, split=SPLIT_ROW):
    out = []
    for y, m in enumerate(rows):
        fg = fg_top if y < split else fg_bottom
        for b in range(4):
            plane = (m if (fg >> b) & 1 else 0) | ((~m & 0xFF) if (bg >> b) & 1 else 0)
            out.append(plane)
    return out


def emit(name, chars, fg_top, fg_bottom, bg, shift=0):
    lines = [f"static const unsigned char {name}[] = {{"]
    for ch in chars:
        data = tile(glyph_rows(ch, shift), fg_top, fg_bottom, bg, SPLIT_ROW - shift)
        lines.append(f"  /* {ch} */ " + ",".join(f"0x{v:02X}" for v in data) + ",")
    lines.append("};")
    return "\n".join(lines)


header = f"""/* GENERADO por tools/gen_font.py a partir de "{SRC.name}". No editar a mano. */
#ifndef FONT_TILES_H
#define FONT_TILES_H

#define FONT_NORMAL_CHARS   "{NORMAL_CHARS}"
#define FONT_NORMAL_COUNT   {len(NORMAL_CHARS)}
#define FONT_INVERTED_COUNT {len(INVERTED_CHARS)}

{emit("font_normal", NORMAL_CHARS, 15, 10, 3)}

{emit("font_inverted", INVERTED_CHARS, 10, 0, 4, shift=1)}

#endif
"""
OUT.write_text(header, encoding="utf-8", newline="\n")
print(f"{OUT.relative_to(ROOT)}: {len(NORMAL_CHARS)} normales + {len(INVERTED_CHARS)} invertidos")
