---
title: "systemctl(1) systemd サービス操作・状態確認実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# systemctl(1) — systemd の操作/確認（実用）

`systemctl` は systemd のユニット（service/socket/timer/mount/target…）を **確認**し、**起動/停止/再起動**し、**自動起動設定（enable）**を管理するコマンドです。

運用では「まず状態を読む」→「ログを見る」→「必要なら再起動」→「自動起動を確認/調整」という流れで使うことが多いです。

## まず結論：よく使うコマンド

```bash
# まず状態（ユニット1つ）
systemctl status sshd

# 今動いてる？（スクリプト向け。終了コードで判定できる）
systemctl is-active --quiet sshd

# 失敗してる？
systemctl is-failed --quiet sshd

# 失敗ユニット一覧
systemctl --failed --no-pager

# タイマー一覧（cron代替の調査で頻出）
systemctl list-timers --all --no-pager

# 再起動（最頻出）
sudo systemctl restart sshd

# 設定変更後に「読込だけ」→対応してなければ再起動
sudo systemctl reload-or-restart nginx

# 自動起動（有効/無効）
sudo systemctl enable --now nginx
sudo systemctl disable --now nginx

# unit ファイルを編集（ドロップイン推奨）
sudo systemctl edit nginx

# unit ファイルを変更したら（重要）
sudo systemctl daemon-reload

# unit の中身（断片 + drop-in）を見る
systemctl cat nginx

# unit のプロパティを機械可読で見る（MainPID等）
systemctl show -p ActiveState -p SubState -p MainPID nginx
```

補足：root が必要かどうかは操作次第です。
- `status` / `is-active` / `show` は一般ユーザーでもできることが多い
- `start/stop/restart/enable/edit/daemon-reload` は基本 `sudo` 前提

## SYNOPSIS（書式）

```text
systemctl [OPTIONS...] COMMAND [UNIT...]
```

- `UNIT` は `sshd.service` のように拡張子付きが基本
- `sshd` のような省略もでき、既定では `.service` が補われます

## まず押さえる概念（混乱しやすい所だけ）

- **unit（ユニット）**
  - systemd が扱う管理単位。代表は `*.service`（サービス）
  - 他に `*.socket`（ソケット起動）、`*.timer`（タイマ起動）、`*.mount`、`*.target` など

- **「動いている」(active) と「自動起動」(enabled) は別**
  - `Active:` は今動いているか（状態）
  - `enabled/disabled` は起動時に起動対象か（設定）

## 状態確認（読むのが先）

### status：人間向けの状態 + 直近ログ

```bash
systemctl status nginx
systemctl status nginx --no-pager
systemctl status nginx --no-pager --lines=200
systemctl status nginx --full
```

`status` は「今と直近」を見る用途です。
古いログ・前回起動分まで遡るなら `journalctl -u nginx` を使う方が確実です。

### list-units：メモリ上のユニット一覧

```bash
# いま systemd が把握しているユニット（既定は active/failed 等）
systemctl list-units

# service だけ、failed だけ
systemctl list-units --type=service --state=failed

# inactive も含める（重い場合あり）
systemctl list-units --all
```

注意：`list-units` は「今メモリにあるもの」です。**インストール済み一覧**は次の `list-unit-files`。

### list-unit-files：インストール済み unit ファイル一覧（enable 状態も）

```bash
systemctl list-unit-files
systemctl list-unit-files --type=service
systemctl list-unit-files | grep -E 'enabled|disabled|masked'
```

### is-active / is-failed：スクリプト向けの判定

```bash
# quiet を付けると表示せず終了コードだけで判定できる
systemctl is-active --quiet nginx
systemctl is-failed --quiet nginx

# 例：動いてなければ再起動（簡易）
if ! systemctl is-active --quiet nginx; then
  sudo systemctl restart nginx
fi
```

## 起動/停止/再起動（運用レシピ）

```bash
sudo systemctl start nginx
sudo systemctl stop nginx
sudo systemctl restart nginx

# 動いている時だけ再起動（止まってたら何もしない）
sudo systemctl try-restart nginx

# 設定再読込に対応していれば reload、無理なら restart
sudo systemctl reload-or-restart nginx
sudo systemctl try-reload-or-restart nginx
```

## systemd タイマー（.timer）

systemd では定期実行/遅延実行を **`*.timer` ユニット**で管理します（いわゆる cron 代替）。
タイマーは多くの場合、対応する `*.service` を起動します。

### まず一覧と状態（次回いつ動く？）

```bash
# 次回実行時刻、前回実行時刻、紐づく service をまとめて見る
systemctl list-timers --all --no-pager

# 特定タイマーの状態
systemctl status logrotate.timer --no-pager
```

`list-timers` の列の目安：

- `NEXT`：次回起動予定
- `LAST`：前回起動
- `UNIT`：タイマーユニット名（`*.timer`）
- `ACTIVATES`：起動されるユニット（多くは `*.service`）

### 有効化/無効化（タイマーを動かす/止める）

```bash
# タイマーを自動起動（= スケジュールに乗せる）し、今すぐ開始もする
sudo systemctl enable --now foo.timer

# 止める（disable だけだと「次回以降起動しない」なので、通常は stop もセットにする）
sudo systemctl disable --now foo.timer
```

### うまく動かない時の切り分け

```bash
# タイマー定義を確認（fragment + drop-in）
systemctl cat foo.timer

# タイマーが起動しているはずの service の定義も確認
systemctl cat foo.service

# 依存関係/関連ユニットを見る
systemctl list-dependencies foo.timer
```

ログは `status` だけだと足りないことが多いので、基本は `journalctl` で掘ります。

```bash
sudo journalctl -u foo.timer -b -e
sudo journalctl -u foo.service -b -e
```

### 事故りやすい点（タイマー特有）

- `foo.service` を enable しても、定期実行にはならない
  - 定期実行を有効化するのは通常 `foo.timer` 側です（`enable --now foo.timer`）
- タイマーは「次回予定」が未来でも、`Active:` は active になり得る
  - 「いま実行中か？」は多くの場合、起動される `foo.service` の状態/ログで判断します

### reload と daemon-reload は別（事故ポイント）

- `systemctl reload nginx`
  - **サービス固有の設定再読込**（例：nginx が `nginx -s reload` を実装している、等）
- `systemctl daemon-reload`
  - **systemd 自身が unit ファイルを読み直す**（`/etc/systemd/system/*.service` を編集した時はこちら）

unit ファイルを編集したら、原則は次です。

```bash
sudo systemctl daemon-reload
sudo systemctl restart nginx
```

（サービスによっては `restart` ではなく `reload-or-restart` が安全な場合もあります）

## 自動起動（enable/disable）とマスク（mask）

```bash
# 自動起動を有効化（次回以降）
sudo systemctl enable nginx

# 今すぐ起動もセットで
sudo systemctl enable --now nginx

# 無効化 + 停止
sudo systemctl disable --now nginx

# 有効か？
systemctl is-enabled nginx

# 強制的に起動不能にする（事故防止・一時封印）
sudo systemctl mask nginx
sudo systemctl unmask nginx
```

`mask` は「enable を外す」より強く、起動そのものをブロックします。

## unit ファイルの確認と編集

### cat：実際に読み込まれる断片 + drop-in を見る

```bash
systemctl cat nginx
```

### edit：推奨（ドロップインで差分管理）

```bash
sudo systemctl edit nginx

# 例：内容確認（保存先パスも出る）
sudo systemctl edit nginx --full
```

運用では、パッケージ配布の unit 本体を直接編集するより、`edit` で drop-in を作る方が更新に強いです。

## show：機械可読で状態/設定を抜く

`show` は「フィールド指定して抜く」用途で便利です。

```bash
# 代表：状態、PID、起動時刻
systemctl show -p ActiveState -p SubState -p MainPID -p ExecMainStartTimestamp nginx

# 値だけ（スクリプト向け）
systemctl show -P MainPID nginx
```

## テンプレートユニット（@）

テンプレート（例：`foo@.service`）は **インスタンス**（例：`foo@bar.service`）を作らないと動きません。

```bash
# 例：インスタンスを起動
sudo systemctl start foo@bar

# テンプレート自体（foo@.service）は list-units には出ないことがある
# インストール済み一覧は list-unit-files を見る
systemctl list-unit-files | grep 'foo@'
```

## 事故りやすい点（短く）

- `systemctl` が効かない環境がある
  - コンテナ/chroot/WSL などで PID 1 が systemd でない場合、`systemctl` は期待通り動きません
  - まず `ps -p 1 -o comm=` や `systemctl is-system-running` で前提を確認します

- glob（`*`）指定は「メモリ上のユニット」にしか当たらないことがある
  - `start/stop` に `nginx*` のようなパターンを渡しても、想定より一致しないことがあります

- `status` は既定でログ 10 行だけ
  - `--lines=`、`--full`、`--no-pager` を使う

- 終了コードは LSB 互換の表に寄せているが、過信しない
  - 自動化は `is-active --quiet` 等の判定を優先し、状態文字列も併用するのが安全です

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# systemctl / systemd のバージョン
systemctl --version

# man
man systemctl
man systemd

# 状態確認系の出力例を自分の環境で採取（ノートに貼ると後で効く）
systemctl status sshd --no-pager
systemctl list-unit-files --type=service | head
systemctl --failed
```
