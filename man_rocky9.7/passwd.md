---
title: "passwd(1) パスワード設定・ロック実用ノート（Rocky Linux 9.7）"
date: 2026-03-21
---

# passwd(1) — パスワード変更/ロック（実用）

`passwd` はユーザー（やアカウント）のパスワードを変更するコマンドです。
Rocky Linux 9.7 では PAM（Linux-PAM）や libuser 経由で動作します。

## まず結論：よく使うコマンド

```bash
# 自分のパスワードを変更（対話式）
passwd

# ユーザー alice のパスワードを設定/変更（root）
sudo passwd alice

# 次回ログインでパスワード変更を強制（root）
sudo passwd -e alice

# アカウントをロック（root）
# 暗号化パスワードの先頭に "!" を付けて実質無効化
sudo passwd -l alice

# ロック解除（root）
sudo passwd -u alice

# パスワードを削除（root）※危険：パスワード無しログインを許すことがある
sudo passwd -d alice

# 状態を確認（root）
sudo passwd -S alice

# 標準入力から読み込む（root）※取り扱い注意
# echo 'NewPass' | sudo passwd --stdin alice
```

## SYNOPSIS（書式）

```text
passwd [options] [username]
```

- `username` 省略：自分のパスワード
- `username` 指定：対象ユーザー（通常 root 権限が必要）

## よく使うオプション（運用で効く範囲）

- `-l`
  - パスワードをロック（root）
- `-u [-f]`
  - ロック解除（root）
  - `-f` は保護を強制解除する用途（むやみに使わない）
- `-d`
  - パスワードを削除（root）
- `-e`
  - 次回ログイン時の変更を強制（root）
- `-S`
  - パスワード状態の簡易表示（root）
- `--stdin`
  - 標準入力から新パスワードを読む（root）

パスワード期限系（root）は、運用ポリシーに合わせて使います。

- `-n mindays`：最小変更間隔（日）
- `-x maxdays`：最大有効期間（日）
- `-w warndays`：失効前警告（日）
- `-i inactivedays`：失効後の猶予（日）

## どのファイルが更新される？（重要）

ローカルユーザー（`/etc/passwd` / `/etc/shadow` で管理されているユーザー）に対して `passwd` を実行すると、通常は **`/etc/shadow` の該当ユーザー行**が更新されます。

- `/etc/passwd` 側は多くの環境でパスワード欄が `x` のままで、実体（ハッシュ・期限情報など）は `/etc/shadow` にあります
- `passwd -l` / `-u` / `-d` / `-e` / `-n/-x/-w/-i` なども、基本的に `/etc/shadow` の状態に反映されます

注意：LDAP/AD/NIS など外部ディレクトリ連携のユーザーを扱っている場合、更新先はローカルの `/etc/shadow` ではなく外部側になることがあります（環境依存）。

## 事故りやすい点（短く）

- `passwd -d` は危険
  - 「パスワード無し」を許す設定だと、意図せずログイン可能になることがあります
- `--stdin` はログ/履歴/プロセス一覧に残り得る
  - 自動化が必要なら、入力経路の秘匿と監査（ログに残らない運用）をセットで考えます
- ロック/解除の対象は「パスワード」
  - 端末ログイン可否は、シェルやSSH設定、鍵認証など別要因でも変わります

## 参考：Rocky Linux 9.7 での確認（エビデンス）

```bash
cat /etc/redhat-release

# passwd コマンドの提供元（環境で差が出るのでまず確認）
rpm -q passwd

passwd --help
man passwd
```
