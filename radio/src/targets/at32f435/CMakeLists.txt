# AT32F435 target (new MCU port)
# ============================================================================
# Target configuration for the AT32F435ZMT7 based radio.
# Hardware pin mapping is NOT yet final - see boards/at32f435/PORTING.md.
# ============================================================================

option(UNEXPECTED_SHUTDOWN "Enable the Unexpected Shutdown screen" ON)
option(AFHDS3 "AFHDS3 TX Module" OFF)
option(MULTIMODULE "DIY Multiprotocol TX Module (https://github.com/pascallanger/DIY-Multiprotocol-TX-Module)" ON)
option(GHOST "Ghost TX Module" ON)
option(MODULE_SIZE_STD "Standard size TX Module" ON)
option(LUA_MIXER "Enable LUA mixer/model scripts support" ON)
option(DISK_CACHE "Enable SD card disk cache" ON)
option(INTERNAL_GPS "Support for internal GPS" OFF)

set(FIRMWARE_FORMAT_UF2 NO)                # AT32F435 has 4MB flash: use DFU instead
set(PWR_BUTTON "PRESS" CACHE STRING "Pwr button type (PRESS/SWITCH)")

# MCU / CPU
set(CPU_TYPE AT32F435)
set(CPU_TYPE_FULL AT32F435ZMT7)
set(CPU_FAMILY AT32)
set(MCU cortex-m4)

# Clock (12 MHz HEXT crystal, consistent with the T20V2 board)
set(HSE_VALUE 12000000)

# Storage / GUI
set(SDCARD YES)
# STORAGE_MODELSLIST requires currModelFilename in RadioData, which is only
# present on color-LCD targets. The AT32 target is a monochrome (128x64) OLED,
# so keep this OFF (matching other monochrome targets).
set(STORAGE_MODELSLIST NO)
set(HAPTIC NO)
set(GUI_DIR 128x64)                          # monochrome OLED (T20V2 consistent)
set(BITMAPS_DIR 128x64)
set(LCD_DRIVER lcd_driver_spi.cpp)           # OLED / ST7565-class over SPI
set(TARGET_DIR at32f435)
set(RTC_BACKUP_RAM NO)
set(PPM_LIMITS_SYMETRICAL YES)
# USB serial (CDC) is not yet supported by the AT32 USB library port (MSC only).
set(USB_SERIAL OFF CACHE BOOL "Enable USB serial (CDC)")
set(HARDWARE_EXTERNAL_MODULE YES)
set(INTERNAL_MODULE_AFHDS3 NO)
set(ROTARY_ENCODER YES)
set(FUNCTION_SWITCHES_WITH_RGB NO)
# The AT32 target follows the T20V2 hardware, which has function switches.
# Required for the t20 YAML datastructs (SWITCH_GLOBAL, FUNC_PUSH_CUST_SWITCH).
set(FUNCTION_SWITCHES ON)
add_definitions(-DFUNCTION_SWITCHES)
set(FLEXSW "2" CACHE STRING "Max flex inputs usable as switches")

# Peripherals present on the board (adjust once known)
set(HARDWARE_TOUCH NO)                       # monochrome target: no touch
set(AUX_SERIAL YES)
set(USB_CHARGER YES)

set(FLAVOUR at32f435)
add_definitions(-DRADIO_AT32F435 -DMANUFACTURER_DEV)
# The AT32 target follows the T20V2 (taranis family) hardware/feature set.
add_definitions(-DPCBTARANIS -DRADIO_FAMILY_T20 -DPCBX7)
add_definitions(-DSTORAGE_USE_SDCARD_SPI)

# Internal / external modules
set(INTERNAL_MODULES CRSF CACHE STRING "Internal modules")
set(DEFAULT_INTERNAL_MODULE CROSSFIRE CACHE STRING "Default internal module")

set(BITMAPS_TARGET 9x_bitmaps)
set(FONTS_TARGET 9x_fonts_1bit)
set(RADIO_DEPENDENCIES ${RADIO_DEPENDENCIES} ${BITMAPS_TARGET} ${FONTS_TARGET})

# ROM / RAM for size report + linker
# AT32F435ZMT7: 4032K flash @ 0x08000000, 384K RAM @ 0x20000000 (rest mapped separately)
set(TARGET_LINKER_DIR at32f435)
set(TARGET_FLASH_SIZE 0x3F0000)
set(SIZE_TARGET_MEM_DEFINE "MEM_SIZE_FLASH=4032")
set(FIRMWARE_STRIP_BOOTLOADER YES)

add_definitions(-DAT32F435 -DAT32F435ZMT7)
add_definitions(-DCPU_TYPE_FULL=AT32F435ZMT7)

# Board
set(BOARD at32f435)
include_directories(boards/${BOARD})
include_directories(targets/${TARGET_DIR})  # for usb_descriptor.h

set(TARGET_SRC_DIR targets/${TARGET_DIR})
set(BOARD_SRC_DIR boards/${BOARD})

# ---- Board sources ---------------------------------------------------------
set(BOARD_COMMON_SRC
  ${BOARD_SRC_DIR}/board.cpp
  ${BOARD_SRC_DIR}/bsp_io.cpp
  ${BOARD_SRC_DIR}/system_clock.c               # systemClockInit()
  ${BOARD_SRC_DIR}/aux_ports.cpp                # auxSerialGetPort()
  ${BOARD_SRC_DIR}/lcd_driver_spi.cpp          # monochrome LCD driver
  ${BOARD_SRC_DIR}/module_ports.cpp            # module port table (WIP)
  ${TARGET_SRC_DIR}/key_driver.cpp
  # TODO: add backlight / audio / SD / haptic drivers as the board is described
  targets/common/arm/at32/at32_gpio.cpp
  targets/common/arm/at32/abnormal_reboot.cpp
  targets/common/arm/at32/delays_driver.cpp
  targets/common/arm/at32/flash_driver.cpp
  targets/common/arm/at32/at32_spi.cpp
  targets/common/arm/at32/sdcard_spi.cpp
  targets/common/arm/at32/diskio_spi.cpp
  targets/common/arm/at32/pwr_driver.cpp
  targets/common/arm/at32/rotary_encoder_driver.cpp
  targets/common/arm/at32/timers_driver.cpp
  targets/common/arm/at32/watchdog_driver.cpp
  targets/common/arm/at32/usb_driver.cpp
  targets/common/arm/at32/msc_diskio.cpp
  targets/common/arm/at32/usbd_desc.cpp
  targets/common/arm/at32/stm32_exti_driver.cpp
)

# Bootloader board library
add_library(board_bl OBJECT EXCLUDE_FROM_ALL
  ${BOARD_COMMON_SRC}
  ${RADIO_SRC_DIR}/gui/common/stdlcd/boot_menu.cpp
)
set(BOOTLOADER_SRC ${BOOTLOADER_SRC} $<TARGET_OBJECTS:board_bl>)

# Firmware board library
add_library(board OBJECT EXCLUDE_FROM_ALL
  ${BOARD_COMMON_SRC}
  targets/common/arm/at32/heartbeat_driver.cpp
  targets/common/arm/at32/mixer_scheduler_driver.cpp
  targets/common/arm/at32/at32_module_timer_driver.cpp
  targets/common/arm/at32/at32_pulse_driver.cpp
  targets/common/arm/at32/at32_switch_driver.cpp
  targets/common/arm/at32/at32_trainer_driver.cpp
  targets/common/arm/at32/switches.cpp
)
set(FIRMWARE_SRC ${FIRMWARE_SRC} $<TARGET_OBJECTS:board>)

# syscalls.c provides `heap` (used by availableMemory()) and the _sbrk()/syscall stubs
# required by newlib. The STM32 port adds it in targets/common/arm/stm32/CMakeLists.txt;
# the AT32 port must add it explicitly.
set(FIRMWARE_SRC ${FIRMWARE_SRC} ${RADIO_SRC_DIR}/syscalls.c)

# Extra GUI sources for the Taranis-family monochrome UI (matches targets/taranis/CMakeLists.txt).
# These files live in gui/${GUI_DIR} but are not listed in the per-GUI CMakeLists.
set(GUI_SRC ${GUI_SRC}
  model_input_edit.cpp
  model_mix_edit.cpp
  model_display.cpp
  radio_diagkeys.cpp
  radio_diaganas.cpp
  view_channels.cpp
  view_telemetry.cpp
  view_about.cpp
  bmp.cpp
)

include_directories(${RADIO_SRC_DIR}/fonts/${GUI_DIR} gui/${GUI_DIR})

# SD manager firmware update / bootloader flashing support (matches other targets)
set(SRC ${SRC}
  io/bootloader_flash.cpp
  io/frsky_firmware_update.cpp
  io/multi_firmware_update.cpp
)

if(BOOTLOADER)
  set(FIRMWARE_TARGET_SRC ${FIRMWARE_TARGET_SRC} ../../bootloader/loadboot.cpp)
endif()
