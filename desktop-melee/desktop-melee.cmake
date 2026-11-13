# desktop-melee additions, included from the root CMakeLists.txt after the melee
# target. Our code lives in desktop-melee/; upstream files only get small hooks.
target_sources(melee PRIVATE desktop-melee/src/desktop_melee.c desktop-melee/src/assets.c
        desktop-melee/src/gctex.c desktop-melee/src/disc_check.cpp)

# The disc check's command-line tester (desktop-melee/tests/disc_check_test.sh builds it).
# Same sources and links as upstream's launcher_data_test.
add_executable(desktop_melee_disc_check EXCLUDE_FROM_ALL desktop-melee/tools/disc_check_main.c
        desktop-melee/src/disc_check.cpp src/pc/launcher_data.cpp src/pc/disc_open.c)
target_include_directories(desktop_melee_disc_check PRIVATE src desktop-melee/src)
target_link_libraries(desktop_melee_disc_check PRIVATE aurora::dvd ${MELEE_CRYPTO_LIBS})
target_include_directories(melee PRIVATE desktop-melee/src)
# The game code (built by GCC) calls desktop_melee_hide_stage() and friends, and
# includes our game mode (GM_DESKTOP_MELEE), which is built as game code too.
target_include_directories(melee_game PRIVATE desktop-melee/src desktop-melee/game)
target_sources(melee_game PRIVATE desktop-melee/game/gmdesktopmelee.c
        desktop-melee/game/desktop_melee_assets.c desktop-melee/game/desktop_melee_platforms.c
        desktop-melee/game/desktop_melee_cpu_practice.c)

# One implementation of desktop-melee/src/platform.h per system
# (docs/desktop-melee/porting.md). Windows and Linux are skeletons that run
# the game in a normal window; the Linux one is plain SDL, so every other
# system (Android, iOS) builds it too.
set(DESKTOP_MELEE_MACOS OFF)
if (APPLE AND NOT (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    set(DESKTOP_MELEE_MACOS ON)
elseif (WIN32)
    target_sources(melee PRIVATE desktop-melee/src/platform_windows.c
            desktop-melee/src/overlay_windows.cpp desktop-melee/src/taskbar_windows.cpp
            desktop-melee/src/settings_file.c desktop-melee/src/tray_sdl.c
            desktop-melee/src/settings_window.cpp desktop-melee/src/welcome_window.cpp
            desktop-melee/src/rml_dashed.cpp desktop-melee/src/window_icon.c
            desktop-melee/src/melee_font.cpp)
    # The overlay's composition swap chain (overlay_windows.cpp), the
    # taskbar's frame (SHAppBarMessage) and the windows' frames (DWM), and
    # the taskbar's buttons through UI Automation (taskbar_windows.cpp, a
    # COM object: ole32 and oleaut32, which every Windows has).
    target_link_libraries(melee PRIVATE dxgi shell32 dwmapi ole32 oleaut32 uuid)
else ()
    target_sources(melee PRIVATE desktop-melee/src/platform_linux.c
            desktop-melee/src/settings_file.c desktop-melee/src/tray_sdl.c
            desktop-melee/src/settings_window.cpp desktop-melee/src/welcome_window.cpp
            desktop-melee/src/rml_dashed.cpp desktop-melee/src/window_icon.c
            desktop-melee/src/melee_font.cpp)
    # The Pause and hide shortcut through the GlobalShortcuts portal, where
    # there is a session bus (desktop Linux; not Android or iOS).
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(DESKTOP_MELEE_DBUS QUIET IMPORTED_TARGET dbus-1)
    endif ()
    if (DESKTOP_MELEE_DBUS_FOUND)
        target_sources(melee PRIVATE desktop-melee/src/hotkey_portal.c
                desktop-melee/src/desktop_entry.c)
        target_link_libraries(melee PRIVATE PkgConfig::DESKTOP_MELEE_DBUS)
        target_compile_definitions(melee PRIVATE DESKTOP_MELEE_HOTKEY_PORTAL=1)
    endif ()

    # The overlay window on X11 (overlay_x11.c), with its Vulkan layer
    # (vk_overlay_alpha.c) built next to the game: aurora's Dawn makes opaque
    # Vulkan swap chains, the layer makes them transparent. It uses the Vulkan
    # headers SDL bundles. Without X11 or them, the game stays a normal window.
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(DESKTOP_MELEE_X11 QUIET IMPORTED_TARGET x11)
    endif ()
    FetchContent_GetProperties(SDL)
    set(_desktop_melee_vulkan_headers "")
    foreach (_dir "${sdl_SOURCE_DIR}" "${SDL3_SOURCE_DIR}")
        if (_dir AND EXISTS "${_dir}/src/video/khronos/vulkan/vk_layer.h")
            set(_desktop_melee_vulkan_headers "${_dir}/src/video/khronos")
            break ()
        endif ()
    endforeach ()
    if (DESKTOP_MELEE_X11_FOUND AND _desktop_melee_vulkan_headers)
        add_library(desktop_melee_overlay_alpha SHARED desktop-melee/src/vk_overlay_alpha.c)
        target_include_directories(desktop_melee_overlay_alpha PRIVATE "${_desktop_melee_vulkan_headers}")
        set_target_properties(desktop_melee_overlay_alpha PROPERTIES
                C_VISIBILITY_PRESET hidden
                LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:melee>")
        file(GENERATE OUTPUT "$<TARGET_FILE_DIR:melee>/desktop_melee_overlay_alpha.json"
                INPUT "${CMAKE_CURRENT_SOURCE_DIR}/desktop-melee/src/vk_overlay_alpha.json")
        add_dependencies(melee desktop_melee_overlay_alpha)
        target_sources(melee PRIVATE desktop-melee/src/overlay_x11.c
                desktop-melee/src/window_list_linux.c)
        target_link_libraries(melee PRIVATE PkgConfig::DESKTOP_MELEE_X11)
        target_compile_definitions(melee PRIVATE DESKTOP_MELEE_X11_OVERLAY=1)
    else ()
        message(STATUS "desktop-melee: no X11 or Vulkan headers: the Linux overlay is off")
    endif ()
endif ()

# The settings window's documents (settings_window.cpp), next to melee-pc's
# in resources/desktop-melee/.
if (NOT DESKTOP_MELEE_MACOS)
    add_custom_target(desktop_melee_resources ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/desktop-melee/resources"
            "$<TARGET_FILE_DIR:melee>/resources/desktop-melee")
    add_dependencies(desktop_melee_resources launcher_resources)
    add_dependencies(melee desktop_melee_resources)
endif ()

# The settings file's parser and writer, tested on their own
# (desktop-melee/tests/settings_file_test.c stubs the game's setters).
if (NOT DESKTOP_MELEE_MACOS)
    add_executable(desktop_melee_settings_test EXCLUDE_FROM_ALL
            desktop-melee/tests/settings_file_test.c desktop-melee/src/settings_file.c)
    target_include_directories(desktop_melee_settings_test PRIVATE desktop-melee/src)
    target_link_libraries(desktop_melee_settings_test PRIVATE ${AURORA_SDL3_TARGET})
    target_compile_options(desktop_melee_settings_test PRIVATE -UNDEBUG)
endif ()

if (DESKTOP_MELEE_MACOS)
    target_sources(melee PRIVATE desktop-melee/src/overlay_mac.m)
    target_link_libraries(melee PRIVATE "-framework AppKit" "-framework ImageIO")

    # The menu bar app (SwiftUI), as a static library linked into the game.
    enable_language(Swift)
    add_library(desktop_melee_ui STATIC
            desktop-melee/ui/MenuBar.swift
            desktop-melee/ui/Panel.swift
            desktop-melee/ui/MatchTab.swift
            desktop-melee/ui/PlayersTab.swift
            desktop-melee/ui/ControlsTab.swift
            desktop-melee/ui/DisplayTab.swift
            desktop-melee/ui/GameAssets.swift
            desktop-melee/ui/Emblem.swift
            desktop-melee/ui/DiscMemory.swift
            desktop-melee/ui/Welcome.swift
            desktop-melee/ui/WelcomeSteps.swift
            desktop-melee/ui/WelcomeMotion.swift
            desktop-melee/ui/WelcomeDebug.swift
            desktop-melee/ui/Hotkey.swift
            desktop-melee/ui/Loading.swift
            desktop-melee/ui/Settings.swift)
    set_target_properties(desktop_melee_ui PROPERTIES
            Swift_MODULE_NAME DesktopMeleeUI
            Swift_LANGUAGE_VERSION 5)
    # The project's directory-wide C flags (-fsigned-char, ...) mean nothing to
    # swiftc; start the Swift target from a clean slate. DesktopMeleeBridge is the C
    # interface the UI calls (module map in desktop-melee/ui/bridge).
    set_property(TARGET desktop_melee_ui PROPERTY COMPILE_OPTIONS
            "SHELL:-I ${CMAKE_CURRENT_SOURCE_DIR}/desktop-melee/ui/bridge")
    set_property(TARGET desktop_melee_ui PROPERTY COMPILE_DEFINITIONS "")
    target_link_libraries(melee PRIVATE desktop_melee_ui
            "-framework SwiftUI" "-framework Combine" "-framework Carbon")
    # The game links with clang++, not swiftc (which CMake would pick for a
    # target with a Swift dependency), so point it at the Swift runtime the
    # Swift objects autolink against.
    set_target_properties(melee PROPERTIES LINKER_LANGUAGE CXX)
    execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE DESKTOP_MELEE_SWIFTC
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(DESKTOP_MELEE_SWIFT_BIN "${DESKTOP_MELEE_SWIFTC}" DIRECTORY)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE DESKTOP_MELEE_SDK
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_link_directories(melee PRIVATE
            "${DESKTOP_MELEE_SWIFT_BIN}/../lib/swift/macosx"
            "${DESKTOP_MELEE_SDK}/usr/lib/swift")
    target_link_options(melee PRIVATE "LINKER:-rpath,/usr/lib/swift")
endif ()
