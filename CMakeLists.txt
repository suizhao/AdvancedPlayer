cmake_minimum_required(VERSION 3.25)

project(AdvancedPlayer VERSION 1.0.0 LANGUAGES CXX)

# 统一源码输入/执行字符集为 UTF-8
if(MSVC)
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
elseif(MINGW)
    add_compile_options(-finput-charset=utf-8 -fexec-charset=utf-8)
endif()

# 使用 C++20，并要求标准必须满足
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启 Qt 自动 MOC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt 组件
find_package(Qt6 REQUIRED
    COMPONENTS
        Quick Core Qml Gui Svg Network Sql Concurrent
)

# FFmpeg 头文件和库路径
set(FFMPEG_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(FFMPEG_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(FFMPEG_LIBRARIES
    ${FFMPEG_LIBRARY_DIR}/libavcodec.dll.a
    ${FFMPEG_LIBRARY_DIR}/libavformat.dll.a
    ${FFMPEG_LIBRARY_DIR}/libavutil.dll.a
    ${FFMPEG_LIBRARY_DIR}/libswscale.dll.a
    ${FFMPEG_LIBRARY_DIR}/libswresample.dll.a
    ${FFMPEG_LIBRARY_DIR}/libavdevice.dll.a
    ${FFMPEG_LIBRARY_DIR}/libpostproc.dll.a
)


# SDL3 头文件和库路径
set(SDL3_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SDL3_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/lib/libSDL3.dll.a)

# SoundTouch 头文件和库路径
set(SOUNDTOUCH_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SOUNDTOUCH_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SOUNDTOUCH_LIBRARY ${SOUNDTOUCH_LIBRARY_DIR}/SoundTouchDLL_x64.lib)
    set(SOUNDTOUCH_DLL ${SOUNDTOUCH_DIR}/bin/SoundTouchDLL_x64.dll)
else()
    set(SOUNDTOUCH_LIBRARY ${SOUNDTOUCH_LIBRARY_DIR}/SoundTouchDLL.lib)
    set(SOUNDTOUCH_DLL ${CMAKE_CURRENT_SOURCE_DIR}/bin/SoundTouchDLL.dll)
endif()

# 初始化 Qt 标准项目配置
qt_standard_project_setup(REQUIRES 6.8)

# 项目源文件
set(PROJECT_SOURCES
    main.cpp

    # Core
    src/core/MediaPlayer.h
    src/core/MediaPlayer.cpp
    src/core/PlaybackController.h
    src/core/PlaybackController.cpp

    src/core/LockFreeRingBuffer.h
    src/core/LockFreePacketQueue.h
    src/core/LockFreePacketQueue.cpp
    src/core/LockFreeVideoFrameQueue.h
    src/core/LockFreeVideoFrameQueue.cpp
    src/core/LockFreeAudioFrameQueue.h
    src/core/LockFreeAudioFrameQueue.cpp

    src/core/DemuxContext.h
    src/core/DemuxThread.h
    src/core/DemuxThread.cpp
    src/core/DecodeContext.h
    src/core/DecodeContext.cpp
    src/core/VideoDecodeThread.h
    src/core/VideoDecodeThread.cpp
    src/core/AudioDecodeThread.h
    src/core/AudioDecodeThread.cpp
    src/core/RenderContext.h
    src/core/VideoRenderThread.h
    src/core/VideoRenderThread.cpp
    src/core/AudioRenderThread.h
    src/core/AudioRenderThread.cpp

    # Decoder
    src/decoder/DecoderInterface.h
    src/decoder/HardwareDecoder.h
    src/decoder/HardwareDecoder.cpp
    src/decoder/SoftwareDecoder.h
    src/decoder/SoftwareDecoder.cpp

    # Audio
    src/audio/AudioOutput.h
    src/audio/AudioOutput.cpp
    src/audio/AudioResampler.h
    src/audio/AudioResampler.cpp
    src/audio/SoundTouchProcessor.h
    src/audio/SoundTouchProcessor.cpp

    # Video
    src/video/VideoOutput.h
    src/video/VideoOutput.cpp
    src/video/OpenGLVideoConverter.h
    src/video/OpenGLVideoConverter.cpp

    # UI
    src/ui/PlaylistModel.h
    src/ui/PlaylistModel.cpp
    src/ui/NetworkPlaylistModel.h
    src/ui/NetworkPlaylistModel.cpp
    src/ui/SettingsManager.h
    src/ui/SettingsManager.cpp

    # Utils
    src/utils/ScreenshotCapture.h
    src/utils/ScreenshotCapture.cpp
    src/utils/FileScanner.h
    src/utils/FileScanner.cpp
)

set(BACKEND_TEST_SOURCES ${PROJECT_SOURCES})
list(REMOVE_ITEM BACKEND_TEST_SOURCES main.cpp)

# QML 资源
set(QML_SOURCES
    qml/HomePage.qml
    qml/Main.qml
    qml/components/ControlPanel.qml
    qml/components/CustomProgressBar.qml
    qml/components/SpeedControl.qml
    qml/components/VolumeControl.qml
    qml/components/PlaylistPanel.qml
    qml/components/SettingsDialog.qml
    qml/components/NotificationToast.qml
)

qt_add_executable(appAdvancedPlayer
    ${PROJECT_SOURCES}
)

qt_add_executable(endbackTest
    endbackTest.cpp
    ${BACKEND_TEST_SOURCES}
)

qt_add_qml_module(appAdvancedPlayer
    URI AdvancedPlayer
    VERSION 1.0
    QML_FILES ${QML_SOURCES}
    RESOURCES
        src/utils/Utils.js
        resources/play.svg
        resources/pause.svg
        resources/stop.svg
        resources/next.svg
        resources/previous.svg
        resources/volume.svg
        resources/mute.svg
        resources/fullscreen.svg
        resources/fullscreen-exit.svg
        resources/screenshot.svg
        resources/settings.svg
        resources/chevron-left.svg
        resources/chevron-right.svg
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appAdvancedPlayer PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appDemoPlayer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# 目标私有头文件路径，供当前目标 include 使用
target_include_directories(appAdvancedPlayer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        ${SDL3_INCLUDE_DIRS}
        ${SOUNDTOUCH_INCLUDE_DIRS}
)

target_link_libraries(appAdvancedPlayer
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Network
        Qt6::Sql
        Qt6::Svg
        Qt6::Concurrent
        ${FFMPEG_LIBRARIES}
        ${SDL3_LIBRARY}
        ${SOUNDTOUCH_LIBRARY}
)

target_include_directories(endbackTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        ${SDL3_INCLUDE_DIRS}
        ${SOUNDTOUCH_INCLUDE_DIRS}
)

target_link_libraries(endbackTest
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Network
        Qt6::Sql
        Qt6::Svg
        Qt6::Concurrent
        ${FFMPEG_LIBRARIES}
        ${SDL3_LIBRARY}
        ${SOUNDTOUCH_LIBRARY}
)

# 平台相关系统库
if(WIN32)
    # Windows:
    # - user32.lib: 提供 SetParent/SetWindowLong 等 Win32 API
    # - d3d11.lib, dxgi.lib: 提供 D3D11 与 DXGI 接口支持
    # - opengl32.lib: 提供 wglGetProcAddress/wglGetCurrentDC 等 WGL 接口
    target_link_libraries(appAdvancedPlayer PRIVATE
        user32
        d3d11
        dxgi
        opengl32
    )
    target_link_libraries(endbackTest PRIVATE
        user32
        d3d11
        dxgi
        opengl32
    )
elseif(UNIX AND NOT APPLE)
    # Linux: 链接 X11，用于窗口相关 API
    find_package(X11 REQUIRED)
    target_include_directories(appAdvancedPlayer PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(appAdvancedPlayer PRIVATE ${X11_LIBRARIES})
elseif(APPLE)
    # macOS: 链接 Cocoa，用于 NSView/NSWindow
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    target_link_libraries(appAdvancedPlayer PRIVATE ${COCOA_LIBRARY})
endif()

# 宏定义：声明可选功能启用状态
target_compile_definitions(appAdvancedPlayer
    PRIVATE
        HAS_FFMPEG
        HAS_SDL3
        HAS_SOUNDTOUCH
)

target_compile_definitions(endbackTest
    PRIVATE
        HAS_FFMPEG
        HAS_SDL3
        HAS_SOUNDTOUCH
)

# 编译警告策略
if(MSVC)
    # Visual Studio / MSVC 警告配置
    target_compile_options(appAdvancedPlayer PRIVATE
        /W4                    # 开启较高等级警告
        /WX                    # 警告视为错误
        /wd4251                # 抑制 Qt 导出相关告警
        /wd4996                # 抑制不安全函数告警（如 sprintf）
    )
    target_compile_options(endbackTest PRIVATE
        /W4
        /WX
        /wd4251
        /wd4996
    )
elseif(MINGW)
    # Windows / MinGW 警告配置
    target_compile_options(appAdvancedPlayer PRIVATE
        -Wall                  # 常用警告
        -Wextra                # 额外警告
        -Werror                # 警告视为错误
        -Wno-unused-parameter  # 忽略未使用参数
        -Wno-missing-field-initializers # 忽略聚合初始化缺省字段告警
        -static-libgcc         # 静态链接 libgcc，减少运行时 DLL 依赖
        -static-libstdc++      # 静态链接 libstdc++，减少运行时 DLL 依赖
    )
    target_compile_options(endbackTest PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -static-libgcc
        -static-libstdc++
    )
else()
    # Linux/macOS 下 GCC/Clang 警告配置
    target_compile_options(appAdvancedPlayer PRIVATE
        -Wall                  # 常用警告
        -Wextra                # 额外警告
        -Werror                # 警告视为错误
        -Wno-unused-parameter  # 忽略未使用参数
    )
    target_compile_options(endbackTest PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
    )
    # Clang 额外配置
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(appAdvancedPlayer PRIVATE
            -Wno-c++98-compat  # 关闭 C++98 兼容性提示（项目使用现代标准）
        )
        target_compile_options(endbackTest PRIVATE
            -Wno-c++98-compat
        )
    endif()
endif()

# Windows: 复制运行时所需 DLL 到目标目录
if(WIN32)
    add_custom_command(TARGET appAdvancedPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/bin
            $<TARGET_FILE_DIR:appAdvancedPlayer>
        COMMENT "复制 FFmpeg/SDL3/SoundTouch DLL 到输出目录"
    )
    add_custom_command(TARGET endbackTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/bin
            $<TARGET_FILE_DIR:endbackTest>
        COMMENT "复制 FFmpeg/SDL3/SoundTouch DLL 到输出目录"
    )
endif()

include(GNUInstallDirs)
install(TARGETS appAdvancedPlayer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS endbackTest
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
