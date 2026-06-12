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

#include <string>
#include <vector>

#include "ShellExec.h"
#include "PowerShellExec.h"
#include "MooError.h"
#include "FileOps.h"
#include "HashImpl.h"
#include "ZipOps.h"

#ifdef _MSC_VER
// #pragma mark は Xcode のコード折り畳み用。MSVC は知らないので C4068 を黙らせる。
#pragma warning(disable: 4068)
#endif

// このプラグイン固有の設定 ----------------------------------------------------
//   PLUGIN_NAME   : システム設定に表示されるプラグイン名
//   PLUGIN_PREFIX : 4 文字のプラグイン ID（FileMaker が内部で使う一意の識別子）。
//                   登録する関数名（moo_shell）とは独立。
#define PLUGIN_NAME   "ZooPlug"
#define PLUGIN_PREFIX "ZooP"


#pragma mark -
#pragma mark Prototypes
#pragma mark -

const fmx::ptrtype Init ( FMX_ExternCallPtr pb );
void Shutdown ( void );

const fmx::QuadCharUniquePtr PluginID ( void );
const fmx::TextUniquePtr PluginPrefix ( void );
const fmx::TextUniquePtr PluginOptionsString ( void );

void GetString ( FMX_ExternCallPtr pb );
const fmx::errcode RegisterFunction ( const std::string prototype, const fmx::ExtPluginType function, const std::string description = "" );
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
		text->AssignWithLength ( utf8.c_str(), static_cast<fmx::uint32>(utf8.size()), fmx::Text::kEncoding_UTF8 );
	}
	fmx::LocaleUniquePtr default_locale;
	reply.SetAsText ( *text, *default_locale );
}


// 数値を戻り値にする。MooPlug の「成功 = true」「真偽値」は数値 1 / 0
//（0.4.9 バイナリは FixPt + SetAsNumber を使い、"True"/"False" 文字列リテラルを持たない）
static void SetReplyNumber ( fmx::Data& reply, fmx::int64 value )
{
	fmx::FixPtUniquePtr number;
	number->AssignInt64 ( value );
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
fmx::errcode Moo_Shell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply );

fmx::errcode Moo_Shell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
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
		result_text->AssignWithLength ( output.c_str(), static_cast<fmx::uint32>(output.size()), fmx::Text::kEncoding_UTF8 );
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
fmx::errcode Zoo_PowerShell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply );

fmx::errcode Zoo_PowerShell ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
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
		result_text->AssignWithLength ( output.c_str(), static_cast<fmx::uint32>(output.size()), fmx::Text::kEncoding_UTF8 );
	}

	fmx::LocaleUniquePtr default_locale;
	reply.SetAsText ( *result_text, *default_locale );

	return error;

} // Zoo_PowerShell


/* ****************************************************************************

 MooPlug 互換関数（Tier A）

 シグネチャ・エラーコード・戻り値は MooPlug 0.4.9 実バイナリ互換
 （docs/mooplug-reference.md / docs/zoo-plug-implementation-spec.md）。
 純粋ロジックは Source/FileOps.* / HashImpl.* / MooError.* にあり、
 ここでは引数の取り出しと戻り値の整形だけを行う。

 **************************************************************************** */

/*
 Moo_Version — MooPlug のバージョン文字列を返す（引数なし）
*/
fmx::errcode Moo_Version ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& /* parameters */, fmx::Data& reply )
{
	SetReplyText ( reply, zoo::kMooVersionString );
	return kSPNoError;
}


/*
 Moo_ErrorDetail ( sError ) — エラーコード文字列を説明文に変換する
*/
fmx::errcode Moo_ErrorDetail ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
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
fmx::errcode Moo_FileExists ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FileExists", 1 ); return kSPNoError; }
	bool exists = false;
	const int err = zoo::FileExists ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ), exists );
	if ( err ) SetReplyMooError ( reply, "Moo_FileExists", err );
	else SetReplyNumber ( reply, exists ? 1 : 0 );
	return kSPNoError;
}


/*
 Moo_FileCopy ( sSource ; sDest {; bOverwrite ; bProgress } ) — ファイルをコピーする
 bProgress は受け取るだけで現状未使用（進捗ダイアログは Tier C で実装予定）
*/
fmx::errcode Moo_FileCopy ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, "Moo_FileCopy", 1 ); return kSPNoError; }
	const std::string source = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string dest = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool overwrite = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::FileCopy ( source, dest, overwrite );
	if ( err ) SetReplyMooError ( reply, "Moo_FileCopy", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileDelete ( sFile ) — ファイルを削除する
*/
fmx::errcode Moo_FileDelete ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FileDelete", 1 ); return kSPNoError; }
	const int err = zoo::FileDelete ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	if ( err ) SetReplyMooError ( reply, "Moo_FileDelete", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileMove ( sSource ; sDest {; bOverwrite } ) — ファイルを移動する
*/
fmx::errcode Moo_FileMove ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, "Moo_FileMove", 1 ); return kSPNoError; }
	const std::string source = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string dest = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool overwrite = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::FileMove ( source, dest, overwrite );
	if ( err ) SetReplyMooError ( reply, "Moo_FileMove", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FileRead ( sFile ) — テキストファイルを読んで内容を返す
 UTF-8 として読み、不正なら Windows では ANSI(CP932) として復号。改行は CR 正規化。
*/
fmx::errcode Moo_FileRead ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FileRead", 1 ); return kSPNoError; }
	std::string text;
	const int err = zoo::FileRead ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ), text );
	if ( err ) SetReplyMooError ( reply, "Moo_FileRead", err );
	else SetReplyText ( reply, text );
	return kSPNoError;
}


/*
 Moo_FileWrite ( sFile ; sText {; bAppend } ) — ファイルへ書き込み/追記する
 0.4.9 実機と同じ 3 引数（ドキュメントにある bOverwrite はバイナリに存在しない）。
 UTF-8 で書き、改行は OS ネイティブ（Win=CRLF / 他=LF）へ変換する。
*/
fmx::errcode Moo_FileWrite ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, "Moo_FileWrite", 1 ); return kSPNoError; }
	const std::string file = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string text = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool append = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	const int err = zoo::FileWrite ( file, text, append );
	if ( err ) SetReplyMooError ( reply, "Moo_FileWrite", err );
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
fmx::errcode Moo_FileInfo ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	const char* const kName = "Moo_FileInfo";
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, kName, 1 ); return kSPNoError; }
	const std::string file = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string info = FoldAsciiLower ( TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) );

	if ( info == "size" ) {
		// 省略時の既定は human（StrFormatByteSize 風）。
		// TODO-compat: 実機 0.4.9 の省略時既定が human か bytes かは未確認（WORK1 で照合する）
		std::string options = "human";
		if ( parameters.Size() >= 3 ) {
			const std::string given = FoldAsciiLower ( TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) );
			if ( !given.empty() ) options = given;
		}
		if ( options != "human" && options != "bytes" ) { SetReplyMooError ( reply, kName, 5 ); return kSPNoError; }
		std::uint64_t bytes = 0;
		const int err = zoo::FileSize ( file, bytes );
		if ( err ) SetReplyMooError ( reply, kName, err );
		else if ( options == "bytes" ) SetReplyNumber ( reply, static_cast<fmx::int64>(bytes) );
		else SetReplyText ( reply, zoo::HumanReadableSize ( bytes ) );
		return kSPNoError;
	}

	if ( info == "version" ) {
		std::string version;
		const int err = zoo::FileVersion ( file, version );
		// 注: 0.4.9 バイナリには "Error retrieving file version" という生テキストの
		// リテラルもあるが、ZooPlug はエラーコード方式（Err_6）で統一する（TODO-compat）
		if ( err ) SetReplyMooError ( reply, kName, err );
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
			if ( parts.year <= 0 ) { SetReplyMooError ( reply, kName, 5 ); return kSPNoError; } // タイムスタンプとして解釈できない
			const int err = zoo::FileTimeSet ( file, creation, parts );
			if ( err ) SetReplyMooError ( reply, kName, err );
			else SetReplyNumber ( reply, 1 );
		} else {
			zoo::FileTimeParts parts;
			const int err = zoo::FileTimeGet ( file, creation, parts );
			if ( err ) { SetReplyMooError ( reply, kName, err ); return kSPNoError; }
			fmx::DateTimeUniquePtr timestamp;
			timestamp->SetNormalizedDate ( static_cast<fmx::int16>(parts.month), static_cast<fmx::int16>(parts.day), static_cast<fmx::int16>(parts.year) );
			timestamp->SetNormalizedTime ( parts.hour, static_cast<fmx::int16>(parts.minute), static_cast<fmx::int16>(parts.second) );
			reply.SetAsTimeStamp ( *timestamp );
		}
		return kSPNoError;
	}

	SetReplyMooError ( reply, kName, 4 ); // 未知の sInfo
	return kSPNoError;
}


/*
 Moo_FolderExists ( sFolder ) — フォルダの存在を 1 / 0 で返す
*/
fmx::errcode Moo_FolderExists ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FolderExists", 1 ); return kSPNoError; }
	bool exists = false;
	const int err = zoo::FolderExists ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ), exists );
	if ( err ) SetReplyMooError ( reply, "Moo_FolderExists", err );
	else SetReplyNumber ( reply, exists ? 1 : 0 );
	return kSPNoError;
}


/*
 Moo_FolderCopy ( sSource ; sDest ) — フォルダを再帰コピーする
*/
fmx::errcode Moo_FolderCopy ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, "Moo_FolderCopy", 1 ); return kSPNoError; }
	const int err = zoo::FolderCopy ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ),
									  TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) );
	if ( err ) SetReplyMooError ( reply, "Moo_FolderCopy", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderCreate ( sFolder ) — フォルダを作成する（中間フォルダも作る）
*/
fmx::errcode Moo_FolderCreate ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FolderCreate", 1 ); return kSPNoError; }
	const int err = zoo::FolderCreate ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	if ( err ) SetReplyMooError ( reply, "Moo_FolderCreate", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderDelete ( sFolder ) — フォルダを中身ごと削除する
*/
fmx::errcode Moo_FolderDelete ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FolderDelete", 1 ); return kSPNoError; }
	const int err = zoo::FolderDelete ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ) );
	if ( err ) SetReplyMooError ( reply, "Moo_FolderDelete", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderMove ( sSource ; sDest ) — フォルダを移動する
*/
fmx::errcode Moo_FolderMove ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, "Moo_FolderMove", 1 ); return kSPNoError; }
	const int err = zoo::FolderMove ( TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) ),
									  TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) );
	if ( err ) SetReplyMooError ( reply, "Moo_FolderMove", err );
	else SetReplyNumber ( reply, 1 );
	return kSPNoError;
}


/*
 Moo_FolderList ( sFolder {; sPattern ; sSeparator } ) — フォルダ直下のファイル一覧
 既定: sPattern = "*.*"（すべて）、sSeparator = "|"
*/
fmx::errcode Moo_FolderList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_FolderList", 1 ); return kSPNoError; }
	const std::string folder = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string pattern = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string("*.*");
	const std::string separator = parameters.Size() >= 3 ? TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::FolderList ( folder, pattern, separator, list );
	if ( err ) SetReplyMooError ( reply, "Moo_FolderList", err );
	else SetReplyText ( reply, list );
	return kSPNoError;
}


/*
 Moo_Hash ( sHash ; sText {; bFile } ) — MD5 / SHA1 / SHA256 / SHA512 ハッシュ
 出力は小文字 16 進。bFile = True なら sText をファイルパスとして扱う。
*/
fmx::errcode Moo_Hash ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 2 ) { SetReplyMooError ( reply, "Moo_Hash", 1 ); return kSPNoError; }
	const std::string algorithm = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string input = TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) );
	const bool is_file = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	std::string hex;
	const int err = is_file ? zoo::HashFile ( algorithm, input, hex )
							: zoo::HashString ( algorithm, input, hex );
	if ( err ) SetReplyMooError ( reply, "Moo_Hash", err );
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
fmx::errcode Moo_ZipCompress ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_ZipCompress", 1 ); return kSPNoError; }
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
	if ( err ) SetReplyMooError ( reply, "Moo_ZipCompress", err );
	else SetReplyText ( reply, zip_path );
	return kSPNoError;
}


/*
 Moo_ZipExtract ( sFile {; bTemp ; bOverwrite } ) — Zip から最初の 1 ファイルを展開
 bTemp = false（既定）なら Zip と同じフォルダへ、true ならテンポラリフォルダへ。
 複数ファイル展開は本家 0.4.9 も未対応（"a future version" のまま）。
*/
fmx::errcode Moo_ZipExtract ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_ZipExtract", 1 ); return kSPNoError; }
	const std::string zip = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const bool to_temp = parameters.Size() >= 2 ? parameters.AtAsBoolean ( kSPSecondParameter ) : false;
	const bool overwrite = parameters.Size() >= 3 ? parameters.AtAsBoolean ( kSPThirdParameter ) : false;
	std::string extracted;
	const int err = zoo::ZipExtract ( zip, to_temp, overwrite, extracted );
	if ( err ) SetReplyMooError ( reply, "Moo_ZipExtract", err );
	else SetReplyText ( reply, extracted );
	return kSPNoError;
}


/*
 Moo_ZipList ( sZip {; sPattern ; sSeparator } ) — Zip の内容一覧
 既定: sPattern = "*.*"（すべて）、sSeparator = "|"。格納順で返す。
*/
fmx::errcode Moo_ZipList ( short /* function_id */, const fmx::ExprEnv& /* environment */, const fmx::DataVect& parameters, fmx::Data& reply )
{
	if ( parameters.Size() < 1 ) { SetReplyMooError ( reply, "Moo_ZipList", 1 ); return kSPNoError; }
	const std::string zip = TextAsUTF8 ( parameters.AtAsText ( kSPFirstParameter ) );
	const std::string pattern = parameters.Size() >= 2 ? TextAsUTF8 ( parameters.AtAsText ( kSPSecondParameter ) ) : std::string("*.*");
	const std::string separator = parameters.Size() >= 3 ? TextAsUTF8 ( parameters.AtAsText ( kSPThirdParameter ) ) : std::string("|");
	std::string list;
	const int err = zoo::ZipList ( zip, pattern, separator, list );
	if ( err ) SetReplyMooError ( reply, "Moo_ZipList", err );
	else SetReplyText ( reply, list );
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
   既存の moo_shell → zoo_powershell の後ろに、MooPlug 互換関数を 0.4.9 実機リスト順で
   追加していく。今後の新関数（Zip/Download/FTP/Tier B/C）も必ず末尾に足すこと。
 - Moo_* のプロトタイプ文字列は 0.4.9 実機の外部関数リスト（docs/mooplug-reference.md）
   の表記そのまま。既存の NumberOfParameters が { ; opt } 記法から必須/省略可能数を出す。
 */

struct PluginFunctionDef {
	const char* prototype;
	fmx::ExtPluginType function;
	const char* description;
};

static const PluginFunctionDef kPluginFunctions[] = {
	{ "moo_shell ( command )",
	  Moo_Shell,
	  "moo_shell ( command ) - Run a one-line shell command and return its output. "
	  "On Windows runs via cmd.exe, on macOS via /bin/sh. Reproduces MooPlug's moo_shell." },
	{ "zoo_powershell ( command { ; bCore } )",
	  Zoo_PowerShell,
	  "zoo_powershell ( command { ; bCore } ) - Run a PowerShell script and return its output as UTF-8. "
	  "bCore=True uses PowerShell 7 (pwsh), False (default) uses Windows PowerShell 5.1. "
	  "On macOS/Linux always uses pwsh. Uses a temp-file approach that survives Constrained Language Mode "
	  "(WDAC). ZooPlug-original; the PowerShell counterpart to moo_shell." },
	{ "Moo_ErrorDetail( sError )",
	  Moo_ErrorDetail,
	  "Moo_ErrorDetail( sError ) - Returns the description for a MooPlug error code "
	  "such as \"Moo_FileCopy|Err_3\"." },
	{ "Moo_FileCopy( sSource ; sDest {; bOverwrite ; bProgress } )",
	  Moo_FileCopy,
	  "Moo_FileCopy( sSource ; sDest {; bOverwrite ; bProgress } ) - Copies a file. "
	  "Returns 1 on success or an error code." },
	{ "Moo_FileDelete( sFile )",
	  Moo_FileDelete,
	  "Moo_FileDelete( sFile ) - Deletes a file. Returns 1 on success or an error code." },
	{ "Moo_FileExists( sFile )",
	  Moo_FileExists,
	  "Moo_FileExists( sFile ) - Checks if a file exists. Returns 1 or 0, or an error code." },
	{ "Moo_FileInfo( sFile ; sInfo {; sOptions } )",
	  Moo_FileInfo,
	  "Moo_FileInfo( sFile ; sInfo {; sOptions } ) - Gets or sets file information. "
	  "sInfo: size (sOptions: human/bytes), version, created, modified "
	  "(pass a timestamp in sOptions to set)." },
	{ "Moo_FileMove( sSource ; sDest {; bOverwrite } )",
	  Moo_FileMove,
	  "Moo_FileMove( sSource ; sDest {; bOverwrite } ) - Moves a file. "
	  "Returns 1 on success or an error code." },
	{ "Moo_FileRead( sFile )",
	  Moo_FileRead,
	  "Moo_FileRead( sFile ) - Reads a text file and returns its contents." },
	{ "Moo_FileWrite( sFile ; sText {; bAppend } )",
	  Moo_FileWrite,
	  "Moo_FileWrite( sFile ; sText {; bAppend } ) - Writes or appends to a file. "
	  "Returns 1 on success or an error code." },
	{ "Moo_FolderCopy( sSource ; sDest )",
	  Moo_FolderCopy,
	  "Moo_FolderCopy( sSource ; sDest ) - Copies a folder recursively. "
	  "Returns 1 on success or an error code." },
	{ "Moo_FolderCreate( sFolder )",
	  Moo_FolderCreate,
	  "Moo_FolderCreate( sFolder ) - Creates a folder. Returns 1 on success or an error code." },
	{ "Moo_FolderDelete( sFolder )",
	  Moo_FolderDelete,
	  "Moo_FolderDelete( sFolder ) - Deletes a folder and its contents. "
	  "Returns 1 on success or an error code." },
	{ "Moo_FolderExists( sFolder )",
	  Moo_FolderExists,
	  "Moo_FolderExists( sFolder ) - Checks if a folder exists. Returns 1 or 0, or an error code." },
	{ "Moo_FolderList( sFolder {; sPattern ; sSeparator } )",
	  Moo_FolderList,
	  "Moo_FolderList( sFolder {; sPattern ; sSeparator } ) - Lists files in a folder. "
	  "Defaults: sPattern = *.* and sSeparator = |." },
	{ "Moo_FolderMove( sSource ; sDest )",
	  Moo_FolderMove,
	  "Moo_FolderMove( sSource ; sDest ) - Moves a folder. Returns 1 on success or an error code." },
	{ "Moo_Hash( sHash ; sText {; bFile } )",
	  Moo_Hash,
	  "Moo_Hash( sHash ; sText {; bFile } ) - Returns the MD5, SHA1, SHA256 or SHA512 hash "
	  "of a string, or of a file when bFile is true. Lower-case hex output." },
	{ "Moo_Version",
	  Moo_Version,
	  "Moo_Version - Returns the MooPlug compatibility version string." },
	// ---- ここから後日追加分（登録順は不変厳守: 必ず末尾に足す） ----
	{ "Moo_ZipCompress( sPath {; bTemp ; bOverwrite ; bFolderName ; sPassword } )",
	  Moo_ZipCompress,
	  "Moo_ZipCompress( sPath {; bTemp ; bOverwrite ; bFolderName ; sPassword } ) - "
	  "Creates or adds to a Zip archive. bTemp: false = same folder, true = temp folder, "
	  "or a string output path/filename. Returns the Zip path or an error code." },
	{ "Moo_ZipExtract( sFile {; bTemp ; bOverwrite } )",
	  Moo_ZipExtract,
	  "Moo_ZipExtract( sFile {; bTemp ; bOverwrite } ) - Extracts the first file from a "
	  "Zip archive. Returns the extracted file path or an error code." },
	{ "Moo_ZipList( sZip {; sPattern ; sSeparator } )",
	  Moo_ZipList,
	  "Moo_ZipList( sZip {; sPattern ; sSeparator } ) - Lists the contents of a Zip archive. "
	  "Defaults: sPattern = *.* and sSeparator = |." },
};


/*
 initialise the plug-in
 perform any setup and register functions
 */

const fmx::ptrtype Init ( FMX_ExternCallPtr /* pb */ )
{
	// 古い FileMaker でもロードされるよう、必要十分な API バージョン（FileMaker 19 = 62）を
	// 報告する。使用している関数登録・Text/Data 操作はこれより古くから存在するので
	// 機能的な影響はない。新しい FileMaker でもそのままロードできる。
	// （最新版を名乗りたい場合は kCurrentExtnVersion に変える。kDoNotEnable で無効化も可能）
	fmx::ptrtype enable = k190ExtnVersion;

	for ( const PluginFunctionDef& def : kPluginFunctions ) {
		const fmx::errcode error = RegisterFunction ( def.prototype, def.function, def.description );
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
			pb->result = Init ( pb );
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
			string->SetText ( *PluginOptionsString() );
			break;

		case kFMXT_NameStr:
		case kFMXT_AppConfigStr:
			string->Assign ( PLUGIN_NAME );
			break;
	}

	string->GetUnicode ( (fmx::uint16*)(pb->result), 0, fmx::Text::kSize_End );

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

const fmx::errcode RegisterFunction ( const std::string prototype, const fmx::ExtPluginType function, const std::string description )
{
	fmx::TextUniquePtr function_protoype;
	function_protoype->Assign ( prototype.c_str() );

	fmx::TextUniquePtr function_description;
	function_description->Assign ( description.c_str() );

	fmx::TextUniquePtr name;
	name->SetText ( *FunctionName ( function_protoype ) );

	short required_parameters = 0;
	short optional_parameters = 0;
	NumberOfParameters ( function_protoype, required_parameters, optional_parameters );

	const fmx::uint32 function_flags = fmx::ExprEnv::kDisplayInAllDialogs;

	const fmx::errcode error = fmx::ExprEnv::RegisterExternalFunctionEx ( *PluginID(),
																		 g_next_function,
																		 *name,
																		 *function_protoype,
																		 *function_description,
																		 required_parameters,
																		 required_parameters + optional_parameters,
																		 function_flags,
																		 function
																		 );

	++g_next_function;

	return error;

} // RegisterFunction


// unregister all registered functions

void UnregisterFunctions ( void )
{
	for ( short i = kSPFirstFunction ; i < g_next_function ; i++ ) {
		fmx::ExprEnv::UnRegisterExternalFunction ( *PluginID(), i );
	}
}


// automatically generate the PluginID from the prefix

const fmx::QuadCharUniquePtr PluginID ( void )
{
	fmx::TextUniquePtr prefix;
	prefix->SetText ( *PluginPrefix() );
	char buffer[kSPPluginIDLength];
	prefix->GetBytes ( buffer, kSPPluginIDLength );
	fmx::QuadCharUniquePtr id ( buffer[0], buffer[1], buffer[2], buffer[3] );

	return id;
}


// use the defined prefix if it exists otherwise use the first four characters of the name

const fmx::TextUniquePtr PluginPrefix ( void )
{
	fmx::TextUniquePtr prefix;

#ifdef PLUGIN_PREFIX
	prefix->Assign ( PLUGIN_PREFIX );
#else
	prefix->Assign ( PLUGIN_NAME );
	prefix->DeleteText ( kSPPrefixLength );
#endif

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

	fmx::TextUniquePtr name;
	name->SetText ( *signature, 0, parameters_start );

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
