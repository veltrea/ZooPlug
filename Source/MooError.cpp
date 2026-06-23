// MooError.cpp — MakeMooError / MooErrorDetail の実装
// Part of ZooPlug. License: see License.txt
//
// エラー表は ZooPlug 独自の実装として整備したもの。各関数が不正引数等で返す
// `Moo_X|Err_N` 形式のコードに対応する説明文を定義する。公開ドキュメントの
// Error Descriptions ページ・各関数詳細ページを参照しつつ、実際の動作と
// ドキュメントの記載が食い違う箇所は観測した挙動に合わせ、コメントで注記する。

#include "MooError.h"

#include <unordered_map>

namespace zoo {

std::string MakeMooError(const std::string& function_name, int error_number) {
    return function_name + "|Err_" + std::to_string(error_number);
}

namespace {

// 説明文の使い回しが多いので、頻出文字列に短い別名を付ける
const char* const kArgs       = "Invalid number of arguments.";
const char* const kCancelled  = "Dialog cancelled by user.";
const char* const kSrcFile    = "Invalid source file input.";
const char* const kSrcFolder  = "Invalid source folder input.";

const std::unordered_map<std::string, std::string>& ErrorTable() {
    static const std::unordered_map<std::string, std::string> table = {
        // ---- Dialog ----
        { "Moo_DialogColour|Err_1", kArgs },
        { "Moo_DialogColour|Err_2", kCancelled },
        { "Moo_DialogFile|Err_1", kArgs },
        // DialogFile キャンセル時の戻りは "MooDialogFile|Err_2.<code>" として
        // 観測される（Moo_ の _ 抜け + 数値サフィックス付き）が、ErrorDetail の表は正規形を引く
        { "Moo_DialogFile|Err_2", kCancelled },
        { "Moo_DialogFolder|Err_1", kArgs },
        { "Moo_DialogFolder|Err_2", kCancelled },
        { "Moo_DialogFolder|Err_3", "Error retrieving chosen path." },

        // ---- Download ----
        { "Moo_DownloadFile|Err_1", kArgs },
        { "Moo_DownloadFile|Err_2", "Invalid input URL." },
        { "Moo_DownloadFile|Err_3", "File download failed." },
        // ドキュメントは "User cancelled download." だが観測される文言はこちら
        { "Moo_DownloadFile|Err_4", "File download cancelled by user." },
        { "Moo_DownloadFile|Err_5", "Local path doesn't exist." },
        { "Moo_DownloadText|Err_1", kArgs },
        { "Moo_DownloadText|Err_2", "Invalid input URL." },
        { "Moo_DownloadText|Err_3", "File download failed." },
        { "Moo_DownloadText|Err_4", "Error opening file." },
        { "Moo_DownloadText|Err_5", "Error reading file." },
        // Moo_DownloadText|Err_6 は戻り値としてのみ観測され、対応する説明文は不明（未登録）

        // ---- File ----
        { "Moo_FileCopy|Err_1", kArgs },
        { "Moo_FileCopy|Err_2", kSrcFile },
        { "Moo_FileCopy|Err_3", "Source file does not exist." },
        { "Moo_FileCopy|Err_4", "Invalid destination file input." },
        { "Moo_FileCopy|Err_5", "Destination file already exists." },
        { "Moo_FileCopy|Err_6", "Error copying file." },
        // Moo_ErrorDetail の応答順から推定（bOverwrite で既存先を消す処理の失敗）。
        // FileMove|Err_7 と違い "destination" 無し。TODO-compat: 動作確認
        { "Moo_FileCopy|Err_7", "Error deleting file." },
        { "Moo_FileDelete|Err_1", kArgs },
        { "Moo_FileDelete|Err_2", kSrcFile },
        { "Moo_FileDelete|Err_3", "Error deleting file." },
        { "Moo_FileDelete|Err_4", "Source file doesn't exist." },
        { "Moo_FileExists|Err_1", kArgs },
        { "Moo_FileExists|Err_2", kSrcFile },
        { "Moo_FileInfo|Err_1", kArgs },
        { "Moo_FileInfo|Err_2", kSrcFile },
        { "Moo_FileInfo|Err_3", "Source file doesn't exist." },
        { "Moo_FileInfo|Err_4", "Unknown sInfo parameter." },
        { "Moo_FileInfo|Err_5", "Unknown sOptions parameter." },
        { "Moo_FileInfo|Err_6", "Error retrieving file info." },
        { "Moo_FileInfo|Err_7", "Error setting file info." },
        { "Moo_FileMove|Err_1", kArgs },
        { "Moo_FileMove|Err_2", kSrcFile },
        { "Moo_FileMove|Err_3", "Source file does not exist." },
        { "Moo_FileMove|Err_4", "Invalid destination file input." },
        { "Moo_FileMove|Err_5", "Destination file already exists." },
        { "Moo_FileMove|Err_6", "Error moving file." },
        { "Moo_FileMove|Err_7", "Error deleting destination file." },
        { "Moo_FileRead|Err_1", kArgs },
        { "Moo_FileRead|Err_2", kSrcFile },
        { "Moo_FileRead|Err_3", "Error opening input file." },
        { "Moo_FileRead|Err_4", "Error creating input buffer." },
        { "Moo_FileRead|Err_5", "Error reading file." },
        { "Moo_FileWrite|Err_1", kArgs },
        { "Moo_FileWrite|Err_2", kSrcFile },
        { "Moo_FileWrite|Err_3", "Source file already exists." },
        { "Moo_FileWrite|Err_4", "Error opening file." },
        { "Moo_FileWrite|Err_5", "Error writing to file." },
        // ドキュメントの Moo_FileWrite|Err_6 (Error removing file.) は 0.4.9 では
        // 現れない（bOverwrite 引数ごと消えた 0.4.7 時代の名残）ので登録しない

        // ---- Folder ----
        { "Moo_FolderCopy|Err_1", kArgs },
        { "Moo_FolderCopy|Err_2", kSrcFolder },
        { "Moo_FolderCopy|Err_3", "Source folder does not exist." },
        { "Moo_FolderCopy|Err_4", "Invalid destination folder input." },
        { "Moo_FolderCopy|Err_5", "Destination folder already exists." },
        // 戻りは "Moo_FolderCopy|Err_6.<n>  (<hex>)" 形式として観測されるが表は正規形
        { "Moo_FolderCopy|Err_6", "Error copying folder." },
        { "Moo_FolderCreate|Err_1", kArgs },
        { "Moo_FolderCreate|Err_2", kSrcFolder },
        { "Moo_FolderCreate|Err_3", "Source folder already exists." },
        { "Moo_FolderCreate|Err_4", "Error creating folder." },
        { "Moo_FolderDelete|Err_1", kArgs },
        { "Moo_FolderDelete|Err_2", "Invalid folder input." },
        { "Moo_FolderDelete|Err_3", "Input folder does not exist." },
        { "Moo_FolderDelete|Err_4", "Error deleting folder." },
        { "Moo_FolderExists|Err_1", kArgs },
        { "Moo_FolderExists|Err_2", kSrcFolder },
        { "Moo_FolderList|Err_1", kArgs },
        { "Moo_FolderList|Err_2", kSrcFolder },
        { "Moo_FolderList|Err_3", "Input folder contains no files." },
        // Err_4/Err_5 はドキュメントに無い（Moo_ErrorDetail の応答順から復元）
        { "Moo_FolderList|Err_4", "Input folder doesn't exist." },
        { "Moo_FolderList|Err_5", "Unknown error." },
        { "Moo_FolderMove|Err_1", kArgs },
        { "Moo_FolderMove|Err_2", kSrcFolder },
        { "Moo_FolderMove|Err_3", "Source folder does not exist." },
        { "Moo_FolderMove|Err_4", "Invalid destination folder input." },
        { "Moo_FolderMove|Err_5", "Destination folder already exists." },
        { "Moo_FolderMove|Err_6", "Error moving folder." },

        // ---- FTP ----
        { "Moo_FTPDownload|Err_1", kArgs },
        { "Moo_FTPDownload|Err_2", kArgs }, // Err_1 と同文（原文ママ）
        { "Moo_FTPDownload|Err_3", "Invalid sServer input parameter." },
        { "Moo_FTPDownload|Err_4", "Invalid sUser input parameter." },
        { "Moo_FTPDownload|Err_5", "Invalid sPassword input parameter." },
        { "Moo_FTPDownload|Err_6", "Error opening internet connection." },
        { "Moo_FTPDownload|Err_7", "Error opening ftp connection." },
        // Err_8/9 のドキュメント文言は FTPUpload からのコピペ。観測される応答順では
        // ローカル保存先まわりの 2 つがここに初出する
        { "Moo_FTPDownload|Err_8", "Local file already exists." },
        { "Moo_FTPDownload|Err_9", "Invalid local file input." },
        { "Moo_FTPDownload|Err_10", "Remote file not found on FTP server." },
        { "Moo_FTPDownload|Err_11", "Error downloading file." },
        { "Moo_FTPDownload|Err_12", "Error retrieving remote file size." },
        { "Moo_FTPDownload|Err_13", "Error opening local file." },
        { "Moo_FTPDownload|Err_14", "Unknown error downloading file." },
        { "Moo_FTPUpload|Err_1", kArgs },
        { "Moo_FTPUpload|Err_2", kArgs }, // Err_1 と同文（原文ママ）
        { "Moo_FTPUpload|Err_3", "Invalid sServer input parameter." },
        { "Moo_FTPUpload|Err_4", "Invalid sUser input parameter." },
        { "Moo_FTPUpload|Err_5", "Invalid sPassword input parameter." },
        { "Moo_FTPUpload|Err_6", "Error opening internet connection." },
        { "Moo_FTPUpload|Err_7", "Error opening ftp connection." },
        { "Moo_FTPUpload|Err_8", "Source file input doesn't exist." },
        { "Moo_FTPUpload|Err_9", kSrcFile },
        { "Moo_FTPUpload|Err_10", "Error setting remote ftp directory." },
        { "Moo_FTPUpload|Err_11", "File already exists on ftp server." },
        { "Moo_FTPUpload|Err_12", "Error opening local source file." },
        { "Moo_FTPUpload|Err_13", "Error reading local source file." },
        { "Moo_FTPUpload|Err_14", "Upload file size mismatch." },
        { "Moo_FTPUpload|Err_15", "Error deleting existing file on FTP Server." },
        { "Moo_FTPUpload|Err_16", "Unknown error uploading file." },
        // Moo_FTPDelete|Err_1〜8 は ErrorDetail で説明が引けない（未登録のまま）

        // ---- Hash ----
        { "Moo_Hash|Err_1", kArgs },
        { "Moo_Hash|Err_2", "Unknown hash input." },
        { "Moo_Hash|Err_3", "Invalid input." },
        // 末尾ピリオド無しは観測された戻り文ママ
        { "Moo_Hash|Err_4", "Input file doesn't exist" },
        { "Moo_Hash|Err_5", "Error generating hash" },

        // ---- Hotkey ----
        { "Moo_HotkeyAdd|Err_1", kArgs },
        { "Moo_HotkeyAdd|Err_2", "Unknown hotkey specified." },
        { "Moo_HotkeyAdd|Err_3", "Error creating hotkey window." },
        { "Moo_HotkeyAdd|Err_4", "Hotkey already registered." },
        { "Moo_HotkeyAdd|Err_5", "Error registering hotkey." },
        { "Moo_HotkeyList|Err_1", kArgs },
        { "Moo_HotkeyList|Err_2", "No hotkeys currentley set." }, // "currentley" は原文ママ
        { "Moo_HotkeyList|Err_3", "Error retrieving list of hotkeys." },
        { "Moo_HotkeyRemove|Err_1", kArgs },
        { "Moo_HotkeyRemove|Err_2", "No hotkeys currentley set." },
        { "Moo_HotkeyRemove|Err_3", "Unknown hotkey specified." },
        { "Moo_HotkeyRemove|Err_4", "Hotkey is not registered." },
        { "Moo_HotkeyRemove|Err_5", "Error unregistering hotkey." },

        // ---- Printer ----
        { "Moo_PrinterDefault|Err_1", kArgs },
        { "Moo_PrinterDefault|Err_2", "Error retrieving default printer." },
        { "Moo_PrinterDefault|Err_3", "Error setting default printer." },
        { "Moo_PrinterList|Err_1", kArgs },
        { "Moo_PrinterList|Err_2", "Error retrieving list of printers." },
        { "Moo_PrinterList|Err_3", "No printers installed." },

        // ---- Process ----
        { "Moo_ProcessCount|Err_1", kArgs },
        { "Moo_ProcessKill|Err_1", kArgs },
        { "Moo_ProcessKill|Err_2", "Error terminating process." },
        // Moo_ProcessKill|Err_3 は戻り値としてのみ観測され、対応する説明文は不明（未登録）
        { "Moo_ProcessList|Err_1", kArgs },
        { "Moo_ProcessRunning|Err_1", kArgs },
        { "Moo_ProcessRunning|Err_2", "Invalid input process." },

        // ---- Shell（未公開関数。Error Descriptions ページ 0.4.7 の記載を採用。
        //      0.4.9 の ErrorDetail では説明が引けないが、説明があった方が有用） ----
        { "Moo_Shell|Err_1", kArgs },
        { "Moo_Shell|Err_2", "Invalid input process." },
        // Moo_Shell|Err_3 は対応する説明文が不明（未登録）

        // ---- Zip ----
        { "Moo_ZipCompress|Err_1", kArgs },
        { "Moo_ZipCompress|Err_2", kSrcFile },
        { "Moo_ZipCompress|Err_3", "Source file doesn't exist." },
        // ドキュメントは "Output Zip file already exists." だが観測される文言はこちら
        { "Moo_ZipCompress|Err_4", "File already exists in Zip archive." },
        { "Moo_ZipCompress|Err_5", "Error creating Zip file." },
        // Err_6 はドキュメントに無い（観測でのみ確認）
        { "Moo_ZipCompress|Err_6", "Output folder doesn't exist." },
        { "Moo_ZipExtract|Err_1", kArgs },
        { "Moo_ZipExtract|Err_2", "Invalid source Zip file input." },
        { "Moo_ZipExtract|Err_3", "Source Zip file doesn't exist." },
        { "Moo_ZipExtract|Err_4", "Error opening source Zip file." },
        { "Moo_ZipExtract|Err_5", "Empty Zip file." },
        { "Moo_ZipExtract|Err_6", "Error extracting Zip file." },
        { "Moo_ZipExtract|Err_7", "Error reading Zip headers." },
        { "Moo_ZipExtract|Err_8", "Error extracting Zip file." },
        { "Moo_ZipExtract|Err_9", "Error extracting Zip file." },
        { "Moo_ZipExtract|Err_10", "Error extracting Zip file." },
        { "Moo_ZipExtract|Err_11", "Error extracting Zip file." },
        { "Moo_ZipExtract|Err_12", "Password Required to Extract File." },
        { "Moo_ZipList|Err_1", kArgs },
        { "Moo_ZipList|Err_2", "Invalid source Zip file input." },
        { "Moo_ZipList|Err_3", "Source Zip file doesn't exist." },
        // 末尾ピリオド無しは観測された戻り文ママ（ZipExtract|Err_5 とは別リテラル）
        { "Moo_ZipList|Err_4", "Empty Zip file" },
        { "Moo_ZipList|Err_5", "Error listing Zip contents." },
    };
    return table;
}

} // namespace

std::string MooErrorDetail(const std::string& error_code) {
    const auto& table = ErrorTable();
    const auto it = table.find(error_code);
    if (it == table.end()) {
        return std::string();
    }
    return it->second;
}

} // namespace zoo
