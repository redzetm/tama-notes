---
title: "journalctl(1) systemd journal ログ調査実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# journalctl(1) — systemd journal の検索/追跡（実用）

`journalctl` は systemd-journald が収集したログ（journal）を検索・追跡するコマンドです。

運用では、まず `systemctl status UNIT` で「状態 + 直近」を見て、その後に `journalctl -u UNIT ...` で原因箇所を掘るのが基本形です。

## まず結論：よく使うコマンド

```bash
# まずはこれ：対象ユニットの「今回の起動(boot)」の末尾へ
sudo journalctl -u nginx -b -e

# 追いかける（tail -f 相当）
sudo journalctl -fu nginx

# 直近 N 行だけ（まず軽く）
sudo journalctl -u nginx -b -n 200 --no-pager

# 前回起動（再起動前後の比較に強い）
sudo journalctl -u nginx -b -1 -e

# 重要度で絞る（err以上）
sudo journalctl -u nginx -b -p err..alert --no-pager

# 時刻で絞る（インシデントの時間軸）
sudo journalctl -u nginx --since "2026-03-21 10:00" --until "2026-03-21 11:00" --no-pager
sudo journalctl -u nginx --since "1 hour ago" --no-pager

# 正規表現で検索（PCRE2）
sudo journalctl -u nginx -b -g "error|fail|timeout" --no-pager

# kernel ログだけ（dmesg 相当。暗黙に -b）
sudo journalctl -k -b -e

# 起動（boot）一覧を出して、どの -b を見るか決める
journalctl --list-boots
```

## SYNOPSIS（書式）

```text
journalctl [OPTIONS...] [MATCHES...]
```

- オプション無しで実行すると、journal 全体を古い順に出します（量が多いので運用では避けがち）
- `MATCHES` は `FIELD=VALUE` 形式（例：`_SYSTEMD_UNIT=sshd.service`）です

## フィルタの基本（まずここ）

### `-u UNIT`：ユニット単位で見る（運用の主軸）

```bash
sudo journalctl -u sshd -b -e
sudo journalctl -u sshd.service -b -e
```

- `-u` は内部的に `_SYSTEMD_UNIT=...` など複数条件へ展開されるため、単純な `FIELD=VALUE` より「それっぽいログ」を拾いやすいです
- 複数指定もできます

```bash
sudo journalctl -b -u sshd -u firewalld --no-pager
```

### `-b`：boot（起動単位）で絞る（ノイズ除去）

```bash
# 今回の起動
sudo journalctl -u sshd -b -e

# 1つ前の起動
sudo journalctl -u sshd -b -1 -e
```

`-b` は `_BOOT_ID=` で絞るイメージです。
「昔の同じエラー」を掘って時間を溶かしやすいので、まず `-b` を付ける癖が安全です。

## 追跡（tail -f 相当）

```bash
# 追跡（follow）
sudo journalctl -fu nginx

# 追跡しつつ最初は過去ログを全部出したいなら
sudo journalctl -fu nginx --no-tail
```

## 行数/末尾ジャンプ（重い時の型）

```bash
# 直近だけ
sudo journalctl -u nginx -b -n 200 --no-pager

# 末尾へ（pager 前提。less の機能に依存）
sudo journalctl -u nginx -b -e
```

注意：`-e` は pager（通常 less）前提で「末尾へジャンプ」です。
ログ採取で pager を切る場合は、`-n` + `--no-pager` の方が扱いやすいです。

## 重要度（priority）で絞る

```bash
# err..alert（エラー以上）
sudo journalctl -u nginx -b -p err..alert --no-pager

# warning..alert（警告以上）
sudo journalctl -b -p warning..alert --no-pager
```

- `-p` は syslog の優先度（0..7 / emerg..debug）です
- 1つ指定なら「そのレベル以上（より重要）」が出ます

## 時刻で絞る（since/until）

```bash
sudo journalctl -u nginx --since "2026-03-21 10:00" --until "2026-03-21 11:00" --no-pager
sudo journalctl -u nginx --since "today" --no-pager
sudo journalctl -u nginx --since "yesterday" --no-pager
sudo journalctl -u nginx --since "now" --no-pager
```

相対時刻（例：`"-1h"` 的なもの）も指定できます。厳密な書式は `man systemd.time`。

## 検索（grep）

```bash
# MESSAGE= フィールドに対する PCRE2 正規表現
sudo journalctl -u nginx -b -g "timeout|refused|failed" --no-pager

# 小文字だけのパターンは既定で大文字小文字を無視する
# 必要なら --case-sensitive で制御できる
```

## 出力形式（見やすさ/コピペ/機械処理）

```bash
# 見やすい：ISO 8601
sudo journalctl -u nginx -b -o short-iso --no-pager

# since/until と同じ形式で時刻が出る（short-full）
sudo journalctl -u nginx -b -o short-full --no-pager

# メッセージだけ（メタ情報なし）
sudo journalctl -u nginx -b -o cat --no-pager

# JSON（機械処理）
sudo journalctl -u nginx -b -o json --no-pager
sudo journalctl -u nginx -b -o json-pretty --no-pager

# どのユニットのログかを頭に出したい（テンプレート @ インスタンス等で便利）
sudo journalctl -b -o with-unit --no-pager
```

補足：端末出力は既定で pager に流れ、長い行は画面幅で見切れます。
- pager を止めたい：`--no-pager`
- hostname を消したい：`--no-hostname`
- 省略を抑えたい：`--full` / `--no-full`（挙動の詳細は `man journalctl`）

## system / user journal（見えない時の切り分け）

```bash
# システムサービス + kernel（既定で見える範囲ならこれで足りることが多い）
sudo journalctl --system -b -n 200 --no-pager

# ユーザーサービス（--user は永続ログが有効でないと期待通り動かないことがある）
journalctl --user -b -n 200 --no-pager
```

権限メモ：
- 既定では root 以外は「システム全体の journal」が読めないことがあります
- `systemd-journal` / `adm` / `wheel` の各グループに属していると読める範囲が広がることがあります（ディストリの方針依存）

## ディスク使用量・掃除（ログ肥大が疑わしい時）

```bash
# 使用量
journalctl --disk-usage

# ローテーション
sudo journalctl --rotate

# 古いログを削る（例：7日より古いもの）
sudo journalctl --vacuum-time=7d

# サイズ上限（例：合計 1G まで）
sudo journalctl --vacuum-size=1G
```

注意：監査要件がある環境では vacuum を勝手にやらない方が良い場合があります。

## 事故りやすい点（短く）

- `systemctl status` の10行だけで結論を出そうとして詰まる
  - 原因究明は `journalctl -u UNIT -b ...` が本番です

- `-b` を付けずに昔のエラーを掘ってしまう
  - まず `-b`、必要なら `--list-boots` で対象起動を決めます

- pager（less）で止まったように見える
  - 収集用途は `--no-pager` を付けます

- `-x`（catalog）は便利だが、貼り付け用途に注意
  - バグ報告に添付するログでは `-x` を避けるのが推奨です（man 記載）

- ログには機密が混ざりうる
  - 貼る/共有する前にトークン・鍵・パスワードっぽい行を必ず点検します

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

journalctl --version
systemctl --version

man journalctl
man systemd-journald.service
man systemd.time
man systemd.journal-fields
```
