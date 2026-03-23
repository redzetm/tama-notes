---
title: "ping(8) ICMP疎通確認（タイムアウト/回数/MTU/経路の初動切り分け）実用ノート（Rocky Linux 9.7）"
date: 2026-03-24
---

# ping(8) — send ICMP ECHO_REQUEST to network hosts

`ping` は ICMP の ECHO_REQUEST を送って、相手から ECHO_REPLY を返してもらうことで、

- 到達性（そもそも届く/返る）
- 往復遅延（RTT）
- パケット損失

をざっくり把握するコマンドです。

ただし、**ping が通らない＝必ずしも「通信できない」ではありません**（ICMP を FW で落としている/優先度が低い等）。
逆に、ping が通っても TCP/UDP が通るとは限りません。

## まず結論：よく使うコマンド

```bash
# 1) まずは回数を決めて短く（終わらない ping を避ける）
ping -c 3 example.com

# 2) タイムアウト短め（1秒）で生存確認に寄せる
# -W: 1パケットあたりの応答待ち（秒）
ping -c 1 -W 1 example.com

# 3) 全体の締切（deadline）を付ける
# -w: 実行全体の上限（秒）
ping -c 10 -w 5 example.com

# 4) IPv4 / IPv6 を固定
ping -4 -c 3 1.1.1.1
ping -6 -c 3 2606:4700:4700::1111

# 5) 送信元（IF/アドレス/VRF）を固定して切り分け
# -I は「アドレス/IF名/VRF名」を受け付ける
ping -c 3 -I eth0 example.com

# 6) 名前解決をしない（逆引きが遅い/壊れているときの保険）
ping -n -c 3 1.1.1.1

# 7) ログ向け：タイムスタンプ＋欠損検出
# -D: 各行の先頭に unix time + usec
# -O: 次の送信前に「未返信」を報告
ping -D -O -i 1 -c 10 example.com

# 8) MTU/フラグメント切り分け（IPv4）
# -M do: DF を立てて、PMTUチェックの範囲で大き過ぎるパケットは拒否
# -s は「データ部分」(デフォルト56)。IP/ICMPヘッダは別。
# 例: Ethernet MTU 1500 を想定するなら data=1472 が目安（1500-20-8）
ping -4 -M do -s 1472 -c 3 1.1.1.1

# 9) TTL を下げて“何ホップくらいか”の雰囲気を見る（tracerouteの代替ではない）
ping -t 5 -c 3 example.com
```

## SYNOPSIS（書式）

```text
ping [options] [hop...] destination
```

運用で覚えるべきはこれだけです。

- `-c` で回数を決める
- `-W`（各パケットの待ち）/`-w`（全体の締切）を使い分ける

## よく使うオプション（運用で効くもの）

- `-c count`
  - `count` 回送ったら止まる。
- `-W timeout`
  - 1パケットあたりの応答待ち時間（秒）。
- `-w deadline`
  - 実行全体の上限時間（秒）。
- `-i interval`
  - 送信間隔（秒）。
  - `man ping` にある通り、一般ユーザーは極端に短い値は設定できない。
- `-n`
  - 数値出力のみ（逆引きしない）。
- `-4` / `-6`
  - IPv4/IPv6 固定。
- `-I interface`
  - 送信元を指定（アドレス/IF名/VRF名）。
- `-s packetsize`
  - 送るデータ部分のサイズ（バイト）。
- `-M pmtudisc_opt`（IPv4で特に出番）
  - PMTU まわりの動作。
  - `do` / `want` / `probe` / `dont`
- `-D`
  - 各行にタイムスタンプ（unix time + usec）を付与。
- `-O`
  - 次パケット送信前に「未返信」を報告（ログで欠損を見つけやすい）。
- `-t ttl`（ping only）
  - TTL を指定。

## 終了コード（EXIT STATUS）

`man ping` の通り：

- `0`：成功（必要な返信が受信できた）
- `1`：返信が一切無い、または `-c` と `-w` を併用したときに締切までに `count` 返信に届かなかった
- `2`：その他のエラー

この環境でも簡単に実測できました：

```bash
ping -c 1 -W 1 127.0.0.1 >/dev/null 2>&1; echo $?
# 0

ping -c 1 -W 1 203.0.113.1 >/dev/null 2>&1; echo $?
# 1（応答なし）
```

## 「ping は通るのに、アプリが死ぬ」あるある

- ping（ICMP）だけ許可されていて、TCP/UDP は閉じている
- 逆に ICMP は落とされているが、TCP 443 は通る
- 遅延は小さいのにスループットが出ない（帯域/輻輳/再送）

なので、

- TCP の疎通：`nc` / `curl` / `openssl s_client`
- 経路と品質：`mtr`
- 実際に流れているか：`tcpdump`

と組み合わせると切り分けが速いです。

## MTU（フラグメント）切り分けのコツ（IPv4）

「SSH は繋がるのに特定の通信だけ詰まる」などで、

- MTU が途中で小さくなっている
- PMTUD が阻害されている（ICMP Fragmentation Needed が戻らない）

みたいな症状が疑われるときに `-M` と `-s` が役立ちます。

- 例：Ethernet MTU=1500 を想定した最大付近
  - `-s 1472`（$1500 - 20 - 8$）

```bash
ping -4 -M do -s 1472 -c 3 1.1.1.1
```

## 権限（SECURITY / capability）

`man ping` の SECURITY 節の通り、状況によって `CAP_NET_RAW` が必要です。

この環境では `ping` に capability が付与されています：

```bash
getcap /usr/bin/ping
# /usr/bin/ping cap_net_raw=ep
```

## IPv6 リンクローカル宛（fe80::/10）の注意

`man ping` にある通り、IPv6 のリンクローカル宛は、出力インタフェースの指定が必要になることがあります。

- `%` 記法（推奨されがち）

```bash
ping fe80::1%eth0
```

- `-I` で指定

```bash
ping -I eth0 fe80::1
```

## 事故りポイント（ping あるある）

- `ping` を止め忘れる → まず `-c` を付ける
- “遅い/不安定”の判断は平均だけでなく損失も見る（`packet loss`）
- `-w`（全体）と `-W`（1回）を混同する
- ping が通らない原因が「名前解決」だった → `ping -n 1.2.3.4` で切り分け
- ICMP を FW が落としている/優先度が低い → `tcpdump` や `mtr` と併用

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
rpm -q iputils

ping -V
man ping
```
