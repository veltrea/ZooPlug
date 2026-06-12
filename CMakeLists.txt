cmake_minimum_required(VERSION 3.16)
project(ZooPlug VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ソースは UTF-8（日本語コメントを含む）。MSVC は既定でシステムコードページ
# として読むため /utf-8 を明示しないと文字化け・構文エラーになる。
if(MSVC)
    add_compile_options(/utf-8)
    # CRT を静的リンク（/MT）して配布時に VC++ ランタイム再頒布を不要にする。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# 1) 純粋ロジックの単体テスト（FileMaker SDK 不要。どの環境でもビルドできる）
#    cmake -B build && cmake --build build && ctest --test-dir build
# ---------------------------------------------------------------------------
add_executable(test_shellexec
    tests/test_shellexec.cpp
    Source/ShellExec.cpp)
target_include_directories(test_shellexec PRIVATE Source)

enable_testing()
add_test(NAME shellexec COMMAND test_shellexec)

# ---------------------------------------------------------------------------
# 2) FileMaker プラグイン本体（FMWrapper のリンクが必要）
#    FMWrapper のリンクライブラリは Libraries/ に同梱済みなので、通常は
#      cmake -B build -DBUILD_PLUGIN=ON
#    だけでビルドできる。別の SDK を使いたい場合のみ場所を上書きする:
#      Windows: -DFMX_LIB="C:/path/FMWrapper.lib"
#      macOS  : -DFMX_FRAMEWORK_DIR="/dir/containing/FMWrapper.framework"
# ---------------------------------------------------------------------------
option(BUILD_PLUGIN "Build the FileMaker plug-in (requires FMWrapper)" OFF)

if(BUILD_PLUGIN)
    set(PLUGIN_SOURCES Source/ZooPlug.cpp Source/ShellExec.cpp)

    if(WIN32)
        add_library(ZooPlug SHARED ${PLUGIN_SOURCES})
        target_include_directories(ZooPlug PRIVATE Source Headers)

        # 64bit FileMaker 用プラグインの拡張子は .fmx64（接頭辞なし）
        set_target_properties(ZooPlug PROPERTIES
            OUTPUT_NAME "ZooPlug"
            PREFIX ""
            SUFFIX ".fmx64")

        # 未指定なら同梱の FMWrapper.lib を使う
        if(NOT FMX_LIB)
            set(FMX_LIB "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/win64/FMWrapper.lib")
        endif()
        if(NOT EXISTS "${FMX_LIB}")
            message(FATAL_ERROR "FMWrapper.lib not found at: ${FMX_LIB}")
        endif()
        target_link_libraries(ZooPlug PRIVATE "${FMX_LIB}")

    elseif(APPLE)
        add_library(ZooPlug MODULE ${PLUGIN_SOURCES})
        target_include_directories(ZooPlug PRIVATE Source Headers)

        # macOS の FileMaker プラグインは X.fmplugin バンドル
        set_target_properties(ZooPlug PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "fmplugin"
            OUTPUT_NAME "ZooPlug"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")

        # 未指定なら同梱の FMWrapper.framework を使う
        if(NOT FMX_FRAMEWORK_DIR)
            set(FMX_FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/macOS")
        endif()
        if(NOT EXISTS "${FMX_FRAMEWORK_DIR}/FMWrapper.framework")
            message(FATAL_ERROR "FMWrapper.framework not found in: ${FMX_FRAMEWORK_DIR}")
        endif()
        target_link_libraries(ZooPlug PRIVATE
            "-F${FMX_FRAMEWORK_DIR}" "-framework FMWrapper" "-framework CoreFoundation")

    elseif(UNIX)
        # Linux（FileMaker Server）。プラグインは拡張子 .fmx の共有モジュール。
        # 公式サンプルにならい clang + libc++ でのビルドを推奨（README 参照）。
        add_library(ZooPlug MODULE ${PLUGIN_SOURCES})
        target_include_directories(ZooPlug PRIVATE Source Headers)
        set_target_properties(ZooPlug PROPERTIES
            OUTPUT_NAME "ZooPlug" PREFIX "" SUFFIX ".fmx"
            # 実行時 libFMWrapper.so は FileMaker の /opt/FileMaker/lib にある
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "/opt/FileMaker/lib")

        # Linux 用 libFMWrapper.so はこのリポジトリには同梱していない（Claris SDK から取得する）。
        # Libraries/linux/<U22|U24>/<x64|arm64>/ に置けば自動検出する。なければ -DFMX_LIB で渡す。
        if(NOT FMX_LIB)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(_zoo_arch arm64)
            else()
                set(_zoo_arch x64)
            endif()
            set(_zoo_ubuntu U24)
            if(EXISTS "/etc/os-release")
                file(READ "/etc/os-release" _zoo_osrel)
                if(_zoo_osrel MATCHES "VERSION_ID=\"22")
                    set(_zoo_ubuntu U22)
                endif()
            endif()
            set(FMX_LIB "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/linux/${_zoo_ubuntu}/${_zoo_arch}/libFMWrapper.so")
        endif()
        if(NOT EXISTS "${FMX_LIB}")
            message(FATAL_ERROR
                "libFMWrapper.so not found at: ${FMX_LIB}\n"
                "  The Linux library is NOT bundled in this repo. Get it from the Claris\n"
                "  FileMaker Plug-In SDK (Libraries/Linux/<U22|U24>/<x64|arm64>/libFMWrapper.so)\n"
                "  and place it under Libraries/linux/... , or pass -DFMX_LIB=/path/to/libFMWrapper.so")
        endif()
        target_link_libraries(ZooPlug PRIVATE "${FMX_LIB}")

    else()
        message(FATAL_ERROR "Unsupported platform for the plug-in target (the test target builds anywhere).")
    endif()
endif()
