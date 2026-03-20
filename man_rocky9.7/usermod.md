---
title: "usermod(8) ユーザー属性変更コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# usermod(8) — ユーザー属性変更（実用）

`usermod` は既存ユーザーの属性（グループ、シェル、ホーム、有効期限など）を変更する管理者向けコマンドです。

## まず結論：よく使うコマンド

```bash
# コメント（用途/氏名など）を変更
sudo usermod -c "Alice (app operator)" alice

# ログインシェルを変更
sudo usermod -s /bin/bash alice

# 主グループを変更（グループは事前に存在が必要）
sudo usermod -g developers alice

# 補助グループを「指定したリストに置き換える」（空白なしのカンマ区切り）
# 注意：リストに無いグループから外れる
sudo usermod -G wheel,developers alice

# ホームディレクトリを変更（必要に応じて内容を移動）
sudo usermod -d /srv/home/alice -m alice

# アカウント有効期限を設定（YYYY-MM-DD）
sudo usermod -e 2026-12-31 alice

# パスワードをロック/解除
sudo usermod -L alice
sudo usermod -U alice

# ログイン名を変更（他は変えない。ホーム名などは別途検討）
sudo usermod -l alice2 alice
```

## SYNOPSIS（書式）

```text
usermod [options] login
```

## よく使うオプション（運用で効く範囲）

- `-c comment`
  - コメント（GECOS）を変更
- `-s shell`
  - ログインシェルを変更
- `-g initial_group`
  - 主グループを変更
- `-G group1,group2`
  - 補助グループを指定（カンマ区切り、空白なし）
  - 指定リストに無いグループから外れる点に注意
- `-d home_dir [-m]`
  - ホームディレクトリを変更
  - `-m` を付けると中身を新ホームへ移動（新ホームが無ければ作成）
- `-e expire_date`
  - 有効期限（`YYYY-MM-DD`）
- `-l login_name`
  - ログイン名を変更（ホーム名などは自動では揃わないことがある）
- `-L` / `-U`
  - パスワードのロック / アンロック（`-p` と同時に使えない）
- `-u uid [-o]`
  - UID を変更（ログイン中は不可。対象プロセスが動いていないことが前提）

## 事故りやすい点（短く）

- `-G` は「追加」ではなく「置き換え」になり得る
  - 既存の所属グループが外れないように、事前に現在の所属を確認してから実行します
- ログイン名（`-l`）変更は影響が広い
  - ホームディレクトリ名、所有ファイル、設定、運用手順の参照名などを別途揃える必要があります
- UID 変更は特に注意
  - ホーム直下以外のファイル所有者は手作業で揃える必要が出ます

## 参考：関係ファイル

- `/etc/passwd`：ユーザーアカウント情報
- `/etc/shadow`：パスワード等の保護情報
- `/etc/group`：グループ情報

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# usermod は通常 shadow-utils パッケージに含まれます
rpm -q shadow-utils

usermod --help
man usermod
```
