---
title: "UmuOS-0.1.7-base-stable: atable起動メカニズム（起動フローを表で追う）"
date: 2026-03-08
---

# UmuOS-0.1.7-base-stable: atable起動メカニズム（起動フローを表で追う）

このノートは UmuOS-0.1.7-base-stable の起動メカニズムを、**層（bootloader/kernel/initramfs/rootfs/userspace）ごとに切り分けて観測できる**形でまとめる。

ここで言う **atable** は「起動の各段階を *a table*（表）として並べ、入力/出力/観測点/失敗時の切り分けを同じ視点で追える」ための書き方を指す。

前提：UmuOS-0.1.7-base-stable のブートは、最小構成として次で固定されている。

- GRUB → Linux kernel → initramfs（/init）→ ext4(rootfs) を UUID で特定 → `switch_root` → BusyBox `init` → `inittab` → `rcS`

## 1. 起動の全体像（レイヤ分離）

UmuOS は研究/観測用OSで、起動失敗を「どの層の問題か」で切り分けられることを最優先にしている。

- **ホスト層（実行環境）**: QEMU/KVM、TAP/bridge、有効なコンソール接続経路
- **ブート層（ISO/GRUB）**: kernel/initrd のロード、kernel cmdline
- **カーネル層**: デバイス提供（virtio-blk/virtio-net）、devtmpfs、ext4
- **initramfs 層**: rootfs（disk.img）探索、ext4 UUID マッチ、`switch_root`
- **永続 rootfs 層（disk.img）**: `/sbin/init`、`/etc/inittab`、`/etc/init.d/rcS`
- **userspace 初期化層（rcS）**: mount/ログ/NW/NTP/telnetd/FTP

## 2. atable（起動フロー表）

### 2.1 ブート全段の atable

横長テーブルは画面幅によって横スクロールになりやすいので、ここでは **段階ごとに縦型**で並べる。

#### 段階 0: ホスト（QEMU）

| 項目 | 内容 |
|---|---|
| 主体（層） | ホスト（QEMU/KVM、TAP/bridge） |
| 入力 | ISO / disk.img / start.sh、（任意）TAP/bridge |
| 主要処理 | QEMU を起動しコンソールを提供し、disk を virtio-blk で接続 |
| 出力（次段へ） | ゲストが起動し、シリアルにログが流れる |
| 観測点（成功判定） | `ttyS0` に GRUB/カーネルログが見える |
| 失敗時の切り分け | まずホスト側の `NET_MODE` / bridge / tap を疑う。`NET_MODE=none` で進むならネット周りが原因。 |

この段階（ホストQEMU起動）で扱うファイルは「起動の3点セット + ホスト側ログ」です。

**① ファイル一覧とその用途（段階0で使うもの）**

- `UmuOS-0.1.7-base-stable-boot.iso`
	- 用途：ゲストが最初に読むブート媒体。
	- 中身：GRUB 設定（`grub.cfg`）と kernel（`vmlinuz-6.18.1`）と initramfs（`initrd.img-6.18.1`）。
	- 中身（役割の分担）：
		- `grub.cfg`
			- GRUB のメニュー定義。
			- どの kernel / initrd をロードするかを決める。
			- kernel cmdline（例：`root=UUID=...`、`console=ttyS0,...`、`net.ifnames=0`）を渡す＝起動条件の固定点。
		- `vmlinuz-6.18.1`
			- Linux kernel 本体。
			- デバイス初期化（virtio-blk/virtio-net 等）と、initramfs の `/init` を `pid=1` として起動する責務を持つ。
			- virtio-blk / virtio-net とは（UmuOS文脈の要点）：
				- virtio は「仮想マシン向けの準仮想化デバイス」。ゲスト（Linux）が virtio ドライバで“素直に扱える”形で、QEMU がデバイスを提供する。
				- **virtio-blk（仮想ディスク）**：disk.img がブロックデバイスとして見える。UmuOS では典型的に `/dev/vda` として現れ、initramfs `/init` がこれを走査して ext4 UUID を読む。
					- 失敗の見え方：virtio-blk が無い/認識されないと、rootfs デバイスが見つからず initramfs で止まりやすい。
				- **virtio-net（仮想NIC）**：ゲストに NIC を提供する。UmuOS では典型的に `eth0` として現れ、rcS が `/etc/umu/network.conf` を元に `ip addr` 等で設定する。
					- 失敗の見え方：起動自体は進んでも、`NET_MODE=none` や virtio-net 未提供だと `eth0` が出ず、telnet/FTP/NTP が成立しない（観測用途の機能が死ぬ）。
		- `initrd.img-6.18.1`
			- initramfs イメージ。
			- UmuOS では `/init` が C 実装で、`root=UUID=...` を手がかりに ext4(rootfs) を探して mount し、`switch_root` で永続 rootfs へ移行する。
	- この段階での見え方：QEMU には「CD-ROM として渡す 1 ファイル」。

- `disk.img`
	- 用途：永続 rootfs（ext4）。起動後の `/` になる。
	- この段階での見え方：QEMU には「virtio-blk のディスクとして渡す 1 ファイル」。
	- 補足：リポジトリ内では `disk/disk.img` として管理される想定だが、実行環境へ持ち込むときは起動スクリプトと同じ場所に `disk.img` という名前で置く運用を前提にしている。

- `UmuOS-0.1.7-base-stable_start.sh`
	- 用途：ホスト側で QEMU を “同じ条件” で起動するための固定スクリプト。
	- 役割：ISO と disk.img の場所を解決し、`NET_MODE=tap|none` 等の環境変数に応じて QEMU の起動引数（シリアル/ネット/ドライブ）を組み立てて起動する。
	- 重要：ここがブレると「ゲストの問題」に見えて実はホスト起動引数の問題、が起きやすい。

- `host_qemu.console_YYYYmmdd_HHMMSS.log`（起動のたびに生成される）
	- 用途：ホスト側で採取するコンソールログ（再現/差分観測のための一次資料）。
	- 生成者：起動スクリプトが `script` コマンド等を使って生成する想定。
	- 使い所：起動が失敗したときでも、ゲストの出力がファイルとして残る（＝再現性の確保）。

#### 段階 1: GRUB（ISO）

| 項目 | 内容 |
|---|---|
| 主体（層） | GRUB（ISO） |
| 入力 | `grub.cfg`、kernel + initrd |
| 主要処理 | kernel を指定 cmdline で起動し initrd をロード |
| 出力（次段へ） | kernel が initramfs を展開して起動 |
| 観測点（成功判定） | 早期カーネルログが出る／`/proc/cmdline` に `root=UUID=...` が入る |
| 失敗時の切り分け | `grub.cfg` のファイル名/パス違い、kernel/initrd 未配置を疑う。 |

#### 段階 2: kernel

| 項目 | 内容 |
|---|---|
| 主体（層） | kernel |
| 入力 | kernel config、cmdline |
| 主要処理 | デバイス初期化（virtio 等）、initrd の `/init` を `pid=1` として実行 |
| 出力（次段へ） | initramfs が動作開始 |
| 観測点（成功判定） | `console=ttyS0,115200n8` でログが見える |
| 失敗時の切り分け | `/dev` が出ない場合は `DEVTMPFS` 等のカーネル設定を疑う。 |

#### 段階 3: initramfs `/init`

| 項目 | 内容 |
|---|---|
| 主体（層） | initramfs `/init` |
| 入力 | `/proc/cmdline` の `root=UUID=...`、`/dev/*` ブロックデバイス |
| 主要処理 | `root UUID` 抽出 → `/dev` 走査 → ext4 superblock の UUID で一致デバイスを探す |
| 出力（次段へ） | `mount(dev, /newroot, ext4)` が成功する |
| 観測点（成功判定） | initramfs ログに `cmdline parsed: root=UUID=...` と `matched: dev=/dev/vdX` が出る |
| 失敗時の切り分け | `root=UUID` 欠落/誤り、disk 未接続、ext4 でない、UUID 不一致。 |

#### 段階 4: initramfs `/init`（switch_root）

| 項目 | 内容 |
|---|---|
| 主体（層） | initramfs `/init` |
| 入力 | `/newroot` に mount 済み |
| 主要処理 | `exec /bin/switch_root /newroot /sbin/init` |
| 出力（次段へ） | 永続 rootfs の init へ遷移 |
| 観測点（成功判定） | initramfs ログが `switching root` で終わり、以降は rootfs 側へ |
| 失敗時の切り分け | `switch_root` が無い（BusyBox applet未有効）／`/newroot/sbin/init` 不在を疑う。 |

#### 段階 5: rootfs BusyBox `init`

| 項目 | 内容 |
|---|---|
| 主体（層） | rootfs BusyBox `init` |
| 入力 | `/sbin/init`（BusyBox）、`/etc/inittab` |
| 主要処理 | `::sysinit:/etc/init.d/rcS` 実行、`getty` を respawn |
| 出力（次段へ） | rcS 実行 + ttyS0/ttyS1 ログイン経路を用意 |
| 観測点（成功判定） | ログインプロンプトが出る、または `rcS done` が見える |
| 失敗時の切り分け | `inittab` の誤り、`rcS` 実行権限、rootfs が ro になっていないか。 |

#### 段階 6: rootfs `rcS`

| 項目 | 内容 |
|---|---|
| 主体（層） | rootfs `rcS` |
| 入力 | `/etc/umu/network.conf`、`/etc/profile`、`/umu_bin/*` |
| 主要処理 | mount/ログ/NW/NTP/telnetd/FTP を初期化 |
| 出力（次段へ） | 観測可能状態（ログ・NW・リモート） |
| 観測点（成功判定） | `/logs/boot.log` が追記される／NTP before/after が残る／telnet/FTP が起動 |
| 失敗時の切り分け | `boot.log` が伸びない→ rcS まで来てない。NWだけ死ぬ→ host tap/bridge or `network.conf` を疑う。 |

### 2.2 重要な設計判断（atableの読み方）

起動を表で追うときのポイントは次の3つ。

1) **境界（どこで責務が切り替わるか）を先に覚える**
	- `switch_root` が境界（initramfs → 永続 rootfs）
	- `rcS` が境界（最低限のOS成立 → 観測/ネット/ログの成立）
2) **観測点は“層を確定する”ために置く**
	- 例：`/logs/boot.log` に追記があるなら「rootfs の rcS が動いている」と言える
3) **失敗時は、表の一段前に戻って“入力が揃っているか”を見る**
	- 例：`root=UUID=...` が `cmdline` に無いなら initramfs の責務ではなく GRUB 側の責務

## 3. initramfs `/init` のメカニズム（UUIDでrootfsを確定）

UmuOS-0.1.7-base-stable の initramfs `/init` は C 実装で、次の方針を持つ。

- udev や `/dev/disk/by-uuid` に依存しない（研究・観測でブラックボックスを減らす）
- `/proc/cmdline` の `root=UUID=...` を「唯一の入力」として rootfs を確定する
- `/dev` を走査して ext4 superblock の UUID を読み、一致するブロックデバイスを選ぶ
- 失敗理由を必ずログに出す（観測性優先）

### 3.1 アルゴリズム（要約）

1. `/proc`, `/sys`, `/dev`, `/dev/pts` を mount（最低限の観測土台）
2. `/proc/cmdline` から `root=UUID=...` をパースし、16byte UUID へ変換
3. `/dev` を走査して候補デバイス名（例：`vd*`, `sd*`, `nvme*`）を見つける
4. 候補ごとに ext4 superblock（offset 1024）の magic と UUID を読んで一致判定
5. 一致したデバイスを `/newroot` へ ext4 mount（rw）
6. `execv("/bin/switch_root", ["switch_root", "/newroot", "/sbin/init"])`
7. 失敗時は一定回数リトライし、最終的に “候補の UUID ダンプ” を出して緊急ループ

### 3.2 代表ログ（何が見えればどこまで進んだか）

- `[/proc/cmdline: ... root=UUID=... ]` → GRUB の引数が initramfs に届いている
- `cmdline parsed: root=UUID=...` → UUID 文字列が正しく解釈できた
- `scan: /dev/vda` が繰り返される → デバイス走査中（virtio-blk が出るまで待っている可能性）
- `matched: dev=/dev/vda uuid=...` → rootfs デバイス確定
- `mount root ok (rw): /dev/vda` → rootfs mount 成功
- `switching root` → switch_root 直前

※ initramfs のログは基本 stderr に集約される設計（コンソールへ二重に出ることがある）。

## 4. rootfs（disk.img）側の init → inittab → rcS

`switch_root` の後は **disk.img 側の内容**が “毎回の起動挙動” を決める。

### 4.1 BusyBox `init` と `inittab`

- `/sbin/init` は BusyBox（`/bin/busybox` への symlink）
- `/etc/inittab` により次が成立する
  - `::sysinit:/etc/init.d/rcS`
  - `ttyS0` / `ttyS1` で `getty` を respawn（シリアルログインを提供）

この段階の観測点：

- `ttyS0` でログインプロンプトが出る
- `ttyS1` へ接続してログインできる（ホスト側 TCP シリアル経由）

### 4.2 `rcS` の責務（初期化を一箇所に固定）

UmuOS-0.1.7-base-stable では `rcS` を “唯一の正” としてテンプレ1本から配置する方針。
理由は rcS が分岐/二重管理になると観測が壊れるため。

`rcS` の主要処理は次。

1) `proc/sys/devtmpfs/devpts` を mount
2) `/logs`, `/run`, `/var/run`, `/umu_bin` の準備、`/var/run/utmp` 初期化
3) `/logs/boot.log` に起動情報（boot_id/time/uptime/cmdline/mount）を追記
4) `/etc/umu/network.conf` を読み static IP を設定（`ip`）
5) `/umu_bin/ntp_sync` を1回実行し、before/after と出力を `boot.log` へ
6) `telnetd` と `/umu_bin/ftpd_start` を起動
7) 目印として `echo "[rcS] rcS done" > /dev/console`

## 5. 観測点（最小セット）

起動が成功したかは、次の “物” と “ログ” を見るのが最短。

### 5.1 rootfs が永続 ext4 であること

ゲスト内で `mount` を見て、`/dev/vda on / type ext4` 相当が確認できること。

### 5.2 `/logs/boot.log` が起動のたびに追記されること

`tail -n 80 /logs/boot.log` で、少なくとも次が含まれること。

- `boot_id`
- `cmdline`
- `[ntp_sync] before:` と `after:`

### 5.3 PATH/TZ が固定されていること

- `echo "$PATH"` の先頭が `/umu_bin`
- `date` が `JST` 表示（`TZ=JST-9`）

## 6. 失敗時の切り分け（表に戻る）

### 6.1 画面が真っ黒 / 何も出ない

- ホスト側：`-serial stdio` でコンソールが出ているか
- ISO / disk.img のパスが起動スクリプトで合っているか

### 6.2 initramfs で止まる（rootfs が見つからない）

- `root=UUID=...` が `/proc/cmdline` に入っているか
- disk.img の UUID が一致しているか（設計固定値）
- virtio-blk が提供されているか（`-drive if=virtio`）

### 6.3 `switch_root` 後に rcS が走らない

- rootfs 側の `/sbin/init` が BusyBox symlink になっているか
- `/etc/inittab` の `::sysinit:/etc/init.d/rcS` が正しいか
- `/etc/init.d/rcS` に実行権限があるか

### 6.4 ネットワークだけ死ぬ（起動はする）

- `NET_MODE=none` で起動していないか（その場合 `eth0` 自体が提供されない）
- ホスト側 bridge/TAP が成立しているか
- ゲスト側 `/etc/umu/network.conf` の `MODE=static` と `IP/GW` が揃っているか

## 7. 参考（固定値の一覧）

UmuOS-0.1.7-base-stable で固定している代表値。

- Kernel: `6.18.1`
- BusyBox: `1.36.1`
- rootfs UUID: `d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`
- ゲストIP（static）: `192.168.0.202/24`
- GW: `192.168.0.1`
- DNS: `8.8.8.8`, `8.8.4.4`
- TZ: `JST-9`
- NTP: `time.google.com`

