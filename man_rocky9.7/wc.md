---
title: "wc(1) 行数/単語数/バイト数カウント実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# wc(1) — 行数/単語数/バイト数を数える（実用）

`wc` は、ファイル（または標準入力）について「改行数（行数）/単語数/文字数/バイト数」などを数えるコマンドです。
ログ件数の概算や、出力の規模感を掴む用途でよく使います。

## まず結論：よく使うコマンド

```bash
# 行数（ログの件数をざっくり）
wc -l /var/log/messages

# 単語数（文章やデータのざっくり尺度）
wc -w README.md

# バイト数（ファイルサイズに近い尺度）
wc -c some.bin

# 文字数（マルチバイトが絡む時。バイト数とは別）
wc -m some.txt

# 最大行長（1行が長すぎるログ/設定の検知）
wc -L nginx.conf

# パイプ（条件に合う行数＝ヒット件数）
grep -i 'error' /var/log/messages | wc -l
```

## SYNOPSIS（書式）

```text
wc [OPTION]... [FILE]...
wc [OPTION]... --files0-from=F
```

- `FILE` 省略/`-`：標準入力から読む
- 複数ファイル：各ファイルの結果と合計（total）を出します

## 出力の順序（混乱しやすいので先に）

`wc` は「何を数えるか」を複数指定した場合、出力順が固定です。

- newline（行数）
- word（単語数）
- character（文字数）
- byte（バイト数）
- maximum line length（最大行長）

例えば `wc -l -w file` は「行数→単語数」の順で出ます。

## よく使うオプション

- `-l, --lines`
  - 行数（改行数）
- `-w, --words`
  - 単語数（空白区切りのトークン数）
- `-c, --bytes`
  - バイト数
- `-m, --chars`
  - 文字数
- `-L, --max-line-length`
  - 最大行長

## 事故りやすい点（短く）

- `-c`（バイト）と `-m`（文字）は別
  - 日本語などマルチバイト文字があると一致しません
- `wc -l` は「改行数」
  - 最終行に改行が無いファイルだと、見た目の行数とズレることがあります
- `grep ... | wc -l` は「ざっくり件数」
  - 正確に数えたい時は `grep` のパターン（`-i`や正規表現）を見直します

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
rpm -q coreutils

wc --version
wc --help
man wc
```
