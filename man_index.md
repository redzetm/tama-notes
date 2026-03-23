---
title: "運用コマンド man風ノート インデックス（Rocky Linux 9.7）"
date: 2026-03-22
---

# 運用コマンド man風ノート（Rocky Linux 9.7）

Rocky Linux 9.7 の運用で頻出なコマンドについて、`man` をベースにしつつ、
「実際に調査で使う形（コピペ/事故りポイント/終了コード/フィルタ）」を日本語でまとめたノート集です。

## 収録ノート

ノート名の `(1)` や `(8)` は、`man` のセクション番号です（ユーザーコマンド/管理者コマンドなどの分類）。

### セクション 1（ユーザーコマンド）

- [grep(1) 行検索・ログ調査]({{ '/man_rocky9.7/grep.html' | relative_url }})（BRE/ERE/固定文字列、再帰、include/exclude、バイナリ扱い、終了コード）
- [sed(1) ストリーム編集（置換/抽出/削除）]({{ '/man_rocky9.7/sed.html' | relative_url }})（`-n`と`p/d/s`、範囲指定、`-i`注意、終了コード）
- [gawk(1) AWK（パターン走査・テキスト処理）]({{ '/man_rocky9.7/gawk.html' | relative_url }})（列/条件/集計、`-F/-v/-f`、`BEGIN/END`、頻度集計レシピ）
- [sort(1) 行ソート（キー指定/数値/重複排除）]({{ '/man_rocky9.7/sort.html' | relative_url }})（locale注意`LC_ALL=C`、`-k/-t`キー指定、`-n/-h/-V`、`-u/-s`）
- [uniq(1) 重複行の整理（カウント/抽出/除外）]({{ '/man_rocky9.7/uniq.html' | relative_url }})（隣接重複だけ、`sort | uniq`定番、`-c/-d/-D/-u`、skip/ignore-case）
- [curl(1) HTTP/HTTPS 通信・API叩き・ファイル取得]({{ '/man_rocky9.7/curl.html' | relative_url }})（`-fsSL`定番、ステータス取得`-w`、ヘッダ`-I/-D`、デバッグ`-v`、タイムアウト/リトライ、TLS切り分け）
- [wget(1) 非対話ダウンロード（取得/再開/再帰/ミラー）]({{ '/man_rocky9.7/wget.html' | relative_url }})（保存`-O/-P`、再開`-c`、ログ`-o/-a`、背景`-b`、リトライ/timeout、再帰`-r/-l/-k/-p`）
- [top(1) プロセス監視・負荷調査]({{ '/man_rocky9.7/top.html' | relative_url }})（%CPU/%MEM、スレッド表示、PID監視、バッチ出力、よく使うキー）
- [ps(1) プロセス一覧・調査]({{ '/man_rocky9.7/ps.html' | relative_url }})（`ps -aux`/`ps -ef`、`-o`列カスタム、`--sort`、スレッド、ツリー表示）
- [systemctl(1) systemd サービス操作・状態確認]({{ '/man_rocky9.7/systemctl.html' | relative_url }})（status/is-active、restart、enable、daemon-reload、ログ導線）
- [journalctl(1) systemd journal ログ調査]({{ '/man_rocky9.7/journalctl.html' | relative_url }})（`-u/-b`、`-f`追跡、`--since`、`-p`重要度、`--list-boots`）
- [less(1) ページャ（ログ閲覧/検索）]({{ '/man_rocky9.7/less.html' | relative_url }})（検索`/`、末尾`G`、追跡`F`、色`-R`、折返し`-S`）
- [find(1) ファイル探索]({{ '/man_rocky9.7/find.html' | relative_url }})（`-name/-type`、更新`-mtime/-mmin`、サイズ`-size`、除外`-prune`、安全`-print0`）
- [ls(1) ディレクトリ一覧]({{ '/man_rocky9.7/ls.html' | relative_url }})（`-l/-h`、隠し`-a/-A`、時刻`-t`、サイズ`-S`、SELinux`-Z`、dir自身`-d`）
- [id(1) ユーザー/グループID確認]({{ '/man_rocky9.7/id.html' | relative_url }})（実UID/実効UID、所属グループ、`-un/-Gn`、SELinux`-Z`）
- [firewall-cmd(1) firewalld 操作（ゾーン/サービス/ポート）]({{ '/man_rocky9.7/firewall-cmd.html' | relative_url }})（runtime/permanent、zone指定、add-service/add-port、reload、永続化）
- [nmcli(1) NetworkManager 操作（状態/接続/デバイス）]({{ '/man_rocky9.7/nmcli.html' | relative_url }})（device/connection、up/down、reload、スクリプト向け`-t/-f/-g`）
- [nmap(1) ネットワーク調査（疎通/開放ポート/サービス推定）]({{ '/man_rocky9.7/nmap.html' | relative_url }})（`-sn`生存確認、`-p/--top-ports`、`-sV`、保存`-oA`、open/filteredの見方）
- [df(1) ファイルシステム容量確認]({{ '/man_rocky9.7/df.html' | relative_url }})（`-hT`の見方、iノード枯渇`-i`、疑似FS除外`-x`、`--output`で列固定、`--total`）
- [du(1) ディレクトリ容量見積り]({{ '/man_rocky9.7/du.html' | relative_url }})（`-sh`合計、`--max-depth`で直下比較、`-x`で別FS回避、`--exclude`、見た目`--apparent-size`）
- [head(1) 先頭抽出]({{ '/man_rocky9.7/head.html' | relative_url }})（`-n`/`-c`、複数ファイル時のヘッダ`-q`/`-v`、`-n -NUM`で末尾除外）
- [tail(1) 末尾抽出・ログ追跡]({{ '/man_rocky9.7/tail.html' | relative_url }})（`-n`/`-c`、`-f`と`-F`の違い、`--pid`で自動終了、ローテーション対策）
- [wc(1) 行数/単語数/バイト数カウント]({{ '/man_rocky9.7/wc.html' | relative_url }})（`-l`件数、`-c`バイト/`-m`文字、`-L`最大行長、`grep | wc -l`）
- [kill(1) シグナル送信（安全に止める/生存確認/PG）]({{ '/man_rocky9.7/kill.html' | relative_url }})（TERM→KILL、`-0`生存確認、`-l`一覧、pid特殊値`0/-1/-PGID`注意、`--timeout`、ビルトイン差）
- [passwd(1) パスワード設定・ロック]({{ '/man_rocky9.7/passwd.html' | relative_url }})（`-l`ロック/`-u`解除、`-e`次回変更強制、`-S`状態、`--stdin`注意）
- [chmod(1) パーミッション変更]({{ '/man_rocky9.7/chmod.html' | relative_url }})（8進数`644/755`、記号形式`u+x`、再帰`-R`注意、`a+X`、setgid/sticky）
- [test(1) 条件判定（ファイル/文字列/数値比較）]({{ '/man_rocky9.7/test.html' | relative_url }})（`[`/`test`、終了コード、空文字・クォート、`&&/||`推奨、`[[`との違い）

### セクション 8（管理者コマンド）

- [ss(8) ソケット統計・接続確認]({{ '/man_rocky9.7/ss.html' | relative_url }})（LISTEN確認、接続状況、TCP stateフィルタ、タイマー、`-i`内部情報、SELinux context）
- [ip(8) ネットワーク操作（addr/link/route/neigh/netns）]({{ '/man_rocky9.7/ip.html' | relative_url }})（`-br/-j`、`addr/link/route/neigh`、`netns`、`monitor`、`-batch`）
- [useradd(8) ユーザー作成]({{ '/man_rocky9.7/useradd.html' | relative_url }})（`-m`でホーム作成、`-G`補助グループ、`-e`期限、`-D`デフォルト確認、`-p`の落とし穴）
- [usermod(8) ユーザー属性変更]({{ '/man_rocky9.7/usermod.html' | relative_url }})（`-G`補助グループ置換注意、`-d -m`でホーム移動、`-L/-U`、`-l`ログイン名変更）
- [userdel(8) ユーザー削除]({{ '/man_rocky9.7/userdel.html' | relative_url }})（`-r`でホーム削除、ログイン中は不可、ホーム外ファイルは残る）
