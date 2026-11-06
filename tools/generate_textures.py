#!/usr/bin/env python3
"""Процедурная генерация текстур для лабораторной работы №3.

Создаёт в папке textures/ PNG-файлы: альбедо (основной цвет), specular-карты
(где поверхность блестит) и emissive-карты (где поверхность светится сама).
Все текстуры бесшовные (тайлящиеся): шум Перлина строится на периодической
решётке, поэтому при режиме адресации REPEAT швов не видно.

Запуск из корня проекта:  python3 tools/generate_textures.py
Нужны пакеты numpy и pillow.
"""

import os

import numpy as np
from PIL import Image

OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "textures")


# ---------------------------------------------------------------------------
# Шум
# ---------------------------------------------------------------------------

def perlin(height, width, res_y, res_x, rng):
    """Градиентный шум Перлина размером height x width, период res_y x res_x ячеек.

    Градиенты на краях решётки совпадают (берутся по модулю), поэтому шум тайлится.
    Значения примерно в [-0.7, 0.7].
    """
    angles = rng.uniform(0.0, 2.0 * np.pi, (res_y, res_x))
    gradients = np.stack([np.cos(angles), np.sin(angles)], axis=-1)

    ys = np.arange(height) * res_y / height
    xs = np.arange(width) * res_x / width
    y, x = np.meshgrid(ys, xs, indexing="ij")

    y0 = np.floor(y).astype(int)
    x0 = np.floor(x).astype(int)
    fy = y - y0
    fx = x - x0

    def grad(iy, ix):
        return gradients[iy % res_y, ix % res_x]

    def dot(g, dy, dx):
        return g[..., 0] * dx + g[..., 1] * dy

    n00 = dot(grad(y0, x0), fy, fx)
    n10 = dot(grad(y0 + 1, x0), fy - 1, fx)
    n01 = dot(grad(y0, x0 + 1), fy, fx - 1)
    n11 = dot(grad(y0 + 1, x0 + 1), fy - 1, fx - 1)

    # Сглаживающая кривая 6t^5 - 15t^4 + 10t^3 (улучшенный шум Перлина)
    def fade(t):
        return t * t * t * (t * (t * 6 - 15) + 10)

    uy = fade(fy)
    ux = fade(fx)
    nx0 = n00 * (1 - ux) + n01 * ux
    nx1 = n10 * (1 - ux) + n11 * ux
    return nx0 * (1 - uy) + nx1 * uy


def fbm(height, width, res_y, res_x, octaves, rng, persistence=0.5):
    """Фрактальный шум: сумма октав шума Перлина с удвоением частоты. Результат в [0, 1]."""
    total = np.zeros((height, width))
    amplitude = 1.0
    norm = 0.0
    for i in range(octaves):
        scale = 2 ** i
        total += amplitude * perlin(height, width, res_y * scale, res_x * scale, rng)
        norm += amplitude
        amplitude *= persistence
    total /= norm
    return np.clip(total * 1.4 + 0.5, 0.0, 1.0)


def ridged(height, width, res_y, res_x, octaves, rng):
    """«Гребневый» шум: 1 - |perlin|. Тонкие яркие линии там, где шум проходит через ноль —
    выглядят как трещины или прожилки. Результат в [0, 1]."""
    total = np.zeros((height, width))
    amplitude = 1.0
    norm = 0.0
    for i in range(octaves):
        scale = 2 ** i
        n = 1.0 - np.abs(perlin(height, width, res_y * scale, res_x * scale, rng)) * 1.6
        total += amplitude * np.clip(n, 0.0, 1.0)
        norm += amplitude
        amplitude *= 0.5
    return total / norm


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    """Линейная интерполяция цветов: a, b — тройки RGB, t — массив весов."""
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    return a + (b - a) * t[..., None]


def save(name, rgb):
    """Сохраняет массив float [0, 1] формы (h, w, 3) или (h, w) как 8-битный PNG."""
    data = np.clip(rgb, 0.0, 1.0)
    if data.ndim == 2:
        data = np.repeat(data[..., None], 3, axis=-1)
    image = Image.fromarray((data * 255.0 + 0.5).astype(np.uint8), "RGB")
    path = os.path.join(OUT_DIR, name)
    image.save(path, optimize=True)
    print("  ", os.path.relpath(path))


def grid_coords(height, width):
    y, x = np.meshgrid(np.arange(height) / height, np.arange(width) / width, indexing="ij")
    return y, x


# ---------------------------------------------------------------------------
# Материалы
# ---------------------------------------------------------------------------

def bricks(rng, size=512):
    """Кирпичная кладка: 8 кирпичей в ряду, 16 рядов, каждый второй ряд сдвинут на полкирпича."""
    rows, columns = 16, 8
    y, x = grid_coords(size, size)

    row = np.floor(y * rows).astype(int)
    shifted_x = x * columns + 0.5 * (row % 2)
    column = np.floor(shifted_x).astype(int) % columns

    # Положение внутри кирпича в [0, 1]
    local_x = shifted_x - np.floor(shifted_x)
    local_y = y * rows - row

    # Шов: тонкая полоска по краям кирпича (с небольшой неровностью от шума)
    edge_noise = fbm(size, size, 16, 16, 3, rng) * 0.04
    mortar_x = np.minimum(local_x, 1 - local_x) * 2.0  # ширина кирпича вдвое больше высоты
    mortar_y = np.minimum(local_y, 1 - local_y)
    mortar = 1.0 - smoothstep(0.03, 0.07 + edge_noise, np.minimum(mortar_x / columns * rows * 0.5, mortar_y))

    # У каждого кирпича свой оттенок
    brick_id = row * columns + column
    tints = rng.uniform(0.0, 1.0, rows * columns)
    tint = tints[brick_id]

    grain = fbm(size, size, 32, 32, 4, rng)
    base = lerp((0.45, 0.16, 0.10), (0.68, 0.30, 0.18), tint)
    base *= (0.75 + 0.45 * grain)[..., None]
    # Тёмные пятна копоти
    soot = fbm(size, size, 4, 4, 4, rng)
    base *= (1.0 - 0.35 * smoothstep(0.55, 0.8, soot))[..., None]

    mortar_color = lerp((0.55, 0.53, 0.50), (0.70, 0.68, 0.64), grain)
    albedo = base * (1 - mortar[..., None]) + mortar_color * mortar[..., None]

    # Кирпичи чуть-чуть блестят (глазурь местами), шов матовый
    glaze = smoothstep(0.45, 0.75, fbm(size, size, 8, 8, 3, rng))
    specular = (0.08 + 0.35 * glaze) * (1 - mortar)

    save("bricks_albedo.png", albedo)
    save("bricks_specular.png", specular)


def tiles(rng, size=512):
    """Напольная плитка 4 x 4 из светлого камня с прожилками и тёмной затиркой."""
    count = 4
    y, x = grid_coords(size, size)
    local_x = x * count - np.floor(x * count)
    local_y = y * count - np.floor(y * count)
    tile_id = (np.floor(y * count) * count + np.floor(x * count)).astype(int)

    edge = np.minimum(np.minimum(local_x, 1 - local_x), np.minimum(local_y, 1 - local_y))
    grout = 1.0 - smoothstep(0.012, 0.03, edge)

    # Мраморные прожилки: синус от координаты, искажённый шумом
    warp = fbm(size, size, 4, 4, 5, rng)
    veins = np.abs(np.sin(2 * np.pi * (2 * x + 3 * y + 2.0 * warp)))
    veins = 1.0 - 0.35 * (1.0 - veins) ** 6

    tints = rng.uniform(0.0, 1.0, count * count)
    tint = tints[tile_id]
    stone = lerp((0.72, 0.68, 0.62), (0.86, 0.83, 0.78), tint)
    stone *= veins[..., None]

    grout_color = np.array((0.16, 0.15, 0.14))
    albedo = stone * (1 - grout[..., None]) + grout_color * grout[..., None]

    # Полированная плитка блестит, затирка — нет; царапины уменьшают блеск
    scratches = smoothstep(0.6, 0.9, ridged(size, size, 8, 8, 3, rng))
    specular = (0.75 - 0.35 * scratches) * (1 - grout)

    save("tiles_albedo.png", albedo)
    save("tiles_specular.png", specular)


def wood(rng, size=512):
    """Доски с годичными кольцами. Только альбедо: specular и emissive нет —
    программа подставит вместо них служебную чёрную текстуру 1 x 1."""
    y, x = grid_coords(size, size)
    boards = 4
    board = np.floor(y * boards).astype(int)
    offsets = rng.uniform(0.0, 10.0, boards)

    # Шум вытянут вдоль досок (по x всего 2 ячейки): волокна длинные и почти прямые
    warp = fbm(size, size, 8, 2, 4, rng)
    rings = np.sin(2 * np.pi * (5 * (y * boards - board) + 3.0 * warp + offsets[board]))
    rings = 0.5 + 0.5 * rings
    fiber = fbm(size, size, 64, 4, 3, rng)

    albedo = lerp((0.30, 0.16, 0.07), (0.62, 0.40, 0.20), 0.6 * rings + 0.4 * fiber)

    local_y = y * boards - board
    gap = 1.0 - smoothstep(0.0, 0.02, np.minimum(local_y, 1 - local_y))
    albedo *= (1 - 0.8 * gap)[..., None]

    save("wood_albedo.png", albedo)


def marble(rng, size=512):
    """Белый мрамор с мягкими серыми разводами и тонкими золотистыми прожилками, полированный."""
    y, x = grid_coords(size, size)
    warp = fbm(size, size, 2, 2, 6, rng)
    phase = np.sin(2 * np.pi * (x + 2 * y + 2.0 * warp))
    # Широкие мягкие разводы и тонкая прожилка в том же месте
    cloud = (1.0 - np.abs(phase)) ** 3
    vein = (1.0 - np.abs(phase)) ** 40

    base = lerp((0.88, 0.87, 0.85), (0.97, 0.96, 0.94), fbm(size, size, 8, 8, 3, rng))
    albedo = base * (1 - 0.30 * cloud)[..., None]
    gold = np.array((0.70, 0.50, 0.18))
    albedo = albedo * (1 - 0.8 * vein[..., None]) + gold * 0.8 * vein[..., None]

    specular = 0.85 - 0.3 * cloud
    save("marble_albedo.png", albedo)
    save("marble_specular.png", specular)


def scifi(rng, size=512):
    """Металлические панели с неоновыми линиями. Есть все три карты:
    металл блестит (specular), линии и индикаторы светятся (emissive)."""
    y, x = grid_coords(size, size)
    count = 4
    local_x = x * count - np.floor(x * count)
    local_y = y * count - np.floor(y * count)
    panel_id = (np.floor(y * count) * count + np.floor(x * count)).astype(int)

    edge = np.minimum(np.minimum(local_x, 1 - local_x), np.minimum(local_y, 1 - local_y))
    seam = 1.0 - smoothstep(0.01, 0.025, edge)

    # Неоновая полоска чуть внутри края панели
    glow_line = smoothstep(0.012, 0.0, np.abs(edge - 0.06))

    # Заклёпки по углам панели
    rivet = np.zeros_like(x)
    for cx in (0.12, 0.88):
        for cy in (0.12, 0.88):
            d = np.hypot(local_x - cx, local_y - cy)
            rivet = np.maximum(rivet, smoothstep(0.03, 0.02, d))

    tints = rng.uniform(0.0, 1.0, count * count)
    brushed = fbm(size, size, 128, 2, 2, rng)
    metal = lerp((0.30, 0.32, 0.36), (0.48, 0.50, 0.55), 0.5 * tints[panel_id] + 0.5 * brushed)
    albedo = metal * (1 - 0.8 * seam)[..., None]
    albedo = albedo * (1 - rivet[..., None]) + np.array((0.7, 0.72, 0.75)) * rivet[..., None]
    albedo = albedo * (1 - glow_line[..., None]) + np.array((0.1, 0.25, 0.3)) * glow_line[..., None]

    # Индикаторы: у некоторых панелей горит прямоугольник в центре
    lit = rng.uniform(0.0, 1.0, count * count) > 0.4
    hues = np.array([(0.1, 1.0, 0.9), (1.0, 0.3, 0.8), (1.0, 0.7, 0.1)])
    hue = hues[rng.integers(0, len(hues), count * count)]
    box = (np.abs(local_x - 0.5) < 0.18) & (np.abs(local_y - 0.5) < 0.05)
    indicator = (box & lit[panel_id]).astype(float)

    emissive = np.zeros((size, size, 3))
    emissive += np.array((0.1, 0.9, 1.0)) * glow_line[..., None]
    emissive += hue[panel_id] * indicator[..., None]

    specular = (0.55 + 0.35 * brushed) * (1 - seam)
    specular = np.maximum(specular, rivet)

    save("scifi_albedo.png", albedo)
    save("scifi_specular.png", specular)
    save("scifi_emissive.png", emissive)


def lava(rng, width=1024, height=512):
    """Лавовая планета (развёртка для сферы 2:1). Тёмный базальт с трещинами,
    из которых светится лава. Emissive-карта построена из шума Перлина."""
    # Период шума по горизонтали вдвое больше, чтобы ячейки были квадратными
    cracks = ridged(height, width, 4, 8, 5, rng)
    heat = fbm(height, width, 2, 4, 4, rng)

    crack_mask = smoothstep(0.72, 0.9, cracks)
    # Где «горячее», трещины шире и ярче
    crack_mask = np.clip(crack_mask * (0.4 + 1.2 * heat), 0.0, 1.0)

    rock_noise = fbm(height, width, 8, 16, 5, rng)
    rock = lerp((0.06, 0.05, 0.05), (0.22, 0.19, 0.17), rock_noise)
    albedo = rock * (1 - crack_mask[..., None]) + np.array((0.35, 0.08, 0.02)) * crack_mask[..., None]

    # Цвет свечения: от тёмно-красного к жёлтому в самых горячих местах
    glow = crack_mask ** 1.5
    emissive = lerp((0.6, 0.05, 0.0), (1.0, 0.75, 0.2), smoothstep(0.4, 1.0, glow * (0.5 + heat)))
    emissive *= glow[..., None]
    # Мягкое подсвечивание вокруг трещин
    halo = smoothstep(0.55, 0.85, cracks) * heat
    emissive += np.array((0.25, 0.04, 0.0)) * (0.5 * halo)[..., None]

    # Застывшая корка слегка блестит, трещины — нет
    specular = 0.25 * smoothstep(0.4, 0.8, rock_noise) * (1 - crack_mask)

    save("lava_albedo.png", albedo)
    save("lava_specular.png", specular)
    save("lava_emissive.png", emissive)


def stained_glass(rng, size=512):
    """Витраж: разноцветные ячейки Вороного со свинцовыми перегородками.
    Яркая разноцветная картинка — на ней хорошо видно нетривиальное сэмплирование."""
    points = rng.uniform(0.0, 1.0, (48, 2))
    y, x = grid_coords(size, size)

    # Расстояния до точек с учётом периодичности (текстура бесшовная)
    dy = np.abs(y[..., None] - points[:, 0])
    dx = np.abs(x[..., None] - points[:, 1])
    dy = np.minimum(dy, 1 - dy)
    dx = np.minimum(dx, 1 - dx)
    dist = np.sqrt(dx * dx + dy * dy)

    order = np.argsort(dist, axis=-1)
    nearest = order[..., 0]
    d1 = np.take_along_axis(dist, order[..., :1], axis=-1)[..., 0]
    d2 = np.take_along_axis(dist, order[..., 1:2], axis=-1)[..., 0]
    # Расстояние до границы — до серединного перпендикуляра между двумя ближайшими точками:
    # (d2² - d1²) / (2 |p2 - p1|), расстояние между точками тоже периодическое
    second = order[..., 1]
    pdy = np.abs(points[nearest, 0] - points[second, 0])
    pdx = np.abs(points[nearest, 1] - points[second, 1])
    pdy = np.minimum(pdy, 1 - pdy)
    pdx = np.minimum(pdx, 1 - pdx)
    edge = (d2 * d2 - d1 * d1) / (2.0 * np.sqrt(pdx * pdx + pdy * pdy) + 1e-6)
    border = 1.0 - smoothstep(0.003, 0.007, edge)

    # Насыщенные цвета стекла: оттенки по кругу цветов
    hues = rng.uniform(0.0, 1.0, len(points))
    h = hues[nearest]
    rgb = np.stack([
        0.5 + 0.5 * np.cos(2 * np.pi * (h + 0.0)),
        0.5 + 0.5 * np.cos(2 * np.pi * (h + 0.33)),
        0.5 + 0.5 * np.cos(2 * np.pi * (h + 0.67)),
    ], axis=-1)
    rgb = 0.15 + 0.85 * rgb ** 1.5
    # Неоднородность стекла: ярче к центру ячейки
    rgb *= (1.0 - 1.8 * d1)[..., None].clip(0.5, 1.0)
    rgb *= (0.85 + 0.3 * fbm(size, size, 16, 16, 3, rng))[..., None]

    lead = np.array((0.08, 0.08, 0.09))
    albedo = rgb * (1 - border[..., None]) + lead * border[..., None]

    # Стекло блестит сильно, свинец — слабее
    specular = 0.9 * (1 - border) + 0.3 * border
    save("stained_glass_albedo.png", albedo)
    save("stained_glass_specular.png", specular)


def uv_checker(size=512):
    """Отладочная шахматка: удобно проверять текстурные координаты.
    Цвет клетки меняется по u (оттенок) и по v (яркость)."""
    y, x = grid_coords(size, size)
    cells = 8
    checker = ((np.floor(x * cells) + np.floor(y * cells)) % 2).astype(float)
    hue = np.floor(x * cells) / cells
    rgb = np.stack([
        0.5 + 0.5 * np.cos(2 * np.pi * (hue + 0.0)),
        0.5 + 0.5 * np.cos(2 * np.pi * (hue + 0.33)),
        0.5 + 0.5 * np.cos(2 * np.pi * (hue + 0.67)),
    ], axis=-1)
    brightness = 0.35 + 0.65 * (1 - np.floor(y * cells) / cells)
    albedo = rgb * brightness[..., None] * (0.35 + 0.65 * checker)[..., None]
    # Белая рамка вокруг всей текстуры — видно, где она повторяется
    frame = (np.minimum(np.minimum(x, 1 - x), np.minimum(y, 1 - y)) < 0.006).astype(float)
    albedo = albedo * (1 - frame[..., None]) + frame[..., None]
    save("uv_checker_albedo.png", albedo)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("Генерация текстур в", OUT_DIR)
    # Фиксированное зерно — при повторном запуске получаются те же самые картинки
    rng = np.random.default_rng(2026)
    bricks(rng)
    tiles(rng)
    wood(rng)
    marble(rng)
    scifi(rng)
    lava(rng)
    stained_glass(rng)
    uv_checker()


if __name__ == "__main__":
    main()
