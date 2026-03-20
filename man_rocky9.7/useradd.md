---
title: "useradd(8) ユーザー作成コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# useradd(8) — ユーザー作成

`useradd` は新規ユーザーを作成する管理者向けコマンドです（通常 root 権限が必要）。

- ユーザー情報は `/etc/passwd` `/etc/shadow` `/etc/group` に反映されます
- ホームディレクトリ作成や初期ファイル配置は `/etc/skel` と関連します

注意：パスワード設定は別コマンド（通常 `passwd`）で行います。

## まず結論：よく使うコマンド

```bash
# 典型：ホーム作成 + bash（必要に応じて）
# ※ Rocky では /bin/bash が一般的
sudo useradd -m -s /bin/bash alice
sudo passwd alice

# コメント（氏名/用途）を入れる
sudo useradd -m -c "Alice (app operator)" -s /bin/bash alice

# ホームディレクトリを明示
sudo useradd -m -d /srv/home/alice -s /bin/bash alice

# 主グループを指定（グループは事前に存在が必要）
sudo useradd -m -g developers -s /bin/bash alice

# 補助グループを追加（カンマ区切り。空白を入れない）
sudo useradd -m -G wheel,developers -s /bin/bash alice

# 有効期限つき（YYYY-MM-DD）
sudo useradd -m -e 2026-12-31 -s /bin/bash tempuser

# デフォルト値の確認（表示）
useradd -D
```

## SYNOPSIS（書式）

```text
useradd [options] login
useradd -D [options]
```

- 通常モード：`login` を作る
- `-D`：デフォルト値を表示/更新

## 重要な考え方（運用で迷うポイント）

### 1) `-m` を付けないとホームが作られないことがある

`-m` を付けると「ホームディレクトリが無ければ作る」動作になります。

- `-m`：ホーム作成（必要なら）
- `-k skeleton_dir`：`-m` と一緒の時だけ有効（初期ファイルをどこからコピーするか）
  - 省略時は通常 `/etc/skel`

### 2) パスワードは `-p` ではなく `passwd` を基本にする

`-p passwd` は **暗号化済みパスワード（crypt(3) の返り値）**を渡すオプションです。
平文を渡す用途ではありません。

運用では、作成後に `passwd login` を使うのが安全です。

### 3) グループ指定の使い分け

- `-g initial_group`：主グループ（既存グループが必要）
- `-G group1,group2`：補助グループ（カンマ区切り、空白なし）

### 4) UID の指定は原則不要（必要な時だけ）

- `-u uid`：UID を明示
- `-o`：重複UIDを許可（通常は避ける）

一般に、通常ユーザーは自動採番に任せる方が事故が減ります。

## `-D`（デフォルト値）

`useradd -D` で現在のデフォルトを表示できます。
変更する場合は `-D` と一緒に各オプションを渡します。

```bash
# 現在のデフォルトを表示
useradd -D

# 例：新規ユーザーのデフォルトシェルを指定
sudo useradd -D -s /bin/bash
```

デフォルト値は `/etc/default/useradd` も参照されます。

## 事故りやすい点（短く）

- `-G` はカンマ区切りで、空白を入れない
- `-p` は「暗号化済み」を渡す（平文を渡さない）
- 有効期限 `-e` は `YYYY-MM-DD`
- ホーム作成が必要なら `-m` を付ける（`/etc/skel` から初期ファイルがコピーされる）

## 参考：関係ファイル

- `/etc/passwd`：ユーザーアカウント情報
- `/etc/shadow`：パスワード等の保護情報
- `/etc/group`：グループ情報
- `/etc/default/useradd`：デフォルト情報
- `/etc/skel/`：ホーム初期ファイルの雛形

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# useradd は通常 shadow-utils パッケージに含まれます
rpm -q shadow-utils

useradd --help
man useradd
```
