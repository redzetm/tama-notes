---
title: "UmuOS-0.1.7-base-stable: 起動メカニズム徹底解説"
date: 2026-03-08
---

# UmuOS-0.1.7-base-stable: 起動メカニズム徹底解説

このノートは UmuOS-0.1.7-base-stable の起動メカニズムを、**層（bootloader/kernel/initramfs/rootfs/userspace）ごとに切り分けて観測できる**形でまとめる。

このノートでは、起動の各段階を同じ観点（主体/入力/主要処理/出力/観測点/失敗時の切り分け）で並べて追跡できるように、段階ごとの **縦型ミニ表**で整理する（ここではこれを「段階別観測表」と呼ぶ）。

前提：UmuOS-0.1.7-base-stable のブートは、最小構成として次で固定されている。

- GRUB → Linux kernel → initramfs（/init）→ ext4(rootfs) を UUID で特定 → `switch_root` → BusyBox `init` → `inittab` → `rcS`

適用範囲：このノートは **UmuOS-0.1.7-base-stable の固定構成**を主な例として書いていますが、0.1.x 系の多く（例：0.1.4-base-stable）でも、少なくとも次は同じ発想で追えます。

- 共通：GRUB → kernel → initramfs `/init` → `switch_root` → BusyBox `init` → `inittab` → `rcS`
- 差分が出やすい：`rcS` の中身（起動する常駐、ログの書き方、ネット設定の細部）、固定値（IP/UUID/ポート等）

読み方のコツ：まずは「段階（どの層が壊れているか）」を確定し、次に “その版の固定値一覧” へ戻って値のズレを疑う、という順にすると迷いにくいです。

## 1. 起動の全体像（レイヤ分離）

UmuOS は研究/観測用OSで、起動失敗を「どの層の問題か」で切り分けられることを最優先にしている。

- **ホスト層（実行環境）**: QEMU/KVM、TAP/bridge、有効なコンソール接続経路
- **ブート層（ISO/GRUB）**: kernel/initrd のロード、kernel cmdline
- **カーネル層**: デバイス提供（virtio-blk/virtio-net）、devtmpfs、ext4
- **initramfs 層**: rootfs（disk.img）探索、ext4 UUID マッチ、`switch_root`
- **永続 rootfs 層（disk.img）**: `/sbin/init`、`/etc/inittab`、`/etc/init.d/rcS`
- **userspace 初期化層（rcS）**: mount/ログ/NW/NTP/telnetd/FTP

### 1.1 用語と観測経路の定義（シリアル/コンソール/tty/telnet）

「徹底解説」として重要なので、UmuOS の起動観測で混乱しやすい用語を先に定義します。
ポイントは **“どの層が、どの経路に文字を出しているか”** を混同しないことです。

#### 1.1.1 console と serial の違い（超要約）

- **serial（シリアル）**：仮想的な“シリアルポート”経由の入出力。
	- ゲスト側では典型的に `ttyS0`, `ttyS1` として見える。
	- ホスト側では QEMU の `-serial ...` で「どこに接続するか」を決める（例：標準入出力、TCP、ファイル）。

- **console（ここでは“画面コンソール”）**：VGA/テキスト画面側の入出力。
	- ゲスト側では典型的に `tty0`（画面側の仮想コンソール）として見える。
	- ホストが GUI で QEMU 画面を出している場合に目視できる。

GRUB も kernel も「出力先（console/serial）」を設定できますが、
**GRUB の設定**と**kernel の `console=...`**は別物なので、段階を跨いで混同しないのがコツです。

#### 1.1.2 `ttyS0` / `ttyS1` / `tty0` とは

- `ttyS0`, `ttyS1`
	- Linux が提供する“シリアル端末”デバイス。
	- `console=ttyS0,115200n8` のように kernel cmdline で指定すると、**kernel のログ（と一部のユーザーランド出力）がそこに流れる**。
	- rootfs 側では `getty` を `ttyS0` や `ttyS1` に立てることで、シリアルログイン経路になる。

- `tty0`
	- 画面側の“現在アクティブな仮想コンソール”を表す。
	- `console=tty0` を入れると、kernel ログを画面側にも出せる（シリアルと併用も可）。

#### 1.1.3 `/dev/console` とは（なぜここに出すのか）

- `/dev/console` は「その時点で kernel が“コンソール”として選んでいる出力先」に繋がる特別なデバイス。
	- どこに繋がるかは `console=...` の指定順などに依存する。
	- UmuOS の `rcS` が `echo ... > /dev/console` のように目印を出すのは、
		- **“人間が見ている経路（たとえば `ttyS0`）に届くようにする”**
		- **“rcS まで到達した”という段階判定ができる**
		ため。

#### 1.1.4 telnet は「コンソール」ではない（ポートとログが分かれる理由）

- telnet は **ネットワーク越しのリモートログイン**で、入出力は `ttyS0` ではなく「telnet のセッション（擬似端末）」に繋がる。
	- そのため、`host_qemu.console_*.log` のような“シリアル/画面のログ採取”とは別系統になりやすい。

- ポート
	- 典型的には **23/TCP**（ただし BusyBox の起動方法や設定によって変更され得る）。
	- ホスト側から接続するときは、QEMU の usernet を使っている場合 `hostfwd=tcp:127.0.0.1:<host-port>-:23` のように
		**ホストの任意ポート**へ転送される構成がよくある（どのポートかは起動オプション次第）。
	- UmuOS の実際の待受ポートは、起動後に `netstat -tlnp`（環境により `ss -tlnp`）等で観測して確定するのが確実。

#### 1.1.5 観測経路の“地図”（どのログが何を拾うか）

- ホストで採取する `host_qemu.console_*.log`
	- 基本的に「ホストが接続している QEMU コンソール（多くはシリアル）」に流れた文字を保存する。
	- telnet セッションの入出力は別物なので、**telnet のログイン操作やコマンド入力がここに残るとは限らない**。

- ゲスト内の `/logs/boot.log`
	- rcS が明示的に追記する“永続ログ”。ネット経由ログインかどうかに関係なく、rcS が動いていれば残る。

#### 1.1.6 devpts（`/dev/pts`）と pts（pseudo-terminal）とは：telnet/ssh の“端末”の正体

結論から言うと、`pts` は **telnet/ssh 等の「ネットワーク越しログイン」を“端末（tty）として成立させる仕組み”**です。
シリアルの `ttyS0` と同じく「端末」ですが、**実体が違う**ため、ここを押さえると切り分けが一気に楽になります。

##### まず用語

- **PTY（Pseudo Terminal）**
	- 「端末のように見えるペア」を作る仕組み。
	- **master（親側）**と**slave（子側）**の2つがあり、
		- master 側：サーバ（例：`telnetd`）が掴む
		- slave 側：ログイン後の `login` / `sh` などが「自分の端末」として掴む
		という分業になります。

- **`pts`（`/dev/pts/<N>`）**
	- PTY の **slave 側デバイス**が見える場所。
	- 例：`/dev/pts/0` が割り当てられると、ログイン後のシェルで `tty` を打つと `pts/0` と出ることがあります。
	- これは「今あなたが操作している端末はシリアル（`ttyS0`）ではなく、疑似端末（`pts`）です」という意味。

- **`devpts`（ファイルシステム）**
	- `/dev/pts` を提供する特殊なファイルシステム。
	- `mount -t devpts devpts /dev/pts` のように mount されて初めて `/dev/pts/0` のようなデバイスが生えます。

- **`ptmx`（`/dev/ptmx` / `/dev/pts/ptmx`）**
	- PTY を「新しく1本払い出して」とカーネルに頼む入口。
	- `telnetd` や `sshd` 等は、まずここを開いて master/slave のペアを作ります。
	- よくある形：
		- `/dev/ptmx`（devtmpfs が用意するキャラクタデバイス）
		- `/dev/pts/ptmx`（devpts 側に現れる入口）
		のどちらか/両方が見える構成。

##### どういう時に pts が効いてくる？（UmuOS 文脈）

- **telnet ログイン**
	- `telnetd` は、接続が来るたびに「そのセッション専用の端末」を作る必要があります。
	- その端末が `pts` です（`ttyS0` とは別物）。

- **ssh ログイン**（もし今後 sshd を入れる場合も同様）
	- ssh も「セッションの端末」を作るため PTY を使います。

- **シリアルログイン（`ttyS0` の getty）**
	- これは `pts` ではなく、物理（仮想）シリアル端末 `ttyS0` を使います。
	- なので「シリアルはOKだが telnet だけ死ぬ」の典型原因が、devpts 周りの欠落になります。

##### いちばん大事なイメージ（データの流れ）

（telnet の例）

- クライアント（あなた） ↔ ネットワーク ↔ `telnetd`（サーバ）
- `telnetd` は PTY を払い出す
	- master：`telnetd` が保持
	- slave：`/dev/pts/0` のようなデバイス
- `login` や `/bin/sh` は slave（`/dev/pts/0`）を自分の標準入出力にして動く

このため、**`/dev/pts` が無い/壊れていると、telnetd が起動していてもログインが成立しません**。

##### 何を見れば「pts が原因」と確定できる？（確認コマンド）

ネットワークは生きている前提で、まずは “devpts が mount されているか” を物証で確認します。

```sh
# devpts が mount されているか
mount | grep -E 'on /dev/pts |type devpts'

# /dev/pts が実体として存在するか
ls -ld /dev/pts
ls -l /dev/pts | head

# ptmx の入口があるか（環境により見え方が違う）
ls -l /dev/ptmx 2>/dev/null || true
ls -l /dev/pts/ptmx 2>/dev/null || true

# 自分の端末が何か（telnet/ssh で入っていると pts になりがち）
tty
```

##### 無い場合どう直す？（最低限の復旧方針）

- **まず mount**（rcS や initramfs `/init` の責務）
	- 代表例：`mount -t devpts devpts /dev/pts`
	- 既に mount 済みなら「権限/所有者/オプション」問題の可能性もあります。

- **`/dev` そのものが無い/貧弱**
	- devtmpfs が mount されていない可能性（`/dev/ptmx` が出ない等）。
	- その場合は devtmpfs の mount（例：`mount -t devtmpfs devtmpfs /dev`）の方が先です。

##### UmuOS ではどこで整備する？（段階との対応）

- initramfs の `/init`（段階 3）でも、観測の土台として `/dev/pts` を mount します。
	- ここは「緊急時に telnet したい」よりも、初期化のテンプレとして入っている側面が強いです。

- 永続 rootfs 側では `rcS`（段階 6）が devtmpfs/devpts を整備し、
	- その上で `telnetd` を起動する流れになります。
	- なので **telnet が死んでいる場合は段階 6（rcS）まで来ているか**と、**devpts が揃っているか**をセットで確認します。

## 2. 段階別観測表（起動フロー）

### 2.1 ブート全段の段階別観測表

横長テーブルは画面幅によって横スクロールになりやすいので、ここでは **段階ごとに縦型**で並べる。

### 2.2 重要な設計判断（表の読み方）

起動を表で追うときのポイントは次の3つ。

1) **境界（どこで責務が切り替わるか）を先に覚える**
	- `switch_root` が境界（initramfs → 永続 rootfs）
	- `rcS` が境界（最低限のOS成立 → 観測/ネット/ログの成立）
2) **観測点は“層を確定する”ために置く**
	- 例：`/logs/boot.log` に追記があるなら「rootfs の rcS が動いている」と言える
3) **失敗時は、表の一段前に戻って“入力が揃っているか”を見る**
	- 例：`root=UUID=...` が `cmdline` に無いなら initramfs の責務ではなく GRUB 側の責務

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
	- GRUB に **gzip で圧縮されたデータを読み解く能力**を追加するモジュール。
	- 何に効く？
		- GRUB は `linux ...` と `initrd ...` で指定されたファイルを「ファイルとして読む」必要がある。
		- その対象（kernel 本体や initrd）が gzip 圧縮されている場合、`gzio` が無いと **GRUB が展開できずロードに失敗**し得る。
	- いつ必要？（UmuOS文脈での見方）
		- UmuOS の例では `initrd.img-6.18.1` という名前からは圧縮有無が断定できない（拡張子が `.gz` でないことも普通にある）。
		- ビルド元や配布形態が変わったときに「たまたま gzip 圧縮になった」ケースで起動が壊れる、という事故を避ける保険として有効。
	- 外すとどう壊れる？（典型）
		- GRUB メニューまでは出るが、kernel もしくは initrd のロードで止まる。
		- 症状は「ファイルが読めない/未知の形式」系のエラーになりやすく、段階 2（kernel）以前で止まる。
	- どう切り分ける？（確認の方向性）
		- “GRUB が読む対象が圧縮かどうか”が論点。
		- もしホスト側で ISO を確認できるなら、ISO 内の該当ファイルに対して `file` コマンド等で圧縮形式を判定できる。
		- ただし最短手は「`insmod gzio` を入れておき、圧縮/非圧縮どちらでも読める状態にして観測を先に進める」。

### 2.1.3 `linux ...` 行（kernel path と cmdline）の徹底解説

`linux /boot/vmlinuz-6.18.1 \` は「ISO 内のどの kernel を起動するか」を指定し、以降の行で **kernel cmdline（= 起動条件の固定）**を渡します。

- `root=UUID=d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`
	- UmuOS の設計上の最重要パラメータ。
	- 意味：永続 rootfs（`disk.img` の ext4）を「UUID で特定する」。
	- どこで使われるか：段階 3 の initramfs `/init` が `/proc/cmdline` を読んでこの UUID をパースし、`/dev/vda` 等の候補ブロックデバイスと照合して rootfs を確定する。
	- UUID はどこで検証される？（ext4 superblock）
		- 照合に使う UUID は「ファイル名」でも「パーティション番号」でもなく、**ext4 の superblock に埋め込まれている UUID**。
		- ext4 superblock はブロックデバイス先頭から **offset 1024 bytes** 付近にあり、UmuOS の `/init` は候補デバイスを開いてそこを読んで UUID を取り出す（= `/dev/disk/by-uuid` 等に依存しない）。
		- ここが一致したデバイスだけを「rootfs」として `/newroot` に mount する。
	- 間違えたときのログの見え方（典型）
		- `root=UUID=...` が無い/パースできない：initramfs 側で「root= が無い」「UUID 形式が不正」系のログが出て、rootfs 確定に進めない。
		- UUID が違う：候補デバイスをスキャンはするが一致が出ず、`scan: /dev/vda` の反復や「matched が出ない」状態で止まる/リトライする。
		- 候補デバイスは見えるが ext4 ではない：superblock の magic 不一致などで「ext4 として読めない」方向のログになりやすい。
	- どう確認する？（観測点）
		- ゲスト起動後に確認できるなら：`cat /proc/cmdline` の UUID と `blkid /dev/vda`（または `dumpe2fs -h /dev/vda`）の UUID が一致するかを見る。
		- 起動に失敗しているときは：initramfs `/init` のスキャンログが「どのデバイスを候補に見たか」「一致が出たか」を教えてくれる（matched が出れば rootfs 確定済み）。

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
	- 何に効く？
		- kernel が `printk()` で吐くメッセージのうち、「**コンソール（`console=` で指定した出力先）へ表示する閾値**」に効く。
			- つまり、ログ自体（リングバッファ）の総量を増やすというより「見える範囲」を広げるための設定。
	- 数値の意味（要点）
		- `0`〜`7` の範囲で、値が大きいほど“より低い重要度（より細かいログ）”まで出す。
		- `7` は典型的に debug 相当まで出す設定で、起動初期の「何がどこで詰まったか」を追うときに有利。
			- 目安：`0`=緊急、`1`=警告級、…、`4`=warning、`6`=info、`7`=debug（実装上の呼び名は環境差があるが、方向性はこの理解でOK）。
	- ねらい（UmuOS の文脈）
		- 失敗切り分けで「どの段階に到達したか」を、ログの粒度で確定しやすくする（観測性優先）。
		- 例：virtio-blk が出ていないのか、ext4 の mount で落ちているのか、`/init` が走っているのか、が文字として見える確率が上がる。
	- 確認方法（起動後に見られるなら）
		- 現在値の確認：`cat /proc/sys/kernel/printk`（先頭の数値が「コンソールへ出す閾値」を表す）。
		- dmesg 側での確認：`dmesg | tail`（見えるログ量が増えているかを見る）。
	- 注意（副作用）
		- 出力が増えるぶん、シリアルログ（ホスト側 `host_qemu.console_*.log`）が大きくなり、必要な行を探しづらくなることがある。
			- 研究/観測フェーズではまず `7` で取り、安定後に必要なら落とすのが運用として楽。

- `panic=-1`
	- 何に効く？
		- kernel が panic に入ったあと、「**自動再起動までの待ち秒数**」の挙動に効く。
		- UmuOS の観測目的では、panic が起きた瞬間のログをその場で保持することが重要なので、基本的に“自動で再起動しない”方向へ寄せる。
	- `-1` の読み方（このノートでの扱い）
		- ここでは「panic 後に自動再起動しない（少なくとも“すぐ再起動してログを流さない”）」意図として理解してよい。
		- ただし `panic=` の細かな扱い（`0`/負値/正値の解釈）はカーネルや設定によって差が出得るため、**起動後に実値で確認する**のが確実。
	- 確認方法（起動後に見られるなら）
		- `cat /proc/sys/kernel/panic`
			- `0` なら「panic 後も自動再起動しない」挙動になるのが一般的。
			- `>0` なら「panic 後 N 秒で再起動する」挙動になり、一次ログが流れやすい。
	- ねらい（観測/再現）
		- 再現実験で「同じ落ち方」を取りたいとき、再起動が走るとログが流れたり、起動ループに入って“最初のpanicログ”を見失いがち。
		- 自動再起動を止めておくと、ホスト側ログ（シリアル）に残った panic 直前/直後の一次情報を確保しやすい。

- `net.ifnames=0 biosdevname=0`
	- 何に効く？（結論）
		- ネットワークIF名を、`ens3`/`enp0s3` のような“予測可能な命名（predictable names）”へリネームする仕組みを抑止し、古典的な `eth0`/`eth1` に寄せるための指定。
		- UmuOS は rcS が `eth0` 前提で設定を当てるため、**IF名がズレると「起動はするがネットだけ死ぬ」**が起きやすい。
	- それぞれの意味（ざっくり）
		- `net.ifnames=0`
			- 代表的には systemd/udev 側の“予測可能な命名”機構を無効化するためのフラグ。
			- systemd が無い最小ユーザーランドでは効かない場合もあるが、効かないなら害もほぼ無い（保険として置ける）。
		- `biosdevname=0`
			- 追加の命名機構（biosdevname）を無効化するためのフラグ。
			- これも環境によっては“そもそも使っていない”ことがあるが、入れておくと「突然 `eth0` が消えた」を避けやすい。
	- 具体的に何が嬉しい？（失敗が減るポイント）
		- rcS が `ip link set eth0 up` / `ip addr add ... dev eth0` のように固定名で操作している場合でも、確実に対象が存在する確率が上がる。
		- 逆にここがズレると、rcS は `eth0` を設定しようとして失敗し、結果として `telnetd`/`ftpd`/NTP が全部「起動しているように見えるが到達できない」状態になりやすい。
	- rcS が `eth0` を触る“想定の中身”（例）
		- UmuOS の説明上は「`/etc/umu/network.conf` を読んで `ip` で設定する」想定なので、概ね次のような処理になる（※実装は rcS の版で差があり得る）。
		
		```sh
		# 例: MODE=static を前提
		# （値は /etc/umu/network.conf から来る想定）
		ip link set dev eth0 up
		ip addr flush dev eth0
		ip addr add ${IP}/${PREFIX} dev eth0
		ip route replace default via ${GW} dev eth0
		# DNS の固定（やる/やらないは実装依存）
		echo "nameserver ${DNS1}" > /etc/resolv.conf
		echo "nameserver ${DNS2}" >> /etc/resolv.conf
		```
		
		- この処理は「`eth0` が存在する」ことが前提なので、IF名が `ens3` 等に変わると `ip link set dev eth0 up` の時点で失敗し、以降のネット機能（telnet/FTP/NTP）が全部連鎖的に落ちる。
	- 確認方法（起動後に見られるなら）
		- IF名の確認：`ip -br link`（`eth0` が存在するかを見る）
		- アドレスの確認：`ip -br addr`（rcS が設定した IP が `eth0` に乗っているか）
		- 経路の確認：`ip route`（default route が `eth0` に向いているか）
		- DNSの確認：`cat /etc/resolv.conf`
	- 注意（切り分けのコツ）
		- `NET_MODE=none`（そもそも NIC を提供しない）だと、当然 `eth0` 自体が現れない。
			- この場合は cmdline ではなく「段階 0（QEMU のネット提供）」が原因。

### 2.1.4 `initrd ...` 行（initramfs の指定）

- `initrd /boot/initrd.img-6.18.1`
	- ISO 内の **initramfs（初期ルートファイルシステム）**を指定する。
	- まず前提：initramfs とは何か？
		- kernel が起動してすぐ使える「最小のユーザーランド」を、1ファイル（アーカイブ）に固めたもの。
		- 典型的には **cpio アーカイブ**で、中身は `/init`、最低限の `/bin` `/sbin` `/proc` `/sys` `/dev` など。
		- 圧縮されていることも多い（gzip/xz/zstd 等）。圧縮されていても kernel 側で展開できる構成が一般的。
		- 用語の注意：
			- `initrd` は **GRUB のコマンド名**（歴史的な呼び方）で、ここで指定している実体は initramfs のことが多い。
			- このノートでは、以降「実体」を指すときは基本的に initramfs と呼ぶ。
	- この 1 行が“どこに効くか”（段階 2→3 の成立条件）
		- GRUB の `initrd ...` は「initramfs のファイルをメモリへロードし、kernel に渡す」宣言。
		- その後 kernel は、受け取った initramfs を **RAM 上に展開（あるいは展開可能な形で取り込む）**して、そこを一時的な root（`/`）として扱う。
		- そして initramfs 内の **`/init` を `pid=1` として起動**する。
			- ここで起動する `/init` が、永続 rootfs（UmuOS では `disk.img`）を見つけて `switch_root` する“橋渡し”を担当する。
			- 補足：`pid=1` は OS 起動の中心点なので、ここが成立しないと後段（rootfs の BusyBox `init` や `rcS`）へ進めない。
			- さらに補足：kernel cmdline の `init=...` を指定すると、`/init` 以外を `pid=1` にできる（デバッグ用の逃げ道）。
	- UmuOS で「initrd が重要」な理由（このノートの主題と直結）
		- UmuOS の起動は「initramfs の `/init` が `root=UUID=...` を読んで ext4(rootfs) を確定する」設計なので、
			**initrd が無い＝rootfs へ到達する経路そのものが消える**。
		- つまり `initrd ...` は「段階 3（initramfs `/init`）が存在する」ことの前提で、この段階別観測表における“段差”そのもの。
	- 壊れるとどう見えるか（典型パターン）
		- initrd のパスが間違い/ファイルが無い：GRUB（段階 1）で initrd ロードに失敗し、kernel 起動以前で止まる/エラーになる。
		- initrd が渡っていない・形式が壊れている：kernel が initramfs を展開できず、`/init` を起動できない。
			- 結果として早期パニック（`No init found` / `Kernel panic - not syncing` 相当）に寄りやすい。
		- initrd はロードできたが `/init` が期待通り動かない：段階 3 で止まる（`root=UUID=...` が読めない/`/dev` が出ない/uuid一致が見つからない等）。
	- 切り分けの観測点（最短で効く順）
		- GRUB 段階：`initrd /boot/initrd.img-6.18.1` のパスが ISO 内で本当に正しいか（単純だが最重要）。
		- kernel 段階：シリアルに early log が出ているなら「kernel は動いている」。そこで `/init` 起動失敗のメッセージが出るかを見る。
		- initramfs 段階：initramfs `/init` のログ（例：`cmdline parsed: root=UUID=...`）が出れば、initrd のロード〜`/init` 起動までは成功している。
		- 可能ならホスト側での確認：ISO をマウントして `boot/initrd.img-6.18.1` の実体を見て、`file` 等で形式（cpio/圧縮方式）を当てると原因が早い。
			- 例（ホストで ISO を見られる場合）：
				- `file boot/initrd.img-6.18.1`（圧縮方式の推定）
				- `lsinitramfs boot/initrd.img-6.18.1 | head`（入っているパス一覧。無ければ次の方法）
				- `zcat boot/initrd.img-6.18.1 | cpio -t | head`（gzip の場合の一覧）
				- `xzcat ... | cpio -t | head` / `zstdcat ... | cpio -t | head`（xz/zstd の場合）
				- 最低限、`/init` が含まれていることが分かれば「段階 3 の入口はある」と言える。

#### 段階 2: kernel

| 項目 | 内容 |
|---|---|
| 主体（層） | kernel |
| 入力 | kernel config、cmdline |
| 主要処理 | デバイス初期化（virtio 等）、initrd の `/init` を `pid=1` として実行 |
| 出力（次段へ） | initramfs が動作開始 |
| 観測点（成功判定） | `console=ttyS0,115200n8` でログが見える |
| 失敗時の切り分け | `/dev` が出ない場合は `DEVTMPFS` 等のカーネル設定を疑う。 |

この段階（kernel）は「**ISO/GRUB が渡した kernel + initrd を、実際に“OS として動き始める”状態へ持っていく**」層です。
段階 1（GRUB）と混同しやすいポイントは、**ここから先のログは GRUB ではなく kernel が出している**という点です。

### 段階 2（kernel）の徹底解説：何が起きているか

kernel は、この段階で大きく次の4つをやります。

1) **初期化（CPU/メモリ/割り込み/タイマ等）して自分の足場を作る**
	- ここは“OS カーネルとしての起動”そのもの。
	- この時点でのログが見えるかどうかは、ほぼ `console=` の成否で決まる（=観測点）。

2) **デバイスを使えるようにする（virtio-blk / virtio-net 等）**
	- UmuOS の「次段（initramfs `/init`）が `disk.img` を探せる」ためには、kernel がブロックデバイスを認識して `/dev/vda` 等を作れる必要がある。
	- ここで virtio-blk が出ないと、段階 3 の `/init` は“探す対象（候補デバイス）”が無くて詰まりやすい。

3) **initramfs（GRUB の `initrd ...` で渡されたもの）を展開し、暫定 rootfs にする**
	- 重要：`initrd` は GRUB のコマンド名で、実体は initramfs（cpioアーカイブ）であることが多い。
	- kernel はそれを RAM 上に取り込み、そこを一時的な `/` として扱う。

4) **initramfs の `/init` を `pid=1` として起動する**
	- これが段階 2 のゴール。
	- UmuOS の場合、`pid=1` の `/init` が rootfs を UUID で確定し `switch_root` へ進む設計なので、段階 2 は段階 3 の“入口を開ける係”。

### 表の各項目の要点（段階 2）

- 入力：`kernel config、cmdline`
	- **kernel config**：kernel に何の機能・ドライバを入れてビルドしたか。
		- UmuOS 文脈で重要なのは、少なくとも次が“起動を進めるのに必要になりがち”ということ。
			- virtio 系ドライバ（virtio-blk/virtio-net）
			- ext4
			- devtmpfs（`/dev` を自動生成する仕組み）
	- **cmdline**：GRUB が `linux ...` 行で渡した起動引数。
		- kernel 自身が読むもの（`console=`/`loglevel=`/`panic=` など）と、ユーザーランドが読むもの（`root=UUID=...`）が混在する。
		- ここが段階 1 の固定点で、段階 2 の挙動（ログの見え方含む）をほぼ決める。

- 主要処理：`デバイス初期化（virtio 等）、initrd の /init を pid=1 として実行`
	- 「virtio 等」は、UmuOS の `disk.img` が **virtio-blk** として見える前提（典型：`/dev/vda`）を含む。
	- 「`/init` を `pid=1`」は、initramfs の中にある `/init` が“最初のユーザーランド”として起動する、という意味。
		- ここが成立すると、段階 3 のログ（`cmdline parsed:` / `scan:` / `matched:` 等）が出始める。

- 出力（次段へ）：`initramfs が動作開始`
	- 言い換えると「kernel が `/init` を起動し、段階 3 の責務（UUIDでrootfs確定）へバトンが渡った」。

- 観測点（成功判定）：`console=ttyS0,115200n8` でログが見える
	- これは「kernel が生きている」ことの最短の証拠。
	- さらに精度を上げるなら次が見えると“より確定”になる。
		- initramfs `/init` のログが出始める（=段階 2 のゴールに到達）
		- `/proc/cmdline` が期待通り（段階 3/5 以降で確認できる）

### 「ログが見えない」の典型原因（段階 1 と段階 2 の境界で混乱しやすい）

- GRUB は動いているが kernel のログが見えない
	- `console=ttyS0,115200n8` が無い/速度が違う/出力先が違う可能性。
	- あるいは QEMU の `-serial ...` の接続が期待と違う可能性。
	- 切り分けの考え方：
		- GRUB メニューが見えるなら段階 1 は概ねOK。
		- そこから先が無音なら「段階 2 のログ経路（kernelのconsole）」をまず疑う。

### 失敗時の切り分け：`/dev` が出ないとは何が困るのか

段階 3（initramfs `/init`）は、`/dev` 配下のブロックデバイス（例：`/dev/vda`）を走査して rootfs を探します。
なので kernel が `/dev` を用意できないと、段階 3 が成立しません。

- 典型症状
	- initramfs に入ったが、`/dev/vda` が見えない / そもそも `/dev` が空に近い。
	- その結果、`scan:` が空振りし続ける／一致が出ない。

- まず疑うポイント
	- kernel の **devtmpfs** 周り（`CONFIG_DEVTMPFS` と `CONFIG_DEVTMPFS_MOUNT`）
		- これが無い/自動マウントされないと、udev を使わない最小 initramfs では `/dev` が増えずに詰まりやすい。
	- initramfs 側が `/dev` を正しく mount していない（段階 3 の責務）可能性もある。
		- ただし「段階 2 の表」では、まず kernel 側要因として `DEVTMPFS` を挙げている。

### この段階の“正解の見え方”（最短の成功パターン）

1) `console=ttyS0,115200n8` 経由で kernel のログが流れ始める
2) そのまま initramfs `/init` のログ（例：`cmdline parsed:`）が出始める
	- ここまで来たら「段階 2 は突破した」と判断してよい（以降の原因は段階 3 以降へ寄る）。

#### 段階 3: initramfs `/init`　＜最重要＞

参考　実装検証済み（C言語）：[init.md](init.md)

| 項目 | 内容 |
|---|---|
| 主体（層） | initramfs `/init` |
| 入力 | `/proc/cmdline` の `root=UUID=...`、`/dev/*` ブロックデバイス |
| 主要処理 | `root UUID` 抽出 → `/dev` 走査 → ext4 superblock の UUID で一致デバイスを探す |
| 出力（次段へ） | `mount(dev, /newroot, ext4)` が成功する |
| 観測点（成功判定） | initramfs ログに `cmdline parsed: root=UUID=...` と `matched: dev=/dev/vdX` が出る |
| 失敗時の切り分け | `root=UUID` 欠落/誤り、disk 未接続、ext4 でない、UUID 不一致。 |

### 段階 3（initramfs `/init`）の徹底解説：何が起きているか

段階 3 は「**永続 rootfs（disk.img）を見つけて mount できる状態を作る**」段階です。
段階 2（kernel）が `/init` を `pid=1` として起動した直後から、この段階が始まります。

この段階で `/init` がやっていることは、ざっくり次の4つです。

1) **観測の土台を作る**（`/proc`/`/sys`/`/dev`/`/dev/pts` を mount）
	- `/proc/cmdline` を読むには `/proc` が必要。
	- `/dev/vda` 等のブロックデバイスを列挙するには `/dev` が必要。
	- UmuOS の `/init` は「udev などでデバイス名を作ってもらう」方針ではないため、**ここで `/dev` を mount できないと後続が全部詰まる**。

2) **起動引数から root UUID を取り出す**（`/proc/cmdline` → `root=UUID=...` をパース）
	- ここで UUID を取れないと「何を探せば良いか」が決まらないので致命的。
	- 成功すると `cmdline parsed: root=UUID=...` が出て、「入力が段階 1/2 から正しく届いている」ことの証明になります。

3) **候補デバイスを走査して、ext4 UUID が一致するものを探す**（`/dev/vda` など）
	- 起動直後は virtio-blk が遅れて出てくることがあるため、見つかるまでリトライします。
	- 走査中は `scan: /dev/vda` のようなログが繰り返し出ます。
	- ext4 かどうかの判定は、ブロックデバイスから **ext4 superblock（offset 1024）**を読んで `magic` と `uuid` を見ることで行います。

4) **一致したデバイスを `/newroot` に mount する**（`mount(dev, /newroot, ext4, rw)`）
	- ここが段階 3 のゴール。
	- 成功すると `matched:` の後に `mount root ok (rw): ...` が出て、「永続 rootfs はもう掴めた」と判断できます。
	- この時点ではまだルート（`/`）は initramfs のままです（次の段階 4 で切り替える）。

### 表の各項目の要点（段階 3）

- 入力：`/proc/cmdline` の `root=UUID=...`、`/dev/*` ブロックデバイス
	- `root=UUID=...` は「探すべき rootfs の唯一の識別子」。
	- `/dev/*` は「探す対象そのもの」。virtio-blk が出ないと対象が存在しません。

- 観測点（成功判定）：`cmdline parsed:` と `matched:`
	- `cmdline parsed:` が出た → `/proc` が mount できていて、cmdline の入力もOK。
	- `matched:` が出た → 走査が成功し、rootfs デバイスが確定。
	- `mount root ok (rw):` が出た → mount まで完了（段階 4 の前提が揃った）。

### 典型的な失敗パターンと見え方（段階 3）

- `root=UUID= not found` / `invalid UUID string`
	- 原因は段階 1（GRUB の linux 行）で cmdline が崩れている可能性が高い。

- `scan:` が出続けて `matched:` が一度も出ない
	- UUID が違う、disk.img が別物、ext4 ではない、または「そもそも対象デバイスが出ていない」のどれか。
	- `/dev/vda` 自体がログに出ない場合は、段階 2（virtio-blk）側の問題に寄ります。

- `mount root failed`
	- 一致はしたが mount に失敗。ext4 ドライバ未搭載、破損、読み取り専用要因などが候補。
	- ただし UmuOS では、この段階のログがそのまま一次資料になります（まずログから errno を読む）。

#### 段階 4: initramfs `/init`（switch_root）　＜最重要＞

参考　実装検証済み（C言語）：[init.md](init.md)

| 項目 | 内容 |
|---|---|
| 主体（層） | initramfs `/init` |
| 入力 | `/newroot` に mount 済み |
| 主要処理 | `exec /bin/switch_root /newroot /sbin/init` |
| 出力（次段へ） | 永続 rootfs の init へ遷移 |
| 観測点（成功判定） | initramfs ログが `switching root` で終わり、以降は rootfs 側へ |
| 失敗時の切り分け | `switch_root` が無い（BusyBox applet未有効）／`/newroot/sbin/init` 不在を疑う。 |

### 段階 4（switch_root）の徹底解説：何が起きているか

段階 4 は「**今まで initramfs だった `/` を、永続 rootfs（`/newroot`）へ切り替える**」段階です。
段階 3 が「rootfs を見つけて `/newroot` に mount する」だったのに対して、段階 4 は「**ルートそのものを入れ替える**」のが仕事です。

UmuOS の `/init` は次の 1 行（実質）でこれを行います。

- `exec /bin/switch_root /newroot /sbin/init`
	- `exec` は「今の `/init` プロセス（pid=1）を、`switch_root` に置き換える」動作。
	- 置き換えに成功すると `/init` は戻ってこない（＝成功時は以降のログが出ない）のが正常です。

### `switch_root` は何をしているのか（最低限の理解）

- `/newroot` を新しい `/` として扱うようにして、引数で指定した init（ここでは `/sbin/init`）を起動します。
- 目的は「起動の主役を initramfs から disk.img へ移す」こと。
- これが成功すると、次に見えるログやプロンプトは **rootfs 側（BusyBox `init` → `inittab` → `rcS`）**のものになります。

### 表の各項目の要点（段階 4）

- 入力：`/newroot` に mount 済み
	- 段階 3 で mount が成功していないと、段階 4 は成立しません。
	- 「`/newroot` はあるが中身が空/期待と違う」場合は、段階 3 のマウント対象を疑います。

- 主要処理：`exec /bin/switch_root /newroot /sbin/init`
	- `switch_root` は initramfs 側に存在する必要があります（BusyBox applet のことが多い）。
	- `/sbin/init` は新しい root（`/newroot`）側に存在する必要があります。

- 観測点（成功判定）：`switching root` で終わり、以降は rootfs 側へ
	- `switching root` が出た直後に、BusyBox `init` の動き（`inittab` / `rcS`）へ移れば成功です。
	- 逆に `execv switch_root failed` が出たら段階 4 で落ちています（段階 5 以降へは行っていない）。

### 典型的な失敗パターンと見え方（段階 4）

- `switch_root` が無い/実行できない
	- 症状：`execv switch_root failed`（`ENOENT` や `EACCES` など）が出て止まる。
	- 原因：initramfs に `/bin/switch_root` が無い、または実行権限が無い。

- `/newroot/sbin/init` が無い
	- 症状：`switch_root` は存在しても、引数の init が見つからず失敗して止まる。
	- 原因：disk.img の中身が想定と違う（rootfs が別物、作成途中、パス違い）。

- 段階 3 は通ったのに段階 5 の気配が無い
	- `switching root` の後に何も出ない場合、console 経路の問題（`console=` と `getty` の整合）も候補。
	- ただしまずは段階 4 の直後にエラーが出ていないか（`execv` 失敗ログ）を優先して確認します。

### なぜ BusyBox `init` が必要か（段階 3/4 と段階 5/6 の役割分担）

ここは混同しやすいですが、UmuOS の起動では **「init が2種類登場する」**だけで、役割が別です。

- initramfs の `/init`（自作C実装、段階 3/4 の主体）
	- 目的：永続 rootfs（disk.img）を見つけて `/newroot` に mount し、`switch_root` でバトンを渡す。
	- つまり「起動の橋渡し」担当。
	- `exec /bin/switch_root /newroot /sbin/init`（実装では `execv`）が成功した時点で、プロセスは置き換わり **自作 `/init` は戻らない**のが正常。

- rootfs の BusyBox `init`（段階 5/6 の主体）
	- 目的：`/` が永続 rootfs に切り替わった後の「OSの通常運用」を開始・維持する。
	- 具体的には `/etc/inittab` を読み、`::sysinit:/etc/init.d/rcS` を実行して初期化（ログ/NW/telnetd/FTP 等）を進める。
	- `getty` の respawn など「起動後も継続して面倒を見る」役割を持つ。

結論として、`switch_root` は **「rootfs の BusyBox `init` が動き始めるための前処理」**であり、BusyBox `init` は **`switch_root` の後に初めて必要になる主役**です。

#### 段階 5: rootfs BusyBox `init`

| 項目 | 内容 |
|---|---|
| 主体（層） | rootfs BusyBox `init` |
| 入力 | `/sbin/init`（BusyBox）、`/etc/inittab` |
| 主要処理 | `::sysinit:/etc/init.d/rcS` 実行、`getty` を respawn |
| 出力（次段へ） | rcS 実行 + ttyS0/ttyS1 ログイン経路を用意 |
| 観測点（成功判定） | ログインプロンプトが出る、または `rcS done` が見える |
| 失敗時の切り分け | `inittab` の誤り、`rcS` 実行権限、rootfs が ro になっていないか。 |

### 段階 5（BusyBox `init`）の徹底解説：何が起きているか

段階 4 の `switch_root` が成功すると、次に起きるのは **rootfs（disk.img）側の `/sbin/init` が pid=1 として動き始める**ことです。

ここで重要なのは、段階 5 の BusyBox `init` が「何か重い初期化を全部やる」わけではなく、次の2つを行う **“司令塔”**だという点です。

1) **最初の初期化（sysinit）を1回だけ実行する**
	- 典型は `/etc/inittab` の `::sysinit:/etc/init.d/rcS`
	- ここで `rcS`（段階 6）へバトンが渡されます。

2) **起動後も継続して “運用を維持する”**
	- 代表例：`getty` を respawn し続けてログイン経路を提供する
	- 代表例：子プロセスを回収（ゾンビ回収）してシステムを安定させる

> 初心者向けの言い換え：段階 5 は「家の管理人（init）」で、段階 6 は「最初の引っ越し作業（rcS）」です。

### 表の各項目の要点（段階 5）

- 入力：`/sbin/init`（BusyBox）と `/etc/inittab`
	- `/sbin/init` は多くの場合 BusyBox（`/bin/busybox`）への symlink です。
	- `/etc/inittab` は「最初に何を実行するか（sysinit）」「どの端末で login を待つか（getty）」を定義します。

- 主要処理：`rcS` 実行、`getty` を respawn
	- `rcS` が成功して最後に `rcS done` を出しても、`init` 自体は終了しません。
	- `init` が生きているからこそ、ログインが何度でも可能になり、落ちたプロセスも再起動できます。

- 観測点（成功判定）：ログインプロンプト or `rcS done`
	- `rcS done` が見えたら「段階 6 まで動いた」が確定します。
	- ログインプロンプトが出たら「getty が動いている」が確定します。
	- 片方しか出ない場合でも、どちらが欠けているかで切り分けが一気に絞れます。

### まず確認するコマンド（段階 5）

ログインできる状態（シェルが取れる状態）で、最短で段階 5 を確定する観測です。

```sh
# pid=1 が何者か（BusyBox init が動いているか）
cat /proc/1/comm
readlink -f /proc/1/exe 2>/dev/null || true
ls -l /proc/1/exe 2>/dev/null || true

# inittab の存在と内容（sysinit と getty の行が肝）
ls -l /etc/inittab
sed -n '1,120p' /etc/inittab

# /sbin/init が BusyBox に向いているか（代表的な確認）
ls -l /sbin/init
```

### 典型的な失敗パターンと見え方（段階 5）

- `switch_root` は成功したのに、以降が無言（プロンプトも `rcS done` も無い）
	- まず疑う順：
		1) **console 経路の不一致**（`console=` が指す先と、`inittab` の getty 対象 tty がズレている）
		2) `/etc/inittab` が無い/壊れている（BusyBox `init` が想定通り動けない）
		3) `/sbin/init` が実行できない（ELF/権限/ファイル欠落など）
	- 目標：段階 5 の問題か、見えていないだけ（表示経路の問題）かを分けること。

- `rcS done` は出たがログインプロンプトが出ない
	- `rcS`（段階 6）は動いたが、**getty の設定やデバイスノード**の問題が濃厚です。
	- `/etc/inittab` の getty 行、`/dev/ttyS0` などの存在、devtmpfs/devpts の mount 状況を確認します。

- ログインプロンプトは出たが `rcS` が走っていない
	- `inittab` の `sysinit:` 行が無い/コメントアウト/パス違い、または `rcS` が即死している可能性。
	- `rcS` の先頭で `echo` を `/dev/console` や `/logs/boot.log` に出す設計にしておくと、段階 6 の切り分けが一気に楽になります（UmuOS はその方針）。

#### 段階 6: rootfs `rcS`

| 項目 | 内容 |
|---|---|
| 主体（層） | rootfs `rcS` |
| 入力 | `/etc/umu/network.conf`、`/etc/profile`、`/umu_bin/*` |
| 主要処理 | mount/ログ/NW/NTP/telnetd/FTP を初期化 |
| 出力（次段へ） | 観測可能状態（ログ・NW・リモート） |
| 観測点（成功判定） | `/logs/boot.log` が追記される／NTP before/after が残る／telnet/FTP が起動 |
| 失敗時の切り分け | `boot.log` が伸びない→ rcS まで来てない。NWだけ死ぬ→ host tap/bridge or `network.conf` を疑う。 |

### 段階 6（`rcS`）の徹底解説：何が起きているか

段階 6 は「**OSを“使える状態”にするための初期化スクリプト**」です。
段階 5 の BusyBox `init` が `inittab` の `sysinit` として `rcS` を起動し、`rcS` が次をまとめて実行します。

- mount 類（`/proc`, `/sys`, devtmpfs, devpts など）を揃える
- ログの受け皿（`/logs/boot.log` など）を作り、以後の切り分け材料を残す
- ネットワーク設定（static IP 等）を反映し、疎通できる土台を作る
- 必要な常駐（telnetd/ftpd など）を起動する

ここでのポイントは「`rcS` は **1回だけ**動く初期化であり、失敗したらその時点で“必要な物が揃わない”」という点です。

### 表の各項目の要点（段階 6）

- 入力：`/etc/umu/network.conf`、`/etc/profile`、`/umu_bin/*`
	- rcS は “設定ファイル + 小さな補助コマンド群” を読み込んで初期化を組み立てます。
	- どれか1つが欠けても、ネットワークだけ死ぬ/ログだけ死ぬなど「部分的に壊れる」ことがあります。

- 観測点（成功判定）：`/logs/boot.log` / NTP / telnet/FTP
	- `boot.log` が伸びる：rcS が実行された（段階 6 に到達した）証拠。
	- NTP before/after がある：rcS が中盤まで進んだ証拠。
	- telnet/FTP が起動：rcS の終盤まで進み、外から入れる状態になった証拠。

### まず確認するコマンド（段階 6）

`rcS` がどこまで進んだかを、なるべく「物証」で確認します。

```sh
# rcS の実行痕跡（ログが一次資料）
ls -l /logs/boot.log
tail -n 120 /logs/boot.log

# mount が揃っているか（devpts が無いと getty が怪しくなる等）
mount

# ネットワークの実態（設定ファイルだけ見てもダメ。実際の状態を見る）
ip link
ip addr
ip route

# サービスが生きているか（存在する場合）
ps w
ss -lntup 2>/dev/null || netstat -lntup 2>/dev/null || true
```

### 典型的な失敗パターンと見え方（段階 6）

- `/logs/boot.log` が存在しない/伸びない
	- そもそも `rcS` が起動されていない（段階 5 の `inittab` / `sysinit` を疑う）か、rcS が冒頭で落ちています。
	- `rcS` の実行権限（`chmod +x`）、shebang（`#!/bin/sh`）、`/bin/sh` の存在をまず確認します。

- `boot.log` は伸びるがネットワークだけ死ぬ
	- rcS は動いているので「段階 5/6 には到達」しています。
	- 切り分けは host 側（TAP/bridge）と guest 側（`/etc/umu/network.conf`、`eth0` 名、`net.ifnames=0`）に分けます。
	- まず guest では `ip link` でインタフェース名と状態を確認し、次に `ip addr`/`ip route` の実体を確認します。

- `boot.log` は伸びるが telnet/FTP が起動しない
	- rcS の後半で落ちているか、該当バイナリ/設定が欠けている可能性。
	- `ps`/`ss` で “プロセスがいないのか、LISTEN してないのか” を分けるのが第一歩です。

- `rcS done` は出るが、その後すぐ固まる/不安定
	- rcS 自体は最後まで走ったが、以降に **常駐がクラッシュループ**して負荷が上がっている、または **pid=1 が想定の BusyBox `init` ではない/落ちている**可能性があります。
	- まず `cat /proc/1/comm` で pid=1 を確認し、`ps` で同じプロセスが短時間で増減していないか（respawn ループ）を確認します。

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
- （QEMU の起動引数などで `ttyS1` を TCP 等へ接続している構成なら）`ttyS1` へ接続してログインできる

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

### 6.5 telnet ログインできない（`devpts`/`/dev/pts` が原因になりやすい）

「起動はしている（`/logs/boot.log` は伸びる）」「IP疎通もできる」なのに telnet が入れない場合の切り分けです。

補足：ここで `inetd` という語が出てきますが、方式の違いを知っていると混乱しません。

- **rcS から `telnetd` を直起動（standalone）**
	- rcS が `telnetd -p 23 -l /bin/login` のように起動し、`telnetd` 自体が常駐して LISTEN します。
	- UmuOS は基本この前提（「初期化は rcS に寄せて観測しやすくする」方針）。

- **`inetd` 方式（スーパーサーバ）**
	- `inetd` が常駐して LISTEN し、接続が来た瞬間に `telnetd`（や `login`）を子プロセスとして起動します。
	- この方式だと「`telnetd` が常駐していない」ように見えることがあります（接続中だけ出る）。

1) まず telnetd が起動していて LISTEN しているか

```sh
ps w | grep -E 'telnetd|inetd' | grep -v grep || true
ss -lntup 2>/dev/null | grep ':23\b' || netstat -lntup 2>/dev/null | grep ':23\b' || true
```

2) 次に `devpts` が揃っているか（これが無いとセッション端末が作れない）

```sh
mount | grep -E 'on /dev/pts |type devpts'
ls -l /dev/ptmx 2>/dev/null || true
ls -l /dev/pts/ptmx 2>/dev/null || true
ls -ld /dev/pts
```

3) telnet で入れた“はず”の時、端末が `pts/<N>` になっているか

```sh
tty
```

見え方の典型：

- `telnetd` がいるのにログインが成立しない／すぐ落ちる
	- `devpts` 未mount、`/dev/ptmx` 不在、`/dev`（devtmpfs）未整備などが候補。

- `telnetd` 自体がいない
	- 段階 6（`rcS`）の後半まで到達していない、または rcS 内で起動に失敗している可能性。
	- まず `/logs/boot.log` を確認して rcS の進捗を確定します。

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

### 7.1 rootfs UUID は何か（どこで作られ、どこで使われるか）

このノートで言う `rootfs UUID` は、**ext4 ファイルシステムの UUID（filesystem UUID）**です。
UmuOS の起動では、これが「起動条件の固定点」として強く効きます。

#### どこで作られる？（生成/固定）

- ext4 を作るとき（`mkfs.ext4`）に filesystem UUID が **自動生成**されます。
- 「再現可能」にするなら、生成時に **UUID を指定して固定**します。

代表例：

```sh
# ext4 作成時に filesystem UUID を固定する
mkfs.ext4 -U d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 /dev/vda
```

既存の ext4 に後から設定もできますが、UUID 変更は “起動条件” を変える行為なので注意です。

```sh
# 既存 ext4 の filesystem UUID を変更する（再現性の観点では慎重に）
tune2fs -U d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 /dev/vda
```

#### GRUB / cmdline / initramfs `/init` との関係（UmuOS での一本の鎖）

UmuOS は「勝手に探す」のではなく、**GRUB が渡した UUID を唯一の入力として rootfs を確定する**設計です。

1) **GRUB（段階 1）**
	- `linux ... root=UUID=<固定UUID> ...` を kernel cmdline として渡す
	- ここで UUID が間違うと、以降の層がいくら正しくても rootfs が確定できません

2) **kernel（段階 2）**
	- cmdline を `/proc/cmdline` として見える形にして、次段へ渡す
	- 注意：ここでの `root=UUID=...` は「カーネルが勝手に mount する」用途ではなく、UmuOS では **initramfs が読むための固定入力**として使っています

3) **initramfs `/init`（段階 3）**
	- `/proc/cmdline` の `root=UUID=...` をパースする
	- `/dev/*` のブロックデバイス候補を走査し、**ext4 superblock（offset 1024）から UUID を読んで一致判定**する
	- 一致したデバイスを `/newroot` に mount し、`switch_root` で rootfs 側へ移る

このため UUID は、**GRUB の設定・disk.img（ext4）・initramfs 実装の三者を結ぶ単一の整合ポイント**になります。

#### PARTUUID との違い（混同しやすい）

- `UUID=`（ここで扱っているもの）
	- **ファイルシステム UUID**（ext4 superblock の UUID）
- `PARTUUID=`
	- **パーティション UUID**（GPT/MBR 由来）

UmuOS の設計（`/init` が ext4 superblock を読む）では、`root=UUID=` は filesystem UUID を想定しています。

#### どう確認する？（観測ポイント）

- GRUB が意図した UUID を渡しているか
	- `cat /proc/cmdline | sed -e 's/ /\n/g' | grep '^root='`

- 実際の ext4 側の UUID は何か（rootfs 側で確認できる場合）
	- `blkid`
	- `lsblk -f`
	- `tune2fs -l /dev/vda | grep -i 'Filesystem UUID'`（環境により `tune2fs` が無い場合あり）

#### UUID 不一致の典型的な見え方

- 段階 3 で `scan:` が出続けて `matched:` が一度も出ない
	- 多くは「GRUB の `root=UUID=...` と disk.img の filesystem UUID がズレている」
	- あるいは disk.img が想定と別物（別ファイル/作り直し/コピー違い）

### 7.2 `switch_root` 前（initramfs）で使う BusyBox コマンド一覧

目的：`switch_root` 前は「initramfs 側の BusyBox（または最小ユーザーランド）」が頼りです。
ここで使うコマンドを台帳化しておくと、のちに **自作コマンドへ差し替える対象**を明確にできます。

注意：BusyBox の applet は **ビルド設定（`.config`）次第**で増減します。
この節は「UmuOS が期待している最低限」と「現物から一覧を採取する方法」をセットで書きます。

#### 現物の一覧を採取する（initramfs 側）

initramfs にシェルがある前提なら、次で「入っている BusyBox」を確定できます。

```sh
# BusyBox 本体の場所は環境差があるので、まず探す
command -v busybox || ls -l /bin/busybox /sbin/busybox 2>/dev/null || true

# applet 一覧（短い）
busybox --list 2>/dev/null || true

# applet 一覧（フルパス形式：symlink で使っている場合に便利）
busybox --list-full 2>/dev/null || true
```

#### このノート（initramfs 段階）で重要度が高いコマンド

`/init` 自体は C 実装ですが、initramfs での障害切り分けや“保険の手動操作”のために、次があると強いです。

- **プロセス/ログ観測**：`dmesg`, `cat`, `echo`, `printf`, `sleep`
- **ファイル操作**：`ls`, `mkdir`, `cp`, `mv`, `rm`, `chmod`, `ln`, `stat`
- **文字列処理**：`grep`, `sed`, `awk`（最低でも `grep`）
- **マウント関連**：`mount`, `umount`
- **デバイス最低限**：`mknod`（`/dev/console` などで詰まった時の復旧用）
- **境界コマンド**：`switch_root`（または `/bin/switch_root` が BusyBox 由来でも可）

ここは「自作 `/init` の責務（探索→mount→`switch_root`）」が中心なので、コマンド数は少なめで良いです。

### 7.3 `switch_root` 後（rootfs）で使う BusyBox コマンド一覧

目的：`switch_root` 後は rootfs 側の BusyBox `init` と `rcS` が主役で、
**“常用コマンド＝将来差し替え対象”**が一気に増えます。
ここを一覧化しておくと、「どれを自作で置き換えるか／置き換えた結果どこが壊れるか」が追えます。

#### 現物の一覧を採取する（rootfs 側）

```sh
# BusyBox の実体と symlink の張られ方を確認
command -v busybox
busybox 2>&1 | head -n 2 || true

# applet 一覧（現物）
busybox --list
busybox --list-full 2>/dev/null || true

# UmuOS は PATH 先頭が /umu_bin なので、ここに何が生えているかを見る
echo "$PATH"
ls -l /umu_bin 2>/dev/null || true
```

#### このノート（rootfs 段階）で重要度が高いコマンド（差し替え対象になりやすい）

- **init/起動制御**：`init`, `getty`, `login`, `sh`
- **ログ**：`syslogd`, `klogd`（採用している場合）
- **プロセス/状態確認**：`ps`, `top`, `free`, `uptime`, `kill`
- **ファイルシステム**：`mount`, `umount`, `df`, `du`, `sync`
- **ネットワーク（rcS が触る領域）**：
	- 低レベル：`ip`（BusyBox の `ip` が有効なら）, もしくは `ifconfig`/`route`
	- 名前解決：`cat`/`echo`（`/etc/resolv.conf` 書き換え）
	- 疎通：`ping`
- **遠隔ログイン系**：`telnetd`（standalone 想定）
- **時刻**：`date`（+ `ntpd` 等を使うならそれ）

#### 差し替え（自作コマンド化）するときの実務メモ

- BusyBox は「1バイナリ + symlink」で applet を提供することが多いので、差し替えは
	- `PATH` の前に自作実装を置く（例：`/umu_bin` の設計方針に合わせる）
	- 既存 symlink を自作バイナリへ差し替える
	- `rcS` / `inittab` が呼んでいる名前を自作名へ変更する
	のいずれかになります。
- 置き換えは **1コマンドずつ**やるのが安全です（`mount` や `sh` のような基盤を早期に替えると、切り分けが難しくなります）。

