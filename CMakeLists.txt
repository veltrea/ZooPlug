cmake_minimum_required(VERSION 3.16)
# C は同梱 miniz（Libraries/miniz/miniz.c）のため
project(ZooPlug VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS の Dialog 実装は Objective-C++（DialogOps_mac.mm・AppKit）。
if(APPLE)
    enable_language(OBJCXX)
endif()

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
    Source/ShellExec.cpp
    Source/ProcessRun.cpp)
target_include_directories(test_shellexec PRIVATE Source)

# zoo_powershell（§18 テンポラリ方式）の純粋ロジック + 実 pwsh 往復（pwsh が無ければ SKIP）
add_executable(test_powershellexec
    tests/test_powershellexec.cpp
    Source/PowerShellExec.cpp
    Source/ProcessRun.cpp
    Source/ShellExec.cpp)
target_include_directories(test_powershellexec PRIVATE Source)

# MooPlug 互換エラー表（MakeMooError / MooErrorDetail）
add_executable(test_mooerror
    tests/test_mooerror.cpp
    Source/MooError.cpp)
target_include_directories(test_mooerror PRIVATE Source)

# File/Folder 純粋ロジック（FileOps は改行正規化で ShellExec に依存）
add_executable(test_fileops
    tests/test_fileops.cpp
    Source/FileOps.cpp
    Source/ShellExec.cpp
    Source/ProcessRun.cpp)
target_include_directories(test_fileops PRIVATE Source)

# Moo_Hash（MD5/SHA-1/SHA-256/SHA-512、NIST ベクタで検証）
add_executable(test_hash
    tests/test_hash.cpp
    Source/HashImpl.cpp
    Source/FileOps.cpp
    Source/ShellExec.cpp
    Source/ProcessRun.cpp)
target_include_directories(test_hash PRIVATE Source)

# Zip 3 関数（同梱 miniz 3.0.2 を使用）
add_executable(test_zipops
    tests/test_zipops.cpp
    Source/ZipOps.cpp
    Source/FileOps.cpp
    Source/ShellExec.cpp
    Source/ProcessRun.cpp
    Libraries/miniz/miniz.c)
target_include_directories(test_zipops PRIVATE Source Libraries/miniz)

# Net 2+3 関数（Windows=WinINet / POSIX=libcurl）。FileOps は PathFromUTF8 のため。
add_executable(test_netops
    tests/test_netops.cpp
    Source/NetOps.cpp
    Source/FileOps.cpp
    Source/ShellExec.cpp
    Source/ProcessRun.cpp)
target_include_directories(test_netops PRIVATE Source)
if(WIN32)
    target_link_libraries(test_netops PRIVATE wininet)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(test_netops PRIVATE CURL::libcurl)
endif()

# Process 4 関数（Windows=Toolhelp32 / macOS=libproc / Linux=/proc）。追加リンク不要。
add_executable(test_processops
    tests/test_processops.cpp
    Source/ProcessOps.cpp)
target_include_directories(test_processops PRIVATE Source)

# Printer 2 関数（Windows=winspool / POSIX=CUPS）。
add_executable(test_printerops
    tests/test_printerops.cpp
    Source/PrinterOps.cpp)
target_include_directories(test_printerops PRIVATE Source)
if(WIN32)
    target_link_libraries(test_printerops PRIVATE winspool)
else()
    target_link_libraries(test_printerops PRIVATE cups)
endif()

# Dialog 3 関数は GUI モーダルのため headless の単体テストを持たない（実機確認は実機で実施）。

# Moo_ProgressOptions（状態保持・クロスプラットフォーム）。
add_executable(test_progressops
    tests/test_progressops.cpp
    Source/ProgressOps.cpp)
target_include_directories(test_progressops PRIVATE Source)

# Hotkey 3 関数。純粋ロジック（キー名パース・シグネチャ・空レジストリ）を検証する。
# 実ホットキー登録は OS 全体に影響するのでテストでは行わない（実機確認は実機 / FileMaker Pro）。
add_executable(test_hotkeyops
    tests/test_hotkeyops.cpp
    Source/HotkeyOps.cpp)
target_include_directories(test_hotkeyops PRIVATE Source)
if(APPLE)
    target_link_libraries(test_hotkeyops PRIVATE "-framework Carbon")
endif()

enable_testing()
add_test(NAME shellexec COMMAND test_shellexec)
add_test(NAME powershellexec COMMAND test_powershellexec)
add_test(NAME mooerror COMMAND test_mooerror)
add_test(NAME fileops COMMAND test_fileops)
add_test(NAME hash COMMAND test_hash)
add_test(NAME zipops COMMAND test_zipops)
add_test(NAME netops COMMAND test_netops)
add_test(NAME processops COMMAND test_processops)
add_test(NAME printerops COMMAND test_printerops)
add_test(NAME progressops COMMAND test_progressops)
add_test(NAME hotkeyops COMMAND test_hotkeyops)

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
    # 成果物・ターゲット名・Info.plist 各フィールドの値。
    set(_zoo_target_name "ZooPlug")
    set(ZOO_PRODUCT       "ZooPlug")
    set(ZOO_PRODUCT_LOWER "zooplug")
    set(ZOO_PLUGIN_ID     "ZooP")
    set(ZOO_VERSION       "1.1.1")

    # Info.plist.in → build ディレクトリへ生成（macOS バンドルが参照する）。
    if(APPLE)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
            "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
            @ONLY)
    endif()

    set(PLUGIN_SOURCES
        Source/ZooPlug.cpp
        Source/ShellExec.cpp
        Source/ProcessRun.cpp
        Source/PowerShellExec.cpp
        Source/MooError.cpp
        Source/FileOps.cpp
        Source/HashImpl.cpp
        Source/ZipOps.cpp
        Source/NetOps.cpp
        Source/ProcessOps.cpp
        Source/PrinterOps.cpp
        Source/DialogOps.cpp
        Source/ProgressOps.cpp
        Source/HotkeyOps.cpp
        Libraries/miniz/miniz.c)

    # macOS は Dialog の AppKit 実装（Objective-C++）を追加。
    # （DialogOps.cpp は __APPLE__ では中身が空、DialogOps_mac.mm が実装する）
    if(APPLE)
        list(APPEND PLUGIN_SOURCES Source/DialogOps_mac.mm)
    endif()

    if(WIN32)
        add_library(${_zoo_target_name} SHARED ${PLUGIN_SOURCES})
        target_include_directories(${_zoo_target_name} PRIVATE Source Headers Libraries/miniz)

        # 64bit FileMaker 用プラグインの拡張子は .fmx64（接頭辞なし）
        set_target_properties(${_zoo_target_name} PROPERTIES
            OUTPUT_NAME "${_zoo_target_name}"
            PREFIX ""
            SUFFIX ".fmx64")

        # 未指定なら同梱の FMWrapper.lib を使う
        if(NOT FMX_LIB)
            set(FMX_LIB "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/win64/FMWrapper.lib")
        endif()
        if(NOT EXISTS "${FMX_LIB}")
            message(FATAL_ERROR "FMWrapper.lib not found at: ${FMX_LIB}")
        endif()
        target_link_libraries(${_zoo_target_name} PRIVATE "${FMX_LIB}")

    elseif(APPLE)
        add_library(${_zoo_target_name} MODULE ${PLUGIN_SOURCES})
        target_include_directories(${_zoo_target_name} PRIVATE Source Headers Libraries/miniz)

        # macOS の FileMaker プラグインは X.fmplugin バンドル
        set_target_properties(${_zoo_target_name} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "fmplugin"
            OUTPUT_NAME "${_zoo_target_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

        # 未指定なら同梱の FMWrapper.framework を使う
        if(NOT FMX_FRAMEWORK_DIR)
            set(FMX_FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/macOS")
        endif()
        if(NOT EXISTS "${FMX_FRAMEWORK_DIR}/FMWrapper.framework")
            message(FATAL_ERROR "FMWrapper.framework not found in: ${FMX_FRAMEWORK_DIR}")
        endif()
        target_link_libraries(${_zoo_target_name} PRIVATE
            "-F${FMX_FRAMEWORK_DIR}" "-framework FMWrapper" "-framework CoreFoundation")

    elseif(UNIX)
        # Linux（FileMaker Server）。プラグインは拡張子 .fmx の共有モジュール。
        # 公式サンプルにならい clang + libc++ でのビルドを推奨（README 参照）。
        add_library(${_zoo_target_name} MODULE ${PLUGIN_SOURCES})
        target_include_directories(${_zoo_target_name} PRIVATE Source Headers Libraries/miniz)
        set_target_properties(${_zoo_target_name} PROPERTIES
            OUTPUT_NAME "${_zoo_target_name}" PREFIX "" SUFFIX ".fmx"
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
        target_link_libraries(${_zoo_target_name} PRIVATE "${FMX_LIB}")

    else()
        message(FATAL_ERROR "Unsupported platform for the plug-in target (the test target builds anywhere).")
    endif()

    # プラットフォームライブラリ:
    #   Net   : Windows=WinINet（OS 同梱）/ POSIX=libcurl
    #   Print : Windows=winspool / POSIX=CUPS
    #   Dialog: Windows=comdlg32+shell32+ole32 / macOS=AppKit+Foundation
    #   Hotkey: Windows=user32（RegisterHotKey/CreateWindowEx）/ macOS=Carbon
    #   Process は追加リンク不要（Toolhelp32 は kernel32、libproc/POSIX は標準）。
    if(WIN32)
        target_link_libraries(${_zoo_target_name} PRIVATE wininet winspool comdlg32 shell32 ole32 user32)
    elseif(APPLE)
        find_package(CURL REQUIRED)
        target_link_libraries(${_zoo_target_name} PRIVATE CURL::libcurl cups
            "-framework AppKit" "-framework Foundation" "-framework Carbon")
    else()
        find_package(CURL REQUIRED)
        target_link_libraries(${_zoo_target_name} PRIVATE CURL::libcurl cups)
    endif()

    message(STATUS "Building ZooPlug (zoo_* prefix + extensions)")
endif()
