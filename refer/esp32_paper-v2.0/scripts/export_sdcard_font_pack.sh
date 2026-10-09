#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-${ROOT_DIR}/build/sdcard_font_pack}"

mkdir -p "${OUT_DIR}/font/ASICC" "${OUT_DIR}/font/GBK" "${OUT_DIR}/font/UTF"

for sz in 12 16 18 24 28 36 48; do
  cp "${ROOT_DIR}/components/epaper_lib/Fonts/Font${sz}/font${sz}EN.FON" \
     "${OUT_DIR}/font/ASICC/font${sz}EN.FON"

  cp "${ROOT_DIR}/components/epaper_lib/Fonts/Font${sz}/GBK_font${sz}CH.FON" \
     "${OUT_DIR}/font/GBK/font${sz}CH.FON"
  cp "${ROOT_DIR}/components/epaper_lib/Fonts/Font${sz}/GBK_font${sz}CH_ASICC.FON" \
     "${OUT_DIR}/font/GBK/font${sz}CH_ASICC.FON"

  cp "${ROOT_DIR}/components/epaper_lib/Fonts/Font${sz}/UTF_font${sz}CH.FON" \
     "${OUT_DIR}/font/UTF/font${sz}CH.FON"
  cp "${ROOT_DIR}/components/epaper_lib/Fonts/Font${sz}/UTF_font${sz}CH_ASICC.FON" \
     "${OUT_DIR}/font/UTF/font${sz}CH_ASICC.FON"
done

cp "${ROOT_DIR}/components/epaper_lib/Fonts/ASCII/font80EN.FON" \
   "${OUT_DIR}/font/ASICC/font80EN.FON"
cp "${ROOT_DIR}/components/epaper_lib/Fonts/ASCII/font182EN.FON" \
   "${OUT_DIR}/font/ASICC/font182EN.FON"

echo "Exported SD card font pack to: ${OUT_DIR}"
