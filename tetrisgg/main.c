#include "SMSlib.h"
#include "PSGlib.h"

/* ================================================================
   PSG directo (puerto $7F) - reproductor de música monofónico
   PSG del SMS/GG: 3 canales tono + 1 ruido.
   - Latch byte: 1 ccr dddd  (c=canal 0..3, r=1 para vol, 0 para tono lo)
   - Data byte:  0 dddddd    (6 bits altos del tono)
   Tono: count = 3579545 / (32 * freq_hz) (aproximado)
   Volumen: 0=máx, 15=silencio (atenuación).
   ================================================================ */
__sfr __at 0x7F PSG_port;
__sfr __at 0x00 GG_port00;    /* GG: bit 7 = Start button (0=pressed) */
#define GG_START_PRESSED()  (!(GG_port00 & 0x80))

static void psg_set_tone(unsigned char chan, unsigned int count)
{
  PSG_port = (unsigned char)(0x80 | (chan << 5) | (count & 0x0F));
  PSG_port = (unsigned char)((count >> 4) & 0x3F);
}
static void psg_set_vol(unsigned char chan, unsigned char atten)
{
  PSG_port = (unsigned char)(0x80 | (chan << 5) | 0x10 | (atten & 0x0F));
}
static void psg_silence_all(void)
{
  psg_set_vol(0, 15); psg_set_vol(1, 15);
  psg_set_vol(2, 15); psg_set_vol(3, 15);
}

/* Índices de nota: A3 a A5 (25 semitonos) */
#define N_A3 0
#define N_AS3 1
#define N_B3 2
#define N_C4 3
#define N_CS4 4
#define N_D4 5
#define N_DS4 6
#define N_E4 7
#define N_F4 8
#define N_FS4 9
#define N_G4 10
#define N_GS4 11
#define N_A4 12
#define N_AS4 13
#define N_B4 14
#define N_C5 15
#define N_CS5 16
#define N_D5 17
#define N_DS5 18
#define N_E5 19
#define N_F5 20
#define N_FS5 21
#define N_G5 22
#define N_GS5 23
#define N_A5 24

#define N_REST 0xFD
#define N_LOOP 0xFE

static const unsigned int note_counts[25] = {
  508, 480, 453,   /* A3  A#3 B3  */
  427, 404, 381, 360, 339, 320, 302, 285, 269,  /* C4..G#4 */
  254, 240, 226,   /* A4  A#4 B4  */
  214, 202, 190, 180, 170, 160, 151, 143, 135,  /* C5..G#5 */
  127              /* A5 */
};

/* Korobéiniki (Tema A de Tetris NES) - línea de melodía principal,
   aproximadamente a 150 BPM. Pares (nota, duración_en_corcheas). */
static const unsigned char korobeiniki[] = {
  /* Primera mitad */
  N_E5, 2, N_B4, 1, N_C5, 1, N_D5, 2, N_C5, 1, N_B4, 1,
  N_A4, 2, N_A4, 1, N_C5, 1, N_E5, 2, N_D5, 1, N_C5, 1,
  N_B4, 3, N_C5, 1, N_D5, 2, N_E5, 2,
  N_C5, 2, N_A4, 2, N_A4, 4,
  /* Segunda mitad (sube octava) */
  N_D5, 3, N_F5, 1, N_A5, 2, N_G5, 1, N_F5, 1,
  N_E5, 3, N_C5, 1, N_E5, 2, N_D5, 1, N_C5, 1,
  N_B4, 2, N_B4, 1, N_C5, 1, N_D5, 2, N_E5, 2,
  N_C5, 2, N_A4, 2, N_A4, 4,
  N_LOOP
};

/* Estado del reproductor */
static const unsigned char *song_ptr   = 0;
static const unsigned char *song_start = 0;
static unsigned int music_timer = 0;     /* frames restantes para la nota actual */
#define EIGHTH_FRAMES 6    /* 1 corchea = 6 frames NTSC ~ 100ms (~300 BPM)  */
#define MUSIC_ATTEN   2    /* volumen de la melodía (0=máx, 15=mudo)        */
#define BASS_ATTEN    5    /* volumen del bajo (más suave que melodía)      */

static void music_start(const unsigned char *song)
{
  song_start  = song;
  song_ptr    = song;
  music_timer = 0;
  psg_silence_all();
}
static void music_stop(void)
{
  song_ptr = 0;
  psg_silence_all();
}

/* Canal 0 = melodía, canal 1 = bajo (1 octava abajo, staccato).
   Llamar 1 vez por frame. */
static unsigned int music_note_len = 0;  /* duración total de la nota actual */

static void music_tick(void)
{
  if (!song_ptr) return;
  if (music_timer == 0) {
    unsigned char note, dur;
    for (;;) {
      note = *song_ptr++;
      if (note == N_LOOP) { song_ptr = song_start; continue; }
      break;
    }
    dur = *song_ptr++;
    music_note_len = (unsigned int)dur * EIGHTH_FRAMES;
    music_timer    = music_note_len;
    if (note == N_REST) {
      psg_set_vol(0, 15);
      psg_set_vol(1, 15);
    } else {
      psg_set_tone(0, note_counts[note]);
      psg_set_vol(0, MUSIC_ATTEN);
      if (note >= 12) {
        psg_set_tone(1, note_counts[note - 12]);
        psg_set_vol(1, BASS_ATTEN);
      } else {
        psg_set_vol(1, 15);
      }
    }
  } else {
    music_timer--;
    /* Bajo staccato: silenciar a mitad de la nota */
    if (music_timer == music_note_len / 2) psg_set_vol(1, 15);
    /* Silenciar melodía al final para separar notas consecutivas */
    if (music_timer == 1) psg_set_vol(0, 15);
  }
}

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
  RGBHTML(0x000000)   /* 15 negro (bordes)             */
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
   Hace música (PSGFrame) + animación de fondo (swap del tile TILE_EMPTY).
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

/* Tile vacío legacy (grid sutil) - lo mantenemos por si queremos volver */
#define EMPTY_ROW(mask) 0x00, (unsigned char)~(mask), 0x00, 0x00
static const unsigned char empty_board_tile[32] = {
  EMPTY_ROW(0xFF),                       /* fila 0: completamente color 2 */
  EMPTY_ROW(0xFF),
  EMPTY_ROW(0xFF),
  EMPTY_ROW(0xEF),                       /* fila 3: punto oscuro en col 4 */
  EMPTY_ROW(0xFF),
  EMPTY_ROW(0xFF),
  EMPTY_ROW(0xFF),
  EMPTY_ROW(0xFF)
};

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

/* ---------------- Fuente 5x7 en 8x8 (texto color 4 sobre panel 3)
   Macro TXT_ROW(bits): bits=1 por pixel de texto, 0 por bg.
   color 4 (0100): p0=0, p1=0, p2=1, p3=0
   color 3 (0011): p0=1, p1=1, p2=0, p3=0
   Para pixel T=1(texto): planes 0,0,1,0
       pixel T=0(bg):     planes 1,1,0,0
   => p0 = ~T, p1 = ~T, p2 = T, p3 = 0                           */
#define TXT_ROW(b) ((unsigned char)~(b)), ((unsigned char)~(b)), (unsigned char)(b), 0x00
#define TXT_TILE(r0,r1,r2,r3,r4,r5,r6,r7) \
  TXT_ROW(r0), TXT_ROW(r1), TXT_ROW(r2), TXT_ROW(r3), \
  TXT_ROW(r4), TXT_ROW(r5), TXT_ROW(r6), TXT_ROW(r7)

/* Fuente invertida: texto OSCURO (color 3 = panel dark) sobre fondo
   CREMA (color 4). Para dibujar texto DENTRO de las cápsulas del HUD.
   Para bit T=1(texto oscuro): planes 1,1,0,0
   Para bit T=0(bg cream):    planes 0,0,1,0
   => p0 = T, p1 = T, p2 = ~T, p3 = 0 */
#define INV_ROW(b) (unsigned char)(b), (unsigned char)(b), ((unsigned char)~(b)), 0x00
#define INV_TILE(r0,r1,r2,r3,r4,r5,r6,r7) \
  INV_ROW(r0), INV_ROW(r1), INV_ROW(r2), INV_ROW(r3), \
  INV_ROW(r4), INV_ROW(r5), INV_ROW(r6), INV_ROW(r7)

static const unsigned char font_tiles[] = {
  /* Dígitos 0..9 (se cargan en tiles 12..21) */
  /* 0 */ TXT_TILE(0x00,0x70,0x88,0x98,0xA8,0xC8,0x88,0x70),
  /* 1 */ TXT_TILE(0x00,0x20,0x60,0x20,0x20,0x20,0x20,0x70),
  /* 2 */ TXT_TILE(0x00,0x70,0x88,0x08,0x10,0x20,0x40,0xF8),
  /* 3 */ TXT_TILE(0x00,0x70,0x88,0x08,0x30,0x08,0x88,0x70),
  /* 4 */ TXT_TILE(0x00,0x10,0x30,0x50,0x90,0xF8,0x10,0x10),
  /* 5 */ TXT_TILE(0x00,0xF8,0x80,0xF0,0x08,0x08,0x88,0x70),
  /* 6 */ TXT_TILE(0x00,0x30,0x40,0x80,0xF0,0x88,0x88,0x70),
  /* 7 */ TXT_TILE(0x00,0xF8,0x08,0x10,0x20,0x40,0x80,0x80),
  /* 8 */ TXT_TILE(0x00,0x70,0x88,0x88,0x70,0x88,0x88,0x70),
  /* 9 */ TXT_TILE(0x00,0x70,0x88,0x88,0x78,0x08,0x10,0x60),

  /* Letras: S C O R E L V I N X T (tiles 22..32) */
  /* S */ TXT_TILE(0x00,0x78,0x80,0x80,0x70,0x08,0x08,0xF0),
  /* C */ TXT_TILE(0x00,0x78,0x80,0x80,0x80,0x80,0x80,0x78),
  /* O */ TXT_TILE(0x00,0x70,0x88,0x88,0x88,0x88,0x88,0x70),
  /* R */ TXT_TILE(0x00,0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88),
  /* E */ TXT_TILE(0x00,0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8),
  /* L */ TXT_TILE(0x00,0x80,0x80,0x80,0x80,0x80,0x80,0xF8),
  /* V */ TXT_TILE(0x00,0x88,0x88,0x88,0x88,0x88,0x50,0x20),
  /* I */ TXT_TILE(0x00,0xF8,0x20,0x20,0x20,0x20,0x20,0xF8),
  /* N */ TXT_TILE(0x00,0x88,0xC8,0xA8,0xA8,0x98,0x88,0x88),
  /* X */ TXT_TILE(0x00,0x88,0x88,0x50,0x20,0x50,0x88,0x88),
  /* T */ TXT_TILE(0x00,0xF8,0x20,0x20,0x20,0x20,0x20,0x20),

  /* --- Letras INVERTIDAS (oscuro sobre crema) para dentro de
         las cápsulas del HUD. Tiles 33..43. --- */
  /* S */ INV_TILE(0x00,0x78,0x80,0x80,0x70,0x08,0x08,0xF0),
  /* C */ INV_TILE(0x00,0x78,0x80,0x80,0x80,0x80,0x80,0x78),
  /* O */ INV_TILE(0x00,0x70,0x88,0x88,0x88,0x88,0x88,0x70),
  /* R */ INV_TILE(0x00,0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88),
  /* E */ INV_TILE(0x00,0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8),
  /* L */ INV_TILE(0x00,0x80,0x80,0x80,0x80,0x80,0x80,0xF8),
  /* V */ INV_TILE(0x00,0x88,0x88,0x88,0x88,0x88,0x50,0x20),
  /* I */ INV_TILE(0x00,0xF8,0x20,0x20,0x20,0x20,0x20,0xF8),
  /* N */ INV_TILE(0x00,0x88,0xC8,0xA8,0xA8,0x98,0x88,0x88),
  /* X */ INV_TILE(0x00,0x88,0x88,0x50,0x20,0x50,0x88,0x88),
  /* T */ INV_TILE(0x00,0xF8,0x20,0x20,0x20,0x20,0x20,0x20),

  /* --- Tapas redondeadas de las cápsulas.
         Encoding igual que TXT: bit 1 = cream (color 4), bit 0 = panel
         (color 3). Cream forma la píldora, panel las esquinas. --- */
  /* L_cap (tile 44): pill izquierda con rounded top-left y bottom-left */
  TXT_TILE(0x3F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x3F),
  /* R_cap (tile 45): pill derecha con rounded top-right y bottom-right */
  TXT_TILE(0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC),

  /* --- Letras extra light-on-dark: A G M P U (tiles 46..50) --- */
  /* A */ TXT_TILE(0x00,0x70,0x88,0x88,0xF8,0x88,0x88,0x88),
  /* G */ TXT_TILE(0x00,0x78,0x80,0x80,0x98,0x88,0x88,0x78),
  /* M */ TXT_TILE(0x00,0x88,0xD8,0xA8,0xA8,0x88,0x88,0x88),
  /* P */ TXT_TILE(0x00,0xF0,0x88,0x88,0xF0,0x80,0x80,0x80),
  /* U */ TXT_TILE(0x00,0x88,0x88,0x88,0x88,0x88,0x88,0x70)
};

/* ---------------- Tile indices --------------------------------- */
#define TILE_BG        0
#define TILE_BRICK     1
#define TILE_EMPTY     2
#define TILE_PANEL     3
#define TILE_PACCENT   4
#define TILE_PIECE(t)  (5 + (t))    /* t: 0..6 => I, O, T, S, Z, J, L */
#define TILE_DIGIT(d)  (12 + (d))   /* d: 0..9 */
#define TILE_S         22
#define TILE_C         23
#define TILE_O         24
#define TILE_R         25
#define TILE_E         26
#define TILE_L         27
#define TILE_V         28
#define TILE_I         29
#define TILE_N         30
#define TILE_X         31
#define TILE_T         32

/* Letras invertidas (oscuro sobre crema) para dentro de cápsulas */
#define TILE_INV_S     33
#define TILE_INV_C     34
#define TILE_INV_O     35
#define TILE_INV_R     36
#define TILE_INV_E     37
#define TILE_INV_L     38
#define TILE_INV_V     39
#define TILE_INV_I     40
#define TILE_INV_N     41
#define TILE_INV_X     42
#define TILE_INV_T     43

#define TILE_LCAP      44
#define TILE_RCAP      45

/* Letras extra para títulos / game over: A G M P U (light-on-dark) */
#define TILE_A         46
#define TILE_G         47
#define TILE_M         48
#define TILE_P         49
#define TILE_U         50

#define TILE_WHITE     51     /* sólido color 13 (blanco) para flash de líneas */
#define TILE_GHOST     52     /* outline de celda para ghost piece             */
#define TILE_STAR      53     /* estrellita para splash screen                 */
#define TILE_MOON      54     /* luna para splash screen                       */

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
static unsigned char char_to_tile(char c)
{
  if (c >= '0' && c <= '9') return (unsigned char)TILE_DIGIT(c - '0');
  switch (c) {
    case 'S': return TILE_S;  case 'C': return TILE_C;
    case 'O': return TILE_O;  case 'R': return TILE_R;
    case 'E': return TILE_E;  case 'L': return TILE_L;
    case 'V': return TILE_V;  case 'I': return TILE_I;
    case 'N': return TILE_N;  case 'X': return TILE_X;
    case 'T': return TILE_T;  case 'A': return TILE_A;
    case 'G': return TILE_G;  case 'M': return TILE_M;
    case 'P': return TILE_P;  case 'U': return TILE_U;
    default:  return TILE_PANEL;   /* cualquier otro = hueco en panel */
  }
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
  switch (c) {
    case 'S': return TILE_INV_S;  case 'C': return TILE_INV_C;
    case 'O': return TILE_INV_O;  case 'R': return TILE_INV_R;
    case 'E': return TILE_INV_E;  case 'L': return TILE_INV_L;
    case 'V': return TILE_INV_V;  case 'I': return TILE_INV_I;
    case 'N': return TILE_INV_N;  case 'X': return TILE_INV_X;
    case 'T': return TILE_INV_T;
    default:  return TILE_PACCENT;   /* cream plano para relleno */
  }
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
  /* S */ { 0x0036, 0x0462, 0x0036, 0x0462 },
  /* Z */ { 0x0063, 0x0264, 0x0063, 0x0264 },
  /* J */ { 0x0071, 0x0226, 0x0470, 0x0322 },
  /* L */ { 0x0074, 0x0622, 0x0170, 0x0223 }
};

static unsigned char cur_type;
static unsigned char cur_rot;
static signed char   cur_x;
static signed char   cur_y;
static unsigned char next_type;    /* pieza que caerá después */

/* PRNG xorshift 16-bit — ciclo 65535, buena distribución */
static unsigned int rng_state;

static void rng_seed(void)
{
  /* Mezclar dos lecturas de VCount para semilla de 16 bits */
  unsigned char a = SMS_getVCount();
  unsigned char b;
  /* Pequeño busy-wait para que la segunda lectura difiera */
  { unsigned char j; for (j = 0; j < a; j++); }
  b = SMS_getVCount();
  rng_state = ((unsigned int)a << 8) | b;
  if (rng_state == 0) rng_state = 0xBEEF;
}

static unsigned char rand7(void)
{
  rng_state ^= rng_state << 7;
  rng_state ^= rng_state >> 9;
  rng_state ^= rng_state << 8;
  return (unsigned char)(rng_state % 7);
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

static void paint_hud_labels(void)
{
  /* Cápsulas: L_cap + 5 letras + R_cap = 7 tiles. Panel tiene 8 cols
     (12..19). Ponemos la cápsula en cols 13..19 (col 12 queda dark). */
  screen_print_capsule(13, 1,  "SCORE");
  screen_print_capsule(13, 4,  "LEVEL");
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

static void paint_hud_numbers(void)
{
  paint_number(13, 2, score, 6);     /* SCORE 6 dígitos */
  paint_number(15, 5, level, 2);     /* LEVEL 2 dígitos, centrado */
  paint_number(14, 8, lines, 4);     /* LINES 4 dígitos */
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

/* Ghost blocks: la pieza en la posición de aterrizaje con tile outline */
static void ghost_blocks(signed char out[4][2])
{
  signed char ly = landing_y();
  unsigned char lx, lly, i = 0;
  for (lly = 0; lly < 4; lly++) {
    for (lx = 0; lx < 4; lx++) {
      if (shape_has(cur_type, cur_rot, lx, lly)) {
        out[i][0] = cur_x + (signed char)lx;
        out[i][1] = ly + (signed char)lly;
        i++;
      }
    }
  }
}

static void paint_ghost_blocks(signed char bl[4][2])
{
  unsigned char i;
  signed char sy;
  for (i = 0; i < 4; i++) {
    if (bl[i][0] < 0 || bl[i][0] >= BOARD_W) continue;
    sy = bl[i][1] - (signed char)HIDDEN_H;
    if (sy < 0 || sy >= (signed char)VISIBLE_H) continue;
    /* Solo pintar ghost si la celda está vacía (no solaparse con pieza real) */
    if (!board[bl[i][1]][bl[i][0]])
      screen_set(BOARD_X0 + (unsigned char)bl[i][0],
                 BOARD_Y0 + (unsigned char)sy, TILE_GHOST);
  }
}

static void restore_ghost_blocks(void)
{
  unsigned char i;
  for (i = 0; i < 4; i++) {
    if (prev_ghost[i][0] < 0 || prev_ghost[i][0] >= BOARD_W) continue;
    if (prev_ghost[i][1] < 0 || prev_ghost[i][1] >= BOARD_H) continue;
    paint_board_cell((unsigned char)prev_ghost[i][0],
                     (unsigned char)prev_ghost[i][1]);
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
static unsigned char game_over  = 0;
static unsigned char need_full_board_repaint = 0;
static unsigned char need_hud_refresh = 1;
static unsigned char heartbeat = 0;

/* ---- Estado del juego para animación de line clear ---- */
#define STATE_TITLE    0
#define STATE_PLAY     1
#define STATE_FLASH    2
#define STATE_GAMEOVER 3
#define STATE_PAUSE    4

static unsigned char game_state = STATE_TITLE;
static unsigned char start_was_pressed = 0;  /* para edge-detect del Start de GG */
static unsigned char flash_rows[4];     /* índices (en board) de las filas llenas */
static unsigned char n_flash_rows = 0;
static unsigned char flash_timer = 0;

#define FLASH_DURATION 24    /* frames totales de animación (~400ms)     */
#define FLASH_PHASE    6     /* alterna blanco/normal cada N frames      */

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

/* Puntuación clásica (NES-style) */
static const unsigned int line_score[5] = { 0, 40, 100, 300, 1200 };

static void add_lines(unsigned char n)
{
  if (n == 0) return;
  if (n > 4) n = 4;
  score += (unsigned long)line_score[n] * (unsigned long)(level + 1);
  lines += n;
  /* Nivel sube cada 10 líneas */
  {
    unsigned char new_level = (unsigned char)(lines / 10);
    if (new_level > MAX_LEVEL) new_level = MAX_LEVEL;
    if (new_level != level) level = new_level;
  }
  need_hud_refresh = 1;
}

static unsigned char soft_dropping = 0;  /* 1 si el descenso actual es por soft drop */

static unsigned char piece_step_down(void)
{
  if (fits(cur_type, cur_rot, cur_x, cur_y + 1)) {
    cur_y++;
    if (soft_dropping) { score++; need_hud_refresh = 1; }
    return 1;
  }
  lock_piece();
  need_full_board_repaint = 1;  /* mostrar pieza recién bloqueada  */
  have_prev = 0;

  /* ¿hay líneas completas? -> entrar en STATE_FLASH y esperar a que
     termine la animación antes de colapsar y spawnear. */
  n_flash_rows = scan_full_rows(flash_rows);
  if (n_flash_rows > 0) {
    game_state  = STATE_FLASH;
    flash_timer = FLASH_DURATION;
    return 0;
  }

  /* Sin líneas: flujo normal, spawn inmediato. */
  if (!spawn_piece()) { game_over = 1; game_state = STATE_GAMEOVER; }
  need_hud_refresh = 1;
  return 0;
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
  unsigned char removed = clear_lines();    /* removerá exactamente n_flash_rows */
  (void)removed;
  add_lines(n_flash_rows);
  n_flash_rows = 0;
  need_full_board_repaint = 1;
  have_prev = 0;
  if (!spawn_piece()) { game_over = 1; game_state = STATE_GAMEOVER; }
  need_hud_refresh = 1;
  game_state = STATE_PLAY;
}

/* ================================================================
   Game Over overlay + reinicio
   ================================================================ */
/* Splash screen estilo GB Tetris adaptada a GG (sin Nintendo, obviamente).
   Cielo nocturno con estrellas + luna + título + silueta del Kremlin
   hecha con piezas de Tetris (meta-homenaje) + muralla de ladrillos. */
static void paint_title_screen(void)
{
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

  /* --- "PRESS START" (cream) --- */
  screen_set(4, 14, char_to_tile('P'));
  screen_set(5, 14, char_to_tile('R'));
  screen_set(6, 14, char_to_tile('E'));
  screen_set(7, 14, char_to_tile('S'));
  screen_set(8, 14, char_to_tile('S'));
  screen_set(10, 14, char_to_tile('S'));
  screen_set(11, 14, char_to_tile('T'));
  screen_set(12, 14, char_to_tile('A'));
  screen_set(13, 14, char_to_tile('R'));
  screen_set(14, 14, char_to_tile('T'));

  /* --- Año --- */
  screen_set(8,  16, TILE_DIGIT(2));
  screen_set(9,  16, TILE_DIGIT(0));
  screen_set(10, 16, TILE_DIGIT(2));
  screen_set(11, 16, TILE_DIGIT(6));
}

/* Overlay de pausa sobre el tablero */
static void paint_pause_overlay(void)
{
  screen_set(3, 8, char_to_tile('P'));
  screen_set(4, 8, char_to_tile('A'));
  screen_set(5, 8, char_to_tile('U'));
  screen_set(6, 8, char_to_tile('S'));
  screen_set(7, 8, char_to_tile('E'));
}

static void paint_gameover_overlay(void)
{
  /* "GAME" centrado en fila 7, "OVER" en fila 9 del tablero */
  screen_set(3, 7, char_to_tile('G'));
  screen_set(4, 7, char_to_tile('A'));
  screen_set(5, 7, char_to_tile('M'));
  screen_set(6, 7, char_to_tile('E'));

  screen_set(3, 9, char_to_tile('O'));
  screen_set(4, 9, char_to_tile('V'));
  screen_set(5, 9, char_to_tile('E'));
  screen_set(6, 9, char_to_tile('R'));
}

static void reset_game(void);  /* forward decl */

/* ================================================================
   Input
   ================================================================ */
#define DAS_FRAMES 10
#define ARR_FRAMES 3
#define ROT_DAS_FRAMES 25
#define ROT_ARR_FRAMES 12

static unsigned char das_counter_l = 0;
static unsigned char das_counter_r = 0;
static unsigned char rot_cd_cw  = 0;
static unsigned char rot_cd_ccw = 0;

static void try_move(signed char dx)
{
  if (fits(cur_type, cur_rot, cur_x + dx, cur_y)) cur_x += dx;
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
  if (dist > 0) score += (unsigned long)dist * 2;
  cur_y = ly;
  need_hud_refresh = 1;
  /* El bloqueo se gestiona al bajar 0 filas en piece_step_down */
}

static void try_rotate(unsigned char nr)
{
  if (fits(cur_type, nr, cur_x, cur_y))          cur_rot = nr;
  else if (fits(cur_type, nr, cur_x - 1, cur_y)) { cur_rot = nr; cur_x--; }
  else if (fits(cur_type, nr, cur_x + 1, cur_y)) { cur_rot = nr; cur_x++; }
}

static void handle_input(void)
{
  unsigned int k = SMS_getKeysPressed();
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
    if (rot_cd_cw == 0) {
      try_rotate((unsigned char)((cur_rot + 1) & 3));
      rot_cd_cw = ROT_DAS_FRAMES;
    } else if (--rot_cd_cw == 0) {
      try_rotate((unsigned char)((cur_rot + 1) & 3));
      rot_cd_cw = ROT_ARR_FRAMES;
    }
  } else rot_cd_cw = 0;

  if (h & PORT_A_KEY_2) {
    if (rot_cd_ccw == 0) {
      try_rotate((unsigned char)((cur_rot + 3) & 3));
      rot_cd_ccw = ROT_DAS_FRAMES;
    } else if (--rot_cd_ccw == 0) {
      try_rotate((unsigned char)((cur_rot + 3) & 3));
      rot_cd_ccw = ROT_ARR_FRAMES;
    }
  } else rot_cd_ccw = 0;

  if (h & PORT_A_KEY_DOWN) {
    if (fall_timer > 1) fall_timer = 1;   /* 60 celdas/seg soft drop */
    soft_dropping = 1;
  } else {
    soft_dropping = 0;
  }

  /* Hard drop: UP tira la pieza al fondo de golpe + bloqueo inmediato */
  if (h & PORT_A_KEY_UP) {
    hard_drop();
    /* Forzamos caída inmediata para que piece_step_down bloquee la pieza */
    fall_timer = 1;
  }
}

/* Datos PSG incluidos aquí (después del código) para que SDCC los
   coloque en el segmento CODE de ROM y no en INITIALIZED (RAM). */
#include "underwater_psg.h"

/* ================================================================
   Reset / reinicio completo del juego (sin reiniciar VRAM/tiles)
   ================================================================ */
static void reset_game(void)
{
  unsigned char x, y;
  for (y = 0; y < BOARD_H; y++)
    for (x = 0; x < BOARD_W; x++)
      board[y][x] = 0;
  score = 0; level = 0; lines = 0;
  game_over = 0;
  game_state = STATE_PLAY;
  n_flash_rows = 0;
  have_prev = 0;
  invalidate_ghost_cache();
  soft_dropping = 0;
  das_counter_l = 0; das_counter_r = 0;
  rot_cd_cw = 0; rot_cd_ccw = 0;
  rng_seed();   /* re-sembrar en cada reinicio */
  next_type = rand7();
  spawn_piece();
  fall_timer = fall_frames();
  /* Pintar layout del juego (marcos + panel + cápsulas) sobre lo
     que hubiera (splash screen, game over screen, etc.) */
  paint_static_layout();
  paint_hud_labels();
  need_full_board_repaint = 1;
  need_hud_refresh = 1;
}

/* ================================================================
   Handler del VBlank IRQ: corre a 60Hz estable aunque el main loop
   se atasque. Ejecuta PSGFrame (música, escribe solo al PSG) y
   marca un flag para que el main loop vuelque el tile animado.
   NUNCA escribe al VDP desde aquí: eso causaba race con nt_flush.
   ================================================================ */
#define ANIM_SPEED 6    /* 60/6 = 10 Hz visual, más tolerante a frame drops */
static void vblank_isr(void)
{
  PSGFrame();
  if (++anim_counter >= ANIM_SPEED) {
    anim_counter = 0;
    anim_frame_idx = (unsigned char)((anim_frame_idx + 1) & 7);
    anim_pending = 1;
  }
}

/* ================================================================
   Main
   ================================================================ */
void main(void)
{
  unsigned char x, y;

  SMS_VRAMmemsetW(0, 0x0000, 16384);

  /* Cargar tileset estático (tile 0 solo) */
  SMS_loadTiles(static_tiles, 0, sizeof(static_tiles));

  /* Tile 1 = ladrillo, 2 = celda vacía, 3 = panel, 4 = panel accent */
  SMS_loadTiles(brick_tile, TILE_BRICK, sizeof(brick_tile));
  /* Generar tiles animados de fondo y cargar el frame 0 */
  gen_anim_tiles();
  SMS_loadTiles(&anim_tiles[0], TILE_EMPTY, 32);
  (void)empty_board_tile;   /* legacy, no usado */
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

  /* Tiles 12..32: fuente (dígitos + 11 letras) */
  SMS_loadTiles(font_tiles, 12, sizeof(font_tiles));

  GG_loadBGPalette(bg_palette);

  SMS_initSprites();
  SMS_copySpritestoSAT();

  nt_init();
  paint_title_screen();
  nt_flush_all();

  game_state = STATE_TITLE;
  psg_silence_all();

  /* Música: Alex Kidd in Miracle World - Underwater (SN76489 puro, 2:27).
     Registramos PSGFrame como handler del VBlank ISR: la música suena a
     60Hz estable sin depender de la velocidad del main loop. */
  PSGPlay(underwater_psg);
  SMS_setFrameInterruptHandler(vblank_isr);   /* música + anim fondo a 60Hz */

  SMS_displayOn();

  for (;;) {
    signed char new_blocks[4][2];
    unsigned char i;

    SMS_waitForVBlank();
    /* PSGFrame corre en el ISR. La anim de fondo marca flag desde el
       ISR y volcamos el tile aquí, en el main loop, para evitar el
       race con nt_flush sobre el VDP. */
    if (anim_pending) {
      SMS_loadTiles(&anim_tiles[anim_frame_idx * 32], TILE_EMPTY, 32);
      anim_pending = 0;
    }
    nt_flush();

    if (game_state == STATE_TITLE) {
      /* Start (o Button 1/2) arranca partida */
      {
        unsigned int k = SMS_getKeysPressed();
        unsigned char start_now = GG_START_PRESSED();
        unsigned char start_edge = (start_now && !start_was_pressed);
        start_was_pressed = start_now;
        if ((k & (PORT_A_KEY_1 | PORT_A_KEY_2)) || start_edge) {
          reset_game();
        }
      }
    } else if (game_state == STATE_PAUSE) {
      paint_pause_overlay();
      /* Start de GG reanuda (flanco: solo al pulsar, no al mantener) */
      if (GG_START_PRESSED() && !start_was_pressed) {
        game_state = STATE_PLAY;
        need_full_board_repaint = 1;
        invalidate_ghost_cache();
      }
      start_was_pressed = GG_START_PRESSED();
    } else if (game_state == STATE_PLAY) {
      if (!game_over) {
        /* Start de GG pausa (flanco) */
        if (GG_START_PRESSED() && !start_was_pressed) {
          game_state = STATE_PAUSE;
        }
        start_was_pressed = GG_START_PRESSED();
        if (game_state == STATE_PLAY) {
          handle_input();
          if (--fall_timer == 0) {
            fall_timer = fall_frames();
            piece_step_down();
          }
        }
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
      if (--flash_timer == 0) {
        finalize_line_clear();
      }
    } else if (game_state == STATE_GAMEOVER) {
      /* Primer frame: pintar overlay GAME OVER sobre el tablero */
      if (need_full_board_repaint) {
        paint_full_board();
        need_full_board_repaint = 0;
      }
      paint_gameover_overlay();

      /* Cualquier botón (A/B/Start) vuelve al menú principal */
      {
        unsigned int k = SMS_getKeysPressed();
        unsigned char start_now = GG_START_PRESSED();
        unsigned char start_edge = (start_now && !start_was_pressed);
        start_was_pressed = start_now;
        if ((k & (PORT_A_KEY_1 | PORT_A_KEY_2)) || start_edge) {
          paint_title_screen();
          game_state = STATE_TITLE;
        }
      }
    }

    /* HUD solo en estados de juego (no en TITLE) */
    if (need_hud_refresh && game_state != STATE_TITLE) {
      paint_hud_numbers();
      paint_next_preview();
      need_hud_refresh = 0;
    }

  }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(0, 4, "antxiko", "Tetris GG",
                                "Tetris for Sega Game Gear - iter 3 HUD+bricks");
