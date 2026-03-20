---
title: "head(1) 先頭抽出コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# head(1) — 先頭を抜く

`head` は、ファイル（または標準入力）の **先頭部分**を取り出すコマンドです。
ログや設定を「まず少しだけ見たい」「パイプで流れてくる内容の冒頭だけ確認したい」時に使います。

## まず結論：よく使うコマンド

```bash
# 先頭10行（デフォルト）
head /var/log/messages

# 先頭50行
head -n 50 /var/log/messages

# 先頭100バイト（バイナリやJSONの先頭確認）
head -c 100 some.bin

# 複数ファイル（ファイル名ヘッダ付き）
head -n 5 file1 file2

# パイプで流れてきた先頭だけ（重い出力のプレビュー）
command | head -n 30

# 「最後のN行以外」を出す（先頭から、末尾N行を除外）
head -n -20 access.log
```

## SYNOPSIS（書式）

```text
head [OPTION]... [FILE]...
```

- `FILE` 省略/`-`：標準入力から読む
- 複数ファイル：各ファイルの前に見出し（ファイル名）が付く（抑制/強制は `-q`/`-v`）

## よく使うオプション

- `-n, --lines=[-]NUM`
  - 行数で指定（デフォルト10行）
  - `-n -NUM`：末尾 `NUM` 行を除いた先頭を出す（ログの「末尾を落として全体を見る」など）
- `-c, --bytes=[-]NUM`
  - バイト数で指定（テキスト以外でも使える）
  - `-c -NUM`：末尾 `NUM` バイトを除いた先頭を出す
- `-q, --quiet, --silent`
  - 複数ファイルでもファイル名ヘッダを出さない
- `-v, --verbose`
  - 常にファイル名ヘッダを出す（1ファイルでも）
- `-z, --zero-terminated`
  - 行区切りを改行ではなく NUL にする（NUL区切りのストリーム用）

## 事故りやすい点（短く）

- `head` は「行」か「バイト」かを明示すると事故が減る
  - テキストなら `-n`、バイナリや固定長なら `-c`
- `head -n -NUM` は「先頭から、末尾NUM行を除外」
  - `tail -n NUM`（末尾NUM行）と逆の発想なので、意図を確認してから使う

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
rpm -q coreutils

head --version
head --help
man head
```
