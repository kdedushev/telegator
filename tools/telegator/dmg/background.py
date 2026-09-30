"""Фон окна установщика Telegator для macOS: стрелка «в Программы» и первый запуск.

    background.py <out.png>   — рисует 640×500 и @2x (out@2x.png), dmgbuild
                                собирает их в один TIFF.
Координаты значков — в settings.py рядом; менять вместе.
"""

import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 640, 500
FONT = "/System/Library/Fonts/SFNS.ttf"
BG_TOP, BG_BOTTOM = (246, 248, 252), (228, 235, 246)
INK, SUB, ACCENT = (28, 32, 40), (96, 104, 118), (42, 140, 235)


def font(size: float, weight: str, scale: int) -> ImageFont.FreeTypeFont:
    result = ImageFont.truetype(FONT, int(size * scale))
    try:
        result.set_variation_by_name(weight)
    except (OSError, ValueError):
        pass
    return result


def draw(scale: int) -> Image.Image:
    s = scale
    image = Image.new("RGB", (W * s, H * s))
    pixels = ImageDraw.Draw(image)
    for y in range(H * s):
        t = y / (H * s - 1)
        pixels.line([(0, y), (W * s, y)], fill=tuple(
            round(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)))

    def centered(text, y, size, weight, fill):
        f = font(size, weight, s)
        width = pixels.textlength(text, font=f)
        pixels.text(((W * s - width) / 2, y * s), text, font=f, fill=fill)

    centered("Установка Telegator", 28, 24, "Bold", INK)
    centered("Перетащите значок в папку «Программы»", 62, 14, "Regular", SUB)

    # Стрелка между значками (центры значков — 170 и 470 по x, 190 по y).
    y = 190 * s
    pixels.line([(250 * s, y), (382 * s, y)], fill=ACCENT, width=5 * s)
    pixels.polygon([(394 * s, y), (376 * s, y - 13 * s), (376 * s, y + 13 * s)], fill=ACCENT)

    # Карточка первого запуска.
    box = (40 * s, 300 * s, (W - 40) * s, 412 * s)
    pixels.rounded_rectangle(box, radius=14 * s, fill=(255, 255, 255), outline=(214, 222, 234), width=s)
    left = 60
    f_head, f_text = font(13, "Semibold", s), font(12.5, "Regular", s)
    pixels.text((left * s, 316 * s), "Первый запуск", font=f_head, fill=INK)
    lines = (
        "Если macOS пишет, что не может проверить программу, нажмите «Готово»,",
        "затем: Системные настройки → Конфиденциальность и безопасность →",
        "внизу «Всё равно открыть». Это нужно сделать только один раз.",
    )
    for i, line in enumerate(lines):
        pixels.text((left * s, (340 + i * 20) * s), line, font=f_text, fill=SUB)
    return image


if __name__ == "__main__":
    out = sys.argv[1]
    draw(1).save(out)
    draw(2).save(out.replace(".png", "@2x.png"))
