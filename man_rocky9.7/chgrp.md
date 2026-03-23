---
title: "chgrp(1) グループ変更（-R/--reference/symlink）実用ノート（Rocky Linux 9.7）"
date: 2026-03-24
---

# chgrp(1) — グループ（group ownership）を変更

`chgrp` は、ファイル/ディレクトリの **グループ（group）**を変更します。

- 「共有ディレクトリ配下を同じグループに揃えたい」
- 「サービスが読むファイルの group を合わせたい」
- 「`chown :group` と同じ用途を、より意図が伝わる形で書きたい」

…のような運用で使います。

## まず結論：よく使うコマンド

```bash
# グループを変更
chgrp staff file

# 再帰（ディレクトリ配下を一括）
chgrp -R staff /srv/shared

# “変更が起きたものだけ”表示
chgrp -cR staff /srv/shared

# 参照ファイルと同じグループに揃える
chgrp --reference=/srv/shared/.group_ref /srv/shared/config.yml

# シンボリックリンク自体に適用（対応FS/カーネルでのみ意味がある）
chgrp -h staff symlink

# 誤爆対策：/ に対する -R を失敗させる
chgrp -R --preserve-root staff /
```

## SYNOPSIS（書式）

`info '(coreutils) chgrp invocation'` と `chgrp --help` より：

```text
chgrp [OPTION]... GROUP FILE...
chgrp [OPTION]... --reference=RFILE FILE...
```

## GROUP の指定（名前/数値ID）

- `GROUP` は **グループ名**（例: `staff`）または **数値GID**（例: `1000`）で指定できます
- 「数値に見えるグループ名」と区別したい場合、数値GIDを明示するために先頭に `+` を付けられます（例: `chgrp +1000 file`）

補足：一般ユーザーが変更できるグループは、所属グループに制限されることがあります（OS依存）。

## 主要オプション（運用で使う分だけ）

`chgrp --help` / `info '(coreutils) chgrp invocation'` より。

- `-R, --recursive`
  - ディレクトリ配下を再帰的に変更（誤爆しやすいので対象パスは二重確認）
- `-c, --changes`
  - 変更が起きたファイルだけ出力
- `-v, --verbose`
  - 処理した各ファイルを出力
- `-f, --silent, --quiet`
  - エラーメッセージを抑制（原因追跡が難しくなるので多用注意）
- `--reference=RFILE`
  - `RFILE` と同じグループに揃える
- `--preserve-root`
  - `-R` のとき `/` を対象にすると失敗させる

## シンボリックリンク（--dereference と -h）

- デフォルトは `--dereference`
  - **リンク自体ではなく参照先**に対して `chgrp` する
- `-h, --no-dereference`
  - **リンク自体**に適用（ただしOS/FSが `lchown(2)` を提供しないと失敗することがあります）

### -R と組み合わせた時の挙動（-H/-L/-P）

`-R` と併用すると「ディレクトリ探索中に symlink を辿るか」が追加で効きます。

- `-P`：一切辿らない（デフォルト）
- `-H`：コマンドライン引数が「ディレクトリへの symlink」のときだけ辿る
- `-L`：探索中に遭遇した「ディレクトリへの symlink」もすべて辿る

注意：`info` にある通り、`-R` と `--dereference` / `-L` の組み合わせは、探索中に攻撃者が symlink を差し込むことで意図しない対象に作用しうるため、権限が強い状況（root での運用）では特に慎重に扱います。

## 事故りポイント（chgrp あるある）

- `chgrp -R` の対象パス誤りは被害が大きい
  - まず `ls` や `find` で対象を確認してから実行する
  - `/` が混ざる可能性がある操作では `--preserve-root` を検討
- 変更できない（Operation not permitted 等）
  - 自分が所属していないグループには変更できない設定の環境があります（OS依存）
  - root 権限が必要な場所/ファイルがあります

## 確認コマンド（グループを素早く見る）

```bash
# 名前で見る（group file）
stat -c '%G %n' file

# 数値IDで見る（gid file）
stat -c '%g %n' file

# 一覧（長い形式）
ls -l file
```

## 終了コード（exit status）

`info '(coreutils) chgrp invocation'` より：

- `0`：成功
- `0` 以外：失敗

失敗時は、標準エラー出力のメッセージ（例：グループが存在しない、権限不足、ファイルが無い）を併せて確認します。

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
chgrp --version
chgrp --help
info '(coreutils) chgrp invocation'
```
