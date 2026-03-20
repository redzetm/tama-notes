---
title: "tail(1) 末尾抽出・ログ追跡コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# tail(1) — 末尾を抜く/追う（実用）

`tail` は、ファイル（または標準入力）の **末尾部分**を取り出すコマンドです。
特に `-f` による **ログ追跡**が運用で定番です。

## まず結論：よく使うコマンド

```bash
# 末尾10行（デフォルト）
tail /var/log/messages

# 末尾200行
tail -n 200 /var/log/messages

# ログを追いかける（追記されるたびに表示）
tail -f /var/log/messages

# ローテーションに強く追いかける（ファイルが置き換わっても追跡し直す）
tail -F /var/log/messages

# 「先頭から数えて N行目以降」を出す（全文の途中から見る）
tail -n +100 access.log

# 複数ファイルを追う（全体の流れを見る）
tail -F /var/log/messages /var/log/secure

# パイプの末尾だけ（重い出力の末尾確認）
command | tail -n 50
```

## SYNOPSIS（書式）

```text
tail [OPTION]... [FILE]...
```

- `FILE` 省略/`-`：標準入力から読む
- 複数ファイル：各ファイルの前に見出し（ファイル名）が付く（抑制/強制は `-q`/`-v`）

## よく使うオプション

### 行/バイト指定

- `-n, --lines=[+]NUM`
  - `-n NUM`：末尾 NUM 行
  - `-n +NUM`：先頭から数えて NUM 行目 **以降**（途中から読む）
- `-c, --bytes=[+]NUM`
  - `-c NUM`：末尾 NUM バイト
  - `-c +NUM`：先頭から数えて NUM バイト目 **以降**

### ログ追跡（follow）

- `-f, --follow[={name|descriptor}]`
  - 追記を追いかける
  - オプション引数省略時は `descriptor` 扱い（環境差の切り分けで重要）
- `-F`
  - `--follow=name --retry` 相当（ローテーション/置換に強い）
- `--retry`
  - 開けない場合も開けるまで再試行
- `-s, --sleep-interval=N`
  - 追跡のチェック間隔
- `--pid=PID`
  - `-f` 中に、指定PIDが死んだら終了（自動終了したい時に便利）

### その他

- `-q, --quiet, --silent`
  - 複数ファイルでもファイル名ヘッダを出さない
- `-v, --verbose`
  - 常にファイル名ヘッダを出す
- `-z, --zero-terminated`
  - 行区切りを改行ではなく NUL にする（NUL区切りのストリーム用）

## 事故りやすい点（短く）

- `tail -f` と `tail -F` の差は運用で大きい
  - `-f` は「開いたもの（ファイル記述子）」を追うため、ログローテーションでファイルが置き換わると、新しいファイルを追わないことがあります
  - ログを“ファイル名として”追いかけたい運用では `-F` を基本にすると事故が減ります
- `-n +NUM` は「先頭から NUM行目以降」
  - `-n NUM`（末尾NUM行）と逆方向なので、意図を確認してから使う

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release
rpm -q coreutils

tail --version
tail --help
man tail
```
