---
title: "wget(1) 非対話ダウンロード（取得/再開/再帰/ミラー）実用ノート（Rocky Linux 9.7）"
date: 2026-03-23
---

# wget(1) — 非対話（non-interactive）でファイルをダウンロードする

`wget` は **非対話でURLからファイルを取得**するダウンローダです。
特に「落として保存する」「回線が不安定でも粘る」「再帰取得してミラーする」あたりが得意です。

- 1ファイルを丁寧に観測しながら叩く（API/ヘッダ/TLS切り分け）→ `curl`
- 取得して保存する、まとめて取る、ミラーする → `wget`

このノートは `man wget` を土台に、運用で頻出なオプション（保存、ログ、リトライ、再開、再帰）を中心に整理します。

## SYNOPSIS（書式：最小）

```sh
wget [option]... [URL]...
```

## まず結論：よく使うコマンド

### 1) とりあえず保存（URLの末尾名で保存）

```sh
wget "https://example.com/file.tar.gz"
```

### 2) 保存先ファイル名を指定

```sh
wget -O file.tar.gz "https://example.com/file.tar.gz"
```

### 3) 途中から再開（リジューム）

```sh
# -c, --continue
wget -c "https://example.com/big.iso"
```

### 4) ログをファイルに残す（運用で強い）

```sh
wget -o wget.log "https://example.com/file.tar.gz"

# 追記したいとき
wget -a wget.log "https://example.com/file2.tar.gz"
```

### 5) バックグラウンドで続行（切断しても続けたい）

```sh
# -b はバックグラウンド。ログは既定で wget-log
wget -b "https://example.com/big.iso"
```

### 6) リトライ・タイムアウト（不安定回線向け）

```sh
# 試行回数、接続/読み取りタイムアウト
wget --tries=10 --timeout=10 "https://example.com/file.tar.gz"
```

### 7) “サイトをミラーする” 入口（再帰取得）

```sh
# -r: 再帰
# -l: 深さ（1=リンク1段）
# -k: ローカル参照にリンク変換
# -p: ページ表示に必要な要素も取得（画像/CSS等）
wget -r -l 2 -k -p "https://example.com/docs/"
```

## 重要オプション（運用で踏むところ）

### 保存先

- `-O FILE`, `--output-document=FILE` : 保存先ファイル名を指定
- `-P DIR`, `--directory-prefix=DIR` : 保存先ディレクトリ

```sh
wget -P /tmp "https://example.com/file.tar.gz"
wget -P /tmp -O app.tar.gz "https://example.com/download?id=123"
```

### ログ

- `-o FILE`, `--output-file=FILE` : 進捗/情報をログに出す（stderr相当）
- `-a FILE`, `--append-output=FILE` : ログ追記
- `-q`, `--quiet` : 余計な出力を抑える

スクリプトでは `-q` を使うより `-o` でログへ逃がす方が、後から追えて便利なことが多いです。

### リトライ/タイムアウト

- `--tries=N` : 最大試行回数
- `--timeout=SEC` : ネットワークI/Oのタイムアウト（まとめ設定）

`wget` は「堅牢性」を重視しており、
ネットワークエラー時に **自動で再試行**し続けるのがデフォルト挙動として強めです。
運用のジョブでは無限に粘らないよう、上限を明示するのが無難です。

### 再開（reget）

- `-c`, `--continue` : 途中から再開

巨大ファイル取得では定番です。
ただしサーバ側が Range を受けない/ファイルが更新された等では期待通りにいかないことがあります。

## 再帰取得（recursive downloading）の注意点

`wget -r` は便利ですが、取りに行く範囲が膨らみやすいので、
最初は **深さ `-l` を小さく**して様子を見るのが安全です。

```sh
# まず1段だけ
wget -r -l 1 "https://example.com/docs/"
```

また `wget` は robots.txt を尊重します（デフォルト）。
「取れない」の原因が robots であることもあります。

## `curl` と似ていて混乱しやすい点

### `wget` は「保存」が自然、`curl` は「標準出力」が自然

- `wget URL` → ファイルに保存
- `curl URL` → ボディが標準出力（保存するなら `-o`）

この違いのせいで、
「端末にバイナリが流れて壊れた！」みたいな事故は `curl` の方が起きがちです。

### API を叩くなら基本は `curl`

`wget` でもHTTPヘッダやPOSTはできますが、
運用の「API疎通/認証/JSON」用途は `curl` の方が書きやすく、調査もしやすいです。

## 関連

- `curl(1)`（HTTPの観測・API叩きが得意）
- `wget2(1)`（後継。HTTP/2などが改善）
