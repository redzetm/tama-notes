---
title: "ss(8) socket統計コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-20
---

# ss(8) — socket 統計コマンド（実用）

`ss` は Linux のソケット（TCP/UDP/UNIX など）の状態を表示するツールです。
`netstat` と似ていますが、`ss` の方が **TCP の内部情報**や **柔軟なフィルタ**を扱いやすいのが強みです。

このノートは「Linux運用で `man` 的に引けること」を目的に、実用寄りの情報だけを整理します。

## まず結論：よく使うコマンド

```bash
# まずはこれ（TCP/UDP の LISTEN を、ポート番号で、プロセス名付きで）
ss -tulnp

# 接続済みの TCP（ESTAB）を眺める（名前解決なし）
ss -tn state established

# タイマー情報つき（TIME-WAIT/FIN-WAIT が多い時に）
ss -tno state time-wait

# TCP内部情報（rtt/cwnd/wscale/pacing 等が出る。重いのでピンポイントで）
ss -tni state established

# SELinux コンテキストも（要 root/権限）
ss -tulnpZ
```

## SYNOPSIS（書式）

```text
ss [options] [ FILTER ]
```

`FILTER` は概ね次の形です。

```text
[ state STATE-FILTER ] [ EXPRESSION ]
```

以降で `STATE-FILTER` と `EXPRESSION` を詳説します。

## 出力の見方（最低限）

`ss` の表示列はオプションにより変化しますが、運用で頻出の見方だけ押さえます。

- `State`
  - TCP の状態（`LISTEN`/`ESTAB`/`TIME-WAIT` など）
- `Recv-Q` / `Send-Q`
  - 受信キュー / 送信キューのサイズ（詰まっているかの目安）
- `Local Address:Port`
  - ローカル側の待受/接続元
- `Peer Address:Port`
  - 相手側の接続先
- `users:(...)`（`-p` を付けたとき）
  - そのソケットを使っているプロセス情報（見えない場合は権限の可能性）

### 名前解決の罠

- 運用では基本 `-n` を推奨
  - 逆引きDNSや `/etc/services` 変換に引っ張られて遅くなる・出力が揺れるのを避けられます。

## OPTIONS（よく使う順）

### ヘルプ/出力整形

- `-h`, `--help`
	- ヘルプ（オプション一覧）
- `-V`, `--version`
	- バージョン表示
- `-H`, `--no-header`
	- ヘッダ行を表示しない（スクリプト用）
- `-Q`, `--no-queues`
	- `Recv-Q`/`Send-Q` 列を省略
- `-O`, `--oneline`
	- 1ソケットを1行で表示（折り返しを避けたい時）

### 表示対象（一覧の範囲）

- `-a`, `--all`
	- listening / 非 listening を両方表示（TCP だと LISTEN + ESTAB 等）
- `-l`, `--listening`
	- listening のみ（待受だけ見たい時の基本）
- `-B`, `--bound-inactive`
	- TCP の「bind 済みだが inactive（LISTEN/CONNECT ではない）」を表示
    - 例：アプリが bind したが listen していない、などの切り分けに使う

### プロトコル/ファミリ（何のソケットを見るか）

- `-t`, `--tcp` / `-u`, `--udp` / `-x`, `--unix`
	- TCP/UDP/UNIX ドメインソケットに絞る
- `-0`, `--packet`
	- PACKET ソケット（`-f link` の別名）
- `-w`, `--raw`
	- RAW ソケット
- `-d`, `--dccp`
	- DCCP ソケット
- `-S`, `--sctp`
	- SCTP ソケット
- `-M`, `--mptcp`
	- MPTCP ソケット
- `--tipc` / `--vsock` / `--xdp`
	- tipc / vsock / xdp に絞る（それぞれ `-f tipc/vsock/xdp` の別名）
- `-4`, `--ipv4` / `-6`, `--ipv6`
	- IPv4/IPv6 に絞る（`-f inet` / `-f inet6` の別名）
- `-f FAMILY`, `--family=FAMILY`
	- ソケットのファミリを指定
	- `FAMILY := inet|inet6|link|unix|netlink|vsock|tipc|xdp|help`
- `-A QUERY`, `--query=QUERY`, `--socket=QUERY`
	- ダンプする「ソケットテーブル」を列挙（カンマ区切り）
	- `QUERY` 例：
		- `all, inet, tcp, udp, raw, unix, packet, netlink, dccp, sctp, tipc, xdp, mptcp`
		- `unix_dgram, unix_stream, unix_seqpacket, packet_raw, packet_dgram, vsock_stream, vsock_dgram`
	- `!tcp` のように `!` で除外も可能（例：`-A 'all,!tcp'`）

### 表示の詳細度（何を追加表示するか）

- `-n`, `--numeric`
	- サービス名/ホスト名の解決をしない（運用では基本これ）
- `-r`, `--resolve`
	- 逆にホスト名解決を試みる（調査用途。遅い場合あり）
- `-p`, `--processes`
	- ソケットを使うプロセスを表示（`users:(...)`）
	- 注意：他ユーザーのプロセスが見えない場合がある（権限）
- `-T`, `--threads`
	- スレッド単位で表示（暗黙に `-p` を含む）
- `-o`, `--options`
	- タイマー情報（`timer:(<name>,<expire>,<retrans>)`）を表示
- `-e`, `--extended`
	- 詳細（`uid:<uid> ino:<inode> sk:<cookie>`）を表示
- `-m`, `--memory`
	- ソケットメモリ（`skmem:(...)`）を表示
- `-i`, `--info`
	- TCP 内部情報（`rtt`/`cwnd`/`wscale`/`pacing_rate` 等）を表示

補足：`-o` / `-e` / `-m` の出力フィールド

- `-o`（タイマー）
	- 出力形式：`timer:(<timer_name>,<expire_time>,<retrans>)`
	- `<timer_name>`
		- `on`：TCP retrans / early retrans / tail loss probe のいずれか
		- `keepalive`：TCP keepalive
		- `timewait`：TIME-WAIT 段階のタイマー
		- `persist`：ゼロウィンドウ probe
		- `unknown`：上記以外
	- `<expire_time>`：期限までの残り時間
	- `<retrans>`：再送が起きた回数
- `-e`（拡張）
	- `uid:<uid_number>`：ソケット所有ユーザーID
	- `ino:<inode_number>`：VFS inode
	- `sk:<cookie>`：ソケットを識別する cookie（uuid 的な値）
- `-m`（メモリ）
	- 出力形式：`skmem:(r<rmem_alloc>,rb<rcv_buf>,t<wmem_alloc>,tb<snd_buf>,f<fwd_alloc>,w<wmem_queued>,o<opt_mem>,bl<back_log>,d<sock_drop>)`
	- `rmem_alloc`：受信パケット向けに確保したメモリ
	- `rcv_buf`：受信に確保可能な総メモリ（バッファ）
	- `wmem_alloc`：送信で使用中（L3へ送られた分）
	- `snd_buf`：送信に確保可能な総メモリ（バッファ）
	- `fwd_alloc`：キャッシュ的に確保したが未使用の分（必要なら先に使われる）
	- `wmem_queued`：送信キューに積まれている（まだ L3 に出ていない）分
	- `opt_mem`：sockopt 等のオプション保存に使われるメモリ（例：TCP MD5 の鍵）
	- `back_log`：`sk_backlog`（プロセスが受信処理中に届いた分の待ち）
	- `sock_drop`：ソケットにデマルチプレクスされる前に落ちたパケット数

補足：`-i`（TCP 内部情報）で出力されうる代表的フィールド

`-i` は “出る可能性がある” 項目が多く、接続やカーネル設定によって表示は変わります。

- フラグ/オプション系
	- `ts`：timestamp option が有効
	- `sack`：SACK が有効
	- `ecn`：ECN が有効
	- `ecnseen`：受信で ECN を観測
	- `fastopen`：TCP Fast Open が有効
- アルゴリズム/スケール
	- `cong_alg`：輻輳制御アルゴリズム（例：`cubic`）
	- `wscale:<snd_wscale>:<rcv_wscale>`：window scale
- タイミング
	- `rto:<icsk_rto>`：再送タイムアウト（ms）
	- `backoff:<icsk_backoff>`：指数バックオフ段数（実RTOは `icsk_rto << icsk_backoff`）
	- `rtt:<rtt>/<rttvar>`：平均RTT / RTT偏差（ms）
	- `ato:<ato>`：ACK timeout（ms、遅延ACK）
- 送受信・輻輳関連
	- `mss:<mss>`：最大セグメントサイズ
	- `cwnd:<cwnd>`：congestion window
	- `pmtu:<pmtu>`：path MTU
	- `ssthresh:<ssthresh>`：slow start threshold
	- `bytes_acked:<bytes_acked>`：ack 済みバイト
	- `bytes_received:<bytes_received>`：受信バイト
	- `segs_out:<segs_out>`：送信セグメント数
	- `segs_in:<segs_in>`：受信セグメント数
- 速度/最近の活動
	- `send <send_bps>bps`：送信bps
	- `lastsnd:<lastsnd>`：最後に送ったのが何ms前か
	- `lastrcv:<lastrcv>`：最後に受け取ったのが何ms前か
	- `lastack:<lastack>`：最後に ACK を受けたのが何ms前か
	- `pacing_rate <pacing_rate>bps/<max_pacing_rate>bps`：pacing rate
	- `rcv_space:<rcv_space>`：受信バッファ自動調整用の内部変数
- MPTCP 関連（環境により）
	- `tcp-ulp-mptcp flags:[...] token:... seq:... sfseq:... ssnoff:... maplen:...`

### 便利/危険なオプション

- `-s`, `--summary`
	- サマリのみ表示（ソケット数が巨大で一覧が重いとき）
- `-E`, `--events`
	- ソケットが破棄されるたびに継続表示
- `-K`, `--kill`
	- ソケットを強制的に閉じようとする
	- 重要：IPv4/IPv6 のみ。運用上は事故りやすいので、使う前に対象フィルタを必ず絞る

### ダンプ/フィルタファイル（調査・自動化向け）

- `-D FILE`, `--diag=FILE`
	- 表示せず、フィルタ適用後の TCP ソケット情報を raw で `FILE` にダンプ
	- `FILE` が `-` の場合は stdout
- `-F FILE`, `--filter=FILE`
	- `FILE` からフィルタを読み込む
	- `FILE` の各行は「コマンドライン引数1行」として解釈される
	- `FILE` が `-` の場合は stdin

### SELinux / cgroup / netns

- `-Z`, `--context`
	- `-p` 相当 + プロセス SELinux コンテキストも表示
- `-z`, `--contexts`
	- `-Z` + ソケットのコンテキストも表示
	- 注意：これは inode に紐づくラベルで、カーネルが保持する「実際の socket context」と一致しないことがある
- `--cgroup`
	- cgroup v2 のパスを表示
	- ここで出るパスは、階層のマウントポイントからの相対パス
- `-N NSNAME`, `--net=NSNAME`
	- 指定した network namespace に切り替えて表示

### 追加情報（ToS / sockopt / BPF）

- `--tos`
	- ToS / priority 情報を表示
	- 出力されうる項目：
		- `tos`：IPv4 ToS byte
		- `tclass`：IPv6 Traffic Class byte
		- `class_id`：net_cls cgroup の class id（0 の場合は `SO_PRIORITY` の優先度が出る）
- `--inet-sockopt`
	- inet ソケットオプション群を表示
- `-b`, `--bpf`
	- classic BPF フィルタを表示（管理者のみ取得できる情報）
- `--bpf-maps`
	- 各ソケットの BPF socket-local storage を表示
- `--bpf-map-id=MAP_ID`
	- 指定 map ID の BPF socket-local storage を表示（複数回指定可能）
- `--tipcinfo`
	- tipc ソケット内部情報を表示

## FILTER（フィルタ）

`ss` の運用価値の半分はフィルタです。
「大量のソケット」から「今見たいもの」だけを最短で抜くために使います。

### フィルタの形

```text
ss [options] [ state STATE-FILTER ] [ EXPRESSION ]
```

### STATE-FILTER（TCP 状態）

`state` の後ろに状態を指定します。

- 標準 TCP 状態
	- `established`, `syn-sent`, `syn-recv`, `fin-wait-1`, `fin-wait-2`, `time-wait`, `closed`, `close-wait`, `last-ack`, `listening`, `closing`
- まとめ指定
	- `all`：全部
	- `connected`：listening と closed 以外
	- `synchronized`：connected から syn-sent を除いたもの
	- `bucket`：`time-wait` と `syn-recv`（minisocket）
	- `big`：bucket の反対側
	- `bound-inactive`：bind 済みで inactive（LISTEN/CONNECT ではない）

例：

```bash
ss -tn state established
ss -tna state connected
ss -tln state listening
```

### EXPRESSION（条件式）

`EXPRESSION` は「述語（条件）」を `and/or/not` で繋いで書けます。
演算子の優先順位は `or` < `and` < `not` です。
また、述語が連続する場合は暗黙に `and` になります。

補足：演算子には別名（エイリアス）があります。

- `=` と等価：`=`, `==`, `eq`
- `!=` と等価：`!=`, `ne`, `neq`
- `>` と等価：`>`, `gt`
- `<` と等価：`<`, `lt`
- `>=` と等価：`>=`, `ge`, `geq`
- `<=` と等価：`<=`, `le`, `leq`
- `not` と等価：`!`, `not`
- `or` と等価：`|`, `||`, `or`
- `and` と等価：`&`, `&&`, `and`

#### 代表的な述語

- `{dst|src} [=] HOST`
  - 宛先/送信元のアドレス条件
- `{dport|sport} [OP] [FAMILY:]:PORT`
  - 宛先/送信元ポート条件
  - `OP` は `< <= = != >= >`（別名 `lt/le/eq/ne/ge/gt` 等も可）
- `dev [=|!=] DEVICE`
  - 使用インタフェース名 or index で条件
- `fwmark [=|!=] MASK`
  - fwmark 条件（`0x01/0x03` のようにビットマスク指定も可）
- `cgroup [=|!=] PATH`
  - cgroup パス条件
- `autobound`
  - source の port/path が自動割当のもの

#### 括弧とクォート（超重要）

シェルが `(` `)` を解釈しないよう、複雑な式は基本的に **シングルクォートで囲う**のが安全です。

```bash
ss -o state established '( dport = :ssh or sport = :ssh )'
```

### HOST 構文（[FAMILY:]ADDRESS[:PORT]）

`HOST` の一般形は次です。

```text
[FAMILY:]ADDRESS[:PORT]
```

- `FAMILY` は `-f` で指定できるファミリ（省略時は `-f` の値、なければ inet/inet6 のどちらか）
- `*` はアドレスやポートのワイルドカードに使える（ただし `unix` は例外あり）

補足：host 条件のファミリ混在

host 条件は、基本的に同じファミリで揃えるのが安全です。`inet` と `inet6` の混在は想定されていますが、
それ以外のファミリが混ざると結果が直感とズレることがあります。

#### inet/inet6

- `ADDRESS` は IP アドレス or DNS 名（解決できる必要あり）
- IPv6 は `:` が多いので、**`[ ]` で囲ってポートと区別**します

例：

```bash
ss -tn dst 192.0.2.10
ss -tn dst [2001:db8::1]
```

#### unix

- `ADDRESS` は glob パターン（`fnmatch(3)`）
- path 名と abstract 名の両方が対象
- `unix` は port 概念がなく、`*` を「アドレス/ポートのワイルドカード」としては使えません

例：

```bash
ss -x src /tmp/.X11-unix/*
```

#### link / netlink / vsock

- link：`ADDRESS` は Ethernet プロトコル名、`PORT` はデバイス名 or index
- netlink：`ADDRESS` は netlink ファミリ記述子、`PORT` は port id（通常 pid）。`kernel` で pid=0 を表せる
- vsock：`ADDRESS` は CID、`PORT` はポート

## 運用レシピ（コピペで使う）

### 1) ポート待受の確認（まず最初にやる）

```bash
# TCP/UDP の LISTEN（ポート番号、プロセス名）
ss -tulnp

# TCP だけ
ss -tlnp

# UDP だけ
ss -ulnp
```

見たいもの：

- そのポートが `LISTEN` しているか
- `users:(...)` に期待するプロセスが出るか
- `Local Address:Port` が `0.0.0.0:PORT`（全IF）なのか、特定IPに bind なのか

### 2) 特定ポートの接続状況を見る

```bash
# 例：SSH の接続だけ（入出両方向）
ss -tn state established '( dport = :ssh or sport = :ssh )'

# 例：HTTPS の待受だけ
ss -tln sport = :https
```

### 3) 特定ホスト（またはサブネット）との通信だけ見る

```bash
# 例：宛先が 193.233.7.0/24 で、http/https 関連の FIN-WAIT-1 をタイマー付きで
ss -o state fin-wait-1 '( sport = :http or sport = :https )' dst 193.233.7/24
```

### 4) TIME-WAIT が多い/繋がらない時の観測

```bash
# TIME-WAIT をタイマー付きで
ss -tno state time-wait

# SYN-SENT（こちらから SYN 出して返ってこない）
ss -tn state syn-sent

# SYN-RECV（SYN/ACK 返して ACK 待ち。SYN flood/経路の片方向不達などの手掛かり）
ss -tn state syn-recv
```

### 5) TCP 内部情報（rtt/cwnd/wscale/pacing 等）を覗く

`-i` は情報量が多く、まずは絞ってから使うのがコツです。

```bash
ss -tni state established

# 例：特定ポートに限定
ss -tni state established '( dport = :https or sport = :https )'
```

### 6) UNIX ドメインソケット（ローカルIPC）

```bash
# UNIX ソケット一覧（必要に応じて -a/-l を併用）
ss -x

# X11 っぽいものに接続中のプロセス
ss -x src /tmp/.X11-unix/*
```

### 7) SELinux コンテキスト込みで観測

```bash
# TCP/UDP の LISTEN をプロセスコンテキスト付きで
ss -tulnpZ

# ソケット inode のコンテキストも
ss -tulnpz
```

注意（man の注記）：`-z` の「socket context」は inode 由来であり、カーネルが保持する実際の socket context と
一致しないことがあります。ポリシー調査の“手掛かり”として使うのが安全です。

### 8) cgroup / netns で切り分け

```bash
# cgroup パス表示
ss --cgroup -tulnp

# netns を切り替えて見る（例：ns 名が分かっている場合）
ss -N myns -tulnp
```

## 事故りやすい点（短く）

- `-p` でプロセスが出ない
	- 自分のプロセス以外を見られない/制限されている可能性があります。root で再実行すると見えることがあります。
- フィルタの括弧が効かない
	- だいたいシェルが原因です。`'( ... )'` のようにシングルクォートで囲ってください。
- `-K` は最後の手段
	- 間違えると本番通信を落とします。まず `-K` なしで同じフィルタをかけ、対象が想定通りに絞れていることを確認してから。

## 参考（man の例）

```bash
# 全 TCP（LISTEN + 接続済み）
ss -t -a

# 全 TCP + SELinux コンテキスト
ss -t -a -Z

# 全 UDP
ss -u -a

# SSH の ESTAB
ss -o state established '( dport = :ssh or sport = :ssh )'

# X server に接続しているローカルプロセス
ss -x src /tmp/.X11-unix/*

# FIN-WAIT-1 のタイマー（apache->特定ネットワーク例）
ss -o state fin-wait-1 '( sport = :http or sport = :https )' dst 193.233.7/24

# TCP 以外を全部
ss -a -A 'all,!tcp'
```
