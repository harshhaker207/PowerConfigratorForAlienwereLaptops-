cmake_minimum_required(VERSION 3.20)
project(PowerConfigurator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific settings
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX- /MP /EHsc /MD /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /utf-8")
    add_compile_options(/permissive-)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O2 -s")
endif()

# Windows SDK and libraries - system libraries don't need find_library
set(POWRPROF powrprof)
set(USER32 user32)
set(GDI32 gdi32)
set(SHELL32 shell32)
set(OLE32 ole32)
set(OLEAUT32 oleaut32)
set(PDH pdh)
set(WBEMUUID wbemuuid)
set(HID hid)
set(SETUPAPI setupapi)
set(COMDLG32 comdlg32)
set(DWMAPI dwmapi)
set(UXTHEME uxtheme)
find_library(NVML nvml)

# NVML paths
set(NVML_INCLUDE_DIR "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/include" CACHE PATH "NVML include directory")
set(NVML_LIBRARY "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/lib/x64/nvml.lib" CACHE FILEPATH "NVML library")

if(EXISTS "${NVML_INCLUDE_DIR}/nvml.h" AND EXISTS "${NVML_LIBRARY}")
    message(STATUS "NVML found: ${NVML_INCLUDE_DIR}")
    include_directories(${NVML_INCLUDE_DIR})
    set(HAVE_NVML 1)
else()
    message(WARNING "NVML not found - GPU power control will be disabled")
    set(HAVE_NVML 0)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(PowerConfigurator
    src/main.cpp
    src/Common.cpp
    src/Theme.cpp
    src/ModernTheme.cpp
    src/UI.cpp
    src/PowerManager.cpp
    src/GpuManager.cpp
    src/CpuManager.cpp
    src/DisplayManager.cpp
    src/WindowsPowerManager.cpp
    src/OptimusManager.cpp
    src/LedManager.cpp
    src/FanManager.cpp
    src/Monitor.cpp
    src/AudioVisualizer.cpp
    src/ScreenAmbient.cpp
    src/Resource.rc
)

target_link_libraries(PowerConfigurator
    ${POWRPROF}
    ${USER32}
    ${GDI32}
    ${SHELL32}
    ${OLE32}
    ${OLEAUT32}
    ${PDH}
    ${WBEMUUID}
    ${HID}
    ${SETUPAPI}
    ${COMDLG32}
    ${DWMAPI}
    ${UXTHEME}
    winmm
)

if(HAVE_NVML)
    target_link_libraries(PowerConfigurator ${NVML_LIBRARY})
    target_compile_definitions(PowerConfigurator PRIVATE HAVE_NVML)
endif()

# Manifest configuration
if(MSVC)
    target_link_options(PowerConfigurator PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/manifest.xml"
    )
endif()

# Resources
target_compile_definitions(PowerConfigurator PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

# Output
set_target_properties(PowerConfigurator PROPERTIES
    OUTPUT_NAME "PowerConfigurator"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    WIN32_EXECUTABLE TRUE
)

# Post-build: strip if possible
if(NOT MSVC)
    add_custom_command(TARGET PowerConfigurator POST_BUILD
        COMMAND strip ${CMAKE_BINARY_DIR}/bin/PowerConfigurator
    )
endif()