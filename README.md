# Tetris GG

Tetris para **Sega Game Gear**. En color. Como debía haber salido en 1989 y no en un ladrillo color hueso con manchas amarillentas que parecía una calculadora Texas Instruments olvidada junto a una lata de Cola Cao de 1978.

La Game Gear era **negra, apaisada y seria**. Parecía un dispositivo electrónico. Parecía algo que podías sacar en el autobús a los 30 años sin que el de al lado sospechase que estabas jugando al Mario. La Game Boy parecía un mando de garaje con un radiodespertador soldado encima, operado por pilas que no duraban lo suficiente como para que te mereciera la pena comprarla pero — ay — aguantaban más que las de la Game Gear, y eso bastó para quedarse con un mercado entero. La historia no recompensa al que tiene mejor industrial design. La historia recompensa al que tiene mejor exclusiva y peor diseño de batería.

Escrito en C sobre [devkitSMS](https://github.com/sverx/devkitSMS) y compilado con SDCC.

## Contexto histórico (lectura recomendada si tu infancia fue monocroma)

El Tetris no lo inventó Nintendo. Lo programó **Alekséi Pázhitnov** en 1984 en un Электроника 60 soviético que no tenía ni sprites, ni scroll, ni color, ni posibilidad de hacerse rico con la IP porque él trabajaba para la Academia de Ciencias de la URSS y el software era propiedad del Estado. Entre 1988 y 1991 los derechos rebotaron por ELORG, Andromeda, Mirrorsoft, Atari, Spectrum HoloByte y Nintendo en una pelea de licencias que es dignísima de un dramatizado de Netflix, y que terminó con Henk Rogers volando a Moscú con un fax y saliendo con la exclusiva portátil mundial bajo el brazo. Nintendo le pagó a Pázhitnov literalmente cero royalties hasta 1996. El juego que define una portátil y su autor sin ver un centavo hasta la quinta generación de hardware.

Que en la cabeza colectiva Tetris esté fusionado con la Game Boy es puro efecto de esa exclusiva, más un bundle agresivo con la consola, más doce años de repetirlo en anuncios. No es una verdad técnica, ni una verdad histórica, ni una verdad estética. Es marketing reforzado por nostalgia. Y la nostalgia, cuando se cuestiona con datos, se cae.

Así que esto no es una "herejía". Es lo que debería haber pasado cuando Sega lanzó en 1990 una portátil con **paleta de 4096 colores**, 32 en pantalla, hardware scroll, sprites de verdad y dos canales de sonido más que la competencia. Aquí tienes tu Korobéiniki con siete tetrominoes en cian, amarillo, magenta, verde, rojo, azul y naranja. De nada.

## Estado

Jugable de principio a fin.

- **Pantalla de título**: "TETRIS GG" + "PRESS START". Pulsa botón para empezar.
- **Gameplay**: 7 tetrominoes con rotación + wall-kick básico. Gravedad por nivel, line clear, game over con reinicio.
- **Input**: DAS/ARR (delay + auto-repeat) sin depender del edge flag del VBlank. Rotación con ambos botones (CW/CCW). Soft drop (↓) + hard drop (↑).
- **Ghost piece**: outline blanco en la posición de aterrizaje. Solo se recalcula cuando la pieza cambia de posición.
- **HUD**: cápsulas crema con esquinas redondeadas en dark mode. SCORE, LEVEL, LINES y NEXT con preview de la siguiente pieza.
- **Scoring NES-style**: 40 / 100 / 300 / 1200 × (nivel+1). +1 por celda de soft drop, +2 por celda de hard drop.
- **Niveles**: suben cada 10 líneas. Gravedad acelera de 0.5s/celda (nivel 0) a 1 celda/frame (nivel 19+).
- **Animación de line clear**: parpadeo blanco/color de las filas antes del colapso.
- **Pausa**: botón Start (Enter en Kega Fusion) pausa y reanuda.
- **Game Over**: overlay "GAME OVER" al morir. Botón para reiniciar.
- **Render**: shadow buffer del name table con dirty tracking y flush presupuestado (max 64 celdas/VBlank). Cero flicker. Cache de pieza: solo se repinta cuando cambia de estado.
- **Muros con ladrillos**, piezas pseudo-3D con highlight/shadow, celda vacía con grid sutil.
- **RNG**: xorshift 16-bit sembrado con VCount para secuencias distintas en cada partida.
- **Música**: tema **Underwater de Alex Kidd in Miracle World** (arreglado como VGM→PSG, multi-canal SN76489, 2:27 en loop). Reproducida vía `PSGlib` con `PSGFrame` registrado como handler del frame interrupt — la música corre en el ISR de VBlank y es inmune a los frame drops del main loop, que es como lo hacen los juegos comerciales.

Pendiente: SFX (rotar, bloquear, line clear, tetris, game over), arte distinto por pieza, high score en SRAM, posible Korobéiniki propio cuando exista un PSG decente.

## Por qué existe

Porque la Game Gear tenía **el VDP del Master System** (el mismo, literalmente) y la practica totalidad del catálogo que le llegó después del 92 parecía programada a propósito para no sacarle ni el 40% de su capacidad. Aún hoy puedes coger cualquier cartucho oficial de la última hornada y ver tiles monocromos en backgrounds que podrían tener tres paletas corriendo. Nos merecíamos más.

Porque SDCC es gratis, devkitSMS es gratis, Kega Fusion es gratis, Gearsystem es gratis, y los emuladores te arrancan el `.gg` en dos segundos sin pedirte una cuenta, una contraseña, ni que aceptes cuarenta y siete páginas de términos que te ceden tu primer hijo varón.

Porque una generación entera creció pensando que el Tetris se juega en cuatro tonos de verde biliar y que eso era "el aspecto puro" del juego cuando en realidad era **la única opción que tenía Nintendo en 1989 con su hardware**. Y a esa generación hay que devolverle los colores.

Y porque me apetecía.

## Estructura del repo

```
TETRISGG/
├── README.md           Esto mismo
├── tetrisgg/           Código del juego
│   ├── Makefile.gg
│   ├── build.sh        Wrapper a make de msys64
│   ├── main.c          Todo en un archivo, como los dioses mandan
│   └── build/          Intermedios (.rel, .ihx, .asm, .map…)
├── ROM/
│   ├── tetrisgg.gg                          Última build, la que cargas
│   └── tetrisgg_YYYYMMDD_HHMMSS.gg          Histórico (últimas 5)
├── devkitSMS/          Toolchain (submódulo, se clona aparte)
├── PSGlib/             Herramientas VGM→PSG + lib (submódulo, aparte)
└── Gearsystem/         Emulador opcional (debug multiplataforma)
```

## Prerequisitos

- **SDCC ≥ 4.5** (`sdcc` en el PATH)
- **GNU make** — en Windows sirve el de msys64 (`/c/msys64/usr/bin/make.exe`)
- **devkitSMS** clonado al mismo nivel (ver sección siguiente)
- Un emulador Game Gear: **Kega Fusion** (Windows), **Gearsystem** (multiplataforma) o cualquier otro que acepte `.gg`

## Setup

```bash
# En el directorio padre donde quieras el proyecto:
git clone https://github.com/antxiko/TetrisGG.git
cd TetrisGG
git clone https://github.com/sverx/devkitSMS.git
git clone https://github.com/sverx/PSGlib.git       # herramientas VGM→PSG

# Rebuild único de SMSlib, crt0 y PSGlib para alinear con SDCC 4.5:
cd devkitSMS/SMSlib/src && make && cd ../../..
cd devkitSMS/crt0/src   && make && cd ../../..
cd devkitSMS/PSGlib/src && make && cd ../../..
```

> **Por qué el rebuild:** las libs precompiladas de devkitSMS fueron generadas con SDCC < 4.5. Desde la 4.5 el calling convention por defecto es `sdcccall(1)` y el linker se queja del desajuste. El rebuild local las compila con tu SDCC y deja de quejarse. Es cosa de una vez.

## Compilar

```bash
cd tetrisgg
/c/msys64/usr/bin/make.exe -f Makefile.gg
# o:
./build.sh
```

La ROM sale en `../ROM/tetrisgg.gg`. Cada build guarda también una copia con timestamp y se mantienen solo las 5 más recientes, por higiene.

## Ejecutar

```bash
# Kega Fusion (Windows)
"Fusion.exe" "../ROM/tetrisgg.gg"

# Gearsystem (multiplataforma)
gearsystem ../ROM/tetrisgg.gg
```

## Controles

| Tecla GG       | Kega default | Acción                                          |
|----------------|--------------|-------------------------------------------------|
| D-pad ←/→      | Flechas      | Mover pieza (tap + auto-repeat con DAS/ARR)     |
| D-pad ↓        | ↓            | Soft drop (60 celdas/seg, +1 pt/celda)          |
| D-pad ↑        | ↑            | Hard drop (instantáneo, +2 pts/celda)           |
| Botón 1        | `A`          | Rotar CW                                        |
| Botón 2        | `S`          | Rotar CCW                                       |
| Start          | Enter        | Iniciar partida / Pausar / Reanudar             |

## Detallitos técnicos que merecen acta

- La ventana visible del LCD (160×144 = 20×18 tiles) es una **ventana centrada** dentro del name-table 32×28 del VDP del Master System. Hay un offset `(6, 3)` en coords de tile. Todo el pintado del juego pasa por `screen_set(sx, sy, tile)`, que aplica el offset internamente. Si pintas a pelo en `nt_set(0,0)`, tu primera columna sale a comer plátanos fuera de pantalla.
- Shadow buffer en RAM (`unsigned char shadow[28][32]`) con bitmap de celdas sucias. En cada VBlank se vuelcan como máximo 64 celdas. Así el juego nunca intenta escribir más VRAM de la que cabe en la ventana de VBlank, ni siquiera durante animaciones grandes (line clear, repaints completos). Lo sobrante queda en cola y sale al siguiente frame.
- Los 7 tiles de pieza se **generan en RAM al arrancar** combinando highlight + color principal + shadow. Cada pieza tiene su color pero comparten la silueta 3D. Cambiar paleta o añadir pieza = unas líneas, no 7×32 bytes a pico y pala.
- Los labels del HUD son cápsulas de 3 tiles: `[L_cap][texto invertido × N][R_cap]`. Las tapas tienen las esquinas redondeadas pintadas en pixeles y se apoyan en una segunda fuente con texto oscuro sobre crema.

## FAQ de defensas que voy a cortar de raíz

**— Pero el Tetris se "siente" mejor en Game Boy. Es más puro.**
"Puro" es una palabra que usa la gente cuando le quitan algo y le han convencido de que la carencia es una virtud. El Tetris en Pong-verde no es puro: es que era lo único que la DMG podía sacar. Si te dieran a elegir hoy, en blanco sobre una consola ciega, entre "Tetris puro" o "Tetris con los siete colores que Pázhitnov vio en su cabeza", no lo elegirías a oscuras.

**— La Game Boy vendió 118 millones. La Game Gear 10. Caso cerrado.**
Vender más no te hace mejor consola, te hace mejor producto. Y vender más en su momento no te da un derecho retroactivo al canon. El Virtual Boy vendió más que el Neo Geo Pocket Color. ¿Quieres defender eso también?

**— La Game Gear era un fracaso comercial.**
La Game Gear vendió **11 millones** de unidades. Es más que toda la familia Dreamcast. Es más que la TurboGrafx-16 portátil, el Lynx y el Neo Geo Pocket Color sumados. La cuentas como "fracaso" porque la comparas con la GB, que fue un fenómeno cultural con tres años de ventaja y bundle de Tetris. Comparado con cualquier otro estándar, la GG fue un éxito sólido que vivió seis años.

**— El multiplayer con cable link del GB Tetris era único.**
Era único porque la Game Gear tenía el suyo (Gear-to-Gear Link Cable) y nadie lo usó para hacer un Tetris competitivo. Fallo de software, no de hardware. Es exactamente lo que este repo demuestra.

**— Los colores distraen del gameplay abstracto del Tetris.**
Dile eso a la versión del Tetris **que jugabas en la NES, la Game Boy Color, la Mega Drive, el arcade, el Virtual Boy y todas las reediciones modernas**. Nadie de los que firma esto consideró que el color distrajese. El único Tetris oficial monocromo fue el GB DMG, y lo fue por obligación técnica, no por criterio de diseño.

**— La música del GB Tetris es icónica, ningún chip PSG la iguualará.**
La música del GB Tetris es Korobéiniki, una canción folk rusa de **1861**. No la compuso Hirokazu Tanaka, la arregló. El mismo arreglo en el SN76489 de la Game Gear suena prácticamente idéntico (mismo tipo de chip de tono cuadrado). Lo que es icónico es la canción, no el tono.

**— Pero la Game Gear se comía las pilas.**
Sí, y el Tetris se inventó en una máquina que pesaba 30 kilos y tenía 24KB de RAM. Ningún dispositivo que haya existido es perfecto. Este repo no reescribe la historia del hardware de la Game Gear. Reescribe la del software.

**— La Game Gear murió por algo. El mercado ya eligió.**
El mercado eligió también VHS sobre Betamax, Facebook sobre Google+, y el QWERTY sobre el Dvorak. El mercado elige una vez, se queda con lo que tiene a mano, y no vuelve a revisar la decisión. Revisarla es, literalmente, para lo que sirven los homebrews.

## Specs, por si alguien sigue en duda

```
                         Game Boy (1989)        Game Gear (1990)
CPU                      Sharp LR35902 @ 4MHz   Zilog Z80 @ 3.58MHz
                         (Z80-like custom)      (Z80 de verdad)
RAM                      8 KB                   8 KB
VRAM                     8 KB                   16 KB       ← el doble
Resolución               160×144                160×144     ← LCD igual
Colores simultáneos      4 (verdes)             32
Paleta total             4 (verdes)             4096
Sprites en pantalla      40                     64          ← +24
Sprites por scanline     10                     8
Canales de sonido PSG    2 tono + 1 onda + 1 ruido
                                                3 tono + 1 ruido (SN76489)
Retroiluminación         NO                     SÍ
Cable link               sí                     sí
Autonomía con pilas AA   15-30 h (con 4 AA)     3-5 h (con 6 AA)
Peso                     220 g                  400 g
Años de soporte oficial  14                     7
```

La Game Boy gana en **autonomía, peso y catálogo**. La Game Gear gana en **todo lo demás, incluido lo único que un videojuego necesita: poder mostrarse**.

## Manifiesto

Este repo no es una protesta. Es una corrección. El Tetris, el juego universal escrito por un matemático soviético en una máquina que apenas tenía RAM, merecía salir en la mejor portátil disponible en 1990. Salió en la segunda mejor por razones de negocio, y la historia lo canonizó ahí.

Hoy, a dos décadas de distancia, con compiladores gratuitos y emuladores de un solo click, es sencillamente **razonable** llevarlo a donde correspondía. No lo hago contra Nintendo. Lo hago contra la idea de que lo que fue, tuvo que ser, y ya no puede cambiarse.

Siempre puede cambiarse.

## Licencia

Código bajo **MIT** (pondré los headers cuando no sean las 1am). `devkitSMS` tiene la suya propia, ver su repo.

---

_No tengo nada contra Nintendo. Tengo algo contra que el Tetris sea identificado para siempre con una consola que necesitaba un flexo para jugar de noche._
