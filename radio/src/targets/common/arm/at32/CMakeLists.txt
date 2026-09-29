# AT32 family common CMake (new MCU family port)
# ============================================================================
# This brings in the AT32F435/F437 device support and peripheral drivers.
# Modeled after targets/common/arm/stm32/f4/CMakeLists.txt
# ============================================================================

set(CPU_FAMILY AT32)
# AT32 is an ARM target; setting ARCH makes radio/src include
# targets/common/arm/CMakeLists.txt (provides DSM2/PXX/DSMP/TIMERS/VARIO macros).
set(ARCH ARM)
set(MCU cortex-m4)
set(FPU_FLAGS "-mfloat-abi=hard -mfpu=fpv4-sp-d16")

# Linker scripts for AT32 (bootloader.ld / firmware.ld)
set(LINKER_DIR ${RADIO_SRC_DIR}/boards/at32/linker)
add_link_options(-L${LINKER_DIR} -L${LINKER_DIR}/${TARGET_LINKER_DIR})

if(NOT NATIVE_BUILD)

  # MCU specific flags (Cortex-M4 + FPU)
  add_link_options(
    -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16
  )

  # AT32 peripheral driver library
  set(AT32LIB_DIR ${THIRDPARTY_DIR}/AT32F435_437_HAL_Driver)
  include_directories(${AT32LIB_DIR}/inc)

  # AT32 USB device library (OTG + USB device middleware)
  set(AT32USBLIB_DIR ${THIRDPARTY_DIR}/AT32F435_437_USB_Library)
  include_directories(
    ${AT32USBLIB_DIR}/middlewares/usb_drivers/inc
    ${AT32USBLIB_DIR}/middlewares/usbd_class/msc
  )

  add_library(at32drv OBJECT EXCLUDE_FROM_ALL
    # AT32 device support (system clock is provided via CMSIS_SRC below;
    # startup / vector table)
    ${THIRDPARTY_DIR}/CMSIS/Device/Artery/AT32F435_437/startup/gcc/startup_at32f435_437.s
    # Peripheral drivers (select the ones needed by the board)
    ${AT32LIB_DIR}/src/at32f435_437_gpio.c
    ${AT32LIB_DIR}/src/at32f435_437_crm.c
    ${AT32LIB_DIR}/src/at32f435_437_scfg.c
    ${AT32LIB_DIR}/src/at32f435_437_flash.c
    ${AT32LIB_DIR}/src/at32f435_437_misc.c
    ${AT32LIB_DIR}/src/at32f435_437_dma.c
    ${AT32LIB_DIR}/src/at32f435_437_exint.c
    ${AT32LIB_DIR}/src/at32f435_437_usart.c
    ${AT32LIB_DIR}/src/at32f435_437_spi.c
    ${AT32LIB_DIR}/src/at32f435_437_tmr.c
    ${AT32LIB_DIR}/src/at32f435_437_i2c.c
    ${AT32LIB_DIR}/src/at32f435_437_wdt.c
    ${AT32LIB_DIR}/src/at32f435_437_wwdt.c
    ${AT32LIB_DIR}/src/at32f435_437_pwc.c
    ${AT32LIB_DIR}/src/at32f435_437_debug.c
    ${AT32LIB_DIR}/src/at32f435_437_usb.c
    # AT32 USB device library middleware
    ${AT32USBLIB_DIR}/middlewares/usb_drivers/src/usb_core.c
    ${AT32USBLIB_DIR}/middlewares/usb_drivers/src/usbd_core.c
    ${AT32USBLIB_DIR}/middlewares/usb_drivers/src/usbd_int.c
    ${AT32USBLIB_DIR}/middlewares/usb_drivers/src/usbd_sdr.c
    ${AT32USBLIB_DIR}/middlewares/usbd_class/msc/msc_class.c
    ${AT32USBLIB_DIR}/middlewares/usbd_class/msc/msc_bot_scsi.c
  )

  target_include_directories(at32drv PRIVATE
    ${THIRDPARTY_DIR}/CMSIS/Device/Artery/AT32F435_437/Include
    ${THIRDPARTY_DIR}/CMSIS/Include
    ${CMAKE_CURRENT_SOURCE_DIR})

  set(FIRMWARE_SRC ${FIRMWARE_SRC} $<TARGET_OBJECTS:at32drv>)
  set(BOOTLOADER_SRC ${BOOTLOADER_SRC} $<TARGET_OBJECTS:at32drv>)
endif()

# AT32 device support
set(AT32_DEVICE_SUPPORT ${THIRDPARTY_DIR}/CMSIS/Device/Artery/AT32F435_437/Include)
include_directories(${AT32_DEVICE_SUPPORT})
include_directories(${THIRDPARTY_DIR}/CMSIS/Include)

add_definitions(-DAT32F4)
add_definitions(-DAT32F435)
# Match the STM32 common target: enables MIXSRC_FIRST_LUA/LAST_LUA.
add_definitions(-DLUA_INPUTS)

# CMSIS device sources used by firmware / bootloader / simulator
set(CMSIS_SRC ${CMSIS_SRC}
  ${AT32_DEVICE_SUPPORT}/system_at32f435_437.c
)

add_library(cmsis INTERFACE)
target_sources(cmsis INTERFACE ${CMSIS_SRC})

if(NOT NATIVE_BUILD)
  include_directories(targets/common/arm/at32)

  # FreeRTOS port (Cortex-M4F)
  add_library(freertos OBJECT EXCLUDE_FROM_ALL
    ${RTOS_DIR}/list.c
    ${RTOS_DIR}/queue.c
    ${RTOS_DIR}/tasks.c
    ${RTOS_DIR}/event_groups.c
    ${RTOS_DIR}/timers.c
    ${RTOS_DIR}/stream_buffer.c
    ${RTOS_DIR}/portable/GCC/ARM_CM4F/port.c
    )

  target_include_directories(freertos
    PUBLIC  ${RTOS_DIR}/portable/GCC/ARM_CM4F
    PRIVATE ${RTOS_DIR}/include
    )

  include_directories(${RTOS_DIR}/portable/GCC/ARM_CM4F)

  # EdgeTx Compat layer
  set(FIRMWARE_SRC ${FIRMWARE_SRC} rtos.c)
  set(FIRMWARE_SRC ${FIRMWARE_SRC} $<TARGET_OBJECTS:freertos>)
endif()
