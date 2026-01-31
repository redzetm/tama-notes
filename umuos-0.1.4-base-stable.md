---
title: "再現可能な自作OS UmuOS-0.1.4-base-stable"
---

# 再現可能な自作OS UmuOS-0.1.4-base-stable

UmuOS-0.1.4-base-stable は、UmuOS のユーザーランド開発へ進むための「ベースOS」です。
0.1.3 系の最小OSを維持しつつ、LAN 内端末からゲストへログインできるように **BusyBox の telnetd（TCP/23）** を追加した版になります。

この版は「起動して終わり」ではなく、
**起動 → 観測（ログ）→ 切り分け** をやりやすくするために、手順・固定値・成果物をなるべく固定しているのが特徴です。

---

## 位置づけ（0.1.3との差分）

UmuOS の文脈では “telnet” が 2 種類あります。

- **ttyS1 の telnet（ホストローカル用途）**
	- QEMU の TCP シリアル（例：`127.0.0.1:5555`）に `telnet` で接続して、ゲストの `ttyS1` を操作する。
- **telnetd（LANログイン用途）**
	- ゲスト側で `telnetd -p 23 -l /bin/login` を起動し、LAN から `192.168.0.202:23` に接続してログインする。

UmuOS-0.1.4-base-stable は **この 2 つを同時に成立** させる前提で組んでいます。

---

## 特徴（固定していること）

特徴を、設計書から抜粋してまとめます。

- 0.1.3 相当の機能を壊さない（`switch_root` / ttyS0+ttyS1 同時ログイン / `/logs/boot.log`）
- 追加は「ゲスト telnetd（TCP/23）」のみ（SSH は入れない）
- ビルドと受入を分離（ビルド：Ubuntu 24.04、起動・受入：RockyLinux 9.7）
- 固定IPでのネットワーク初期化（ゲスト `192.168.0.202/24`、GW `192.168.0.1`）
- BusyBox を静的リンクで構成し、必要 applet を明示（`telnetd` / `login` / `ip` / `nc`）
- ファイル転送は `nc` に固定（ポート `12345`、ゲスト受信→Ubuntu送信のフローに固定）

---

## 全体像（ざっくりアーキテクチャ）

ホスト・ゲストの責務はこんな感じ。

- ホスト（RockyLinux 9.7）
	- `br0` + `tap-umu`（手動作成）でゲストを LAN に L2 参加させる
	- QEMU/KVM を CLI 起動し、ログは `script` で必ず採取する
- ゲスト（UmuOS-0.1.4-base-stable）
	- kernel：Linux 6.18.1
	- initramfs：起動初期化と `switch_root`
	- 永続 rootfs：ext4（`disk/disk.img`）
	- ユーザーランド：BusyBox
	- `rcS` が network 初期化 + `telnetd` 起動 + `/logs/boot.log` 追記を担う

---

## 使い方（最短のイメージ）

詳細は設計書に譲りつつ、「何をすれば触れる状態になるか」だけを短く紹介です。

1. Ubuntu で kernel / BusyBox / initramfs / ext4 / ISO をビルドして成果物を作る
2. Rocky に成果物一式を配置して、`umuOSstart.sh` で起動する
3. 観測：ttyS0/ttyS1 のログと `/logs/boot.log` を見る
4. LAN から `192.168.0.202:23` に telnet して `root` / `tama` でログインする
5. 必要なら `nc` でファイル転送して疎通を確認する

（切り分け用途として）ネット無しで起動だけしたい場合は `./umuOSstart.sh --net none` を使います。

---

## 受入（合格条件の目安）

「最低限ここまで行けばOK」というチェック。

- 0.1.3 互換
	- ttyS0 でログインできる（root/tama）
	- ttyS1（TCPシリアル）でもログインできる（同時ログイン）
	- ゲスト `/logs/boot.log` が追記される
- 0.1.4 追加（telnetd）
	- ゲスト `eth0` に `192.168.0.202/24` が入る
	- `default via 192.168.0.1` が入る
	- LAN から `192.168.0.202:23` に接続できる（root/tama）
- 追加（nc転送）
	- ゲスト受信（`nc -l -p 12345 > payload.bin`）
	- Ubuntu送信（`nc 192.168.0.202 12345 < payload.bin`）

---

## セキュリティ上の注意

telnet は平文なので、前提として **自宅 LAN 内に限定**する運用です。
外に出すなら（この版のスコープ外ですが）SSH へ置換するのが自然です。

---

## リンク

- [UmuOS リポジトリ（umu_project）](https://github.com/redzetm/umu_project)
- UmuOS-0.1.4-base-stable ディレクトリ：
  - [UmuOS-0.1.4-base-stable ディレクトリ](https://github.com/redzetm/umu_project/tree/main/UmuOS-0.1.4-base-stable)
- 設計書・ノート（docs）
  - [基本設計書](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-%E5%9F%BA%E6%9C%AC%E8%A8%AD%E8%A8%88%E6%9B%B8.md)
  - [詳細設計書](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-%E8%A9%B3%E7%B4%B0%E8%A8%AD%E8%A8%88%E6%9B%B8.md)
  - [実装ノート](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-%E5%AE%9F%E8%A3%85%E3%83%8E%E3%83%BC%E3%83%88.md)
  - [使い方](https://github.com/redzetm/umu_project/blob/main/UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-%E4%BD%BF%E3%81%84%E6%96%B9.md)

