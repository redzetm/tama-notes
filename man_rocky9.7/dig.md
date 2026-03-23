---
title: "dig(1) DNS 問い合わせ（nslookup代替・権威/キャッシュ切り分け）実用ノート（Rocky Linux 9.7）"
date: 2026-03-23
---

# dig(1) — DNS lookup utility

`dig` は DNS サーバに問い合わせて、返ってきた応答（A/AAAA/CNAME/MX/TXT/NS/SOA など）を表示するツールです。

運用での出番は主にこれです。

- 「名前解決できない」：そもそも DNS 応答が返る？NXDOMAIN？
- 「引けたり引けなかったり」：どのサーバに聞いている？（`/etc/resolv.conf`）
- 「意図しないIPに向いている」：CNAME の先/TTL/権威情報
- 「逆引き」：PTR（`-x`）

Rocky Linux 9 系では、だいたい `bind-utils` に入っています。

```bash
# インストール（入ってない場合）
sudo dnf install -y bind-utils
```

## まず結論：よく使うコマンド

```bash
# 1) 一番短い形（Aレコードを短く）
dig +short example.com

# 2) AAAA（IPv6）
dig +short AAAA example.com

# 3) CNAME を追う（まずは“何に向いているか”）
dig CNAME example.com

# 4) どのDNSに聞くかを固定（切り分けの基本）
dig @8.8.8.8 example.com

# 5) 逆引き（PTR）
dig -x 1.2.3.4

# 6) NS（委任先）と SOA（ゾーンの管理情報）
dig NS example.com

dig SOA example.com

# 7) TXT（SPF/検証など）
dig TXT example.com

# 8) ルートから追う（権威を追跡。DNSの“どこで壊れてるか”）
dig +trace example.com

# 9) 応答の要点だけ（ANSWER セクション中心）
dig +noall +answer example.com

# 10) 応答コード（NOERROR/NXDOMAIN等）や問い合わせ先も含めて見る
# +cmd はデフォルトで出がちだが、明示しておくと読みやすいことがある
dig +cmd example.com
```

## SYNOPSIS（書式）

```text
dig [@server] [-b address] [-p port#] [-q name] [-t type] [-x addr] [queryopt...]
```

ざっくり覚え方：

- `dig @SERVER NAME TYPE`
  - `@SERVER`：問い合わせ先のDNSサーバ（省略すると `/etc/resolv.conf`）
  - `NAME`：問い合わせ対象の名前
  - `TYPE`：A/AAAA/MX/TXT/NS/SOA/PTR…

例：

```bash
dig @1.1.1.1 www.example.com A
```

## まず見るべき出力（どこを読むか）

`dig` の出力は情報が多いので、運用では次を優先すると速いです。

- `status:`
  - `NOERROR`：成功
  - `NXDOMAIN`：その名前は存在しない
  - `SERVFAIL`：サーバ側エラー（上流解決失敗/DNSSEC/到達性など色々）
- `ANSWER SECTION:`
  - 実際に返ったレコード（A/AAAA/CNAME…）
  - TTL（キャッシュ時間）
- `SERVER:`
  - どのDNSサーバに聞いたか（切り分けで重要）
- `Query time:`
  - 遅い/タイムアウト気味の判断に使う

## よく使う query option（+オプション）

### `+short`

短く（値だけ）出す。

```bash
dig +short A example.com
```

### `+noall +answer`

余計な節を消して、ANSWER だけ出す定番。

```bash
dig +noall +answer example.com
```

必要に応じて `+authority`（権威情報）や `+additional` も追加します。

### `+trace`

ルートDNSから順に辿って解決します。

- 委任（NS）で詰まっている
- 権威DNSが返している内容が想定と違う

みたいなときに、壊れている層が見つけやすいです。

```bash
dig +trace example.com
```

## 「nslookup 的な使い方」からの移行メモ

- “引けるかどうかだけ”なら `dig +short name`
- “どのDNSに聞くか固定”は `dig @server name`
- “逆引き”は `dig -x ip`

`dig` は出力が長い分、`+short` と `+noall +answer` を覚えると使い勝手が良くなります。

## `/etc/resolv.conf` と `.digrc`

- `dig` は通常 `/etc/resolv.conf` に書かれた DNS サーバへ問い合わせます
- ユーザーごとのデフォルトは `${HOME}/.digrc` で指定できます
  - スクリプトで予測可能にしたい場合は `.digrc` の影響に注意

## RETURN CODES（終了コード）

`man dig` にある通り：

- `0`：DNS 応答を受信（NXDOMAIN を含む）
- `1`：使い方エラー
- `8`：バッチファイルを開けない
- `9`：サーバから応答が無い
- `10`：内部エラー

注意：`NXDOMAIN` でも `0` です（「応答を受け取った」ため）。

## 事故りポイント（dig あるある）

- 「引けない」を見たら、まず `@server` で問い合わせ先を固定（ローカル設定のせいか、権威側のせいかを分離）
- `NXDOMAIN` は“応答としては成功”なので、終了コードだけで判断しない（`status:` も見る）
- CDN などで応答が場所/時間で変わることがある（複数回/複数DNSで確認する）
