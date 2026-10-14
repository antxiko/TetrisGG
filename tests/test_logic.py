"""Tests de la lógica de juego y de los datos de música.

No hay emulador en CI: estos tests leen las tablas directamente de los .c
(formas, kicks SRS, secuencias de música) y replican los algoritmos de
main.c (fits / try_rotate / tspin_check) para validar los DATOS reales
que se compilan en la ROM.
"""
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "tetrisgg" / "main.c").read_text(encoding="utf-8")
MUSIC = (ROOT / "tetrisgg" / "music.c").read_text(encoding="utf-8")

NAMES = "IOTSZJL"
W, H = 10, 20


# ------------------------------------------------------------ lectura de tablas
def _shapes():
    body = re.search(r"shapes\[7\]\[4\] = \{(.*?)\n\};", MAIN, re.S).group(1)
    rows = [[int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{4}", ln)]
            for ln in body.splitlines() if "0x" in ln]
    assert len(rows) == 7 and all(len(r) == 4 for r in rows)
    return rows


def _kicks(name):
    body = re.search(name + r"\[8\]\[10\] = \{(.*?)\n\};", MAIN, re.S).group(1)
    rows = [[int(v) for v in re.findall(r"-?\d+", ln)] for ln in body.splitlines() if "{" in ln]
    assert len(rows) == 8 and all(len(r) == 10 for r in rows)
    return rows


SHAPES = _shapes()
KJ, KI = _kicks("kick_jlstz"), _kicks("kick_i")


def cells(t, r):
    s = SHAPES[t][r]
    return {(x, y) for y in range(4) for x in range(4) if (s >> (y * 4 + x)) & 1}


def fits(board, t, r, x, y):
    for lx, ly in cells(t, r):
        bx, by = x + lx, y + ly
        if bx < 0 or bx >= W or by >= H:
            return False
        if by >= 0 and board[by][bx]:
            return False
    return True


def rotate(board, t, r, x, y, cw):
    """Réplica de try_rotate() de main.c."""
    nr = (r + (1 if cw else 3)) & 3
    idx = (r << 1) if cw else (((r << 1) + 7) & 7)
    k = (KI if NAMES[t] == "I" else KJ)[idx]
    for i in range(5):
        nx, ny = x + k[2 * i], y + k[2 * i + 1]
        if fits(board, t, nr, nx, ny):
            return nr, nx, ny, i
    return None


def tspin(board, x, y, r, kick):
    """Réplica de tspin_check() (asumiendo last_rotate=1). 0/1 mini/2 full."""
    cx, cy = [0, 2, 2, 0], [0, 0, 2, 2]

    def blk(a, b):
        if a < 0 or a >= W or b >= H:
            return 1
        if b < 0:
            return 0
        return 1 if board[b][a] else 0

    f = [blk(x + cx[i], y + cy[i]) for i in range(4)]
    if sum(f) < 3:
        return 0
    return 2 if (f[r] and f[(r + 1) & 3]) or kick == 4 else 1


def board_from(rows):
    b = [[0] * W for _ in range(H)]
    for i, row in enumerate(rows):
        for j, ch in enumerate(row):
            if ch == "#":
                b[H - len(rows) + i][j] = 1
    return b


# ------------------------------------------------------------------- formas
@pytest.mark.parametrize("t", [i for i, n in enumerate(NAMES) if n != "O"])
@pytest.mark.parametrize("r", range(4))
def test_estado_siguiente_es_giro_horario(t, r):
    n = 4 if NAMES[t] == "I" else 3          # caja SRS: 4x4 para I, 3x3 resto
    girado = {(n - 1 - y, x) for (x, y) in cells(t, r)}
    assert girado == cells(t, (r + 1) % 4)


@pytest.mark.parametrize("t", range(7))
def test_todas_las_piezas_tienen_4_bloques(t):
    assert all(len(cells(t, r)) == 4 for r in range(4))


def test_kick_cero_primero_y_tablas_simetricas():
    # El primer test de cada transición es sin desplazamiento, y la
    # transición inversa (0>R vs R>0) usa los kicks negados.
    for tab in (KJ, KI):
        for row in tab:
            assert row[0:2] == [0, 0]
        for a, b in [(0, 1), (2, 3), (4, 5), (6, 7)]:
            assert tab[b] == [-v for v in tab[a]]


# ---------------------------------------------------------------- rotación
def test_palo_contra_la_pared_izquierda_puede_girar():
    vacio = [[0] * W for _ in range(H)]
    res = rotate(vacio, NAMES.index("I"), 1, -2, 10, False)   # vertical en col 0
    assert res is not None
    nr, nx, ny, _ = res
    assert min(nx + x for x, _ in cells(0, nr)) >= 0


def test_t_spin_double_alcanzable_y_detectado():
    T = NAMES.index("T")
    b = board_from([
        "###.......",
        "##...#####",
        "###.######",
    ])
    encontrados = []
    for r0 in range(4):
        for x0 in range(-1, 9):
            for y0 in range(10, 19):
                if not fits(b, T, r0, x0, y0) or fits(b, T, r0, x0, y0 + 1):
                    continue
                for cw in (True, False):
                    res = rotate(b, T, r0, x0, y0, cw)
                    if not res:
                        continue
                    nr, nx, ny, k = res
                    bb = [row[:] for row in b]
                    for lx, ly in cells(T, nr):
                        bb[ny + ly][nx + lx] = 1
                    if sum(all(row) for row in bb) == 2 and tspin(b, nx, ny, nr, k) == 2:
                        encontrados.append(res)
    assert encontrados


def test_t_spin_mini_con_una_sola_esquina_frontal():
    # T apuntando arriba (estado 0) en el suelo junto a la pared: esquinas de
    # abajo ocupadas (suelo) + una de arriba (pared) -> 3 esquinas, 1 frontal.
    b = [[0] * W for _ in range(H)]
    assert tspin(b, -1, H - 2, 0, 0) == 1


def test_t_sin_esquinas_no_es_t_spin():
    b = [[0] * W for _ in range(H)]
    assert tspin(b, 4, 5, 0, 0) == 0


# ------------------------------------------------------------------ música
def _defines(src):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (\w+)\s+(\d+)\b", src)}


def _expand(expr, macros):
    # Expande macros sin argumentos y O8/Q4 del bajo, y suma duraciones.
    for _ in range(10):
        for name, body in macros.items():
            expr = re.sub(r"\b%s\b(?!\()" % name, body, expr)
    expr = re.sub(r"O8\(([^)]*)\)", r"\1,1, \1,1, \1,1, \1,1", expr)
    expr = re.sub(r"Q4\(([^)]*)\)", r"\1,2, \1,2", expr)
    return [t.strip() for t in expr.split(",") if t.strip()]


def _stream_eighths(name):
    # Solo macros de secuencia (llevan comas); LOOP, R y las notas se quedan
    macros = {}
    for m in re.finditer(r"#define (\w+) ((?:.*\\\n)*.*)", MUSIC):
        body = re.sub(r"/\*.*?\*/", "", m.group(2).replace("\\\n", " "))
        if "," in body:
            macros[m.group(1)] = body
    body = re.search(r"%s\[\] = \{(.*?)\};" % name, MUSIC, re.S).group(1)
    toks = _expand(body, macros)
    assert toks[-1] == "LOOP"
    durs = toks[:-1][1::2]
    return sum(int(d) for d in durs)


def test_los_cuatro_canales_de_korobeiniki_duran_lo_mismo():
    largos = {n: _stream_eighths(n) for n in ("k_mel", "k_har", "k_bas", "k_drm")}
    # A A B = 24 compases de 8 corcheas
    assert set(largos.values()) == {24 * 8}, largos


def test_notas_de_la_musica_dentro_de_la_tabla():
    d = _defines(MUSIC)
    usadas = [v for k, v in d.items() if re.fullmatch(r"[A-G]S?\d", k)]
    assert usadas and max(usadas) < 52
    tabla = re.search(r"note_counts\[52\] = \{(.*?)\};", MUSIC, re.S).group(1)
    tabla = re.sub(r"/\*.*?\*/", "", tabla)
    cuentas = [int(v) for v in re.findall(r"\d+", tabla)]
    assert len(cuentas) == 52
    # Todas caben en los 10 bits del divisor del PSG y bajan de tono a tono
    assert max(cuentas) <= 1023
    assert all(a > b for a, b in zip(cuentas, cuentas[1:]))
