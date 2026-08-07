#
# LVGL fonts
#

if (FONT_TIER)
  set(FONT_DIR ${FONT_TIER})
elseif (BITMAPS_DIR STREQUAL 320x240)
  set(FONT_DIR sml)
elseif (BITMAPS_DIR STREQUAL 800x480)
  set(FONT_DIR lrg)
else()
  set(FONT_DIR std)
endif()

file(GLOB LVGL_FONTS
  CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/..
  lvgl/${FONT_DIR}/lv_font_*.c)

# Filter CN font family: only compile the selected variant, exclude others
if(FONT_CN_FAMILY STREQUAL "LXGW")
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_cn_")
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_ss_")
  message(STATUS "Font filter: CN=LXGW (excluded Noto cn_ and SmileySans ss_)")
elseif(FONT_CN_FAMILY STREQUAL "SmileySans")
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_cn_")
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_lxgw_")
  message(STATUS "Font filter: CN=SmileySans (excluded Noto cn_ and LXGW lxgw_)")
else()
  # Default (Noto): exclude LXGW and Smiley Sans variants
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_lxgw_")
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_ss_")
  message(STATUS "Font filter: CN=Noto/default (excluded lxgw_ and ss_)")
endif()

# Filter Latin font family: only compile the selected variant, exclude others
# Note: lv_font_en_bold_LXL is always required regardless of font family
if(FONT_FAMILY STREQUAL "Orbitron")
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_en_")
  # lv_font_en_bold_LXL is always needed (used unconditionally in fonts.cpp)
  list(APPEND LVGL_FONTS "fonts/lvgl/${FONT_DIR}/lv_font_en_bold_LXL.c")
  message(STATUS "Font filter: EN=Orbitron (excluded en_, kept en_bold_LXL)")
else()
  # Default (Roboto): exclude Orbitron variant
  list(FILTER LVGL_FONTS EXCLUDE REGEX "lv_font_ob_")
  message(STATUS "Font filter: EN=Roboto/default (excluded ob_)")
endif()

set(LVGL_FONT_SOURCES ${LVGL_FONTS} PARENT_SCOPE)

set(LVGL_FONT_SOURCES_MINIMAL
  fonts/lvgl/${FONT_DIR}/lv_font_bl.c
  PARENT_SCOPE
)

