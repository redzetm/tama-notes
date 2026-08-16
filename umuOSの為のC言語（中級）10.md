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

### １０章の５　さまざまな時間表現

Unix 系 OS と C ライブラリには、時間を次の形で相互変換する関数群があります。

```text
time_t:
	epoch からの秒数

struct tm:
	人が読みやすい日付時刻の分解表現

文字列:
	画面表示やログ出力向けのテキスト表現
```

古い API も多く、今では `strftime()` を中心に考える方が扱いやすい場面が多いのですが、既存コードとの遭遇率が高いので一通り知っておく価値はあります。

#### １０章の５の１　tm から文字列へ

古典的には `asctime()` があります。

```c
#include <time.h>

char *asctime(const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
```

`asctime()` は `struct tm` を固定形式の ASCII 文字列へ変換します。
ただし戻り値は内部静的領域を指すため、スレッドセーフではありません。

`asctime_r()` は呼び出し側が用意したバッファへ書き込む版です。
古い仕様では 26 バイト以上必要とされます。

とはいえ、新規コードで積極的に `asctime()` / `asctime_r()` を使う理由はあまりありません。
出力形式を制御しにくく、ロケールや可読性の面でも `strftime()` の方が普通は適しています。

#### １０章の５の２　tm から time_t へ

`struct tm` を epoch 秒へ戻すには `mktime()` を使います。

```c
#include <time.h>

time_t mktime(struct tm *tm);
```

これは `tm` の内容を、ローカルタイムとして解釈して `time_t` へ変換します。
また、各フィールドの正規化も行います。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct tm broken_down = {
		.tm_year = 2026 - 1900,
		.tm_mon = 8 - 1,
		.tm_mday = 16,
		.tm_hour = 12,
		.tm_min = 34,
		.tm_sec = 56,
		.tm_isdst = -1,
	};
	time_t value;

	value = mktime(&broken_down);
	if (value == (time_t)-1) {
		fputs("mktime failed\n", stderr);
		return EXIT_FAILURE;
	}

	printf("epoch seconds: %lld\n", (long long)value);
	return EXIT_SUCCESS;
}
```

古い説明では「内部で `tzset()` を呼ぶ」といった実装寄りの話が出ますが、利用者として重要なのは、`mktime()` がローカルタイムの解釈に依存することです。

#### １０章の５の３　time_t から文字列へ

`time_t` を直接文字列化する古典的 API が `ctime()` です。

```c
#include <time.h>

char *ctime(const time_t *timep);
char *ctime_r(const time_t *timep, char *buf);
```

`ctime()` も内部静的領域を返すため、スレッドセーフではありません。
また、返す文字列末尾に改行が含まれる点も癖があります。

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

	printf("the time a mere line ago: %s", ctime(&now));
	return EXIT_SUCCESS;
}
```

ただし、今のコードなら多くの場合 `localtime_r()` と `strftime()` を組み合わせる方が安全で柔軟です。

#### １０章の５の４　time_t から tm へ

UTC へ分解するなら `gmtime()`、ローカルタイムへ分解するなら `localtime()` を使います。

```c
#include <time.h>

struct tm *gmtime(const time_t *timep);
struct tm *gmtime_r(const time_t *timep, struct tm *result);

struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
```

`gmtime()` と `localtime()` は静的領域を返すため、スレッドセーフではありません。
新規コードでは、基本的に `_r` 版を優先してよいです。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	time_t now;
	struct tm local_result;

	now = time(NULL);
	if (now == (time_t)-1) {
		perror("time");
		return EXIT_FAILURE;
	}

	if (localtime_r(&now, &local_result) == NULL) {
		fputs("localtime_r failed\n", stderr);
		return EXIT_FAILURE;
	}

	printf("year=%d month=%d mday=%d\n",
	       local_result.tm_year + 1900,
	       local_result.tm_mon + 1,
	       local_result.tm_mday);
	return EXIT_SUCCESS;
}
```

`gmtime()` は UTC 固定で扱いたいログ処理やプロトコル処理で便利です。
`localtime()` は人が読む日時表示に向いています。

#### １０章の５の５　時間差の計算

`time_t` 同士の差を秒で求めるには `difftime()` があります。

```c
#include <time.h>

double difftime(time_t time1, time_t time0);
```

戻り値が `double` なのは、標準上 `time_t` の表現が単純整数に固定されていない歴史的事情もあるためです。
Linux では多くの場合、単純な減算と見なして差し支えないことが多いですが、可搬性を意識するなら `difftime()` を使う方が筋が通ります。

### １０章の６　システムの時計を合わせる

実時間を急に大きく飛ばすと、時刻に依存するアプリケーションは混乱します。
ビルドツール、キャッシュ、証明書検証、ログ解析、ジョブスケジューラなど、時刻順序に意味を持つ処理が壊れやすくなります。

そのため、単純な時刻ジャンプではなく、徐々に補正する仕組みが重要になります。

#### １０章の６の１　緩やかな補正

古典的には `adjtime()` があります。

```c
#include <sys/time.h>

int adjtime(const struct timeval *delta, struct timeval *olddelta);
```

これはシステム時計を即座に飛ばすのではなく、少し速く進めたり少し遅く進めたりして、徐々にずれを補正します。

```text
delta が正:
	時計を少し速める

delta が負:
	時計を少し遅くする
```

重要なのは、負方向でも通常は時計を巻き戻すのではなく、進み方を遅くして追い付かせるという発想です。
この性質により、`make` のような時刻依存ツールへの破壊的影響を少し抑えられます。

ただし、これは通常アプリケーション向けというより、NTP 系や時刻同期系の仕組み向けです。
一般アプリケーションが勝手に触るべきものではありません。

#### １０章の６の２　より高度な補正

Linux 固有には `adjtimex()` があります。

```c
#include <sys/timex.h>

int adjtimex(struct timex *adj);
```

これは `adjtime()` よりはるかに多くのパラメータを扱えます。
オフセット、周波数誤差、許容誤差、状態などを調整・参照できます。

```text
用途:
	NTP 実装や時刻同期デーモン向け

性質:
	Linux 固有
	通常アプリケーションには過剰
```

古い本では RFC 1305 との関連が前面に出ますが、学習上は「Linux が高機能な時計補正インタフェースを持っている」と理解すればまず十分です。
日常的なシステムプログラミングでは、これを直接扱うより、既存の時刻同期サービスの存在を前提に設計する方が自然です。

### １０章の７　スリープ

指定時間だけ処理を止める方法はいくつもあります。
ただし、今の Linux で新規コードを書くなら、基本は `nanosleep()` または `clock_nanosleep()` を考える方がよいです。

#### １０章の７の１　秒単位のスリープ

もっとも単純なのが `sleep()` です。

```c
#include <unistd.h>

unsigned int sleep(unsigned int seconds);
```

指定秒数だけ眠りますが、シグナルで割り込まれることがあります。
戻り値は「眠れなかった残り秒数」です。

```c
#include <unistd.h>

int main(void)
{
	sleep(7);
	return 0;
}
```

厳密に指定秒数だけ眠りたいなら、残り秒数が 0 になるまで繰り返す方法があります。

```c
unsigned int remaining = 5;

while ((remaining = sleep(remaining)) != 0) {
	/* interrupted by signal, continue sleeping */
}
```

ただし、秒単位は粒度が粗く、現代のプログラムでは用途がかなり限られます。

#### １０章の７の２　マイクロ秒単位のスリープ

古典的には `usleep()` があります。

```c
#include <unistd.h>

int usleep(useconds_t usec);
```

しかし、これは今では第一選択ではありません。

```text
理由:
	歴史的経緯で仕様の揺れがある
	POSIX 的には古い立場
	より明確な nanosleep() がある
```

そのため、新規コードでは `usleep()` を積極的に選ぶ理由はあまりありません。
既存コードを読むために知っておく API という位置づけが妥当です。

#### １０章の７の３　ナノ秒単位のスリープ

現代の基本は `nanosleep()` です。

```c
#include <time.h>

int nanosleep(const struct timespec *req, struct timespec *rem);
```

これにより、より細かい相対時間スリープができます。
シグナルで割り込まれた場合、`rem` を使って残り時間を再試行できます。

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct timespec req = {
		.tv_sec = 0,
		.tv_nsec = 200000,
	};

	if (nanosleep(&req, NULL) == -1) {
		perror("nanosleep");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

割り込まれても最後まで眠る例です。

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct timespec req = {
		.tv_sec = 0,
		.tv_nsec = 1369,
	};
	struct timespec rem;

	while (nanosleep(&req, &rem) == -1) {
		if (errno != EINTR) {
			perror("nanosleep");
			return EXIT_FAILURE;
		}

		req = rem;
	}

	return EXIT_SUCCESS;
}
```

#### １０章の７の４　高度なスリープ

より実用的なのが `clock_nanosleep()` です。

```c
#include <time.h>

int clock_nanosleep(clockid_t clock_id,
			    int flags,
			    const struct timespec *req,
			    struct timespec *rem);
```

これが重要なのは、使用する時計を選べることと、絶対時刻指定のスリープができることです。

```text
相対スリープ:
	今からどれだけ待つか

絶対スリープ:
	どの時点まで待つか
```

相対スリープでは、「現在時刻を読む」「目標時刻との差を計算する」「その差で眠る」の間に競合が入ります。
絶対スリープなら目標時刻そのものを渡せるため、この種のずれを減らせます。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct timespec ts = {
		.tv_sec = 1,
		.tv_nsec = 500000000,
	};

	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL) != 0) {
		perror("clock_nanosleep");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

絶対時刻まで眠る例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	struct timespec deadline;

	if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1) {
		perror("clock_gettime");
		return EXIT_FAILURE;
	}

	deadline.tv_sec += 1;

	if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) != 0) {
		perror("clock_nanosleep");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

なお、`clock_nanosleep()` は `nanosleep()` と違って、エラー時に `-1` ではなくエラー番号自体を返す実装仕様です。
この点は古い説明で省略されがちなので注意が必要です。

#### １０章の７の５　移植性のあるスリープ

非常に古い移植コードでは、`select()` をスリープ代わりに使う流儀があります。

```c
#include <sys/select.h>

int select(int nfds,
	   fd_set *readfds,
	   fd_set *writefds,
	   fd_set *exceptfds,
	   struct timeval *timeout);
```

ファイル記述子集合を全部 `NULL` にして、`timeout` だけ渡せば、一定時間待てます。

```c
struct timeval tv = {
	.tv_sec = 0,
	.tv_usec = 757,
};

select(0, NULL, NULL, NULL, &tv);
```

これは歴史的には有用でしたが、今の Linux 新規コードでスリープ目的だけに `select()` を使う理由はほぼありません。
本当に待ちたいのが時間だけなら `nanosleep()` 系の方が明確です。

#### １０章の７の６　時間の超過と設計上の注意

どのスリープ API でも、指定時間ぴったりに再開するとは限りません。
保証されるのは、多くの場合「少なくともそのくらいは眠る」ことです。

長引く理由は次のようなものです。

```text
スケジューリング遅延:
	起床可能になってもすぐ CPU をもらえない

タイマ分解能:
	要求時間が内部粒度より細かい

システム負荷:
	他の処理で起床が後ろへずれる
```

そのため、周期実行を組むときに「毎回相対時間で眠る」だけだと、ずれが蓄積しやすいです。
一定周期を維持したいなら、`CLOCK_MONOTONIC` と `TIMER_ABSTIME` を組み合わせた絶対時刻スリープの方が安定します。

また、設計論としては「スリープでごまかす」コードを増やしすぎないことが重要です。

```text
避けたい例:
	while 文で状態を見ながら sleep を繰り返す

望ましい例:
	ファイル記述子、イベント通知、タイマ fd、シグナル、待ち合わせ API でブロックする
```

イベント待機は、できる限りカーネルへ任せる方が効率も正確さもよくなります。
ビジーウェイトに近いポーリングへ短いスリープを散りばめる設計は、だいたい後で苦しくなります。

### １０章の８　タイマ

タイマは「指定した時刻または指定した間隔に達したら通知してほしい」という要求を表す仕組みです。
単なるスリープと違い、処理の停止そのものが目的ではなく、期限到達の通知が目的です。

```text
スリープ:
	自分が一定時間休む

タイマ:
	一定時間後に通知を受ける
```

実際の用途はかなり多いです。

```text
周期処理:
	1 秒間に 60 回画面を更新する

タイムアウト:
	500 ミリ秒以内に応答がなければ中止する

監視:
	一定時間ごとに状態確認する
```

Linux には複数のタイマ API がありますが、古いものほどシグナルに寄り、より新しいものほど柔軟で高機能です。

#### １０章の８の１　単純なアラーム

もっとも単純なのは `alarm()` です。

```c
#include <unistd.h>

unsigned int alarm(unsigned int seconds);
```

これは、指定秒数後に `SIGALRM` を送る予約を 1 つだけ持てる、非常に単純なタイマです。

```text
seconds > 0:
	その秒数後に SIGALRM を送る

seconds == 0:
	既存のアラームを取り消す
```

ただし、いまの観点では用途はかなり限定的です。

```text
弱点:
	秒単位で粗い
	通知が SIGALRM 固定
	1 プロセスあたり 1 本の単純な予約しか扱いにくい
```

説明用の最小例です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t g_fired = 0;

static void alarm_handler(int signo)
{
	(void)signo;
	g_fired = 1;
}

int main(void)
{
	if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
		fputs("signal failed\n", stderr);
		return EXIT_FAILURE;
	}

	alarm(5);
	while (!g_fired) {
		pause();
	}

	puts("Five seconds passed");
	return EXIT_SUCCESS;
}
```

古いサンプルではハンドラ内で `printf()` を呼ぶことがありますが、前章の通り、今は避けた方が安全です。

#### １０章の８の２　インターバルタイマ

`alarm()` を拡張した古典的 API が `getitimer()` / `setitimer()` です。

```c
#include <sys/time.h>

int getitimer(int which, struct itimerval *value);
int setitimer(int which, const struct itimerval *value, struct itimerval *ovalue);
```

タイマ種別は次の 3 つです。

```text
ITIMER_REAL:
	実時間ベース
	満了で SIGALRM

ITIMER_VIRTUAL:
	ユーザ空間 CPU 時間ベース
	満了で SIGVTALRM

ITIMER_PROF:
	ユーザ空間 + カーネル空間 CPU 時間ベース
	満了で SIGPROF
```

設定値は `struct itimerval` で与えます。

```c
struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};
```

意味は次の通りです。

```text
it_value:
	最初の満了までの残り時間

it_interval:
	満了後に自動再装填する周期
```

つまり、`it_interval` を 0 にすればワンショット、非 0 にすれば周期タイマになります。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t g_hits = 0;

static void alarm_handler(int signo)
{
	(void)signo;
	++g_hits;
}

int main(void)
{
	struct itimerval delay = {
		.it_interval = { .tv_sec = 1, .tv_usec = 0 },
		.it_value = { .tv_sec = 5, .tv_usec = 0 },
	};

	if (signal(SIGALRM, alarm_handler) == SIG_ERR) {
		fputs("signal failed\n", stderr);
		return EXIT_FAILURE;
	}

	if (setitimer(ITIMER_REAL, &delay, NULL) == -1) {
		perror("setitimer");
		return EXIT_FAILURE;
	}

	while (g_hits == 0) {
		pause();
	}

	puts("Timer hit");
	return EXIT_SUCCESS;
}
```

ただし、これも通知がシグナル中心なので、イベントループ主体の新規コードでは第一選択にならないことが多いです。

#### １０章の８の３　高度なタイマ

POSIX timer API は、より柔軟なタイマ機構です。
作成、設定、参照、削除が分離されています。

```text
timer_create():
	タイマを作る

timer_settime():
	時間を設定して動かす

timer_gettime():
	現在値を読む

timer_getoverrun():
	超過回数を読む

timer_delete():
	削除する
```

##### １０章の８の３の１　タイマの作成

```c
#include <signal.h>
#include <time.h>

int timer_create(clockid_t clockid, struct sigevent *evp, timer_t *timerid);
```

これは、指定したクロックを使うタイマオブジェクトを作るだけで、まだ動きません。

`evp` で通知方法を指定できます。

```text
SIGEV_NONE:
	何も通知しない

SIGEV_SIGNAL:
	指定シグナルを送る

SIGEV_THREAD:
	ライブラリが新しいスレッドで関数を呼ぶ
```

ここで重要なのは、`SIGEV_THREAD` は「カーネルが直接スレッドを作る」単純像で理解しないことです。
実際には libc 実装も関与するため、学習上は「タイマ満了時に関数コールバック風に処理できる高水準通知」と考える方が安全です。

簡単な例です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	timer_t timer;

	if (timer_create(CLOCK_REALTIME, NULL, &timer) == -1) {
		perror("timer_create");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

##### １０章の８の３の２　タイマの設定

```c
#include <time.h>

int timer_settime(timer_t timerid,
		      int flags,
		      const struct itimerspec *value,
		      struct itimerspec *ovalue);
```

設定構造は `struct itimerspec` です。

```c
struct itimerspec {
	struct timespec it_interval;
	struct timespec it_value;
};
```

意味は `setitimer()` とほぼ同じですが、分解能は `timespec` なのでナノ秒単位です。

```text
it_value:
	最初の満了までの時間

it_interval:
	周期再装填間隔
```

`flags` に `TIMER_ABSTIME` を渡すと、相対値ではなく絶対時刻として扱います。
周期処理のドリフト抑制に有効です。

```c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void)
{
	timer_t timer;
	struct itimerspec ts = {
		.it_interval = { .tv_sec = 1, .tv_nsec = 0 },
		.it_value = { .tv_sec = 1, .tv_nsec = 0 },
	};

	if (timer_create(CLOCK_REALTIME, NULL, &timer) == -1) {
		perror("timer_create");
		return EXIT_FAILURE;
	}

	if (timer_settime(timer, 0, &ts, NULL) == -1) {
		perror("timer_settime");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

##### １０章の８の３の３　タイマ時間の参照

```c
#include <time.h>

int timer_gettime(timer_t timerid, struct itimerspec *value);
```

これにより、タイマを再設定せずに残り時間や周期設定を読めます。

```c
struct itimerspec ts;

if (timer_gettime(timer, &ts) == -1) {
	perror("timer_gettime");
}
```

##### １０章の８の３の４　タイマ超過の参照

```c
#include <time.h>

int timer_getoverrun(timer_t timerid);
```

通知を処理しきれないうちに満了が重なった回数を調べられます。

ただし、ここで重要なのは「すべての満了を完全に数え上げる高信頼キュー」と思い込まないことです。
通知方法やシグナル配送の性質も絡むため、用途に応じた解釈が必要です。

##### １０章の８の３の５　タイマの削除

```c
#include <time.h>

int timer_delete(timer_t timerid);
```

使い終わったタイマは削除します。
プロセス終了時にまとめて片付く場合もありますが、長寿命プロセスでは明示的に管理した方が分かりやすいです。

#### １０章の８の４　今の Linux での見方

本の時代背景では、シグナル通知型タイマがかなり前面に出ています。
今でも API 自体は有効ですが、現代の Linux アプリケーションでは、イベントループと相性のよい `timerfd` を選ぶ場面も多いです。

```text
シグナル通知型タイマ:
	既存の signal 設計に乗せやすい

timerfd:
	fd として epoll/select/poll に載せやすい
	イベントループと相性がよい
```

つまり、現代的な設計では「タイマ満了をシグナルで受けるか、fd で受けるか」という選択肢があります。
この章では POSIX / 古典 Unix の流れに沿ってシグナル系タイマを中心に学びつつ、実装時には `timerfd` も候補に入れる、という整理が実践的です。

### １０章の９　UmuOSでどう考えるか

UmuOS の観点では、この章の核は「時刻そのもの」よりも、「時間をどの基準で測り、どの単位で通知し、どの API で見せるか」です。

特に重要なのは、次の 3 つを分離して考えることです。

```text
実時間:
	人に見せる日付と時刻

単調時間:
	経過時間やタイムアウトの基準

CPU 時間:
	処理量や負荷の観測
```

この分離ができていないと、タイムアウトに壁時計を使ってしまったり、NTP 補正で周期処理がずれたり、プロファイル計測に不適切な時計を使ったりします。

UmuOS を発展させるなら、最初から Linux ほど多機能である必要はありませんが、少なくとも段階的には次の順で実装すると理解しやすいです。

```text
第1段階:
	tick カウンタまたは単純な単調時間
	sleep 相当

第2段階:
	実時間の保持
	ファイル時刻やログ時刻の管理

第3段階:
	タイマ割り込み
	プロセスやタスクへの通知

第4段階:
	複数クロック
	高分解能タイマ
	絶対時刻タイマ
```

また、UmuOS のシェルやサーバや監視ツールを考えると、次の感覚が重要です。

```text
待つだけなら sleep ではなく待ち合わせ機構を使う

周期処理は相対時間の繰り返しより絶対時刻基準で組む

表示用の時刻と制御用の時刻を混同しない
```

この章を学ぶ意味は、時刻 API の名前を覚えることではありません。
OS が時間をどうモデル化し、プログラムへどのように渡し、どこで誤差や競合が入り得るかを理解することにあります。




