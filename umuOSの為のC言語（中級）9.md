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

### ９章の３　シグナルの送信

シグナル送信の基本は `kill()` です。
名前のせいで「プロセスを殺す関数」に見えますが、実際には任意のシグナルを送るための一般インタフェースです。

```c
#include <signal.h>
#include <sys/types.h>

int kill(pid_t pid, int signo);
```

`pid` の意味は少し特殊です。

```text
pid > 0:
	その PID の 1 プロセスへ送る

pid == 0:
	自プロセスと同じプロセスグループ全体へ送る

pid == -1:
	送信権限のある多くのプロセスへ送る
	通常は管理用途で、乱用しない

pid < -1:
	-pid のプロセスグループ全体へ送る
```

エラーとしては、主に次が重要です。

```text
EINVAL:
	無効なシグナル番号

EPERM:
	送信権限がない

ESRCH:
	対象プロセスまたは対象プロセスグループが見つからない
```

#### ９章の３の１　パーミッション

他プロセスへシグナルを送るには権限が必要です。
単純化すると、自分と同じユーザのプロセスへは送れて、他人のプロセスへは通常送れません。

Linux では capability を持つプロセス、典型的には強い権限を持つ管理系プロセスなら広く送信できます。
一方、通常ユーザでは UID の一致条件が重要です。

ただし、現在の Linux では user namespace や capability の文脈もあるため、「常に root だけが特別」とだけ覚えるのは少し雑です。
実務上は「自分の所有プロセスへ送るのが基本、他は権限次第」と理解するのがよいです。

`SIGCONT` には job control との関係で少し特別な扱いがありますが、まずは例外として覚えるより、ジョブ制御の文脈で理解した方が混乱しにくいです。

#### ９章の３の２　サンプルコード

特定 PID へ `SIGHUP` を送る最小例です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	pid_t target = 1722;

	if (kill(target, SIGHUP) == -1) {
		perror("kill");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

これはシェルで次を実行するのと同じ意味です。

```sh
kill -HUP 1722
```

ただし、実際には PID を決め打ちするより、設定再読込のような用途では PID ファイルや supervisor 経由で対象を特定する設計の方が安全です。

`signo == 0` は特別で、シグナルは送らず、存在確認や権限確認のために使えます。

```c
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	pid_t target = 1722;

	if (kill(target, 0) == -1) {
		if (errno == EPERM) {
			puts("process exists, but permission is denied");
		} else if (errno == ESRCH) {
			puts("process does not exist");
		} else {
			perror("kill");
		}
		return EXIT_FAILURE;
	}

	puts("process exists and signal permission looks valid");
	return EXIT_SUCCESS;
}
```

ただし、これはあくまでその瞬間の確認です。
確認直後に対象プロセスが終了する競合は普通に起こるので、`kill(pid, 0)` を絶対的な存在保証だと思ってはいけません。

#### ９章の３の３　自プロセスへシグナルを送信する

自分自身へ送るなら `raise()` が簡潔です。

```c
#include <signal.h>

int raise(int signo);
```

概念的には次と同じです。

```c
kill(getpid(), signo);
```

簡単な例です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t g_seen = 0;

static void usr1_handler(int signo)
{
	(void)signo;
	g_seen = 1;
}

int main(void)
{
	if (signal(SIGUSR1, usr1_handler) == SIG_ERR) {
		fputs("handler registration failed\n", stderr);
		return EXIT_FAILURE;
	}

	if (raise(SIGUSR1) != 0) {
		fputs("raise failed\n", stderr);
		return EXIT_FAILURE;
	}

	puts(g_seen ? "SIGUSR1 handled" : "SIGUSR1 not yet handled");
	return EXIT_SUCCESS;
}
```

#### ９章の３の４　プロセスグループ全体へシグナルを送信する

プロセスグループ全体へ送る意図を明示したいなら `killpg()` が使えます。

```c
#include <signal.h>

int killpg(pid_t pgrp, int signo);
```

意味としては次と同等です。

```c
kill(-pgrp, signo);
```

シェル、ジョブ制御、端末制御では、個々の PID ではなくプロセスグループ単位でシグナルを送る設計がとても重要です。
たとえば Ctrl-C がフォアグラウンドジョブ全体へ届くのは、この考え方とつながっています。

### ９章の４　リエントラント

シグナルハンドラは、プログラムの任意の位置へ割り込んできます。
メモリ確保の途中、標準入出力の内部状態更新中、共有構造体の更新中などに突然実行される可能性があります。

そのため、シグナルハンドラでは「今どの関数の途中に割り込んだのか分からない」という前提で設計しなければなりません。

古い説明ではリエントラントという言葉が前面に出ますが、実務では async-signal-safe という観点で理解する方が重要です。
つまり「シグナルハンドラから呼んでも安全と規定されているか」です。

危険な例は次の通りです。

```text
printf():
	標準入出力の内部状態に依存する

malloc() / free():
	割り当て器の内部状態を壊す恐れがある

strsignal():
	内部静的領域を返す実装がある

pthread mutex の多くの操作:
	シグナルハンドラ向きではない
```

したがって、シグナルハンドラでは原則として次の方針が安全です。

```text
やること:
	volatile sig_atomic_t のフラグ更新
	必要最小限の write()

やらないこと:
	複雑なログ出力
	動的メモリ確保
	非同期安全性が不明なライブラリ関数呼び出し
```

#### ９章の４の１　シグナルセーフなインタフェース

POSIX は、シグナルハンドラから安全に呼べる async-signal-safe な関数群を定義しています。
古い本では長い表が並びますが、実際にまず覚えるべきものは多くありません。

代表例は次の通りです。

```text
即時終了系:
	_Exit(), _exit(), abort()

ファイル記述子 I/O:
	read(), write(), close()

シグナル操作:
	sigaction(), sigprocmask(), sigpending(), sigsuspend(), kill(), raise()

プロセス待機系の一部:
	wait(), waitpid()

時刻や軽量な問い合わせの一部:
	alarm(), getpid(), getppid()
```

逆に、日常的によく使う高水準 API の多くはハンドラ向きではありません。

```text
使わない方がよい代表例:
	printf(), fprintf(), snprintf()
	malloc(), calloc(), realloc(), free()
	strtok(), strerror(), strsignal()
```

最小限の出力が必要なら、文字列リテラルを `write()` で直接出す方法が現実的です。

```c
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void term_handler(int signo)
{
	(void)signo;
	g_stop = 1;
	(void)write(STDERR_FILENO, "SIGTERM received\n", 17);
}
```

ただし、この種の直接出力も本当に必要なときだけに留める方がよいです。
多くの場合はフラグだけ立てて、通常文脈で整った終了処理をする方が設計しやすくなります。

### ９章の５　シグナルセット

複数のシグナルをまとめて扱うために `sigset_t` を使います。
シグナルマスク、保留シグナル集合、待機対象シグナル集合など、以後の API はほぼこの型と組で登場します。

```c
#include <signal.h>

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);
```

使い方の流れはかなり定型です。

```text
1:
	まず空集合または全要素集合で初期化する

2:
	sigaddset() や sigdelset() で調整する

3:
	その集合を sigprocmask() や sigsuspend() へ渡す
```

簡単な例です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	sigset_t set;

	if (sigemptyset(&set) == -1) {
		perror("sigemptyset");
		return EXIT_FAILURE;
	}

	if (sigaddset(&set, SIGINT) == -1) {
		perror("sigaddset");
		return EXIT_FAILURE;
	}

	if (sigaddset(&set, SIGTERM) == -1) {
		perror("sigaddset");
		return EXIT_FAILURE;
	}

	if (sigismember(&set, SIGINT) == 1) {
		puts("SIGINT is in the set");
	}

	return EXIT_SUCCESS;
}
```

`sigset_t` の内部表現は実装依存です。
ビットマスクに見えても直接触らず、必ず専用関数で操作します。

#### ９章の５の１　シグナルセット操作インタフェース（非標準）

glibc には GNU 拡張として補助関数があります。

```c
#define _GNU_SOURCE
#include <signal.h>

int sigisemptyset(const sigset_t *set);
int sigorset(sigset_t *dest, const sigset_t *left, const sigset_t *right);
int sigandset(sigset_t *dest, const sigset_t *left, const sigset_t *right);
```

これらは便利ですが、POSIX 専用の可搬コードでは前提にしない方が安全です。

```text
sigisemptyset():
	空集合かどうか調べる

sigorset():
	和集合を作る

sigandset():
	積集合を作る
```

ライブラリ依存を増やしたくない場合は、標準 API だけで十分なことが多いです。
特に学習段階では、まず `sigemptyset()`、`sigaddset()`、`sigprocmask()`、`sigsuspend()` の連携を確実に理解する方が重要です。

ここまでがシグナル処理の最初の入口です。
次に進むと、`signal()` より実用的な `sigaction()`、シグナルマスク、保留シグナル、同期的待機といった、実務で本当に使う論点が出てきます。



