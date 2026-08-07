#!/bin/bash
set -euo pipefail

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RADIO_SRC_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TRANSLATIONS_DIR="${RADIO_SRC_DIR}/translations/i18n"

SYMBOLS_FONT="${RADIO_SRC_DIR}/thirdparty/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
# test if realpath supports --relative-to as macOS BSD version doesn't
if realpath --relative-to="." "." >/dev/null 2>&1; then
    # GNU realpath with --relative-to support
    SYMBOLS_FONT_REL="$(realpath --relative-to="${SCRIPT_DIR}" "${SYMBOLS_FONT}")"
else
    # BSD realpath or no realpath - use hardcoded relative path
    SYMBOLS_FONT_REL="../../thirdparty/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
fi
SYMBOLS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

EXTRA_FONT="EdgeTX/extra.ttf"
EXTRA_SYM="0x88-0x96"

# https://yeun.github.io/open-arrow/
ARROWS_FONT="EdgeTX/OpenArrow-Regular.woff"
# 0x80: right, 0x81: left, 0x82: up, 0x83: down
ARROWS="0x21E8=>0x80,0x21E6=>0x81,0x21E7=>0x82,0x21E9=>0x83"

LATIN_FONT="Roboto/Roboto-Regular.ttf"
LATIN_FONT_BOLD="Roboto/Roboto-Bold.ttf"

ORBITRON_FONT="Orbitron/Orbitron-Regular.ttf"
ORBITRON_FONT_BOLD="Orbitron/Orbitron-Bold.ttf"
LXGW_FONT="LXGWNeoXiHei/LXGWNeoXiHei.ttf"  # No separate bold variant
SMILEYSANS_FONT="SmileySans/SmileySans-Oblique.ttf"  # Oblique only, no separate bold

ASCII="0x20-0x7F"
DEGREE="0xB0"
BULLET="0x2022"
LATIN1_SUPPLEMENT="0xC0-0xFF"
LATIN1_EXT_A="0x100-0x17F"
LATIN1="${LATIN1_SUPPLEMENT},${LATIN1_EXT_A}"
COMPARE="0x2265"

# LV_SYMBOL_CHARGE, LV_SYMBOL_NEW_LINE, LV_SYMBOL_SD_CARD, LV_SYMBOL_CLOSE
# LV_SYMBOL_FILE, LV_SYMBOL_OK, LV_SYMBOL_WIFI, LV_SYMBOL_USB
BL_SYMBOLS="61671,63650,63426,61453,61787,61452,61931,62087"

check_dependencies() {
    # Check if lv_font_conv is available
    if ! command -v lv_font_conv >/dev/null 2>&1; then
        echo "ERROR: lv_font_conv not found. Please install it from https://github.com/lvgl/lv_font_conv or npm registry" >&2
        exit 1
    fi
    
    # Check if we're on macOS and provide helpful info about realpath
    if [[ "$(uname)" == "Darwin" ]]; then
        if ! realpath --relative-to="." "." >/dev/null 2>&1; then
            echo "INFO: Using BSD realpath (no --relative-to support). Using hardcoded relative paths." >&2
            echo "INFO: To install GNU coreutils (optional): brew install coreutils" >&2
        fi
    fi
}

get_translation_symbols() {
    TW_SYMBOLS=$(python3 get_char_ck.py "${TRANSLATIONS_DIR}/tw.h" 2>/dev/null || echo "")
    CN_SYMBOLS=$(python3 get_char_ck.py "${TRANSLATIONS_DIR}/cn.h" 2>/dev/null || echo "")
    JP_SYMBOLS=$(python3 get_char_jp.py "${TRANSLATIONS_DIR}/jp.h" 2>/dev/null || echo "")
    HE_SYMBOLS=$(python3 get_char_he.py "${TRANSLATIONS_DIR}/he.h" 2>/dev/null || echo "")
    KO_SYMBOLS=$(python3 get_char_ko.py "${TRANSLATIONS_DIR}/ko.h" 2>/dev/null || echo "")
    RU_SYMBOLS=$(python3 get_char_cyrillic.py "${TRANSLATIONS_DIR}/ru.h" 2>/dev/null || echo "")
    UA_SYMBOLS=$(python3 get_char_cyrillic.py "${TRANSLATIONS_DIR}/ua.h" 2>/dev/null || echo "")

    # Export variables for later use
    export TW_SYMBOLS CN_SYMBOLS JP_SYMBOLS HE_SYMBOLS KO_SYMBOLS RU_SYMBOLS UA_SYMBOLS
}

function compress_font() {
  local name=$1
  local no_kern=$2

  # Compile the compression tool
  local gcc_cmd="gcc -I \"${RADIO_SRC_DIR}/thirdparty\""
  if [[ -n "${no_kern}" ]]; then
    gcc_cmd="${gcc_cmd} ${no_kern}"
  fi
  gcc_cmd="${gcc_cmd} \"${SCRIPT_DIR}/lz4_font.cpp\" \"${RADIO_SRC_DIR}/thirdparty/lz4/lz4hc.c\" \"${RADIO_SRC_DIR}/thirdparty/lz4/lz4.c\" -o \"${SCRIPT_DIR}/lz4_font\""
  eval "${gcc_cmd}"
  
  "${SCRIPT_DIR}/lz4_font" "${name}"
}

function make_bootloader_font() {
  local name=$1
  local ttf=$2
  local size=$3
  local dir=$4

  echo "Creating bootloader font: ${name} (size: ${size})"
  
  lv_font_conv --no-prefilter --bpp 1 --size "${size}" --no-compress \
               --font "../${ttf}" -r "${ASCII}" \
               --font "${SYMBOLS_FONT_REL}" -r "${BL_SYMBOLS}" \
               --format lvgl -o "${dir}/lv_font_${name}.c" --force-fast-kern-format --no-compress
}

function make_font_no_sym_no_trans() {
  local name=$1
  local latin_ttf=$2
  local size=$3
  local sfx=$4
  local dir=$5

  echo "Creating basic font: ${name}_${sfx} (size: ${size})"
  
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${latin_ttf}" -r "${ASCII},${DEGREE}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" ""
}

function make_en_font() {
  local name=$1
  local latin_ttf=$2
  local size=$3
  local sfx=$4
  local dir=$5

  echo "Creating EN font: ${name}_${sfx} (size: ${size})"
  
  # Use relative paths for lv_font_conv to avoid absolute paths in generated files
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${latin_ttf}" -r "${ASCII},${DEGREE},${BULLET},${COMPARE},${LATIN1}" \
               --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
               --font "${ARROWS_FONT}" -r "${ARROWS}" \
               --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
               --format lvgl -o "${dir}/lv_font_${name}_${sfx}.c" --force-fast-kern-format --no-compress
}

function make_en_font_lz4() {
  local name=$1
  local latin_ttf=$2
  local size=$3
  local sfx=$4
  local dir=$5

  echo "Creating EN compressed font: ${name}_${sfx} (size: ${size})"
  
  # Use relative paths for lv_font_conv
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${latin_ttf}" -r "${ASCII},${DEGREE},${BULLET},${COMPARE},${LATIN1}" \
               --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
               --font "${ARROWS_FONT}" -r "${ARROWS}" \
               --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" ""
}

function make_en_font_w_extra_sym() {
  local name=$1
  local latin_ttf=$2
  local size=$3
  local sfx=$4
  local dir=$5

  echo "Creating EN compressed font with extra symbols only: ${name}_${sfx} (size: ${size})"
  
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${latin_ttf}" -r "${ASCII},${DEGREE},${LATIN1}" \
               --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" ""
}

function make_en_font_no_sym() {
  local name=$1
  local latin_ttf=$2
  local size=$3
  local sfx=$4
  local dir=$5

  echo "Creating EN compressed font without symbols: ${name}_${sfx} (size: ${size})"
  
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${latin_ttf}" -r "${ASCII},${DEGREE},${LATIN1}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" ""
}

function make_font() {
  local name=$1
  local latin_ttf=$2
  local ttf=$3
  local size=$4
  local sfx=$5
  local dir=$6
  local chars=$7

  echo "Creating font: ${name}_${sfx} (size: ${size})"
  
  # Use relative paths for lv_font_conv to avoid absolute paths in generated files
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${ttf}" -r "${chars}" \
               --format lvgl -o "${dir}/lv_font_${name}_${sfx}.c" --force-fast-kern-format --no-compress --lv-fallback lv_font_en_${sfx}
}

function make_font_lz4() {
  local name=$1
  local latin_ttf=$2
  local ttf=$3
  local size=$4
  local sfx=$5
  local dir=$6
  local chars=$7
  local no_kern=$8

  echo "Creating compressed font: ${name}_${sfx} (size: ${size})"
  
  # Use relative paths for lv_font_conv
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${ttf}" -r "${chars}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" "${no_kern}"
}

function make_font_w_extra_sym() {
  local name=$1
  local latin_ttf=$2
  local ttf=$3
  local size=$4
  local sfx=$5
  local dir=$6
  local chars=$7
  local no_kern=$8

  echo "Creating font with extra symbols: ${name}_${sfx} (size: ${size})"
  
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${ttf}" -r "${chars}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" "${no_kern}"
}

function make_font_no_sym() {
  local name=$1
  local latin_ttf=$2
  local ttf=$3
  local size=$4
  local sfx=$5
  local dir=$6
  local chars=$7
  local no_kern=$8

  echo "Creating font without symbols: ${name}_${sfx} (size: ${size})"
  
  lv_font_conv --no-prefilter --bpp 4 --size "${size}" \
               --font "../${ttf}" -r "${chars}" \
               --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "${dir}/lv_font_${name}_${sfx}" "${no_kern}"
}

function make_en_font_set() {
  local name=$1

  echo "Creating EN font set for: ${name}"

  # Standard LCD fonts (480x272, 480x320, 320x480)
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 9 "XXS" "std"
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 13 "XS" "std"
  make_en_font "${name}" "${LATIN_FONT}" 16 "STD" "std"
  make_en_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" 16 "STD" "std"
  make_en_font_w_extra_sym "${name}" "${LATIN_FONT}" 24 "L" "std"
  make_en_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" 32 "XL" "std"

  # Small LCD fonts (320x240)
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 8 "XXS" "sml"
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 10 "XS" "sml"
  make_en_font "${name}" "${LATIN_FONT}" 13 "STD" "sml"
  make_en_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" 13 "STD" "sml"
  make_en_font_w_extra_sym "${name}" "${LATIN_FONT}" 19 "L" "sml"
  make_en_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" 25 "XL" "sml"

  # Mid LCD fonts (480x320)
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 11 "XXS" "mid"
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 16 "XS" "mid"
  make_en_font "${name}" "${LATIN_FONT}" 20 "STD" "mid"
  make_en_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" 20 "STD" "mid"
  make_en_font_w_extra_sym "${name}" "${LATIN_FONT}" 30 "L" "mid"
  make_en_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" 40 "XL" "mid"

  # Large LCD fonts (800x480)
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 12 "XXS" "lrg"
  make_en_font_lz4 "${name}" "${LATIN_FONT}" 18 "XS" "lrg"
  make_en_font "${name}" "${LATIN_FONT}" 22 "STD" "lrg"
  make_en_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" 22 "STD" "lrg"
  make_en_font_w_extra_sym "${name}" "${LATIN_FONT}" 33 "L" "lrg"
  make_en_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" 44 "XL" "lrg"
}

function make_orbitron_font_set() {
  echo "Creating Orbitron font set (prefix: ob)"

  # XXL fonts for Orbitron
  make_font_no_sym_no_trans "ob_bold" "${ORBITRON_FONT_BOLD}" 64 "XXL" "std"
  make_font_no_sym_no_trans "ob_bold" "${ORBITRON_FONT_BOLD}" 50 "XXL" "sml"
  make_font_no_sym_no_trans "ob_bold" "${ORBITRON_FONT_BOLD}" 76 "XXL" "mid"
  make_font_no_sym_no_trans "ob_bold" "${ORBITRON_FONT_BOLD}" 88 "XXL" "lrg"

  # Orbitron EN font set with Roboto fallback for chars Orbitron lacks
  # (COMPARE U+2265 and extended Latin 0x100-0x17F are taken from Roboto)
  local OB_RANGES="${ASCII},${DEGREE},${BULLET}"
  local OB_FALLBACK_RANGES="${COMPARE},${LATIN1}"

  # std
  lv_font_conv --no-prefilter --bpp 4 --size 9 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "std/lv_font_ob_XXS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 13 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "std/lv_font_ob_XS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 16 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "std/lv_font_ob_STD.c" --force-fast-kern-format --no-compress

  lv_font_conv --no-prefilter --bpp 4 --size 16 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "std/lv_font_ob_bold_STD" ""

  lv_font_conv --no-prefilter --bpp 4 --size 24 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${LATIN1_EXT_A}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "std/lv_font_ob_L" ""

  lv_font_conv --no-prefilter --bpp 4 --size 32 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${LATIN1_EXT_A}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "std/lv_font_ob_bold_XL" ""

  # sml
  lv_font_conv --no-prefilter --bpp 4 --size 8 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "sml/lv_font_ob_XXS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 10 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "sml/lv_font_ob_XS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 13 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "sml/lv_font_ob_STD.c" --force-fast-kern-format --no-compress

  lv_font_conv --no-prefilter --bpp 4 --size 13 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "sml/lv_font_ob_bold_STD" ""

  lv_font_conv --no-prefilter --bpp 4 --size 19 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${LATIN1_EXT_A}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "sml/lv_font_ob_L" ""

  lv_font_conv --no-prefilter --bpp 4 --size 25 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${LATIN1_EXT_A}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "sml/lv_font_ob_bold_XL" ""

  # lrg
  lv_font_conv --no-prefilter --bpp 4 --size 12 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "lrg/lv_font_ob_XXS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 18 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "lrg/lv_font_ob_XS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 22 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lrg/lv_font_ob_STD.c" --force-fast-kern-format --no-compress

  lv_font_conv --no-prefilter --bpp 4 --size 22 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "lrg/lv_font_ob_bold_STD" ""

  lv_font_conv --no-prefilter --bpp 4 --size 33 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${LATIN1_EXT_A}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "lrg/lv_font_ob_L" ""

  lv_font_conv --no-prefilter --bpp 4 --size 44 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${LATIN1_EXT_A}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "lrg/lv_font_ob_bold_XL" ""

  # mid (480x320: XXS=10 XS=14 STD=18 bold_STD=18 L=28 bold_XL=38)
  lv_font_conv --no-prefilter --bpp 4 --size 10 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "mid/lv_font_ob_XXS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 14 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "mid/lv_font_ob_XS" ""

  lv_font_conv --no-prefilter --bpp 4 --size 18 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "mid/lv_font_ob_STD.c" --force-fast-kern-format --no-compress

  lv_font_conv --no-prefilter --bpp 4 --size 18 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${OB_FALLBACK_RANGES}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" --font "${ARROWS_FONT}" -r "${ARROWS}" \
      --font "${SYMBOLS_FONT_REL}" -r "${SYMBOLS}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "mid/lv_font_ob_bold_STD" ""

  lv_font_conv --no-prefilter --bpp 4 --size 28 \
      --font "../${ORBITRON_FONT}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT}" -r "${LATIN1_EXT_A}" \
      --font "${EXTRA_FONT}" -r "${EXTRA_SYM}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "mid/lv_font_ob_L" ""

  lv_font_conv --no-prefilter --bpp 4 --size 38 \
      --font "../${ORBITRON_FONT_BOLD}" -r "${OB_RANGES},${LATIN1_SUPPLEMENT}" \
      --font "../${LATIN_FONT_BOLD}" -r "${LATIN1_EXT_A}" \
      --format lvgl -o "lv_font.inc" --force-fast-kern-format --no-compress
  compress_font "mid/lv_font_ob_bold_XL" ""
}

function make_lxgw_font_set() {
  echo "Creating LXGW NeoXiHei font set (prefix: lxgw)"

  # LXGW NeoXiHei has no separate bold variant; use regular for both.
  # Uses the same simplified Chinese character list as the 'cn' translation.
  make_font_set "lxgw" "${LXGW_FONT}" "${LXGW_FONT}" "${CN_SYMBOLS}" "-DNO_KERN"
}

function make_smileysans_font_set() {
  echo "Creating Smiley Sans font set (prefix: ss)"

  # Smiley Sans (得意黑) has only an oblique variant; use it for both regular/bold.
  # Uses the same simplified Chinese character list as the 'cn' translation.
  make_font_set "ss" "${SMILEYSANS_FONT}" "${SMILEYSANS_FONT}" "${CN_SYMBOLS}" "-DNO_KERN"
}

function make_font_set() {
  local name=$1
  local ttf_normal=$2
  local ttf_bold=$3
  local chars=$4
  local no_kern=$5

  if [[ -z "$chars" ]]; then
    echo "WARNING: No characters found for ${name} font set. Skipping." >&2
    return 0
  fi

  echo "Creating font set for: ${name}"

  # Standard LCD fonts (480x272, 480x320, 320x480)
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 9 "XXS" "std" "${chars}" "${no_kern}"
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 13 "XS" "std" "${chars}" "${no_kern}"
  make_font "${name}" "${LATIN_FONT}" "${ttf_normal}" 16 "STD" "std" "${chars}"
  make_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 16 "STD" "std" "${chars}" "${no_kern}"
  make_font_w_extra_sym "${name}" "${LATIN_FONT}" "${ttf_normal}" 24 "L" "std" "${chars}" "${no_kern}"
  make_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 32 "XL" "std" "${chars}" "${no_kern}"

  # Small LCD fonts (320x240)
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 8 "XXS" "sml" "${chars}" "${no_kern}"
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 10 "XS" "sml" "${chars}" "${no_kern}"
  make_font "${name}" "${LATIN_FONT}" "${ttf_normal}" 13 "STD" "sml" "${chars}"
  make_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 13 "STD" "sml" "${chars}" "${no_kern}"
  make_font_w_extra_sym "${name}" "${LATIN_FONT}" "${ttf_normal}" 19 "L" "sml" "${chars}" "${no_kern}"
  make_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 25 "XL" "sml" "${chars}" "${no_kern}"

  # Mid LCD fonts (480x320)
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 11 "XXS" "mid" "${chars}" "${no_kern}"
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 16 "XS" "mid" "${chars}" "${no_kern}"
  make_font "${name}" "${LATIN_FONT}" "${ttf_normal}" 20 "STD" "mid" "${chars}"
  make_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 20 "STD" "mid" "${chars}" "${no_kern}"
  make_font_w_extra_sym "${name}" "${LATIN_FONT}" "${ttf_normal}" 30 "L" "mid" "${chars}" "${no_kern}"
  make_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 40 "XL" "mid" "${chars}" "${no_kern}"

  # Large LCD fonts (800x480)
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 12 "XXS" "lrg" "${chars}" "${no_kern}"
  make_font_lz4 "${name}" "${LATIN_FONT}" "${ttf_normal}" 18 "XS" "lrg" "${chars}" "${no_kern}"
  make_font "${name}" "${LATIN_FONT}" "${ttf_normal}" 22 "STD" "lrg" "${chars}"
  make_font_lz4 "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 22 "STD" "lrg" "${chars}" "${no_kern}"
  make_font_w_extra_sym "${name}" "${LATIN_FONT}" "${ttf_normal}" 33 "L" "lrg" "${chars}" "${no_kern}"
  make_font_no_sym "${name}_bold" "${LATIN_FONT_BOLD}" "${ttf_bold}" 44 "XL" "lrg" "${chars}" "${no_kern}"
}

# Main execution starts here
main() {
    echo "Starting font generation..."

    # Parse arguments
    local generate_orbitron=false
    local generate_lxgw=false
    local generate_smileysans=false
    for arg in "$@"; do
        case "$arg" in
            --orbitron)    generate_orbitron=true ;;
            --lxgw)        generate_lxgw=true ;;
            --smileysans)  generate_smileysans=true ;;
            --all)         generate_orbitron=true; generate_lxgw=true; generate_smileysans=true ;;
        esac
    done

    # Change to script directory for python and other scripts to work correctly
    cd "${SCRIPT_DIR}"

    # Check dependencies and setup
    check_dependencies
    get_translation_symbols

    # Bootloader fonts
    # echo "Generating bootloader fonts..."
    make_bootloader_font "bl" "Roboto/Roboto-Regular-BL.ttf" 16 "std" # 480x272
    make_bootloader_font "bl" "Roboto/Roboto-Regular-BL.ttf" 14 "sml" # 320x240
    make_bootloader_font "bl" "Roboto/Roboto-Regular-BL.ttf" 20 "mid" # 480x320
    make_bootloader_font "bl" "Roboto/Roboto-Regular-BL.ttf" 24 "lrg" # 800x480

    # LXL fonts (no translation chars) - twice size of L, bold
    # echo "Generating LXL fonts..."
    make_font_no_sym_no_trans "en_bold" "${LATIN_FONT_BOLD}" 48 "LXL" "std"
    make_font_no_sym_no_trans "en_bold" "${LATIN_FONT_BOLD}" 38 "LXL" "sml"
    make_font_no_sym_no_trans "en_bold" "${LATIN_FONT_BOLD}" 66 "LXL" "lrg"

    # XXL fonts (no translation chars) - twice size of XL, bold
    # echo "Generating XXL fonts..."
    make_font_no_sym_no_trans "en_bold" "${LATIN_FONT_BOLD}" 64 "XXL" "std"
    make_font_no_sym_no_trans "en_bold" "${LATIN_FONT_BOLD}" 50 "XXL" "sml"
    make_font_no_sym_no_trans "en_bold" "${LATIN_FONT_BOLD}" 76 "XXL" "mid"

    # Language fonts
    echo "Generating language font sets..."
    make_en_font_set "en"
    make_font_set "tw" "Noto/NotoSansCJKsc-Regular.otf" "Noto/NotoSansCJKsc-Bold.otf" "${TW_SYMBOLS}" "-DNO_KERN"
    make_font_set "cn" "Noto/NotoSansCJKsc-Regular.otf" "Noto/NotoSansCJKsc-Bold.otf" "${CN_SYMBOLS}" "-DNO_KERN"
    make_font_set "jp" "Noto/NotoSansCJKsc-Regular.otf" "Noto/NotoSansCJKsc-Bold.otf" "${JP_SYMBOLS}" ""
    make_font_set "he" "Arimo/Arimo-Regular.ttf" "Arimo/Arimo-Bold.ttf" "${HE_SYMBOLS}" "-DNO_KERN"
    make_font_set "ru" "Arimo/Arimo-Regular.ttf" "Arimo/Arimo-Bold.ttf" "${RU_SYMBOLS}" ""
    make_font_set "ua" "Arimo/Arimo-Regular.ttf" "Arimo/Arimo-Bold.ttf" "${UA_SYMBOLS}" ""
    make_font_set "ko" "Nanum/NanumBarunpenR.ttf" "Nanum/NanumBarunpenB.ttf" "${KO_SYMBOLS}" "-DNO_KERN"

    # Orbitron font set (optional, pass --orbitron or --all to generate)
    if [[ "${generate_orbitron}" == "true" ]]; then
        echo "Generating Orbitron font set..."
        make_orbitron_font_set
    fi

    # LXGW NeoXiHei font set (optional, pass --lxgw or --all to generate)
    if [[ "${generate_lxgw}" == "true" ]]; then
        echo "Generating LXGW NeoXiHei font set..."
        make_lxgw_font_set
    fi

    # Smiley Sans font set (optional, pass --smileysans or --all to generate)
    if [[ "${generate_smileysans}" == "true" ]]; then
        echo "Generating Smiley Sans font set..."
        make_smileysans_font_set
    fi
    rm -f "${SCRIPT_DIR}/lz4_font"
    
    echo "Font generation completed successfully!"
}

# Run main function
main "$@"
