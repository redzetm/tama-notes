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
