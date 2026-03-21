---
title: "find(1) ファイル探索（名前/時刻/サイズ/権限）実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# find(1) — ディレクトリ階層からファイルを探す

`find` は「ディレクトリを再帰的に走査して、条件に一致したファイルを出す」コマンドです。
運用では「設定ファイルがどこか分からない」「特定拡張子のゴミを消したい」「最近更新されたものだけ集めたい」みたいな場面で刺さります。

`find` は強力ですが、**削除（`-delete`）やコマンド実行（`-exec`）は事故りやすい**ので、まずは `-print`（表示）で確認してから実行する癖が安全です。

## まず結論：よく使うコマンド

```bash
# 1) 名前で探す（まずはここ）
find /etc -name 'sshd_config'

# 2) 大文字小文字を無視して名前で探す
find . -iname '*.log'

# 3) 種類で絞る（ファイル/ディレクトリ）
find /var/log -type f -name '*.log'
find /var -type d -name 'cache'

# 4) 更新が新しいもの（直近N日/分）
find /var/log -type f -mtime -1     # 24時間以内
find /var/log -type f -mmin -60     # 60分以内

# 5) サイズで絞る（大きいログ/コア探し）
find /var -type f -size +100M

# 6) まず一覧→削除（安全に二段階）
find /tmp -type f -name '*.tmp' -print
# 問題なければ
find /tmp -type f -name '*.tmp' -delete

# 7) 別FSに降りない（マウントまたぎ事故を防ぐ）
find / -xdev -type f -name '*.core'

# 8) 権限で探す（SUID/SGIDや世界書き込み等）
find / -xdev -type f -perm /4000 -print    # SUID の可能性
find / -xdev -type f -perm /2000 -print    # SGID の可能性
find / -xdev -type d -perm -0002 -print    # world-writable dir

# 9) コマンド実行（安全寄り：まずは echo で確認）
find . -type f -name '*.conf' -exec echo cp -v {} /backup/ \;

# 10) 「空白や変な文字のファイル名」に強い出し方
find . -type f -name '*.log' -print0
```

## SYNOPSIS（書式）

```text
find [starting-point...] [expression]
```

- 開始点（starting-point）を省略すると `.`（カレント）になります
- `expression`（条件やアクション）を省略すると、暗黙に `-print`（表示）相当になります

## `expression` の考え方（最低限）

`find` の `expression` は大きく分けて次の組み合わせです。

- **tests（条件）**: 例 `-name`, `-type`, `-mtime`, `-size`, `-perm` など
- **actions（実行/出力）**: 例 `-print`, `-print0`, `-delete`, `-exec` など
- **operators（論理）**: 例 `-a`（AND）, `-o`（OR）, `!`（NOT）, `( )`

重要：

- 何も書かないと AND（`-a`）扱いになりがちです
- AND は OR（`-o`）より優先されます。複雑になったら **`( )` で括る**のが安全です

## まず使う条件（tests）

### 名前：`-name` / `-iname`

```bash
find . -name 'httpd.conf'
find . -name '*.conf'
find . -iname '*.conf'   # 大文字小文字を無視
```

注意：`'*.conf'` は **シングルクォート**推奨です。
クォートしないと、シェルが先に `*` を展開して事故ります。

### 種類：`-type`

```bash
find /var/log -type f
find /var -type d -name 'log'
find / -type l           # symlink
```

### 時刻：`-mtime` / `-mmin`（更新時刻）

```bash
# 24時間以内（「1日未満」）
find . -type f -mtime -1

# 7日より古い（「7日より前」）
find . -type f -mtime +7

# 30分以内
find . -type f -mmin -30
```

`+n` / `-n` / `n` の意味：

- `+n`: より大きい（より古い/より大きい）
- `-n`: より小さい（より新しい/より小さい）
- `n`: ちょうど

### サイズ：`-size`

```bash
find . -type f -size +10M
find . -type f -size -1k
```

単位例：`c`（バイト）, `k`（KiB相当）, `M`（MiB相当）, `G` など。

### パーミッション：`-perm`

よく使うのは「特定ビットが立っているものを探す」系です。

```bash
# /MODE : いずれかのビットが立っていればOK
find / -xdev -type f -perm /4000 -print   # SUID
find / -xdev -type f -perm /2000 -print   # SGID

# -MODE : 指定ビットがすべて立っていればOK
find . -type f -perm -0644 -print
```

## 出力と連携（actions）：`-print` / `-print0` / `xargs`

### `-print0` を優先（ファイル名が安全）

ファイル名に空白や改行が混ざると、`xargs` やシェルで事故ります。
その回避が `-print0`（NUL区切り）です。

```bash
# 安全に xargs へ渡す
find . -type f -name '*.log' -print0 | xargs -0 grep -nH 'ERROR'
```

（`xargs -0` とセットで使います）

## コマンド実行：`-exec`（まずは慎重に）

### 1件ずつ実行：`-exec ... \;`

```bash
# まずは echo で確認（本番の rm に置き換える前）
find . -type f -name '*.bak' -exec echo rm -v {} \;
```

### まとめて実行：`-exec ... {} +`

```bash
# たくさん見つかる場合は {} + の方が速いことが多い
find . -type f -name '*.bak' -exec echo rm -v {} +
```

実務の注意：

- まず `-print` で対象が合っているか確認
- いきなり `rm` せず、`echo rm ...` で dry-run
- 可能ならバックアップ/スナップショットの段取りも考える

## 除外（prune）：見たくない場所をスキップ

例：`.git` や `node_modules` を探索しない。

```bash
find . \( -name .git -o -name node_modules \) -prune -o -type f -name '*.md' -print
```

括弧はシェルに解釈されないよう `\(` `\)` としてあります。

## シンボリックリンクの扱い（ざっくり）

- 何も指定しない場合、多くの環境では **リンクを辿らない**（リンク自体を対象にする）挙動が基本です
- リンクを辿ると無限ループや想定外の範囲に入りやすいので、運用では慎重に

（必要なときだけ `-L` 等を検討、が無難です）

## 事故りポイント（find でよくやる）

- **`*` をクォートし忘れて、シェル展開で別物を探してしまう**
  - `-name '*.log'` のようにクォートする
- **`-delete` をいきなり撃って消しすぎる**
  - まず `-print` → 次に `-delete`
- **別ファイルシステムまで降りて時間が溶ける**
  - `-xdev`（または `-mount`）を付ける
- **OR（`-o`）の優先順位で意図しない結果になる**
  - `\( ... \) -o \( ... \)` のように括弧で明示
- **空白や改行を含むファイル名でパイプ連携が壊れる**
  - `-print0 | xargs -0` を使う
