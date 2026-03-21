---
title: "nmcli(1) NetworkManager 操作コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# nmcli(1) — NetworkManager の操作（状態/接続/デバイス）

`nmcli` は NetworkManager を CLI で操作・照会するコマンドです。
GUI が無いサーバ（headless）でも「今どう繋がっているか」「接続プロファイル（connection）をどう切り替えるか」を扱えます。

このノートは `nmcli` のうち、運用で頻出の確認・切替・復旧に必要な範囲に絞ります。

## まず結論：よく使うコマンド

```bash
# NetworkManager 動いてる？
nmcli -t -f RUNNING general
nmcli general status

# NIC と状態（まず最初に見る）
nmcli device status

# 接続プロファイル一覧（いま active なものだけ）
nmcli connection show --active

# 特定の NIC の詳細（IP/DHCP などを含む）
nmcli device show eth0

# 接続の切替（例：接続名 "My wired connection" を eth0 で up）
nmcli connection up "My wired connection" ifname eth0

# 接続の down（注意点あり。下の「落とし穴」参照）
nmcli connection down "My wired connection"

# 接続ファイルを再読込（手編集した/配布した時の反映）
nmcli connection reload

# リモートで怖い変更は checkpoint で保険（タイムアウトで自動ロールバック）
nmcli device checkpoint --timeout 60 eth0 -- nmcli connection up "My wired connection" ifname eth0

# 疎通状態（NetworkManager 観点）
nmcli networking connectivity
nmcli networking connectivity check
```

## SYNOPSIS（書式）

```text
nmcli [OPTIONS...] {help | general | networking | radio | connection | device | agent | monitor} [COMMAND] [ARGUMENTS...]
```

## 重要な考え方：connection と device

- **connection**: NetworkManager が保存する「接続プロファイル」
  - 例：DHCP 用、固定IP 用、検証用などを切り替えられる
- **device**: 実際のネットワークデバイス（NIC）
  - 例：`eth0` / `ens192` / `wlan0` など

運用ではまず `nmcli device status` で device の全体像を掴み、
次に `nmcli connection show --active` で「どの connection がどの device に載っているか」を確認します。

## 状態確認（最初にやる）

```bash
# 全体状態
nmcli general status

# デバイス一覧
nmcli device status

# active 接続
nmcli connection show --active

# 特定デバイス詳細（長いが強い）
nmcli device show eth0
```

## 典型タスク

### 接続プロファイル一覧・詳細

```bash
# すべて
nmcli connection show

# active のみ
nmcli connection show --active

# 詳細（出力が大きいので multiline が見やすい）
nmcli -p -m multiline connection show "My wired connection"
```

### 接続を up / down する

```bash
# up: 接続プロファイルを有効化
# ifname を付けると「どのNICで上げるか」を明示できる
nmcli connection up "My wired connection" ifname eth0

# down: 接続を無効化
nmcli connection down "My wired connection"
```

- `--wait`（例：`nmcli --wait 10 ...`）で待ち時間を調整できます
- `nmcli connection down` は「その時点の active connection を落とす」操作です
  - device が完全に使えなくなる/自動復旧しない、などの挙動が絡むことがあるので、リモート作業では慎重に

### リモート作業の安全策：checkpoint（自動ロールバック保険）

リモート（SSH 等）でネットワーク設定を触る時は、
切断＝復旧が面倒/物理コンソールが必要、になりがちです。

`nmcli device checkpoint` は、変更前の状態をチェックポイントとして保存し、
**確認されなければ timeout で自動的に元へ戻す**（ロールバックする）仕組みを提供します。

```bash
# eth0 に対して、60秒のチェックポイントを取ってからコマンドを実行する
# コマンド終了後に確認プロンプトが出るので、keep するか戻すかを判断できる
nmcli device checkpoint --timeout 60 eth0 -- \
  nmcli connection up "My wired connection" ifname eth0
```

- まずは `--timeout` を短め（例：30〜120秒）にして試すのが安全です
- 複数 IF が絡むなら `eth0` の代わりに対象 IF を並べます（例：`eth0 ens192`）
- うまくいっても油断せず、別端末から再ログインできるかを確認してから keep します

### 変更（永続）: connection modify

接続プロファイルに対する変更は `nmcli connection modify` で行います。

```bash
# 例：自動接続を切る（接続名を指定して設定変更）
nmcli connection modify "My wired connection" connection.autoconnect no
```

#### 固定IP/DNS/ゲートウェイへ切替（最小例）

「DHCP から固定IPへ」「DNS を明示したい」といった変更は、
対象 connection に対して `ipv4.*` を設定します。

まず、対象 connection 名を特定します（active の一覧を見るのが速いです）。

```bash
nmcli connection show --active
```

次に、例として次のように設定します（値は環境に合わせて差し替え）。

```bash
# 例：固定IP（192.168.1.23/24）、GW（192.168.1.1）、DNS（8.8.8.8/1.1.1.1）
nmcli connection modify "My wired connection" \
  ipv4.method manual \
  ipv4.addresses "192.168.1.23/24" \
  ipv4.gateway "192.168.1.1" \
  ipv4.dns "8.8.8.8 1.1.1.1"

# 反映（接続の張り直し）
nmcli connection down "My wired connection"
nmcli connection up "My wired connection" ifname eth0
```

リモート作業で怖い場合は、上の「反映」部分を checkpoint で包むのが安全です。

```bash
nmcli device checkpoint --timeout 60 eth0 -- \
  nmcli connection up "My wired connection" ifname eth0
```

変更後の確認は、まず `device show` で IP と経路が期待通りかを見ます。

```bash
nmcli device show eth0
```

DHCP に戻す（最小例）は次です。

```bash
nmcli connection modify "My wired connection" ipv4.method auto
nmcli connection down "My wired connection"
nmcli connection up "My wired connection" ifname eth0
```

- `--temporary` を使うと「永続プロファイルではなく一時的に」変更できます
- 何を設定できるかはディストリ/NetworkManager の構成で変わるため、迷ったら `nmcli connection show` のフィールドを見ながら進めます

### 変更（一時）: device modify

「今このデバイスにだけ一時的に」設定を当てるなら `nmcli device modify` を使います。
（再度同じ connection が有効化されると戻ることがあります）

```bash
# 例：一時的な設定変更（項目名は環境依存なので、まずは device show で現状を確認する）
nmcli device show eth0
```

### 接続ファイルを再読込する（手編集したとき）

NetworkManager は接続ファイルの変更を常時監視しないことがあります。
「手で編集したのに nmcli が反映しない」時は次を使います。

```bash
# 接続ファイル全体を再読込
nmcli connection reload

# 特定ファイルを load（複数指定可）
# nmcli connection load /path/to/connectionfile
```

### NetworkManager 自体の reload（conf/DNS 周り）

`nmcli general reload` は「NetworkManager の設定や DNS 周りの更新」を同期的に行えます。

```bash
# 何を reload するかを flags で指定できる
nmcli general reload conf
nmcli general reload dns-rc
nmcli general reload dns-full
```

## スクリプト向け出力（運用で超重要）

`nmcli` は人間向け出力と機械向け出力を切り替えられます。

```bash
# 1行1レコードで扱いやすくする
nmcli -t device status

# 列（fields）を絞る
nmcli -t -f DEVICE,TYPE,STATE device status

# 値だけ取り出す（1行1値）
nmcli -g RUNNING general
```

注意点：`nmcli` はロケールで表示言語が変わります。
スクリプトで解析するなら次のように固定するのが安全です。

```bash
LC_ALL=C nmcli -t device status
```

## 落とし穴（事故りやすい点）

- 省略コマンド（短縮形）は将来ユニークでなくなる可能性がある
  - 長期運用のスクリプトでは、コマンドやプロパティ名を省略せずに書くのが安全
- ロケールで出力が変わる
  - スクリプトでは `LC_ALL=C` を推奨
- `--ask` は対話入力になる
  - 自動化（cron/Ansible等）では避ける
- `--show-secrets` は秘密情報を出力する
  - 端末ログや監視に残り得るので、トラブル対応時も取り扱い注意

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# バージョン
nmcli --version

# NetworkManager の状態
systemctl status NetworkManager --no-pager

# ヘルプ/マニュアル
nmcli --help
man nmcli
```
