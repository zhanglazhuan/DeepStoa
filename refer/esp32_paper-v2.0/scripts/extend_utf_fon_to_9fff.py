#!/usr/bin/env python3
"""
Extend legacy UTF .FON files from U+9FA5 to U+9FFF.

The current renderer indexes UTF Chinese glyphs with:
  offset = (unicode - 0x4E00) * glyph_size
and expects coverage up to U+9FFF.

Some bundled .FON files only contain U+4E00..U+9FA5 (20902 glyphs).
This script appends missing glyphs U+9FA6..U+9FFF (90 glyphs) so that
all UTF .FON files match the expected range U+4E00..U+9FFF (20992 glyphs).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Tuple

from PIL import Image, ImageDraw, ImageFont


FIRST_CODEPOINT = 0x4E00
LAST_CODEPOINT = 0x9FFF
TARGET_GLYPH_COUNT = LAST_CODEPOINT - FIRST_CODEPOINT + 1

FONT_SOURCE = Path(
    "managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
)


@dataclass(frozen=True)
class FontSpec:
    name: str
    width: int
    height: int
    path: Path

    @property
    def glyph_bytes(self) -> int:
        return ((self.width + 7) // 8) * self.height


FONT_SPECS = [
    FontSpec(
        name="UTF_font12CH.FON",
        width=16,
        height=21,
        path=Path("components/epaper_lib/Fonts/Font12/UTF_font12CH.FON"),
    ),
    FontSpec(
        name="UTF_font16CH.FON",
        width=24,
        height=28,
        path=Path("components/epaper_lib/Fonts/Font16/UTF_font16CH.FON"),
    ),
    FontSpec(
        name="UTF_font18CH.FON",
        width=24,
        height=31,
        path=Path("components/epaper_lib/Fonts/Font18/UTF_font18CH.FON"),
    ),
    FontSpec(
        name="UTF_font24CH.FON",
        width=32,
        height=41,
        path=Path("components/epaper_lib/Fonts/Font24/UTF_font24CH.FON"),
    ),
    FontSpec(
        name="UTF_font28CH.FON",
        width=40,
        height=48,
        path=Path("components/epaper_lib/Fonts/Font28/UTF_font28CH.FON"),
    ),
    FontSpec(
        name="UTF_font36CH.FON",
        width=48,
        height=62,
        path=Path("components/epaper_lib/Fonts/Font36/UTF_font36CH.FON"),
    ),
    FontSpec(
        name="UTF_font48CH.FON",
        width=64,
        height=83,
        path=Path("components/epaper_lib/Fonts/Font48/UTF_font48CH.FON"),
    ),
]


def get_text_bbox(ch: str, font: ImageFont.FreeTypeFont) -> Tuple[int, int, int, int]:
    img = Image.new("1", (4, 4), 1)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), ch, font=font)
    if bbox is None:
        return (0, 0, 0, 0)
    return bbox


def choose_preferred_size(
    spec: FontSpec, cache: Dict[int, ImageFont.FreeTypeFont], sample_char: str = "国"
) -> int:
    # Start from target height and find the largest size that fits.
    for size in range(spec.height, 3, -1):
        font = cache.get(size)
        if font is None:
            font = ImageFont.truetype(str(FONT_SOURCE), size=size)
            cache[size] = font
        l, t, r, b = get_text_bbox(sample_char, font)
        w = r - l
        h = b - t
        if w <= spec.width - 1 and h <= spec.height - 1:
            return size
    return 4


def render_glyph(
    codepoint: int,
    spec: FontSpec,
    preferred_size: int,
    cache: Dict[int, ImageFont.FreeTypeFont],
) -> bytes:
    ch = chr(codepoint)
    chosen = preferred_size

    # Fallback shrink for unusually tall/wide glyphs.
    while chosen >= 4:
        font = cache.get(chosen)
        if font is None:
            font = ImageFont.truetype(str(FONT_SOURCE), size=chosen)
            cache[chosen] = font
        l, t, r, b = get_text_bbox(ch, font)
        gw = r - l
        gh = b - t
        if gw <= spec.width - 1 and gh <= spec.height - 1:
            break
        chosen -= 1

    if chosen < 4:
        chosen = 4
        font = cache.get(chosen)
        if font is None:
            font = ImageFont.truetype(str(FONT_SOURCE), size=chosen)
            cache[chosen] = font
        l, t, r, b = get_text_bbox(ch, font)
        gw = r - l
        gh = b - t
    else:
        font = cache[chosen]

    img = Image.new("1", (spec.width, spec.height), 1)  # white background -> bit 1
    draw = ImageDraw.Draw(img)

    x = (spec.width - gw) // 2 - l
    y = (spec.height - gh) // 2 - t
    draw.text((x, y), ch, font=font, fill=0)  # black glyph -> bit 0

    bpr = (spec.width + 7) // 8
    out = bytearray(spec.height * bpr)
    pix = img.load()
    idx = 0
    for yy in range(spec.height):
        for bx in range(bpr):
            val = 0
            for bit in range(8):
                xx = bx * 8 + bit
                bit_val = 1
                if xx < spec.width:
                    bit_val = 1 if pix[xx, yy] else 0
                val |= bit_val << (7 - bit)
            out[idx] = val
            idx += 1

    return bytes(out)


def extend_font_file(spec: FontSpec) -> None:
    if not spec.path.exists():
        raise FileNotFoundError(f"Missing font file: {spec.path}")

    raw = spec.path.read_bytes()
    if raw.endswith(b"\r\n"):
        payload = raw[:-2]
    else:
        payload = raw

    if len(payload) % spec.glyph_bytes != 0:
        raise ValueError(
            f"{spec.path}: payload size {len(payload)} is not aligned to "
            f"glyph size {spec.glyph_bytes}"
        )

    current_count = len(payload) // spec.glyph_bytes
    if current_count >= TARGET_GLYPH_COUNT:
        print(
            f"[SKIP] {spec.name}: already has {current_count} glyphs "
            f"(target {TARGET_GLYPH_COUNT})"
        )
        return

    missing = TARGET_GLYPH_COUNT - current_count
    start_cp = FIRST_CODEPOINT + current_count
    end_cp = LAST_CODEPOINT
    print(
        f"[PATCH] {spec.name}: {current_count} -> {TARGET_GLYPH_COUNT} "
        f"(append {missing} glyphs, U+{start_cp:04X}..U+{end_cp:04X})"
    )

    cache: Dict[int, ImageFont.FreeTypeFont] = {}
    preferred = choose_preferred_size(spec, cache)

    out = bytearray(payload)
    for cp in range(start_cp, LAST_CODEPOINT + 1):
        glyph = render_glyph(cp, spec, preferred, cache)
        if len(glyph) != spec.glyph_bytes:
            raise RuntimeError(
                f"Unexpected glyph size for U+{cp:04X}: {len(glyph)} "
                f"(expected {spec.glyph_bytes})"
            )
        out.extend(glyph)

    out.extend(b"\r\n")
    spec.path.write_bytes(out)
    print(f"[DONE] {spec.name}: wrote {len(out)} bytes")


def main() -> int:
    if not FONT_SOURCE.exists():
        raise FileNotFoundError(f"Missing OTF source: {FONT_SOURCE}")

    for spec in FONT_SPECS:
        extend_font_file(spec)

    print("All UTF .FON files are now extended to U+9FFF.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
