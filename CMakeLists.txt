cmake_minimum_required(VERSION 3.21)
project(KnobApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Homebrew Qt on macOS: cmake -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(SOURCES
    src/main.cpp
    src/app/Config.h
    src/app/Keycodes.h
    src/app/TrayApp.h
    src/app/TrayApp.cpp
    src/app/SettingsWindow.h
    src/app/SettingsWindow.cpp
    src/engine/KnobScrollModel.h
    src/engine/ScrollEngine.h
)

if(APPLE)
    enable_language(OBJCXX)
    list(APPEND SOURCES
        src/engine/mac/MacScrollEngine.h
        src/engine/mac/MacScrollEngine.mm
        src/app/mac/MacTray.h
        src/app/mac/MacTray.mm
        src/app/mac/KnobHidChannel.h
        src/app/mac/KnobHidChannel.mm
    )
    set_source_files_properties(
        src/engine/mac/MacScrollEngine.mm src/app/mac/MacTray.mm src/app/mac/KnobHidChannel.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
endif()

qt_add_executable(KnobApp MACOSX_BUNDLE ${SOURCES})

target_include_directories(KnobApp PRIVATE src)
target_link_libraries(KnobApp PRIVATE Qt6::Widgets)

if(APPLE)
    set_target_properties(KnobApp PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Knob"
        MACOSX_BUNDLE_GUI_IDENTIFIER "tech.baselinedesign.knob"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # Menu-bar-only app: no Dock icon, no app switcher entry.
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    )
    target_link_libraries(KnobApp PRIVATE
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework IOKit"
        "-framework AppKit"
    )

    # Signing with a real identity keeps the TCC grants (Accessibility,
    # Input Monitoring) across rebuilds; ad-hoc signatures lose them every
    # build. Set e.g. -DKNOB_CODESIGN_IDENTITY="Developer ID Application: …"
    set(KNOB_CODESIGN_IDENTITY "" CACHE STRING "codesign identity for the app bundle")
    if(KNOB_CODESIGN_IDENTITY)
        add_custom_command(TARGET KnobApp POST_BUILD
            COMMAND codesign --force --sign "${KNOB_CODESIGN_IDENTITY}"
                    "$<TARGET_BUNDLE_DIR:KnobApp>"
            COMMENT "codesign: ${KNOB_CODESIGN_IDENTITY}"
            VERBATIM
        )
    endif()
endif()
