---
title: "UmuOSの為のC言語１０（中級）　10章　時間"
---

# UmuOSの為のC言語（中級）　１０

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結すると思います。

## １０章　時間

この章は、参考文献をもとに、自分用の研究ノートとして再構成していきます。
古い説明は、現在のLinuxやC17相当の書き方に寄せながら、UmuOSの設計へつながる形で整理します。

現代のオペレーティングシステムでは、時間はほとんどすべての層で使われます。
ログ時刻、タイムアウト、スリープ、再送制御、ベンチマーク、プロファイリング、監視、スケジューリングなど、時間を正しく扱えないとシステム全体の挙動が崩れます。

Linux では、少なくとも次の 3 種類の時間を区別して考えると整理しやすいです。

```text
実時間:
	人間が読む日付と時刻
	ログ、ファイル時刻、予定時刻に向く

プロセス時間:
	あるプロセスが実際に消費した CPU 時間
	性能計測や統計に向く

モノトニック時間:
	単調増加する経過時間
	タイムアウトや経過時間測定に向く
```

特に重要なのは、実時間とモノトニック時間を混同しないことです。
ユーザが時計を合わせたり、NTP が補正したり、うるう秒の影響を受けたりするのは実時間側です。
一方で「5秒待つ」「500ミリ秒以内に応答が来なければ失敗」のような処理は、通常モノトニック時間で考えるべきです。

また、時間の表し方としては、相対時間と絶対時間があります。

```text
相対時間:
	今から 5 秒後、20 ミリ秒待つ、3 分前など

絶対時間:
	1970-01-01 からの経過秒数や、カレンダー上の日時
```

Linux や Unix の多くの時間 API は、Unix epoch、つまり UTC の 1970-01-01 00:00:00 を基点にしています。
ただし、これも API によって、秒単位だけか、ナノ秒まで扱うか、壁時計か、単調時計か、CPU 時間かが異なります。

古い資料では、時間の進み方を jiffy や HZ から説明することが多いですが、今の Linux は高分解能タイマや tickless カーネルを前提とした設計がかなり進んでいます。
そのため、ユーザ空間プログラムでは HZ を前提にした設計を避け、POSIX 時間 API を通して扱う方が自然です。

### １０章の１　時刻を表現する構造体

Unix 系 OS では、時間を扱う構造が 1 つではありません。
用途に応じて、秒だけの単純な型、マイクロ秒付き構造体、ナノ秒付き構造体、分解済みの日付時刻構造体、CPU 時間用の型などが使い分けられます。

ここを最初に整理しておくと、後で `time()`、`gettimeofday()`、`clock_gettime()`、`nanosleep()`、`strftime()` などを学ぶときに混乱しにくくなります。

#### １０章の１の１　初期の時刻表現

もっとも基本的なのは `time_t` です。

```c
#include <time.h>
```

`time_t` は、通常 Unix epoch からの経過秒数を表すための抽象型です。

古い説明では「Linux では単なる long」と断定されることがありますが、今はそう決め打ちすべきではありません。
`time_t` の実体は ABI や libc やアーキテクチャに依存します。
32 ビット環境では歴史的事情を引きずることがありますが、現代の 64 ビット Linux では 64 ビットの `time_t` が一般的です。

```text
time_t の役割:
	epoch からの秒数を保持する

重要な点:
	具体的な整数型は実装依存
```

いわゆる 2038 年問題も、ここに関係します。
32 ビット符号付き秒数で表すと上限が来るためです。
ただし現在は、多くの環境で 64 ビット `time_t` への移行が進んでいます。
そのため、学習上は「問題そのものは重要だが、現代 Linux では移行がかなり進んでいる」と押さえる方が実態に合います。

#### １０章の１の２　現状の時刻表現（マイクロ秒）

秒単位だけでは粗すぎるため、昔からよく使われてきたのが `timeval` 構造体です。

```c
#include <sys/time.h>

struct timeval {
	time_t      tv_sec;
	suseconds_t tv_usec;
};
```

`tv_sec` は秒、`tv_usec` はマイクロ秒です。

```text
tv_sec:
	秒

tv_usec:
	0 以上 999999 以下のマイクロ秒
```

現在でも `select()`、`gettimeofday()`、一部のソケット API など、重要な場面で `timeval` は残っています。
そのため「古いから完全に不要」とは言えません。

ただし、新規コードで高精度時間 API を選べるなら、後述の `timespec` を使う設計が優先されることが多いです。

#### １０章の１の３　さらに細かい時刻表現（ナノ秒）

現代の POSIX 時間 API では `timespec` が中核です。

```c
#include <time.h>

struct timespec {
	time_t tv_sec;
	long   tv_nsec;
};
```

`tv_nsec` はナノ秒です。

```text
tv_sec:
	秒

tv_nsec:
	0 以上 999999999 以下のナノ秒
```

`clock_gettime()`、`clock_nanosleep()`、`nanosleep()`、POSIX タイマなど、多くの現代 API はこれを使います。
そのため、時間処理を学ぶなら `timespec` を最重要構造体として覚えるのが実用的です。

ただし、インタフェースがナノ秒単位だからといって、実ハードウェアやカーネルが常にその精度で時刻を供給するわけではありません。
分解能と精度と正確さは別物です。

```text
型の表現力:
	ナノ秒まで持てる

実際の分解能:
	カーネル、クロック源、ハードウェアに依存する
```

#### １０章の１の４　時刻表現の細分化

人が読める日付や時刻へ変換したいときは、`struct tm` を使います。

```c
#include <time.h>

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};
```

実装によっては GNU 拡張や BSD 由来の追加メンバが見えることもありますが、移植性を意識するなら上の基本メンバを中心に考える方が安全です。

各メンバの意味は次の通りです。

```text
tm_sec:
	秒

tm_min:
	分

tm_hour:
	時

tm_mday:
	月内の日

tm_mon:
	0 が 1 月、11 が 12 月

tm_year:
	1900 年からの経過年数

tm_wday:
	0 が日曜

tm_yday:
	0 がその年の 1 月 1 日

tm_isdst:
	夏時間情報
```

ここで混乱しやすいのは `tm_mon` と `tm_year` です。

```text
tm_mon:
	1 から 12 ではなく 0 から 11

tm_year:
	西暦そのものではなく 1900 からの差
```

したがって、2026 年 8 月を表したいなら、概念的には次のようになります。

```c
struct tm tm_value = {
	.tm_year = 2026 - 1900,
	.tm_mon = 8 - 1,
	.tm_mday = 16,
	.tm_hour = 12,
	.tm_min = 0,
	.tm_sec = 0,
	.tm_isdst = -1,
};
```

`tm_isdst = -1` は、夏時間かどうかをライブラリ側に判断させる、という意味でよく使います。

古い資料には `tm_gmtoff` や `tm_zone` を前面に出すものもありますが、これは非標準拡張です。
Linux/glibc では見えても、標準 C や厳密 POSIX の中心ではありません。

#### １０章の１の５　プロセス時間の型

CPU 時間の計測では `clock_t` が出てきます。

```c
#include <time.h>
```

`clock_t` は、主に `clock()` のような API で使われる時間単位の型です。
ここで重要なのは、`clock_t` の値は必ずしも wall clock の秒ではない、という点です。

古い説明では tick や HZ と直結して語られがちですが、ユーザ空間では `CLOCKS_PER_SEC` を使って秒換算するのが標準的です。

```text
clock_t:
	CPU 時間表現に使われる

CLOCKS_PER_SEC:
	clock_t を秒へ換算するためのスケール
```

一方で、Linux カーネル内部の HZ や jiffies を、ユーザ空間アプリケーションが直接前提にするべきではありません。
現代の Linux では高分解能タイマや dynamic tick があるため、なおさらです。

ユーザ空間から `sysconf(_SC_CLK_TCK)` で得られる値もありますが、これは主に `times()` など特定 API の単位換算に関係するもので、カーネル内部の HZ をそのまま表すものとして乱用しない方が安全です。

簡単な例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	long ticks_per_second = sysconf(_SC_CLK_TCK);

	if (ticks_per_second == -1) {
		perror("sysconf");
		return EXIT_FAILURE;
	}

	printf("_SC_CLK_TCK = %ld\n", ticks_per_second);
	return EXIT_SUCCESS;
}
```

ここまでの段階では、まず次の対応だけ押さえれば十分です。

```text
time_t:
	秒単位の epoch 時刻

timeval:
	秒 + マイクロ秒

timespec:
	秒 + ナノ秒

tm:
	人間が読む日付時刻への分解表現

clock_t:
	CPU 時間系の値
```

### １０章の２　POSIXクロック

時間関連の多くの API は、どの時計を使うかを `clockid_t` で指定します。
この「時計」は、単なる現在時刻の保存場所ではなく、どんな性質の時間を返すかを表すクロックソースの種類です。

古い資料では 4 種類だけを並べることがありますが、現在の Linux には `CLOCK_BOOTTIME`、`CLOCK_MONOTONIC_RAW`、`CLOCK_TAI` など、追加のクロックもあります。
ただし、POSIX の基本を学ぶ段階では、まず次の 4 つを押さえれば十分です。

```text
CLOCK_REALTIME:
	実時間
	日付や時刻の表示、タイムスタンプ向け

CLOCK_MONOTONIC:
	単調増加する経過時間
	タイムアウト、間隔計測向け

CLOCK_PROCESS_CPUTIME_ID:
	プロセス全体の CPU 消費時間

CLOCK_THREAD_CPUTIME_ID:
	スレッド単位の CPU 消費時間
```

ここで重要なのは、`CLOCK_MONOTONIC` は wall clock ではないという点です。
「今何時か」ではなく、「ある基準点からどれだけ進んだか」を扱います。

また、古い本では CPU 時間クロックの実装を TSC など特定ハードウェアレジスタへ強く結び付けて説明することがありますが、ユーザ空間からはそこを前提にすべきではありません。
どのハードウェア源をどう使うかは、カーネルとアーキテクチャ実装に依存します。

移植性の観点では、`CLOCK_REALTIME` は広く使えますが、現在の POSIX 系 OS では `CLOCK_MONOTONIC` も実用上かなり一般的です。
新規コードで相対時間を測るなら、まず `CLOCK_MONOTONIC` を疑う、という感覚が重要です。

#### １０章の２の１　時計の分解能

指定した時計の理論上の分解能は `clock_getres()` で参照できます。

```c
#include <time.h>

int clock_getres(clockid_t clock_id, struct timespec *res);
```

簡単な例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	clockid_t clocks[] = {
		CLOCK_REALTIME,
		CLOCK_MONOTONIC,
		CLOCK_PROCESS_CPUTIME_ID,
		CLOCK_THREAD_CPUTIME_ID,
		(clockid_t)-1
	};

	for (size_t index = 0; clocks[index] != (clockid_t)-1; ++index) {
		struct timespec resolution;

		if (clock_getres(clocks[index], &resolution) == -1) {
			perror("clock_getres");
			continue;
		}

		printf("clock=%d sec=%ld nsec=%ld\n",
		       (int)clocks[index],
		       (long)resolution.tv_sec,
		       resolution.tv_nsec);
	}

	return EXIT_SUCCESS;
}
```

ただし、この値は「その clock API が表現できる分解能」を示すのであって、常に実測精度や実際の更新粒度と完全一致するとは限りません。
特に現代の Linux では高分解能タイマが有効だと、`CLOCK_REALTIME` や `CLOCK_MONOTONIC` でも 1ns と表示されることがあります。

つまり、古い説明にあるように「4ms と出たから HZ=250 だ」と単純に読む時代ではありません。
今は tickless と high-resolution timers の影響を踏まえて、分解能表示はあくまで API の性質の目安と見る方が安全です。

また、昔は POSIX clock 関数の利用時に `-lrt` が必要な環境がありました。
しかし、現代の glibc では多くの場合 libc 本体へ統合されており、通常は追加の `-lrt` が不要です。
古い環境や別 libc では必要な場合もあるため、移植時だけ個別確認すれば十分です。

### １０章の３　現在時刻の参照

現在時刻を得る用途はさまざまです。

```text
ユーザへ表示する
	現在日時を見せる

記録する
	ログやメタデータに残す

比較する
	タイムアウトや経過時間を判定する
```

ただし、ここでも「何の時計を見たいか」が重要です。
表示や記録なら `CLOCK_REALTIME`、経過時間なら `CLOCK_MONOTONIC`、CPU 使用量なら CPU 時間系、という切り分けが必要です。

もっとも古典的なインタフェースは `time()` です。

```c
#include <time.h>

time_t time(time_t *t);
```

これは epoch からの経過秒数で表した現在時刻を返します。
秒単位しか要らない場合は今でも十分有用です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	time_t now = time(NULL);

	if (now == (time_t)-1) {
		perror("time");
		return EXIT_FAILURE;
	}

	printf("current time: %lld\n", (long long)now);
	return EXIT_SUCCESS;
}
```

なお、`time_t` は「現実世界の秒そのもの」を完全厳密に表すわけではありません。
Unix 時刻系はうるう秒の扱いなどで天文学的厳密さを狙うものではなく、一貫したシステム時刻表現を提供することを主眼としています。

#### １０章の３の１　より良いインタフェース

`time()` を細かくした古典的な API が `gettimeofday()` です。

```c
#include <sys/time.h>

int gettimeofday(struct timeval *tv, void *tz);
```

実際の Linux では第 2 引数は使わず、常に `NULL` を渡します。
古い型表記では `struct timezone *` になっていますが、これは歴史的な名残です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

int main(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == -1) {
		perror("gettimeofday");
		return EXIT_FAILURE;
	}

	printf("seconds=%lld useconds=%ld\n",
	       (long long)tv.tv_sec,
	       (long)tv.tv_usec);
	return EXIT_SUCCESS;
}
```

ただし、新規コードでは `gettimeofday()` を第一選択にしない方がよい場面が多いです。
理由は次の通りです。

```text
wall clock 依存:
	時刻調整の影響を受ける

API が古い:
	より明確な clock_gettime() がある

単調時計を直接選べない:
	経過時間測定に不向き
```

#### １０章の３の２　高度なインタフェース

現在時刻取得の中心は `clock_gettime()` です。

```c
#include <time.h>

int clock_gettime(clockid_t clock_id, struct timespec *ts);
```

これにより、どの時計から時刻を取るかを明示できます。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct timespec realtime_now;
	struct timespec monotonic_now;

	if (clock_gettime(CLOCK_REALTIME, &realtime_now) == -1) {
		perror("clock_gettime realtime");
		return EXIT_FAILURE;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &monotonic_now) == -1) {
		perror("clock_gettime monotonic");
		return EXIT_FAILURE;
	}

	printf("realtime:  sec=%lld nsec=%ld\n",
	       (long long)realtime_now.tv_sec,
	       realtime_now.tv_nsec);
	printf("monotonic: sec=%lld nsec=%ld\n",
	       (long long)monotonic_now.tv_sec,
	       monotonic_now.tv_nsec);
	return EXIT_SUCCESS;
}
```

使い分けは次のように覚えるとよいです。

```text
現在日時を記録する:
	CLOCK_REALTIME

経過時間を測る:
	CLOCK_MONOTONIC

CPU 使用時間を測る:
	CLOCK_PROCESS_CPUTIME_ID
	CLOCK_THREAD_CPUTIME_ID
```

#### １０章の３の３　プロセス時間の参照

プロセスや子プロセスが消費した CPU 時間を見る古典的 API が `times()` です。

```c
#include <sys/times.h>

struct tms {
	clock_t tms_utime;
	clock_t tms_stime;
	clock_t tms_cutime;
	clock_t tms_cstime;
};

clock_t times(struct tms *buf);
```

意味は次の通りです。

```text
tms_utime:
	自プロセスのユーザ CPU 時間

tms_stime:
	自プロセスのシステム CPU 時間

tms_cutime:
	回収済み子プロセスのユーザ CPU 時間

tms_cstime:
	回収済み子プロセスのシステム CPU 時間
```

ここで注意すべきは、子プロセス時間は通常、終了後に親が `wait()` 系で回収してから反映されるという点です。

また、戻り値の `clock_t` それ自体に絶対的な意味を求めすぎない方がよいです。
古い Linux では uptime ベースの説明がされることもありましたが、今の学習では「差分を取るための値」と考える方が分かりやすいです。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/times.h>
#include <unistd.h>

int main(void)
{
	struct tms usage;
	clock_t ticks;
	long ticks_per_second;

	ticks = times(&usage);
	if (ticks == (clock_t)-1) {
		perror("times");
		return EXIT_FAILURE;
	}

	ticks_per_second = sysconf(_SC_CLK_TCK);
	if (ticks_per_second == -1) {
		perror("sysconf");
		return EXIT_FAILURE;
	}

	printf("user=%.6f sec system=%.6f sec\n",
	       (double)usage.tms_utime / (double)ticks_per_second,
	       (double)usage.tms_stime / (double)ticks_per_second);
	return EXIT_SUCCESS;
}
```

新規コードでは `clock_gettime(CLOCK_PROCESS_CPUTIME_ID, ...)` や `clock_gettime(CLOCK_THREAD_CPUTIME_ID, ...)` の方が素直なことも多いです。

### １０章の４　現在時刻の設定

通常アプリケーションが現在時刻を変更する場面は多くありません。
多くの場合、`date`、`timedatectl`、NTP クライアント、systemd-timesyncd などの専用機構が扱います。

それでも、時刻設定 API 自体を知っておく価値はあります。
ただし、この領域は特権が必要で、しかも古い API が混ざっています。

#### １０章の４の１　歴史的なインタフェース

古典的には `stime()` があります。

```c
#include <time.h>

int stime(const time_t *t);
```

しかし、これは歴史的 API であり、現代の新規コードで使うべきものではありません。
glibc でも新しい環境では前面に出てこないことがあり、実務では `clock_settime()` を考える方が自然です。

それでも意味としては単純で、`CLOCK_REALTIME` に相当するシステム時刻を秒単位で設定するものです。
当然、通常は強い権限が必要です。

#### １０章の４の２　精度の高い時刻設定

`gettimeofday()` に対応する古い設定 API は `settimeofday()` です。

```c
#include <sys/time.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz);
```

ここでも `tz` は歴史的互換の名残で、Linux では通常 `NULL` を渡します。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

int main(void)
{
	struct timeval tv = {
		.tv_sec = 314109260,
		.tv_usec = 271828,
	};

	if (settimeofday(&tv, NULL) == -1) {
		perror("settimeofday");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

ただし、これも通常アプリケーションから直接使う場面はかなり限られます。
特権が必要ですし、時刻ジャンプはシステム全体へ影響するため、運用上かなり慎重であるべきです。

#### １０章の４の３　時刻設定の高度なインタフェース

より現代的なのは `clock_settime()` です。

```c
#include <time.h>

int clock_settime(clockid_t clock_id, const struct timespec *ts);
```

通常、変更対象として意味を持つのは `CLOCK_REALTIME` です。
`CLOCK_MONOTONIC` のような単調時計をユーザが自由に巻き戻したり進めたりできるわけではありません。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct timespec ts = {
		.tv_sec = 314109260,
		.tv_nsec = 271828000,
	};

	if (clock_settime(CLOCK_REALTIME, &ts) == -1) {
		perror("clock_settime");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

`settimeofday()` と比べた利点は、ナノ秒単位で扱えることと、どの時計を対象にするかを API として明示できることです。

ただし実運用では、時刻を「ジャンプさせる」だけでなく、徐々に補正する仕組みも重要です。
NTP 系ツールや `adjtime()` 系の考え方が関係してきますが、それは時刻管理をさらに深く扱う場面で考えれば十分です。




