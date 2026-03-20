---
title: "firewall-cmd(1) firewalld 操作コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-20
---


# firewall-cmd(1) — firewalld の操作（runtime/permanent）

`firewall-cmd` は firewalld のコマンドライン・クライアントです。
**ランタイム設定（runtime）**と**永続設定（permanent）**が分かれているのが最大の特徴で、運用の事故ポイントでもあります。

- runtime：いま動いている firewall の状態（再起動や reload で消える/変わる）
- permanent：設定ファイルとして残る状態

このノートは「運用で迷わず変更できる」ことを目的に、よく使うパターンだけを整理します。

## まず結論：よく使うコマンド

```bash
# 動いてる？
sudo firewall-cmd --state

# 今の有効ゾーン（どのIFがどのzoneか）
sudo firewall-cmd --get-active-zones

# デフォルトゾーン
sudo firewall-cmd --get-default-zone

# いまのゾーンの設定を丸ごと確認（サービス/ポート/rich rule等）
sudo firewall-cmd --list-all

# SSH を許可（runtime）
sudo firewall-cmd --add-service=ssh

# 8080/tcp を許可（runtime）
sudo firewall-cmd --add-port=8080/tcp

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
2) `--reload` で runtime に反映

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

## よく使う変更（サービス/ポート）

### サービス（定義済みのまとまり）

```bash
# 追加/削除/確認
sudo firewall-cmd --add-service=ssh
sudo firewall-cmd --remove-service=ssh
sudo firewall-cmd --query-service=ssh

# 一覧
sudo firewall-cmd --list-services

# 永続としてやるなら --permanent
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --reload
```

サービスは `--get-services` で「どんな名前が使えるか」を確認してから使うと安全です。

### ポート（直接許可）

```bash
# 追加/削除/確認
sudo firewall-cmd --add-port=8080/tcp
sudo firewall-cmd --remove-port=8080/tcp
sudo firewall-cmd --query-port=8080/tcp

# 一覧
sudo firewall-cmd --list-ports

# 永続
sudo firewall-cmd --permanent --add-port=8443/tcp
sudo firewall-cmd --reload
```

## ゾーン（zone）の扱い

ゾーンは「どのインターフェイス/ソースにどのルールを適用するか」をまとめる単位です。

```bash
# どのIFがどのzoneか
sudo firewall-cmd --get-active-zones

# 特定IFが属するゾーン
sudo firewall-cmd --get-zone-of-interface=eth0

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

`--get-log-denied` / `--set-log-denied=<value>` で、拒否ログの出し方を調整できます。

```bash
sudo firewall-cmd --get-log-denied
sudo firewall-cmd --set-log-denied=all
```

ログの実体は `journalctl -u firewalld -b` などで追います。

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
