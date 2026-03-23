---
title: "curl(1) HTTP/HTTPS 通信・API叩き・ファイル取得 実用ノート（Rocky Linux 9.7）"
date: 2026-03-23
---

# curl(1) — URLへデータを転送する（主にHTTP/HTTPS）

`wget` と似てるが非なるものなりーー
`curl` は **URL へアクセスしてデータを送受信する**コマンドです。
運用ではだいたい次の用途に集約されます。

- 疎通確認（HTTPステータス、TLS、リダイレクト）
- API の叩き（GET/POST/PUT、ヘッダ、認証）
- ファイルのダウンロード/アップロード

`curl` は標準出力にボディを出すのが基本なので、**ログ・スクリプト用途では「どこに何を出すか」**を最初に決めるのがコツです。

## SYNOPSIS（書式：最小）

```sh
curl [options] URL
curl [options] URL1 URL2 ...
```

## まず結論：現場でよく使う定番セット

### 1) “落ちたら落ちたと分かる” 取得（スクリプト向け）

```sh
# -f: HTTPエラー(4xx/5xx)を失敗にする
# -s: 進捗等を黙らせる（標準出力が汚れない）
# -S: -s時でもエラーは表示
# -L: リダイレクト追従
curl -fsSL "https://example.com/file" -o file
```

### 2) HTTP ステータスだけ欲しい（body不要）

```sh
# -o /dev/null: ボディ捨て
# -w: 最後に整形して出す
curl -sS -o /dev/null -w "%{http_code}\n" "https://example.com/"
```

### 3) ヘッダを見たい（調査の最初）

```sh
# -I: HEAD（サーバによってはGETに化ける場合もある）
curl -sS -I "https://example.com/"

# GETしつつヘッダも見たい
curl -sS -D - "https://example.com/" -o /dev/null
```

### 4) どこへリダイレクトしているか追う

```sh
curl -sS -I -L "http://example.com/"

# 途中の Location を含むヘッダを逐次出したいなら -v が強い
curl -v -L "http://example.com/" -o /dev/null
```

## OUTPUT（標準出力/保存先の使い分け）

### `-o` と `-O`

- `-o FILE` : 保存先ファイル名を指定
- `-O` : URL の末尾名で保存（`--remote-name`）

```sh
curl -fsSLo app.tar.gz "https://example.com/download/app.tar.gz"
curl -fsSL -O "https://example.com/download/app.tar.gz"
```

複数URLのときは、保存先オプションも複数必要です。

## 疎通・原因切り分けで使うオプション

### `-v`（まずこれ）

`-v` は **DNS解決・接続・TLSハンドシェイク・リクエスト/レスポンスヘッダ**の流れが見えるので、
「つながらない」「遅い」「TLSで落ちる」を切る第一手として強いです。

```sh
curl -v "https://example.com/" -o /dev/null
```

### タイムアウト（ハング対策）

```sh
# 全体の最大時間
curl --max-time 10 "https://example.com/"

# 接続確立までの最大時間
curl --connect-timeout 3 "https://example.com/"
```

### リトライ（瞬断に強くする）

```sh
curl --retry 5 --retry-delay 1 --max-time 30 "https://example.com/"
```

注意：API を叩く場合、POST/PUT は **リトライで二重実行**になり得ます。
安全にするなら冪等性（idempotency）やサーバ仕様を確認します。

### TLS/証明書まわり

```sh
# CAを明示（社内CAなど）
curl --cacert /path/to/ca.pem "https://internal.example.com/"

# 証明書検証を無効化（切り分け専用。恒久運用では避ける）
curl -k "https://example.com/"
```

`-k`（`--insecure`）で通るなら、
CAチェーン・中間証明書・社内プロキシの置換・時刻ズレなどを疑います。

### 圧縮

```sh
# Accept-Encoding を付けて圧縮を受ける（自動で展開して出力）
curl --compressed "https://example.com/"
```

## API を叩く（GET/POST/JSON）

### HTTPメソッド

```sh
curl -X GET "https://api.example.com/v1/items"
curl -X POST "https://api.example.com/v1/items"
curl -X PUT "https://api.example.com/v1/items/1"
curl -X DELETE "https://api.example.com/v1/items/1"
```

注意：`-X` は強制指定なので、意図しない動き（リダイレクト後のメソッド等）を招くことがあります。
単純な POST は `-d` を使うと自然に POST になります。

### ヘッダ付与（JSON の基本形）

```sh
curl -sS \
  -H "Accept: application/json" \
  -H "Content-Type: application/json" \
  "https://api.example.com/v1/items"
```

### JSONボディ（POST）

```sh
curl -sS \
  -H "Content-Type: application/json" \
  -d '{"name":"apple","count":3}' \
  "https://api.example.com/v1/items"
```

- `-d` は `application/x-www-form-urlencoded` を想定した挙動もあるので、JSONは `Content-Type` を明示します。
- `@file.json` でファイル送信できます。

```sh
curl -sS -H "Content-Type: application/json" -d @payload.json "https://api.example.com/v1/items"
```

### フォーム送信（multipart）

```sh
# -F は multipart/form-data
curl -sS -F "name=apple" -F "file=@./a.png" "https://api.example.com/upload"
```

### ベーシック認証

```sh
curl -u "user:pass" "https://example.com/protected/"
```

（シェル履歴に残るので、可能なら `-u user` でパスワードは対話入力、
またはトークン/設定ファイル等へ寄せるのが安全です）

## URL のクォート（超重要）

`&` `?` `*` `[` `]` `{` `}` などはシェルが特別扱いします。
特にクエリ付きURLは **ダブルクォート/シングルクォート**で囲むのが基本です。

```sh
# NG: & がバックグラウンド実行に解釈される
curl https://example.com/?a=1&b=2

# OK
curl "https://example.com/?a=1&b=2"
```

## 終了コード（トラブル時に見る）

`curl` の終了コードは HTTP ステータスとは別物です。
スクリプトでは `-f`（HTTPエラーを失敗扱い）と組み合わせると扱いやすいです。

代表例（よく踏むもの）：

- `0` : 成功
- `6` : ホスト名解決失敗
- `7` : 接続失敗（TCP接続不可）
- `22` : HTTPページ取得失敗（主に `-f` 使用時の 4xx/5xx）
- `28` : タイムアウト
- `35` : TLS接続エラー
- `51` : サーバ証明書の検証失敗
- `60` : CA証明書関連の問題（証明書検証失敗）

## “危険な定番” の扱い（注意）

よく見る `curl ... | sh` は便利ですが、
**ネットワーク上の内容をそのまま実行**するため事故りやすいです。
運用では次の形に寄せるのが無難です。

- まずファイルに保存して中身確認
- 可能なら checksum / 署名検証
- `--fail` を付けてエラー時に空実行しない

```sh
curl -fsSL "https://example.com/install.sh" -o install.sh
less install.sh
# sh install.sh
```

## 関連

- `wget(1)`（用途が近いが設計思想が違う）
- `openssl s_client`（TLS切り分けの低レベル観測）
- `jq(1)`（JSON整形）
