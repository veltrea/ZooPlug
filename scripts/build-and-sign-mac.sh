#!/usr/bin/env bash
# Build the ZooPlug fmplugin for macOS, ad-hoc deep-sign it, and stage it in dist/.
#
# ad-hoc 署名（Apple Developer 不要）の鉄則 — FloatingMacro で確立済み:
#   1. すべての同梱物が配置し終わってから deep sign する（途中で再パッケージすると
#      署名シールが壊れ、TCC は不正署名のアプリの権限要求を登録しない）。
#   2. ad-hoc では Apple のタイムスタンプ鯖を使わない（--timestamp=none）。
#   3. 検証は浅い `codesign --verify <bundle>` で十分（--deep --strict は framework の
#      symlink 警告が出ることがあるが、FileMaker が見るのは主実行ファイルの署名）。
#
# Usage:
#   bash scripts/build-and-sign-mac.sh                # ZooPlug.fmplugin (zoo_* + 拡張)
#   INSTALL=1 bash scripts/build-and-sign-mac.sh      # 同時に ~/.../FileMaker/Extensions/ へ配置
#
# Part of ZooPlug. License: see License.txt

set -u -o pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

TARGET_NAME="ZooPlug"
BUILD_DIR="$REPO_ROOT/build-mac"
CMAKE_EXTRA=""

DIST_DIR="$REPO_ROOT/dist"
BUNDLE="${TARGET_NAME}.fmplugin"
INSTALL_TARGET="$HOME/Library/Application Support/FileMaker/Extensions"

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m   ok\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m!! \033[0m %s\n' "$*" >&2; }

cd "$REPO_ROOT"

# 1. CMake 構成（BUILD_PLUGIN=ON + バリアント）
# universal (arm64 + x86_64) でビルドする。FMWrapper.framework が両アーキを含むので
# Intel / Apple Silicon の両方で動く .fmplugin になる（指定しないとホスト arch 単一になる）。
say "Configuring CMake (variant: $TARGET_NAME, universal arm64+x86_64)"
cmake -B "$BUILD_DIR" -DBUILD_PLUGIN=ON -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" $CMAKE_EXTRA >/dev/null
ok "configured"

# 2. ビルド
say "Building ${BUNDLE}"
cmake --build "$BUILD_DIR" --target "$TARGET_NAME" 2>&1 | tail -5
[ -d "$BUILD_DIR/$BUNDLE" ] || { err "build did not produce $BUNDLE"; exit 1; }
ok "built"

# 3. dist/ へコピー
say "Staging to dist/"
mkdir -p "$DIST_DIR"
rm -rf "$DIST_DIR/$BUNDLE"
cp -R "$BUILD_DIR/$BUNDLE" "$DIST_DIR/$BUNDLE"
ok "staged at $DIST_DIR/$BUNDLE"

# 4. Ad-hoc deep sign（全同梱が済んだ後で 1 回だけ）
say "Ad-hoc deep signing"
codesign --sign - --deep --force --timestamp=none "$DIST_DIR/$BUNDLE" 2>&1 | tail -2
ok "signed"

# 5. 浅い検証（実用上はこれで十分。--deep --strict は framework の symlink 警告が出る）
say "Verifying signature"
codesign --verify "$DIST_DIR/$BUNDLE" && ok "shallow verify passed" || { err "verify failed"; exit 1; }
codesign -dvv "$DIST_DIR/$BUNDLE" 2>&1 | grep -E '^(Identifier|Authority|Signature|TeamIdentifier|Hash type|Format)' || true

# 6. リンク確認（システム依存先が想定どおりか）
say "Linkage (otool)"
otool -L "$DIST_DIR/$BUNDLE/Contents/MacOS/$TARGET_NAME" 2>/dev/null | grep -iE 'curl|cups|AppKit|Foundation|Carbon|FMWrapper' | sed 's/^/   /'

# 7. （任意）FileMaker Extensions へ配置
if [ "${INSTALL:-0}" = "1" ]; then
    say "Installing to $INSTALL_TARGET"
    mkdir -p "$INSTALL_TARGET"
    rm -rf "$INSTALL_TARGET/$BUNDLE"
    cp -R "$DIST_DIR/$BUNDLE" "$INSTALL_TARGET/$BUNDLE"
    ok "installed (restart FileMaker to load)"
fi

echo
say "Done. $DIST_DIR/$BUNDLE is ready."
echo "  配布手順（受け取り側）:"
echo "    1. .fmplugin をダウンロードし quarantine を外す:"
echo "         xattr -dr com.apple.quarantine $BUNDLE"
echo "    2. ~/Library/Application Support/FileMaker/Extensions/ にコピー"
echo "    3. FileMaker Pro を再起動 → 環境設定 > プラグイン で有効化"
