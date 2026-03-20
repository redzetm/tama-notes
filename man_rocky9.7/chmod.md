---
title: "chmod(1) パーミッション変更コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# chmod(1) — パーミッション（mode bits）変更

`chmod` はファイル/ディレクトリの **パーミッション（mode bits）**を変更します。
運用では「アクセスできない/実行できない/書けない」を直す場面で頻出です。

## まず結論：よく使うコマンド

```bash
# 典型：実行できるようにする（自分に実行権）
chmod u+x script.sh

# 典型：644（ファイル）/ 755（ディレクトリ）
chmod 644 file.txt
chmod 755 dir

# 再帰：ディレクトリ配下を一括変更
chmod -R 755 /srv/www

# 再帰で「ディレクトリだけ」実行（検索）権限を付けたい時の定番
# X は「ディレクトリ、または既に誰かに実行権がある場合だけ x を付ける」
chmod -R a+X /srv/www

# グループ書き込みを足す
chmod g+w shared.txt

# 参照ファイルと同じ権限に揃える
chmod --reference=ref.txt target.txt

# 変更があったものだけ表示
chmod -c 640 *.key
```

## SYNOPSIS（書式）

```text
chmod [OPTION]... MODE[,MODE]... FILE...
chmod [OPTION]... OCTAL-MODE FILE...
chmod [OPTION]... --reference=RFILE FILE...
```

- `MODE`：記号形式（`u+rwx` など）
- `OCTAL-MODE`：8進数（`644`/`755` など）
- `--reference`：参照ファイルに揃える

## MODE の書き方（実用だけ）

### 1) 記号形式（symbolic mode）

基本形：`[ugoa...][-+=][rwxXst...]`

- 対象：`u`（所有者）`g`（グループ）`o`（その他）`a`（全部）
- 演算子：`+`（追加）`-`（削除）`=`（置き換え）
- 権限：`r`（読む）`w`（書く）`x`（実行/検索）
- 便利：`X`（条件付きで x を付与）
- 特殊：`s`（setuid/setgid）`t`（sticky）

例：

```bash
# 所有者に実行権を追加
chmod u+x a.out

# グループとその他から書き込みを剥がす
chmod go-w config.cfg

# 全員読み取り、所有者のみ書き込み（置き換え）
chmod a=r,u+w file.txt

# 再帰でディレクトリだけ x を付ける定番
chmod -R a+X /path
```

### 2) 8進数（octal mode）

よく使う値だけ覚えるのが実用的です。

- `644`：`rw-r--r--`（一般的なテキスト）
- `600`：`rw-------`（秘密鍵/パスワード系の候補）
- `755`：`rwxr-xr-x`（ディレクトリ、実行ファイル）
- `700`：`rwx------`（自分だけ）

先頭桁（4桁目）は特殊ビット：

- `4xxx`：setuid
- `2xxx`：setgid
- `1xxx`：sticky

## 再帰（-R）の注意

- `-R` は強力なので、対象パスを間違えると被害が大きいです
- `--preserve-root` は `/` を再帰操作しないための安全策です（環境によって既定挙動が異なる可能性があります）

```bash
# 誤爆対策（/ に対する再帰を防ぐ）
chmod -R --preserve-root 755 /somewhere
```

## setuid/setgid/sticky（最低限）

### setuid/setgid（`s` / 先頭桁 4xxx,2xxx）

- 実行ファイルに付けると、権限の意味が大きく変わるため慎重に扱います
- ディレクトリでは setgid が「新規作成ファイルのグループ継承」に使われることがあります

```bash
# 記号形式で setgid を付ける/外す
chmod g+s /srv/shared
chmod g-s /srv/shared
```

### sticky（`t` / 先頭桁 1xxx）

- 典型例：`/tmp`（誰でも書けるが、他人のファイルは消せない）

```bash
chmod +t /some/world-writable-dir
```

## 事故りやすい点（短く）

- `chmod` はシンボリックリンク自体の権限を変えない（多くの環境でリンクの権限は意味を持たない）
- `chmod -R` は事故りやすい
  - `pwd` と対象パスを声に出して確認するくらいでちょうどいいです
- `a+X` は「ファイル全部を実行可能にしない」ための定番
  - `a+x` と混同しない

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
rpm -q coreutils

chmod --version
chmod --help
man chmod
```
