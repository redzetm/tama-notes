---
title: "uniq(1) 重複行の整理（カウント/抽出/除外）実用ノート（Rocky Linux 9.7）"
date: 2026-03-23
---

# uniq(1) — 隣接する重複行をまとめる/数える

`uniq` は「**隣接（adjacent）** している同一行」をまとめたり、重複だけ/ユニークだけを取り出したりするコマンドです。

最重要ポイント：

- `uniq` は **隣接していない重複は検出しません**
  - つまり「全体の重複排除」をしたいなら、通常は `sort` とセットです

## まず結論：よく使うコマンド

```bash
# 1) まずは基本（隣接重複を1行にまとめる）
uniq file

# 2) “全体で”重複を潰したい（定番）
sort file | uniq
# 同等（sort側でユニーク化）
sort -u file

# 3) 出現回数を数える（頻度集計の基本）
sort file | uniq -c

# 4) 多い順に上位を表示
sort file | uniq -c | sort -nr | head

# 5) 重複している行だけ欲しい（グループごとに1行）
sort file | uniq -d

# 6) 重複している行を“全部”出したい（全重複行を列挙）
sort file | uniq -D

# 7) ユニークな行だけ欲しい（重複グループは捨てる）
sort file | uniq -u

# 8) 大文字小文字を無視
sort file | uniq -i
```

## SYNOPSIS（書式）

```text
uniq [OPTION]... [INPUT [OUTPUT]]
```

- `INPUT` を省略、または `-` を指定すると標準入力
- `OUTPUT` を指定するとファイルへ出力（指定しなければ標準出力）

## “隣接だけ”の意味（uniq の本質）

`uniq` は「同じ行が続いているところ」を 1 グループと見なします。

例：

```text
A
B
A
```

この入力を `uniq` しても、`A` は隣接していないので重複扱いされず、結果は同じになります。

だから運用では、だいたいこう使います：

- `sort ... | uniq ...`

（`info '(coreutils) uniq invocation'` でも、この注意点が強調されています）

## 主なオプション

### 表示/抽出の切り替え

- 何も付けない：各重複グループの **先頭だけ** 出す
- `-c`, `--count`：回数を付ける
- `-d`, `--repeated`：重複している行だけ（各グループ1行）
- `-D`, `--all-repeated[=METHOD]`：重複行を**全部**出す（GNU拡張）
- `-u`, `--unique`：ユニークな行だけ（重複グループは出さない）

```bash
# 重複している行だけ、回数付きで見たい
sort file | uniq -c | awk '$1 >= 2'
```

### 比較範囲の調整（ログ/CSV で便利）

- `-f N`, `--skip-fields=N`：先頭Nフィールドを無視して比較
- `-s N`, `--skip-chars=N`：先頭N文字を無視して比較
- `-w N`, `--check-chars=N`：比較する最大文字数

例：

```bash
# 「先頭の日時」を無視して、メッセージだけで重複を見る（例）
# (YYYY-MM-DD hh:mm:ss ...) みたいなログで、先頭19文字をスキップ
sort app.log | uniq -s 19
```

### 大文字小文字無視

- `-i`, `--ignore-case`

## locale の影響

`uniq` の比較も `LC_COLLATE` の規則に従います。
ログの定型処理や再現性を重視するスクリプトでは、必要に応じて `LC_ALL=C` を検討します。

```bash
LC_ALL=C sort file | LC_ALL=C uniq
```

## NUL 区切り（`-z`）

`-z`, `--zero-terminated` を付けると、改行ではなく NUL（`\0`）区切りの“項目”として扱います。

- `find -print0` / `xargs -0` の流儀と合わせたいときに便利
- `-z` のとき、改行はフィールド区切り扱いになる点に注意

## 終了コード（coreutils uniq）

coreutils の多くのコマンドと同様、

- `0`：成功
- `0` 以外：失敗（何らかのエラー）

が基本です（詳細は coreutils の共通ドキュメント *Exit status* 参照）。

## 事故りポイント（uniq あるある）

- `uniq` だけで重複が消えない → 隣接していない。だいたい `sort | uniq` が正解
- `sort -u` と `sort | uniq` の違いで混乱 → どちらも“全体の重複排除”には使える。キー指定が絡むときは `sort` 側の `-k` を意識
- 頻度集計の最後を `sort -nr` にし忘れて回数順にならない
