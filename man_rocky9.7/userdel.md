---
title: "userdel(8) ユーザー削除コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# userdel(8) — ユーザー削除

`userdel` はユーザーアカウントを削除する管理者向けコマンドです。
削除は `/etc/passwd` `/etc/shadow` `/etc/group` のエントリを対象に行われます。

## まず結論：よく使うコマンド

```bash
# ユーザーだけ削除（ホーム等は残る）
sudo userdel alice

# ホームディレクトリとメールスプールも削除
sudo userdel -r alice
```

## SYNOPSIS（書式）

```text
userdel [-r] login
```

## よく使うオプション

- `-r`
  - ホームディレクトリとユーザーのメールスプールも削除
  - ただし、ホーム以外に存在する当該ユーザー所有ファイルは自動では消えません

## 事故りやすい点（短く）

- 対象ユーザーがログイン中だと削除できない
  - 先に当該ユーザーの実行中プロセスを止める必要があります
- `-r` を付けても「ホーム以外」のファイルは残る
  - 影響範囲を見落とさないように、削除前後で所有ファイルの棚卸しを検討します

## 参考：関係ファイル

- `/etc/passwd`：ユーザーアカウント情報
- `/etc/shadow`：パスワード等の保護情報
- `/etc/group`：グループ情報

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# userdel は通常 shadow-utils パッケージに含まれます
rpm -q shadow-utils

userdel --help
man userdel
```
