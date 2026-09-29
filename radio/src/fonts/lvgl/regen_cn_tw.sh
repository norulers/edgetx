#!/bin/bash
# Regenerate only cn and tw font sets using the same variables/functions
# as make_fonts.sh, but without running all other fonts.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Extract the second complete copy (lines 603-1235) which has all functions
# including make_font_w_extra_sym and make_font_no_sym.
# Place the extracted lib IN this directory so BASH_SOURCE[0] gives the correct
# SCRIPT_DIR when sourced (so RADIO_SRC_DIR is computed correctly).
FONTS_LIB="${SCRIPT_DIR}/.fonts_lib_tmp.sh"
sed -n '603,1235p' "${SCRIPT_DIR}/make_fonts.sh" > "${FONTS_LIB}"

# Source the function definitions - BASH_SOURCE[0] will now point to SCRIPT_DIR
# so all path variables will be computed correctly.
source "${FONTS_LIB}"
rm -f "${FONTS_LIB}"

cd "${SCRIPT_DIR}"

echo "=== Checking dependencies ==="
check_dependencies

echo "=== Loading translation symbols ==="
get_translation_symbols

echo "CN symbols count: $(echo "${CN_SYMBOLS}" | tr ',' '\n' | wc -l)"
echo "TW symbols count: $(echo "${TW_SYMBOLS}" | tr ',' '\n' | wc -l)"

# Verify 返 (U+8FD4) is in CN_SYMBOLS
if echo "${CN_SYMBOLS}" | grep -q '0x8fd4'; then
    echo "GOOD: 0x8fd4 (返) found in CN_SYMBOLS"
else
    echo "WARNING: 0x8fd4 (返) NOT found in CN_SYMBOLS"
fi

echo ""
echo "=== Regenerating CN fonts ==="
make_font_set "cn" "Noto/NotoSansCJKsc-Regular.otf" "Noto/NotoSansCJKsc-Bold.otf" "${CN_SYMBOLS}" "-DNO_KERN"

echo ""
echo "=== Regenerating TW fonts ==="
make_font_set "tw" "Noto/NotoSansCJKsc-Regular.otf" "Noto/NotoSansCJKsc-Bold.otf" "${TW_SYMBOLS}" "-DNO_KERN"

rm -f "${SCRIPT_DIR}/lz4_font"
echo ""
echo "=== Done: cn and tw fonts regenerated ==="
