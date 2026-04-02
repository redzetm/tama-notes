---
title: "C言語ライブラリ／システムコール man風ノート インデックス"
date: 2026-04-03
---

# C言語ライブラリ／システムコール man風ノート

Linux 上で C 言語を書くときに使う API について、`man` の整理のしかたを土台にしつつ、
「何者なのか」「どう使うのか」「どこで混同しやすいのか」を日本語でまとめるノート集です。

この索引では、シェルコマンドではなく、Cプログラムから呼ぶ関数・システムコールを扱います。

## このノートで整理するもの

- セクション 2 相当：システムコール（`open`、`read`、`write`、`fork` など）
- セクション 3 相当：ライブラリ関数（`printf`、`malloc`、`fopen`、`strcpy` など）
- `libc` のラッパー経由で使う API と、カーネルのシステムコールそのものの違い
- 戻り値、`errno`、ヘッダファイル、典型的な落とし穴

## 既存の関連ノート

- [UmuOSの為のC言語　Linuxシステムコール、低レベルライブラリ関数１（ファイルI/O）]({{ '/system_call_lib1.html' | relative_url }})

## 個別APIノート

個別の API ノートは [man_lib-syscall](man_lib-syscall) に順次追加していきます。

### セクション 3（標準ライブラリ関数・ライブラリ関数）

- [printf(3) 標準出力へ整形して出力する]({{ '/man_lib-syscall/printf.html' | relative_url }})
	- 分類：標準ライブラリ関数（`<stdio.h>`）
- [puts(3) 文字列を標準出力へ簡単に出力する]({{ '/man_lib-syscall/puts.html' | relative_url }})
	- 分類：標準ライブラリ関数（`<stdio.h>`）

最初に増やしやすい候補：

- ファイルI/O：`open` / `read` / `write` / `close` / `lseek`
- プロセス制御：`fork` / `execve` / `wait` / `exit`
- メモリ：`malloc` / `calloc` / `realloc` / `free`
- 標準I/O：`printf` / `fprintf` / `fgets` / `fread` / `fwrite`
- 文字列：`strlen` / `strcmp` / `strncpy` / `strtok`
- エラー処理：`errno` / `perror` / `strerror`

## 読み方の目安

各ノートでは、できるだけ次の順で整理します。

- これは何か
- ヘッダファイル
- 関数プロトタイプ
- 引数
- 戻り値
- `errno` とエラー
- 最小サンプルコード
- よくあるミス
- 関連API

※ 個別ノートは順次追加予定