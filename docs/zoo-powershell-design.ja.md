# zoo_powershell 関数 — 設計書

**他の言語で読む:** [English](zoo-powershell-design.md)

> ZooPlug に PowerShell 実行関数 `zoo_powershell` を追加する設計。`zoo_Shell`（cmd.exe / CP932）と
> **対になる別関数**として作る。なぜ 1 本にできないかはこのプロジェクトの検証で
> 確定済み（cmd と PowerShell は出力エンコーディング既定・起動・クオートが
> 両立しないため、単一の関数に 1 つの挙動を焼くとどちらかが必ず破綻する）。
>
> 関連: `Source/ShellExec.*`（zoo_Shell の実装）, `docs/zoo-plug-implementation-spec.md`。

> 🟢 **最終設計は §18（テンポラリファイル方式）。** 実機マトリクス（§17）で、当初案の
> `-EncodedCommand` + `[Console]::OutputEncoding` 強制（§5/§6）も base64 化も **Constrained Language Mode で
> 例外死**することが判明したため降格。**§18 を読むこと。** §1〜§16 は検討の経緯（前提が実測で 2 度覆った記録）。

---

## 1. 位置づけ — なぜ zoo_Shell と分けるのか

`zoo_Shell` を PowerShell に流用できない／すべきでない、決定的な理由:

| 観点 | zoo_Shell（cmd） | zoo_powershell（PowerShell） |
|---|---|---|
| 実行系 | `cmd.exe`（コンソール CP が母語） | `powershell.exe`(5.1) / `pwsh.exe`(7+)（Unicode 母語） |
| **出力の既定エンコーディング** | **CP932（OEM CP）で復号** | **UTF-8 で復号**（PS は Unicode 母語・pwsh7 既定 UTF-8） |
| コマンドの渡し方 | `cmd /S /C "<command>"` | `-EncodedCommand <base64(UTF-16LE)>`（クオート地獄を回避） |
| クオート/エスケープ | `^` エスケープ・`%VAR%` 展開 | バッククォート・`$var`・`@'...'@` |
| 互換目的 | **MooPlug 0.4.9 の `zoo_Shell` 再現** | ZooPlug 独自拡張（MooPlug に存在しない） |

> ⚠️ **訂正（§16 の実機検証による）**: 当初この表は「出力デフォルトが正反対（cmd=CP932 / PowerShell=UTF-8）」
> としていたが、**実測で覆った**。**捕捉する stdout は cmd も PowerShell（5.1・pwsh7 とも）も CP932** だった
> （pwsh7 の utf8 既定はファイル系の話で、パイプ stdout は `[Console]::OutputEncoding`=CP932 が支配）。
> したがって別関数にする本当の理由は「stdout デフォルトの違い」ではなく、**起動方法・クオート・互換目的・
> 版/導入形態ごとの起動可否**の違い。`zoo_powershell` は版に関係なく **UTF-8 を強制（or 出力 base64 化）**
> する必要がある。詳細と実測値は §16。

---

## 2. シグネチャ

> ⚠️ **出荷シグネチャ（最終・`ZooPlug.cpp` に登録されているもの）:**
> ```
> zoo_PowerShell ( command { ; bCore } )
> ```
> 下に列挙する `sEncoding` 引数は **歴史的な記録**。§18 への過程で
> 取り下げられた。テンポラリ方式は `Out-File -Encoding utf8` を無条件で
> 通すので出力は常に UTF-8 になり、復号を切り替えるツマミ自体が仕事を
> 失った。以降の本節は設計の経緯として読むこと。

```
zoo_powershell ( command {; bCore ; sEncoding } )
```

- **command**（String, 必須）— 実行する PowerShell スクリプト/ワンライナー（UTF-8）。
- **bCore**（Boolean, 省略可, 既定 False）— True で **PowerShell 7+（`pwsh.exe`）**、False で **Windows PowerShell 5.1（`powershell.exe`）**。5.1 は Windows に標準同梱なので既定はこちら。
- **sEncoding**（String, 省略可, 既定 `"utf8"`）— 出力の復号方法を上書き。`"utf8"`（既定）/ `"cp932"`（=`"ansi"`）/ `"oem"`。基本は触らない。*（出荷ビルドでは未登録 — 上の注意参照）*

> MooPlug の表記流儀（小文字・省略可は `{ }`）に合わせる。既存の登録パーサ（`NumberOfParameters`）が
> `{ ; ... }` を解釈するので、このプロトタイプ文字列で登録すれば必須/省略可能の数は自動算出される。

戻り値: 標準出力＋標準エラーを結合したテキスト（UTF-8）。改行は CR(`\r`) 正規化（zoo_Shell と同じ）。

---

## 3. 既定の挙動（何も指定しないとき）

`zoo_powershell ( command )` は次を行う:

1. **Windows PowerShell 5.1（`powershell.exe`）** を起動。
2. 起動フラグ: `-NoProfile -NonInteractive -ExecutionPolicy Bypass -OutputFormat Text`
   - `-NoProfile`: ユーザープロファイルを読まない（高速・再現性）。
   - `-NonInteractive`: 対話プロンプトで止まらない。
   - `-ExecutionPolicy Bypass`: スクリプト実行ポリシーで弾かれない。
   - `-OutputFormat Text`: CLIXML でなくプレーンテキスト出力。
3. コマンドは **`-EncodedCommand`（base64 の UTF-16LE）** で渡す（§5）。
4. 出力は **UTF-8 を強制**して受け取り、UTF-8 として復号する（§6）。
5. stdout と stderr を 1 本のパイプにまとめて捕捉（§7）。
6. 改行を CR 正規化し、末尾改行を除去。

---

## 4. 実装方針（純粋ロジック分離・CreateProcessW 共有）

既存 ZooPlug の設計を踏襲する。

```
Source/
├─ ProcessRun.h/.cpp     ★新規・共通。CreateProcessW + パイプで「コマンドラインを起動して
│                          生バイトを捕捉」する低レベル関数。zoo_Shell と zoo_powershell が共有。
├─ ShellExec.*           zoo_Shell（既存）。ProcessRun を使う形にリファクタ可能（任意）。
├─ PowerShellExec.h/.cpp ★新規。RunPowerShell(command_utf8, opts) — FMWrapper 非依存・単体テスト可。
│                          ・コマンドのラップ（UTF-8 強制スクリプトを前置）
│                          ・UTF-16LE → base64 で -EncodedCommand 構築
│                          ・host(powershell/pwsh) 選択
│                          ・出力を sEncoding で復号 + 改行正規化
└─ ZooPlug.cpp           グルー。zoo_powershell を登録し PowerShellExec を呼ぶ。
```

- **`ProcessRun`** に zoo_Shell の Windows 実装（`CreateProcessW` + 匿名パイプ + 生バイト読み）を切り出して共有するのが綺麗。`RunShellCommand` も `RunPowerShell` も「コマンドライン文字列を作って ProcessRun に渡す」だけになる。
- **純粋ロジック層**（PowerShellExec）は FMWrapper に依存しないので、`tests/test_powershellexec.cpp` で FileMaker なしに単体テストする（base64 生成・ラップ文字列の検証など）。
- base64 と UTF-16LE 変換は既存の `MultiByteToWideChar` ヘルパー＋小さな base64 エンコーダ（自前 or 同梱）。

---

## 5. コマンドの渡し方 — `-EncodedCommand` が設計の核心

PowerShell に**任意のスクリプトを安全に渡す唯一クリーンな方法**。これがこの関数の肝。

- ユーザーが FileMaker フィールドから渡す任意のテキストには、引用符・`$`・改行・`;`・バッククォートが
  含まれうる。これを `-Command "..."` に素のまま埋め込むと、**cmd の `^`/`%` とは別系統の
  PowerShell クオート地獄**にはまる（しかも cmd 経由なら二重のエスケープ）。
- **`-EncodedCommand` は、スクリプトを UTF-16LE → base64 した 1 トークンで渡す。** 引用もエスケープも
  一切不要。PowerShell が公式に「クオート問題を避けるため」に用意した仕組み。これで:
  - クオート/エスケープのバグが原理的に発生しない。
  - 改行を含む複数行スクリプトもそのまま渡せる。
  - コマンド文字列自体のエンコーディング欠落も起きない（UTF-16 なので CP932 外の文字も保持）。

### 構築手順（PowerShellExec 内）

```
wrapped = "$OutputEncoding=[Console]::OutputEncoding=[Text.Encoding]::UTF8\n" + <user command(UTF-8→UTF-16)>
b64     = Base64( UTF16LE_bytes(wrapped) )
cmdline = "<host> -NoProfile -NonInteractive -ExecutionPolicy Bypass -OutputFormat Text -EncodedCommand " + b64
```

- 先頭の 1 行で **出力エンコーディングを UTF-8 に固定**（§6）。これも base64 の中に同梱するので、別途
  プリコマンドを挟む必要がない。
- `<host>` = `bCore ? "pwsh.exe" : "powershell.exe"`。

---

## 6. 出力エンコーディング — UTF-8 を強制し UTF-8 で復号（zoo_Shell と正反対）

PowerShell の捕捉時出力エンコーディングは **バージョン・設定依存でブレる**。調査（Microsoft Learn
`about_character_encoding` 等）で判明した生のデフォルト:

- **Windows PowerShell 5.1（生）**: `[Console]::OutputEncoding` は **OEM CP（日本語＝CP932）**、
  ネイティブへパイプする `$OutputEncoding` は **US-ASCII**。**UTF-8 ではない**。しかも cmdlet 間で不統一
  （Set-Content→ANSI/CP932、Export-Csv→ASCII、Out-File→UTF-16LE）。
- **PowerShell 7+（pwsh）**: 破壊的変更で **utf8NoBOM が既定**。**ただしそれは Out-File 等のファイル系**。
  **パイプに流す stdout は別**で、`[Console]::OutputEncoding`（＝日本語機では CP932）が支配する。

→ **「PowerShell だから UTF-8」は誤り。しかも 5.1 だけでなく pwsh 7 でも、捕捉する stdout は CP932 だった
（§16 で実機実証）。** だから版に関係なく任せられない。**UTF-8 を強制**するか、**出力を base64 化**する（§16）。
これが zoo_powershell が UTF-8 を「強制」する根拠:

1. **送信側（PS に強制）**: ラップ先頭で
   `$OutputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8`
   → PS 自身が UTF-8 バイトで stdout に書く。
2. **受信側（プラグインで復号）**: 既定 `sEncoding="utf8"` で UTF-8 → UTF-16 → UTF-8（FileMaker へ）。
3. **保険**: それでも環境差で化ける/レガシーなネイティブ exe を PS 経由で呼ぶ等のケース用に、
   `sEncoding` で `cp932`/`oem` に切替可能（auto 推測はしない。明示指定主義）。

> これは **zoo_Shell の「CP932 復号」と鏡像**。同じ「シェル実行」でもデフォルトが逆になる事実こそ、
> 1 本化できない理由そのもの。「cmd=CP932 / PowerShell=Unicode、
> 別物」という結論を実装で表す。

---

## 7. 標準エラーの扱い

- zoo_Shell と同様、**プロセスの stderr ハンドルを stdout と同じパイプへリダイレクト**して結合捕捉する。
- 注意: PowerShell の**エラーはオブジェクト（ErrorRecord）**で、ネイティブ stderr とは別ストリーム。
  確実にテキストで拾うには、ラップに `$ErrorActionPreference='Continue'` を入れ、必要なら
  ユーザーコマンド末尾に `2>&1`（エラーストリームを成功ストリームへ）を促す。
- 既定の設計: OS レベル stderr をパイプへ束ねる（ネイティブ stderr を確実に拾う）＋
  `$ErrorActionPreference='Continue'` をラップに含める。PS 例外の整形差は仕様として文書化。

---

## 8. パラメータ詳細

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| command | String | Yes | — | PowerShell スクリプト/ワンライナー（UTF-8、複数行可） |
| bCore | Boolean | No | False | True=pwsh(7+) / False=powershell(5.1) |
| sEncoding | String | No | `utf8` | 出力復号: `utf8` / `cp932`(=`ansi`) / `oem` |

- **bCore=True で pwsh が見つからない**場合: 起動失敗 → 空文字列（§10）。将来は明示エラーも検討。
- **sEncoding 不正値**: `utf8` にフォールバック（または明示エラー。要決定 §13）。

---

## 9. macOS / Linux

- PowerShell（`pwsh`）はクロスプラットフォームだが**標準では入っていない**。
- 実装: `pwsh` を `/bin/sh` 経由 or 直接起動。出力は UTF-8 ネイティブなのでそのまま UTF-8 復号。
- `pwsh` 不在なら起動失敗 → 空文字列。**mac/linux では実質「pwsh を入れている人向け」**と文書化。
- zoo_Shell が mac/linux で `/bin/sh` を使うのと違い、zoo_powershell は OS 標準シェルではない点に注意。

---

## 10. エラー・戻り値の契約

- **成功**: stdout+stderr を結合した UTF-8 テキスト（PS 自身のエラー文言もここに混ざりうる）。
- **プロセス起動失敗**（host が無い等）: **空文字列**を返す（zoo_Shell と対称）。
- MooPlug 形式の `関数|Err_N` は **使わない**（zoo_Shell も使っていない＝ZooPlug の shell 系は素直に出力/空を返す方針で統一）。
- 将来、詳細なエラーが要るなら別途 `zoo_powershell_lasterror()` 的な補助関数を足す案（§13）。

---

## 11. セキュリティ

- zoo_Shell と同じく**任意コード実行**。`-ExecutionPolicy Bypass` でスクリプトが実行される。
- **`-EncodedCommand` はサニタイズではない**。クオート事故を防ぐだけで、渡された内容はそのまま実行される。
  信頼できない入力（ユーザー入力フィールド・Web 取得値）からコマンドを組み立てない。
- 文書（README/関数説明）に zoo_Shell と同等のシェルインジェクション警告を明記する。

---

## 12. zoo_Shell との対比（別関数である根拠の一覧）

| | zoo_Shell | zoo_powershell |
|---|---|---|
| 実行系 | cmd.exe | powershell.exe / pwsh.exe |
| 既定出力復号 | **CP932 (OEM)** | **UTF-8** |
| コマンド渡し | `cmd /S /C "..."` | `-EncodedCommand base64(UTF-16LE)` |
| クオート対策 | `/S /C` で外側 1 組剥がし | base64 化で**エスケープ不要** |
| 互換 | MooPlug 0.4.9 再現 | ZooPlug 独自 |
| mac/linux | `/bin/sh`（標準） | `pwsh`（任意導入） |
| 文字化けの主因 | UTF-8 を吐くツールを叩くと化ける | CP932 を吐くレガシー exe を PS 経由で叩くと化ける（→sEncoding で対処） |

**結論**: 既定エンコーディングが正反対・クオート機構が別物・互換目的が別。**統合は不可能で、分離が正解。**

---

## 13. テスト方針

1. **純粋ロジック単体**（FileMaker 不要）: ラップ文字列生成・UTF-16LE 化・base64 が正しいか
   （`tests/test_powershellexec.cpp`、`ctest`）。
2. **Windows 実機**（FileMaker Pro 11/19 or テスト exe）:
   - 日本語の往復: `zoo_powershell("'表予能'")` が UTF-8 `E8 A1 A8 E4 BA 88 E8 83 BD` を返すか
     （zoo_Shell で実証した方法と同じく**16進で検証**）。
   - 複数行スクリプト・`$` や引用符を含むスクリプトが `-EncodedCommand` で壊れず通るか。
   - `bCore=True`（pwsh7）と False（5.1）の両方で出力一致を確認。
   - CP932 を吐くネイティブ exe を PS 経由で呼び、`sEncoding="cp932"` で正しく拾えるか。
3. zoo_Shell と並べ、**同じ日本語が両方で正しく往復**することを確認（CP932 経路 / UTF-8 経路の両立確認）。

---

## 14. 未決定事項（着手前に決める）

1. **エラー契約**: 起動失敗は空文字列で良いか（zoo_Shell 対称案）、`zoo_powershell|Err_N` を導入するか。
2. **sEncoding 不正値**: フォールバック（utf8）か明示エラーか。
3. **stderr**: OS stderr 束ね + `$ErrorActionPreference=Continue` で足りるか、`2>&1` を自動付与するか。
4. **bCore 既定**: 5.1（標準同梱・確実）で良いか。pwsh7 を優先したい運用があるか。
5. **タイムアウト**: 計算スレッドをブロックする以上、上限タイムアウト引数を足すか（zoo_Shell にも共通の課題）。
6. **関数名**: `zoo_powershell` で確定か（`moo_` でなく `zoo_` プレフィックス＝ZooPlug 独自関数の明示）。
7. **汎用 `zoo_exec` を併設するか**（§15 の業界パターンを踏まえた選択）。

---

## 15. 付録: 業界の実装パターンと ZooPlug の選択（調査の裏付け）

「cmd と PowerShell をどう扱うか」を他のプラグイン/言語ランタイムで調べた結果（2026-06-12、Web 調査 + 反証検証）。
**結論: 業界の主流は「別関数に分ける」ではなく「1 つの汎用実行関数 + エンコーディングを引数化」だった。**
ただし ZooPlug の事情（zoo_Shell が MooPlug 互換で凍結）では、別関数 `zoo_powershell` を足すのが正しい。

### 各実装の実態

| 実装 | やり方 | エンコーディング | 出典 |
|---|---|---|---|
| **MBS Plugin** | **汎用 `Shell.Execute(実行ファイルパス)`**。cmd.exe / powershell.exe / sh を**自分で渡す**。シェル別関数は無い | `Shell.ReadOutputText(shell; encoding)` で**読み取り時に指定**（UTF-8/ANSI/Native…、内部 iconv、CP932/Shift_JIS 対応、v11.2+ は UTF-16 BOM 自動判定） | mbsplugins.eu/component_Shell.shtml ほか |
| **BaseElements** | **単一 `BE_ExecuteSystemCommand`**。Poco::Process::launch、**生バイトをそのまま**返す（復号なし） | **引数なし**。OS ネイティブ（日本語＝CP932）任せ → **UTF-8 を吐くツールでは化けるリスク** | github.com/GoyaPtyLtd/BaseElements-Plugin |
| **Python subprocess** | `shell=` と `encoding=` を**別々の引数**で指定 | `encoding='cp932'` 等を呼び出し側が指定 | docs.python.org/3/library/subprocess |
| **Node child_process / execa** | `shell`（bool/パス）と `encoding`（既定 utf8）を**引数** | 同上 | nodejs.org/api/child_process |
| **Rust std::process / Go os/exec** | シェル引数なし。`cmd`/`sh` を**自分で起動**、エンコーディングは I/O 層で処理 | 呼び出し側管理 | doc.rust-lang.org / pkg.go.dev/os/exec |

→ **支配的パターン = 「shell とエンコーディングを分離して引数化」**（Python/Node/execa/MBS）。「シェルごとに別関数」は少数派。

### この調査で総括が修正された点

- 「他の人は cmd と PowerShell を**別プラグイン/別関数に分けている**」は**方向は正しいが、最頻パターンではない**。
  実際の主流は **MBS 流の「汎用実行 + 読み取り時エンコーディング指定」**。BaseElements は単一関数で**復号すらしない**
  （ゆえに UTF-8 ツールで化ける）。
- 「PowerShell は UTF-8 が既定」は **pwsh7 と設定済み 5.1 に限った話**。素の 5.1 は OEM/ASCII（§6）。
- ただし「**単一のハードコード既定では cmd と PowerShell の両方を正しく扱えない**」という核心は**実機計測 +
  Microsoft 公式ドキュメントで裏付け済み**（cmd=CP932 / pwsh7=UTF-8、起動・クオートも別系統）。

### ZooPlug の設計判断（裏付けを踏まえて）

1. **zoo_Shell は凍結**（MooPlug 0.4.9 互換。cmd/CP932/引数固定）。ここに引数を足すと互換が崩れる → **触らない**。
2. **zoo_powershell を別関数で追加**（本設計）。既定 UTF-8・`-EncodedCommand`。zoo_Shell と対。
   - BaseElements の「生バイト垂れ流し」より一段堅い（明示的に UTF-8 強制 + 復号）。
3. **将来案: 汎用 `zoo_exec ( program ; args {; sEncoding ; sStdin } )` を併設**（MBS/Python 流）。
   - 任意の実行ファイル + エンコーディング引数。最も柔軟で業界標準的。
   - zoo_Shell / zoo_powershell は「よく使う組合せの薄いラッパ」として残し、`zoo_exec` を土台にできる。
   - これを採るなら §4 の `ProcessRun` がそのまま `zoo_exec` の本体になる（設計が一直線に繋がる）。

> まとめ: **「分ける/分けない」は二択ではない。** 主流は「汎用 + エンコーディング引数」。ZooPlug は
> 「zoo_Shell（凍結）+ zoo_powershell（対）」を足しつつ、土台に汎用 `zoo_exec` を据えるのが、互換性と
> 業界標準の両取り。

---

## 16. 実機検証マトリクス（決定的・机上設計を2点覆した）

> Windows 実機（日本語 Windows 11 / ja-JP）で、プラグインと同じ `CreateProcessW + CREATE_NO_WINDOW + パイプ`
> で 3 ホストを起動し、**生バイトを 16 進で**観測（2026-06-12）。論理だけの設計が版マトリクスで崩れることを実証。

3 つの「PowerShell」（同一マシンに同居）:
- **Windows PowerShell 5.1**（`powershell.exe`、.NET Framework、in-box、凍結）
- **pwsh 7.5.4（MSI 版）**（`C:\Program Files\PowerShell\7\pwsh.exe`）
- **pwsh 7.5.4（MSIX/Store 版エイリアス）**（`...\WindowsApps\pwsh.exe`）

テスト語: 表予能（UTF-8 `E8 A1 A8 E4 BA 88 E8 83 BD` / CP932 `95 5C 97 5C 94 5C`）。

| ホスト | (a) 何もしない | (b) `[Console]::OutputEncoding=UTF8` 強制 | (c) 出力を base64 |
|---|---|---|---|
| WinPS 5.1 | **CP932** (`95 5C 97 5C 94 5C`) | UTF-8 ✓ | ASCII ✓ |
| pwsh 7.5 MSI | **CP932** (`95 5C 97 5C 94 5C`) | UTF-8 ✓ | ASCII ✓ |
| pwsh 7.5 MSIX | **`<CreateProcessW FAILED gle=15612>`** | 起動不能 | 起動不能 |

### 覆された前提 ①: 「pwsh 7 は UTF-8 既定」は捕捉 stdout には当てはまらない

- **pwsh 7 MSI ですら、パイプに流す stdout は CP932 だった**（`95 5C…`）。`utf8NoBOM` 既定は
  Out-File 等の**ファイル系**の話で、**コンソール/stdout は `[Console]::OutputEncoding`（日本語機＝CP932）が支配**する。
- → 「cmd=CP932 / pwsh=UTF-8 で既定が正反対」という、別関数化の論拠の一部に使っていた図式は、
  **捕捉経路では誤り**（両方 CP932）。両者の本当の違いは**起動方法・クオート・版/導入形態の起動可否**であって、
  stdout のデフォルト文字コードではない。
- → **zoo_powershell は「PS だから UTF-8」を前提にできない。版に関係なく UTF-8 を強制するか、出力を base64 化する**
  （次項）。

### 覆された前提 ②: MSIX/Store 版 pwsh は CreateProcessW で起動できない

- WindowsApps の `pwsh.exe` は**アプリ実行エイリアス（0 バイトの再解析ポイント）**で、
  シェル経由の起動を前提とする。**プラグインの `CreateProcessW(パス)` では起動不能**（`gle=15612`）。
- → ユーザーが **Store/MSIX 版 pwsh しか持っていない**と、zoo_powershell は**エンコーディング以前に起動段階で失敗**する。
- 対策: 実体パス（`C:\Program Files\PowerShell\7\pwsh.exe` 等）を**解決して起動**する／見つからなければ
  **明示エラー**を返す。`bCore=True` で素朴にエイリアスパスを叩く設計は不可。

### 生き残った設計と、堅牢化の結論

- **`[Console]::OutputEncoding=UTF8` 強制は 5.1 でも pwsh7 MSI でも例外なく機能**（無コンソールでも効いた）。
  → 起動さえできれば、UTF-8 強制は有効。「無コンソールで壊れるかも」という懸念は 5.1/MSI では外れた。
- **出力 base64 化（c）は全ホストで ASCII になり、エンコーディングを一切経由しない最も堅い経路**
  （ただし起動失敗は救えない）。`-EncodedCommand`（入力 base64）と対称に、**出力も base64** にすれば
  コンソール/ストリームのエンコーディング差を完全に回避できる。
- **改訂後の zoo_powershell 必須要件**:
  1. **起動ホストの解決**: powershell.exe(5.1) / 実体 pwsh.exe を探索。MSIX エイリアスは避ける。不在は明示エラー。
  2. **出力エンコーディングの確定**: UTF-8 強制 **＋**（最堅牢を採るなら）出力 base64 + プラグイン側 base64 復号。
  3. **版・導入形態は検証してから対応を名乗る**: 1 セル（5.1）だけの成功で全 PowerShell 対応を主張しない。

### 検証状況（正直な現状）

| セル | 状態 |
|---|---|
| WinPS 5.1 / FullLanguage | ✅ 検証済み（強制 UTF-8・base64 とも動作） |
| pwsh 7.5 MSI / FullLanguage | ✅ 検証済み（同上。ただし**素の stdout は CP932**） |
| pwsh 7.5 MSIX(Store) | ⛔ **CreateProcessW 起動不能（gle=15612）** |
| Constrained Language Mode / WDAC / AppLocker | ❓ 未検証（`-EncodedCommand` 禁止の可能性。要実機） |
| pwsh 6.x / 7.0–7.4 / ARM64 / dotnet global tool / portable zip | ❓ 未検証 |

> 教訓: **「論理的に正しい」は版マトリクスで容易に崩れる。** 実機計測で前提が 2 つ覆った（pwsh の stdout が
> CP932、MSIX が起動不能）。PowerShell 対応は「想定」でなく**セルごとの実測**を積んで初めて主張できる。

---

## 17. 全バージョン × エンコーディング × Constrained Language Mode 実測（2026-06-12）

> Windows 実機に **7 ホスト**（5.1 / 6.2.7 / 7.0.3 / 7.2.6 / 7.4.6 / 7.5 MSI / 7.5 MSIX）を side-by-side で導入し、
> 汎用ランチャ `runraw.exe`（`CreateProcessW + CREATE_NO_WINDOW + パイプ`）で生バイトを 16 進測定。
> テスト語 表予能（UTF-8 `E8A1A8E4BA88E883BD` / CP932 `955C975C945C`）。

### A. ホスト × 出力制御法（FullLanguage）— 全版で挙動が一致

| 方法 | 5.1 | 6.2.7 | 7.0.3 | 7.2.6 | 7.4.6 | 7.5 MSI | 7.5 MSIX |
|---|---|---|---|---|---|---|---|
| (a) 無制御 | **CP932** | CP932 | CP932 | CP932 | CP932 | CP932 | 起動不能 |
| (b) `[Console]::OutputEncoding=UTF8` | UTF-8 | UTF-8 | UTF-8 | UTF-8 | UTF-8 | UTF-8 | 起動不能 |
| (c) 出力 base64 | base64 | base64 | base64 | base64 | base64 | base64 | 起動不能 |
| (d) `$OutputEncoding=UTF8` のみ | **CP932** | CP932 | CP932 | CP932 | CP932 | CP932 | 起動不能 |

**判明:**
1. **全 PowerShell（5.1〜7.5）が、捕捉 stdout の既定で CP932 を吐く。** pwsh 7 系も例外なし。「pwsh=UTF-8」は捕捉経路では完全に誤り（版に一切依存しない）。
2. **`$OutputEncoding` は捕捉 stdout を制御しない**（CP932 のまま）。**効くのは `[Console]::OutputEncoding` だけ**。
   → 設計のラップから `$OutputEncoding=…` は削除可（無害だが無意味）。`[Console]::OutputEncoding` を使う。
3. (b)(c) は FullLanguage では**全版で機能**。MSIX は**全行で起動不能**（gle 15612）。

### B. Constrained Language Mode（5.1 / 7.4.6）— ★設計の生死を分けた

| 方法 | 5.1（CLM） | 7.4.6（CLM） |
|---|---|---|
| 素の出力 | CP932（出る） | CP932（出る） |
| (b) `[Console]::OutputEncoding=UTF8` 強制 | **PS-ERROR**（"プロパティを設定できません"）→ CP932 のまま | **PS-ERROR**（InvalidOperation）→ CP932 のまま |
| (c) base64（`[Convert]`/`[Text.Encoding]`） | **PS-ERROR exit=1**（型アクセス禁止） | **PS-ERROR exit=1** |
| (d) `Out-File -Encoding utf8` → ファイル読み | **UTF-8 ✓**（ただし先頭に BOM `EFBBBF`） | **UTF-8 ✓**（BOM なし） |

**判明（決定的）:**
- **CLM では (b) も (c) も死ぬ。** `[Console]::OutputEncoding=…` も `[Convert]`/`[Text.Encoding]` も
  **.NET 型のメソッド/プロパティアクセス**であり、CLM が禁止する → どちらも例外。設計の主力 2 案が同時に全滅。
- **生き残るのは cmdlet 経由のみ。** `… | Out-File -Encoding utf8 <file>`（cmdlet はCLMでも許可）で
  ファイルに UTF-8 で書き、**プラグインがそのファイルを読む**。これは FullLanguage でも CLM でも全版で成立。
- **5.1 の `Out-File -Encoding utf8` は BOM を付ける**（7.x は付けない）→ プラグイン側で BOM 除去が必要。

### C. コマンド渡し（7.4.6）

- `-Command` / `-EncodedCommand` とも FullLanguage で UTF-8 ✓。
- `__PSLockdownPolicy=4` 環境変数では **CLM を誘発できなかった**（UTF-8 のまま）。
  → 実運用の CLM は **WDAC/AppLocker のスクリプト施行**で課される。`__PSLockdownPolicy` は今の Windows では不十分。
  **WDAC スクリプト施行下では `-Command`/`-EncodedCommand` 自体が拒否されうる**が、**これはポリシー配備が要るため未検証**（既知の穴）。

### D. PowerShell が native exe を中継（7.4.6）

- `cmd /c echo 表`（cmd が CP932 で出力）を pwsh が中継 → 既定では `95 5C`（CP932 素通し）、
  `[Console]::OutputEncoding=UTF8` 強制時は `E8 A1 A8`（正しく UTF-8 化）。FullLanguage では強制が中継も正す。

### 改訂版・堅牢設計（matrix を踏まえた結論）

1. **起動ホストの解決**: `powershell.exe`（5.1）/ 実体 `pwsh.exe`（`C:\Program Files\PowerShell\7\` 等）を探索。
   **MSIX エイリアスは `CreateProcessW` 不可** → 実体を探すか明示エラー。
2. **エンコーディング確定は 2 層**:
   - **既定（高速・FullLanguage 想定）**: `[Console]::OutputEncoding=UTF8` 強制 + UTF-8 復号（全版で動作）。
   - **CLM 安全モード（普遍）**: ユーザーコマンド出力を `Out-File -Encoding utf8 <temp>`（cmdlet）でファイルへ書き、
     **プラグインが temp を読む**。**FullLanguage でも CLM でも全版で成立する唯一の経路**。**5.1 の BOM 除去**を入れる。
   - → 配布物では「**ファイル経由（Out-File cmdlet）を既定**」にするのが最も堅い。console 強制/base64 は CLM で死ぬため、
     ロックダウン環境を想定するなら採れない。
3. **未検証の穴**: WDAC スクリプト施行（`-Command`/`-EncodedCommand` 拒否）、pwsh 6.0/7.1/7.3、ARM64、
   dotnet global tool 版。「対応」を名乗る前にセルごとに実測する。

### 検証状況マトリクス（更新）

| セル | 状態 |
|---|---|
| 5.1 / 6.2 / 7.0 / 7.2 / 7.4 / 7.5MSI × FullLanguage | ✅ 全版 CP932 既定・(b)(c) 動作・MSIX のみ起動不能 |
| 5.1 / 7.4 × ConstrainedLanguage | ✅ 測定：**(b)(c) は死亡、Out-File cmdlet のみ生存（5.1 は BOM）** |
| WDAC / AppLocker スクリプト施行 | ❓ 未検証（ポリシー配備要。`-EncodedCommand` 拒否の可能性） |
| pwsh 6.0 / 7.1 / 7.3 / ARM64 / dotnet tool | ❓ 未検証 |

> 結論: **論理設計の主力 2 案は CLM で全滅した。** 版・言語モードを跨いで唯一普遍に効くのは
> 「**cmdlet `Out-File -Encoding utf8` でファイルに書き、プラグインが読む**」経路（+5.1 の BOM 除去）。
> 「今の想定では対応しきれない」というロジック駆動案の限界が、CLM で実証された。

---

## 18. 確定アーキテクチャ — テンポラリファイル方式（実証済み・主設計）

> §5/§6 の `-EncodedCommand` + `[Console]::OutputEncoding` 強制案は **CLM で死ぬ**（§17 B 参照）ため降格。
> **本章が zoo_powershell の正式な主設計。** 2026-06-12、全 6 版 × FullLanguage/CLM で実機実証済み。

### 方式

1. プラグインが、ユーザーのスクリプトを **temp .ps1（UTF-8 **BOM付き**）** に書き出す。
   - スクリプト末尾で結果を **`... | Out-File -Encoding utf8 -FilePath <out.txt>`**（cmdlet）に流す形にラップ。
   - stderr も拾うなら `<user command> 2>&1 | Out-File ...`。
2. `<host> -NoProfile -NonInteractive -ExecutionPolicy Bypass -File <temp.ps1>` を
   `CreateProcessW + CREATE_NO_WINDOW + パイプ`（既存 ProcessRun）で起動。
3. プラグインが **out.txt を読む**（UTF-8）。**5.1 が付ける BOM を除去**し、改行を CR 正規化して返す。
4. temp.ps1 / out.txt は一意名（並行呼び出し対策）にし、終了後に削除。

### なぜこれが全制約を溶かすか（実証対応）

| かつての制約 | テンポラリ方式での扱い | 実証 |
|---|---|---|
| 全版 stdout 既定 CP932 | **stdout を使わない**。出力はファイル（Out-File -Encoding utf8） | §18 表で全版 UTF-8 |
| `[Console]` 強制が CLM で例外死 | **使わない**。.NET 型アクセスをどこにも持たない | CLM で UTF-8✓ |
| base64(.NET) が CLM で例外死 | **使わない** | 同上 |
| `-Command` のクオート地獄 | **`-File` で渡す**。引用・エスケープ一切不要 | 多行・日本語リテラルが通る |
| .ps1 入力の文字化け | **UTF-8 BOM付きで書く**（5.1 対策） | 負の対照で BOM 必須を実証 |

### 実証データ（§17 と同マシン）

```
BOM付き / Full :  5.1=UTF-8(+BOM)  6.2=UTF-8  7.0=UTF-8  7.2=UTF-8  7.4=UTF-8  7.5MSI=UTF-8
BOM付き / CLM  :  5.1=UTF-8(+BOM)  7.4=UTF-8
NO-BOM / Full :  5.1=GARBLED(!)   7.4=UTF-8     ← .ps1 は BOM付きが必須（5.1）
```

### 実装の必須事項（チェックリスト）

- [ ] temp .ps1 は **UTF-8 with BOM** で書く（5.1 がリテラルを CP932 誤読しないため）
- [ ] 出力は **Out-File -Encoding utf8**（cmdlet＝CLM 安全）。`[Console]`/`[Convert]`/`[Text.Encoding]` を**使わない**
- [ ] 読み取った out.txt の **先頭 BOM(`EF BB BF`) を除去**（5.1 が付ける）
- [ ] 起動ホストは **5.1(`powershell.exe`) を既定**（必ず起動可）。pwsh は**実体パス解決**、MSIX エイリアスは使わない
- [ ] temp/out は**一意名**＋**後始末**。書き込み可能な temp ディレクトリを選ぶ
- [ ] stderr は `2>&1` でファイルへ混ぜる（or 別 out.err にして結合）
- [ ] 残ガード: WDAC/AppLocker 施行下は temp スクリプトが弾かれうる → **明示エラーで落ちる**（"ロックダウン環境は非対応"）

### 残る穴（未検証）

- **WDAC/AppLocker スクリプト施行**: temp パスの .ps1 はむしろ弾かれやすい。ここは「落ちるべき」環境として
  明示エラー設計にする。実測は要ポリシー配備（別途）。
- pwsh 6.0 / 7.1 / 7.3 / ARM64 / dotnet tool 版（FullLanguage で動く公算大だが未測）。

> 総括: **encoding と言語モードの軸では「テンポラリファイル方式」で実質制約が消える**ことを実証した。
> `[Console]` 強制 / base64 という API 小細工は CLM で全滅したが、「**全部ファイルでやる**」が一刀両断にした。
> 残るのは launch 解決（5.1 床で吸収）と WDAC 天井（明示エラー）のみ。

---

## 19. AppLocker 実測 — 生成パス基準の確定（Test-AppLockerPolicy で検証）

> Windows 実機（Win11 Pro）で実測（2026-06-12）。`Test-AppLockerPolicy`（**起動せずポリシー判定する公式コマンド**＝
> AppLocker と同じ評価ロジック）で「標準ユーザーが各パスの .ps1 を実行できるか」を測った。
> ポリシー = 既定スクリプトルール（Windows / Program Files / 管理者は許可、他は既定で拒否）＋
> **`%OSDRIVE%\ProgramData\ZooPlug\*` を許可**。

| パス | 標準ユーザー | 管理者 |
|---|---|---|
| `C:\ProgramData\ZooPlug\…`（許可登録した的） | **Allowed**（rule="ZooPlug allowed"） | Allowed |
| `C:\ProgramData\NotAllowed\…`（temp に見えない名前） | **DeniedByDefault** | Allowed（管理者除外） |
| `%TEMP%\…` | **DeniedByDefault** | Allowed |
| `C:\Users\Public\…` | **DeniedByDefault** | Allowed |
| `C:\Windows\Temp\…` | Allowed（%WINDIR% 既定ルール。但し標準ユーザー書込不可で実用外） | Allowed |
| `C:\Program Files\…` | Allowed（既定ルール。但し実行時書込不可） | Allowed |

### 確定した結論

1. **懸念は実在した**: `%TEMP%` / `Public` / 任意の ProgramData サブフォルダは、AppLocker スクリプト施行下で
   標準ユーザーには **DeniedByDefault（実行不可）**。**`%TEMP%` に temp スクリプトを書く設計は、ロックダウン環境で確実に死ぬ**。
2. **「temp に見えない名前」では解決しない**（確定）: `ProgramData\NotAllowed` という temp らしくない名前でも
   **DeniedByDefault**。判定は**許可リスト方式でパス名ヒューリスティックではない**。名前を変えても無意味。
3. **正解の基準が実証された**: **固定パス `%PROGRAMDATA%\ZooPlug\scripts\` に書き、管理者が 1 ルールだけ許可登録**すれば、
   標準ユーザーでも **Allowed**。レバーは「名前」でなく「**許可登録できる固定パス＋管理者の 1 ルール**」。

### → 生成パス基準（確定・実装必須）

```
ZooPlug は生成スクリプト/出力ファイルを必ず  %PROGRAMDATA%\ZooPlug\scripts\  に置く。
%TEMP% / %LOCALAPPDATA% / Public は使わない。
ロックダウン環境の管理者向けに、配布文書へ次の 1 ルールを明記する:
  AppLocker/WDAC のスクリプト許可に  %OSDRIVE%\ProgramData\ZooPlug\*  を追加。
```

- `C:\ProgramData` は標準ユーザーも書込可（既定 ACL）かつ**安定した固定パス**＝許可登録の的にできる。
- 非ロックダウン環境（大多数）では、ProgramData\ZooPlug は普通に書込・実行でき、何の追加設定も要らない。
- ロックダウン環境では、上記 1 ルールで開通。許可登録も無く完全ブロックされた時のみ明示エラー。

### 検証の限界（正直に）

- `Test-AppLockerPolicy` は**ポリシー判定ロジック**を権威的に評価する（エディション非依存）。
  → 「固定 ProgramData パス＋許可ルール」戦略の正しさは**確定**。
- ただし **Win11 Pro の実行時 enforcement そのものはこの測定では未測**（判定ロジックの評価に留まる）。実 enforcement は
  Enterprise/Education の AppLocker か WDAC で起き、そこでの**判定ロジックは上の評価と同一**なので戦略は通る。
- WDAC スクリプト施行（多くは CLM 強制で、§18 のテンポラリ方式は CLM を生存済み）は別途・要ポリシー配備。

---

## 20. AppLocker 実行時 enforcement の実測 — 「Win11 Pro は施行しない」を実機確定（2026-06-12）

> §19 の「限界」だった**実行時 enforcement そのもの**を、今回 Windows 実機（Windows 11 **Pro**）で実測した。
> 前回ブロックしていた非対話セッションのウィンドウステーション問題（`0xC0000142`）は、
> **事前作成した標準ユーザーアカウントを「タスクスケジューラ」で起動**することで解決
> （スケジュールタスクは標準ユーザーのトークン + 非対話ウィンドウステーションで起動できる）。
> ハーネスは検証用ハーネス（marker 方式・try/finally で必ず revert）。

### 方法（marker 方式）

1. 標準ユーザーアカウントに `SeBatchLogonRight` を `secedit` で付与（`schtasks /ru /rp` は**自動付与しない**）。
2. 許可パス `C:\ProgramData\ZooPlug\scripts\`・拒否パス `C:\ProgramData\NotAllowed\`・`C:\Users\Public\…` に
   テスト .ps1 を置く。各 .ps1 は最初に marker（`C:\Users\Public\zoo_<tag>.txt`、LanguageMode 込み）を書く。
3. 各 .ps1 を **標準ユーザーのスケジュールタスク**（`/rl LIMITED`＝標準トークン）で起動 → marker の有無で判定。
   marker あり＝実行された / marker 無し＝AppLocker がブロックした。
4. ポリシー無し（SANITY）→ Script 施行 Enabled（DEPLOY）→ 再テスト、を同一ハーネスで比較。

### 実測結果（決定的）

| フェーズ | allowed (ProgramData\ZooPlug) | notallowed (ProgramData\NotAllowed) | public |
|---|---|---|---|
| ポリシー無し（SANITY） | RAN / FullLanguage | RAN / FullLanguage | RAN / FullLanguage |
| **Script `EnforcementMode="Enabled"`** | **RAN / FullLanguage** | **RAN / FullLanguage**（本来ブロックされるべき） | **RAN / FullLanguage** |
| AppLocker イベント | — | **0 件**（8003/8004/8007 一切なし） | — |

- effective policy は `Type="Script" EnforcementMode="Enabled"`、`AppIDSvc=Running` を確認済み。
  **それでも**拒否パスのスクリプトが FullLanguage で素通りし、AppLocker のブロック/監査イベントが**1 件も出ない**。

### 確定した結論

1. **Windows 11 Pro は AppLocker スクリプト施行を実行時に enforce しない**（実機確定）。
   ポリシーを「Enabled」に設定でき・サービスも動くが、**判定も遮断もイベントも発生しない**。
   Microsoft 公式の「AppLocker enforcement は Enterprise/Education のみ」を実測で裏付けた。
2. したがって **Pro 上では AppLocker は ZooPlug の脅威にならない**（temp .ps1 はどのパスでも走る）。
   §19 の `%PROGRAMDATA%\ZooPlug\*` 許可リストは **Enterprise/Education では必要かつ正しい**
   （Test-AppLockerPolicy のロジック検証＝§19 は edition 非依存で確定済み）。Pro では「あっても無害・効果なし」。
3. **Pro で実際に効くロックダウンは WDAC（CI/UMCI）**。WDAC のユーザーモード CI（スクリプト施行）が
   無効の標準的な Win11 環境では、WDAC のスクリプト施行も効いていない。
   §18 のテンポラリ方式が WDAC 施行下で通るかは**次の天井**（§21 予定）。

### 副産物（ハーネスの教訓）

- `schtasks /create /ru <user> /rp <pass>` は **`SeBatchLogonRight` を付与しない**（警告のみ／タスクは未実行=`0x41303`）。
  `secedit /export → 行を編集 → /configure` で明示付与する（INF は **UTF-16LE+BOM**）。REVERT で元の SID 列に戻す。
- 標準ユーザーを非対話セッションから起動できない問題は、**タスクスケジューラ起動**が正解
  （`CreateProcessWithLogonW` 直接起動は `0xC0000142` で不可）。

---

## 21. CLM 生存の独立再確認（self-imposed ConstrainedLanguage・安全経路）（2026-06-12）

> §20 で「Pro は AppLocker を施行しない／本命は WDAC」と判明したが、WDAC 実配備は再起動を伴う
> 実リスクがある。そこでまず**再起動なしの安全経路**で「§18 が CLM 下で生き残るか」を
> 独立に再確認した。検証用ハーネスを用い、実行は標準ユーザー×タスクスケジューラ。

### CLM の誘発方法（重要な実測メモ）

- **`__PSLockdownPolicy=4`（env var）はこの環境（Win11 Pro, 5.1 / pwsh7.5.4）で CLM を誘発しなかった**
  （プロセス env で設定しても `mode=FullLanguage` のまま）。古い PS の test hook は当てにできない。
- 代わりに **temp .ps1 の先頭で `$ExecutionContext.SessionState.LanguageMode = 'ConstrainedLanguage'` に自前降格**
  してから §18 本体を走らせた。これは CLM の**言語制約を忠実に再現**する安全な方法（downgrade は常に許可・unset 不可）。

### 実測結果（5.1 / pwsh7 とも CLM で UTF-8 往復が無傷）

| ホスト | CLM 下の mode | `表予能` の hex | BOM |
|---|---|---|---|
| 5.1 (`powershell.exe`) | **ConstrainedLanguage** | `E8 A1 A8 E4 BA 88 E8 83 BD` ✓ | 5.1 が付けた BOM を除去 ✓ |
| pwsh7 (`pwsh.exe`) | **ConstrainedLanguage** | `E8 A1 A8 E4 BA 88 E8 83 BD` ✓ | BOM 無し ✓ |

- §18 本体（`& { <user> } 2>&1 | Out-File -FilePath … -Encoding utf8` → 読取 → 先頭 BOM 除去）は
  **CLM-legal な構成だけ**（cmdlet / 文字列連結 / `$ExecutionContext` の property 参照）なので CLM で例外を出さず、
  日本語が UTF-8 で完全往復した。`Out-File -Width 8192` の折返し対策も込み。

### 結論と限界

1. **§18 はテンポラリ方式＋Out-File-utf8＋BOM 除去で CLM 下でも壊れない**（5.1/pwsh7・標準ユーザーで実証）。
   WDAC が CLM を強制する設定なら §18 は通る公算が**強く裏付いた**。
2. **限界（正直に）**: self-imposed CLM は CLM の*言語制約*を再現するが、WDAC ポリシー由来 CLM の
   **ファイル信頼層**（未署名 .ps1 のブロック可否・dot-source/`& {}` の信頼判定）までは再現しない。
   これは §20 で deferred とした**実 WDAC 配備（再起動必須）**でしか確証できない。
   → ユーザーのリスク承認が得られたら、`AllowMicrosoft` ベース + UMCI 有効ポリシーを配備して実測する（§22 予定）。

---

## 22. WDAC 実配備の実測 — 「enforce 下でも §18 は通る」を実機確定（最後の天井）（2026-06-13）

> §21 で積み残した **WDAC 由来 CLM のファイル信頼層** を、実 WDAC ポリシーを Windows 実機（Win11 Pro）へ
> 配備・再起動して実測した。検証用ハーネス（preflight → build_deploy → setup_failsafe
> → to_enforce → measure → revert → verify）を用いた。**段階的・安全網付き**で実施し、完全クリーン復帰を確認済み。

### 安全に実施できた根拠（preflight で確認）

- 未署名ポリシーを使えるブート構成かを事前確認（回復が容易な状態であることを確かめてから配備）。
- ポリシーに **option 10「Boot Audit on Failure」+ 9「Advanced Boot Options」** を付与（ブート必須コードを弾いても起動が死なない）。
- **起動時 SYSTEM タスクの自動復旧フェイルセーフ**（12分後に未 disarm なら `CiTool --remove-policy` + 再起動）を arm。
- 計測/復旧スクリプトは **完全 CLM-safe**（.NET メソッド/静的呼び出しゼロ、cmdlet と演算子のみ）で書き、enforce(=orchestrator も CLM)下でも完走。

### 方法

`AllowMicrosoft` を base に **UMCI 有効 + Script Enforcement 有効**（option 11 を外す）の WDAC ポリシーを CiTool で配備。
Stage1=Audit（option 3 有）→ 再起動 → 実測 → Stage2=Enforce（同 GUID で option 3 削除）→ 再起動 → 実測。
§18 の temp.ps1（未署名・自前 CLM 降格なし）を標準ユーザー×タスクスケジューラで起動し、観測される LanguageMode と
日本語 UTF-8 往復（`表予能`=`E8 A1 A8 E4 BA 88 E8 83 BD`）を検証。

### 実測結果

| ホスト | Audit (UMCI=1) | **Enforce (UMCI=2)** | UTF-8 往復 |
|---|---|---|---|
| 5.1 (`powershell.exe`) | FullLanguage（audit では非 CLM） | **ConstrainedLanguage** | ✓（BOM 除去込み） |
| pwsh7 (`pwsh.exe`) | ConstrainedLanguage（audit でも CLM） | **ConstrainedLanguage** | ✓（BOM 無し） |

- **enforce 下でも未署名 temp.ps1 は「ブロックされず・CLM で実行」された**（両ホスト）。`Out-File -Encoding utf8` の
  日本語往復は完全に無傷。orchestrator 自身（未署名 .ps1）も `ConstrainedLanguage` で動いたが CLM-safe 実装で完走。
- CodeIntegrity に **3077（enforce ブロック）** が出るが、対象は**未署名 DLL/exe**であり、
  **PowerShell スクリプト(.ps1) は 3077 ブロック対象ではなく CLM 化されるのみ**＝WDAC のスクリプト施行の本質。
- revert 後 **UMCI=0 完全復帰**を確認。

### 確定した結論（§18 設計の最終裏付け）

1. **Win11 Pro は WDAC を実行時に enforce する**（AppLocker=§20 とは対照的）。Pro のロックダウンの本命はやはり WDAC。
2. **real WDAC enforce 下でも §18 テンポラリ方式は通る**：未署名 .ps1 はブロックされず、5.1/pwsh7 とも CLM で実行され、
   `Out-File -Encoding utf8` 経由の UTF-8 往復が壊れない。§21 の積み残し（ファイル信頼層）は**解消＝合格**。
3. ゆえに ZooPlug `zoo_powershell` は **AppLocker（§19/§20）と WDAC（§22）の両天井を §18+§19 で満たす**：
   - §18 の temp.ps1 は **CLM-safe を厳守**（`[Console]`/`[Convert]`/`[Text.Encoding]`/`Add-Type` を**生成スクリプトに絶対入れない**。
     §5/§6 の旧案を捨てた判断がここでも正しい）。
   - §19 の `%PROGRAMDATA%\ZooPlug\*` 許可は **AppLocker(Enterprise/Education) のパス施行**向け。WDAC はパスでなく
     信頼でCLM化するので、§19 とは別レイヤだが両立する。
4. **実装上の含意**: `PowerShellExec` が生成する temp.ps1 は cmdlet・文字列演算のみで構成し、.NET 型アクセスを
   一切含めないこと（テストで CLM 実行を必ず確認する）。ホスト(5.1/pwsh7)は enforce 下で挙動差が無い（両 CLM）。

### 残課題（正直に）

- 今回の WDAC は `AllowMicrosoft` ベース。企業の実ポリシーは署名必須・ISG・Managed Installer 等で更に厳しいことがあるが、
  「.ps1 は CLM 化で実行される」というスクリプト施行の核は同じなので §18 は通る公算が高い（追試は任意）。
- WDAC でも **AppLocker 風の明示 .ps1 deny を併用**された場合はブロックされうる（その時は §10 の「明示エラーで落ちる」契約に従う）。
