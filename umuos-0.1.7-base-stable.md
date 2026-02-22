---
title: "再現可能な自作OS UmuOS-0.1.7-base-stable"
---

# 再現可能な自作OS UmuOS-0.1.7-base-stable

UmuOS-0.1.7-base-stable は、研究用の「再現可能なベースOS」です。
目的は、便利に使うことよりも **起動・初期化・ネットワーク・ログ・認証（ログイン）の成立を観測できる** ことに寄せて固定することです。

- 起動：GRUB → Linux kernel → initramfs → ext4(rootfs) へ `switch_root`
- 観測：永続ログ `/logs/boot.log` に起動情報と NTP 同期の結果を残す
- ネットワーク：static 設定（telnet / FTP / NTP が成立する最小）
- リモート：BusyBox の `telnetd` / `tcpsvd` + `ftpd`
- シェル：標準は BusyBox ash（`/bin/sh`）。任意で外から ush を持ち込む運用も想定

注意：telnet/ftp は平文です。UmuOS は安全な運用を目的にしていないため、研究・観測用途（自宅LAN等）に限定ですね。

---

## 位置づけ

- UmuOS-0.1.7-base-stable は「途中で修正を挟まずに最後まで再現できる」ことを優先した、固定版のベース
- 機能拡張は次期 dev 系で行う方針（0.1.7-base-stable自体は極力固定）

---

## 特徴（固定していること）

- 起動の最小構成を固定（GRUB → kernel → initramfs → ext4 rootfs へ `switch_root`）
- 成果物（3点セット）を固定（ISO + disk.img + 起動スクリプト）
- 固定値を直書きできる範囲で固定（kernel/busybox バージョン、rootfs UUID、ゲストIP、TZ、NTP 等）
- 観測点を固定（永続ログ `/logs/boot.log` に起動情報と NTP の before/after を残す）
- リモート手段を固定（telnetd/FTP。SSH等はこの版のスコープ外）
- `/umu_bin` を PATH 最優先に固定（持ち込み/自作コマンドを置くための“逃げ道”）

このOSは「便利に拡張」ではなく「同じ条件で起動して同じ観測ができる」を優先てます。

---

## 全体像（ざっくりアーキテクチャ）

ホスト・ゲストの責務はこんな感じです。

- ホスト（例：RockyLinux 9.7）
  - QEMU/KVM を起動（起動スクリプトで起動）
  - ネットワークを使う場合は `br0` + `tap-umu` でゲストを L2 に参加させる
  - 起動ログを `host_qemu.console_*.log` として残す
- ゲスト（UmuOS-0.1.7-base-stable）
  - kernel: Linux 6.18.1
  - initramfs: rootfs（ext4 disk.img）を探して mount → `switch_root`
  - 永続 rootfs: ext4（`disk.img`）
  - ユーザーランド: BusyBox
  - `rcS` が mount / ログ / NW / NTP / telnetd / ftpd を初期化

---

## BusyBox を中心にした理由（設計判断）

UmuOS-0.1.7-base-stable ではユーザーランドの多くを BusyBox applet で揃えます。狙いは「便利」ではなく **起動・観測・再現性** です。

- 依存関係を最小化できる（rootfs が小さく、切り分けがしやすい）
- 成果物の中身が確認しやすい（`busybox --list` で収録コマンドを確認できる）
- rcS の初期化をシェルで直に追える（mount / ip / ntpd / telnetd / ftpd など）
- UmuOS の方針「開発環境は内包しない（外で作って必要なものだけ持ち込む）」に合う

一方で BusyBox だけでは「できないこと」も増えるので、足りない部分は無理に内側で増やさず、バイナリ持ち込み（例：`/umu_bin` 配置）で扱う前提にします。

---

## 固定値（このOSで固定している前提）

- Kernel: `6.18.1`
- BusyBox: `1.36.1`
- ISO: `UmuOS-0.1.7-base-stable-boot.iso`
  - kernel: `vmlinuz-6.18.1`
  - initramfs: `initrd.img-6.18.1`
- 永続ディスク: ext4 の `disk.img`（元はリポジトリ内 `disk/disk.img`）
  - rootfs UUID: `d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`
- ゲストIP（static）: `192.168.0.202/24`
  - GW: `192.168.0.1`
  - DNS: `8.8.8.8`, `8.8.4.4`
- タイムゾーン: `JST-9`
- NTP サーバ: `time.google.com`
- デバッグ用シリアル: ttyS1 をホスト `127.0.0.1:5555` に TCP 転送（デフォルト想定）
- ホストNW: `br0` + `tap-umu`（もしくは `NET_MODE=none`）

---

## ユーザー/認証（login と su）

- ログインは BusyBox の `getty` / `login` を利用します（ttyS0/ttyS1/telnetd）
- ユーザーは `root` / `tama（一般ユーザ）` を想定しています（`/etc/passwd` + `/etc/shadow`）
- `/umu_bin/su` は自作の最小 `su` で、SUID root（`4755`）として配置されます。

使い所：ログイン直後は一般ユーザーで入り、必要な操作だけ `su` で root へ上げて観測・切り分けしますが、いきなりrootログインも許可です。
一般ユーザーは、実装時に変更、もしくは実装後に`/etc/passwd` + `/etc/shadow` + `/etc/group`で追加できます。

---

## ユーティリティ（/umu_bin）

このOSは `/umu_bin` を PATH の先頭に置き、OS内部の「最小ツール」と、外から持ち込む自作バイナリの置き場を分けています。

代表例：

- `ll`（`ls -l` の薄いラッパ）
- `ntp_sync`（起動時に1回の時刻同期処理）
- `ftpd_start`（FTP サーバ起動処理。内部で `tcpsvd` + `ftpd`）
- `su`（自作。必要なときだけ root へ上げる）
**自作コマンド置き場です。/bin:/sbinよりも優先されるのでBusyboxコマンドと名前が被っても大丈夫です**

---

## 成果物（3点セット）と配置

起動に必要なのは次の3点のみです。

- ISO: `UmuOS-0.1.7-base-stable-boot.iso`
- 永続ディスク: `disk.img`　※一旦4GBで想定していますが、適時変更してください。
- 起動スクリプト: `UmuOS-0.1.7-base-stable_start.sh`

ポイント：リポジトリ内ではディスクが `disk/disk.img` にありますが、起動スクリプトは「同じディレクトリにある `disk.img`」を参照します。

---

## 起動（最短）

起動はスクリプトが担当します（例：3点セットを `/root` に置いた場合）。

```bash
cd /root
sudo ./UmuOS-0.1.7-base-stable_start.sh
```

ネットワーク無し起動（切り分け用）：

```bash
cd /root
sudo NET_MODE=none ./UmuOS-0.1.7-base-stable_start.sh
```

起動スクリプトの主な環境変数（起動前に上書き可能）：

- `NET_MODE=tap|none`（デフォルト `tap`）
- `BRIDGE=br0`（tap時に接続するブリッジ）
- `TAP_IF=tap-umu`（tapデバイス名）
- `TTYS1_PORT=5555`（ttyS1 の TCP ポート）

ホスト側ログ：

- 起動スクリプトは `host_qemu.console_YYYYmmdd_HHMMSS.log` を生成（1起動=1ファイル）

---

## コンソール（ttyS0 / ttyS1）

ログイン経路は大きく次の2つです。

- シリアル（ttyS0/ttyS1）: `getty` → `login`
- ネットワーク（telnetd）: telnet 接続 → `login`（セッションは `/dev/pts/*` にぶら下がる）

### ttyS0

- 起動スクリプトを実行したターミナルが、そのままコンソールになる（QEMU `-serial stdio`）

### ttyS1（TCP シリアル）

- 追加コンソール。ホストの `127.0.0.1:${TTYS1_PORT}` へ TCP 転送されます。

接続例：

```bash
telnet 127.0.0.1 5555
```

接続後に何も出ない場合は Enter を数回押す（表示タイミングの問題が多い）。

---

## ゲスト内での「成功判定（最小）」

最低限、次が成立していれば「起動・初期化・観測」が通ったと判断できます。

- rootfs が ext4 でマウントされている（例：`/dev/vda on / type ext4`）
- `/logs/boot.log` に `boot_id` と `[ntp_sync] before/after` が追記される
- `echo "$PATH"` の先頭が `/umu_bin`
- `date` が JST で出る（NTP が通れば概ね正しい時刻になる）
- telnet / FTP が起動している（例：`telnetd` プロセス、`/run/ftpd.pid`）

確認コマンド例：

```sh
echo "[whoami]"; whoami

echo "[PATH]"; echo "$PATH"

echo "[date]"; date -R 2>/dev/null || date

echo "[mount rootfs]"; mount | grep -E ' on / type ' || true

echo "[boot.log tail]"; tail -n 120 /logs/boot.log 2>/dev/null || true

echo "[telnetd]"; ps w | grep -E 'telnetd|\[telnetd\]' | grep -v grep || true

echo "[ftpd pid]"; ls -l /run/ftpd.pid 2>/dev/null || true
echo "[ftpd ps]"; ps w | grep -E 'tcpsvd|ftpd|\[tcpsvd\]|\[ftpd\]' | grep -v grep || true
```

---

## 受入（合格条件の目安）

「最低限ここまで到達すればOK」の目安です。

- 起動〜永続rootfs
  - `mount` で `/dev/vda on / type ext4` が見える
  - ログインできる（ttyS0）
  - 追加コンソール（ttyS1）にも入れる（ホストから `telnet 127.0.0.1 5555`）

- 観測（永続ログ）
  - `/logs/boot.log` が存在し、起動のたびに追記される
  - `boot_id` と `[ntp_sync] before/after` が残る

- ネットワーク/リモート（NET_MODE=tap のとき）
  - `ip a` で `192.168.0.202/24` が入る
  - `ip r` で default route が入る
  - LAN から `telnet 192.168.0.202 23` で `login:` が出る
  - LAN から `ftp 192.168.0.202` で接続できる（必要なら `ps` と `/run/ftpd.pid` で裏取り）

補足：`NET_MODE=none` は切り分け用です。この場合は `eth0` が出ないので、telnet/FTP/NTP は成立しません。

---

## rcS の初期化と観測点（切り分けの入口）

「rcS が走って、どこまで到達したか」をコマンド単位で観測できるよう、代表的な到達点を並べます。

### 1) proc/sys/dev/pts を用意

- 例：`mount -t proc` / `mount -t sysfs` / `mount -t devtmpfs` / `mount -t devpts`
- 観測：`mount` の出力に `/proc` `/sys` `/dev` `/dev/pts` が見える

### 2) 永続ログの土台

- 例：`mkdir -p /logs /run /var/run`、`/var/run/utmp` を初期化
- 観測：`/logs/boot.log` が作られ、起動のたびに追記される

### 3) 起動情報を記録

- 例：`boot_id` / `time` / `uptime` / `cmdline` / `mount` を `/logs/boot.log` へ追記
- 観測：`tail /logs/boot.log` で追記されていく

### 4) ネットワーク初期化（static）

- I/F：`/etc/umu/network.conf`
- 観測：`ip a` で `192.168.0.202/24`、`ip r` で default route

### 5) NTP 同期（起動時に1回）

- 例：`/umu_bin/ntp_sync`
- 観測：`/logs/boot.log` に `[ntp_sync] before/after` と実行ログ

### 6) telnetd 起動（LANログイン）

- 例：`telnetd -p 23 -l /bin/login`
- 観測：`ps` に `telnetd`。LANから接続すると `login:`

### 7) FTP 起動

- 例：`/umu_bin/ftpd_start`（内部で `tcpsvd` + `ftpd`）
- 観測：`/run/ftpd.pid` が生成される、`ps` に `tcpsvd/ftpd`

---

## ネットワーク（ゲスト）

ネットワーク設定は `/etc/umu/network.conf` を I/F にします。

主なキー：

- `IFNAME=eth0`
- `MODE=static`
- `IP=192.168.0.202/24`
- `GW=192.168.0.1`

補足：ホスト起動で `NET_MODE=none` の場合、ゲストに `eth0` が提供されないため、telnet/FTP/NTP は成立しません。

---

## リモート機能（telnet / FTP / NTP）

### telnetd（LAN からログイン）

ゲスト側で `telnetd -p 23 -l /bin/login` が起動します。

LAN内の別マシンからの例：

```bash
telnet 192.168.0.202 23
```

### FTP（ゲストがサーバ）

ゲスト側で BusyBox の `tcpsvd` + `ftpd` を使い、`0.0.0.0:21` で待ち受けます。

```bash
ftp 192.168.0.202
```

公開ルートは `/` です（全ディレクトリが見えます。実アクセスは権限に従います）。

### NTP（起動時に1回）

起動時に `/umu_bin/ntp_sync` が1回走り、前後の時刻と出力が `/logs/boot.log` に追記されます。

---

## シェル方針（ash と ush）

### 標準のシェル

UmuOS-0.1.7-base-stable の標準は BusyBox ash（`/bin/sh`）です。

- 互換性寄り / スクリプトの基準：`/bin/sh`

### ush（任意で持ち込む）

ush は標準同梱を前提にせず、必要なら「外で作って持ち込む」運用で使います。

使い分けの考え方：

- 対話操作は ush を基本（導入した場合）
- 本格スクリプト（分岐/ループ/引数処理/多段パイプ等）が必要なら `/bin/sh`（ash）へ逃がす

#### ush 0.0.4 のビルド例（開発ホスト）

```sh
cd /home/tama/umu_project/ush-0.0.4/ush  # 自分の環境のパスに合わせる

musl-gcc -static -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings \
  -Iinclude \
  -o ush \
  src/main.c src/utils.c src/env.c src/prompt.c src/lineedit.c \
  src/tokenize.c src/expand.c src/parse.c src/exec.c src/builtins.c
```

#### UmuOS へ持ち込む例（概念）

- `ush` バイナリをゲストへ転送（FTP等）し、書き込み可能な場所へ置く
- ゲスト側で root になり（`/umu_bin/su`）、`/umu_bin/ush` として配置する

```sh
su
cp /home/tama/ush /umu_bin/ush
chown root:root /umu_bin/ush
chmod 0755 /umu_bin/ush
```

---

## セキュリティ上の注意（重要）

- telnet/FTP は平文です。自宅LANなど閉域での観測用途に限定します。
- ホスト側のFW/SELinux設定で 23/TCP, 21/TCP が遮断されることがあります（疎通しない場合はホスト側も疑う）。

---

## ありがちな詰まりと切り分け

- `eth0` が出ない
  - `NET_MODE=none` で起動していないか確認
  - ホスト側で bridge/tap が用意されているか確認

- LAN から `192.168.0.202:23` / `:21` に繋がらない
  - ゲスト側で `ps` を見て `telnetd` / `tcpsvd` が起動しているか
  - ゲスト側で `ip a` / `ip r` を確認（IP/route）
  - ホスト側の FW/SELinux、ブリッジ配下到達性を確認

- NTP が失敗する
  - ゲスト側で `ping -c 1 8.8.8.8` など疎通確認
  - `/etc/resolv.conf` の DNS を確認

- `/logs/boot.log` が増えない
  - `/etc/inittab` の `::sysinit:/etc/init.d/rcS` 成立を疑う（rcSが走っていない）

---

## リンク

- [UmuOS リポジトリ（umu_project）](https://github.com/redzetm/umu_project)
- UmuOS-0.1.7-base-stable ディレクトリ：
  - [UmuOS-0.1.7-base-stable ディレクトリ](https://github.com/redzetm/umu_project/tree/main/UmuOS-0.1.7-base-stable)
- 0.1.7-base-stable docs
  - [機能一覧](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-%E6%A9%9F%E8%83%BD%E4%B8%80%E8%A6%A7.md)
  - [基本設計書](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-%E5%9F%BA%E6%9C%AC%E8%A8%AD%E8%A8%88%E6%9B%B8.md)
  - [詳細設計書](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-%E8%A9%B3%E7%B4%B0%E8%A8%AD%E8%A8%88%E6%9B%B8.md)
  - [実装ノート](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-%E5%AE%9F%E8%A3%85%E3%83%8E%E3%83%BC%E3%83%88.md)
  - [解説書](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-%E8%A7%A3%E8%AA%AC%E6%9B%B8.md)
