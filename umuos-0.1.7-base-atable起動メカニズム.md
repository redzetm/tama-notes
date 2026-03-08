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

## 1.1 用語と観測経路の定義（シリアル/コンソール/tty/telnet）

「徹底解説」として重要なので、UmuOS の起動観測で混乱しやすい用語を先に定義します。
ポイントは **“どの層が、どの経路に文字を出しているか”** を混同しないことです。

### 1.1.1 console と serial の違い（超要約）

- **serial（シリアル）**：仮想的な“シリアルポート”経由の入出力。
	- ゲスト側では典型的に `ttyS0`, `ttyS1` として見える。
	- ホスト側では QEMU の `-serial ...` で「どこに接続するか」を決める（例：標準入出力、TCP、ファイル）。

- **console（ここでは“画面コンソール”）**：VGA/テキスト画面側の入出力。
	- ゲスト側では典型的に `tty0`（画面側の仮想コンソール）として見える。
	- ホストが GUI で QEMU 画面を出している場合に目視できる。

GRUB も kernel も「出力先（console/serial）」を設定できますが、
**GRUB の設定**と**kernel の `console=...`**は別物なので、段階を跨いで混同しないのがコツです。

### 1.1.2 `ttyS0` / `ttyS1` / `tty0` とは

- `ttyS0`, `ttyS1`
	- Linux が提供する“シリアル端末”デバイス。
	- `console=ttyS0,115200n8` のように kernel cmdline で指定すると、**kernel のログ（と一部のユーザーランド出力）がそこに流れる**。
	- rootfs 側では `getty` を `ttyS0` や `ttyS1` に立てることで、シリアルログイン経路になる。

- `tty0`
	- 画面側の“現在アクティブな仮想コンソール”を表す。
	- `console=tty0` を入れると、kernel ログを画面側にも出せる（シリアルと併用も可）。

### 1.1.3 `/dev/console` とは（なぜここに出すのか）

- `/dev/console` は「その時点で kernel が“コンソール”として選んでいる出力先」に繋がる特別なデバイス。
	- どこに繋がるかは `console=...` の指定順などに依存する。
	- UmuOS の `rcS` が `echo ... > /dev/console` のように目印を出すのは、
		- **“人間が見ている経路（たとえば `ttyS0`）に確実に届く”**
		- **“rcS まで到達した”という段階判定ができる**
		ため。

### 1.1.4 telnet は「コンソール」ではない（ポートとログが分かれる理由）

- telnet は **ネットワーク越しのリモートログイン**で、入出力は `ttyS0` ではなく「telnet のセッション（擬似端末）」に繋がる。
	- そのため、`host_qemu.console_*.log` のような“シリアル/画面のログ採取”とは別系統になりやすい。

- ポート
	- 典型的には **23/TCP**（ただし BusyBox の起動方法や設定によって変更され得る）。
	- ホスト側から接続するときは、QEMU の usernet を使っている場合 `hostfwd=tcp:127.0.0.1:<host-port>-:23` のように
		**ホストの任意ポート**へ転送される構成がよくある（どのポートかは起動オプション次第）。
	- UmuOS の実際の待受ポートは、起動後に `netstat -tlnp`（環境により `ss -tlnp`）等で観測して確定するのが確実。

### 1.1.5 観測経路の“地図”（どのログが何を拾うか）

- ホストで採取する `host_qemu.console_*.log`
	- 基本的に「ホストが接続している QEMU コンソール（多くはシリアル）」に流れた文字を保存する。
	- telnet セッションの入出力は別物なので、**telnet のログイン操作やコマンド入力がここに残るとは限らない**。

- ゲスト内の `/logs/boot.log`
	- rcS が明示的に追記する“永続ログ”。ネット経由ログインかどうかに関係なく、rcS が動いていれば残る。

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
	- SWAP について（UmuOS の前提）
		- UmuOS の基本構成は **SWAP 領域（swap パーティション / swapfile）を設定しない**前提で、起動フロー上も（initramfs `/init` や `rcS` に）SWAP を有効化する処理が登場しない。
		- まず用語の整理
			- **メモリ（RAM）**：CPU が直接アクセスできる主記憶。プロセスの実行・カーネル・ページキャッシュ等に使われる。
			- **SWAP**：RAM が足りないときに、メモリページを退避させるための「ディスク上の領域」。swap パーティションまたは swapfile で提供される。
			- 注意：SWAP は「速いメモリの代わり」ではなく、足りないときの“延命”であり、I/O 依存（遅くなりやすい）。
		- ねらい（UmuOS で SWAP を置かない理由）
			- 観測/再現性を高めるために「メモリ不足時の挙動」を I/O（ホストのストレージ性能、キャッシュ、バックエンド）に依存させない。
			- 構成を単純に保つ（追加パーティション・追加ディスク・永続領域の設計を増やさない）。
		- メモリは“物理サーバ全体”を使うのか？（ゲスト/ホストの関係）
			- ゲスト（UmuOS）から見える RAM 容量は **QEMU が割り当てた分**で固定される（例：QEMU の `-m` / `-machine` 設定相当）。
			- ホスト（物理サーバ）は、その割り当て分のメモリを **ホストの物理RAMから裏で確保してゲストに提供**するが、「ゲストがホストの RAM 全体を無制限に使う」わけではない。
			- 切り分け上は「ゲストのメモリ不足」か「ホスト側の過負荷（他プロセス含む）」かを分けて考えるのが重要。
		- 影響（SWAP なしでメモリが足りなくなると何が起きるか）
			- メモリが不足すると、ゲスト内で **OOM Killer** が動作してプロセスが kill される（最悪、重要プロセスが落ちて起動後の観測機能が不安定になる）。
			- 研究用途では「足りないなら QEMU のメモリ割当を増やす」で切り分けがしやすい（I/O による遅延や揺れを避けられる）。
		- 観測/確認ポイント（ゲスト内）
			- SWAP が無いこと：`cat /proc/swaps` が空、または `swapon --show` が空。
			- メモリ総量と逼迫の兆候：`free -m`、`cat /proc/meminfo`。
			- OOM の痕跡：`dmesg` に `Out of memory` / `Killed process` が残ることがある。
		- もし SWAP が必要なら（追加する方法）
			- `disk.img` 内に swapfile を作って `mkswap`/`swapon` する（永続 rootfs 上で完結する）。
			- もしくは SWAP 専用の追加ディスク（例：`/dev/vdb`）を virtio-blk として別途渡す（ディスク分離で観測しやすい）。

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

この段階の要（固定点）は、突き詰めると **`grub.cfg` が kernel cmdline をどう固定しているか**です。
UmuOS-0.1.7-base-stable の `grub.cfg` 実例（ビルド元：`iso_root/boot/grub/grub.cfg`）を、読み下しながら“どこに効いているか”を徹底解説します。

### 2.1.0 kernel cmdline とは（UmuOS で重要な理由）

**kernel cmdline**（kernel command line）は、GRUB が kernel を起動するときに渡す「引数の文字列」です。
この 1 行（正確には 1 つの文字列）が、起動条件のかなりの部分を固定します。

- 形式
	- 基本は **スペース区切り**で、トークンの並びになる。
		- `KEY=VALUE` 形式（例：`root=UUID=...`）
		- フラグ形式（例：`rw`）
		- 同じキーを複数回指定（例：`console=... console=...`）
	- `grub.cfg` では見やすさのために行末 `\` で複数行に分けて書けるが、実際に kernel に渡るのは「空白で連結された 1 本の cmdline」として扱う。

- 誰が読むか（2 系統ある）
	- **kernel 自身**が読む：例として `console=...`、`loglevel=...`、`panic=...` など。早期ログの見え方・デバイス初期化の挙動に直結する。
	- **ユーザーランド（initramfs や rootfs の init）**が読む：`/proc/cmdline` という形で参照できるため、initramfs `/init` が `root=...` を読んで rootfs を確定するといった“設計の入力”に使える。

- どこで観測できるか
	- 最短はゲスト内の `cat /proc/cmdline`。
	- initramfs `/init` のログが `cmdline parsed: ...` のように出す場合、段階 3 の観測点としても使える（GRUB→kernel→initramfs へ引数が届いていることの証明）。

- 壊れ方の典型（切り分けに効く）
	- `root=UUID=...` が無い/違う → initramfs `/init` が rootfs を確定できず止まる（段階 3 の問題に見えるが、原因は段階 1 の固定点）。
	- `console=ttyS0,...` が無い/速度が違う → ゲストは動いていてもホストの観測点（シリアル）に出ず「何も起きてない」ように見える。
	- `net.ifnames=0 biosdevname=0` が無い → NIC 名がブレて `eth0` 前提の初期化が外れ、起動はしてもネットだけ死ぬ。

### 2.1.1 `grub.cfg`（実例）

```cfg
set timeout=20
set default=0

serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console

menuentry "UmuOS-0.1.7-base-stable" {
insmod gzio

linux /boot/vmlinuz-6.18.1 \
root=UUID=d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 \
rw \
console=tty0 console=ttyS0,115200n8 \
loglevel=7 \
panic=-1 \
net.ifnames=0 biosdevname=0

initrd /boot/initrd.img-6.18.1
}
```

### 2.1.2 `grub.cfg` の読みどころ（行ごと）

- `set timeout=20`
	- GRUB メニューの待ち時間（秒）。研究/観測用途では「手で止めて確認できる」余地を残す意味がある。

- `set default=0`
	- デフォルト起動するメニューエントリの index（0 始まり）。
	- ここがズレると、別エントリ（別 kernel/cmdline）を起動して「同じ条件で起動しているはずなのに挙動が違う」が起こる。

- `serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1`
	- GRUB 自身が「シリアルに文字を出す/シリアルからキー入力を受ける」ための初期設定。
	- `--unit=0` は“1本目のシリアル”（PC で言う COM1 相当）を指す。
	- `--speed=115200 ...` は通信条件（115200 8N1）。ここがホスト側の受け方（QEMU の `-serial ...` の接続先や端末側設定）とズレると、次が起きやすい。
		- 文字化けする
		- 何も表示されない（=「真っ黒」に見える）
	- 注意：これは **GRUB の入出力**の話（段階 1 の話）で、「kernel の `console=ttyS0,115200n8`」（段階 2 の話）とは別物。
		- 用語の整理は 1.1 を参照。

- `terminal_input serial console` / `terminal_output serial console`
	- GRUB の入出力先を「シリアル」と「画面（VGA/コンソール）」の **両方**にする設定。
		- `terminal_output ...`：GRUB が出す文字が、シリアルにも画面にも出る
		- `terminal_input ...`：操作（キー入力）を、シリアル経由でも画面でも受け付ける
	- ここでの `console` は **GRUB の“画面側”**の意味（kernel の `console=` とは別）。
		- 用語の整理は 1.1 を参照。
	- ねらい（UmuOS の観測観点）：
		- ヘッドレス運用（画面を見ない）でも、ホスト側でシリアルをログとして確実に回収できる
		- いざというときは画面でも GRUB メニュー操作ができる（デバッグの逃げ道を残す）
	- 壊れるとどう見えるか：
		- `serial` を外すと、シリアルに GRUB が出ず「ホストのログが空」に見えることがある
		- `console` を外すと、画面側に GRUB が出ず“目視操作”ができなくなる

- `menuentry "UmuOS-0.1.7-base-stable" { ... }`
	- 起動メニューの 1 エントリ。
	- このブロック内で「どの kernel と initrd を、どんな cmdline で起動するか」が完全に決まる。

- `insmod gzio`
	- gzip 圧縮物を扱うためのモジュール。
	- kernel や initrd が圧縮されている構成で必要になることがある（環境依存の“読めない”事故を避ける意図）。

### 2.1.3 `linux ...` 行（kernel path と cmdline）の徹底解説

`linux /boot/vmlinuz-6.18.1 \` は「ISO 内のどの kernel を起動するか」を指定し、以降の行で **kernel cmdline（= 起動条件の固定）**を渡します。

- `root=UUID=d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`
	- UmuOS の設計上の最重要パラメータ。
	- 意味：永続 rootfs（`disk.img` の ext4）を「UUID で特定する」。
	- どこで使われるか：段階 3 の initramfs `/init` が `/proc/cmdline` を読んでこの UUID をパースし、`/dev/vda` 等の候補デバイスの ext4 superblock UUID と照合して rootfs を確定する。
	- 壊れるとどう見えるか：initramfs `/init` が rootfs を見つけられずに止まる（「UUID 不一致」「root= が無い」系のログが出る）。

- `rw`
	- ルートファイルシステムを read-write でマウントする意図。
	- UmuOS では `/logs/boot.log` への追記など“永続 rootfs に書く”設計が前提なので、観測に直結する。

- `console=tty0 console=ttyS0,115200n8`
	- kernel のコンソール出力先を複数指定している（典型的には画面 `tty0` とシリアル `ttyS0`）。
	- ねらい：ホスト側で `ttyS0` を観測点として固定する。
	- 壊れるとどう見えるか：シリアル指定が無い/速度が違うと、ゲストは動いていてもホスト側が「何も出ない」ように見える。
	- ここは **kernel の入出力（段階 2）**の話。
		- `ttyS0`/`tty0`/`/dev/console` の意味は 1.1 を参照。

- `loglevel=7`
	- printk の冗長度を上げて、起動初期のログを最大限出す（観測性優先）。
	- 失敗切り分けのとき「どこで止まったか」の粒度が上がる。

- `panic=-1`
	- パニック後に自動再起動しない設定。
	- ねらい：落ちた瞬間のログ（一次情報）を消さない／再現時に“同じ落ち方”を観測しやすくする。

- `net.ifnames=0 biosdevname=0`
	- NIC 名を予測可能に固定するための設定。
	- UmuOS の rcS は `eth0` 前提でネット設定を当てるので、ここがブレると「起動はするがネットだけ死ぬ」が起きやすい。

### 2.1.4 `initrd ...` 行（initramfs の指定）

- `initrd /boot/initrd.img-6.18.1`
	- ISO 内の initramfs を指定する。
	- どこに効くか：次段（段階 2→3）で kernel が initramfs を展開し、`/init` を `pid=1` として起動する。
	- 壊れるとどう見えるか：initramfs がロードされないと `/init` が起動できず、早期パニック（`No init found` 相当）に寄りやすい。

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

