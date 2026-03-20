---
title: "grep(1) 文字列検索コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-20
---

# grep(1) — 行検索（実用）

`grep` は「テキストからパターンに一致する行を抜く」ための標準ツールです。
Linux運用ではログ調査・設定検索・簡易集計で毎日使います。

このノートは `man grep` を土台に、**運用で迷いやすい点**（正規表現モード、再帰、バイナリ扱い、終了コード、クォート）を実用寄りに整理します。

## まず結論：よく使うコマンド

```bash
# ログ調査の基本（ファイル名+行番号、色付き、大小無視）
grep -nH --color=auto -i 'error' /var/log/messages

# ディレクトリを再帰検索（.git 等は後述の --exclude-dir が便利）
grep -nH -r --color=auto 'Listen' /etc/httpd

# “単語”として一致（前後が英数字や _ に繋がるのは除外）
grep -nH -w 'root' /etc/passwd

# 一致行の前後も出す（コンテキスト）
grep -nH -C 2 'panic' /var/log/messages

# 一致したファイル名だけ（存在確認に便利）
grep -rl 'PermitRootLogin' /etc/ssh

# 一致しない行を抜く（ノイズ除去）
grep -v 'healthcheck' access.log

# 固定文字列として検索（正規表現のメタ文字を気にしない）
grep -nH -F 'a.b[c]' file.txt
```

## SYNOPSIS（書式）

```text
grep [OPTION...] PATTERNS [FILE...]
grep [OPTION...] -e PATTERNS ... [FILE...]
grep [OPTION...] -f PATTERN_FILE ... [FILE...]
```

- `FILE` に `-` を指定すると標準入力
- `FILE` を省略した場合
  - 再帰検索（`-r` 等）のとき：カレントディレクトリを探索
  - 非再帰のとき：標準入力を読む

## パターン指定（最重要）

### 1) PATTERNS は「複数指定」できる

- `-e PATTERN` を複数回：OR 的に全部探す
- `-f FILE`：ファイルから 1行1パターンで読む（複数回指定や `-e` と併用も可）

```bash
# error または warn を探す
grep -nH -e 'error' -e 'warn' app.log

# パターンリスト（1行1パターン）で探す
grep -nH -f patterns.txt app.log
```

### 2) 先頭が `-` のパターンは `-e` か `--` で守る

```bash
# これは危険（オプション扱いされる可能性）
# grep -nH '-foo' file

# 安全
grep -nH -e '-foo' file
# or
grep -nH -- '-foo' file
```

### 3) シェルのクォート

- 正規表現の `*` `?` `[]` `()` `|` `\` などは、シェル展開と衝突しがちです
- 基本は **シングルクォート**で囲う：`'pattern'`

## 正規表現モード（ここで事故る）

`grep` には複数の正規表現方言があります。
「どの記号がメタ文字か」が変わるので、運用ではモードを明示するのが安全です。

- `-G`, `--basic-regexp`（BRE）
  - デフォルト
  - `+` `?` `|` `()` がメタ文字ではない（メタにしたいなら `\+` `\?` `\|` `\( \)`）
- `-E`, `--extended-regexp`（ERE）
  - だいたい直感どおり
  - `+` `?` `|` `()` がメタ文字
  - いわゆる `egrep` 相当（`egrep` 自体は互換のため残っているが非推奨扱い）
- `-F`, `--fixed-strings`
  - 正規表現ではなく「固定文字列」検索
  - メタ文字を気にせず速く・安全（運用ではかなり推奨）
  - 複数パターンは `-e` を複数回、または `-f` で
- `-P`, `--perl-regexp`（PCRE）
  - Perl 互換正規表現（環境依存/実験的な扱いもある）
  - `-z`（NUL区切り）と併用すると未実装警告が出ることがある

### BRE と ERE の違い（最小例）

「数字が1回以上」のつもりで `+` を使うとき：

```bash
# ERE: + がメタ文字なのでそのまま
grep -E '[0-9]+' file

# BRE: + は普通の文字なので、メタにするには \+（正規表現上は \+）
grep -G '[0-9]\+' file
```

## 一致の制御（matching）

- `-i`, `--ignore-case`
  - 大文字小文字を無視
- `--no-ignore-case`
  - `-i` を打ち消す（シェルスクリプト等で便利）
- `-v`, `--invert-match`
  - 一致しない行を選ぶ（ノイズ除去）
- `-w`, `--word-regexp`
  - “単語”として一致
  - 単語構成文字は「英数字と `_`」
  - `-x` があると無効
- `-x`, `--line-regexp`
  - 行全体が一致（正規表現なら `^(pattern)$` 相当）

## 出力の制御（運用でよく使う）

- `-n`, `--line-number`
  - 行番号を付ける（調査の基本）
- `-H`, `--with-filename`
  - ファイル名を必ず付ける
- `-h`, `--no-filename`
  - ファイル名を付けない（1ファイルだけ見たいとき）
- `-o`, `--only-matching`
  - 行全体ではなく「一致した部分だけ」出す（1行に複数回一致すると複数行出る）
- `-c`, `--count`
  - 行内容は出さず、件数だけ
  - `-v` と併用すると「不一致行数」を数える
- `-l`, `--files-with-matches`
  - 一致したファイル名だけ
- `-L`, `--files-without-match`
  - 一致しなかったファイル名だけ
- `-m NUM`, `--max-count=NUM`
  - NUM 行一致したらそのファイルの読み取りを打ち切り
- `-q`, `--quiet`, `--silent`
  - 何も表示せず、終了ステータスだけで判定（スクリプト向け）
- `-s`, `--no-messages`
  - 読めないファイル等のエラー表示を抑止
- `--color[=WHEN]`
  - `auto/always/never`（運用では `--color=auto` が便利）

### 前後行（コンテキスト）

- `-A NUM`, `--after-context=NUM`
  - 一致した後ろ NUM 行
- `-B NUM`, `--before-context=NUM`
  - 一致した前 NUM 行
- `-C NUM`, `--context=NUM`
  - 前後 NUM 行（`-NUM` も同義）

注意：`-o` とコンテキストは併用できず、警告が出ます。

## 再帰検索（ディレクトリ運用）

- `-r`, `--recursive`
  - 配下を再帰的に探索
  - シンボリックリンクは「コマンドライン引数として与えたもの」だけ追う
- `-R`, `--dereference-recursive`
  - 配下を再帰的に探索し、**すべての symlink を追う**

### include / exclude（運用のコツ）

- `--include=GLOB`
  - 指定 glob に合うファイルだけ検索
- `--exclude=GLOB`
  - 指定 glob に合うファイルをスキップ
- `--exclude-from=FILE`
  - 除外 glob をファイルから読む
- `--exclude-dir=GLOB`
  - 指定 glob に合うディレクトリをスキップ

```bash
# .log だけ検索
grep -nH -r --include='*.log' 'timeout' /var/log

# .git と node_modules を避ける
grep -nH -r --exclude-dir='.git' --exclude-dir='node_modules' 'TODO' .
```

## バイナリ/文字コードまわり

### grep がバイナリだと判断した場合

`grep` は「NUL文字を含む」などでバイナリ扱いになると、デフォルトでは出力を抑制したり挙動が変わります。

- `--binary-files=TYPE`
  - `binary`（デフォルト）：バイナリっぽいと判断したら以降の出力を抑制することがある
  - `without-match`：バイナリは一致なしとして扱う（`-I` と同じ）
  - `text`：バイナリでもテキストとして処理（`-a` と同じ）
- `-a`, `--text`
  - バイナリでもテキストとして処理（ただし端末にゴミが出る危険あり）
- `-I`
  - バイナリは無視（`--binary-files=without-match`）

文字コードが怪しいファイル群を雑に探すときは、`-I` や `-a` の使い分けが現実的です。

### NUL区切り（-z）と NUL出力（-Z）

- `-z`, `--null-data`
  - 入力の「行区切り」を改行ではなく NUL（`\0`）として扱う
- `-Z`, `--null`
  - `-l` 等で出すファイル名の末尾を改行ではなく NUL にする

`find -print0` / `xargs -0` と組み合わせると、「変なファイル名」でも安全に扱えます。

## デバイス/ディレクトリの扱い

- `-d ACTION`, `--directories=ACTION`
  - `read`（デフォルト）/ `recurse` / `skip`
- `-D ACTION`, `--devices=ACTION`
  - `read`（デフォルト）/ `skip`

通常は `-r`（再帰）と `--exclude-dir` が分かりやすいです。

## 終了ステータス（スクリプトで重要）

- `0`：一致する行があった
- `1`：一致する行がなかった
- `2`：エラーが起きた（読めない等）

`-q` を使うと「何も出さずに終了コードだけ」で判定できます。

```bash
# 設定に PermitRootLogin があるかだけ知りたい
if grep -q '^PermitRootLogin' /etc/ssh/sshd_config; then
  echo "found"
else
  echo "not found"
fi
```

## 運用レシピ（コピペで使う）

### 1) 設定ディレクトリを調べる（/etc 配下）

```bash
# 例：sshd 設定全体から探す
grep -nH -r --color=auto 'PermitRootLogin' /etc/ssh

# コメント行を除外しつつ探す（単純な例）
grep -nH -r --color=auto 'PermitRootLogin' /etc/ssh | grep -v '^#'
```

### 2) ログからエラー周辺を抜く

```bash
# error の前後 3 行を出す
grep -nH --color=auto -C 3 -i 'error' /var/log/messages
```

### 3) IPアドレスっぽいものを拾う（ERE 推奨）

```bash
# 雑に IPv4 を拾う（厳密ではないが運用の足掛かりとして）
grep -nH -E '([0-9]{1,3}\.){3}[0-9]{1,3}' access.log
```

### 4) 末尾一致 / 先頭一致

```bash
# 末尾が .service の行
grep -nH -E '\.service$' file

# 先頭が Listen の行
grep -nH -E '^Listen\b' httpd.conf
```

### 5) ファイル名だけ欲しい（存在確認）

```bash
grep -rl --color=auto 'server_name' /etc/nginx
```

## 事故りやすい点（短く）

- 期待した `+` や `|` が効かない
  - `-E` を付けているか確認（BRE/ERE の違い）
- `*` や `[]` が勝手に展開される
  - シェルの glob です。パターンを `'...'` で囲う
- 1件でも見つかれば良いのに遅い
  - `-m 1` や `-q`、または `-l`（ファイル名だけ）を使う
- バイナリ扱いで期待通りに出ない
  - `-I`（無視）か `-a`（テキスト扱い）を検討

## 参考（man の例）

```bash
# 複数ファイルで、正規表現（パターンが - で始まる可能性があるので --）
grep -n -- 'f.*\.c$' *g*.h /dev/null
```
