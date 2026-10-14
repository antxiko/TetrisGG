#include "music.h"

/* ================================================================
   Secuenciador PSG de 4 canales para la música de partida.
     canal 0: melodía      canal 1: armonía (lo pisan los SFX tonales)
     canal 2: bajo         canal 3: percusión con ruido (y SFX de ruido)
   Unidad de duración = corchea. Un reloj global de corcheas mantiene
   los 4 canales sincronizados aunque cambie el tempo (modo peligro).
   PSG: latch 1ccrdddd (r=1 volumen), data 0ddddddd. Atenuación 15=mudo.
   ================================================================ */
__sfr __at 0x7F PSG_port;

/* Índices de nota (semitonos desde A2). Cuenta PSG = 3579545/(32*f).
   A2 es la nota más grave que cabe en los 10 bits del divisor.     */
#define A2   0
#define B2   2
#define C3   3
#define D3   5
#define E3   7
#define FS3  9
#define G3   10
#define E4   19
#define G4   22
#define GS4  23
#define A4   24
#define B4   26
#define C5   27
#define D5   29
#define DS5  30
#define E5   31
#define F5   32
#define FS5  33
#define G5   34
#define GS5  35
#define A5   36
#define B5   38
#define C6   39
#define D6   41
#define E6   43
#define G6   46
#define A6   48
#define B6   50

static const unsigned int note_counts[52] = {
  1017, 960, 906, 855, 807, 762, 719, 679, 641, 605, 571, 539,  /* A2..G#3 */
   508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269,  /* A3..G#4 */
   254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135,  /* A4..G#5 */
   127, 120, 113, 107, 101,  95,  90,  85,  80,  76,  71,  67,  /* A5..G#6 */
    64,  60,  57,  53                                           /* A6..C7  */
};

#define R     0xFF    /* silencio            */
#define LOOP  0xFE    /* volver al principio */
#define HAT   1       /* percusión: charles  */
#define SNR   2       /* percusión: caja     */

/* ---------------- Korobeiniki (Tetris tipo A) ------------------
   Forma: A A B (24 compases de 4/4, 8 corcheas cada uno).      */
#define MEL_A \
  E5,2, B4,1, C5,1, D5,2, C5,1, B4,1, \
  A4,2, A4,1, C5,1, E5,2, D5,1, C5,1, \
  B4,3, C5,1, D5,2, E5,2, \
  C5,2, A4,2, A4,2, R,2, \
  D5,3, F5,1, A5,2, G5,1, F5,1, \
  E5,3, C5,1, E5,2, D5,1, C5,1, \
  B4,2, B4,1, C5,1, D5,2, E5,2, \
  C5,2, A4,2, A4,2, R,2
#define MEL_B \
  E5,4, C5,4, \
  D5,4, B4,4, \
  C5,4, A4,4, \
  GS4,4, B4,2, R,2, \
  E5,4, C5,4, \
  D5,4, B4,4, \
  C5,2, E5,2, A5,4, \
  GS5,6, R,2

#define HAR_A \
  GS4,4, B4,2, GS4,2, \
  E4,2, E4,2, C5,2, A4,2, \
  GS4,3, A4,1, B4,2, GS4,2, \
  A4,2, E4,2, E4,2, R,2, \
  A4,4, F5,2, D5,2, \
  C5,3, G4,1, C5,2, G4,2, \
  GS4,3, A4,1, B4,2, GS4,2, \
  A4,2, E4,2, E4,2, R,2
#define HAR_B \
  C5,4, A4,4, \
  B4,4, GS4,4, \
  A4,4, E4,4, \
  E4,4, GS4,2, R,2, \
  C5,4, A4,4, \
  B4,4, GS4,4, \
  A4,2, C5,2, E5,4, \
  E5,6, R,2

/* Bajo: octavas alternas. O8 = media parte en corcheas, Q4 = en negras */
#define O8(n) (n),1, (n)+12,1, (n),1, (n)+12,1
#define Q4(n) (n),2, (n)+12,2
#define BAS_A \
  O8(E3), O8(E3),  O8(A2), O8(A2),  O8(B2), O8(E3),  O8(A2), O8(A2), \
  O8(D3), O8(D3),  O8(C3), O8(C3),  O8(B2), O8(E3),  O8(A2), O8(A2)
#define BAS_B \
  Q4(A2), Q4(A2),  Q4(E3), Q4(E3),  Q4(A2), Q4(A2),  Q4(E3), Q4(E3), \
  Q4(A2), Q4(A2),  Q4(E3), Q4(E3),  Q4(A2), Q4(A2),  Q4(E3), Q4(E3)

#define DR_A1 HAT,1, HAT,1, SNR,1, HAT,1, HAT,1, HAT,1, SNR,1, HAT,1
#define DR_A  DR_A1, DR_A1, DR_A1, DR_A1, DR_A1, DR_A1, DR_A1, DR_A1
#define DR_B1 HAT,2, HAT,2, SNR,2, HAT,2
#define DR_FILL HAT,2, SNR,1, SNR,1, SNR,1, SNR,1, SNR,1, SNR,1
#define DR_B  DR_B1, DR_B1, DR_B1, DR_B1, DR_B1, DR_B1, DR_B1, DR_FILL

static const unsigned char k_mel[] = { MEL_A, MEL_A, MEL_B, LOOP };
static const unsigned char k_har[] = { HAR_A, HAR_A, HAR_B, LOOP };
static const unsigned char k_bas[] = { BAS_A, BAS_A, BAS_B, LOOP };
static const unsigned char k_drm[] = { DR_A,  DR_A,  DR_B,  LOOP };

/* ---------------- Kalinka (Larionov, 1860) - música del título ----
   Melodía según "Singing Together" (BBC, 1975), subida una octava.
   Armonía, bajo y percusión: arreglo propio. 2/4, unidad =
   semicorchea (8 por compás). Estribillo rápido y luego la estrofa. */
#define KC_A  A5,4, FS5,2, G5,2          /* "ka-lin-ka"               */
#define KC_B  A5,4, G5,2, FS5,2          /* "ka-lin-ka mo-"           */
#define KC_C  A5,3, G5,1, FS5,2, G5,2
#define KAL_PHRASE1 KC_A, KC_A, KC_B, E5,4, B5,4
#define KAL_PHRASE2 KC_C, KC_A, KC_B
#define KAL_MEL_CHORUS \
  B5,4, B5,4, \
  KAL_PHRASE1, KAL_PHRASE2, E5,4, B5,4, \
  KAL_PHRASE1, KAL_PHRASE2, E5,8
#define KV_TURN  B5,2, D6,2, C6,2, B5,1
#define KAL_MEL_VERSE \
  E6,8,  D6,8,  KV_TURN, G5,1,  G5,4, D5,4, \
  KV_TURN, A5,1,  G5,4, D5,4,  D5,4, E5,2, FS5,2, \
  A5,2, G5,2, FS5,2, E5,2,  D5,4, D6,4,  D6,8, \
  KV_TURN, A5,1,  G5,4, E5,4,  KV_TURN, A5,1,  G5,4, D5,4, \
  E5,4, E5,2, FS5,2,  A5,2, G5,2, FS5,2, E5,2,  D6,4, C6,4,  B5,8

/* Armonía: una nota del acorde por compás, por debajo de la melodía */
#define KH_B7  DS5,8
#define KAL_HAR_CHORUS \
  KH_B7, \
  KH_B7, KH_B7, KH_B7, G4,8, KH_B7, KH_B7, KH_B7, G4,8, \
  KH_B7, KH_B7, KH_B7, G4,8, KH_B7, KH_B7, KH_B7, G4,8
#define KAL_HAR_VERSE \
  G5,8, FS5,8, D5,8, B4,8, D5,8, B4,8, A4,8, C5,8, A4,4, FS5,4, FS5,8, \
  D5,8, B4,8, D5,8, B4,8, B4,8, C5,8, FS5,8, D5,8

/* Bajo "um-pa": fundamental y quinta en negras */
#define KB_B7  B2,4, FS3,4
#define KB_EM  E3,4, B2,4
#define KB_D   D3,4, A2,4
#define KB_G   G3,4, D3,4
#define KAL_BAS_CHORUS \
  KB_B7, \
  KB_B7, KB_B7, KB_B7, KB_EM, KB_B7, KB_B7, KB_B7, KB_EM, \
  KB_B7, KB_B7, KB_B7, KB_EM, KB_B7, KB_B7, KB_B7, KB_EM
#define KAL_BAS_VERSE \
  KB_EM, KB_D, KB_G, KB_G, KB_G, KB_G, KB_D, KB_D, KB_D, KB_D, \
  KB_G, KB_EM, KB_G, KB_G, KB_EM, KB_D, KB_D, KB_G

/* Percusión: estribillo con caja en la parte 2; estrofa solo charles */
#define KD_C  HAT,2, HAT,2, SNR,2, HAT,2
#define KD_V  HAT,4, HAT,4
#define KD_C4 KD_C, KD_C, KD_C, KD_C
#define KD_V6 KD_V, KD_V, KD_V, KD_V, KD_V, KD_V
#define KAL_DRM_CHORUS KD_C, KD_C4, KD_C4, KD_C4, KD_C4
#define KAL_DRM_VERSE  KD_V6, KD_V6, KD_V6

static const unsigned char t_mel[] = { KAL_MEL_CHORUS, KAL_MEL_VERSE, LOOP };
static const unsigned char t_har[] = { KAL_HAR_CHORUS, KAL_HAR_VERSE, LOOP };
static const unsigned char t_bas[] = { KAL_BAS_CHORUS, KAL_BAS_VERSE, LOOP };
static const unsigned char t_drm[] = { KAL_DRM_CHORUS, KAL_DRM_VERSE, LOOP };

static const unsigned char * const k_streams[4] = { k_mel, k_har, k_bas, k_drm };
static const unsigned char * const t_streams[4] = { t_mel, t_har, t_bas, t_drm };

/* Instrumentos de los canales tonales: atenuación inicial, frames por
   paso de decaimiento, atenuación mínima a la que decae, y frames de
   silencio al final de la nota (articulación).                     */
static const unsigned char inst_vol0[3]  = { 3, 7, 4 };
static const unsigned char inst_decay[3] = { 4, 5, 2 };
static const unsigned char inst_floor[3] = { 7, 11, 10 };
static const unsigned char inst_gate[4]  = { 2, 2, 4, 0 };

/* Percusión: control de ruido (blanco + frecuencia), atenuación inicial */
static const unsigned char drum_ctrl[3] = { 0, 0x04, 0x05 };
static const unsigned char drum_vol0[3] = { 15, 11, 6 };

#define FPE_NORMAL 12     /* Korobeiniki: frames por corchea, 150 BPM */
#define FPE_DANGER  9     /* pila alta: 200 BPM                       */
#define FPE_TITLE   6     /* Kalinka: frames por semicorchea, 150 BPM */

static const unsigned char *ch_ptr[4];
static unsigned char ch_left[4];      /* corcheas que le quedan a la nota */
static unsigned char ch_note[4];
static unsigned char ch_vol[4];       /* atenuación actual                */
static unsigned char ch_env[4];
static unsigned char ch_decay[4];
static unsigned char ch_floor[4];

static volatile unsigned char m_on = 0;
static volatile unsigned char m_paused = 0;
static volatile unsigned char m_fpe = FPE_NORMAL;
static const unsigned char * const *m_streams = k_streams;
static unsigned char m_is_game = 0;   /* el modo peligro solo afecta a la partida */
static unsigned char m_frame;

/* ---------------- SFX ------------------------------------------
   Secuencias de (nota, atenuación, frames), terminadas en 0xFF.
   Los tonales suenan en el canal 1, los de ruido en el canal 3
   (ahí "nota" es el valor de control del ruido).                 */
#define SFX_END 0xFF
static const unsigned char sfx_rotate[]   = { E6,9,2, B6,11,2, SFX_END };
static const unsigned char sfx_lock[]     = { 0x06,5,2, 0x06,8,2, 0x06,11,2, SFX_END };
static const unsigned char sfx_line[]     = { C5,4,3, E5,4,3, G5,4,3, C6,4,4, C6,8,4, SFX_END };
static const unsigned char sfx_tetris[]   = { A4,3,3, C5,3,3, E5,3,3, A5,3,3, C6,3,3, E6,3,3,
                                              A6,3,8, A6,7,6, SFX_END };
static const unsigned char sfx_levelup[]  = { G5,4,4, C6,4,4, E6,4,4, G6,4,10, G6,9,6, SFX_END };
static const unsigned char sfx_pause[]    = { E6,6,3, A5,6,5, SFX_END };
static const unsigned char sfx_gameover[] = { E5,4,8, D5,4,8, C5,4,8, B4,4,8, A4,4,8,
                                              GS4,4,8, A4,4,30, A4,9,10, SFX_END };

static const unsigned char sfx_tspin[]    = { G5,3,3, B5,3,3, D6,3,3, G6,3,4, D6,3,3, G6,3,8,
                                              G6,8,6, SFX_END };

/* "Tiruriruriiiiin" del logo de Compila!: Sol-Si-Re-Si rápidos y un Sol
   agudo largo que se apaga poco a poco */
static const unsigned char sfx_logo[]     = { G5,3,4, B5,3,4, D6,3,4, B5,3,4,
                                              G6,2,24, G6,4,12, G6,6,12, G6,8,12,
                                              G6,11,12, SFX_END };

static const unsigned char * const sfx_table[10] = {
  0, sfx_rotate, sfx_lock, sfx_line, sfx_tetris, sfx_levelup, sfx_pause, sfx_gameover,
  sfx_tspin, sfx_logo
};

static volatile unsigned char sfx_req_tone  = 0;   /* petición desde el main loop */
static volatile unsigned char sfx_req_noise = 0;
static const unsigned char *sfx_ptr[2];             /* 0 = canal 1, 1 = canal 3 */
static unsigned char sfx_left[2];
static unsigned char sfx_mask = 0;                  /* bit n = canal n ocupado por SFX */

static void psg_tone(unsigned char ch, unsigned int count)
{
  PSG_port = (unsigned char)(0x80 | (ch << 5) | (count & 0x0F));
  PSG_port = (unsigned char)((count >> 4) & 0x3F);
}

static void psg_vol(unsigned char ch, unsigned char atten)
{
  PSG_port = (unsigned char)(0x90 | (ch << 5) | atten);
}

static void fetch(unsigned char i)
{
  unsigned char n = *ch_ptr[i];
  if (n == LOOP) {
    ch_ptr[i] = m_streams[i];
    n = *ch_ptr[i];
  }
  ch_ptr[i]++;
  ch_left[i] = *ch_ptr[i]++;
  ch_note[i] = n;
  ch_env[i]  = 0;
  if (n == R) { ch_vol[i] = 15; return; }
  if (i == 3) {
    ch_vol[3]   = drum_vol0[n];
    ch_decay[3] = 1;
    ch_floor[3] = 15;
    if (!(sfx_mask & 0x08)) PSG_port = (unsigned char)(0xE0 | drum_ctrl[n]);
  } else {
    ch_vol[i]   = inst_vol0[i];
    ch_decay[i] = inst_decay[i];
    ch_floor[i] = inst_floor[i];
    if (!(sfx_mask & (1 << i))) psg_tone(i, note_counts[n]);
  }
}

static void music_play(const unsigned char * const *streams,
                       unsigned char fpe, unsigned char is_game)
{
  unsigned char i;
  m_on = 0;
  m_streams = streams;
  m_is_game = is_game;
  m_fpe     = fpe;
  for (i = 0; i < 4; i++) {
    ch_ptr[i]  = streams[i];
    ch_left[i] = 1;
    ch_note[i] = R;
    ch_vol[i]  = 15;
  }
  m_frame  = (unsigned char)(m_fpe - 1);   /* el primer frame carga notas */
  m_paused = 0;
  m_on = 1;
}

void music_play_game(void)  { music_play(k_streams, FPE_NORMAL, 1); }
void music_play_title(void) { music_play(t_streams, FPE_TITLE,  0); }

void music_stop(void)
{
  unsigned char i;
  m_on = 0;
  for (i = 0; i < 4; i++)
    if (!(sfx_mask & (1 << i))) psg_vol(i, 15);
}

void music_set_paused(unsigned char paused)
{
  unsigned char i;
  m_paused = paused;
  if (paused)
    for (i = 0; i < 4; i++)
      if (!(sfx_mask & (1 << i))) psg_vol(i, 15);
}

void music_set_danger(unsigned char danger)
{
  if (m_is_game) m_fpe = danger ? FPE_DANGER : FPE_NORMAL;
}

void sfx_play(unsigned char id)
{
  if (id == SFX_LOCK) sfx_req_noise = id;
  else                sfx_req_tone  = id;
}

static void sfx_frame(void)
{
  unsigned char k;
  for (k = 0; k < 2; k++) {
    unsigned char ch  = k ? 3 : 1;
    unsigned char bit = (unsigned char)(1 << ch);
    unsigned char req = k ? sfx_req_noise : sfx_req_tone;
    if (req) {
      sfx_ptr[k]  = sfx_table[req];
      sfx_left[k] = 0;
      sfx_mask   |= bit;
      if (k) sfx_req_noise = 0; else sfx_req_tone = 0;
    }
    if (!(sfx_mask & bit)) continue;
    if (sfx_left[k] == 0) {
      unsigned char n = *sfx_ptr[k]++;
      if (n == SFX_END) {
        /* Devolver el canal a la música: restaurar su tono actual */
        sfx_mask &= (unsigned char)~bit;
        psg_vol(ch, 15);
        if (m_on && ch_note[ch] != R) {
          if (ch == 3) PSG_port = (unsigned char)(0xE0 | drum_ctrl[ch_note[3]]);
          else         psg_tone(ch, note_counts[ch_note[ch]]);
        }
        continue;
      }
      if (ch == 3) PSG_port = (unsigned char)(0xE0 | n);
      else         psg_tone(ch, note_counts[n]);
      psg_vol(ch, *sfx_ptr[k]++);
      sfx_left[k] = *sfx_ptr[k]++;
    }
    sfx_left[k]--;
  }
}

void music_frame(void)
{
  unsigned char i;
  if (m_on && !m_paused) {
    if (++m_frame >= m_fpe) {
      m_frame = 0;
      for (i = 0; i < 4; i++)
        if (--ch_left[i] == 0) fetch(i);
    }
    for (i = 0; i < 4; i++) {
      if (ch_vol[i] < 15) {
        if (ch_left[i] == 1 && inst_gate[i] &&
            m_frame >= (unsigned char)(m_fpe - inst_gate[i])) {
          ch_vol[i] = 15;
        } else if (++ch_env[i] >= ch_decay[i]) {
          ch_env[i] = 0;
          if (ch_vol[i] < ch_floor[i]) ch_vol[i]++;
        }
      }
      if (!(sfx_mask & (1 << i))) psg_vol(i, ch_vol[i]);
    }
  }
  sfx_frame();
}
