---
title: "chown(1) 所有者/グループ変更（-R/--from/symlink）実用ノート（Rocky Linux 9.7）"
date: 2026-03-24
---

# chown(1) — 所有者（owner）/グループ（group）を変更

`chown` は、ファイル/ディレクトリの **所有者（ユーザー）とグループ**を変更します。

- 「root が管理する場所にファイルを配置した」
- 「サービス用ユーザー（例: nginx）に所有権を寄せたい」
- 「ディレクトリ配下をまとめて chown したい」

…のような運用で頻出です。

## まず結論：よく使うコマンド

```bash
# 所有者だけ変更
chown appuser file

# 所有者+グループ（OWNER:GROUP）
chown appuser:appgroup file

# グループだけ変更（OWNER を省略して :GROUP）
chown :appgroup file
# （この用途は chgrp appgroup file でも同じ）

# 再帰（ディレクトリ配下を一括）
chown -R appuser:appgroup /srv/app

# “変更が起きたものだけ”表示（ログがうるさくなりにくい）
chown -cR appuser:appgroup /srv/app

# 処理した全ファイルを表示（デバッグ）
chown -vR appuser:appgroup /srv/app

# 参照ファイルと同じ所有者/グループに揃える
chown --reference=/srv/app/.owner_ref /srv/app/config.yml

# 期待する現在の所有者/グループのときだけ変更（レースや誤爆の軽減）
chown --from=olduser:oldgroup appuser:appgroup /srv/app/config.yml

# シンボリックリンク自体に適用（対応FS/カーネルでのみ意味がある）
chown -h appuser:appgroup symlink

# 誤爆対策：/ に対する -R を失敗させる
chown -R --preserve-root appuser:appgroup /
```

## SYNOPSIS（書式）

`info '(coreutils) chown invocation'` と `chown --help` より：

```text
chown [OPTION]... [OWNER][:[GROUP]] FILE...
chown [OPTION]... --reference=RFILE FILE...
```

## OWNER / GROUP の指定ルール（ここだけ覚える）

`[OWNER][:[GROUP]]` の形で指定します（空白は入れない）。

- `OWNER`
  - 所有者だけ変更（グループは変更しない）
- `OWNER:GROUP`
  - 所有者とグループを両方変更
- `OWNER:`
  - 所有者を `OWNER` に変更し、グループを「OWNER のログイングループ」に変更
- `:GROUP`
  - グループだけ変更（この場合 `chgrp` と同じ）

補足（運用の落とし穴）：

- `OWNER` や `GROUP` は **名前**だけでなく **数値ID**（uid/gid）でも指定できます
- 「数値に見える名前」と区別したいときは、`+` を先頭に付けて数値IDを明示できます（例: `chown +1000 file`）
- 古いスクリプトで `OWNER.GROUP` を見かけることがありますが、GNU `chown` では互換のために残っているだけで、移植性が低く将来的に外れる可能性があるため避けるのが無難です（`:` を使う）

## 主要オプション（運用で使う分だけ）

`chown --help` / `info '(coreutils) chown invocation'` より。

- `-R, --recursive`
  - ディレクトリ配下を再帰的に変更（誤爆しやすいので対象パスは二重確認）
- `-c, --changes`
  - 変更が起きたファイルだけ出力
- `-v, --verbose`
  - 処理した各ファイルを出力
- `-f, --silent, --quiet`
  - エラーメッセージを抑制（監視/運用では原因追跡が難しくなるので多用注意）
- `--reference=RFILE`
  - `RFILE` の所有者/グループに揃える
- `--from=CURRENT_OWNER:CURRENT_GROUP`
  - 現在の所有者/グループが一致した場合のみ変更（安全側）
- `--preserve-root`
  - `-R` のとき `/` を対象にすると失敗させる

## シンボリックリンク（--dereference と -h）

`chown` はシンボリックリンクに対して、どちらを変更するかが重要です。

- デフォルトは `--dereference`
  - **リンク自体ではなく参照先**に対して `chown` する
- `-h, --no-dereference`
  - **リンク自体**に適用（ただしOS/FSが `lchown(2)` を提供しないと失敗することがあります）

### -R と組み合わせた時の挙動（-H/-L/-P）

`-R` と併用すると「ディレクトリ探索中に symlink を辿るか」が追加で効きます。

- `-P`：一切辿らない（デフォルト）
- `-H`：コマンドライン引数が「ディレクトリへの symlink」のときだけ辿る
- `-L`：探索中に遭遇した「ディレクトリへの symlink」もすべて辿る

注意：`info` にある通り、`-R` と `--dereference` / `-L` の組み合わせは、探索中に攻撃者が symlink を差し込むことで意図しない対象に作用しうるため、権限が強い状況（root での運用）では特に慎重に扱います。

## 事故りポイント（chown あるある）

- `chown -R` の対象パス誤りは被害が大きい
  - まず `ls` や `find` で対象を確認してから実行する
  - `/` が混ざる可能性がある操作では `--preserve-root` を検討
- 期待した通りに権限が変わらない
  - 一般ユーザーは、所有者変更はできず（通常は root/CAP_CHOWN が必要）、グループも所属しているグループに制限されることがあります（挙動はOS依存）
- setuid/setgid ビットが落ちることがある
  - `info` にある通り、`chown` により setuid/setgid がクリアされるかはシステム依存です。変更後に `ls -l` 等で確認する

## 確認コマンド（所有者/グループを素早く見る）

```bash
# 名前で見る（例：owner:group file）
stat -c '%U:%G %n' file

# 数値IDで見る（uid:gid file）
stat -c '%u:%g %n' file

# 一覧（長い形式）
ls -l file
```

## 終了コード（exit status）

`info '(coreutils) chown invocation'` より：

- `0`：成功
- `0` 以外：失敗

失敗時は、標準エラー出力のメッセージ（例：ユーザー/グループが存在しない、権限不足、ファイルが無い）を併せて確認します。

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
chown --version
chown --help
info '(coreutils) chown invocation'
```
