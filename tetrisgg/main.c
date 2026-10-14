#include "SMSlib.h"

#include "music.h"

__sfr __at 0x00 GG_port00;    /* GG: bit 7 = Start button (0=pressed) */
#define GG_START_PRESSED()  (!(GG_port00 & 0x80))

/* ================================================================
   Tetris GG - iteración 3: HUD, NEXT, ladrillos, piezas 3D
   ================================================================ */

/* ---------------- Paleta BG (16 colores, 12-bit GG) -------------- */
static const unsigned int bg_palette[16] = {
  RGBHTML(0x0a0a2e),  /*  0 fondo azul oscuro          */
  RGBHTML(0xe8c547),  /*  1 ladrillo claro / oro       */
  RGBHTML(0x1a1a3f),  /*  2 celda tablero vacía        */
  RGBHTML(0x3a1a0a),  /*  3 panel HUD oscuro           */
  RGBHTML(0xf5e6b3),  /*  4 texto HUD / accent crema   */
  RGBHTML(0x00d7d7),  /*  5 pieza I cian               */
  RGBHTML(0xf0d020),  /*  6 pieza O amarillo           */
  RGBHTML(0xb040e0),  /*  7 pieza T magenta            */
  RGBHTML(0x40d040),  /*  8 pieza S verde              */
  RGBHTML(0xe04040),  /*  9 pieza Z rojo               */
  RGBHTML(0x4060f0),  /* 10 pieza J azul               */
  RGBHTML(0xf08020),  /* 11 pieza L naranja            */
  RGBHTML(0x200020),  /* 12 pieza shadow (casi negro púrpura) */
  RGBHTML(0xffffff),  /* 13 pieza highlight (blanco)   */
  RGBHTML(0x8a6b1f),  /* 14 ladrillo oscuro / mortero  */
  RGBHTML(0x8ad0ff)   /* 15 azul claro (texto, arriba)  */
};

/* ---------------- Helpers para generar bytes de tiles 4bpp ------- */
#define ROW(b0,b1,b2,b3) (b0),(b1),(b2),(b3)
#define SOLID_TILE(b0,b1,b2,b3) \
  ROW(b0,b1,b2,b3), ROW(b0,b1,b2,b3), ROW(b0,b1,b2,b3), ROW(b0,b1,b2,b3), \
  ROW(b0,b1,b2,b3), ROW(b0,b1,b2,b3), ROW(b0,b1,b2,b3), ROW(b0,b1,b2,b3)
#define P(n,bit) (((n)>>(bit))&1 ? 0xFF : 0x00)
#define SOLID_COLOR(n) SOLID_TILE(P(n,0), P(n,1), P(n,2), P(n,3))

/* ---------------- Tile de ladrillo (cols 1 y 14 brillo + mortero) ---
   Patrón repetible en vertical. Ladrillo claro (color 1) con
   junta oscura (color 14). Alternamos offset cada 4 filas para
   dar apariencia de bloques decalados.

   Plantilla 8x8:
     row 0: LLL.LLLL     <- ladrillo, junta vertical en col 3
     row 1: LLL.LLLL
     row 2: LLL.LLLL
     row 3: ........     <- junta horizontal
     row 4: L.LLLLLL     <- ladrillo, junta en col 1
     row 5: L.LLLLLL
     row 6: L.LLLLLL
     row 7: ........     <- junta horizontal

   Para color 1 (0001): p0=1, p1=p2=p3=0
   Para color 14 (1110): p0=0, p1=1, p2=1, p3=1
   Para cada bit B=1 (ladrillo):  p0=B, p1=~B, p2=~B, p3=~B  (aplicado por fila) */
#define BR(b) (b),((unsigned char)~(b)),((unsigned char)~(b)),((unsigned char)~(b))
static const unsigned char brick_tile[32] = {
  BR(0xEF), BR(0xEF), BR(0xEF),          /* ladrillo fila 0-2, junta en bit 4 */
  BR(0x00),                              /* junta horizontal */
  BR(0xBF), BR(0xBF), BR(0xBF),          /* ladrillo fila 4-6, junta en bit 6 */
  BR(0x00)                               /* junta horizontal */
};

/* ISR de VBlank: corre a 60Hz estable independiente del main loop.
   Hace música (music_frame) + animación de fondo (swap del tile TILE_EMPTY).
   Declaración forward porque se referencia antes de su definición. */
static void vblank_isr(void);

/* ---------------- Tiles animados del fondo del tablero ----------
   Pre-generamos 8 variantes del tile TILE_EMPTY, cada una con la
   diagonal `/` desplazada una posición. Cada N frames volcamos la
   siguiente a VRAM → todas las celdas vacías del tablero se animan
   a la vez sin tocar el tilemap.

   Base: color 2 (azul oscuro). Acento: color 14 (marrón mortero).
   Color 2 (0010): planes p0=0, p1=1, p2=0, p3=0
   Color 14 (1110): planes p0=0, p1=1, p2=1, p3=1
   Para pixel en diagonal (bit 1): planes 0, 0xFF, bit_mask, bit_mask */
static unsigned char anim_tiles[8 * 32];
static volatile unsigned char anim_frame_idx = 0;    /* escrito desde ISR */
static volatile unsigned char anim_counter = 0;
static volatile unsigned char anim_pending = 0;      /* 1 = main loop debe volcar tile */

static void gen_anim_tiles(void)
{
  unsigned char frame, row;
  for (frame = 0; frame < 8; frame++) {
    for (row = 0; row < 8; row++) {
      unsigned char bits = (unsigned char)(1 << ((row - frame) & 7));
      unsigned char *p = &anim_tiles[frame * 32 + row * 4];
      p[0] = 0x00;
      p[1] = 0xFF;
      p[2] = bits;
      p[3] = bits;
    }
  }
}

/* ---------------- Tileset estático -----------------------------
   Orden:
     0: fondo
     1: ladrillo (wall)
     2: celda vacía con grid
     3: panel HUD (color 3 sólido)
     4: panel accent (color 4 sólido)
     (5..11 pieza I..L se generan en RAM)
     (12..32 fuente se cargan desde font_tiles)                  */
static const unsigned char static_tiles[] = {
  SOLID_COLOR(0),                        /* 0: fondo */
};

/* ---------------- Fuente: AccessDenied Mono (PNG) en tonos azules.
   Los tiles salen de tools/gen_font.py -> font_tiles.h:
     font_normal   : 0-9, A-Z, '-'  sobre panel (color 3), degradado
                     azul claro (15) arriba -> azul (10) abajo
     font_inverted : A-Z dentro de las cápsulas, sobre crema (4),
                     azul (10) arriba -> marino (0) abajo            */
#include "font_tiles.h"

/* Tapas redondeadas de las cápsulas: bit 1 = crema (color 4), bit 0 =
   panel (color 3). color 4: p2=1; color 3: p0=p1=1.                  */
#define TXT_ROW(b) ((unsigned char)~(b)), ((unsigned char)~(b)), (unsigned char)(b), 0x00
#define TXT_TILE(r0,r1,r2,r3,r4,r5,r6,r7) \
  TXT_ROW(r0), TXT_ROW(r1), TXT_ROW(r2), TXT_ROW(r3), \
  TXT_ROW(r4), TXT_ROW(r5), TXT_ROW(r6), TXT_ROW(r7)
/* La cápsula mide 9 px: la fila de tiles de la cápsula (8 px, letra en
   filas 0-6 + margen) y encima un "labio" de 1 px (última fila de los
   tiles LIP). El redondeo es simétrico arriba y abajo.             */
static const unsigned char cap_tiles[] = {
  TXT_TILE(0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x3F),   /* L_cap */
  TXT_TILE(0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC),   /* R_cap */
  TXT_TILE(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F),   /* L_lip */
  TXT_TILE(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF),   /* lip   */
  TXT_TILE(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC)    /* R_lip */
};

/* ---------------- Tile indices --------------------------------- */
#define TILE_BG        0
#define TILE_BRICK     1
#define TILE_EMPTY     2
#define TILE_PANEL     3
#define TILE_PACCENT   4
#define TILE_PIECE(t)  (5 + (t))    /* t: 0..6 => I, O, T, S, Z, J, L */
#define TILE_DIGIT(d)  (12 + (d))            /* 0..9  -> 12..21 */
#define TILE_LETTER(c) (22 + ((c) - 'A'))    /* A..Z  -> 22..47 */
#define TILE_DASH      48
#define TILE_INV(c)    (49 + ((c) - 'A'))    /* A..Z invertidas -> 49..74 */
#define TILE_INV_BASE  49

#define TILE_LCAP      75
#define TILE_RCAP      76
#define TILE_WHITE     77     /* sólido color 13 (blanco) para flash de líneas */
#define TILE_GHOST     78     /* outline de celda para ghost piece             */
#define TILE_STAR      79     /* estrellita para splash screen                 */
#define TILE_MOON      80     /* luna para splash screen                       */
#define TILE_LIP_L     81     /* labio superior de la cápsula (1 px crema)     */
#define TILE_LIP       82
#define TILE_LIP_R     83
#define TILE_PUNCT     84     /* ! . , : < > (font_punct) -> 84..89 */
#define TILE_DARK      90     /* font_dark: misma tabla que normal+punct, fondo oscuro -> 90..132 */

/* ---------------- Generación en RAM de los 7 tiles de pieza -----
   Patrón uniforme "3D panel" para las 7 piezas: fila superior e
   izquierda con highlight (color 13, blanco), fila inferior y
   columna derecha con shadow (color 12, casi negro), interior con
   el color principal de la pieza (colores 5..11).
   Así cada pieza se distingue por color pero todas comparten el
   mismo look 3D. Los estilos "propios" por pieza (estrías, símbolo
   interior, etc) se harán en una iteración posterior de arte.    */
static unsigned char piece_tiles[7 * 32];   /* 224 bytes RAM      */

static void write_row_planes(unsigned char *dst, unsigned char color)
{
  /* Escribe 4 bytes = una fila plana totalmente rellena del color dado */
  dst[0] = (color & 1) ? 0xFF : 0x00;
  dst[1] = (color & 2) ? 0xFF : 0x00;
  dst[2] = (color & 4) ? 0xFF : 0x00;
  dst[3] = (color & 8) ? 0xFF : 0x00;
}

static void write_row_mixed(unsigned char *dst, unsigned char mask,
                            unsigned char color_on, unsigned char color_off)
{
  /* Para cada bit de mask: 1 -> color_on, 0 -> color_off.
     Cada bitplane = (mask & color_on_bit) | (~mask & color_off_bit). */
  unsigned char b;
  for (b = 0; b < 4; b++) {
    unsigned char on  = (color_on  >> b) & 1;
    unsigned char off = (color_off >> b) & 1;
    unsigned char plane = 0;
    if (on)  plane |= mask;
    if (off) plane |= (unsigned char)~mask;
    dst[b] = plane;
  }
}

static void gen_piece_tile(unsigned char *dst, unsigned char main_color)
{
  const unsigned char H = 13;  /* highlight (blanco) */
  const unsigned char S = 12;  /* shadow (casi negro) */
  unsigned char y;
  /* Fila 0: highlight completo */
  write_row_planes(dst, H); dst += 4;
  /* Filas 1..6: columna 0 = H, columnas 1..6 = main, columna 7 = S.
     Para un enfoque de 3 colores por fila usamos dos pasadas: primero
     mezclamos main y shadow en cols 1..7 según mask, luego sobreponemos
     highlight en col 0 vía write_row_mixed otra vez. Simplificamos
     dividiendo en dos regiones:                                        */
  for (y = 1; y < 7; y++) {
    /* Mask: bit 7 (col 0) = highlight, bits 1..6 (cols 1..6) = main,
       bit 0 (col 7) = shadow. Como write_row_mixed solo maneja 2
       colores, lo hacemos en 2 pasos: primero main vs shadow, luego
       sobrescribimos col 0 con highlight.                             */
    /* Paso 1: cols 0..6 = main, col 7 = shadow */
    write_row_mixed(dst, 0xFE, main_color, S);
    /* Paso 2: fuerza col 0 a highlight. Recalculamos planes
       combinando: para cada plane, bit 7 -> H plane bit. */
    {
      unsigned char b;
      for (b = 0; b < 4; b++) {
        unsigned char hb = (H >> b) & 1;
        dst[b] = (unsigned char)((dst[b] & 0x7F) | (hb ? 0x80 : 0x00));
      }
    }
    dst += 4;
  }
  /* Fila 7: shadow completo */
  write_row_planes(dst, S);
}

static void gen_all_piece_tiles(void)
{
  unsigned char i;
  for (i = 0; i < 7; i++)
    gen_piece_tile(&piece_tiles[i * 32], (unsigned char)(5 + i));
}

/* ================================================================
   Shadow buffer del name table
   ================================================================ */
#define NT_W 32
#define NT_H 28

static unsigned char shadow[NT_H][NT_W];
static unsigned char dirty_bits[NT_H][NT_W/8];
static unsigned char any_dirty;
#define FLUSH_BUDGET 64

static void nt_init(void)
{
  unsigned char y, x;
  for (y = 0; y < NT_H; y++) {
    for (x = 0; x < NT_W; x++) shadow[y][x] = 0;
    for (x = 0; x < NT_W/8; x++) dirty_bits[y][x] = 0;
  }
  any_dirty = 0;
}

static void nt_set(unsigned char x, unsigned char y, unsigned char tile)
{
  if (x >= NT_W || y >= NT_H) return;
  if (shadow[y][x] == tile) return;
  shadow[y][x] = tile;
  dirty_bits[y][x >> 3] |= (unsigned char)(1 << (x & 7));
  any_dirty = 1;
}

static void nt_flush_budget(unsigned char budget)
{
  unsigned char y, bi, x, bits;
  unsigned char count = 0;
  unsigned char remaining = 0;
  if (!any_dirty) return;       /* nada que volcar → saltar scan */
  for (y = 0; y < NT_H; y++) {
    for (bi = 0; bi < NT_W/8; bi++) {
      bits = dirty_bits[y][bi];
      if (!bits) continue;
      for (x = 0; x < 8; x++) {
        if (!(bits & (1 << x))) continue;
        if (budget && count >= budget) { remaining = 1; continue; }
        {
          unsigned char xx = (unsigned char)((bi << 3) | x);
          SMS_setTileatXY(xx, y, shadow[y][xx]);
          dirty_bits[y][bi] = (unsigned char)(dirty_bits[y][bi] & ~(1 << x));
          count++;
        }
      }
    }
  }
  any_dirty = remaining;
}
static void nt_flush(void)     { nt_flush_budget(FLUSH_BUDGET); }
static void nt_flush_all(void) { nt_flush_budget(0); }

/* ---- Coords de pantalla (GG ventana visible 20x18 con offset 6,3) ---- */
#define VIEW_X0 6
#define VIEW_Y0 3

static void screen_set(unsigned char sx, unsigned char sy, unsigned char tile)
{
  nt_set((unsigned char)(VIEW_X0 + sx), (unsigned char)(VIEW_Y0 + sy), tile);
}

/* ---- Escritura de texto (solo ASCII básico que definimos arriba) ---- */
/* 1 = texto sobre el fondo oscuro (título, intro); 0 = sobre el panel */
static unsigned char text_dark = 0;

static unsigned char char_to_tile_panel(char c);
static unsigned char char_to_tile(char c)
{
  unsigned char t = char_to_tile_panel(c);
  if (!text_dark) return t;
  if (t == TILE_PANEL) return TILE_BG;
  /* font_dark sigue el orden de font_normal (12..48) y luego font_punct */
  if (t >= TILE_PUNCT) return (unsigned char)(TILE_DARK + FONT_NORMAL_COUNT + (t - TILE_PUNCT));
  return (unsigned char)(TILE_DARK + (t - TILE_DIGIT(0)));
}

static unsigned char char_to_tile_panel(char c)
{
  if (c >= '0' && c <= '9') return (unsigned char)TILE_DIGIT(c - '0');
  if (c >= 'A' && c <= 'Z') return (unsigned char)TILE_LETTER(c);
  if (c == '-')             return TILE_DASH;
  if (c == '!')             return TILE_PUNCT;
  if (c == '.')             return TILE_PUNCT + 1;
  if (c == ',')             return TILE_PUNCT + 2;
  if (c == ':')             return TILE_PUNCT + 3;
  if (c == '<')             return TILE_PUNCT + 4;
  if (c == '>')             return TILE_PUNCT + 5;
  return TILE_PANEL;   /* cualquier otro = hueco en panel */
}

static void screen_print(unsigned char sx, unsigned char sy, const char *s)
{
  while (*s) {
    screen_set(sx++, sy, char_to_tile(*s));
    s++;
  }
}

/* Char -> tile INVERTIDO (oscuro sobre crema, para dentro de cápsulas) */
static unsigned char char_to_inv_tile(char c)
{
  if (c >= 'A' && c <= 'Z') return (unsigned char)TILE_INV(c);
  return TILE_PACCENT;   /* crema plano para relleno */
}

/* Pinta una cápsula con texto oscuro sobre crema y esquinas redondeadas.
   (sx, sy) = esquina sup-izq. La cápsula ocupa strlen(s)+2 tiles. */
static void screen_print_capsule(unsigned char sx, unsigned char sy, const char *s)
{
  unsigned char len = 0;
  const char *p = s;
  while (*p) { len++; p++; }
  screen_set(sx, sy, TILE_LCAP);
  {
    unsigned char i = 0;
    for (i = 0; i < len; i++)
      screen_set((unsigned char)(sx + 1 + i), sy, char_to_inv_tile(s[i]));
  }
  screen_set((unsigned char)(sx + 1 + len), sy, TILE_RCAP);
  /* Labio de 1 px en la fila de encima (siempre libre en el panel) */
  screen_set(sx, (unsigned char)(sy - 1), TILE_LIP_L);
  for (p = s; *p; p++)
    screen_set((unsigned char)(sx + 1 + (p - s)), (unsigned char)(sy - 1), TILE_LIP);
  screen_set((unsigned char)(sx + 1 + len), (unsigned char)(sy - 1), TILE_LIP_R);
}

/* ================================================================
   Geometría tablero
   ================================================================ */
#define BOARD_W   10
#define BOARD_H   20
#define VISIBLE_H 18
#define HIDDEN_H  (BOARD_H - VISIBLE_H)
#define BOARD_X0  1         /* col de pantalla donde empieza tablero    */
#define BOARD_Y0  0

static unsigned char board[BOARD_H][BOARD_W];

/* ================================================================
   Tetrominoes
   ================================================================ */
#define PIECE_I 0
#define PIECE_O 1
#define PIECE_T 2
#define PIECE_S 3
#define PIECE_Z 4
#define PIECE_J 5
#define PIECE_L 6

static const unsigned int shapes[7][4] = {
  /* I */ { 0x00F0, 0x4444, 0x0F00, 0x2222 },
  /* O */ { 0x0660, 0x0660, 0x0660, 0x0660 },
  /* T */ { 0x0072, 0x0262, 0x0270, 0x0232 },
  /* S */ { 0x0036, 0x0462, 0x0360, 0x0231 },
  /* Z */ { 0x0063, 0x0264, 0x0630, 0x0132 },
  /* J */ { 0x0071, 0x0226, 0x0470, 0x0322 },
  /* L */ { 0x0074, 0x0622, 0x0170, 0x0223 }
};

static unsigned char cur_type;
static unsigned char cur_rot;
static signed char   cur_x;
static signed char   cur_y;
static unsigned char next_type;    /* pieza que caerá después */

static unsigned char down_block = 0;  /* soft drop bloqueado hasta soltar ABAJO */

/* Lock delay (estilo guideline): al apoyarse, la pieza tarda LOCK_DELAY
   frames en fijarse. Mover/rotar reinicia la cuenta, como mucho
   LOCK_MAX_RESETS veces (si no, se podría girar eternamente). Bajar a
   una fila nueva devuelve las reinicializaciones. */
#define LOCK_DELAY       30
#define LOCK_MAX_RESETS  15
static unsigned char lock_timer;
static unsigned char lock_resets;
static signed char   lowest_y;
static unsigned char lock_now = 0;     /* hard drop: fijar ya */
static unsigned char last_rotate = 0;  /* última acción fue una rotación (T-spin) */
static unsigned char last_kick = 0;    /* índice del kick SRS usado (0..4) */

/* PRNG xorshift 16-bit — ciclo 65535, buena distribución */
static unsigned int rng_state;
static volatile unsigned int frame_count = 0;   /* lo incrementa la ISR */

/* La semilla sale del contador de frames de la ISR: depende de cuánto
   tarda el jugador en pulsar Start, así que cada partida es distinta.
   (Leer VCount justo tras el VBlank daba casi siempre el mismo valor.) */
static void rng_seed(void)
{
  rng_state = (unsigned int)(frame_count * 0x9E37u) ^ SMS_getVCount();
  if (rng_state == 0) rng_state = 0xBEEF;
}

static unsigned char rng_next(void)
{
  rng_state ^= rng_state << 7;
  rng_state ^= rng_state >> 9;
  rng_state ^= rng_state << 8;
  return (unsigned char)rng_state;
}

/* Bolsa de 7: cada tanda de 7 piezas contiene una de cada tipo en orden
   aleatorio. Evita sequías largas de palos y rachas de S/Z.          */
static unsigned char bag[7];
static unsigned char bag_pos = 7;

static unsigned char rand7(void)
{
  if (bag_pos >= 7) {
    unsigned char i, j, t;
    for (i = 0; i < 7; i++) bag[i] = i;
    for (i = 6; i > 0; i--) {
      j = (unsigned char)(rng_next() % (unsigned char)(i + 1));
      t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bag_pos = 0;
  }
  return bag[bag_pos++];
}

/* Optimizado: extrae el nibble de la fila primero (shift constante 0/4/8/12)
   y luego shift de 0..3 en el nibble. Evita shifts variables de 0..15 en
   el Z80 (sin barrel shifter = N ciclos por bit). */
static unsigned char shape_has(unsigned char t, unsigned char r,
                               unsigned char lx, unsigned char ly)
{
  unsigned int shape = shapes[t][r];
  unsigned char nibble;
  switch (ly) {
    case 0:  nibble = (unsigned char)(shape & 0x0F);        break;
    case 1:  nibble = (unsigned char)((shape >> 4) & 0x0F); break;
    case 2:  nibble = (unsigned char)((shape >> 8) & 0x0F); break;
    default: nibble = (unsigned char)((shape >> 12) & 0x0F); break;
  }
  return (nibble >> lx) & 1;
}

static unsigned char fits(unsigned char t, unsigned char r,
                          signed char x, signed char y)
{
  unsigned char lx, ly;
  signed char bx, by;
  for (ly = 0; ly < 4; ly++) {
    for (lx = 0; lx < 4; lx++) {
      if (!shape_has(t, r, lx, ly)) continue;
      bx = x + (signed char)lx;
      by = y + (signed char)ly;
      if (bx < 0 || bx >= BOARD_W) return 0;
      if (by >= BOARD_H)           return 0;
      if (by < 0)                  continue;
      if (board[by][bx])           return 0;
    }
  }
  return 1;
}

static void lock_piece(void)
{
  unsigned char lx, ly;
  signed char bx, by;
  unsigned char color = (unsigned char)(cur_type + 1);
  for (ly = 0; ly < 4; ly++) {
    for (lx = 0; lx < 4; lx++) {
      if (!shape_has(cur_type, cur_rot, lx, ly)) continue;
      bx = cur_x + (signed char)lx;
      by = cur_y + (signed char)ly;
      if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W)
        board[by][bx] = color;
    }
  }
}

static unsigned char clear_lines(void)
{
  signed char y, yy;
  unsigned char x, full, cleared = 0;
  for (y = BOARD_H - 1; y >= 0; y--) {
    full = 1;
    for (x = 0; x < BOARD_W; x++) {
      if (!board[y][x]) { full = 0; break; }
    }
    if (full) {
      cleared++;
      for (yy = y; yy > 0; yy--)
        for (x = 0; x < BOARD_W; x++)
          board[yy][x] = board[yy - 1][x];
      for (x = 0; x < BOARD_W; x++) board[0][x] = 0;
      y++;
    }
  }
  return cleared;
}

static unsigned char spawn_piece(void)
{
  cur_type = next_type;
  next_type = rand7();
  cur_rot  = 0;
  cur_x    = 3;
  cur_y    = (signed char)-1;
  down_block = 1;   /* si ABAJO sigue pulsado, no arrastrar el soft drop */
  lock_timer  = LOCK_DELAY;
  lock_resets = 0;
  lowest_y    = cur_y;
  last_rotate = 0;
  return fits(cur_type, cur_rot, cur_x, cur_y);
}

/* ================================================================
   Render
   ================================================================ */

/* Celda del tablero (coords tablero x,y -> pantalla) */
static void paint_board_cell(unsigned char x, unsigned char y)
{
  unsigned char cell = board[y][x];
  unsigned char sy;
  if (y < HIDDEN_H) return;
  sy = y - HIDDEN_H;
  screen_set(BOARD_X0 + x, BOARD_Y0 + sy,
             cell ? TILE_PIECE(cell - 1) : TILE_EMPTY);
}

/* Static layout: ladrillos en cols 0 y 11, tablero vacío cols 1-10,
   panel oscuro cols 12-19. Se pinta UNA vez al inicio. */
static void paint_static_layout(void)
{
  unsigned char x, y, tile;
  for (y = 0; y < 18; y++) {
    for (x = 0; x < 20; x++) {
      if (x == 0 || x == 11)           tile = TILE_BRICK;
      else if (x >= 1 && x <= 10)      tile = TILE_EMPTY;
      else                             tile = TILE_PANEL;
      screen_set(x, y, tile);
    }
  }
}

/* ================================================================
   HUD en el panel (cols 12..19, 8 tiles ancho)
     col 12: borde/sombra (TILE_PANEL dejado plano aqui)
     cols 13..18: interior
     col 19: borde derecho
   Layout vertical:
     fila  1: "SCORE"           (cols 13..17)
     fila  2: dígitos puntuación (cols 13..18, derecha)
     fila  4: "LEVEL"
     fila  5: dígito nivel
     fila  7: "LINES"
     fila  8: dígitos líneas
     fila 10: "NEXT"
     filas 11..15: cuadro 4x4 para preview
   ================================================================ */
static unsigned long score = 0;
static unsigned char level = 0;
static unsigned int  lines = 0;

/* ---- Modos de juego ---- */
#define MODE_MARATHON 0      /* sin fin, el nivel sube cada 10 líneas   */
#define MODE_40LINES  1      /* 40 líneas contrarreloj                  */
#define MODE_ULTRA    2      /* máxima puntuación en 3 minutos          */
#define LINES_GOAL    40
#define ULTRA_FRAMES  (3u * 60u * 60u)
static unsigned char game_mode   = MODE_MARATHON;
static unsigned char start_level = 0;
static unsigned int  play_frames = 0;   /* cronómetro de la partida (frames) */

/* Récords: [MARATHON] puntos, [40LINES] mejor tiempo en frames (0 =
   ninguno), [ULTRA] puntos. Se guardan en la SRAM del cartucho.     */
static unsigned long records[3];

/* "M:SS.d" (7 bytes con el terminador). Tope 9:59.9 */
static void format_time(char *buf, unsigned int frames)
{
  unsigned int  secs = frames / 60;
  unsigned char m = (unsigned char)(secs / 60);
  unsigned char s = (unsigned char)(secs % 60);
  unsigned char d = (unsigned char)((frames % 60) / 6);
  if (m > 9) { m = 9; s = 59; d = 9; }
  buf[0] = (char)('0' + m);  buf[1] = ':';
  buf[2] = (char)('0' + s / 10); buf[3] = (char)('0' + s % 10);
  buf[4] = '.'; buf[5] = (char)('0' + d); buf[6] = 0;
}

/* ---- SRAM: firma "CPL1", 12 bytes de récords y checksum ---- */
#define SAVE_SIZE 12
static const char save_magic[4] = { 'C', 'P', 'L', '1' };

static void load_records(void)
{
  unsigned char i, sum = 0, ok = 1;
  unsigned char *dst = (unsigned char *)records;
  SMS_enableSRAM();
  for (i = 0; i < 4; i++)
    if (SMS_SRAM[i] != (unsigned char)save_magic[i]) ok = 0;
  for (i = 0; i < SAVE_SIZE; i++) sum += SMS_SRAM[4 + i];
  if (ok && sum == SMS_SRAM[4 + SAVE_SIZE])
    for (i = 0; i < SAVE_SIZE; i++) dst[i] = SMS_SRAM[4 + i];
  else
    for (i = 0; i < SAVE_SIZE; i++) dst[i] = 0;   /* SRAM vacía o corrupta */
  SMS_disableSRAM();
}

static void save_records(void)
{
  unsigned char i, sum = 0;
  const unsigned char *src = (const unsigned char *)records;
  SMS_enableSRAM();
  for (i = 0; i < 4; i++) SMS_SRAM[i] = (unsigned char)save_magic[i];
  for (i = 0; i < SAVE_SIZE; i++) { SMS_SRAM[4 + i] = src[i]; sum += src[i]; }
  SMS_SRAM[4 + SAVE_SIZE] = sum;
  SMS_disableSRAM();
}

static void paint_hud_labels(void)
{
  /* Cápsulas: L_cap + 5 letras + R_cap = 7 tiles. Panel tiene 8 cols
     (12..19). Ponemos la cápsula en cols 13..19 (col 12 queda dark). */
  screen_print_capsule(13, 1,  game_mode == MODE_40LINES ? "TIME" : "SCORE");
  screen_print_capsule(13, 4,  game_mode == MODE_ULTRA   ? "TIME" : "LEVEL");
  screen_print_capsule(13, 7,  "LINES");
  /* NEXT = 4 letras -> cápsula 6 tiles. Cols 13..18 (col 12 y 19 dark). */
  screen_print_capsule(13, 10, "NEXT");
}

/* Pinta número alineado a la derecha en el panel. Ancho = width.
   Coord inicio (sx, sy) = esquina superior-izquierda de la zona. */
static void paint_number(unsigned char sx, unsigned char sy,
                         unsigned long value, unsigned char width)
{
  unsigned char i;
  unsigned char buf[8];
  if (width > 8) width = 8;
  /* genera dígitos de derecha a izquierda */
  for (i = 0; i < width; i++) {
    buf[i] = (unsigned char)(value % 10);
    value /= 10;
  }
  /* pintamos: ceros a la izquierda como espacios (panel) */
  {
    unsigned char leading = 1;
    for (i = 0; i < width; i++) {
      unsigned char digit = buf[width - 1 - i];
      unsigned char tile;
      if (leading && digit == 0 && i < (unsigned char)(width - 1))
        tile = TILE_PANEL;      /* hueco */
      else {
        leading = 0;
        tile = TILE_DIGIT(digit);
      }
      screen_set((unsigned char)(sx + i), sy, tile);
    }
  }
}

/* Cronómetro del HUD: 40 LINES cuenta hacia arriba (en vez de SCORE),
   ULTRA cuenta hacia atrás desde 3:00.0 (en vez de LEVEL). */
static void paint_hud_time(void)
{
  char buf[7];
  if (game_mode == MODE_40LINES) {
    format_time(buf, play_frames);
    screen_print(13, 2, buf);
  } else if (game_mode == MODE_ULTRA) {
    format_time(buf, play_frames >= ULTRA_FRAMES ? 0 : ULTRA_FRAMES - play_frames);
    screen_print(13, 5, buf);
  }
}

static void paint_hud_numbers(void)
{
  if (game_mode != MODE_40LINES) paint_number(13, 2, score, 6);   /* SCORE 6 dígitos */
  if (game_mode != MODE_ULTRA)   paint_number(15, 5, level, 2);   /* LEVEL 2 dígitos */
  if (game_mode == MODE_40LINES)                                  /* líneas que faltan */
    paint_number(14, 8, lines >= LINES_GOAL ? 0 : LINES_GOAL - lines, 4);
  else
    paint_number(14, 8, lines, 4);
  paint_hud_time();
}

/* Preview de la siguiente pieza en cuadro 4x4. Se sitúa en
   cols 14..17, filas 12..15 (1 fila de aire tras el label NEXT). */
#define NEXT_PREVIEW_X 14
#define NEXT_PREVIEW_Y 12

static void paint_next_preview(void)
{
  unsigned char lx, ly;
  for (ly = 0; ly < 4; ly++)
    for (lx = 0; lx < 4; lx++)
      screen_set((unsigned char)(NEXT_PREVIEW_X + lx),
                 (unsigned char)(NEXT_PREVIEW_Y + ly), TILE_PANEL);
  for (ly = 0; ly < 4; ly++)
    for (lx = 0; lx < 4; lx++)
      if (shape_has(next_type, 0, lx, ly))
        screen_set((unsigned char)(NEXT_PREVIEW_X + lx),
                   (unsigned char)(NEXT_PREVIEW_Y + ly),
                   TILE_PIECE(next_type));
}

/* ---- pieza activa sobre el tablero ---- */
static signed char prev_blocks[4][2];
static signed char prev_ghost[4][2];
static unsigned char have_prev = 0;

/* Cache de ghost: solo recalculamos landing_y cuando la pieza cambia */
static unsigned char ghost_last_type = 0xFF;
static unsigned char ghost_last_rot  = 0xFF;
static signed char   ghost_last_x    = -100;
static signed char   ghost_last_y    = -100;

static void invalidate_ghost_cache(void)
{
  ghost_last_type = 0xFF;
}

static void current_blocks(signed char out[4][2])
{
  unsigned char lx, ly, i = 0;
  for (ly = 0; ly < 4; ly++) {
    for (lx = 0; lx < 4; lx++) {
      if (shape_has(cur_type, cur_rot, lx, ly)) {
        out[i][0] = cur_x + (signed char)lx;
        out[i][1] = cur_y + (signed char)ly;
        i++;
      }
    }
  }
}

static void paint_piece_blocks(signed char bl[4][2])
{
  unsigned char i, tile = TILE_PIECE(cur_type);
  signed char sy;
  for (i = 0; i < 4; i++) {
    if (bl[i][0] < 0 || bl[i][0] >= BOARD_W) continue;
    sy = bl[i][1] - (signed char)HIDDEN_H;
    if (sy < 0 || sy >= (signed char)VISIBLE_H) continue;
    screen_set(BOARD_X0 + (unsigned char)bl[i][0],
               BOARD_Y0 + (unsigned char)sy, tile);
  }
}

static void restore_prev_blocks(void)
{
  unsigned char i;
  for (i = 0; i < 4; i++) {
    if (prev_blocks[i][0] < 0 || prev_blocks[i][0] >= BOARD_W) continue;
    if (prev_blocks[i][1] < 0 || prev_blocks[i][1] >= BOARD_H) continue;
    paint_board_cell((unsigned char)prev_blocks[i][0],
                     (unsigned char)prev_blocks[i][1]);
  }
}

static void paint_full_board(void)
{
  unsigned char x, y;
  for (y = HIDDEN_H; y < BOARD_H; y++)
    for (x = 0; x < BOARD_W; x++)
      paint_board_cell(x, y);
}

/* ================================================================
   Tiempo, gravedad, puntuación
   ================================================================ */
/* Frames por caída según nivel — tabla fiel al Tetris de Game Boy
   (21 niveles 0..20). A partir del 20 la gravedad no sube más. */
static const unsigned char fall_table[] = {
  53, 49, 45, 41, 37, 33, 28, 22, 17, 11,
  10,  9,  8,  7,  6,  6,  5,  5,  4,  4,
   3
};
#define MAX_LEVEL ((sizeof(fall_table)/sizeof(fall_table[0])) - 1)

static unsigned char fall_frames(void)
{
  return fall_table[level > MAX_LEVEL ? MAX_LEVEL : level];
}

static unsigned char fall_timer = 48;
static unsigned char need_full_board_repaint = 0;
static unsigned char need_hud_refresh = 1;

/* ---- Estado del juego para animación de line clear ---- */
#define STATE_TITLE    0
#define STATE_PLAY     1
#define STATE_FLASH    2
#define STATE_GAMEOVER 3
#define STATE_PAUSE    4
#define STATE_INTRO    5

static unsigned char game_state = STATE_TITLE;
static unsigned char start_was_pressed = 0;  /* para edge-detect del Start de GG */
static unsigned char flash_rows[4];     /* índices (en board) de las filas llenas */
static unsigned char n_flash_rows = 0;
static unsigned char flash_timer = 0;
static unsigned char gameover_timer = 0; /* frames desde que empezó el game over */
#define END_TOPOUT 0
#define END_CLEAR  1     /* 40 LINES completado */
#define END_TIMEUP 2     /* ULTRA: se acabó el tiempo */
static unsigned char end_result = END_TOPOUT;
static unsigned char end_record = 0;   /* 1 = récord nuevo */

#define FLASH_DURATION 24    /* frames totales de animación (~400ms)     */
#define FLASH_PHASE    6     /* alterna blanco/normal cada N frames      */
#define SCORE_MAX      999999UL   /* el HUD muestra 6 dígitos              */

/* Escanea board[][] y rellena `out` con los índices de filas llenas.
   Devuelve el número encontrado (0..4). No modifica board. */
static unsigned char scan_full_rows(unsigned char *out)
{
  unsigned char y, x, full, n = 0;
  for (y = 0; y < BOARD_H; y++) {
    full = 1;
    for (x = 0; x < BOARD_W; x++) {
      if (!board[y][x]) { full = 0; break; }
    }
    if (full) {
      out[n++] = y;
      if (n >= 4) break;
    }
  }
  return n;
}

static void add_score(unsigned long pts)
{
  score += pts;
  if (score > SCORE_MAX) score = SCORE_MAX;
  need_hud_refresh = 1;
}

/* Puntuación clásica (NES-style) */
static const unsigned int line_score[5] = { 0, 40, 100, 300, 1200 };

static void add_lines(unsigned char n)
{
  if (n == 0) return;
  if (n > 4) n = 4;
  lines += n;
  /* Maratón: el nivel sube cada 10 líneas, nunca por debajo del
     elegido. En 40 LINES y ULTRA el nivel es fijo. */
  if (game_mode == MODE_MARATHON) {
    unsigned char new_level = (unsigned char)(lines / 10);
    if (new_level < start_level) new_level = start_level;
    if (new_level > MAX_LEVEL) new_level = MAX_LEVEL;
    if (new_level != level) {
      level = new_level;
      sfx_play(SFX_LEVELUP);
    }
  }
  need_hud_refresh = 1;
}

/* Música rápida cuando la pila llega a las 5 filas visibles de arriba */
#define DANGER_ROW (HIDDEN_H + 5)
static void update_danger(void)
{
  unsigned char y, x;
  for (y = 0; y < DANGER_ROW; y++)
    for (x = 0; x < BOARD_W; x++)
      if (board[y][x]) { music_set_danger(1); return; }
  music_set_danger(0);
}

/* Única entrada al estado de game over (antes había 2 caminos y uno
   se perdía: ver finalize_line_clear). */
static void enter_game_over(unsigned char result)
{
  game_state = STATE_GAMEOVER;
  gameover_timer = 0;
  end_result = result;
  end_record = 0;
  if (game_mode == MODE_40LINES) {
    if (result == END_CLEAR &&
        (records[MODE_40LINES] == 0 || play_frames < records[MODE_40LINES])) {
      records[MODE_40LINES] = play_frames;
      end_record = 1;
    }
  } else if (score > records[game_mode]) {
    records[game_mode] = score;
    end_record = 1;
  }
  if (end_record) save_records();
  music_stop();
  sfx_play(result == END_TOPOUT ? SFX_GAMEOVER : SFX_LEVELUP);
}

static unsigned char soft_dropping = 0;  /* 1 si el descenso actual es por soft drop */

/* Gravedad: baja una fila si cabe. La fijación la decide el lock delay. */
static void piece_step_down(void)
{
  if (!fits(cur_type, cur_rot, cur_x, cur_y + 1)) return;
  cur_y++;
  last_rotate = 0;
  if (soft_dropping) add_score(1);
  if (cur_y > lowest_y) {
    lowest_y    = cur_y;
    lock_resets = 0;
    lock_timer  = LOCK_DELAY;
  }
}

/* T-spin, regla de las 3 esquinas: la T se fijó justo tras rotar y 3 de
   las 4 esquinas de su caja 3x3 están ocupadas (o fuera del tablero).
   Completo si las 2 esquinas "de frente" (hacia donde apunta) están
   ocupadas o si entró con el último kick SRS; si no, es mini. */
#define TSPIN_NONE 0
#define TSPIN_MINI 1
#define TSPIN_FULL 2
static unsigned char cell_blocked(signed char x, signed char y)
{
  if (x < 0 || x >= BOARD_W || y >= BOARD_H) return 1;
  if (y < 0) return 0;
  return board[y][x] != 0;
}
static unsigned char tspin_check(void)
{
  /* Esquinas en orden horario: sup-izq, sup-der, inf-der, inf-izq.
     Las de frente para la rotación r son la r y la r+1. */
  static const signed char cx[4] = { 0, 2, 2, 0 };
  static const signed char cy[4] = { 0, 0, 2, 2 };
  unsigned char filled[4], i, n = 0;
  if (cur_type != PIECE_T || !last_rotate) return TSPIN_NONE;
  for (i = 0; i < 4; i++) {
    filled[i] = cell_blocked((signed char)(cur_x + cx[i]), (signed char)(cur_y + cy[i]));
    n += filled[i];
  }
  if (n < 3) return TSPIN_NONE;
  if ((filled[cur_rot] && filled[(cur_rot + 1) & 3]) || last_kick == 4)
    return TSPIN_FULL;
  return TSPIN_MINI;
}

static unsigned char pending_tspin = TSPIN_NONE;
static unsigned char back_to_back = 0;   /* último clear fue Tetris o T-spin */
static unsigned char label_timer = 0;    /* frames que queda visible el cartel */
static const char *label_text = 0;

/* Puntos de T-spin según líneas (escala NES, x (nivel+1)) */
static const unsigned int tspin_score[3][4] = {
  {   0,   0,   0,    0 },
  {  20,  80, 200,    0 },   /* mini   */
  { 100, 300, 700, 1200 }    /* T-spin */
};

/* Puntúa un bloqueo: líneas normales, Tetris, T-spins y back-to-back
   (x1.5 si se encadenan dos clears "difíciles"). */
static void score_clear(unsigned char n, unsigned char tspin)
{
  unsigned long pts;
  unsigned char difficult = (n == 4) || (tspin && n > 0);
  if (n > 4) n = 4;
  pts = tspin ? tspin_score[tspin][n] : line_score[n];
  pts *= (unsigned long)(level + 1);
  if (difficult && back_to_back) pts += pts / 2;
  if (n > 0) back_to_back = difficult;
  if (pts) add_score(pts);

  if (tspin)       { label_text = (tspin == TSPIN_FULL) ? "T-SPIN" : "MINI"; label_timer = 90; }
  else if (n == 4) { label_text = "TETRIS"; label_timer = 90; }
  if (n) add_lines(n);
}

static void lock_current_piece(void)
{
  pending_tspin = tspin_check();
  lock_piece();
  need_full_board_repaint = 1;  /* mostrar pieza recién bloqueada  */
  have_prev = 0;
  sfx_play(SFX_LOCK);

  /* ¿hay líneas completas? -> entrar en STATE_FLASH y esperar a que
     termine la animación antes de colapsar y spawnear. */
  n_flash_rows = scan_full_rows(flash_rows);
  if (n_flash_rows > 0) {
    game_state  = STATE_FLASH;
    flash_timer = FLASH_DURATION;
    sfx_play(pending_tspin ? SFX_TSPIN : (n_flash_rows == 4 ? SFX_TETRIS : SFX_LINE));
    return;
  }

  /* Sin líneas: un T-spin sin líneas también puntúa */
  if (pending_tspin) {
    score_clear(0, pending_tspin);
    sfx_play(SFX_TSPIN);
  }
  update_danger();
  if (!spawn_piece()) enter_game_over(END_TOPOUT);
  need_hud_refresh = 1;
}

/* Pinta las filas que están en flash: alterna entre blanco y su color
   original según la fase del temporizador. */
static void paint_flash_rows(void)
{
  unsigned char white_phase = (unsigned char)((flash_timer / FLASH_PHASE) & 1);
  unsigned char i, x, y, sy, tile;
  for (i = 0; i < n_flash_rows; i++) {
    y = flash_rows[i];
    if (y < HIDDEN_H) continue;
    sy = y - HIDDEN_H;
    for (x = 0; x < BOARD_W; x++) {
      if (white_phase) {
        tile = TILE_WHITE;
      } else {
        unsigned char cell = board[y][x];
        tile = cell ? TILE_PIECE(cell - 1) : TILE_EMPTY;
      }
      screen_set(BOARD_X0 + x, BOARD_Y0 + sy, tile);
    }
  }
}

/* Al terminar el flash: elimina filas llenas del modelo, ajusta
   puntuación y spawnea nueva pieza. */
static void finalize_line_clear(void)
{
  clear_lines();    /* removerá exactamente n_flash_rows */
  score_clear(n_flash_rows, pending_tspin);
  if (game_mode == MODE_40LINES && lines >= LINES_GOAL) {
    n_flash_rows = 0;
    need_full_board_repaint = 1;
    have_prev = 0;
    need_hud_refresh = 1;
    enter_game_over(END_CLEAR);
    return;
  }
  n_flash_rows = 0;
  need_full_board_repaint = 1;
  have_prev = 0;
  update_danger();
  /* BUG corregido: antes se ponía STATE_GAMEOVER y a continuación se
     pisaba con STATE_PLAY, dejando el juego congelado con game_over=1. */
  game_state = STATE_PLAY;
  if (!spawn_piece()) enter_game_over(END_TOPOUT);
  need_hud_refresh = 1;
}

/* ================================================================
   Game Over overlay + reinicio
   ================================================================ */
/* ================================================================
   Pantallas de entrada: logo de Compila! y disclaimer. Cada una
   entra y sale con fundido de paleta; se saltan con cualquier botón.
   ================================================================ */
static void clear_screen(void)
{
  unsigned char x, y;
  for (y = 0; y < 18; y++)
    for (x = 0; x < 20; x++)
      screen_set(x, y, TILE_BG);
}

/* Texto centrado en la ventana de 20 columnas */
static void print_centered(unsigned char sy, const char *s)
{
  unsigned char len = 0;
  while (s[len]) len++;
  screen_print((unsigned char)((20 - len) / 2), sy, s);
}

/* Logo del grupo: pantalla vacía con COMPILA! en el centro. La "!"
   no se pinta aquí: cae desde arriba (animate_logo_bang). */
#define LOGO_X      6       /* "COMPILA!" centrado: (20 - 8) / 2 */
#define LOGO_ROW    8
#define LOGO_DELAY  10      /* frames de pausa antes de que caiga la ! */
static void paint_logo_screen(void)
{
  text_dark = 1;
  clear_screen();
  screen_print(LOGO_X, LOGO_ROW, "COMPILA");
}

/* La ! cae a 1 fila por frame hasta su sitio y suena el "tiruriruriiin".
   t = frames desde que terminó el fundido de entrada. */
static void animate_logo_bang(unsigned int t)
{
  unsigned char x = LOGO_X + 7, y;
  if (t < LOGO_DELAY || t - LOGO_DELAY > LOGO_ROW) return;
  y = (unsigned char)(t - LOGO_DELAY);
  if (y > 0) screen_set(x, (unsigned char)(y - 1), TILE_BG);
  screen_set(x, y, char_to_tile('!'));
  if (y == LOGO_ROW) sfx_play(SFX_LOGO);
}

static void paint_disclaimer_screen(void)
{
  text_dark = 1;
  clear_screen();
  print_centered(1,  "A FREE, NON-PROFIT");
  print_centered(2,  "TRIBUTE TO A GAME");
  print_centered(3,  "OF MY CHILDHOOD,");
  print_centered(4,  "ON THE CONSOLE OF");
  print_centered(5,  "MY CHILDHOOD.");
  print_centered(8,  "UNOFFICIAL.");
  print_centered(9,  "NOT FOR SALE.");
  print_centered(11, "NOT AFFILIATED WITH");
  print_centered(12, "OR ENDORSED BY");
  print_centered(13, "THE TETRIS COMPANY.");
  print_centered(15, "TETRIS IS A");
  print_centered(16, "TRADEMARK OF");
  print_centered(17, "TETRIS HOLDING.");
}

/* "PRESS START" del título (parpadea: on=0 lo borra) */
#define PRESS_START_ROW 17
static void paint_press_start(unsigned char on)
{
  static const char txt[] = "PRESS START";
  unsigned char i;
  for (i = 0; txt[i]; i++)
    screen_set((unsigned char)(4 + i), PRESS_START_ROW,
               (on && txt[i] != ' ') ? char_to_tile(txt[i]) : TILE_BG);
}

/* Splash screen estilo GB Tetris adaptada a GG (sin Nintendo, obviamente).
   Cielo nocturno con estrellas + luna + título + silueta del Kremlin
   hecha con piezas de Tetris (meta-homenaje) + muralla de ladrillos. */
/* ---- Menú del título: modo y nivel inicial, y récord del modo ---- */
#define MENU_ROW_MODE   13
#define MENU_ROW_LEVEL  14
#define MENU_ROW_TOP    16
static const char * const mode_names[3] = { "MARATHON", "40 LINES", "ULTRA" };
static unsigned char menu_row = 0;     /* 0 = modo, 1 = nivel */

static void clear_row(unsigned char sy)
{
  unsigned char x;
  for (x = 0; x < 20; x++) screen_set(x, sy, TILE_BG);
}

/* Fila del menú centrada; la seleccionada va entre < > */
static void paint_menu_row(unsigned char sy, const char *txt, unsigned char selected)
{
  unsigned char len = 0, x;
  while (txt[len]) len++;
  x = (unsigned char)((20 - len) / 2);
  clear_row(sy);
  screen_print(x, sy, txt);
  if (selected) {
    screen_set((unsigned char)(x - 2), sy, char_to_tile('<'));
    screen_set((unsigned char)(x + len + 1), sy, char_to_tile('>'));
  }
}

static void paint_menu(void)
{
  static char lvl[] = "LEVEL 0";
  char top[11] = "TOP ";
  text_dark = 1;
  paint_menu_row(MENU_ROW_MODE, mode_names[game_mode], menu_row == 0);
  lvl[6] = (char)('0' + start_level);
  paint_menu_row(MENU_ROW_LEVEL, lvl, menu_row == 1);
  if (game_mode == MODE_40LINES) {
    if (records[MODE_40LINES]) format_time(&top[4], (unsigned int)records[MODE_40LINES]);
    else { top[4] = '-'; top[5] = ':'; top[6] = '-'; top[7] = '-'; top[8] = '.'; top[9] = '-'; top[10] = 0; }
  } else {
    unsigned long v = records[game_mode];
    unsigned char i;
    for (i = 9; i >= 4; i--) { top[i] = (char)('0' + (unsigned char)(v % 10)); v /= 10; }
    top[10] = 0;
  }
  paint_menu_row(MENU_ROW_TOP, top, 0);
}

static void menu_input(unsigned int k)
{
  if (k & (PORT_A_KEY_UP | PORT_A_KEY_DOWN)) {
    menu_row ^= 1;
  } else if (k & (PORT_A_KEY_LEFT | PORT_A_KEY_RIGHT)) {
    unsigned char right = (k & PORT_A_KEY_RIGHT) ? 1 : 0;
    if (menu_row == 0) game_mode   = (unsigned char)((game_mode + (right ? 1 : 2)) % 3);
    else               start_level = (unsigned char)((start_level + (right ? 1 : 9)) % 10);
  } else {
    return;
  }
  sfx_play(SFX_ROTATE);
  paint_menu();
}

static void paint_title_screen(void)
{
  text_dark = 1;
  unsigned char x, y;

  /* Limpiar toda el área visible (20x18) con fondo oscuro */
  for (y = 0; y < 18; y++)
    for (x = 0; x < 20; x++)
      screen_set(x, y, TILE_BG);

  /* --- Estrellas dispersas en el cielo --- */
  screen_set(1, 1,  TILE_STAR);
  screen_set(4, 2,  TILE_STAR);
  screen_set(8, 1,  TILE_STAR);
  screen_set(14, 2, TILE_STAR);
  screen_set(18, 3, TILE_STAR);
  screen_set(2, 4,  TILE_STAR);
  screen_set(11, 4, TILE_STAR);

  /* --- Luna arriba-derecha --- */
  screen_set(16, 1, TILE_MOON);

  /* --- Logo TETRIS GG centrado --- */
  screen_set(6,  6, char_to_tile('T'));
  screen_set(7,  6, char_to_tile('E'));
  screen_set(8,  6, char_to_tile('T'));
  screen_set(9,  6, char_to_tile('R'));
  screen_set(10, 6, char_to_tile('I'));
  screen_set(11, 6, char_to_tile('S'));
  screen_set(12, 6, char_to_tile('G'));
  screen_set(13, 6, char_to_tile('G'));

  /* --- Silueta del "Kremlin" hecha con piezas Tetris (cúpulas) --- */
  /* Cúpula izquierda (morado) */
  screen_set(4,  9, TILE_PIECE(PIECE_T));
  screen_set(5,  9, TILE_PIECE(PIECE_T));
  screen_set(4, 10, TILE_PIECE(PIECE_T));
  screen_set(5, 10, TILE_PIECE(PIECE_T));
  /* Cúpula central grande (amarillo) */
  screen_set(9,  8, TILE_PIECE(PIECE_O));
  screen_set(10, 8, TILE_PIECE(PIECE_O));
  screen_set(9,  9, TILE_PIECE(PIECE_O));
  screen_set(10, 9, TILE_PIECE(PIECE_O));
  screen_set(9, 10, TILE_PIECE(PIECE_O));
  screen_set(10,10, TILE_PIECE(PIECE_O));
  /* Cúpula derecha (rojo) */
  screen_set(14, 9, TILE_PIECE(PIECE_Z));
  screen_set(15, 9, TILE_PIECE(PIECE_Z));
  screen_set(14,10, TILE_PIECE(PIECE_Z));
  screen_set(15,10, TILE_PIECE(PIECE_Z));

  /* --- Muralla de ladrillos en la base --- */
  for (x = 0; x < 20; x++) {
    screen_set(x, 11, TILE_BRICK);
    screen_set(x, 12, TILE_BRICK);
  }

  paint_menu();
  paint_press_start(1);
}

/* Rellena una fila visible del tablero (coords de pantalla) con un tile */
static void fill_board_row(unsigned char sy, unsigned char tile)
{
  unsigned char x;
  for (x = 0; x < BOARD_W; x++)
    screen_set((unsigned char)(BOARD_X0 + x), (unsigned char)(BOARD_Y0 + sy), tile);
}

static void fill_panel_row(unsigned char sy)
{
  unsigned char x;
  for (x = 13; x < 19; x++) screen_set(x, sy, TILE_PANEL);
}

/* Pausa: se oculta el tablero (si no, pausar sirve para pensar sin
   reloj) y se muestra PAUSE. */
static void paint_pause_overlay(void)
{
  unsigned char y;
  for (y = 0; y < VISIBLE_H; y++)
    fill_board_row(y, TILE_EMPTY);
  fill_board_row(8, TILE_PANEL);
  screen_print(4, 8, "PAUSE");
}

/* Texto centrado en el tablero (10 columnas, x de pantalla 1..10) */
static void board_print(unsigned char sy, const char *s)
{
  unsigned char len = 0;
  while (s[len]) len++;
  screen_print((unsigned char)(BOARD_X0 + (BOARD_W - len) / 2), sy, s);
}

static void paint_gameover_overlay(void)
{
  unsigned char y;
  char buf[7];
  for (y = 6; y <= (end_record ? 12 : 10); y++)
    fill_board_row(y, TILE_PANEL);
  if (end_result == END_CLEAR) {
    board_print(7, "CLEAR");
    format_time(buf, play_frames);
    board_print(9, buf);
  } else if (end_result == END_TIMEUP) {
    board_print(7, "TIME");
    board_print(9, "UP");
  } else {
    board_print(7, "GAME");
    board_print(9, "OVER");
  }
  if (end_record) board_print(11, "RECORD!");
}

static void reset_game(void);  /* forward decl */

/* ================================================================
   Input
   ================================================================ */
#define DAS_FRAMES 10
#define ARR_FRAMES 3
#define SOFT_DROP_FRAMES 2   /* soft drop: 1 celda cada 2 frames (30/s) */

static unsigned char das_counter_l = 0;
static unsigned char das_counter_r = 0;
/* Botones de acción única (rotar, hard drop): disparan al pulsar y no
   se repiten hasta soltar. Se usa el estado "held" + flag en vez del
   flanco de SMS_getKeysPressed (ver nota DAS en memoria del proyecto). */
static unsigned char held_cw   = 0;
static unsigned char held_ccw  = 0;
static unsigned char held_drop = 0;

/* Mover o rotar con éxito estando apoyada reinicia el lock delay */
static void lock_reset(void)
{
  if (lock_resets < LOCK_MAX_RESETS) {
    lock_resets++;
    lock_timer = LOCK_DELAY;
  }
}

static void try_move(signed char dx)
{
  if (fits(cur_type, cur_rot, cur_x + dx, cur_y)) {
    cur_x += dx;
    last_rotate = 0;
    lock_reset();
  }
}

/* Calcula la Y de aterrizaje. Extrae los 4 bloques una vez y luego
   escanea filas con checks directos al board (evita 16 × shape_has
   por cada fila candidata → x10 más rápido en Z80). */
static signed char landing_y(void)
{
  signed char y = cur_y;
  signed char bx_off[4], by_off[4];
  unsigned char lx, ly, n = 0, i;

  for (ly = 0; ly < 4; ly++)
    for (lx = 0; lx < 4; lx++)
      if (shape_has(cur_type, cur_rot, lx, ly)) {
        bx_off[n] = (signed char)lx;
        by_off[n] = (signed char)ly;
        n++;
      }

  for (;;) {
    signed char ny = (signed char)(y + 1);
    unsigned char ok = 1;
    for (i = 0; i < n; i++) {
      signed char bx = (signed char)(cur_x + bx_off[i]);
      signed char by = (signed char)(ny + by_off[i]);
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) { ok = 0; break; }
      if (by >= 0 && board[by][bx]) { ok = 0; break; }
    }
    if (!ok) break;
    y++;
  }
  return y;
}

/* Hard drop: cae de golpe hasta el fondo, bloquea, suma 2 pts por celda */
static void hard_drop(void)
{
  signed char ly = landing_y();
  signed char dist = (signed char)(ly - cur_y);
  if (dist > 0) { add_score((unsigned long)dist * 2); last_rotate = 0; }
  cur_y = ly;
  lock_now = 1;
}

/* Super Rotation System: tablas de kicks oficiales (dx, dy) con dy
   hacia ABAJO positivo (la tabla de la guideline usa y hacia arriba).
   Orden de transiciones: 0>R, R>0, R>2, 2>R, 2>L, L>2, L>0, 0>L.
   Los kicks verticales son los que permiten meter la pieza en un hueco
   girando (T-spins, "twists" de S/Z/J/L). */
static const signed char kick_jlstz[8][10] = {
  { 0,0, -1,0, -1,-1, 0, 2, -1, 2 },
  { 0,0,  1,0,  1, 1, 0,-2,  1,-2 },
  { 0,0,  1,0,  1, 1, 0,-2,  1,-2 },
  { 0,0, -1,0, -1,-1, 0, 2, -1, 2 },
  { 0,0,  1,0,  1,-1, 0, 2,  1, 2 },
  { 0,0, -1,0, -1, 1, 0,-2, -1,-2 },
  { 0,0, -1,0, -1, 1, 0,-2, -1,-2 },
  { 0,0,  1,0,  1,-1, 0, 2,  1, 2 }
};
static const signed char kick_i[8][10] = {
  { 0,0, -2,0,  1,0, -2, 1,  1,-2 },
  { 0,0,  2,0, -1,0,  2,-1, -1, 2 },
  { 0,0, -1,0,  2,0, -1,-2,  2, 1 },
  { 0,0,  1,0, -2,0,  1, 2, -2,-1 },
  { 0,0,  2,0, -1,0,  2,-1, -1, 2 },
  { 0,0, -2,0,  1,0, -2, 1,  1,-2 },
  { 0,0,  1,0, -2,0,  1, 2, -2,-1 },
  { 0,0, -1,0,  2,0, -1,-2,  2, 1 }
};

static void try_rotate(unsigned char cw)
{
  unsigned char nr  = (unsigned char)((cur_rot + (cw ? 1 : 3)) & 3);
  unsigned char idx = cw ? (unsigned char)(cur_rot << 1)
                         : (unsigned char)(((cur_rot << 1) + 7) & 7);
  const signed char *k = (cur_type == PIECE_I) ? kick_i[idx] : kick_jlstz[idx];
  unsigned char i;
  if (cur_type == PIECE_O) return;   /* el O no rota */
  for (i = 0; i < 5; i++, k += 2) {
    signed char nx = (signed char)(cur_x + k[0]);
    signed char ny = (signed char)(cur_y + k[1]);
    if (fits(cur_type, nr, nx, ny)) {
      cur_rot = nr; cur_x = nx; cur_y = ny;
      last_rotate = 1;
      last_kick   = i;
      lock_reset();
      sfx_play(SFX_ROTATE);
      return;
    }
  }
}

static void handle_input(void)
{
  unsigned int h = SMS_getKeysStatus();

  if (h & PORT_A_KEY_LEFT) {
    if (das_counter_l == 0) {
      try_move(-1); das_counter_l = DAS_FRAMES;
    } else if (--das_counter_l == 0) {
      try_move(-1); das_counter_l = ARR_FRAMES;
    }
  } else das_counter_l = 0;

  if (h & PORT_A_KEY_RIGHT) {
    if (das_counter_r == 0) {
      try_move(1); das_counter_r = DAS_FRAMES;
    } else if (--das_counter_r == 0) {
      try_move(1); das_counter_r = ARR_FRAMES;
    }
  } else das_counter_r = 0;

  if (h & PORT_A_KEY_1) {
    if (!held_cw) { try_rotate(1); held_cw = 1; }
  } else held_cw = 0;

  if (h & PORT_A_KEY_2) {
    if (!held_ccw) { try_rotate(0); held_ccw = 1; }
  } else held_ccw = 0;

  /* Soft drop. Tras aparecer una pieza nueva hay que soltar ABAJO antes
     de que vuelva a funcionar: si no, la pieza siguiente cae disparada. */
  soft_dropping = 0;
  if (h & PORT_A_KEY_DOWN) {
    if (!down_block) {
      if (fall_timer > SOFT_DROP_FRAMES) fall_timer = SOFT_DROP_FRAMES;
      soft_dropping = 1;
    }
  } else {
    down_block = 0;
  }

  /* Hard drop: ARRIBA tira la pieza al fondo. Una vez por pulsación;
     antes, mantenerlo encadenaba caídas de las piezas siguientes. */
  if (h & PORT_A_KEY_UP) {
    if (!held_drop) {
      held_drop = 1;
      hard_drop();      /* pone lock_now: se fija este mismo frame */
    }
  } else held_drop = 0;
}

/* ---- Música: Kalinka en el título, Korobeiniki en partida, ambas con
   el motor propio de music.c (sin PSGlib). */
static void start_title_music(void)
{
  music_play_title();
}

/* ---- Intro: logo Compila! -> disclaimer -> título, con fundidos ----
   Cada pantalla: INTRO_BLACK frames en negro (el shadow buffer se vuelca
   con presupuesto sin que se vea), fundido de entrada, espera (se salta
   con cualquier botón) y fundido de salida. */
#define INTRO_BLACK  8
#define INTRO_FADE   32                       /* 16 niveles x 2 frames */
#define INTRO_IN     (INTRO_BLACK + INTRO_FADE)
#define INTRO_LOGO       0                    /* COMPILA! solo, centrado */
#define INTRO_DISCLAIMER 1
#define INTRO_TITLE      2                    /* "pantalla" 2 = título    */
static unsigned char intro_screen = INTRO_LOGO;
static unsigned int  intro_timer  = 0;
static const unsigned int intro_hold[2] = { 180, 480 };   /* 3 s y 8 s */

/* k = 0 negro .. 15 paleta normal: resta (15-k) a cada componente */
static void fade_level(unsigned char k)
{
  static unsigned int pal[16];
  unsigned char i, s = (unsigned char)(15 - k);
  for (i = 0; i < 16; i++) {
    unsigned int col = bg_palette[i];
    unsigned char r = (unsigned char)(col & 0x0F);
    unsigned char g = (unsigned char)((col >> 4) & 0x0F);
    unsigned char b = (unsigned char)((col >> 8) & 0x0F);
    r = r > s ? (unsigned char)(r - s) : 0;
    g = g > s ? (unsigned char)(g - s) : 0;
    b = b > s ? (unsigned char)(b - s) : 0;
    pal[i] = RGB(r, g, b);
  }
  GG_loadBGPalette(pal);
}

static unsigned char fade_in_level(unsigned int t)
{
  if (t <= INTRO_BLACK) return 0;
  t = (t - INTRO_BLACK) / 2;
  return (unsigned char)(t > 15 ? 15 : t);
}

static void update_intro(void)
{
  unsigned int k = SMS_getKeysPressed();
  unsigned char start_now = GG_START_PRESSED();
  unsigned char skip = (k & (PORT_A_KEY_1 | PORT_A_KEY_2)) || (start_now && !start_was_pressed);
  start_was_pressed = start_now;

  intro_timer++;
  if (intro_timer <= INTRO_IN) {
    fade_level(fade_in_level(intro_timer));
    if (intro_timer == INTRO_IN && intro_screen == INTRO_TITLE)
      game_state = STATE_TITLE;
    return;
  }
  {
    unsigned int hold = intro_hold[intro_screen];
    unsigned int t;
    if (intro_timer <= INTRO_IN + hold) {
      if (intro_screen == INTRO_LOGO)    animate_logo_bang(intro_timer - INTRO_IN);
      if (skip) intro_timer = INTRO_IN + hold;   /* saltar: directo al fundido */
      return;
    }
    t = intro_timer - INTRO_IN - hold;           /* 1..INTRO_FADE */
    if (t < INTRO_FADE) {
      fade_level((unsigned char)(15 - t / 2));
      return;
    }
  }
  /* Fundido de salida terminado (pantalla en negro): siguiente */
  fade_level(0);
  intro_timer = 0;
  if (intro_screen == INTRO_LOGO) {
    intro_screen = INTRO_DISCLAIMER;
    paint_disclaimer_screen();
  } else {
    intro_screen = INTRO_TITLE;
    paint_title_screen();
    start_title_music();
  }
}

static void start_game_music(void)
{
  music_play_game();
  music_set_danger(0);
}

static void reset_game(void)
{
  text_dark = 0;   /* HUD y overlays van sobre el panel */
  unsigned char x, y;
  for (y = 0; y < BOARD_H; y++)
    for (x = 0; x < BOARD_W; x++)
      board[y][x] = 0;
  score = 0; level = start_level; lines = 0;
  play_frames = 0;
  game_state = STATE_PLAY;
  n_flash_rows = 0;
  have_prev = 0;
  invalidate_ghost_cache();
  soft_dropping = 0;
  das_counter_l = 0; das_counter_r = 0;
  /* El botón con el que se empezó la partida no debe rotar ni soltar */
  held_cw = 1; held_ccw = 1; held_drop = 1;
  rng_seed();   /* re-sembrar en cada reinicio */
  bag_pos = 7;  /* bolsa nueva */
  back_to_back = 0; label_timer = 0; lock_now = 0;
  next_type = rand7();
  spawn_piece();
  fall_timer = fall_frames();
  /* Pintar layout del juego (marcos + panel + cápsulas) sobre lo
     que hubiera (splash screen, game over screen, etc.) */
  paint_static_layout();
  paint_hud_labels();
  need_full_board_repaint = 1;
  need_hud_refresh = 1;
  start_game_music();
}

/* ================================================================
   Handler del VBlank IRQ: corre a 60Hz estable aunque el main loop
   se atasque. Ejecuta la música (motor propio, music.c) y marca un flag para que el main loop vuelque el tile
   animado. NUNCA escribe al VDP desde aquí: eso causaba race con
   nt_flush.
   ================================================================ */
#define ANIM_SPEED 6    /* 60/6 = 10 Hz visual, más tolerante a frame drops */
static void vblank_isr(void)
{
  frame_count++;
  music_frame();
  if (++anim_counter >= ANIM_SPEED) {
    anim_counter = 0;
    anim_frame_idx = (unsigned char)((anim_frame_idx + 1) & 7);
    anim_pending = 1;
  }
}

/* Cronómetro de 40 LINES / ULTRA: avanza 1 por frame de juego y el HUD
   se repinta cada décima (6 frames). */
static void tick_timer(void)
{
  if (game_mode == MODE_MARATHON) return;
  if (play_frames < 0xFFFF) play_frames++;
  if ((unsigned char)(play_frames % 6) == 0) paint_hud_time();
}

#define GAMEOVER_CURTAIN (2 * VISIBLE_H)     /* una fila cada 2 frames */
#define GAMEOVER_WAIT    (GAMEOVER_CURTAIN + 45)

/* ================================================================
   Main
   ================================================================ */
void main(void)
{
  SMS_VRAMmemsetW(0, 0x0000, 16384);

  /* Cargar tileset estático (tile 0 solo) */
  SMS_loadTiles(static_tiles, 0, sizeof(static_tiles));

  /* Tile 1 = ladrillo, 2 = celda vacía, 3 = panel, 4 = panel accent */
  SMS_loadTiles(brick_tile, TILE_BRICK, sizeof(brick_tile));
  /* Generar tiles animados de fondo y cargar el frame 0 */
  gen_anim_tiles();
  SMS_loadTiles(&anim_tiles[0], TILE_EMPTY, 32);
  {
    static const unsigned char solid_panel[32]   = { SOLID_COLOR(3) };
    static const unsigned char solid_paccent[32] = { SOLID_COLOR(4) };
    static const unsigned char solid_white[32]   = { SOLID_COLOR(13) };
    /* Ghost tile: outline color 13 (blanco), interior color 2 (celda vacía).
       outline 13 (1101): p0=1, p1=0, p2=1, p3=1
       interior 2 (0010): p0=0, p1=1, p2=0, p3=0
       Para bit O=1 (outline): p0=O, p1=~O, p2=O, p3=O */
    #define GHOST_ROW(b) (unsigned char)(b), (unsigned char)~(b), (unsigned char)(b), (unsigned char)(b)
    static const unsigned char ghost_tile[32] = {
      GHOST_ROW(0xFF),                           /* fila 0: todo outline */
      GHOST_ROW(0x81), GHOST_ROW(0x81),          /* filas 1..6: borde */
      GHOST_ROW(0x81), GHOST_ROW(0x81),
      GHOST_ROW(0x81), GHOST_ROW(0x81),
      GHOST_ROW(0xFF)                            /* fila 7: todo outline */
    };
    SMS_loadTiles(solid_panel,   TILE_PANEL,   sizeof(solid_panel));
    SMS_loadTiles(solid_paccent, TILE_PACCENT, sizeof(solid_paccent));
    SMS_loadTiles(solid_white,   TILE_WHITE,   sizeof(solid_white));
    SMS_loadTiles(ghost_tile,    TILE_GHOST,   sizeof(ghost_tile));

    /* Tiles decorativos para la splash screen: cream sobre dark bg (color 0).
       color 4 (0100): p0=0, p1=0, p2=1, p3=0
       color 0 (0000): todos 0
       Para bit B=1 (cream): plane0=0, plane1=0, plane2=B, plane3=0 */
    #define DECO_ROW(m) 0x00, 0x00, (unsigned char)(m), 0x00
    static const unsigned char star_tile[32] = {
      DECO_ROW(0x00),
      DECO_ROW(0x00),
      DECO_ROW(0x08),    /*     #     */
      DECO_ROW(0x1C),    /*    ###    */
      DECO_ROW(0x08),    /*     #     */
      DECO_ROW(0x00),
      DECO_ROW(0x00),
      DECO_ROW(0x00)
    };
    static const unsigned char moon_tile[32] = {
      DECO_ROW(0x3C),    /*   ####    */
      DECO_ROW(0x7E),    /*  ######   */
      DECO_ROW(0xFF),    /* ######### */
      DECO_ROW(0xFF),
      DECO_ROW(0xFF),
      DECO_ROW(0xFF),
      DECO_ROW(0x7E),
      DECO_ROW(0x3C)
    };
    SMS_loadTiles(star_tile, TILE_STAR, sizeof(star_tile));
    SMS_loadTiles(moon_tile, TILE_MOON, sizeof(moon_tile));
  }

  /* Tiles 5..11: piezas 3D generadas en RAM */
  gen_all_piece_tiles();
  SMS_loadTiles(piece_tiles, 5, sizeof(piece_tiles));

  /* Fuente (12..48), letras invertidas (49..74) y tapas de cápsula */
  SMS_loadTiles(font_normal,   TILE_DIGIT(0),  sizeof(font_normal));
  SMS_loadTiles(font_inverted, TILE_INV_BASE,  sizeof(font_inverted));
  SMS_loadTiles(cap_tiles,     TILE_LCAP,      2 * 32);
  SMS_loadTiles(&cap_tiles[2 * 32], TILE_LIP_L, 3 * 32);
  SMS_loadTiles(font_punct,    TILE_PUNCT,     sizeof(font_punct));
  SMS_loadTiles(font_dark,     TILE_DARK,      sizeof(font_dark));

  fade_level(0);    /* arranca en negro: la intro hace el fundido */

  SMS_initSprites();
  SMS_copySpritestoSAT();

  load_records();
  nt_init();
  paint_logo_screen();
  nt_flush_all();

  /* Intro (logo + disclaimer) y luego el título. Música del título:
     Kalinka; en partida, Korobeiniki. Ambas corren en la ISR de VBlank
     a 60Hz estables (music.c). */
  game_state = STATE_INTRO;
  SMS_setFrameInterruptHandler(vblank_isr);   /* música + anim fondo a 60Hz */

  SMS_displayOn();

  for (;;) {
    signed char new_blocks[4][2];
    unsigned char i;

    SMS_waitForVBlank();
    /* La música corre en el ISR. La anim de fondo marca flag desde el
       ISR y volcamos el tile aquí, en el main loop, para evitar el
       race con nt_flush sobre el VDP. */
    if (anim_pending) {
      SMS_loadTiles(&anim_tiles[anim_frame_idx * 32], TILE_EMPTY, 32);
      anim_pending = 0;
    }
    nt_flush();

    if (game_state == STATE_INTRO) {
      update_intro();
    } else if (game_state == STATE_TITLE) {
      paint_press_start(!((unsigned char)frame_count & 0x20));
      /* Start (o Button 1/2) arranca partida */
      {
        unsigned int k = SMS_getKeysPressed();
        unsigned char start_now = GG_START_PRESSED();
        unsigned char start_edge = (start_now && !start_was_pressed);
        start_was_pressed = start_now;
        if ((k & (PORT_A_KEY_1 | PORT_A_KEY_2)) || start_edge) {
          reset_game();
        } else {
          menu_input(k);
        }
      }
    } else if (game_state == STATE_PAUSE) {
      /* Start de GG reanuda (flanco: solo al pulsar, no al mantener) */
      if (GG_START_PRESSED() && !start_was_pressed) {
        game_state = STATE_PLAY;
        need_full_board_repaint = 1;
        invalidate_ghost_cache();
        music_set_paused(0);
      }
      start_was_pressed = GG_START_PRESSED();
    } else if (game_state == STATE_PLAY) {
      /* Start de GG pausa (flanco) */
      if (GG_START_PRESSED() && !start_was_pressed) {
        game_state = STATE_PAUSE;
        paint_pause_overlay();
        music_set_paused(1);
        sfx_play(SFX_PAUSE);
      }
      start_was_pressed = GG_START_PRESSED();
      if (game_state == STATE_PAUSE) continue;

      handle_input();
      tick_timer();
      if (game_mode == MODE_ULTRA && play_frames >= ULTRA_FRAMES) {
        enter_game_over(END_TIMEUP);
        continue;
      }
      if (lock_now) {
        lock_now = 0;
        lock_current_piece();
      } else if (!fits(cur_type, cur_rot, cur_x, cur_y + 1)) {
        /* Apoyada: corre el lock delay (la gravedad no hace nada) */
        if (lock_timer <= 1) lock_current_piece();
        else lock_timer--;
      } else if (--fall_timer == 0) {
        fall_timer = fall_frames();
        piece_step_down();
      }

      /* Cache de pieza: solo re-renderizar si realmente cambió */
      {
        static unsigned char piece_last_type = 0xFF;
        static unsigned char piece_last_rot  = 0xFF;
        static signed char   piece_last_x    = -100;
        static signed char   piece_last_y    = -100;
        unsigned char piece_moved = (cur_type != piece_last_type ||
                                     cur_rot  != piece_last_rot  ||
                                     cur_x    != piece_last_x    ||
                                     cur_y    != piece_last_y);

        if (need_full_board_repaint) {
          paint_full_board();
          need_full_board_repaint = 0;
          invalidate_ghost_cache();
          piece_last_type = 0xFF;
          piece_moved = 1;
        } else if (piece_moved && have_prev) {
          restore_prev_blocks();
        }

        piece_last_type = cur_type;
        piece_last_rot  = cur_rot;
        piece_last_x    = cur_x;
        piece_last_y    = cur_y;

        if (!piece_moved) goto skip_piece_render;
      }

      /* Ghost piece: solo depende de tipo/rotación/columna (NO de cur_y).
         Al caer por gravedad no hace falta recalcular landing_y. Esto
         ahorra landing_y() en todos los frames de soft drop (60/seg). */
      {
        unsigned char gc = (cur_type != ghost_last_type ||
                            cur_rot  != ghost_last_rot  ||
                            cur_x    != ghost_last_x);
        if (gc) {
          signed char gy;
          unsigned char lx, ly, gi;

          /* Borrar ghost anterior */
          if (ghost_last_type != 0xFF) {
            for (gi = 0; gi < 4; gi++) {
              if (prev_ghost[gi][0] >= 0 && prev_ghost[gi][0] < BOARD_W &&
                  prev_ghost[gi][1] >= HIDDEN_H && prev_ghost[gi][1] < BOARD_H)
                paint_board_cell((unsigned char)prev_ghost[gi][0],
                                 (unsigned char)prev_ghost[gi][1]);
            }
          }

          /* Calcular landing Y */
          gy = landing_y();

          /* Pintar nuevo ghost + guardar posiciones */
          gi = 0;
          for (ly = 0; ly < 4; ly++) {
            for (lx = 0; lx < 4; lx++) {
              if (shape_has(cur_type, cur_rot, lx, ly)) {
                signed char gx = cur_x + (signed char)lx;
                signed char gby = gy + (signed char)ly;
                prev_ghost[gi][0] = gx;
                prev_ghost[gi][1] = gby;
                gi++;
                /* Pintar si visible y celda vacía */
                if (gx >= 0 && gx < BOARD_W && gby >= HIDDEN_H && gby < BOARD_H) {
                  unsigned char sy = (unsigned char)(gby - HIDDEN_H);
                  if (!board[gby][gx])
                    screen_set(BOARD_X0 + (unsigned char)gx,
                               BOARD_Y0 + sy, TILE_GHOST);
                }
              }
            }
          }
          ghost_last_type = cur_type;
          ghost_last_rot  = cur_rot;
          ghost_last_x    = cur_x;
        }
      }
      current_blocks(new_blocks);
      paint_piece_blocks(new_blocks);
      for (i = 0; i < 4; i++) {
        prev_blocks[i][0] = new_blocks[i][0];
        prev_blocks[i][1] = new_blocks[i][1];
      }
      have_prev = 1;
    skip_piece_render: ;
    } else if (game_state == STATE_FLASH) {
      if (need_full_board_repaint) {
        paint_full_board();
        need_full_board_repaint = 0;
      }
      paint_flash_rows();
      tick_timer();
      if (--flash_timer == 0) {
        finalize_line_clear();
      }
    } else if (game_state == STATE_GAMEOVER) {
      if (need_full_board_repaint) {
        paint_full_board();
        need_full_board_repaint = 0;
      }
      /* Telón de ladrillos de abajo arriba y luego el cartel */
      if (gameover_timer < 255) gameover_timer++;
      if (gameover_timer <= GAMEOVER_CURTAIN) {
        if (!(gameover_timer & 1))
          fill_board_row((unsigned char)(VISIBLE_H - gameover_timer / 2), TILE_BRICK);
      } else {
        paint_gameover_overlay();
      }

      /* Cualquier botón (A/B/Start) vuelve al título, pero no antes de
         que acabe el telón: si no, machacar el botón de rotar mientras
         mueres se saltaba la pantalla de game over sin verla. */
      {
        unsigned int k = SMS_getKeysPressed();
        unsigned char start_now = GG_START_PRESSED();
        unsigned char start_edge = (start_now && !start_was_pressed);
        start_was_pressed = start_now;
        if (gameover_timer >= GAMEOVER_WAIT &&
            ((k & (PORT_A_KEY_1 | PORT_A_KEY_2)) || start_edge)) {
          paint_title_screen();
          game_state = STATE_TITLE;
          start_title_music();
        }
      }
    }

    /* HUD solo en estados de juego (no en TITLE) */
    if (label_timer && game_state != STATE_TITLE && game_state != STATE_INTRO) {
      if (label_timer == 90) {
        fill_panel_row(17);
        screen_print(13, 17, label_text);
      }
      if (--label_timer == 0) fill_panel_row(17);
    }
    if (need_hud_refresh && game_state != STATE_TITLE && game_state != STATE_INTRO) {
      paint_hud_numbers();
      paint_next_preview();
      need_hud_refresh = 0;
    }

  }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(0, 4, "antxiko", "Tetris GG",
                                "Tetris for Sega Game Gear - iter 3 HUD+bricks");
