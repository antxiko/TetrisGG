#ifndef MUSIC_H
#define MUSIC_H

/* Motor de música propio (Kalinka en el título, Korobeiniki en la
   partida) + efectos.
   Todo el acceso al PSG ocurre en music_frame(), llamado desde la ISR
   de VBlank. Las funciones de control solo tocan flags de 1 byte, así
   que son seguras desde el main loop. */

#define SFX_ROTATE    1
#define SFX_LOCK      2
#define SFX_LINE      3
#define SFX_TETRIS    4
#define SFX_LEVELUP   5
#define SFX_PAUSE     6
#define SFX_GAMEOVER  7
#define SFX_TSPIN     8

void music_play_game(void);
void music_play_title(void);
void music_stop(void);
void music_set_paused(unsigned char paused);
void music_set_danger(unsigned char danger);   /* 1 = tempo rápido */
void sfx_play(unsigned char id);
void music_frame(void);                         /* solo desde la ISR */

#endif
