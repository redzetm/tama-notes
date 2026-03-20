---
title: "運用コマンド man風ノート インデックス（Rocky Linux 9.7）"
date: 2026-03-20
---

# 運用コマンド man風ノート（Rocky Linux 9.7）

Rocky Linux 9.7 の運用で頻出なコマンドについて、`man` をベースにしつつ、
「実際に調査で使う形（コピペ/事故りポイント/終了コード/フィルタ）」を日本語でまとめたノート集です。

## 収録ノート

- [ss(8) ソケット統計・接続確認（実用ノート）]({{ '/man_rocky9.7/ss.html' | relative_url }})（LISTEN確認、接続状況、TCP stateフィルタ、タイマー、`-i`内部情報、SELinux context）
- [grep(1) 行検索・ログ調査（実用ノート）]({{ '/man_rocky9.7/grep.html' | relative_url }})（BRE/ERE/固定文字列、再帰、include/exclude、バイナリ扱い、終了コード）
- [top(1) プロセス監視・負荷調査（実用ノート）]({{ '/man_rocky9.7/top.html' | relative_url }})（%CPU/%MEM、スレッド表示、PID監視、バッチ出力、よく使うキー）
- [ps(1) プロセス一覧・調査（実用ノート）]({{ '/man_rocky9.7/ps.html' | relative_url }})（`ps -aux`/`ps -ef`、`-o`列カスタム、`--sort`、スレッド、ツリー表示）
- [df(1) ファイルシステム容量確認（実用ノート）]({{ '/man_rocky9.7/df.html' | relative_url }})（`-hT`の見方、iノード枯渇`-i`、疑似FS除外`-x`、`--output`で列固定、`--total`）
- [head(1) 先頭抽出（実用ノート）]({{ '/man_rocky9.7/head.html' | relative_url }})（`-n`/`-c`、複数ファイル時のヘッダ`-q`/`-v`、`-n -NUM`で末尾除外）
- [tail(1) 末尾抽出・ログ追跡（実用ノート）]({{ '/man_rocky9.7/tail.html' | relative_url }})（`-n`/`-c`、`-f`と`-F`の違い、`--pid`で自動終了、ローテーション対策）
- [du(1) ディレクトリ容量見積り（実用ノート）]({{ '/man_rocky9.7/du.html' | relative_url }})（`-sh`合計、`--max-depth`で直下比較、`-x`で別FS回避、`--exclude`、見た目`--apparent-size`）
- [wc(1) 行数/単語数/バイト数カウント（実用ノート）]({{ '/man_rocky9.7/wc.html' | relative_url }})（`-l`件数、`-c`バイト/`-m`文字、`-L`最大行長、`grep | wc -l`）
- [useradd(8) ユーザー作成（実用ノート）]({{ '/man_rocky9.7/useradd.html' | relative_url }})（`-m`でホーム作成、`-G`補助グループ、`-e`期限、`-D`デフォルト確認、`-p`の落とし穴）
