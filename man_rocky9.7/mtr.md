---
title: "mtr(8) 経路+遅延/損失をまとめて診断（traceroute+ping）実用ノート（Rocky Linux 9.7）"
date: 2026-03-23
---

# mtr(8) — a network diagnostic tool

`mtr` は `traceroute`（経路）と `ping`（遅延/損失）を合体させたような診断ツールです。

- 宛先までの **各ホップ**（ルータ/中継点）ごとに
  - パケット損失（Loss）
  - RTT（往復遅延：Last/Avg/Best/Wrst など）
  を継続的に観測できます。

`ping` だけだと「どこで遅い/落ちてるか」が分からず、`traceroute` だけだと「統計としての品質」が分かりにくいので、障害調査の初動で便利です。

## まず結論：よく使うコマンド

```bash
# 1) 運用の定番（レポートモードで統計を出して終了）
# -r: report（対話UIではなく、統計を出して終わる）
# -w: wide（ホスト名を切らない）
# -n: no-dns（逆引きしない＝速い・DNSの影響を排除）
# -c: cycles（何回測るか）
# -i: interval（間隔。root は 0〜1 秒も選べる）
# -o: 表示列（Loss/Last/Avg/Best/Wrst/StDev など）
# ※ 例の列指定は「Loss, Sent, Last, Avg, Best, Wrst, StDev」
mtr -r -w -n -c 100 -i 0.2 -o "LSD NBAW V" example.com

# 2) まずはサクッと（短時間で傾向を見る）
mtr -r -n -c 20 example.com

# 3) DNS（逆引き）が遅い/壊れている疑い → とりあえず -n で測る
mtr -r -n -c 50 1.1.1.1

# 4) ICMP が通らない/優先度が低い疑い → TCP SYN で試す（例: 443）
# -T: tcp, -P: port
mtr -r -n -T -P 443 -c 50 example.com

# 5) 出口回線/IF を固定して測る（マルチホーム切り分け）
# -I: interface
mtr -r -n -I eth0 -c 50 example.com

# 6) ソースIPを固定（経路が変わる環境の切り分け）
# -a: address
mtr -r -n -a 192.0.2.10 -c 50 example.com

# 7) 機械処理向けに JSON/CSV/XML
mtr -r -n -c 50 --json example.com
mtr -r -n -c 50 --csv  example.com
mtr -r -n -c 50 --xml  example.com
```

## SYNOPSIS（書式）

```text
mtr [-4|-6] [-F FILENAME] [--report] [--report-wide] [--xml] [--gtk] [--curses]
    [--displaymode MODE] [--raw] [--csv] [--json] [--split] [--no-dns] [--show-ips]
    [-o FIELDS] [-y IPINFO] [--aslookup] [-i INTERVAL] [-c COUNT] [-s PACKETSIZE]
    [-B BITPATTERN] [-G GRACEPERIOD] [-Q TOS] [--mpls] [-I NAME] [-a ADDRESS]
    [-f FIRST-TTL] [-m MAX-TTL] [-U MAX-UNKNOWN] [--udp] [--tcp] [--sctp]
    [-P PORT] [-L LOCALPORT] [-Z TIMEOUT] [-M MARK] HOSTNAME
```

運用目線の覚え方：

- 目視で見る → `mtr HOST`（対話UI）
- 証跡・共有する → `mtr -r -c N HOST`（レポートモード）

## どこを見るか（読み方のコツ）

`mtr` の表示列（デフォルトは対話キー `o` の説明にある `LRS N BAWV`）は、要するに「損失と遅延の統計」です。

- Loss（損失）
  - “宛先まで”の損失が問題かどうかをまず見る
- Avg / Wrst（平均/最悪）
  - 体感遅延やタイムアウトが出る系は `Wrst` が跳ねやすい
- StDev（標準偏差）や Jitter
  - “たまに遅い”タイプは平均より分散（揺れ）のほうが効くことがある

### 超重要：中継ホップの Loss% を鵜呑みにしない

途中のルータは **ICMP を低優先度にしたり、レート制限**したりします（`man mtr` の BUGS にも注意書きがあります）。

その結果：

- 途中の hop で Loss% が高い
- でも次の hop 以降（最終宛先含む）は Loss% が低い/0

ということが起きます。

この場合は「経路上のそのルータが *応答を絞っているだけ*」の可能性が高く、即「そこが障害」とは断定しません。

逆に、**最終宛先まで Loss/遅延が悪化**しているなら、実害として疑う価値が高いです。

## よく使うオプション（運用で効くやつ）

- `-r`, `--report`
  - レポートモード（統計を出して終了）。運用で貼るならまずこれ。
- `-w`, `--report-wide`
  - report でホスト名を切らない。
- `-c COUNT`, `--report-cycles COUNT`
  - 何回測るか（1 cycle=だいたい 1 秒）。
- `-i SECONDS`, `--interval SECONDS`
  - 送信間隔（root は 0〜1 秒の範囲も選べる）。
- `-n`, `--no-dns`
  - 逆引きしない（数値IPのみ）。DNSの遅さ/不調の影響を排除でき、速い。
- `-b`, `--show-ips`
  - ホスト名とIPを両方表示。
- `-o FIELDS`, `--order FIELDS`
  - 表示列を指定。
  - 指定可能なフィールド（代表）：
    - `L` Loss ratio
    - `D` Dropped packets
    - `R` Received packets
    - `S` Sent packets
    - `N` Newest RTT(ms)
    - `B` Min/Best RTT(ms)
    - `A` Average RTT(ms)
    - `W` Max/Worst RTT(ms)
    - `V` Standard Deviation
    - `J` Current Jitter
    - `M` Jitter Mean/Avg.
    - `X` Worst Jitter
    - `I` Interarrival Jitter
- `-y n`, `--ipinfo n`
  - hop ごとの付加情報。
  - `0` は AS番号（`-z` と同等）。
- `-z`, `--aslookup`（=`--ipinfo 0`）
  - AS番号を表示。
- `-4` / `-6`
  - IPv4/IPv6 固定。
- `-I NAME`, `--interface NAME`
  - 送信に使うインタフェースを指定。
- `-a ADDRESS`, `--address ADDRESS`
  - 送信元アドレスを bind。
  - 注意：DNSリクエストには適用されない（`man mtr` の NOTE）。

### ICMP が怪しいときの手札（プロトコル切り替え）

- `-u`, `--udp`：UDP
- `-T`, `--tcp`：TCP SYN（`PACKETSIZE` は無視される）
- `-S`, `--sctp`：SCTP
- `-P PORT`, `--port PORT`：TCP/SCTP/UDP の宛先ポート
- `-L LOCALPORT`, `--localport LOCALPORT`：UDP の送信元ポート

### 詰まりやすいリソース系

- `-Z SECONDS`, `--timeout SECONDS`
  - ソケットを開いたまま待つ秒数。
  - `man mtr` にある通り、**大きい timeout + 短い interval** は FD を食い潰しやすいです。

## 対話操作（INTERACTIVE CONTROL）

`mtr` 実行中に押せるキー（`man mtr` より）：

- `?` / `h`：ヘルプ
- `p`：一時停止（SPACE で再開）
- `d`：表示モード切替
- `n`：DNS on/off（逆引きの切替）
- `o`：表示列（デフォルト `LRS N BAWV`）
- `j`：latency / jitter 統計の切替
- `c`：report cycle 数（デフォルト無限）
- `i`：interval 秒（デフォルト 1）
- `f`：first TTL（デフォルト 1）
- `m`：max TTL
- `u`：ICMP/UDP 切替
- `y`：IP info 切替
- `z`：ASN info on/off
- `q`：終了

## 権限・実行に関するメモ（Rocky 9系の現実）

ICMP などの probe には raw socket が必要なので、権限周りで詰まることがあります。

この環境では `mtr-packet` に capability が付与されています：

```bash
getcap /usr/bin/mtr-packet
# /usr/bin/mtr-packet cap_net_raw=ep
```

`man mtr` にもある通り、probe の送受信に `mtr-packet`（`MTR_PACKET`）を使います。

## ENVIRONMENT

- `MTR_OPTIONS`
  - 環境変数でデフォルトオプションを指定（コマンドラインのほうが優先）。
- `MTR_PACKET`
  - `mtr-packet` のパス。
- `DISPLAY`
  - GTK+ フロントエンド用（X11）。

## 終了コード（exit status）

`man mtr` には独立した「EXIT STATUS」節が見当たりませんでした。

また、この環境では例えば解決不能なホスト名でも **exit=0** になりました（stderr にエラーは出ます）。

```bash
mtr -r -c 1 -n no.such.host.invalid; echo "exit=$?"
# mtr: Failed to resolve host: ...
# exit=0
```

なので、スクリプトで成否判定するなら：

- 可能なら stderr（エラーメッセージ）も含めて扱う
- もしくは JSON/CSV/XML 出力を使い、Loss/RTT などの値で判断する

…のように「終了コード一本足で判定しない」方針が安全です。

## 事故りポイント（mtr あるある）

- 途中 hop の Loss% だけで断定しない（ICMP の低優先度/レート制限がある）
- DNS（逆引き）が遅いと表示が重くなる → まず `-n`、必要なら後で `-b`
- `mtr` は継続的に probe を投げるので、回数/間隔を雑に上げるとネットワークに負荷が出る（`man mtr` に注意あり）
- ICMP が通りにくい経路では `-T -P 443` などで “アプリが使う経路” に寄せて測る
- `-Z` を大きくしすぎると FD を食う（特に短い `-i` と組み合わせると危険）
