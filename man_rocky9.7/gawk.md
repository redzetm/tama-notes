---
title: "gawk(1) AWK（パターン走査・テキスト処理）実用ノート（Rocky Linux 9.7）"
date: 2026-03-23
---

# gawk(1) — AWK（パターン走査・テキスト処理言語）

`gawk` は GNU 版の AWK 実装で、**「行（レコード）を読みながら、条件（パターン）に一致した行に処理（アクション）を適用する」**ための言語/コマンドです。

- `grep`：探す
- `sed`：変える/抜く
- `awk/gawk`：**構造化して抜く・集計する**（列・条件・集計が得意）

このノートは Rocky Linux 9.7 での運用を想定し、AWK の“使うところ”に絞って長めにまとめます。
（環境で `gawk` の有無やバージョンが違うことがあるので、必要に応じて末尾の確認コマンドも参照してください）

## まず結論：よく使うワンライナー

```bash
# 0) まず雰囲気（1行目の1列目と3列目）
awk '{print $1, $3}' file

# 1) 区切り指定（: 区切りで /etc/passwd のログイン名だけ）
awk -F: '{print $1}' /etc/passwd

# 2) 条件（2列目が 0 より大きい行だけ）
awk '$2 > 0 {print $0}' file

# 3) 正規表現条件（ERROR を含む行）
awk '/ERROR/ {print}' /var/log/messages

# 4) 行数カウント（wc -l 相当）
awk 'END{print NR}' file

# 5) 合計（例：2列目を合計）
awk '{sum += $2} END{print sum}' file

# 6) 平均（0除算を避ける）
awk '{sum += $2; n++} END{if(n>0) print sum/n; else print 0}' file

# 7) 件数集計（頻度）
# 1列目ごとの件数 → 多い順
awk '{cnt[$1]++} END{for (k in cnt) print cnt[k], k}' file | sort -nr

# 8) CSVの2列目だけ抽出（簡易。厳密CSVは別途注意）
awk -F, '{print $2}' file.csv

# 9) ヘッダ行をスキップ（1行目は除外）
awk 'NR==1{next} {print}' file

# 10) “空行とコメント行”を捨てる
awk 'NF==0 || $1 ~ /^#/ {next} {print}' file
```

## SYNOPSIS（書式）

`gawk` は大きく2通りの呼び方になります。

```text
gawk [POSIX or GNU style options] -f program-file [ -- ] file ...
gawk [POSIX or GNU style options] [ -- ] 'program-text' file ...
```

- `-f program.awk`：AWKプログラムをファイルから読む（長い処理/再利用/デバッグ向き）
- `'program-text'`：1行で書く（ワンライナー向き）
- `--`：オプション終端（ファイル名や変数が `-` で始まるときに安全）

## AWK の基本モデル（最重要）

AWK は基本的にこの形です。

```text
pattern { action }
pattern { action }
...
```

- `pattern`（条件）が真なら `action` が実行される
- `pattern` を省略すると全行に適用
- `action` を省略すると「その行を表示（print）」相当

さらに特別なブロックがあります。

- `BEGIN { ... }`：入力を読む前に1回
- `END { ... }`：入力を読み終わった後に1回

例：

```bash
# : 区切りで /etc/passwd のユーザー名だけ表示
awk -F: '{print $1}' /etc/passwd

# 最初にヘッダを出してから処理
awk 'BEGIN{print "user"} {print $1}' /etc/passwd
```

## すぐ使う「変数」と「フィールド」

### 行（レコード）と列（フィールド）

- `$0`：行全体
- `$1` `$2` ...：1列目、2列目…
- `NF`：その行のフィールド数

```bash
# 列数が3以上ある行だけ、1列目と3列目
awk 'NF>=3 {print $1, $3}' file
```

### 行番号

- `NR`：全体の行番号（複数ファイルを通して連番）
- `FNR`：ファイルごとの行番号（ファイルごとに1から）

```bash
# 先頭10行に行番号を付ける
awk 'NR<=10 {print NR, $0}' file
```

### 区切り文字（FS/OFS）

- `FS`：入力のフィールド区切り（デフォルトは空白の連続）
- `OFS`：出力のフィールド区切り（デフォルトは空白）

指定方法は2つ。

- `-F ':'`：起動オプションで指定（お手軽）
- `BEGIN{FS=":"; OFS="\t"}`：プログラム内で指定

```bash
# タブ区切りで出したい
awk -F: 'BEGIN{OFS="\t"} {print $1,$3,$7}' /etc/passwd
```

### レコード区切り（RS/ORS）

- `RS`：入力のレコード区切り（通常は改行）
- `ORS`：出力のレコード区切り（通常は改行）

ログや特殊フォーマットで“行”以外を単位にしたいときに使いますが、まずは FS/OFS が主戦場です。

## 条件（pattern）の書き方

### 1) 数値条件

```bash
# 2列目が 100 以上
awk '$2 >= 100 {print}' file
```

### 2) 正規表現条件

```bash
# ERROR を含む行
awk '/ERROR/ {print}' file

# 1列目が root
awk '$1 ~ /^root$/ {print}' /etc/passwd

# 逆（マッチしない）
awk '$0 !~ /DEBUG/ {print}' app.log
```

### 3) 複合条件（AND/OR）

```bash
# 1列目が eth0 かつ 5列目が DOWN
awk '$1=="eth0" && $5=="DOWN" {print}' status.txt

# ERROR または WARN
awk '$0 ~ /ERROR/ || $0 ~ /WARN/ {print}' app.log
```

## アクション（action）でよく使うもの

### print

```bash
# 行全体
awk '{print $0}' file

# 特定列
awk '{print $1, $3}' file
```

### printf（整形出力）

`printf` は改行しません（必要なら `\n` を書きます）。

```bash
# 右寄せで桁を揃える
awk '{printf "%8s %s\n", $1, $2}' file
```

### 集計（配列）

AWK の配列は「連想配列（キー→値）」です。

```bash
# 1列目ごとの件数
awk '{cnt[$1]++} END{for (k in cnt) print k, cnt[k]}' file

# 合計サイズ（例：3列目を合計）
awk '{sum += $3} END{print sum}' file
```

## 実務でよくある用途別レシピ

### 1) ログの“頻出”を出す

```bash
# access.log のIP（1列目）上位
awk '{cnt[$1]++} END{for (ip in cnt) print cnt[ip], ip}' access.log | sort -nr | head

# 末尾のURL（例：7列目）上位
awk '{cnt[$7]++} END{for (u in cnt) print cnt[u], u}' access.log | sort -nr | head
```

### 2) しきい値でフィルタ

```bash
# レスポンスタイムが 1.0 秒を超えた行（仮に最後列が秒）
awk '$NF > 1.0 {print}' app.log
```

### 3) “最初の一致だけ”欲しい（早く終わる）

```bash
# 最初に ERROR が出た行を出して終了
awk '/ERROR/ {print; exit 0}' app.log
```

### 4) 簡易的な“列の入れ替え/追加”

```bash
# 1列目と3列目を入れ替える
awk '{print $3, $2, $1}' file

# 先頭に行番号を付ける
awk '{print NR, $0}' file
```

### 5) `/etc/passwd` の情報を抜く

```bash
# user:uid:shell
awk -F: 'BEGIN{OFS=":"} {print $1,$3,$7}' /etc/passwd

# ログインシェルが /sbin/nologin じゃないユーザー
awk -F: '$7 !~ /\/nologin$/ {print $1, $7}' /etc/passwd
```

## オプション（運用でよく使う）

### `-F`（フィールド区切り）

- `-F:` のように指定

### `-v`（変数を外から渡す）

シェル変数を AWK に渡す定番です。

```bash
threshold=100
awk -v th="$threshold" '$2 >= th {print}' file
```

### `-f`（スクリプトファイル）

長くなったらファイル化が安全です。

```bash
# program.awk を作って
awk -f program.awk input.txt
```

### `--posix`（互換性を優先）

gawk の GNU 拡張を避けたい（移植性を上げたい）ときに検討します。

### `--lint`（怪しい書き方の警告）

運用スクリプトでの事故防止に役立ちます（gawk拡張）。

### `--profile` / `--debug`

大きい AWK プログラムになったときの解析向け（gawk拡張）。

## 文字列/正規表現/クォートで事故りがちポイント

### 1) シェルのクォート

AWK プログラムは基本 **シングルクォート**で囲うのが安全です。

- OK：`awk '{print $1}' file`
- 注意：ダブルクォートだと `$1` がシェルに解釈される危険がある

### 2) `"` と `\` のエスケープ

AWK 内の文字列では `"` や `\n` が出てくるので、ワンライナーは読みづらくなりがちです。
複雑になったら `-f` を検討します。

### 3) CSV は“厳密には難しい”

`-F,` は簡単ですが、ダブルクォートやエスケープを含む厳密CSVは素直に壊れます。
本気のCSVなら `python -c`、`csvtool`、`mlr` などを検討した方が安全です。

## 終了コード（exit status）

AWK では、プログラム内で `exit N` を書けば、その値がプロセスの終了コードになります。

```bash
# 条件を満たしたら exit 2
awk '$1=="panic" {exit 2} END{exit 0}' file
```

一般に、文法エラーや入力エラーなどがある場合は 0 以外で終了します。
正確な値は実装/バージョンで差があり得るので、Rocky 側で次を確認するのが確実です。

```bash
gawk --version
man gawk
```

## “awk” と “gawk” の関係（Rockyでの実務）

多くの環境で `awk` が `gawk` を指している（または互換AWK）ことがあります。

- まず `awk --version` / `gawk --version` を確認
- 運用スクリプトで gawk 拡張に依存するなら、明示的に `gawk` を使う

```bash
command -v awk gawk
awk --version 2>&1 | head
```

## 参考：最初に作りがちな awk スクリプト雛形

```awk
# program.awk
BEGIN {
  FS = "[[:space:]]+"   # 入力区切り
  OFS = "\t"            # 出力区切り
}

# 例：2列目がしきい値以上
$2 >= th {
  cnt[$1]++
}

END {
  for (k in cnt) {
    print k, cnt[k]
  }
}
```

実行：

```bash
awk -v th=100 -f program.awk input.txt | sort -k2,2nr
```
