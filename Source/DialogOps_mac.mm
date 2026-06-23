// DialogOps_mac.mm
//
// Dialog 3 関数の macOS 実装（AppKit）。詳細は DialogOps.h を参照。
// このファイルは __APPLE__ でのみ中身を持つ（CMake が .mm を Objective-C++ としてビルド）。
//
// Part of ZooPlug. License: see License.txt

#ifdef __APPLE__

#import <AppKit/AppKit.h>
#include "DialogOps.h"

#include <string>

namespace zoo {

namespace {

// AppKit はメインスレッド専用。プラグインが別スレッドから呼ばれても安全なように
// メインキューへ同期ディスパッチする（Pro では計算スレッド＝メインなので直実行）。
void RunOnMain(dispatch_block_t block)
{
    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

std::string NSStringToUtf8(NSString* s)
{
    if (!s) return std::string();
    const char* c = [s UTF8String];
    return c ? std::string(c) : std::string();
}

} // namespace

int DialogColour(bool /*full*/, std::string& /*colour_out*/)
{
    // macOS には Windows の ChooseColor 相当の「モーダル一発カラーピッカー」が無い
    // （NSColorPanel は非モーダル）。Windows が主対象なので未対応＝キャンセル扱い(Err_2)。
    // TODO-compat: 必要なら自前のモーダルウィンドウ + NSColorWell を作る。
    return 2;
}

int DialogFile(bool open, const std::string& title, const std::string& default_name,
               std::string& path_out)
{
    __block int rc = 2;
    __block std::string result;
    NSString* nsTitle = title.empty() ? nil : [NSString stringWithUTF8String:title.c_str()];
    NSString* nsDef   = default_name.empty() ? nil : [NSString stringWithUTF8String:default_name.c_str()];

    RunOnMain(^{
        @autoreleasepool {
            if (open) {
                NSOpenPanel* p = [NSOpenPanel openPanel];
                p.canChooseFiles = YES;
                p.canChooseDirectories = NO;
                p.allowsMultipleSelection = NO;
                if (nsTitle) p.message = nsTitle;
                if (nsDef)   p.nameFieldStringValue = nsDef;
                if ([p runModal] == NSModalResponseOK) {
                    result = NSStringToUtf8(p.URL.path);
                    rc = 0;
                }
            } else {
                NSSavePanel* p = [NSSavePanel savePanel];
                if (nsTitle) p.message = nsTitle;
                if (nsDef)   p.nameFieldStringValue = nsDef;
                if ([p runModal] == NSModalResponseOK) {
                    result = NSStringToUtf8(p.URL.path);
                    rc = 0;
                }
            }
        }
    });

    if (rc == 0) path_out = result;
    return rc;
}

int DialogFolder(const std::string& title, bool new_folder, std::string& path_out)
{
    __block int rc = 2;
    __block std::string result;
    NSString* nsTitle = title.empty() ? nil : [NSString stringWithUTF8String:title.c_str()];

    RunOnMain(^{
        @autoreleasepool {
            NSOpenPanel* p = [NSOpenPanel openPanel];
            p.canChooseFiles = NO;
            p.canChooseDirectories = YES;
            p.allowsMultipleSelection = NO;
            p.canCreateDirectories = new_folder ? YES : NO;
            if (nsTitle) p.message = nsTitle;
            if ([p runModal] == NSModalResponseOK) {
                NSString* path = p.URL.path;
                if (path) { result = NSStringToUtf8(path); rc = 0; }
                else      { rc = 3; }
            }
        }
    });

    if (rc == 0) path_out = result;
    return rc;
}

} // namespace zoo

#endif // __APPLE__
