/*
 ZooPlug.cpp
 ZooPlug — a FileMaker plug-in that reproduces MooPlug's moo_shell().

 構造は Mark Banks の SimplePlugin テンプレートを土台にしている。
   http://banks.id.au/filemaker/plugins/simpleplugin/
 プラグインの土台部分（登録・GetString 等）はほぼ SimplePlugin のまま、
 提供する関数だけを moo_shell に差し替えている。

 License: see License.txt
*/

#define FMX_USE_UNIQUE_PTR 1

#include <FMWrapper/FMXBinaryData.h>
#include <FMWrapper/FMXCalcEngine.h>
#include <FMWrapper/FMXClient.h>
#include <FMWrapper/FMXData.h>
#include <FMWrapper/FMXDateTime.h>
#include <FMWrapper/FMXExtern.h>
#include <FMWrapper/FMXFixPt.h>
#include <FMWrapper/FMXText.h>
#include <FMWrapper/FMXTextStyle.h>
#include <FMWrapper/FMXTypes.h>

// FileMaker 11 SDK 互換シム。FM Pro 11 で動く 32bit プラグインは FM11 世代の
// FMWrapper(PlugInSDK11)にリンクする必要がある(新しい SDK は FM11 の FMWrapper.dll
// に無いシンボルを import してロード不能になる)。FM11 SDK は新しい *UniquePtr /
// fmx::ptrtype を持たず、auto_ptr ベースの *AutoPtr と FMX_Long(result)を使うので、
// FM11 ビルド時(/DFM11_SDK=1)だけ新 SDK の名前を橋渡しする。本コードはこれらを
// ローカル RAII と値返し(FunctionName)でしか使わず、auto_ptr の所有権移譲セマンティクス
// でも正しく動く。
#ifdef FM11_SDK
namespace fmx {
	typedef TextAutoPtr      TextUniquePtr;
	typedef QuadCharAutoPtr  QuadCharUniquePtr;
	typedef FixPtAutoPtr     FixPtUniquePtr;
	typedef DateTimeAutoPtr  DateTimeUniquePtr;
	typedef DataAutoPtr      DataUniquePtr;
	typedef LocaleAutoPtr    LocaleUniquePtr;
	typedef long             ptrtype;   // FM11 の FMExternCallStruct::result は FMX_Long(32bit)
	// FM11 SDK は固定幅整数の別名(uint32 等)を持たないので定義する。
	typedef short              int16;
	typedef unsigned short     uint16;
	typedef int                int32;
	typedef unsigned int       uint32;
	typedef long long          int64;
}
#endif

#include <string>
#include <vector>

#include "ShellExec.h"
#include "PowerShellExec.h"
#include "MooError.h"
#include "FileOps.h"
#include "HashImpl.h"
#include "ZipOps.h"
#include "NetOps.h"
#include "ProcessOps.h"
#include "PrinterOps.h"
#include "DialogOps.h"
#include "ProgressOps.h"
#include "HotkeyOps.h"
#include "Variant.h"

// Windows 32bit (x86) では FileMaker plug-in 関数は __stdcall を要求する。
// 64bit (x64) では呼び出し規約は MS x64 calling convention に統一されており
// __cdecl / __stdcall の区別が無くなるので空マクロでよい。Mac/Linux も空。
#if defined(_MSC_VER) && !defined(_WIN64)
#define FMX_CALL __stdcall
#else
#define FMX_CALL
#endif

#ifdef _MSC_VER
// #pragma mark は Xcode のコード折り畳み用。MSVC は知らないので C4068 を黙らせる。
#pragma warning(disable: 4068)
#endif

// プラグイン名 / 4 文字 ID / 関数プレフィックス / バージョンは Variant.h で定義。


#pragma mark -
#pragma mark Prototypes
#pragma mark -

const fmx::ptrtype Init ( FMX_ExternCallPtr pb );
void Shutdown ( void );

const fmx::QuadCharUniquePtr PluginID ( void );
const fmx::TextUniquePtr PluginPrefix ( void );
const fmx::TextUniquePtr PluginOptionsString ( void );

void GetString ( FMX_ExternCallPtr pb );
const fmx::errcode RegisterFunction ( short func_id, const std::string prototype, const fmx::ExtPluginType function, const std::string description = "", short min_override = -1, short max_override = -1 );
void UnregisterFunctions ( void );
const fmx::TextUniquePtr FunctionName ( const fmx::TextUniquePtr& signature );
void NumberOfParameters ( const fmx::TextUniquePtr& signature, short& required, short& optional );


// pragma mark generates a warning C4068 in Visual Studio so that warning is disabled in VS

#pragma mark -
#pragma mark Enums, Defines & Globals
#pragma mark -

enum {
	kSPOptionsString = 1
};


enum {
	kSPManyParameters = -1,
	kSPPrefixLength = 4,
	kSPPluginIDLength = 5,	//	kSPPrefixLength = 4 + 1
	kSPFirstFunction = 1000
};


enum {
	kSPFirstParameter = 0,
	kSPSecondParameter = 1,
	kSPThirdParameter = 2
};


enum errors {
	kSPNoError = 0,
};


// Globals

short g_next_function;	// used to keep track of the function id number
std::vector<short> g_registered_func_ids;	// 実際に登録した funcId 群(Shutdown で解除するため)


#pragma mark -
#pragma mark Functions
#pragma mark -

/* ****************************************************************************

 プラグイン関数はここに書く。各関数は Init 内で RegisterFunction を使って
 登録すること。

 **************************************************************************** */

// fmx::Text を UTF-8 の std::string として取り出す
static std::string TextAsUTF8 ( const fmx::Text& text )
{
	const fmx::uint32 size = text.GetSize();	// Unicode 文字数
	if ( size == 0 ) {
		return std::string();
	}
	// UTF-8 は 1 文字あたり最大 4 バイト。終端 null の分を足して確保する。
	std::vector<char> buffer ( static_cast<std::size_t>(size) * 4 + 1, '\0' );
	text.GetBytes ( buffer.data(), static_cast<fmx::uint32>(buffer.size()), 0, size, fmx::Text::kEncoding_UTF8 );
	return std::string ( buffer.data() );	// null 終端まで
}


// UTF-8 テキストを戻り値にする
static void SetReplyText ( fmx::Data& reply, const std::string& utf8 )
{
	fmx::TextUniquePtr text;
	if ( !utf8.empty() ) {
		text->Assign ( utf8.c_str(), fmx::Text::kEncoding_UTF8 );
	}
	fmx::LocaleUniquePtr default_locale;
	reply.SetAsText ( *text, *default_locale );
}


// 数値を戻り値にする。MooPlug の「成功 = true」「真偽値」は数値 1 / 0
//（真偽値・件数は数値 1/0 で返す設計。"True"/"False" の文字列は返さない）
static void SetReplyNumber ( fmx::Data& reply, fmx::int64 value )
{
	// FM Pro 11 の FMWrapper.dll は FM_FixPt_AssignInt64 を export しない(FM12+)。
	// ZooPlug が返すのは真偽/件数だけなので int32(AssignInt) で十分。
	fmx::FixPtUniquePtr number;
	number->AssignInt ( static_cast<fmx::int32>( value ) );
	reply.SetAsNumber ( *number );
}


// MooPlug 形式のエラー文字列 "Moo_関数名|Err_N" を戻り値にする
static void SetReplyMooError ( fmx::Data& reply, const char* function_name, int error_number )
{
	SetReplyText ( reply, zoo::MakeMooError ( function_name, error_number ) );
}


// ASCII の大文字小文字を畳む（sInfo / sOptions の比較用）
static std::string FoldAsciiLower ( const std::string& s )
{
	std::string out = s;
	for ( char& c : out ) {
		if ( c >= 'A' && c <= 'Z' ) {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return out;
}


/*
 moo_shell ( command )

 第 1 引数のコマンドをワンライナーとしてシェルで実行し、標準出力＋標準エラーを
 テキストで返す。MooPlug の moo_shell の再現。
   Windows : cmd.exe /S /C "<command>"
   macOS   : /bin/sh -c "<command>"
 改行は CR に正規化され、末尾の改行は取り除かれる。
*/
fmx::errcode FMX_CALL Moo_Shell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply );

fmx::errcode FMX_CALL Moo_Shell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	fmx::errcode error = kSPNoError;

	// 引数が無ければ空文字列を返す
	if ( parameters.Size() < 1 ) {
		fmx::TextUniquePtr empty;
		fmx::LocaleUniquePtr empty_locale;
		reply.SetAsText ( *empty, *empty_locale );
		return error;
	}

	// 第 1 引数（コマンド）を UTF-8 で取り出し、純粋ロジックで実行する
	const std::string command = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string output = zoo::RunShellCommand ( command );

	// 結果を UTF-8 として FileMaker に返す
	fmx::TextUniquePtr result_text;
	if ( !output.empty() ) {
		result_text->Assign ( output.c_str(), fmx::Text::kEncoding_UTF8 );
	}

	fmx::LocaleUniquePtr default_locale;
	reply.SetAsText ( *result_text, *default_locale );

	return error;

} // Moo_Shell


/*
 zoo_powershell ( command { ; bCore } )

 第 1 引数のスクリプトを PowerShell で実行し、標準出力＋標準エラーを UTF-8 テキストで返す。
 ZooPlug 独自関数（moo_shell の cmd に対する PowerShell 版）。
   command : PowerShell スクリプト／ワンライナー（UTF-8・複数行可）
   bCore   : 省略可・既定 False。True=PowerShell 7(pwsh) / False=Windows PowerShell 5.1
             （macOS/Linux には 5.1 が無いので常に pwsh）
 実装は docs/zoo-powershell-design.md §18 の「テンポラリファイル方式」:
   temp.ps1(UTF-8 BOM) に書き -File で渡し、出力は Out-File -Encoding utf8 でファイルへ → 読む。
 これにより全 PowerShell 版・FullLanguage/ConstrainedLanguage（WDAC enforce 下含む）で
 UTF-8 往復が壊れない（§21/§22 で実機実証）。改行は CR 正規化・末尾改行除去（moo_shell と同じ）。
 出力エンコーディングは Out-File が常に UTF-8 にするため、moo_shell の CP932 復号とは対照的に
 sEncoding 引数を持たない（必要になれば末尾省略可引数として追加可能）。
*/
fmx::errcode FMX_CALL Zoo_PowerShell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply );

fmx::errcode FMX_CALL Zoo_PowerShell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	fmx::errcode error = kSPNoError;

	// 引数が無ければ空文字列を返す
	if ( parameters.Size() < 1 ) {
		fmx::TextUniquePtr empty;
		fmx::LocaleUniquePtr empty_locale;
		reply.SetAsText ( *empty, *empty_locale );
		return error;
	}

	// 第 1 引数（スクリプト）を UTF-8 で取り出す
	const std::string command = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );

	// 第 2 引数 bCore（省略可）
	zoo::PowerShellOptions opts;
	if ( parameters.Size() >= 2 ) {
		opts.use_core = parameters.AtAsBoolean ( kSPSecondParameter );
	}

	const std::string output = zoo::RunPowerShell ( command, opts );

	// 結果を UTF-8 として FileMaker に返す
	fmx::TextUniquePtr result_text;
	if ( !output.empty() ) {
		result_text->Assign ( output.c_str(), fmx::Text::kEncoding_UTF8 );
	}

	fmx::LocaleUniquePtr default_locale;
	reply.SetAsText ( *result_text, *default_locale );

	return error;

} // Zoo_PowerShell


/* ****************************************************************************

 MooPlug 互換関数（Tier A）

 シグネチャ・エラーコード・戻り値は MooPlug 0.4.9 の挙動観察に合わせてある
 （docs/zoo-plug-implementation-spec.md）。
 純粋ロジックは Source/FileOps.* / HashImpl.* / MooError.* にあり、
 ここでは引数の取り出しと戻り値の整形だけを行う。

 **************************************************************************** */

/*
 zoo_Version — ZooPlug のバージョン文字列を返す（引数なし）
*/
fmx::errcode FMX_CALL Moo_Version ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& /* parameters */, fmx::Data& reply )
{
	SetReplyText ( reply, zoo::kVariant.version );
	return kSPNoError;
}


/*
 Moo_ErrorDetail ( sError ) — エラーコード文字列を説明文に変換する
*/
fmx::errcode FMX_CALL Moo_ErrorDetail ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	std::string detail;
	if ( parameters.Size() >= 1 ) {
		detail = zoo::MooErrorDetail ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	}
	SetReplyText ( reply, detail );
	return kSPNoError;
}


/*
 Moo_FileExists ( sFile ) — ファイルの存在を 1 / 0 で返す
*/
fmx::errcode FMX_CALL Moo_FileExists ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FileExists").c_str(), 1 ); return kSPNoError; }
	bool exists = false;
	const int err = zoo::FileExists ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ), exists );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FileExists").c_str(), err );
	else SetReplyNumber ( reply, exists ? 1 : 0 );
	return kSPNoError;
}


/*
 Moo_FileCopy ( sSource ; sDest {; bOverwrite ; bProgress } ) — ファイルをコピーする
 bProgress は受け取るだけで現状未使用（進捗ダイアログは Tier C で実装予定）
*/
fmx::errcode FMX_CALL Moo_FileCopy ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, zoo::FnName("FileCopy").c_str(), 1 ); return kSPNoError; }
	const std::string source = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string dest = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool overwrite = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::FileCopy ( source, dest, overwrite );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FileCopy").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileDelete ( sFile ) — ファイルを削除する
*/
fmx::errcode FMX_CALL Moo_FileDelete ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FileDelete").c_str(), 1 ); return kSPNoError; }
	const int err = zoo::FileDelete ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FileDelete").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileMove ( sSource ; sDest {; bOverwrite } ) — ファイルを移動する
*/
fmx::errcode FMX_CALL Moo_FileMove ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, zoo::FnName("FileMove").c_str(), 1 ); return kSPNoError; }
	const std::string source = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string dest = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool overwrite = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::FileMove ( source, dest, overwrite );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FileMove").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileRead ( sFile ) — テキストファイルを読んで内容を返す
 UTF-8 として読み、不正なら Windows では ANSI(CP932) として復号。改行は CR 正規化。
*/
fmx::errcode FMX_CALL Moo_FileRead ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FileRead").c_str(), 1 ); return kSPNoError; }
	std::string text;
	const int err = zoo::FileRead ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ), text );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FileRead").c_str(), err );
	else SetReplyText ( reply, text );
	return kSPNoError;
}


/*
 Moo_FileWrite ( sFile ; sText {; bAppend } ) — ファイルへ書き込み/追記する
 0.4.9 実機と同じ 3 引数で動作する（4 番目の引数は無視される）。
 UTF-8 で書き、改行は OS ネイティブ（Win=CRLF / 他=LF）へ変換する。
*/
fmx::errcode FMX_CALL Moo_FileWrite ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, zoo::FnName("FileWrite").c_str(), 1 ); return kSPNoError; }
	const std::string file = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string text = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool append = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::FileWrite ( file, text, append );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FileWrite").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileInfo ( sFile ; sInfo {; sOptions } ) — ファイル情報の取得・設定
   sInfo = size     : sOptions = human（既定）/ bytes
   sInfo = version  : Windows の VERSIONINFO（%d.%d.%d.%d）。他 OS は Err_6
   sInfo = created  : sOptions 省略で取得（タイムスタンプ）、タイムスタンプを渡すと設定
   sInfo = modified : 同上
*/
fmx::errcode FMX_CALL Moo_FileInfo ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const std::string kName = zoo::FnName ( "FileInfo" );
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, kName.c_str(), 1 ); return kSPNoError; }
	const std::string file = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string info = FoldAsciiLower ( TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) );

	if ( info == "size" ) {
		// 省略時の既定は human（StrFormatByteSize 風）。
		// TODO-compat: 実機 0.4.9 の省略時既定が human か bytes かは未確認（実機で照合する）
		std::string options = "human";
		if ( parameters.Size() >= 3 ) {
			const std::string given = FoldAsciiLower ( TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) );
			if ( !given.empty() ) options = given;
		}
		if ( options != "human" && options != "bytes" ) { SetReplyMooError ( reply, kName.c_str(), 5 ); return kSPNoError; }
		std::uint64_t bytes = 0;
		const int err = zoo::FileSize ( file, bytes );
		if ( err ) SetReplyMooError ( reply, kName.c_str(), err );
		else if ( options == "bytes" ) SetReplyNumber ( reply, static_cast<fmx::int64>(bytes) );
		else SetReplyText ( reply, zoo::HumanReadableSize ( bytes ) );
		return kSPNoError;
	}

	if ( info == "version" ) {
		std::string version;
		const int err = zoo::FileVersion ( file, version );
		// 注: ZooPlug はエラーコード方式（Err_6）で統一する（TODO-compat）
		if ( err ) SetReplyMooError ( reply, kName.c_str(), err );
		else SetReplyText ( reply, version );
		return kSPNoError;
	}

	if ( info == "created" || info == "modified" ) {
		const bool creation = ( info == "created" );
		if ( parameters.Size() >= 3 ) {
			// sOptions にタイムスタンプを渡すと日時を設定する（成功 = 1）
			const fmx::DateTime& ts = parameters.AtAsTimeStamp ( kSPThirdParameter );
			zoo::FileTimeParts parts;
			parts.year = ts.GetYear();
			parts.month = ts.GetMonth();
			parts.day = ts.GetDay();
			parts.hour = static_cast<int>(ts.GetHour());
			parts.minute = ts.GetMinute();
			parts.second = ts.GetSec();
			if ( parts.year <= 0 ) { SetReplyMooError ( reply, kName.c_str(), 5 ); return kSPNoError; } // タイムスタンプとして解釈できない
			const int err = zoo::FileTimeSet ( file, creation, parts );
			if ( err ) SetReplyMooError ( reply, kName.c_str(), err );
			else SetReplyNumber ( reply, 1 );
		} else {
			zoo::FileTimeParts parts;
			const int err = zoo::FileTimeGet ( file, creation, parts );
			if ( err ) { SetReplyMooError ( reply, kName.c_str(), err ); return kSPNoError; }
			fmx::DateTimeUniquePtr timestamp;
			timestamp->SetNormalizedDate ( static_cast<fmx::int16>(parts.month), static_cast<fmx::int16>(parts.day), static_cast<fmx::int16>(parts.year) );
			timestamp->SetNormalizedTime ( parts.hour, static_cast<fmx::int16>(parts.minute), static_cast<fmx::int16>(parts.second) );
			// FM Pro 11 の FMWrapper.dll は FM_Data_SetAsTimeStamp を export しない(FM12+)。
			// そのため SetAsDate を使う(0.4.9 互換として created/modified は Date 型で返す)。
			reply.SetAsDate ( *timestamp );
		}
		return kSPNoError;
	}

	SetReplyMooError ( reply, kName.c_str(), 4 ); // 未知の sInfo
	return kSPNoError;
}


/*
 Moo_FolderExists ( sFolder ) — フォルダの存在を 1 / 0 で返す
*/
fmx::errcode FMX_CALL Moo_FolderExists ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FolderExists").c_str(), 1 ); return kSPNoError; }
	bool exists = false;
	const int err = zoo::FolderExists ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ), exists );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FolderExists").c_str(), err );
	else SetReplyNumber ( reply, exists ? 1 : 0 );
	return kSPNoError;
}


/*
 Moo_FolderCopy ( sSource ; sDest ) — フォルダを再帰コピーする
*/
fmx::errcode FMX_CALL Moo_FolderCopy ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, zoo::FnName("FolderCopy").c_str(), 1 ); return kSPNoError; }
	const int err = zoo::FolderCopy ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ),
									  TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FolderCopy").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderCreate ( sFolder ) — フォルダを作成する（中間フォルダも作る）
*/
fmx::errcode FMX_CALL Moo_FolderCreate ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FolderCreate").c_str(), 1 ); return kSPNoError; }
	const int err = zoo::FolderCreate ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FolderCreate").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderDelete ( sFolder ) — フォルダを中身ごと削除する
*/
fmx::errcode FMX_CALL Moo_FolderDelete ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FolderDelete").c_str(), 1 ); return kSPNoError; }
	const int err = zoo::FolderDelete ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FolderDelete").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderMove ( sSource ; sDest ) — フォルダを移動する
*/
fmx::errcode FMX_CALL Moo_FolderMove ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, zoo::FnName("FolderMove").c_str(), 1 ); return kSPNoError; }
	const int err = zoo::FolderMove ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ),
									  TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FolderMove").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderList ( sFolder {; sPattern ; sSeparator } ) — フォルダ直下のファイル一覧
 既定: sPattern = "*.*"（すべて）、sSeparator = "|"
*/
fmx::errcode FMX_CALL Moo_FolderList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("FolderList").c_str(), 1 ); return kSPNoError; }
	const std::string folder = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string pattern = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string("*.*");
	const std::string separator = parameters.Size() >= 3 ? TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::FolderList ( folder, pattern, separator, list );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FolderList").c_str(), err );
	else SetReplyText ( reply, list );
	return kSPNoError;
}


/*
 Moo_Hash ( sHash ; sText {; bFile } ) — MD5 / SHA1 / SHA256 / SHA512 ハッシュ
 出力は小文字 16 進。bFile = True なら sText をファイルパスとして扱う。
*/
fmx::errcode FMX_CALL Moo_Hash ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, zoo::FnName("Hash").c_str(), 1 ); return kSPNoError; }
	const std::string algorithm = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string input = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool is_file = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	std::string hex;
	const int err = is_file ? zoo::HashFile ( algorithm, input, hex )
							: zoo::HashString ( algorithm, input, hex );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("Hash").c_str(), err );
	else SetReplyText ( reply, hex );
	return kSPNoError;
}


/*
 Moo_ZipCompress ( sPath {; bTemp ; bOverwrite ; bFolderName ; sPassword } ) — Zip 圧縮/追加
 bTemp は Boolean/String の二刀流（本家仕様）:
   false/省略 = 入力と同じフォルダ / true = テンポラリフォルダ /
   文字列 = 出力先（フルパス or ファイル名のみ）
 既存 Zip を指すと追加になる。sPassword は未対応（指定すると Err_5）。
*/
fmx::errcode FMX_CALL Moo_ZipCompress ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ZipCompress").c_str(), 1 ); return kSPNoError; }
	const std::string path = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );

	zoo::ZipCompressOptions options;
	if ( parameters.Size() >= 2 ) {
		const std::string temp_text = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
		const std::string folded = FoldAsciiLower ( temp_text );
		if ( temp_text.empty() || folded == "0" || folded == "false" ) {
			options.output = zoo::ZipCompressOptions::Output::SameFolder;
		} else if ( folded == "1" || folded == "true" ) {
			options.output = zoo::ZipCompressOptions::Output::TempFolder;
		} else {
			options.output = zoo::ZipCompressOptions::Output::Explicit;
			options.explicit_path_utf8 = temp_text;
		}
	}
	if ( parameters.Size() >= 3 ) options.overwrite_in_zip = parameters.AtAsBoolean ( kSPThirdParameter );
	if ( parameters.Size() >= 4 ) options.include_folder_name = parameters.AtAsBoolean ( 3 );
	if ( parameters.Size() >= 5 ) options.password_utf8 = TextAsUTF8 ( parameters.AtAsText ( 4 ) );

	std::string zip_path;
	const int err = zoo::ZipCompress ( path, options, zip_path );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ZipCompress").c_str(), err );
	else SetReplyText ( reply, zip_path );
	return kSPNoError;
}


/*
 Moo_ZipExtract ( sFile {; bTemp ; bOverwrite } ) — Zip から最初の 1 ファイルを展開
 bTemp = false（既定）なら Zip と同じフォルダへ、true ならテンポラリフォルダへ。
 複数ファイル展開は本家 0.4.9 も未対応（"a future version" のまま）。
*/
fmx::errcode FMX_CALL Moo_ZipExtract ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ZipExtract").c_str(), 1 ); return kSPNoError; }
	const std::string zip = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const bool to_temp = parameters.Size() >= 2 ? parameters.AtAsBoolean ( kSPSecondParameter ) : false;
	const bool overwrite = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	std::string extracted;
	const int err = zoo::ZipExtract ( zip, to_temp, overwrite, extracted );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ZipExtract").c_str(), err );
	else SetReplyText ( reply, extracted );
	return kSPNoError;
}


/*
 Moo_ZipList ( sZip {; sPattern ; sSeparator } ) — Zip の内容一覧
 既定: sPattern = "*.*"（すべて）、sSeparator = "|"。格納順で返す。
*/
fmx::errcode FMX_CALL Moo_ZipList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ZipList").c_str(), 1 ); return kSPNoError; }
	const std::string zip = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string pattern = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string("*.*");
	const std::string separator = parameters.Size() >= 3 ? TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::ZipList ( zip, pattern, separator, list );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ZipList").c_str(), err );
	else SetReplyText ( reply, list );
	return kSPNoError;
}


/* ****************************************************************************

 MooPlug 互換関数（Tier A・Net = Download 2 + FTP 3）

 純粋ロジックは Source/NetOps.*（Windows=WinINet / POSIX=libcurl）。
 bProgress は Tier C 待ちのため、受け取っても無視する（進捗 UI なし）。

 **************************************************************************** */

/*
 Moo_DownloadText ( sFile ) — URL の本文テキストを返す
*/
fmx::errcode FMX_CALL Moo_DownloadText ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("DownloadText").c_str(), 1 ); return kSPNoError; }
	const std::string url = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	std::string text;
	const int err = zoo::DownloadText ( url, text );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("DownloadText").c_str(), err );
	else SetReplyText ( reply, text );
	return kSPNoError;
}


/*
 Moo_DownloadFile ( sFile {; sLocal ; bProgress } ) — URL をローカルへ保存しパスを返す
 sLocal 省略時はテンポラリフォルダに保存。bProgress は未対応（無視）。
*/
fmx::errcode FMX_CALL Moo_DownloadFile ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("DownloadFile").c_str(), 1 ); return kSPNoError; }
	const std::string url = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string local = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string();
	std::string path;
	const int err = zoo::DownloadFile ( url, local, path );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("DownloadFile").c_str(), err );
	else SetReplyText ( reply, path );
	return kSPNoError;
}


/*
 Moo_FTPDownload ( sServer ; sUser ; sPassword ; sRemotePath ; sLocalFile {; bProgress } )
 リモートをダウンロードし保存先パスを返す。sLocalFile 省略時はテンポラリへ。
*/
fmx::errcode FMX_CALL Moo_FTPDownload ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 4 ) { SetReplyMooError ( reply, zoo::FnName("FTPDownload").c_str(), 1 ); return kSPNoError; }
	zoo::FtpParams ftp;
	ftp.server   = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	ftp.user     = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	ftp.password = TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) );
	const std::string remote = TextAsUTF8 ( parameters.AtAsText ( 3 ) );
	const std::string local  = parameters.Size() >= 5 ? TextAsUTF8 ( parameters.AtAsText ( 4 ) ) : std::string();
	std::string path;
	const int err = zoo::FTPDownload ( ftp, remote, local, path );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FTPDownload").c_str(), err );
	else SetReplyText ( reply, path );
	return kSPNoError;
}


/*
 Moo_FTPUpload ( sServer ; sUser ; sPassword ; sLocalFile ; sRemotePath {; bOverwrite ; bProgress } )
 ローカルをアップロードする。成功時は 1。bProgress は未対応（無視）。
*/
fmx::errcode FMX_CALL Moo_FTPUpload ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 5 ) { SetReplyMooError ( reply, zoo::FnName("FTPUpload").c_str(), 1 ); return kSPNoError; }
	zoo::FtpParams ftp;
	ftp.server   = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	ftp.user     = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	ftp.password = TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) );
	const std::string local  = TextAsUTF8 ( parameters.AtAsText ( 3 ) );
	const std::string remote = TextAsUTF8 ( parameters.AtAsText ( 4 ) );
	const bool overwrite = parameters.Size() >= 6 ? parameters.AtAsBoolean ( 5 ) : false;
	const int err = zoo::FTPUpload ( ftp, local, remote, overwrite );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FTPUpload").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FTPDelete ( sServer ; sUser ; sPass ; sRemotePath ) — 未公開関数。リモートを削除。成功時 1。
*/
fmx::errcode FMX_CALL Moo_FTPDelete ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 4 ) { SetReplyMooError ( reply, zoo::FnName("FTPDelete").c_str(), 1 ); return kSPNoError; }
	zoo::FtpParams ftp;
	ftp.server   = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	ftp.user     = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	ftp.password = TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) );
	const std::string remote = TextAsUTF8 ( parameters.AtAsText ( 3 ) );
	const int err = zoo::FTPDelete ( ftp, remote );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("FTPDelete").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/* ****************************************************************************

 MooPlug 互換関数（Tier B・Dialog 3 + Printer 2 + Process 4）

 ロジックは Source/DialogOps.*（GUI）/ PrinterOps.*（winspool/CUPS）/ ProcessOps.*。
 GUI・システム系のため Server/WebDirect では意味をなさない（登録で kServerCompatible は
 付けない＝既定の kDisplayInAllDialogs のみ）。Dialog は GUI モーダルで headless テスト不可。

 **************************************************************************** */

/*
 Moo_DialogColour( {bFull} ) — カラーピッカーを出し選択色（RRGGBB hex）を返す。bFull は省略可。
*/
fmx::errcode FMX_CALL Moo_DialogColour ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const bool full = parameters.Size() >= 1 ? parameters.AtAsBoolean ( kSPFirstParameter ) : false;
	std::string colour;
	const int err = zoo::DialogColour ( full, colour );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("DialogColour").c_str(), err );
	else SetReplyText ( reply, colour );
	return kSPNoError;
}


/*
 Moo_DialogFile( {bOpen ; sTitle ; sDefault} ) — ファイル選択（bOpen=true で開く/false で保存）。
*/
fmx::errcode FMX_CALL Moo_DialogFile ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const bool open = parameters.Size() >= 1 ? parameters.AtAsBoolean ( kSPFirstParameter ) : false;
	const std::string title = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string();
	const std::string def   = parameters.Size() >= 3 ? TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) : std::string();
	std::string path;
	const int err = zoo::DialogFile ( open, title, def, path );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("DialogFile").c_str(), err );
	else SetReplyText ( reply, path );
	return kSPNoError;
}


/*
 Moo_DialogFolder( {sTitle ; bNewFolder} ) — フォルダ選択。bNewFolder=true で新規フォルダボタン。
*/
fmx::errcode FMX_CALL Moo_DialogFolder ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const std::string title = parameters.Size() >= 1 ? TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) : std::string();
	const bool new_folder = parameters.Size() >= 2 ? parameters.AtAsBoolean ( kSPSecondParameter ) : false;
	std::string path;
	const int err = zoo::DialogFolder ( title, new_folder, path );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("DialogFolder").c_str(), err );
	else SetReplyText ( reply, path );
	return kSPNoError;
}


/*
 Moo_PrinterDefault( {sPrinter} ) — 引数なし/空=既定プリンタ名を取得、非空=既定に設定し 1 を返す。
*/
fmx::errcode FMX_CALL Moo_PrinterDefault ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const std::string name = parameters.Size() >= 1 ? TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) : std::string();
	if ( !name.empty() ) {
		const int err = zoo::PrinterSetDefault ( name );
		if ( err ) SetReplyMooError ( reply, zoo::FnName("PrinterDefault").c_str(), err );
		else SetReplyNumber ( reply, 1 );
	} else {
		std::string out;
		const int err = zoo::PrinterGetDefault ( out );
		if ( err ) SetReplyMooError ( reply, zoo::FnName("PrinterDefault").c_str(), err );
		else SetReplyText ( reply, out );
	}
	return kSPNoError;
}


/*
 Moo_PrinterList( {sSeparator} ) — インストール済みプリンタ名を一覧。既定 separator は "|"。
*/
fmx::errcode FMX_CALL Moo_PrinterList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const std::string sep = parameters.Size() >= 1 ? TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::PrinterList ( sep, list );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("PrinterList").c_str(), err );
	else SetReplyText ( reply, list );
	return kSPNoError;
}


/*
 Moo_ProcessCount ( sProcess ) — sProcess が空なら全プロセス数、非空なら一致数を返す。
*/
fmx::errcode FMX_CALL Moo_ProcessCount ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ProcessCount").c_str(), 1 ); return kSPNoError; }
	const std::string name = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	long count = 0;
	const int err = zoo::ProcessCount ( name, count );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ProcessCount").c_str(), err );
	else SetReplyNumber ( reply, count );
	return kSPNoError;
}


/*
 Moo_ProcessKill( sProcess ) — 一致するプロセスをすべて終了させる。成功時 1。
*/
fmx::errcode FMX_CALL Moo_ProcessKill ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ProcessKill").c_str(), 1 ); return kSPNoError; }
	const std::string name = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const int err = zoo::ProcessKill ( name );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ProcessKill").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_ProcessList( {sSeparator} ) — 実行中プロセス名の一覧。既定 separator は "|"。
*/
fmx::errcode FMX_CALL Moo_ProcessList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const std::string sep = parameters.Size() >= 1 ? TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::ProcessList ( sep, list );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ProcessList").c_str(), err );
	else SetReplyText ( reply, list );
	return kSPNoError;
}


/*
 Moo_ProcessRunning ( sProcess ) — sProcess が実行中なら 1、そうでなければ 0。
*/
fmx::errcode FMX_CALL Moo_ProcessRunning ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ProcessRunning").c_str(), 1 ); return kSPNoError; }
	const std::string name = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	bool running = false;
	const int err = zoo::ProcessRunning ( name, running );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ProcessRunning").c_str(), err );
	else SetReplyNumber ( reply, running ? 1 : 0 );
	return kSPNoError;
}


/* ****************************************************************************

 MooPlug 互換関数（Tier C・ProgressOptions + Hotkey 3）

 ProgressOptions は Source/ProgressOps.*（状態保持。進捗 UI は将来）。
 Hotkey は Source/HotkeyOps.*（登録）+ ZooPlug.cpp の kFMXT_Idle（StartScript ディスパッチ）。
 GUI/常駐系のため Server/WebDirect では意味をなさない（kServerCompatible は付けない）。

 **************************************************************************** */

/*
 Moo_ProgressOptions( sTitle {; sCaption ; bCancel } ) — Download/FTP の進捗ダイアログ設定。成功時 1。
*/
fmx::errcode FMX_CALL Moo_ProgressOptions ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("ProgressOptions").c_str(), 1 ); return kSPNoError; }
	const std::string title   = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string caption = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string();
	const bool cancel = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::SetProgressOptions ( title, caption, cancel );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("ProgressOptions").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_HotkeyAdd( sHotkey ; sFile ; sScript {; sParam ; bAlt ; bControl ; bShift ; bGlobal } ) —
 ホットキーを登録し、押下で sFile の sScript を起動する。成功時 1。
*/
fmx::errcode FMX_CALL Moo_HotkeyAdd ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 3 ) { SetReplyMooError ( reply, zoo::FnName("HotkeyAdd").c_str(), 1 ); return kSPNoError; }
	const std::string hotkey = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string file   = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const std::string script = TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) );
	const std::string param  = parameters.Size() >= 4 ? TextAsUTF8 ( parameters.AtAsText ( 3 ) ) : std::string();
	const bool alt     = parameters.Size() >= 5 ? parameters.AtAsBoolean ( 4 ) : false;
	const bool control = parameters.Size() >= 6 ? parameters.AtAsBoolean ( 5 ) : false;
	const bool shift   = parameters.Size() >= 7 ? parameters.AtAsBoolean ( 6 ) : false;
	const bool global  = parameters.Size() >= 8 ? parameters.AtAsBoolean ( 7 ) : false;
	const int err = zoo::HotkeyAdd ( hotkey, file, script, param, alt, control, shift, global );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("HotkeyAdd").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_HotkeyList( { sSeparator } ) — 登録済みホットキーの一覧。既定 separator は "|"。
*/
fmx::errcode FMX_CALL Moo_HotkeyList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const std::string sep = parameters.Size() >= 1 ? TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::HotkeyList ( sep, list );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("HotkeyList").c_str(), err );
	else SetReplyText ( reply, list );
	return kSPNoError;
}


/*
 Moo_HotkeyRemove( sHotkey ) — 登録済みホットキーを解除する。成功時 1。
*/
fmx::errcode FMX_CALL Moo_HotkeyRemove ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, zoo::FnName("HotkeyRemove").c_str(), 1 ); return kSPNoError; }
	const std::string hotkey = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const int err = zoo::HotkeyRemove ( hotkey );
	if ( err ) SetReplyMooError ( reply, zoo::FnName("HotkeyRemove").c_str(), err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/* ***************************************************************************

 Public plug-in functions

 **************************************************************************** */

#pragma mark -
#pragma mark Plugin
#pragma mark -

/*
 登録テーブル

 - 順序は不変厳守（funcId は登録順に振られるため、途中への挿入は既存の計算式を壊す）。
   新しい関数は必ず末尾に足すこと。
 - プロトタイプ文字列の表記は MooPlug 0.4.9 の挙動観察に合わせてある。
   NumberOfParameters が { ; opt } 記法から必須/省略可能数を出す。
 */

// 関数定義テーブル。プロトタイプは prefix(zoo_) を頭に付けて Init 時に組み立てる:
//   zoo_ + base_name + params  (例: zoo_FileCopy( sSource ; sDest ... ))
// is_extension=true の関数(zoo_PowerShell)は has_extensions で登録要否を切替える。
//
// 注: params の spacing は MooPlug 0.4.9 の実機プロトタイプを忠実に再現している
//     (Moo_ProcessCount / Moo_ProcessRunning だけ "Name (" と空白あり、Moo_DialogColour
//      は閉じ括弧の前に空白なし等の特殊形が含まれる)。
struct FunctionDef {
	const char* base_name;        // "FileCopy" — プレフィックスを除いた本体
	const char* params;           // "( sSource ; sDest ... )" / " ( sProcess )" / ""
	fmx::ExtPluginType function;
	const char* meaning;          // 動詞句で始まる説明文(プロトタイプ部分は含めない)。
	                              // description = prototype + " - " + meaning で組み立て
	bool is_extension = false;    // true = 独自拡張(zoo_PowerShell)
	short min_params = -1;        // -1 = プロトタイプから自動算出(spec §5.4)
	short max_params = -1;
};

// 登録テーブル。登録順がそのまま funcId (0..38) になる。
// 順序は不変厳守: 途中に挿入すると既存の計算式の funcId 参照がずれて壊れる。
// 末尾の zoo_PowerShell は ZooPlug 独自拡張 (is_extension=true)。
static const FunctionDef kFunctions[] = {
	{ "ProcessRunning", " ( sProcess )",                 Moo_ProcessRunning,
	  "Returns 1 if a process matching sProcess is running, else 0." },
	{ "ProcessCount",   " ( sProcess )",                 Moo_ProcessCount,
	  "Counts running processes matching sProcess "
	  "(empty = all processes). Returns a number." },
	{ "DialogColour",   "( bFull)",                      Moo_DialogColour,
	  "Shows a colour picker and returns the chosen colour as "
	  "RRGGBB hex. bFull shows the full picker. Windows/macOS only.",
	  false, 0, 1 },
	{ "ErrorDetail",    "( sError )",                    Moo_ErrorDetail,
	  "Returns the description for a MooPlug error code "
	  "such as \"Moo_FileCopy|Err_3\"." },
	{ "FTPDownload",    "( sServer ; sUser ; sPass ; sRemotePath {; sLocalFile ; bProgress } )", Moo_FTPDownload,
	  "Downloads a file via FTP. sLocalFile optional (defaults to the temp folder). "
	  "Returns the local path or an error code. bProgress is not yet supported (ignored)." },
	{ "PrinterDefault", "( { sPrinter } )",              Moo_PrinterDefault,
	  "With no argument returns the system default printer; "
	  "with a printer name sets it as default and returns 1. Does not affect FileMaker printing.",
	  false, 0, 1 },
	{ "ProgressOptions","( sTitle {; sCaption ; bCancel } )", Moo_ProgressOptions,
	  "Sets up the progress dialog used "
	  "by Download/FTP (bProgress). Returns 1. (The progress UI itself is not yet wired.)" },
	{ "ProcessKill",    "( sProcess )",                  Moo_ProcessKill,
	  "Terminates all processes matching sProcess. "
	  "Returns 1 on success or an error code." },
	{ "HotkeyAdd",      "( sHotkey ; sFile ; sScript {; sParam ; bAlt ; bControl ; bShift ; bGlobal } )", Moo_HotkeyAdd,
	  "Registers a global hotkey that runs sScript in sFile (with sParam) when pressed. "
	  "Keys: A-Z, 0-9, F1-F12, Space, Esc, End, Home, Up, Down, Left, Right. Returns 1 or an error code." },
	{ "HotkeyRemove",   "( sHotkey )",                   Moo_HotkeyRemove,
	  "Removes a previously registered hotkey. Returns 1 or an error code." },
	{ "HotkeyList",     "( { sSeparator } )",            Moo_HotkeyList,
	  "Lists registered hotkeys. Default separator is |.",
	  false, 0, 1 },
	{ "Hash",           "( sHash ; sText {; bFile } )",  Moo_Hash,
	  "Returns the MD5, SHA1, SHA256 or SHA512 hash "
	  "of a string, or of a file when bFile is true. Lower-case hex output." },
	{ "Shell",          " ( sShellCommand )",            Moo_Shell,
	  "Run a one-line shell command and return its output. "
	  "On Windows runs via cmd.exe, on macOS via /bin/sh. Reproduces MooPlug's Moo_Shell." },
	{ "DownloadText",   "( sURL )",                      Moo_DownloadText,
	  "Downloads a text file from an http(s) URL and returns its "
	  "contents. Returns the body text or an error code." },
	{ "DownloadFile",   "( sURL {; sLocal ; bProgress } )", Moo_DownloadFile,
	  "Downloads a file from an http(s) URL. "
	  "sLocal optional (defaults to the temp folder). Returns the local path or an error code. "
	  "bProgress is not yet supported (ignored)." },
	{ "FileCopy",       "( sSource ; sDest {; bOverwrite ; bProgress } )", Moo_FileCopy,
	  "Copies a file. Returns 1 on success or an error code." },
	{ "FileMove",       "( sSource ; sDest {; bOverwrite } )", Moo_FileMove,
	  "Moves a file. Returns 1 on success or an error code." },
	{ "FileExists",     "( sFile )",                     Moo_FileExists,
	  "Checks if a file exists. Returns 1 or 0, or an error code." },
	{ "FileDelete",     "( sFile )",                     Moo_FileDelete,
	  "Deletes a file. Returns 1 on success or an error code." },
	{ "FolderCopy",     "( sSource ; sDest )",           Moo_FolderCopy,
	  "Copies a folder recursively. Returns 1 on success or an error code." },
	{ "FolderMove",     "( sSource ; sDest )",           Moo_FolderMove,
	  "Moves a folder. Returns 1 on success or an error code." },
	{ "FolderExists",   "( sFolder )",                   Moo_FolderExists,
	  "Checks if a folder exists. Returns 1 or 0, or an error code." },
	{ "FolderDelete",   "( sFolder )",                   Moo_FolderDelete,
	  "Deletes a folder and its contents. Returns 1 on success or an error code." },
	{ "FolderCreate",   "( sFolder )",                   Moo_FolderCreate,
	  "Creates a folder. Returns 1 on success or an error code." },
	{ "ZipCompress",    "( sPath {; bTemp ; bOverwrite ; bFolderName ; sPassword } )", Moo_ZipCompress,
	  "Creates or adds to a Zip archive. bTemp: false = same folder, true = temp folder, "
	  "or a string output path/filename. Returns the Zip path or an error code." },
	{ "ZipExtract",     "( sFile {; bTemp ; bOverwrite } )", Moo_ZipExtract,
	  "Extracts the first file from a "
	  "Zip archive. Returns the extracted file path or an error code." },
	{ "DialogFile",     "( {bOpen ; sTitle ; sDefault } )", Moo_DialogFile,
	  "Shows a file open (bOpen=true) or save "
	  "dialog and returns the chosen path. Windows/macOS only.",
	  false, 0, 3 },
	{ "Version",        "",                              Moo_Version,
	  "Returns the MooPlug compatibility version string." },
	{ "DialogFolder",   "( { sTitle ; bNewFolder } )",   Moo_DialogFolder,
	  "Shows a folder picker and returns the "
	  "chosen path. bNewFolder shows the new-folder button. Windows/macOS only.",
	  false, 0, 2 },
	{ "FileInfo",       "( sFile ; sInfo {; sOptions } )", Moo_FileInfo,
	  "Gets or sets file information. "
	  "sInfo: size (sOptions: human/bytes), version, created, modified "
	  "(pass a timestamp in sOptions to set)." },
	{ "FTPDelete",      "( sServer ; sUser ; sPass ; sRemotePath )", Moo_FTPDelete,
	  "Deletes a remote file via FTP (undocumented in MooPlug). Returns 1 on success or an error code." },
	{ "FileRead",       "( sFile )",                     Moo_FileRead,
	  "Reads a text file and returns its contents." },
	{ "FolderList",     "( sFolder {; sPattern ; sSeparator } )", Moo_FolderList,
	  "Lists files in a folder. Defaults: sPattern = *.* and sSeparator = |." },
	{ "FTPUpload",      "( sServer ; sUser ; sPass ; sLocalFile ; sRemotePath {; bOverwrite ; bProgress } )", Moo_FTPUpload,
	  "Uploads a file via FTP. Returns 1 on success or an error code. "
	  "bProgress is not yet supported (ignored)." },
	{ "ZipList",        "( sZip {; sPattern ; sSeparator } )", Moo_ZipList,
	  "Lists the contents of a Zip archive. "
	  "Defaults: sPattern = *.* and sSeparator = |." },
	{ "PrinterList",    "( { sSeparator } )",            Moo_PrinterList,
	  "Lists installed printers. Default separator is |.",
	  false, 0, 1 },
	{ "FileWrite",      "( sFile ; sText {; bAppend } )", Moo_FileWrite,
	  "Writes or appends to a file. Returns 1 on success or an error code." },
	{ "ProcessList",    "( { sSeparator } )",            Moo_ProcessList,
	  "Lists running process names. Default separator is |.",
	  false, 0, 1 },
	// ---- ZooPlug 拡張(末尾)。has_extensions=false なら登録スキップ ----
	{ "PowerShell",     " ( command { ; bCore } )",      Zoo_PowerShell,
	  "Run a PowerShell script and return its output as UTF-8. "
	  "bCore=True uses PowerShell 7 (pwsh), False (default) uses Windows PowerShell 5.1. "
	  "On macOS/Linux always uses pwsh. Uses a temp-file approach that survives Constrained Language Mode "
	  "(WDAC). ZooPlug-original; the PowerShell counterpart to Shell.",
	  true /* is_extension — 独自拡張 */ },
};


/*
 initialise the plug-in
 perform any setup and register functions
 */

const fmx::ptrtype Init ( FMX_ExternCallPtr /* pb */ )
{
	// FileMaker Pro 11 (= 52) を含む古い世代から最新までロードされるよう、最低互換
	// バージョンを名乗る。FileMaker は前方互換: 低い API バージョンを返すと古い API
	// しか使えないが、新しい FileMaker でもそのままロードされる。逆に高すぎる値を
	// 返すと FM Pro 11 のような古い世代は「未知のバージョン」として plug-in を拒否する。
	// ZooPlug が使う関数登録・Text/Data 操作は k110ExtnVersion から存在する API のみ。
	fmx::ptrtype enable = k110ExtnVersion;

	g_registered_func_ids.clear();

	for ( const FunctionDef& def : kFunctions ) {
		// has_extensions=false のビルドでは独自拡張(zoo_PowerShell 等)をスキップ。
		if ( def.is_extension && !zoo::kVariant.has_extensions ) continue;

		// プロトタイプは prefix + base_name + params で組み立て。
		// description は "prototype - meaning"(空 meaning ならプロトタイプのみ)。
		const std::string prototype = std::string ( zoo::kVariant.prefix ) + def.base_name + def.params;
		const std::string description = ( def.meaning && *def.meaning )
		                              ? prototype + " - " + def.meaning
		                              : prototype;

		// funcId は kFunctions の登録順をそのまま使う(0..N の連番)。
		const short func_id = static_cast<short> ( &def - kFunctions );
		const fmx::errcode error = RegisterFunction ( func_id, prototype, def.function, description, def.min_params, def.max_params );
		if ( kSPNoError != error ) {
			enable = (fmx::ptrtype)kDoNotEnable;
			break;
		}
	}

	return enable;

} // Init


/*
 clean up anything set up or allocated in Init
 plug-in functions are un-registered automatically before this function is called
 */

void Shutdown ( void )
{
	// 常駐しているホットキーを解除し、メッセージ専用ウィンドウ/ハンドラを破棄する（鉄則 8）。
	zoo::HotkeyShutdown ( );
}


/*
 Hotkey の StartScript ディスパッチ

 kFMXT_Init で cStartScript / cCurrentEnv を自前グローバルへコピーし（毎回上書きされる
 gFMX_ExternCallPtr に依存しない＝鉄則 3）、kFMXT_Idle（メインスレッド）でホットキーの
 発火キューを刈り取って FMX_StartScript を呼ぶ。kFMXT_Unsafe（非メインスレッド相当）の
 ときは FMX を一切触らず次の機会を待つ（鉄則 2）。
 */
static FMX_StartScriptCall g_start_script = nullptr;
static FMX_CurrentEnvCall  g_current_env  = nullptr;

static void Do_PluginIdle ( FMX_ExternCallPtr pb )
{
	if ( pb->parm1 == kFMXT_Unsafe ) return;   // 非メインスレッド相当
	if ( !g_start_script ) return;

	std::vector<zoo::HotkeyFire> fires;
	zoo::HotkeyDrainPending ( fires );
	for ( const zoo::HotkeyFire& f : fires ) {
		fmx::TextUniquePtr file, script, paramText;
		if ( !f.file.empty() )   file->Assign ( f.file.c_str(),   fmx::Text::kEncoding_UTF8 );
		if ( !f.script.empty() ) script->Assign ( f.script.c_str(), fmx::Text::kEncoding_UTF8 );
		if ( !f.param.empty() )  paramText->Assign ( f.param.c_str(), fmx::Text::kEncoding_UTF8 );
		fmx::DataUniquePtr param;
		fmx::LocaleUniquePtr loc;
		param->SetAsText ( *paramText, *loc );
		g_start_script ( &(*file), &(*script), kFMXT_Pause, &(*param) );
	}
}


/*
 the main entry point for the plug-in

 calls from FileMaker go either here or directly to a registered plugin function
 see also the options for FMX_ExternCallSwitch in FMXExtern.h
 */

void FMX_ENTRYPT FMExternCallProc ( FMX_ExternCallPtr pb )
{
	switch ( pb->whichCall )
	{
		case kFMXT_GetString:
			GetString ( pb );
			break;

		case kFMXT_Init:
			g_next_function = kSPFirstFunction;
			g_start_script  = pb->cStartScript;   // 鉄則 3: グローバルに依存せずコピー
			g_current_env   = pb->cCurrentEnv;
			pb->result = Init ( pb );
			break;

		case kFMXT_Idle:
			Do_PluginIdle ( pb );
			break;

		case kFMXT_Shutdown:
			UnregisterFunctions ( );
			Shutdown ( );
			break;
	}

}	// FMExternCallProc


/* ***************************************************************************
 You should not need to edit anything in this section.
 *************************************************************************** */

#pragma mark -
#pragma mark Private Functions
#pragma mark -


// get the plug-in name or options string and hand back to FileMaker

void GetString ( FMX_ExternCallPtr pb )
{
	fmx::TextUniquePtr string;

	switch ( pb->parm1 )
	{
		case kSPOptionsString:
		case kFMXT_OptionsStr:
			// FM_Text_SetText は FM11 に無い(FM12+)。空 Text へ InsertText でコピー。
			string->InsertText ( *PluginOptionsString(), 0 );
			break;

		case kFMXT_NameStr:
		case kFMXT_AppConfigStr:
			string->Assign ( zoo::kVariant.product );
			break;
	}

	// FM_Text_GetUnicode は FM11 に無い(FM12+)。FM11 互換のため FM_Text_GetBytes
	// だけを使う。SDK に UTF-16 出力 encoding が無いので、ASCII(=プラグイン名・options
	// 文字列は常に ASCII)を UTF-8 で取り出し、各バイトを UTF-16 へ widen して result に
	// 書き、parm3(最大文字数)で範囲を制限して手動で NUL 終端する。
	const fmx::uint32 max_chars = static_cast<fmx::uint32>( pb->parm3 );
	fmx::uint32 n = string->GetSize();
	char tmp[256];
	if ( n > 254 ) n = 254;
	string->GetBytes ( tmp, sizeof(tmp), 0, n, fmx::Text::kEncoding_UTF8 );
	fmx::uint16* out = reinterpret_cast<fmx::uint16*>( pb->result );
	fmx::uint32 i = 0;
	for ( ; i < n && ( max_chars == 0 || i + 1 < max_chars ); ++i ) {
		out[i] = static_cast<fmx::uint16>( static_cast<unsigned char>( tmp[i] ) );
	}
	out[i] = 0;

} // GetString


/*
 register plug-in functions

 RegisterFunction takes three parameters:
 1. the external function signature as it should appear in the calculation dialogs,
    e.g. "moo_shell ( command )". MooPlug 互換にするため、SimplePlugin と違って
    プラグインのプレフィックスは関数名に付けず、与えた名前のまま登録する。
 2. the plug-in function to call when the function is used in FileMaker
 3. (optional) a description of the function ... default: ""
 */

const fmx::errcode RegisterFunction ( short func_id, const std::string prototype, const fmx::ExtPluginType function, const std::string description, short min_override, short max_override )
{
	fmx::TextUniquePtr function_protoype;
	function_protoype->Assign ( prototype.c_str() );

	fmx::TextUniquePtr function_description;
	function_description->Assign ( description.c_str() );

	fmx::TextUniquePtr name;
	name->InsertText ( *FunctionName ( function_protoype ), 0 );   // FM_Text_SetText は FM11 に無い

	short required_parameters = 0;
	short optional_parameters = 0;
	NumberOfParameters ( function_protoype, required_parameters, optional_parameters );

	// プロトタイプから算出した値が既定。min_override / max_override が >= 0 なら上書きする。
	// （全引数が省略可能な関数は NumberOfParameters が min=1 にしてしまうため）
	const short min_parameters = ( min_override >= 0 ) ? min_override : required_parameters;
	const short max_parameters = ( max_override >= 0 ) ? max_override : static_cast<short>( required_parameters + optional_parameters );

	const fmx::uint32 function_flags = fmx::ExprEnv::kDisplayInAllDialogs;

	// 非 Ex 版 RegisterExternalFunction を使う: FM Pro 11 の FMWrapper.dll は
	// FM_ExprEnv_RegisterExternalFunctionEx を export しない(Ex は FM12+)。
	// よって非 Ex 版で登録する。非 Ex 版は description 引数を取らない(関数の
	// 説明は計算式ダイアログにプロトタイプとして表示される)。
	(void) function_description;  // 非 Ex 版では未使用
	const fmx::errcode error = fmx::ExprEnv::RegisterExternalFunction ( *PluginID(),
																		 func_id,
																		 *name,
																		 *function_protoype,
																		 min_parameters,
																		 max_parameters,
																		 function_flags,
																		 function
																		 );

	if ( kSPNoError == error ) {
		g_registered_func_ids.push_back ( func_id );
	}

	return error;

} // RegisterFunction


// unregister all registered functions

void UnregisterFunctions ( void )
{
	// 実際に登録した funcId を g_registered_func_ids に控えてあるので、それを
	// 解除する。
	for ( short func_id : g_registered_func_ids ) {
		fmx::ExprEnv::UnRegisterExternalFunction ( *PluginID(), func_id );
	}
	g_registered_func_ids.clear();
}


// automatically generate the PluginID from the prefix

const fmx::QuadCharUniquePtr PluginID ( void )
{
	fmx::TextUniquePtr prefix;
	prefix->InsertText ( *PluginPrefix(), 0 );   // FM_Text_SetText は FM11 に無い
	char buffer[kSPPluginIDLength];
	prefix->GetBytes ( buffer, kSPPluginIDLength );
	fmx::QuadCharUniquePtr id ( buffer[0], buffer[1], buffer[2], buffer[3] );

	return id;
}


// Variant.h で定義された 4 文字プラグイン ID を返す。

const fmx::TextUniquePtr PluginPrefix ( void )
{
	fmx::TextUniquePtr prefix;
	prefix->Assign ( zoo::kVariant.plugin_id );
	return prefix;
}


// use the options string defined above otherwise turn everything on

const fmx::TextUniquePtr PluginOptionsString ( void )
{
	fmx::TextUniquePtr optionsString;

#ifdef PLUGIN_OPTIONS_STRING
	optionsString->Assign ( PLUGIN_OPTIONS_STRING );
#else
	optionsString->Assign ( "1YnYYnn" );
#endif

	optionsString->InsertText ( *PluginPrefix(), 0 );

	return optionsString;
}


// extract the function name from a function signature/prototype

const fmx::TextUniquePtr FunctionName ( const fmx::TextUniquePtr& signature )
{
	fmx::TextUniquePtr separator;
	separator->Assign ( "(" );

	fmx::uint32 parameters_start = signature->Find ( *separator, 0 );
	if ( parameters_start == fmx::Text::kSize_Invalid ) {
		parameters_start = fmx::Text::kSize_End;
	} else {

		// there may or may not be spaces between the function name and the bracket

		fmx::TextUniquePtr space;
		space->Assign ( " " );

		fmx::uint32 last = parameters_start - 1;
		while ( signature->Find ( *space, last ) == last ) {
			--last;
		}
		parameters_start = last + 1;
	}

	// FM_Text_SetText(範囲コピー)は FM11 に無い。GetBytes(UTF-8)で先頭
	// parameters_start 文字を取り出し、Assign で name に代入する(どちらも
	// FM11 互換シンボル)。関数名は ASCII なのでバイト数=文字数。
	fmx::TextUniquePtr name;
	fmx::uint32 ncopy = parameters_start;
	if ( ncopy > 254 ) ncopy = 254;
	char namebuf[256];
	signature->GetBytes ( namebuf, sizeof(namebuf), 0, ncopy, fmx::Text::kEncoding_UTF8 );
	namebuf[ncopy] = 0;
	name->Assign ( namebuf, fmx::Text::kEncoding_UTF8 );

	return name;

} // FunctionName


// calculate the number of required and optional parameters from a function signature/prototype

void NumberOfParameters ( const fmx::TextUniquePtr& signature, short& required, short& optional )
{
	required = 0;
	optional = 0;

	fmx::TextUniquePtr separator;
	separator->Assign ( "(" );

	const fmx::uint32 parameters_start = signature->Find ( *separator, 0 );
	if ( parameters_start == fmx::Text::kSize_Invalid ) {
		return;
	}

	// we have parameters

	fmx::TextUniquePtr semi_colon;
	semi_colon->Assign ( ";" );

	fmx::TextUniquePtr curly_bracket;
	curly_bracket->Assign ( "{" );

	bool has_optional_parameters = false;
	fmx::uint32 next = parameters_start;

	while ( next != fmx::Text::kSize_Invalid ) {

		++next;
		const fmx::uint32 next_semi_colon = signature->Find ( *semi_colon, next );
		const fmx::uint32 next_curly_bracket = signature->Find ( *curly_bracket, next );

		if ( next_curly_bracket < next_semi_colon && has_optional_parameters == false ) {

			next = signature->Find ( *semi_colon, next_curly_bracket + 1 );
			++required;
			has_optional_parameters = true;

			fmx::TextUniquePtr elipsis;
			elipsis->Assign ( "\xE2\x80\xA6", fmx::Text::kEncoding_UTF8 ); // …

			if ( signature->Find ( *elipsis, next_curly_bracket + 1 ) != fmx::Text::kSize_Invalid ) {
				optional = -1;
				next = fmx::Text::kSize_Invalid;
			} else {

				fmx::TextUniquePtr faux_elipsis;
				faux_elipsis->Assign ( "..." );

				if ( signature->Find ( *faux_elipsis, next_curly_bracket + 1 ) != fmx::Text::kSize_Invalid ) {
					optional = kSPManyParameters;
					next = fmx::Text::kSize_Invalid;
				}
			}

		} else {
			next = next_semi_colon;

			if ( has_optional_parameters == true ) {
				++optional;
			} else {
				++required;
			}
		}

	}

} // NumberOfParameters
