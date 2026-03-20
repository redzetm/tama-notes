---
title: "ps(1) プロセス一覧・調査コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-20
---

# ps(1) — プロセス一覧

`ps` は「今いるプロセス（またはスレッド）を静的に一覧表示する」コマンドです。
`top` はリアルタイム監視、`ps` は **スナップショット**として正確に抜き出す用途に向きます。

このノートは Rocky Linux 9.7 前提で、運用で迷いやすい
`-o` で列を作る方法、`--sort`、スレッド表示を中心にまとめます。

表記ゆれを減らすため、このノートのコマンド例は **基本的に「ダッシュ付き（`-` / `--`）」に統一**します。

## まず結論：よく使うコマンド

```bash
# よく見る（ユーザー/CPU/MEM/コマンド）
# このノートは「-aux が使える」前提で例を出します（環境差はあり）
ps -aux

# 全体（まずはこれ）
ps -ef
ps -ef --forest

# CPUを食っている順に上位を見たい（列を固定してソート）
ps -eo pid,ppid,user,stat,%cpu,%mem,etime,cmd --sort=-%cpu | head

# メモリを食っている順
ps -eo pid,ppid,user,stat,%mem,rss,vsz,cmd --sort=-%mem | head

# 特定PIDを詳しく（列カスタム）
ps -p 1234 -o pid,ppid,user,group,stat,lstart,etime,%cpu,%mem,rss,vsz,cmd

# 名前で雑に探す（コマンドラインを含めて探したい時は grep と併用）
ps -ef | grep -i '[n]ginx'
```

`ps` は「表示列とソート」を自分で作れるようになると運用力が上がります。

## SYNOPSIS（書式の考え方）

`ps` は短いオプション（`-e` など）と長いオプション（`--sort` など）が混在します。
運用で迷いにくいように、このノートでは次の型を基本にします。

```bash
# まずは全体を出す
ps -ef

# 欲しい列だけ自分で固定して出す（運用で最重要）
ps -eo pid,ppid,user,stat,%cpu,%mem,etime,cmd

# 並び順も固定する
ps -eo pid,user,stat,%cpu,%mem,etime,cmd --sort=-%cpu

# 親子関係（ツリー）
ps -ef --forest
```

## プロセス選択（フィルタ）の考え方

`ps` は「表示列」だけでなく「**どのプロセスを出すか（選択）**」もオプションで変わります。

- 何も付けない `ps`
  - 基本は「自分のユーザー + 今の端末（TTY）に紐づくプロセス」だけ
- 迷ったら `-e`（または `-A`）
  - **全プロセス**を対象にする（運用でまず困らない）

補足：選択オプションは歴史的経緯があり、組み合わせによって「追加で選ぶ」動作になったりします。迷ったら「`-e` を付けてから `-o` と `--sort` で整形」が一番事故が少ないです。

### まず覚える選択フラグ（よく使う順）

```bash
# 全プロセス（まずはこれ）
ps -e
ps -ef

# 端末ありのプロセスを中心に（デーモンは除外されがち）
ps -a

# 端末なし（デーモン等）も含めたい
ps -x

# Running（実行中/実行可能）のみ
ps -r

# 現在の端末に紐づくプロセス（TTY指定なしの t 相当）
ps -T
```

### リストで選ぶ（PID/ユーザー/TTY/コマンド名）

単発調査で一番効くのがこの系です。

```bash
# PID 指定（複数OK）
ps -p 1,1234,5678 -o pid,ppid,user,stat,lstart,cmd

# 親 PID 指定（その親の子だけ）
ps --ppid 1 -o pid,ppid,user,stat,cmd

# コマンド名（実行ファイル名）で選ぶ（コマンドライン全体ではない点に注意）
ps -C sshd -o pid,user,stat,cmd

# 実ユーザー（RUID） / 実効ユーザー（EUID）
ps -U root -o pid,user,euser,stat,cmd
ps -u apache -o pid,user,stat,cmd

# TTY で選ぶ（例：tty1 / pts/0。端末なしは "-"）
ps -t pts/0 -o pid,user,stat,cmd
ps -t - -o pid,user,stat,cmd
```

### quick-pid（大量環境の高速化に使う）

`-q` は PID 指定だけを高速に読むモードです。

```bash
ps -q 1234,5678 -o pid,ppid,stat,cmd
```

注意：`-q` は「指定PIDだけを読む」都合上、追加のフィルタやソート、ツリー表示が制限されます。普段は `-p` で十分です。

### 逆選択（除外）

`-N` / `--deselect` で条件を反転（その条件を満たすものを除外）できます。

```bash
# 例：端末なし（TTYなし）を除外したい…など
ps -e -N -t - -o pid,tty,user,stat,cmd
```

## フィールド（列）の基本

`ps` は列を理解すると強いです。最低限、次だけ覚えれば実用になります。

- `pid`：プロセスID
- `ppid`：親プロセスID
- `user` / `uid`：ユーザー
- `group` / `gid`：グループ
- `stat`：状態（R/S/D/Z など）＋フラグ
- `%cpu`：CPU使用率
- `%mem`：メモリ使用率
- `rss`：常駐メモリ（KiB 単位のことが多い）
- `vsz`：仮想メモリ（KiB 単位のことが多い）
- `etime`：経過時間（起動からの経過）
- `lstart`：開始時刻（人間向け）
- `cmd`：コマンドライン
- `comm`：実行ファイル名（短い）

## `-o` で「欲しい列だけ」作る（最重要）

`ps -o` で列を指定できます。

```bash
# 例：運用で使いやすい最小セット
ps -eo pid,ppid,user,stat,%cpu,%mem,etime,cmd

# 例：ヘッダ無し（スクリプトで扱いやすい）
ps -eo pid=,ppid=,user=,stat=,cmd=
```

### 列名の一覧を知りたい

環境で使える列は多いので、一覧は `man ps` で確認するのが確実です。

## `--sort` で並び順を固定する

`--sort` でソートできます。

- `--sort=key`：昇順
- `--sort=-key`：降順

```bash
# CPUが高い順
ps -eo pid,user,stat,%cpu,etime,cmd --sort=-%cpu | head

# メモリが高い順
ps -eo pid,user,stat,%mem,rss,cmd --sort=-%mem | head
```

## スレッドを見たい（top の `-H` に対応）

「プロセスは軽そうに見えるのに重い」時は、スレッド単位で見ると原因に近づきます。

```bash
# スレッドを表示（LWP=LinuxのスレッドID）
ps -eL -o pid,lwp,tid,psr,stat,%cpu,%mem,etime,cmd

# 特定PIDのスレッドだけ
ps -L -p 1234 -o pid,lwp,tid,psr,stat,%cpu,etime,cmd
```

補足：フィールド名は環境で差が出るので、うまく出ない場合は `man ps` で確認してください。

## 親子関係（ツリー）を見たい

```bash
# 全体 + ツリー
ps -ef --forest
```

「親子を厳密に追う」なら `pstree` も強いですが、`ps` だけでもまずは十分です。

## 出力（表示列/ヘッダ/幅）を制御する

`ps` は「何を選ぶか」より「どう見せるか」で差が出ます。運用で効く範囲だけ押さえます。

### よく使う出力スタイル

```bash
# フルフォーマット
ps -ef

# さらに列が増える（-F は -f を含む）
ps -eF

# long format（状態/フラグ系が見やすい）
ps -el

# user-oriented format
ps -eu
```

### ヘッダ制御（スクリプト向け）

```bash
# ヘッダ無し（人格/スタイル差があるので、迷ったら長い方が安全）
ps -eo pid=,ppid=,user=,stat=,cmd=
ps -eo pid,ppid,user,stat,cmd --no-headers

# ヘッダを繰り返す（ページング出力など）
ps -eo pid,ppid,user,stat,cmd --headers
```

### 行が長い/切れる時

```bash
# ワイド出力（2回でさらに広く）
ps -eww -o pid,user,stat,cmd
```

列を減らす（`cmd`→`comm` にする等）も有効です。

### 「コマンド名」だけ見たい（argv を捨てる）

通常は `cmd`（コマンドライン）が出ます。
実行ファイル名（短い名前）だけ見たい時は `comm` を使うと安全です。

```bash
ps -eo pid,user,stat,comm
```

## STAT（状態）の読み方（最低限）

`stat` は「1文字の状態 + 追加フラグ」です。詰まり/ハング/ゾンビの切り分けに効きます。

- 代表的な状態文字
  - `R`：実行中/実行可能
  - `S`：割り込み可能スリープ（待ち）
  - `D`：割り込み不能スリープ（だいたい IO 待ち。目立つと要注意）
  - `T`：停止（ジョブ制御/トレース）
  - `Z`：ゾンビ（親が回収していない）
- よく見る追加フラグ
  - `s`：セッションリーダ
  - `l`：マルチスレッド
  - `+`：フォアグラウンドプロセスグループ
  - `<` / `N`：優先度が高い/低い（nice）

例：`Ssl+` のように複数文字になります。

## よくある調査レシピ

### 1) サービスが起動しているか（名前で当たりを付ける）

```bash
ps -ef | grep -i '[s]shd'
ps -ef | grep -i '[n]ginx'
```

ポイント：`[n]ginx` のように書くと `grep nginx` 自身が結果に出にくいです。

### 2) PID が分かっている：そのプロセスだけ詳しく

```bash
ps -p 1234 -o pid,ppid,user,stat,lstart,etime,%cpu,%mem,rss,vsz,cmd
```

### 3) ゾンビがいる？

```bash
ps -eo pid,ppid,user,stat,etime,cmd | grep ' Z'
```

`stat` に `Z` が入っていれば zombie です。親（PPID）側の回収問題が疑われます。

### 4) D 状態（IO待ち）が多い？

```bash
ps -eo pid,ppid,user,stat,wchan:20,etime,cmd | grep ' D'
```

`wchan` は「どのカーネル関数で寝ているか」のヒントです（権限や設定で見え方が変わることがあります）。

## 事故りやすい点（短く）

- `ps -ef` だけで判断しない
  - 欲しい列（%CPU/%MEM/経過時間など）は `-o` で固定して見る
- `cmd` と `comm` を混同しない
  - `cmd` はコマンドライン（引数込み）、`comm` は実行ファイル名（短い）
- `ps -C` は「コマンド名（実行ファイル名）」一致
  - 引数やフルパスで探したいなら `ps -ef | grep ...` の方が確実なことが多い
- `ps -aux` は環境差がある
  - このノートは「手元の Rocky Linux 9.7 で `ps -aux` が実用的に使えている」前提で例を出します。別環境へ持ち出すスクリプトでは `ps -ef` や `ps -eo ...` に寄せる方が安全です
- `ps | grep` で引っかからない
  - 大文字小文字、正規表現、フルパス、権限の可能性があります。`-i` や `cmd`/`comm` の違いも意識
- 1行が長すぎる
  - `cmd` を `comm` に変える、`w`（ワイド出力）系の指定を検討、または列を減らす

## 参考：バージョン確認（環境差あり）

このノートは Rocky Linux 9.7 を想定していますが、`ps`（procps-ng）の版数は更新状況で変わります。
挙動差を疑ったら、まず自分の環境で確認してください。

```bash
# Rocky 側の前提確認（Rocky 9.x で有効）
cat /etc/redhat-release
rpm -q procps-ng

ps --version

# 例（Rocky Linux 9.7 環境の実測例）
# ps from procps-ng 3.3.17
```
