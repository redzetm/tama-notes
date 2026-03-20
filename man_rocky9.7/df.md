---
title: "df(1) ファイルシステム容量確認コマンド実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# df(1) — ディスク使用量（実用）

`df` は「各ファイルシステムの空き容量（または iノードの空き）」を一覧するコマンドです。

- ディスクが逼迫している（`No space left on device`）
- どのマウントポイントが詰まっているか知りたい
- 容量は空いているのに作れない（iノード枯渇）

…の一次切り分けで、まず使います。

## まず結論：よく使うコマンド

```bash
# まずはこれ（容量 + FS種別）
df -hT

# 特定パスが属するファイルシステムだけ見たい
df -hT /var

# 疑似FSを除外して見やすく（環境により有効）
df -hT -x tmpfs -x devtmpfs

# iノード枯渇の確認（容量が空いているのに作れない時）
df -i

# 出力列を固定して見たい（手順書/スクリプト向け）
df -h --output=source,fstype,size,used,avail,pcent,target

# 合計（--total）を付けてざっくり把握
df -h --total
```

## SYNOPSIS（書式）

```text
df [OPTION]... [FILE]...
```

- 引数なし：現在マウントされているファイルシステム一覧
- `FILE` を指定：その `FILE` が属するファイルシステムを表示

## 出力の見方（最低限）

`df` の基本出力は概ね次の列です（`-h` で見やすい単位になります）。

- `Filesystem`：デバイス名やボリューム名（例：`/dev/sda2`）
- `Size`：総容量
- `Used`：使用量
- `Avail`：利用可能（空き）
- `Use%`：使用率
- `Mounted on`（`target`）：マウントポイント

`-T` を付けると `Type`（`xfs` など）が出ます。

## よく使うオプション（運用で効く範囲）

### 表示単位

- `-h, --human-readable`
  - 1024基準（GiB寄り）の見やすい単位
- `-H, --si`
  - 1000基準（GB寄り）の見やすい単位
- `-B, --block-size=SIZE`
  - 単位を固定（例：`-BM` で MiB 単位、`-B1` でバイト）

### inode（容量ではなく「個数」）

- `-i, --inodes`
  - ブロック使用量ではなく iノード情報を表示します。

```bash
# iノードが枯渇していないか
df -i
```

### ファイルシステム種別で絞る

- `-T, --print-type`
  - ファイルシステム種別を表示
- `-t, --type=TYPE`
  - 指定した種別だけに絞る
- `-x, --exclude-type=TYPE`
  - 指定した種別を除外

```bash
# 例：tmpfs/devtmpfs を除外して見やすく
df -hT -x tmpfs -x devtmpfs

# 例：xfs だけを見る
df -hT -t xfs
```

### 便利な表示

- `--output[=FIELD_LIST]`
  - 出力列を固定できます（`FIELD_LIST` 省略で全列）。

```bash
# 例：運用でよく欲しい列だけ
# source: デバイス / target: マウントポイント
df -h --output=source,fstype,size,used,avail,pcent,target

# 例：スクリプト向けに列を固定（POSIX形式に寄せたいなら -P も検討）
df -P --output=source,size,used,avail,pcent,target
```

### その他（頻度は低い）

- `-a, --all`
  - 疑似FSやアクセス不能なものも含める（運用ではノイズになることが多い）
- `-l, --local`
  - ローカルFSだけに制限
- `--direct`
  - マウントポイントではなく、そのファイル自体の統計を表示（調査用途）
- `--total`
  - 合計行を出す

## よくある調査レシピ

### 1) 「どこが詰まってる？」を一発で見る

```bash
df -hT
```

疑似FSが多くて見づらければ：

```bash
df -hT -x tmpfs -x devtmpfs
```

### 2) `/var` が怪しい：そのFSだけ確認

```bash
df -hT /var
```

### 3) 容量はあるのに作れない（iノード枯渇）

```bash
df -i
```

`IUse%` が 100% に近いファイルシステムがあると、容量が残っていてもファイル作成に失敗します。

## 事故りやすい点（短く）

- `-h` と `-H` は数字が変わる
  - 1024基準（`-h`）と 1000基準（`-H`）の違い。チーム内で揃えると混乱が減ります
- `Avail` と `Use%` の印象がズレることがある
  - ファイルシステムによっては予約領域などの都合で、一般ユーザー視点の空きが少なく見えることがあります
- `df FILE` は「そのFILEが属するFS」を出す
  - bind mount/コンテナ/overlay などが絡むと直感とズレることがあります。必要なら `-T` や `--output=...` で情報を増やします

## 環境変数（触らないのが基本）

`df` は表示単位が環境変数でも変わります。運用では「変えない」が原則です。

- `DF_BLOCK_SIZE`, `BLOCK_SIZE`, `BLOCKSIZE`
  - 表示単位の既定に影響します
- `POSIXLY_CORRECT`
  - 既定ブロックサイズが 512 bytes になることがあります

## 参考：Rocky Linux 9.7 での確認（エビデンス）

挙動差を疑ったら、まず自分の環境で次を控えてください。

```bash
cat /etc/redhat-release
rpm -q coreutils

df --version

df --help
man df
```
