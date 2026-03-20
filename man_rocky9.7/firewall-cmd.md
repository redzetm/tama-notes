---
title: "firewall-cmd(1) firewalld 操作コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---


# firewall-cmd(1) — firewalld の操作（runtime/permanent）

`firewall-cmd` は firewalld のコマンドライン・クライアントです。
**ランタイム設定（runtime）**と**永続設定（permanent）**が分かれているのが最大の特徴で、運用の事故ポイントでもあります。

- runtime：いま動いている firewall の状態（再起動や reload で消える/変わる）
- permanent：設定ファイルとして残る状態（`--reload` や再起動後に runtime に反映される元）

このノートは「運用で迷わず変更できる」ことを目的に、よく使うパターンを整理します。

## まず結論：よく使うコマンド

```bash
# 動いてる？
sudo firewall-cmd --state

# 今の有効ゾーン（どのIF/sourceがどのzoneか）
sudo firewall-cmd --get-active-zones

# デフォルトゾーン
sudo firewall-cmd --get-default-zone

# いまのゾーンの設定を丸ごと確認（サービス/ポート/rich rule等）
sudo firewall-cmd --list-all

# SSH を許可（runtime）
sudo firewall-cmd --add-service=ssh

# 8080/tcp を許可（runtime）
sudo firewall-cmd --add-port=8080/tcp

# rich rule 一覧（runtime）
sudo firewall-cmd --list-rich-rules

# 変更を永続化（runtime → permanent）
# 「とりあえず通す」→「問題なければ永続化」の流れで使う
sudo firewall-cmd --runtime-to-permanent

# 永続設定として追加（--permanent）してから反映（reload）
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --reload

# ルール反映（状態維持）
sudo firewall-cmd --reload

# ルール反映（状態を捨てる：強め）
sudo firewall-cmd --complete-reload

# ゾーンを指定して操作（例：public）
sudo firewall-cmd --zone=public --list-all
sudo firewall-cmd --zone=public --add-port=8443/tcp
```

## SYNOPSIS（書式）

```text
firewall-cmd [OPTIONS...]
```

多くの操作は「対象ゾーン」を次で選びます。

- `--zone=<zone>` を付ける：そのゾーンに対して操作
- 省略：デフォルトゾーンに対して操作（`--get-default-zone` で確認）

また、永続設定に触る時は `--permanent` を付けます。

## 重要：runtime と permanent の扱い（まずここ）

### よくある安全な流れ（おすすめ）

1) runtime で変更（影響確認）
2) 問題なければ `--runtime-to-permanent` で永続化

```bash
sudo firewall-cmd --add-port=8080/tcp
sudo firewall-cmd --list-all

# 問題なければ永続化
sudo firewall-cmd --runtime-to-permanent
```

### もう1つの流れ（設定管理寄り）

1) `--permanent` で永続設定を変更
2) `--reload` で runtime に反映（=永続が runtime の新しい元になる）

```bash
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --reload
```

どちらでも良いですが、
「いきなり永続設定を変えて reload して通信断」になりうる環境では runtime 先行が安全です。

## 状態確認（まずは現状を読む）

```bash
sudo firewall-cmd --state
sudo firewall-cmd --get-default-zone
sudo firewall-cmd --get-active-zones
sudo firewall-cmd --get-zones
sudo firewall-cmd --get-services

# いまのゾーン（デフォルト）
sudo firewall-cmd --list-all

# 全ゾーンを全部ダンプ（調査向け。量が増えることがある）
sudo firewall-cmd --list-all-zones
```

## よく使う変更（サービス/ポート/rich rule）

### サービス（定義済みのまとまり）

```bash
# 追加/削除/確認（runtime）
sudo firewall-cmd --add-service=ssh
sudo firewall-cmd --remove-service=ssh
sudo firewall-cmd --query-service=ssh

# 一覧（runtime）
sudo firewall-cmd --list-services

# 永続としてやるなら --permanent + reload
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --reload
```

サービスは `--get-services` で「どんな名前が使えるか」を確認してから使うと安全です。

### ポート（直接許可）

```bash
# 追加/削除/確認（runtime）
sudo firewall-cmd --add-port=8080/tcp
sudo firewall-cmd --remove-port=8080/tcp
sudo firewall-cmd --query-port=8080/tcp

# 一覧（runtime）
sudo firewall-cmd --list-ports

# 永続として追加するなら --permanent + reload
sudo firewall-cmd --permanent --add-port=8443/tcp
sudo firewall-cmd --reload
```

### rich rule（条件付きルール：IP制限/ログ付け等）

service/port だけでは表現しにくい「送信元IPで制限したい」「拒否ログを残したい」などは rich rule を使います。

基本コマンドはこの4つです（`--zone=` を付けないとデフォルトゾーンに入る点に注意）。

```bash
# 一覧（改行区切り）
sudo firewall-cmd --zone=public --list-rich-rules

# 追加
sudo firewall-cmd --zone=public --add-rich-rule='rule family="ipv4" source address="203.0.113.10/32" service name="ssh" accept'

# 確認（--query-* は「ある→0 / ない→1」の戻り値が基本）
sudo firewall-cmd --zone=public --query-rich-rule='rule family="ipv4" source address="203.0.113.10/32" service name="ssh" accept'

# 削除（文字列は“完全一致”が必要なので、まず list の出力をコピペするのが安全）
sudo firewall-cmd --zone=public --remove-rich-rule='rule family="ipv4" source address="203.0.113.10/32" service name="ssh" accept'
```

#### 例1: allowlist（特定IPだけSSHを許可）

考え方は「ゾーンから `ssh` サービスを外して全閉じ」→「rich rule で特定IPだけ accept」です。

```bash
# いまの状態を確認
sudo firewall-cmd --zone=public --list-all

# まず ssh をサービス許可から外す（=デフォルトの開放を止める）
sudo firewall-cmd --zone=public --remove-service=ssh

# allowlist: 特定IPだけ accept
sudo firewall-cmd --zone=public --add-rich-rule='rule family="ipv4" source address="203.0.113.10/32" service name="ssh" accept'

# 確認
sudo firewall-cmd --zone=public --list-all
sudo firewall-cmd --zone=public --list-rich-rules
```

永続化はどちらかで行います。

- runtime で動作確認後に永続化したい: `sudo firewall-cmd --runtime-to-permanent`
- 永続として積みたい: `sudo firewall-cmd --permanent ...` を同内容で実行して `sudo firewall-cmd --reload`

#### 例2: blocklist（特定IPだけ拒否 + ログ）

ゾーン側で `ssh` は通常通り開けておきつつ、「このIPだけ drop/reject」します。
ログを付けたい場合は rich rule 側で `log ...` を併用します。

```bash
# 例: 198.51.100.77 からの SSH だけログして drop
sudo firewall-cmd --zone=public --add-rich-rule='rule family="ipv4" source address="198.51.100.77/32" service name="ssh" log prefix="DROP_SSH " level="info" drop'

# 確認
sudo firewall-cmd --zone=public --list-rich-rules
```

ログは `journalctl -u firewalld -b`、あるいはシステムのログ設定に応じて追跡します。

## ゾーン（zone）の扱い

ゾーンは「どのインターフェイス/ソースにどのルールを適用するか」をまとめる単位です。

```bash
# どのIF/sourceがどのzoneか
sudo firewall-cmd --get-active-zones

# 特定IFが属するゾーン
sudo firewall-cmd --get-zone-of-interface=eth0

# 特定sourceが属するゾーン
sudo firewall-cmd --get-zone-of-source=203.0.113.0/24

# ゾーンを明示して表示
sudo firewall-cmd --zone=public --list-all
```

注意：`--zone=` を付け忘れると「デフォルトゾーン」に入ってしまい、思った場所が開かない/閉じない原因になります。

## reload / complete-reload

```bash
# 設定を再読込し、状態情報を維持
sudo firewall-cmd --reload

# 設定を再読込し、状態情報を失う（強い）
sudo firewall-cmd --complete-reload
```

運用では基本 `--reload` を使い、
どうしても挙動が変で状態を捨てたい場合に `--complete-reload` を検討します。

## よくある運用スニペット

### 「いま何が開いてる？」を素早く把握

```bash
sudo firewall-cmd --get-active-zones
sudo firewall-cmd --list-all
```

### 変更の差分確認（runtime と permanent を見比べる）

```bash
# runtime
sudo firewall-cmd --list-all

# permanent
sudo firewall-cmd --permanent --list-all
```

### 変更を永続化する（runtime → permanent）

```bash
sudo firewall-cmd --runtime-to-permanent
```

## ログ（deny のログ）

`--get-log-denied` / `--set-log-denied=<value>` で、drop/reject の直前にログを挿入する設定ができます。

- 値: `all` / `unicast` / `broadcast` / `multicast` / `off`
- 注意: `--set-log-denied=...` は「runtime と permanent の両方に効く変更」かつ「reload を伴う」挙動です

```bash
sudo firewall-cmd --get-log-denied

# 例: すべてログ
sudo firewall-cmd --set-log-denied=all

# 例: 無効化
sudo firewall-cmd --set-log-denied=off

# ログを見る（例）
journalctl -u firewalld -b -e
```

## masquerade（NAT）

masquerade は、いわゆる NAT（送信元アドレス変換）で、
ホストが「内側ネットワークの出口」になるような構成で必要になることがあります。

- `firewall-cmd` の `--add-masquerade` は IPv4 向けです（IPv6 は rich language 側で扱う系）
- 有効化すると IP forwarding が暗黙に有効化されます（構成確認は必須）

```bash
# ゾーンで有効化（runtime）
sudo firewall-cmd --zone=public --add-masquerade

# 確認/解除
sudo firewall-cmd --zone=public --query-masquerade
sudo firewall-cmd --zone=public --remove-masquerade

# 永続
sudo firewall-cmd --permanent --zone=public --add-masquerade
sudo firewall-cmd --reload
```

注意：masquerade を有効にする前に、
「どのゾーンで」「どのインターフェイスがそのゾーンか」を必ず確認します。

```bash
sudo firewall-cmd --get-active-zones
sudo firewall-cmd --get-zone-of-interface=eth0
```

## forward-port（ポート転送）

forward-port は「来たポートを別のポート/別の宛先に転送する」設定です。

- `firewall-cmd` の `--add-forward-port` は IPv4 forward-port です（IPv6 は rich language 側で扱う系）
- `toport` だけなら「同一ホスト内で公開ポートを変える」用途に使いやすいです
- `toaddr` を付けると別ホスト転送になり、IP forwarding が暗黙に有効化される挙動です（設計確認推奨）

```bash
# 追加（例：80/tcp → 8080/tcp へ。toportのみ = 同一ホスト内の転送イメージ）
sudo firewall-cmd --zone=public --add-forward-port=port=80:proto=tcp:toport=8080

# 追加（例：443/tcp → 10.0.0.10:8443 へ。toaddrあり = 別ホスト転送）
sudo firewall-cmd --zone=public --add-forward-port=port=443:proto=tcp:toport=8443:toaddr=10.0.0.10

# 一覧/確認/削除
sudo firewall-cmd --zone=public --list-forward-ports
sudo firewall-cmd --zone=public --query-forward-port=port=80:proto=tcp:toport=8080
sudo firewall-cmd --zone=public --remove-forward-port=port=80:proto=tcp:toport=8080

# 永続
sudo firewall-cmd --permanent --zone=public --add-forward-port=port=80:proto=tcp:toport=8080
sudo firewall-cmd --reload
```

注意：forward-port は構成次第で masquerade や OS 側の転送設計が絡みます。
「やりたいこと（同一ホスト内の転送なのか、別ホストへ転送なのか）」を先に整理してから入れるのが安全です。

## source/interface の紐付け（ゾーン割当）

ゾーンは「どこから来た通信に、そのルールを当てるか」を決めるため、
**interface**（NIC）や **source**（送信元アドレス/ネットワーク）をゾーンに紐付けて運用します。

### interface をゾーンに紐付ける

```bash
# 現在の紐付け確認
sudo firewall-cmd --get-active-zones
sudo firewall-cmd --get-zone-of-interface=eth0

# 一覧（そのゾーンに紐付くIF）
sudo firewall-cmd --zone=public --list-interfaces

# 追加（runtime）
sudo firewall-cmd --zone=public --add-interface=eth0

# ゾーン変更（runtime）: remove + add の等価
sudo firewall-cmd --zone=trusted --change-interface=eth0

# クエリ（そのゾーンに属する？）
sudo firewall-cmd --zone=trusted --query-interface=eth0

# 解除（runtime）
sudo firewall-cmd --remove-interface=eth0

# 永続（例）
sudo firewall-cmd --permanent --zone=public --add-interface=eth0
sudo firewall-cmd --reload
```

補足：環境によっては NetworkManager がゾーン割当を管理しており、`--add-interface` 等が「接続のゾーン」を変える方向に働くことがあります。挙動が怪しい時は `--get-active-zones` で観測しながら進めます。

### source（送信元）をゾーンに紐付ける

```bash
# どのゾーン扱いになるか（source → zone）
sudo firewall-cmd --get-zone-of-source=203.0.113.0/24

# 一覧
sudo firewall-cmd --zone=trusted --list-sources

# 追加（runtime）
sudo firewall-cmd --zone=trusted --add-source=203.0.113.0/24

# ゾーン変更（runtime）
sudo firewall-cmd --zone=public --change-source=203.0.113.0/24

# クエリ（そのゾーンに属する？）
sudo firewall-cmd --zone=public --query-source=203.0.113.0/24

# 解除（runtime）
sudo firewall-cmd --remove-source=203.0.113.0/24

# 永続（例）
sudo firewall-cmd --permanent --zone=trusted --add-source=203.0.113.0/24
sudo firewall-cmd --reload
```

### 事故りやすい点（紐付け）

- interface/source を間違ったゾーンに入れると「想定外に開く/閉じる」が起きます
  - 変更前に `--get-active-zones` と `--list-all`（必要なら `--list-all-zones`）で現状を必ず確認します
- `--permanent` の変更は `--reload` しないと runtime に反映されません
- `--zone=` の付け忘れで「デフォルトゾーン」に紐付けられてしまうのが典型です

## 事故りやすい点（短く）

- runtime と permanent を混同する
  - `--permanent` を付けたのに `--reload` していない／runtime だけ変えて再起動で戻る、が典型
- `--zone=` を付け忘れる
  - 期待したゾーンではなくデフォルトゾーンに追加される
- リモート作業で SSH を閉じる
  - `--remove-service=ssh` やゾーン変更は、接続断のリスクがあります
- `--reset-to-defaults` は強い
  - 既定状態に戻す操作なので、実行前に影響範囲を慎重に確認します
- `--panic-on` は「全部落とす」系
  - 非常手段としては有用ですが、実行タイミングを間違えると復旧作業も困難になります

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# パッケージ/バージョン
rpm -q firewalld
firewall-cmd --version

# ヘルプ/マニュアル
firewall-cmd --help
man firewall-cmd

# デーモンの状態とログ
systemctl status firewalld --no-pager
journalctl -u firewalld -b -e
```
