---
title: "UmuOSの為のC言語９（中級）　9章　シグナル"
---

# UmuOSの為のC言語（中級）　９

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結すると思います。

## ９章　シグナル

この章は、参考文献をもとに、自分用の研究ノートとして再構成していきます。
古い説明は、現在のLinuxやC17相当の書き方に寄せながら、UmuOSの設計へつながる形で整理します。

シグナルは、ソフトウェア割り込みの一種です。
ユーザの Ctrl-C、子プロセスの終了、ゼロ除算、無効メモリアクセス、タイマ満了など、さまざまな非同期イベントをプロセスへ通知します。

Linux のシステムプログラミングでは、シグナルは単なる例外通知ではありません。
終了処理、ジョブ制御、子プロセス回収、タイマ通知、軽量なプロセス間通知など、かなり広い場面で関係してきます。

古い Unix ではシグナル実装に互換性の揺れや信頼性の問題がありましたが、現在の Linux では POSIX ベースの扱いでかなり整理されています。
ただし「昔からある仕組み」であるがゆえに、使い方を誤ると扱いにくい分野でもあります。

### ９章の１　シグナルの概念

シグナルには、おおまかに次の流れがあります。

```text
発生する:
	カーネルや他プロセス、またはプロセス自身がシグナルを生成する

保留される:
	すぐ配送できない場合は pending 状態になる

配送される:
	ブロックされていなければ、カーネルがプロセスへ届ける

処理される:
	無視、デフォルト動作、登録済みハンドラ実行のいずれかになる
```

シグナルの処理結果は、基本的に次の 3 種類です。

無視する:

シグナルを無視すると、到着しても特別な処理はしません。
ただし、`SIGKILL` と `SIGSTOP` は無視できません。

この 2 つが無視可能だと、管理者が止められないプロセスを作れてしまい、運用上もセキュリティ上も困ります。

捕捉して処理する:

プロセスがあらかじめシグナルハンドラを登録しておくと、該当シグナル到着時に、カーネルは通常の実行をいったん中断してその関数を実行します。
ハンドラから戻ると、通常は中断前の実行へ復帰します。

よく使うのは次のような場面です。

```text
SIGINT:
	Ctrl-C を受けたときに後始末して終了する

SIGTERM:
	サービス停止要求を受けたときに整然と終了する

SIGCHLD:
	子プロセス終了を検知して wait 系で回収する
```

ただし、シグナルハンドラは任意のタイミングで割り込んできます。
そのため、通常の関数コールバックよりはるかに制約が強く、ハンドラ内で安全に呼べる関数は限定されます。

デフォルト動作を実行する:

ハンドラを登録せず、無視もしていなければ、シグナルごとに決まったデフォルト動作が行われます。

```text
終了する:
	SIGINT, SIGTERM, SIGPIPE など

コアダンプして終了する:
	SIGSEGV, SIGABRT, SIGILL など

停止する:
	SIGSTOP, SIGTSTP など

無視する:
	SIGCHLD, SIGWINCH など
```

なお、同期的な障害として見えるシグナルもあります。
たとえば `SIGSEGV` や `SIGFPE` は、見た目には「その命令を実行した結果ただちに発生した」ように見えますが、仕組みとしてはカーネルからのシグナル配送です。

信頼性と POSIX 化:

古い Unix では、同種シグナルの扱い、再入時の挙動、ハンドラ登録後の自動解除などに実装差がありました。
現在の Linux では POSIX ベースでかなり安定していますが、それでも「シグナルは状態付きの高機能メッセージキューではない」と理解しておくべきです。

標準的なシグナルは、同じ種類が連続して届いても 1 個に畳み込まれることがあります。
一方、POSIX realtime signal はキューイングされますが、これは後で扱う方が整理しやすいです。

#### ９章の１の１　シグナル名

すべてのシグナルには、`SIGINT` や `SIGTERM` のように `SIG` で始まる名前があります。
これらは [signal.h](man_lib-syscall/README.md) で定義される整数マクロです。

つまり、シグナルは本質的には番号ですが、番号を直接書くべきではありません。
同じ意味でも番号は環境依存性があるため、コードでは必ず名前を使います。

```text
良い例:
	SIGINT, SIGTERM, SIGCHLD

避ける例:
	2, 15, 17 のような生番号
```

特別なのがシグナル番号 0 です。
これは通常のシグナルではなく、`kill()` 系で「実際には送らず、存在確認や権限確認だけしたい」ときに使われる特殊値です。

利用可能なシグナル一覧は、シェルで `kill -l` を実行すると確認できます。

#### ９章の１の２　Linux でよく使うシグナル

横長表は見づらいので、ここでは用途別に整理します。

##### ９章の１の２の１　端末操作とジョブ制御

```text
SIGHUP:
	制御端末切断に関連
	デーモンの設定再読込用途にもよく使われる

SIGINT:
	Ctrl-C
	通常は終了要求として扱う

SIGQUIT:
	Ctrl-\
	多くはコアダンプ付き終了

SIGTSTP:
	Ctrl-Z
	対話的停止

SIGCONT:
	停止後の再開通知

SIGTTIN / SIGTTOU:
	バックグラウンドジョブの端末読み書きで停止

SIGWINCH:
	端末サイズ変更通知
```

##### ９章の１の２の２　プロセス制御と IPC

```text
SIGTERM:
	通常の終了要求
	捕捉して後始末してから終了することが多い

SIGKILL:
	強制終了
	捕捉も無視も不可

SIGSTOP:
	強制停止
	捕捉も無視も不可

SIGCHLD:
	子プロセス状態変化の通知

SIGUSR1 / SIGUSR2:
	アプリケーション定義用途

SIGPIPE:
	読み手のいないパイプやソケットへ書いたときに発生
```

ここで重要なのは、`SIGTERM` は捕捉可能だという点です。
強制終了のための捕捉不能シグナルは `SIGKILL` であり、`SIGTERM` ではありません。

##### ９章の１の２の３　障害と診断

```text
SIGABRT:
	abort() や assert 失敗で使われる

SIGSEGV:
	無効アドレス参照や保護違反

SIGBUS:
	メモリマップドファイルの不正アクセスなど

SIGILL:
	不正命令

SIGFPE:
	算術例外

SIGTRAP:
	デバッガのブレークポイントなど

SIGSYS:
	無効システムコールや seccomp 連携で現れることがある
```

古い説明では `SIGSYS` を「新しいバイナリを古い OS 上で動かした場合」に寄せて語ることがありますが、現在は seccomp 違反などでも目にします。

##### ９章の１の２の４　タイマ、資源、I/O 関連

```text
SIGALRM:
	alarm() や ITIMER_REAL

SIGVTALRM:
	仮想 CPU 時間ベースのタイマ

SIGPROF:
	プロファイリング用タイマ

SIGXCPU:
	CPU 時間上限超過

SIGXFSZ:
	ファイルサイズ上限超過

SIGIO:
	非同期 I/O 通知

SIGURG:
	ソケットの緊急データ通知

SIGPWR:
	電源系イベントに関連することがある
```

`SIGSTKFLT` のように、現在の一般的な Linux 利用ではほぼ意識しない互換性目的のシグナルもあります。
全部を覚える必要はなく、まずは `SIGINT`、`SIGTERM`、`SIGKILL`、`SIGCHLD`、`SIGPIPE`、`SIGSEGV` を優先すれば十分です。

### ９章の２　シグナル処理の基礎

歴史的に最も有名なインタフェースは `signal()` です。
ただし、現代の Linux で実用コードを書くなら、通常は `sigaction()` を使う方が適切です。

それでも `signal()` を知る価値はあります。
古い資料、既存コード、最小デモでは今でも出会うからです。

```c
#include <signal.h>

typedef void (*sighandler_t)(int);
sighandler_t signal(int signo, sighandler_t handler);
```

`signal()` は、指定シグナルの現在の動作を新しいハンドラへ置き換えます。
設定対象の代表例は次の 3 種類です。

```text
関数ポインタ:
	シグナル到着時にその関数を実行する

SIG_DFL:
	デフォルト動作へ戻す

SIG_IGN:
	無視する
```

`SIGKILL` と `SIGSTOP` にはハンドラ登録できません。

また、`signal()` には歴史的な曖昧さがあります。
システムによっては挙動差があり得るため、移植性と明示性を重視するなら `sigaction()` を選ぶ方が良いです。

#### ９章の２の１　任意のシグナルを待つ

もっとも単純な待機方法が `pause()` です。

```c
#include <unistd.h>

int pause(void);
```

`pause()` は、シグナルを受け取るまでプロセスをスリープさせます。
ハンドラが実行されて復帰すると、通常は `-1` を返し、`errno` は `EINTR` になります。

```c
for (;;) {
	if (pause() == -1) {
		/* シグナルで中断されて戻る */
	}
}
```

ただし、実運用コードで「ある条件を満たすまで安全に待つ」用途では、`pause()` だけでは競合を作りやすいです。
シグナルマスクと組み合わせるなら、普通は `sigsuspend()` の方が筋が良いです。

#### ９章の２の２　サンプルコード

まずは `SIGINT` を受けたら終了するだけの最小例です。
古い本ではハンドラ内で `printf()` や `exit()` を直接呼ぶ例がよく出ますが、今の観点では推奨できません。

シグナルハンドラ内では、非同期シグナル安全な処理だけを行い、詳細な出力や終了判定はメインループ側へ戻して行う方が安全です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void sigint_handler(int signo)
{
	(void)signo;
	g_stop = 1;
}

int main(void)
{
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		fputs("SIGINT handler registration failed\n", stderr);
		return EXIT_FAILURE;
	}

	puts("waiting for SIGINT (Ctrl-C)...");

	while (!g_stop) {
		pause();
	}

	puts("SIGINT received, exiting.");
	return EXIT_SUCCESS;
}
```

`volatile sig_atomic_t` を使っているのは、ハンドラと通常処理の間で最低限安全に共有しやすい型だからです。
複雑な状態更新をしたくなったら、設計を見直すべきです。

次は、同じハンドラを `SIGINT` と `SIGTERM` に登録し、`SIGHUP` を無視し、`SIGPROF` をデフォルトへ戻す例です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_last_signal = 0;

static void signal_handler(int signo)
{
	g_last_signal = signo;
}

int main(void)
{
	if (signal(SIGINT, signal_handler) == SIG_ERR) {
		fputs("SIGINT handler registration failed\n", stderr);
		return EXIT_FAILURE;
	}

	if (signal(SIGTERM, signal_handler) == SIG_ERR) {
		fputs("SIGTERM handler registration failed\n", stderr);
		return EXIT_FAILURE;
	}

	if (signal(SIGPROF, SIG_DFL) == SIG_ERR) {
		fputs("SIGPROF reset failed\n", stderr);
		return EXIT_FAILURE;
	}

	if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
		fputs("SIGHUP ignore setup failed\n", stderr);
		return EXIT_FAILURE;
	}

	puts("waiting for SIGINT or SIGTERM...");

	while (g_last_signal == 0) {
		pause();
	}

	printf("caught %s\n", strsignal(g_last_signal));
	return EXIT_SUCCESS;
}
```

この例でも、文字列化や表示はハンドラ外で行っています。

#### ９章の２の３　実行と親プロセスからの継承

ここは `fork()` と `execve()` で動きが違うので重要です。

```text
fork() 後:
	子プロセスは親のシグナル処理設定を引き継ぐ

execve() 後:
	捕捉していたシグナルはデフォルトへ戻る
	SIG_IGN になっていたものはそのまま残る
```

なぜこうなるかというと、`fork()` 直後の子は親の実行文脈をほぼそのまま複製している一方、`execve()` は新しいプログラム像へ置き換えるからです。

この性質はシェル実装で重要です。
シェルがバックグラウンドジョブを起動するとき、`SIGINT` や `SIGQUIT` をどう扱うかで端末操作の印象が変わります。

古い流儀では `signal()` を使って、無視されている場合だけハンドラ登録する例がよく示されます。

```c
if (signal(SIGINT, SIG_IGN) != SIG_IGN) {
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		fputs("Failed to handle SIGINT\n", stderr);
	}
}
```

考え方自体は分かりますが、現在はこの種の判定も `sigaction()` でより明示的に扱う方が適切です。
`signal()` は「現在の設定確認」と「新しい設定適用」が混ざっていて、設計として少し扱いにくいです。

#### ９章の２の４　シグナル番号とシグナル名の対応

ログや診断では、シグナル番号だけでなく、人間に読める名前や説明が必要になります。

古い資料では `sys_siglist` を使う例が出ますが、これは非標準で、今の新規コードで積極的に選ぶ対象ではありません。
まずは `strsignal()` を知っておく方が実用的です。

```c
#include <signal.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	int signo = SIGTERM;
	const char *description = strsignal(signo);

	if (description == NULL) {
		puts("unknown signal");
		return 0;
	}

	printf("%d: %s\n", signo, description);
	return 0;
}
```

`psignal()` もありますが、これは標準エラー出力へ直接出す用途に寄っています。
ログ文字列を組み立てたいなら `strsignal()` の方が柔軟です。

ただし `strsignal()` は、実装によっては内部静的領域を返します。
つまり、再入性やスレッドセーフ性を強く期待してはいけません。
また、シグナルハンドラ内で呼ぶべき関数でもありません。

要するに次の整理で覚えるとよいです。

```text
ハンドラ内:
	名前変換しない
	最低限のフラグ更新にとどめる

通常文脈:
	strsignal() やログ出力を行う
```

ここまでがシグナル処理の最初の入口です。
次に進むと、`signal()` より実用的な `sigaction()`、シグナルマスク、保留シグナル、同期的待機といった、実務で本当に使う論点が出てきます。



