---
title: "ip(8) ネットワーク操作（addr/link/route/neigh/netns）実用ノート（Rocky Linux 9.7）"
date: 2026-03-22
---

# ip(8) — ネットワーク（経路/IF/アドレス/近隣/namespace）を表示・操作

`ip`（iproute2）は、Linux のネットワーク状態を表示し、操作するための中核コマンドです。

基本の形は **「OBJECT（何を） + COMMAND（どうする）」** です。

- `addr(ess)` : IPアドレス
- `link` : NIC（リンク）
- `route` : ルート
- `neigh(bour)` : 近隣（ARP/NDISC）
- `netns` : network namespace
- `monitor` : 監視

重要：`ip` での変更は **基本的にランタイム**です。
Rocky Linux では NetworkManager が有効なことが多く、恒久反映は `nmcli` 側の接続プロファイルで行うのが安全です。
（`ip` は「いまの状態観測」「一時的な切り分け」に強い）

## SYNOPSIS（書式：manより）

```text
ip [ OPTIONS ] OBJECT { COMMAND | help }

ip [ -force ] -batch filename
```

- `OBJECT` は `address|link|route|neighbor|netns|monitor|...`（多数）
- `OBJECT` 名は省略形OK（例: `address` → `addr` → `a`）

重要：**OPTIONS は OBJECT より前**に置きます。

```text
# OK（OPTIONS→OBJECT）
ip -br addr

# NG（OBJECT の後ろに OPTIONS を置くと、OBJECT側の引数として解釈されがち）
ip addr -br
```

## まず結論：よく使うコマンド

```bash
# まずはこれ（最頻出）：addr の省略形 a（= ip addr）
ip a

# 見やすい簡易表示（manの -br/-brief）
ip -br link
ip -br addr
ip -br neigh

# IPv4/IPv6 を分けて混乱を減らす（manの -4/-6）
ip -4 addr
ip -6 addr
ip -4 route
ip -6 route

# 1行出力（manの -o/-oneline）
ip -o addr show dev eth0

# 詳細/統計（manの -d/-details, -s/-stats）
ip -d link show dev eth0
ip -s link show dev eth0
ip -s -s link show dev eth0

# JSON（manの -j/-json, -p/-pretty）
ip -j addr show | less
ip -j -p addr show | less

# 監視（manの monitor と -t/-ts）
ip monitor
ip -t monitor
ip -ts monitor

# namespace 内で実行（manの -n/-netns）
ip netns list
ip netns exec ns1 ip -br addr
ip -n ns1 -br addr
```

## OPTIONS（manより：よく使うもの）

### 表示・整形

- `-br`, `-brief`: 表形式の簡易表示（`ip addr show`/`ip link show`/`ip neigh show` で対応）
- `-o`, `-oneline`: 1レコード1行（改行を `\` に置換。`wc`/`grep` に便利）
- `-j`, `-json`: JSON
- `-p`, `-pretty`: JSONを見やすく
- `-s`, `-stats`, `-statistics`: 情報を増やす（複数回で増える）
- `-d`, `-details`: 詳細表示
- `-h`, `-human-readable`: 統計値を読みやすく
- `-r`, `-resolve`: 名前解決して表示（遅くなることあり）
- `-N`, `-Numeric`: 数値をそのまま表示（変換しない）

### 対象の絞り込み（ファミリ）

- `-f`, `-family { inet | inet6 | link | ... }`
- `-4`: `-family inet`
- `-6`: `-family inet6`
- `-B`: `-family bridge`
- `-M`: `-family mpls`
- `-0`: `-family link`

### その他（manより）

- `-V`, `-Version`: バージョン表示
- `-n`, `-netns NETNS`: 指定netnsで実行（`ip netns exec` の省略）
- `-a`, `-all`: **対応する COMMAND のときだけ**「全オブジェクトに対して」実行
- `-c[color][={always|auto|never}]`: 色（JSON時は無効）
- `-t`, `-timestamp` / `-ts`, `-tshort`: monitorに時刻を付ける
- `-rc`, `-rcvbuf SIZE`: netlink受信バッファ（既定 1MB）
- `-iec`: IEC単位で人間向けレート（例: 1Ki=1024）
- `-l`, `-loops COUNT`: `ip address flush` の最大試行回数（0は全部消えるまで）
- `-echo`: 適用設定をカーネルが送り返すよう要求

#### `-a` / `-all` が「効かない」典型パターン

`-a` は万能スイッチではなく、**その COMMAND が対応している場合にだけ**意味があります。
特に `addr/link/route/neigh` の「表示」は、そもそもデフォルトで全対象が出るので、`-a` を付ける必要がありません。

まずはこれでOK：

```bash
# すでに全IFが対象（-a不要）
ip addr
ip link
ip route
ip neigh
```

`-all` が活きる代表例（netnsの全走査）：

```bash
# すべての netns で同じコマンドを実行
sudo ip -all netns exec ip -br addr
```

## OBJECT（manより：代表）

添付の man にある OBJECT を「何者か」だけ短く整理します。

- `address`: デバイス上のIPアドレス
- `link`: ネットワークデバイス
- `route`: ルーティング
- `neighbor` / `neighbour`: ARP/NDISC 近隣キャッシュ
- `netns`: network namespace
- `monitor`: netlinkメッセージ監視
- その他: `rule`, `tunnel`, `vrf`, `xfrm`, `mptcp`, ...（必要になったら `ip OBJECT help` が早い）

## 典型タスク（運用の切り分け順）

### 0) まずはこれ（全体像）

```bash
ip -br link
ip -br addr
ip route
ip neigh
```

「疎通しない」を切る基本観測：

1. **link** が UP か（物理/仮想IF）
2. **addr** が付いているか（IPv4/IPv6）
3. **route** があるか（特に default）
4. **neigh** が解決しているか（ARP/ND）

### 1) アドレス：`ip addr`

```bash
ip a
ip addr
ip addr show dev eth0
ip -4 addr show dev eth0
ip -6 addr show dev eth0
```

追加/削除（ランタイム変更）：

```bash
sudo ip addr add 192.0.2.10/24 dev eth0
sudo ip addr del 192.0.2.10/24 dev eth0
```

flush（消しすぎ注意）：

```bash
sudo ip addr flush dev eth0
sudo ip -l 20 addr flush dev eth0
```

### 2) リンク：`ip link`

```bash
ip link
ip link show dev eth0
ip -d link show dev eth0
ip -s link show dev eth0
```

UP/DOWN（リモートでは自爆注意）：

```bash
sudo ip link set dev eth0 up
sudo ip link set dev eth0 down
```

### 3) ルート：`ip route`

```bash
ip route
ip -4 route
ip -6 route

# 宛先へどう出るか（調査で強い）
ip route get 8.8.8.8
```

追加/置換（例）：

```bash
sudo ip route add default via 192.0.2.1 dev eth0
sudo ip route replace default via 192.0.2.1 dev eth0
```

### 4) 近隣：`ip neigh`

```bash
ip neigh
ip neigh show dev eth0
ip -br neigh
```

キャッシュ操作（影響に注意）：

```bash
sudo ip neigh del 192.0.2.1 dev eth0
sudo ip neigh flush dev eth0
```

### 5) namespace：`ip netns`

```bash
ip netns list
ip netns exec ns1 ip -br addr
ip -n ns1 -br addr
```

### 6) 監視：`ip monitor`

```bash
ip monitor
ip -t monitor
ip -ts monitor
```

## バッチ：`-batch` と `-force`（manより）

```bash
# ファイル（または標準入力）から一連のコマンドを実行
sudo ip -batch ip_batch.txt

# エラーがあっても止めずに継続
sudo ip -force -batch ip_batch.txt
```

- `-batch`: 最初の失敗で終了
- `-force`: エラーでも継続（ただし戻り値は非0になり得る）

## EXIT STATUS（manより）

- `0`: 成功
- `1`: 構文エラー
- `2`: カーネルがエラーを返した

## 事故りポイント（重要）

- **リモートで `ip link set ... down` / `ip addr flush` して切断**
  - 作業前に `ip -br link` / `ip -br addr` / `ip route` を控える
  - 代替経路（コンソール/別NIC/別セッション）確保
- **永続化のつもりがランタイムだけで消える/NetworkManagerに上書きされる**
  - 恒久対応は `nmcli` の接続設定へ
- **IPv4/IPv6 を混ぜて見て混乱**
  - `ip -4 ...` / `ip -6 ...` で分ける
- **`-resolve` で遅くなる/調査がDNS依存になる**
  - 調査では `-r` を付けないのが無難な場面も多い
- **出力が長くて見づらい**
  - `-br`、`-o`、`-j -p`、`| less -S` を使い分ける

## manにある EXAMPLES（抜粋）

```text
ip addr     Shows addresses assigned to all network interfaces.
ip neigh    Shows the current neighbour table in kernel.
ip link set x up   Bring up interface x.
ip link set x down Bring down interface x.
ip route    Show table routes.
```
