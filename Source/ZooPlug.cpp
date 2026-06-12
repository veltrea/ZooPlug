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


/* ***************************************************************************

 Public plug-in functions

 **************************************************************************** */

#pragma mark -
#pragma mark Plugin
#pragma mark -

/*
 initialise the plug-in
 perform any setup and register functions
 */

const fmx::ptrtype Init ( FMX_ExternCallPtr /* pb */ )
{
	fmx::errcode error = kSPNoError;
	// 古い FileMaker でもロードされるよう、必要十分な API バージョン（FileMaker 19 = 62）を
	// 報告する。moo_shell が使う関数登録・Text/Data 操作はこれより古くから存在するので
	// 機能的な影響はない。新しい FileMaker でもそのままロードできる。
	// （最新版を名乗りたい場合は kCurrentExtnVersion に変える。kDoNotEnable で無効化も可能）
	fmx::ptrtype enable = k190ExtnVersion;

	/*
	 register plug-in functions

	 functions must always be registered in the same order (to avoid breaking
	 existing calculations in FileMaker).
	 */

	error = RegisterFunction (
		"moo_shell ( command )",
		Moo_Shell,
		"moo_shell ( command ) - Run a one-line shell command and return its output. "
		"On Windows runs via cmd.exe, on macOS via /bin/sh. Reproduces MooPlug's moo_shell." );

	if ( kSPNoError != error ) {
		enable = (fmx::ptrtype)kDoNotEnable;
	}

	// 関数は常に同じ順序で登録すること（既存の計算式を壊さないため）。
	// moo_shell の後に zoo_powershell を追加する。
	error = RegisterFunction (
		"zoo_powershell ( command { ; bCore } )",
		Zoo_PowerShell,
		"zoo_powershell ( command { ; bCore } ) - Run a PowerShell script and return its output as UTF-8. "
		"bCore=True uses PowerShell 7 (pwsh), False (default) uses Windows PowerShell 5.1. "
		"On macOS/Linux always uses pwsh. Uses a temp-file approach that survives Constrained Language Mode "
		"(WDAC). ZooPlug-original; the PowerShell counterpart to moo_shell." );

	if ( kSPNoError != error ) {
		enable = (fmx::ptrtype)kDoNotEnable;
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
