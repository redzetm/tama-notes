---
title: "徹底解説！自作シェル ush（うーしゅ）0.0.6解説書"
date: 2026-03-09
---

# ush 0.0.6 徹底解説（UmuOS User Shell）

対象: UmuOS-0.1.7-base-stable 想定

この文書は、ush 0.0.6 を「対話シェル／小物スクリプト用途」として最大限使うための、仕様と実装に基づく実用解説である。

- 目的: 軽量・観測可能・挙動が固定されたシェルとして、`/bin/sh`（BusyBox ash）に逃げずに書ける範囲を広げる
- 非目的: POSIX / bash 互換、ジョブ制御、多段パイプ、ヒアドキュメント、関数、算術式、配列、`break/continue` など

以降の説明は、0.0.6 の設計（tokenize/expand/script_parse/script_exec/exec）に沿っている。

---

## はじめに（読み方・マスター用）

この文書は「仕様を読む」だけで終わらず、実際に ush を“使い回せる”状態にするための構成にしている。おすすめの進め方は次の通り。

### A. まず制約を固定する（最短で事故を減らす）

- まずは「0. 最短チートシート」と「13. 落とし穴」を読む
- 次に「5. simple 構文」「9. 展開」「10. glob」を読む

ここで ush の地雷（多段パイプ不可、`$(...)` 入れ子不可、`NAME=VALUE` 不可、builtins のパイプ/リダイレクト不可、など）が頭に入る。

### B. 次に“書ける最小スクリプト”を作る

- 「12. 例集」から 2〜3個をそのまま手元で動かす
- そのあと、引数分岐（`case`）＋存在チェック（`test`）＋ログ出力（`>`/`>>`）の3点セットを、自分の用途に合わせて書き換える

### C. 仕上げに、失敗の切り分けを身につける

- `unsupported syntax` と `syntax error` の違いを「14. よくあるエラーと切り分け」で確認する
- 迷ったら「何が 1トークンか（3章）」と「どこまでが 1文か（2章/5章/7章）」へ戻る

### D. 環境差を疑う（UmuOS実機で大事）

ush 自体の仕様とは別に、

- そのコマンドが存在するか（例: `/sbin/ip`、`/sbin/udhcpc`、`tar`、`dmesg`）
- PATH に入っているか

で動きが変わる。ush スクリプト中では `test -e /path/to/cmd` で存在チェックして分岐するのが安全。

---

## 優先順位（ざっくり）: どこまでが「1文」か

ush 0.0.6 は「script 構文」と「simple 構文」を分けて処理する。実用上の最重要ポイントは次の通り。

- `;` は script レイヤの文区切り（複数行入力でも内部的に `;` 連結される）
- `;` で区切られた 1区間が「単純文（ST_SIMPLE）」として simple パーサへ渡る
- simple パーサ内では、おおむね次の優先順位で解釈される
  - `|`（パイプ、ただし 1段のみ）
  - `&&` / `||`（左から評価）

つまり「`a | b && c` のような混在」は、“1文”の中で解釈されるが、複雑にしすぎるより `;` と一時ファイルで段階化した方が事故が少ない。

---

## 0. 最短チートシート

### 0.1 使える演算子（重要）

- 直列: `;`
- 条件連結: `&&` / `||`
- パイプ: `|`（**1段のみ**。`a|b|c` は不可）
- リダイレクト: `<` / `>` / `>>`

### 0.2 使える制御構文（0.0.6 で追加）

- `if ... then ... elif ... then ... else ... fi`
- `while ... do ... done`
- `for NAME in WORDS... ; do ... done`（`do` の直前に `;`（または改行）が必須）
- `case WORD in PAT) ... ;; ... esac`（各項目は `;;` で終端）

### 0.3 使える展開

- 変数: `$NAME` / `${NAME}`
- 特殊: `$?`（直前の終了ステータス）, `$#`（位置引数の個数）, `$0..$9`
- チルダ: `~` / `~/path`（**`~user` は不可**）
- コマンド置換: `$(...)`（**入れ子不可**）

### 0.4 builtins

- `cd [DIR]`
- `pwd`
- `export NAME=VALUE` / `export NAME`
- `test EXPR` / `[ EXPR ]`
- `exit [N]`
- `help`

注意:
- builtin は **リダイレクト不可**（`pwd > f` などは `unsupported syntax`）
- builtin は **パイプ不可**（`pwd | cat` などは `unsupported syntax`）

### 0.5 重要な終了コード（目安）

- `0`: 成功
- `1`: 実行時エラー（syscall 失敗など）
- `2`: 構文エラー／未対応構文／引数不正
- `127`: コマンド未発見
- `126`: 実行不可

---

## 1. ush の位置づけ（ash との使い分け）

ush は「軽量」「観測しやすい」「未対応は検出してエラー」を優先する。

- ush が得意: 対話操作、起動後の簡単な作業、短い自動化（条件分岐・繰り返し・分岐表程度）
- ush が不得意: 本格スクリプト（互換性要求、複雑な I/O、関数・算術、ジョブ制御）

本格スクリプトが必要なら `/bin/sh`（BusyBox ash）で実行する前提。

---

## 2. 入力単位: 「行」ではなく「完結したブロック」

ush 0.0.6 は入力を 1 行ごとに評価しない。内部的には「行末に `;` を付けて連結」し、ブロックが完結したところで評価する。

### 2.1 対話モード

- 1行入力を読み、内部バッファへ追加する（追加時に行末へ `;` を補う）
- tokenize → script_parse
  - `PARSE_INCOMPLETE`: 追加行を読み続ける
  - `PARSE_OK`: 実行してバッファをクリア
  - その他: エラー表示してバッファをクリア

補足（実用上重要）:
- 複合構文の途中で空行を入れた場合、空行は「区切り（`;`）」として扱われる（ブロック終了ではない）

### 2.2 スクリプトモード

- ファイルを上から行単位で読み、同様に行末 `;` を付けて連結
- ブロックが完結したタイミングで評価
- EOF でブロックが閉じていなければ `syntax error`（`last_status=2`）

### 2.3 実用上の結論（書き方のコツ）

- `if` / `while` の条件部と `then` / `do` は **同一トークン列として続けて書けない**
  - `then` / `do` は **直前に `;`（または改行）** が必要
- `for` は **words の直後に `;`（または改行）** が必要

つまり、次のように「キーワードの前後を改行（= `;`）で区切る」書き方が最も安全。

```sh
if test -e /etc/passwd
then
  echo ok
fi

while test -e /tmp/flag
do
  echo looping
done

for x in a b c
do
  echo "$x"
done
```

---

## 3. 字句（tokenize）: 何が 1トークンになるか

ush は入力をトークン列に分解する。重要なルールだけ抜粋する。

### 3.1 区切り

- 空白（スペース／タブ）で区切る
- 演算子は空白の有無に関わらず独立トークンになる

### 3.2 演算子トークン

- `|`, `&&`, `||`, `;`, `<`, `>`, `>>`, `;;`, `)`

備考:
- `)` と `;;` は `case` 用
- `;` は script の文区切り、および「ブロック連結」の内部表現

### 3.3 コメント

- 未クォートで、かつ「トークン先頭の `#`」から行末までをコメントとして無視する

例:

```sh
echo a # ここからコメント
echo a#b # a#b は WORD の一部
```

### 3.4 クォートは「トークンを丸ごと囲う」方式

- シングルクォート `'...'` とダブルクォート `"..."` がある
- どちらも「そのトークンはそこで終わる」

つまり、次は **構文エラー**。

```sh
'foo'bar
"foo"bar
```

### 3.5 未クォート WORD の中に `'` / `"` は置けない

未クォート WORD の途中にクォート文字が出現すると `syntax error` になる。

### 3.6 エスケープ（バックスラッシュ）

未クォートでは、次の文字の前の `\` を「その 1 文字をリテラル扱い」にできる（内部的にはマーカーで保持され、後段の展開／globで意味を持つ）。

- 空白, `\\`, `'`, `"`, `$`, `;`, `|`, `&`, `<`, `>`, `*`, `?`, `[`, `]`

ダブルクォート内では、次の 3 種だけが「エスケープ対象」。

- `"`, `\\`, `\$`

注意:
- 行継続（`\` + 改行）は未対応
- 行末の `\` は「通常文字」として取り込まれる

### 3.7 未対応トークンの検出

- `&` 単体（`&&` 以外）は未対応
- `<<` / `<<<`（ヒアドキュメント）は未対応
- `(` は `$(...)` 以外未対応
- `{` `}` は「単体トークンとして現れると」未対応（ただし `${NAME}` のために WORD 内の `{` `}` は許容される）

---

## 4. 構文レイヤ: script 構文（0.0.6）と simple 構文

ush 0.0.6 は 2段階で構文を扱う:

1. script_parse: `if/while/for/case` と `;` による文の並び
2. parse_line: 1つの「単純文（simple）」をさらに `|` / `&&` / `||` / リダイレクトで解析

イメージ:

- script レイヤ: `ST_IF` / `ST_WHILE` / `ST_FOR` / `ST_CASE` / `ST_SEQ` / `ST_SIMPLE`
- simple レイヤ: `pipeline` と `and-or`（`&&` / `||`）とリダイレクト

---

## 5. simple 構文（単純文）: `|` / `&&` / `||` / リダイレクト

### 5.1 パイプは 1段のみ

- `A | B` はOK
- `A | B | C` は **構文エラー**

実用例:

```sh
# OK
cat /etc/passwd | grep root

# NG
cat /etc/passwd | grep root | wc -l
```

回避策（例）:

```sh
# 2段以上が必要なら、一時ファイルで分解
cat /etc/passwd | grep root > /tmp/x
wc -l < /tmp/x
```

### 5.2 `&&` / `||`

- `A && B`: A が 0 のときだけ B を実行
- `A || B`: A が 0 以外のときだけ B を実行

例:

```sh
mkdir /tmp/dir && echo created
cat /no/such/file || echo failed
```

### 5.3 リダイレクトの制約

- `< file`, `> file`, `>> file`
- 1コマンドにつき入力 `<` は最大 1回、出力 `>`/`>>` は最大 1回
- `<` は `>` より前にのみ書ける（簡易文法）
- リダイレクトは「コマンド末尾」にしか置けない（後ろに引数を置けない）

例:

```sh
# OK
cat < in > out
cat < in >> out

# NG（リダイレクトの後ろに単語）
cat < in foo

# NG（> のあとに <）
cat > out < in
```

### 5.4 パイプとリダイレクトの相互制約

`A | B` のとき:

- `<` は A（左）にだけ許可
- `>`/`>>` は B（右）にだけ許可

例:

```sh
# OK
cat < in | grep x > out

# NG（右に <）
cat | grep x < in

# NG（左に >）
cat > out | grep x
```

### 5.5 builtins と I/O

- builtins はリダイレクト不可
- builtins はパイプ不可

例:

```sh
# NG
pwd > out
pwd | cat
```

### 5.6 `NAME=VALUE cmd` 形式は未対応

先頭単語が `NAME=VALUE` に見える場合、`unsupported syntax` になる。

回避策:

```sh
export NAME=VALUE
cmd
```

---

## 6. 予約語（if/then/fi など）の扱い

script_parse は「未クォート WORD」だけを予約語として認識する。

- `if` / `then` / `elif` / `else` / `fi`
- `while` / `do` / `done`
- `for` / `in`
- `case` / `esac`

ポイント:
- 予約語は **未クォートのときだけ** 予約語扱い
- 予約語が「予約語として出てはいけない場所」に出現すると `syntax error`

例:

```sh
# 予約語として扱われない（単なる単語）
echo "if"
echo 'then'
```

---

## 7. 制御構文: if / while / for / case

### 7.1 if

構文（概略）:

```sh
if <cond-stmts>
then
  <then-stmts>
elif <cond-stmts>
then
  <elif-stmts>
else
  <else-stmts>
fi
```

重要:
- `then` は **条件部の後に `;`（または改行）** が必要
- 条件部・then部・elif部・else部は「文の並び（`;` 区切り）」
- 条件判定は「条件部の実行結果（終了コード）」で行う
  - `0` が真、`0以外`が偽

例（典型）:

```sh
if test -e /etc/passwd
then
  echo "exists"
else
  echo "missing"
fi
```

例（複数文の条件部）:

```sh
# cond-stmts は ';' で複数文にできる
if echo "probe"; test -e /etc/passwd
then
  echo ok
fi
```

注意:
- `if test -e /etc/passwd then ...` のように `then` を同じ行で続けると、tokenize/parse の都合で意図通りにならない（`then` が条件部の WORD として扱われる）。
  - 安全策: 改行するか、明示 `; then` にする

### 7.2 while

構文:

```sh
while <cond-stmts>
do
  <body-stmts>
done
```

重要:
- `do` は **条件部の後に `;`（または改行）** が必要

例:

```sh
i=0 # ※これは未対応（NAME=VALUE）なので使えない
```

ush では `NAME=VALUE` が未対応なので、カウンタは外部コマンドや環境変数で代替する必要がある。
（例: `export i=0` → `export i=$(expr ...)` のような形式は、`expr` がある前提になりがちで、ush 単体で完結させにくい）

現実的な while の使い方は、ファイルや存在判定をループ条件にすることが多い。

```sh
# /tmp/flag がある間ループ
while test -e /tmp/flag
do
  echo "running"
  sleep 1
  # 別端末から rm /tmp/flag すると終了

done
```

while の終了ステータス:
- ループ本体が 1回も実行されなかった場合は 0
- 1回以上実行された場合は「最後の body のステータス」

### 7.3 for

構文:

```sh
for NAME in WORDS...
; do
  <body-stmts>
done
```

実装上の要点:
- NAME は「有効な名前」で、未クォート WORD である必要がある
- `do` の直前に **区切り（`;`）が必須**（改行でも良い）
- WORDS は実行時に 1語ずつ expand され、結果は「語リスト」に展開される
  - 未クォートの語は glob により複数語へ増える可能性がある
- ループ変数は `setenv()` で環境変数として設定される

例:

```sh
for x in a b c
do
  echo "$x"
done
```

例（glob を使う）:

```sh
# *.md が展開され、各ファイル名が x に入る
for x in *.md
do
  echo "$x"
done
```

空リストの場合:
- 1回も実行されず、終了ステータスは 0

### 7.4 case

構文:

```sh
case WORD in
  PAT) <body-stmts> ;;
  PAT2|PAT3) <body-stmts> ;;
  *) <body-stmts> ;;
esac
```

実装上の要点:
- `case` の対象 WORD は最初に expand される
- PAT は 1つ以上。`|` で複数パターンを並べられる
- PAT の終端は `)`（`PAT)` のように空白なしでもOK。tokenize が `WORD + )` に分割する）
- 各項目は `;;` が必須

マッチング:
- PAT が未クォートの場合: glob パターン（`fnmatch`）で比較
- PAT がクォートされている場合: 文字列完全一致
- `[a-z]` 形式の範囲指定は未対応（`unsupported syntax`）

例（典型）:

```sh
case "$1" in
  start)
    echo start
    ;;
  stop)
    echo stop
    ;;
  *)
    echo "usage: $0 start|stop"
    ;;
esac
```

`case` の注意:
- 末尾の `;;` を忘れると `syntax error`

---

## 8. 条件評価: `test` / `[ ... ]`

`test` と `[` は builtin として提供される。返り値は次の通り。

- 真: 0
- 偽: 1
- 構文/引数不正: 2

### 8.1 単項

- `-n STR`（空でない）
- `-z STR`（空）
- `-e PATH`（存在）
- `-f PATH`（通常ファイル）
- `-d PATH`（ディレクトリ）

例:

```sh
test -e /etc/passwd && echo ok
[ -d /tmp ] || echo missing
```

### 8.2 二項（文字列）

- `A = B`
- `A != B`

例:

```sh
[ "$1" = "start" ] && echo start
[ "$1" != "" ] && echo has-arg
```

### 8.3 否定 `!`

- `! EXPR`

例:

```sh
[ ! -e /tmp/flag ] && echo "no flag"
```

### 8.4 単一引数

- `test STR` は STR が空でなければ真

例:

```sh
[ "$1" ] && echo "arg is non-empty"
```

### 8.5 `[ ... ]` の注意

`[` は末尾の `]` が必須。

```sh
# OK
[ -e /etc/passwd ]

# NG
[ -e /etc/passwd
```

---

## 9. 展開（expand）

### 9.1 展開されるタイミング

- 各コマンドの argv を「実行直前」に 1語ずつ expand する
- リダイレクトのパスも expand する
- ただし builtins ではリダイレクト自体が禁止

### 9.2 チルダ（`~`）

未クォートで先頭が `~` のときだけ展開される。

- `~` → `$HOME`（未設定なら `/`）
- `~/path` → `$HOME/path`
- `~user` は未対応（`unsupported syntax`）

例:

```sh
cd ~
cd ~/work
```

### 9.3 変数 `$NAME` と `${NAME}`

- `$NAME`: NAME は `[A-Za-z_][A-Za-z0-9_]*`
- `${NAME}`: NAME が「有効な名前」である必要がある（それ以外は未対応）

未定義は空文字。

例:

```sh
export FOO=bar
echo $FOO
echo ${FOO}
```

注意:
- `${}` のように空は未対応

### 9.4 特殊変数

- `$?`: 直前の終了ステータス
- `$#`: 位置引数の個数
- `$0`: スクリプトパス（対話では "ush" 扱い）
- `$1..$9`: 位置引数

未対応:
- `$10` 以降
- `$@`, `$*`

例:

```sh
echo "argc=$#"
echo "arg1=$1"
```

### 9.5 コマンド置換 `$(...)`

#### 仕様（外側のクォートで挙動が変わる）

- `$(...)` は子プロセスで ush の 1ブロックとして評価され、stdout を文字列として取り込む
- stdout の「末尾の改行（\r/\n）」は削除される
- `"$(...)"`（ダブルクォート内）:
  - 内部の改行は保持される（末尾改行だけ削除）
- `$(...)`（未クォート）:
  - 内部の改行はスペースに正規化され、末尾の空白類も削られる

例（改行の扱い）:

```sh
# 例: cmdsub が複数行を出す場合
x=$(printf "a\nb\n")
echo "$x"   # → "a b" 的になる（未クォート規則で改行が空白化）

y="$(printf "a\nb\n")"
echo "$y"   # → "a\nb" 的になる（内部改行保持、末尾改行のみ削除）
```

#### 制約

- `$( $(...) )` のような **入れ子は未対応**（`unsupported syntax`）
- `` `...` ``（バッククォート）も未対応

実用回避策:
- 「入れ子が必要」に見える場合は、段階的に変数へ落とす

```sh
# NG: echo $(echo $(date))

# OK: 2段階に分ける
x=$(date)
y=$(echo "$x")
echo "$y"
```

#### 注意（エラーの見え方）

`$(...)` は stdout のみを取り込むため、内部で起きたエラーが「外側の構文エラー」として直感的に見えない場合がある。
（未対応構文や字句レベルの問題は検出されるが、実行時の失敗は stdout が空になるなどの形になりやすい）

コマンド置換内は、まず短い単純コマンドで動作確認してから組み込むのが安全。

---

## 10. glob（`*` / `?` / `[...]`）

glob は **未クォートの語** に対してだけ適用される。

- `*`, `?`, `[...]` をメタ文字として扱う
- エスケープされたメタ文字（例: `\*`）はリテラル
- `[...]` は `]` が後続して初めてメタ文字扱い
- `[a-z]` のような **範囲指定**は未対応（`unsupported syntax`）

マッチ 0 件の場合:
- その語は「そのまま」（メタ文字を含んだ文字列）として残る

例:

```sh
# もし *.md が無ければ、引数は "*.md" のまま
ls *.md

# リテラルとして扱わせたい
ls \*.md
ls "*.md"
```

---

## 11. 外部コマンド実行と PATH

- 外部コマンドは fork/exec で実行される
- 実行ファイルがスクリプト等で `ENOEXEC` の場合に `/bin/sh`（BusyBox ash）へフォールバックする
- PATH が未設定の場合、既定の探索パス（例: `/umu_bin:/sbin:/bin`）が使われる想定

外部コマンドの終了コード:
- 未発見: 127
- 実行不可: 126
- シグナル終了: `128 + signal`

---

## 12. 例集（制約込みの実用スクリプト）

ここからは「ush だけで書ける範囲」を意識して例を集める。

### 12.1 引数で分岐するミニコマンド

```sh
# usage: demo.sh start|stop
case "$1" in
  start)
    echo start
    ;;
  stop)
    echo stop
    ;;
  *)
    echo "usage: $0 start|stop"
    ;;
esac
```

### 12.2 成否で分岐（`&&` / `||`）

```sh
mkdir /tmp/x && echo ok
mkdir /tmp/x || echo "maybe already exists"
```

### 12.3 ファイルの存在で分岐（test）

```sh
if test -e /etc/resolv.conf
then
  echo has-dns
else
  echo no-dns
fi
```

### 12.4 監視ループ（フラグファイル方式）

```sh
# 別端末で: touch /tmp/run
# 停止: rm /tmp/run

while test -e /tmp/run
do
  date
  sleep 1
done
```

### 12.5 ディレクトリ内の md を列挙（glob + for）

```sh
for f in *.md
do
  echo "$f"
done
```

### 12.6 2段パイプ相当を一時ファイルで置き換える

```sh
cat /etc/passwd | grep root > /tmp/rootlines
wc -l < /tmp/rootlines
```

### 12.7 `case` のパターンで拡張子判定

```sh
for f in *
do
  case "$f" in
    *.md)
      echo "md: $f"
      ;;
    *.conf)
      echo "conf: $f"
      ;;
    *)
      # 何もしない
      echo "other: $f"
      ;;
  esac
done
```

### 12.8 `$(...)` を使う（クォート必須になりやすい例）

```sh
# date が複数語を返す可能性があるため、まずはクォート推奨
now="$(date)"
echo "now=$now"
```

### 12.9 起動後点検（最小チェックをまとめて採取）

UmuOS の「観測・切り分け」用途では、起動直後に以下を一括で採取できると便利。

注意:
- ush の builtin はリダイレクト不可なので、出力採取は外部コマンド（`uname`/`cat`/`dmesg` 等）を使う
- `>>` 追記リダイレクトは「各コマンドの末尾」に置く

```sh
# 保存先（必要なら作る）
mkdir -p /logs/support

# ファイル名に空白を入れたくないので、日付は数字推奨
export ts="$(date +%Y%m%d-%H%M%S)"
export out="/logs/support/postboot-$ts.txt"

uname -a > "$out"
echo "---" >> "$out"

cat /proc/cmdline >> "$out"
echo "---" >> "$out"

# UmuOS 側で永続化しているブートログがある前提の例
if test -e /logs/boot.log
then
  echo "[boot.log]" >> "$out"
  cat /logs/boot.log >> "$out"
  echo "---" >> "$out"
fi

# dmesg がある場合だけ取る
if test -e /bin/dmesg
then
  echo "[dmesg]" >> "$out"
  dmesg >> "$out"
  echo "---" >> "$out"
fi

echo "saved: $out"
```

### 12.10 ネットワーク点検（存在チェック → 表示）

「疎通以前に、リンクが上がっているか／IPが付いているか／デフォルトルートがあるか」を短く見る。

```sh
# iproute2 がある場合
if test -e /sbin/ip
then
  /sbin/ip link
  /sbin/ip addr
  /sbin/ip route
else
  # busybox/ifconfig がある場合
  if test -e /sbin/ifconfig
  then
    /sbin/ifconfig -a
  fi

  if test -e /sbin/route
  then
    /sbin/route -n
  fi
fi

if test -e /etc/resolv.conf
then
  echo "[resolv.conf]"
  cat /etc/resolv.conf
fi
```

### 12.11 ネットワーク設定（固定IPの最小例）

前提:
- iproute2 がある（`/sbin/ip`）
- インターフェース名が `eth0`（UmuOS の固定設定に合わせて読み替え）

```sh
export IF=eth0
export IP=192.168.10.50/24
export GW=192.168.10.1

/sbin/ip link set "$IF" up
/sbin/ip addr add "$IP" dev "$IF"
/sbin/ip route add default via "$GW"

# DNS（ファイル上書き）
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

注意:
- `echo` が外部コマンドとして存在しない環境では、`/etc/resolv.conf` の書き換えは別手段（`sh -c` やエディタ等）を使う

### 12.12 ネットワーク設定（DHCPがある場合の例）

UmuOS 側で DHCP クライアント（例: `udhcpc`）がある場合の最小例。

```sh
export IF=eth0

if test -e /sbin/udhcpc
then
  /sbin/udhcpc -i "$IF"
else
  echo "no udhcpc"
fi
```

### 12.13 ログ採取（必要なものだけ tar で固める）

前提:
- `tar` がある（BusyBox の tar でも可）

```sh
mkdir -p /logs/support
export ts="$(date +%Y%m%d-%H%M%S)"
export pkg="/logs/support/support-$ts.tar"

# あるものだけ追加していく（無いファイルは tar がエラーにする可能性があるので分岐）

# まず最低限で作る
tar -cf "$pkg" /proc/cmdline

if test -e /logs/boot.log
then
  tar -rf "$pkg" /logs/boot.log
fi

if test -e /etc/resolv.conf
then
  tar -rf "$pkg" /etc/resolv.conf
fi

echo "saved: $pkg"
```

---

## 13. 落とし穴（現象→原因→回避策）

`$(...)` / glob / リダイレクトは便利だが、ush 0.0.6 は「POSIX互換ではない」ため、よくある前提が崩れる。ここでは実害が出やすい点だけを、現象→原因→回避策の順でまとめる。

### 13.1 `VAR=...` と書いたら `unsupported syntax` になる

- 現象: `i=0` や `IF=eth0` のような行で `unsupported syntax`
- 原因: ush は `NAME=VALUE` 形式（代入・一時環境変数）を未対応として検出する
- 回避策: `export NAME=VALUE` を使う（環境変数として扱う）

```sh
# NG
i=0

# OK
export i=0
echo "$i"
```

### 13.2 `$(...)` の入れ子で `unsupported syntax`

- 現象: `echo $(echo $(date))` のような形で失敗する
- 原因: `$(...)` の入れ子は未対応
- 回避策: 1段ずつ変数へ落とす

```sh
x="$(date)"
y="$(echo "$x")"
echo "$y"
```

### 13.3 `$(...)` の改行が消える／空白化する

- 現象: `$(...)` の結果に改行が含まれると、想定より 1 行になったり、単語が増えたりする
- 原因:
  - `"$(...)"`（クォートあり）: 内部改行は保持（末尾改行だけ削除）
  - `$(...)`（未クォート）: 改行がスペースに正規化され、末尾の空白類も削除される
- 回避策: 改行を残したい／壊したくない場合は基本 `"$(...)"` を使う

```sh
a=$(printf "x\ny\n")
echo "$a"   # 未クォート規則で "x y" 的になりやすい

b="$(printf "x\ny\n")"
echo "$b"   # 内部改行は保持しやすい
```

### 13.4 glob が 0件でもエラーにならず、リテラルが残る

- 現象: `ls *.md` で `.md` が無いのに `*.md` を引数として受け取り、意図した結果にならない
- 原因: ush の glob は「0件なら語をそのまま残す」
- 回避策: 0件を許容しないなら、0件時に `*.md` がそのまま残る性質を利用して分岐する

```sh
for f in *.md
do
  case "$f" in
    "*.md")
      echo "no md files"
      ;;
    *)
      echo "$f"
      ;;
  esac
done
```

### 13.5 glob の `[a-z]` 範囲指定で `unsupported syntax`

- 現象: `ls [a-z]*` や `case` のパターンで `[a-z]` を使うと `unsupported syntax`
- 原因: bracket の範囲指定（`-` を含む形式）を未対応として検出する
- 回避策: `*` や `?`、範囲を列挙（例: `[abc]`）にする

### 13.6 リダイレクトを末尾以外に置くと `syntax error`

- 現象: `cat < in foo` や `cmd > out arg` が失敗
- 原因: ush の簡易文法では、リダイレクトは「コマンド末尾」にしか置けない
- 回避策: 引数を先に書き、最後にリダイレクトを書く

```sh
# OK
grep root /etc/passwd > /tmp/root
```

### 13.7 パイプとリダイレクトの位置関係で `syntax error`

- 現象: `cat > out | grep x` や `cat | grep x < in` が失敗
- 原因: `A | B` のとき、`<` は左だけ、`>`/`>>` は右だけに制限している
- 回避策: 一時ファイルで段階化する

```sh
cat /etc/passwd > /tmp/passwd
cat /tmp/passwd | grep root
```

### 13.8 リダイレクト先に glob を使うとハマる（複数マッチ）

- 現象: `cmd > *.log` のような書き方で `syntax error` になったり、意図しないファイルに出たりする
- 原因: リダイレクトパスも expand/glob の対象で、glob が複数マッチすると「1つに決まらない」ためエラーになる
- 回避策: リダイレクト先は glob を避け、固定名にする

---

## 14. よくあるエラーと切り分け

### 14.1 `unsupported syntax`

代表例:
- `&` 単体
- `<<` / `<<<`
- 多段パイプ `a|b|c`
- `$(...)` の入れ子
- `~user`
- glob の `[a-z]` 形式
- `NAME=VALUE cmd` 形式
- builtins のリダイレクト／パイプ

### 14.2 `syntax error`

代表例:
- クォート未閉鎖
- `if/while/for/case` のキーワードや終端（`fi`/`done`/`esac`/`;;`）不足
- `for` の `do` の直前に `;`（または改行）が無い
- リダイレクトの書き方違反（末尾以外に置いた、重複した、順序違反など）

### 14.3 `ush --version` が失敗する

古いバイナリを実行して `--version` を「スクリプトファイル名」と解釈している可能性がある。

ゲスト側チェック:

```sh
command -v ush
ls -l /umu_bin/ush
/umu_bin/ush --version; echo $?
```

---

## 15. まとめ

- ush 0.0.6 は「ブロック単位」で評価し、`if/while/for/case` を扱える
- ただし POSIX 互換は狙っておらず、未対応は明確にエラーになる
- 最重要制約は「多段パイプ不可」「`$(...)` 入れ子不可」「builtins のパイプ／リダイレクト不可」「`NAME=VALUE cmd` 不可」

必要なら、この文書に「UmuOS で実際に使う運用スクリプト（ネット設定／ログ採取／起動後点検）」の例を追加していく。
