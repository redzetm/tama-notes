---
title: "UmuOSの為のC言語８（中級）　8章　メモリ管理"
---

# UmuOSの為のC言語（中級）　８

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結すると思います。

## ８章　メモリ管理

この章は、参考文献をもとに、自分用の研究ノートとして再構成していきます。
古い説明は、現在のLinuxやC17相当の書き方に寄せながら、UmuOSの設計へつながる形で整理します。

プロセスが使う資源の中でも、メモリは最も基礎的で重要なものです。
この章では、メモリの割り当て、初期化、サイズ変更、解放、そして仮想アドレス空間の見方を整理します。

ここでいうメモリ管理は、単に「たくさん確保する」話ではありません。
どの領域がいつ必要で、いつまで生かし、どこで返却するかを正しく設計することが本質です。

特に C 言語では、動的メモリの扱いをかなり直接的にプログラマが管理します。
そのため、メモリリーク、解放後使用、サイズ計算ミス、アラインメント違反といった問題が起きやすく、理解の価値が高い分野です。

本章では、スタック、ヒープ、マッピング、ページ、copy-on-write といった基礎から入り、そのうえで `malloc()`、`calloc()`、`realloc()`、`free()`、`posix_memalign()` などの実用APIを見ていきます。

### ８章の１　プロセスアドレス空間

現在の Linux では、プロセスは物理メモリを直接扱いません。
各プロセスには仮想アドレス空間が与えられ、プログラムはその仮想アドレス空間の中にあるかのようにメモリを使います。

```text
プロセスから見えるもの:
	仮想アドレス空間

カーネルとMMUが管理するもの:
	仮想アドレスから物理メモリや各種バックストアへの対応付け
```

この仕組みによって、プロセス同士は互いのメモリへ原則として直接干渉できず、また物理メモリより広いアドレス空間を使うことも可能になります。

仮想アドレス空間は、見た目としては 0 番地から始まる連続した番地の世界です。
しかし、すべての番地が常に使えるわけではありません。
実際には、未割り当て領域や保護された領域、ファイルマッピング領域などが混在しています。

#### ８章の１の１　ページとページイン/ページアウト

仮想アドレス空間の基本単位がページです。
ページサイズはアーキテクチャやカーネル設定に依存し、固定ではありません。

古い資料では「32ビットでは4KB、64ビットでは8KB」といった説明が見られることがありますが、これは今の Linux では一般則としては正確ではありません。
実際には x86-64 でも 4KB ページが標準的ですし、アーキテクチャによっては 16KB や 64KB を使うこともあります。
したがって、ページサイズは決め打ちせず、実行時に確認する前提で考える方が安全です。

```text
有効なページ:
	その仮想アドレスに意味のある対応先がある

無効なページ:
	未割り当て、またはアクセス不能
```

無効なページへアクセスすると、通常はセグメンテーション違反としてプロセスが落ちます。

一方で、有効なページでも、その時点ではまだ物理メモリに載っていないことがあります。
このとき、アクセスでページフォルトが発生し、カーネルが必要なページを用意します。

```text
ページイン:
	必要なページを物理メモリへ載せる

ページアウト:
	物理メモリから追い出し、必要なら別の保存先へ退避する
```

実際の Linux では、ページアウト先はスワップだけとは限りません。
ファイルマッピングなら元ファイルから再読込できるため、退避の考え方も匿名メモリとは少し異なります。

プログラムから見ると、こうした処理は原則として透過的です。
しかし、リアルタイム処理や大規模データ処理では、このページフォルトやページイン遅延が性能へ直接効いてきます。

##### ８章の１の１の１　ページ共有とcopy-on-write

複数の仮想ページが、同じ物理ページを共有することがあります。
これは同一プロセス内でも、別プロセス間でも起こり得ます。

代表例は次の2つです。

```text
読み取り専用の実行ファイルや共有ライブラリ:
	複数プロセスで同じ物理ページを共有できる

fork() 直後の親子プロセス:
	最初はページを共有し、書き込み時に分離する
```

この「書き込み時に初めて複製する」仕組みが copy-on-write、つまり COW です。

```text
読むだけなら共有のまま

書き込もうとしたときだけ専用ページを作る
```

この設計により、`fork()` は「メモリ全コピー」の印象よりずっと軽く動作できます。
5章でも扱ったように、親子は最初から全ページを複製しているわけではありません。

#### ８章の１の２　メモリ領域

カーネルは、アドレス空間を属性ごとの領域として管理します。
これをメモリ領域、セグメント、マッピングなどと呼びます。

学習上は、少なくとも次の領域を区別できるようにしておくと整理しやすいです。

```text
テキスト領域:
	実行コードや読み取り専用データ

スタック:
	関数呼び出しごとの自動変数や戻り情報

ヒープ:
	malloc() などで動的確保する領域

bss:
	未初期化グローバル変数や静的変数

ファイルマッピング領域:
	共有ライブラリ、mmap() されたファイル、匿名マッピングなど
```

古い本では「データセグメント、またはヒープ」とかなり近い言い方をすることがありますが、現在の Linux では `malloc()` が常に単純な `brk()` 領域だけを使うとは限りません。
大きな確保では `mmap()` ベースになることもあるため、「動的メモリはヒープだけ」と固定的に覚えるより、ユーザ空間の割り当て器が複数の仕組みを使い分ける、と理解した方が実態に近いです。

`bss` についても重要です。
未初期化グローバル変数は、実行ファイルにそのまま巨大なゼロ列を書き込むのではなく、より効率的な形で表現されます。
結果として「初期値 0 の大量データ」を比較的コンパクトに扱えます。

現在のプロセスのマッピングは `/proc/self/maps` や `pmap` で観察できます。
この観察は、UmuOS の学習にもかなり役立ちます。

### ８章の２　動的メモリの割り当て

スタック変数や静的変数もメモリの一部ですが、プログラム設計で難しくなるのは、実行時までサイズや寿命が決まらない動的メモリです。

たとえば次のような場面では、必要量をコンパイル時に決められません。

```text
入力ファイル全体を読み込みたい

ユーザ入力の長さが分からない

実行中に要素数が増減する配列を持ちたい
```

C では「動的メモリ上の変数」を宣言するのではなく、「必要サイズの生メモリを確保し、その先頭アドレスをポインタで扱う」形を取ります。

#### ８章の２の１　malloc()

動的メモリ確保の基本が `malloc()` です。

```c
#include <stdlib.h>

void *malloc(size_t size);
```

成功すると `size` バイト分のメモリ領域の先頭ポインタを返します。
失敗時は `NULL` を返し、通常は `errno` に `ENOMEM` が入ります。

重要なのは、`malloc()` が返すメモリは初期化されていないことです。

```text
malloc():
	内容は不定
	0 で埋まっていると仮定してはいけない
```

単純な例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	char *buffer = malloc(2048);
	if (buffer == NULL) {
		perror("malloc");
		return EXIT_FAILURE;
	}

	free(buffer);
	return EXIT_SUCCESS;
}
```

構造体を確保する場合は、型サイズを使う方が安全です。

```c
struct treasure_map *map = malloc(sizeof(*map));
if (map == NULL) {
	perror("malloc");
	return EXIT_FAILURE;
}
```

`sizeof(*map)` と書くと、型名を重複せずに済み、宣言変更にも比較的強くなります。

古い解説では、C++ と比較して `malloc()` の戻り値をキャストする例が出ます。
しかし C ではキャスト不要ですし、むしろ付けない方がよいです。
現在の C では暗黙関数宣言は許されませんが、それでも不要なキャストは型不一致やヘッダ忘れの発見を鈍らせます。

失敗時に即終了するラッパを作る設計もよく使われます。

```c
#include <stdio.h>
#include <stdlib.h>

void *xmalloc(size_t size)
{
	void *pointer = malloc(size);
	if (pointer == NULL) {
		perror("xmalloc");
		exit(EXIT_FAILURE);
	}

	return pointer;
}
```

ただし、ライブラリ内部で勝手に `exit()` してよいかは設計方針次第です。
アプリケーション本体なら便利ですが、再利用ライブラリではエラーを呼び出し側へ返した方が扱いやすいこともあります。

#### ８章の２の２　配列の割り当て

配列の動的確保では、要素サイズと要素数を分けて考えたい場面が多くあります。
そのために `calloc()` が用意されています。

```c
#include <stdlib.h>

void *calloc(size_t count, size_t size);
```

`calloc()` は `count * size` バイト分を確保し、内容をゼロ初期化します。

```c
int *x = malloc(50 * sizeof(*x));
int *y = calloc(50, sizeof(*y));
```

この2つは確保サイズ自体は同じですが、意味は違います。

```text
malloc():
	内容は不定

calloc():
	全バイト 0 で初期化される
```

そのため、直後に全要素を上書きしないなら、`calloc()` の方が安全です。

さらに `calloc()` には、`count * size` の計算に対するオーバーフロー検出が実装上考慮されることが多く、配列確保では意味が大きいです。
単純な `malloc(count * size)` より意図が明確になります。

古い資料では「整数 0 と浮動小数点 0 は違う」といった注意が出ることがありますが、`calloc()` のゼロ埋めは「全ビット 0」を行う操作です。
現代の実用環境では多くの型で期待どおりのゼロ初期値になりますが、言語仕様上の細部まで厳密に言えば「どんな抽象値でも常に完全に同義」と乱暴に一般化しない方が安全です。

`malloc0()` や `xmalloc0()` のような薄いラッパを作ることもできます。

```c
#include <stdio.h>
#include <stdlib.h>

void *malloc0(size_t size)
{
	return calloc(1, size);
}

void *xmalloc0(size_t size)
{
	void *pointer = calloc(1, size);
	if (pointer == NULL) {
		perror("xmalloc0");
		exit(EXIT_FAILURE);
	}

	return pointer;
}
```

#### ８章の２の３　メモリ領域のサイズ変更

一度確保した領域を伸ばしたり縮めたりしたいときは `realloc()` を使います。

```c
#include <stdlib.h>

void *realloc(void *ptr, size_t size);
```

`realloc()` は成功すると、新しいサイズの領域を返します。
ただし、そのアドレスが元の `ptr` と同じとは限りません。

```text
その場で拡張できた:
	同じアドレスのまま

その場で拡張できない:
	新しい領域を確保して内容をコピーし、古い領域を解放することがある
```

内容は、新旧サイズのうち小さい方まで保持されます。

`ptr == NULL` のときは `malloc(size)` に近い意味になります。
一方、`size == 0` の扱いは歴史的にやや癖があり、規格や実装差をまたぐと分かりにくい点です。
今の実務では「サイズ 0 に縮めるために `realloc()` を使う」より、明示的に `free()` する方が読みやすく、安全です。

`realloc()` を使うときの重要な注意は、失敗時に元の領域がそのまま有効だという点です。
そのため、戻り値をいきなり元の変数へ上書きしてはいけません。

```c
struct map *resized;

resized = realloc(original, sizeof(*original));
if (resized == NULL) {
	perror("realloc");
	/* original はまだ有効 */
	return -1;
}

original = resized;
```

配列拡張の例も載せておきます。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	size_t count = 2;
	int *values = calloc(count, sizeof(*values));
	int *grown;

	if (values == NULL) {
		perror("calloc");
		return EXIT_FAILURE;
	}

	values[0] = 10;
	values[1] = 20;

	count = 4;
	grown = realloc(values, count * sizeof(*values));
	if (grown == NULL) {
		perror("realloc");
		free(values);
		return EXIT_FAILURE;
	}

	values = grown;
	values[2] = 30;
	values[3] = 40;

	free(values);
	return EXIT_SUCCESS;
}
```

#### ８章の２の４　動的メモリの解放

`malloc()`、`calloc()`、`realloc()` で得た領域は、不要になったら `free()` で返却します。

```c
#include <stdlib.h>

void free(void *ptr);
```

解放対象は、確保時に返された先頭ポインタでなければなりません。
途中アドレスや、部分領域だけを `free()` することはできません。

`free(NULL)` は何もしません。
そのため、呼ぶ前に毎回 NULL 判定を書く必要は通常ありません。

簡単な例です。

```c
#include <stdio.h>
#include <stdlib.h>

void print_chars(int n, char c)
{
	for (int i = 0; i < n; i++) {
		char *string = calloc((size_t)i + 2U, sizeof(*string));
		if (string == NULL) {
			perror("calloc");
			break;
		}

		for (int j = 0; j < i + 1; j++) {
			string[j] = c;
		}

		printf("%s\n", string);
		free(string);
	}
}
```

ここでは各反復で確保した文字列を、使い終わったその場で返しています。
このように「確保した場所」と「返す場所」の対応を見失わない設計が重要です。

`free()` を忘れてポインタも失うと、メモリリークになります。
一方、`free()` 済み領域へ再びアクセスすると use-after-free です。
どちらも C では非常に多い不具合です。

```text
メモリリーク:
	返すべき領域を返さず、参照も失う

use-after-free:
	返却済み領域へアクセスする

double free:
	同じ領域を二重に free() する
```

古い資料には `MALLOC_CHECK_`、`mcheck()`、`mtrace()`、`mallinfo()` などの glibc 機能が挙がることがあります。
歴史的には有用ですが、現在は valgrind、AddressSanitizer、LeakSanitizer などの方がまず使われることが多いです。
また `mallinfo()` は古く、今は `mallinfo2()` が使われる場面があります。

##### ８章の２の４の１　アラインメント

アラインメントとは、ある型のデータが、ハードウェアにとって扱いやすい境界へ配置されているかという問題です。

たとえば 4 バイト整数が 4 の倍数アドレスへ置かれている、といった状態が典型です。

アラインメント規則は CPU アーキテクチャ依存です。
厳しい環境では、ずれた位置にあるデータを読み込むと例外になることもあります。
緩い環境でも、性能低下の原因になります。

通常はコンパイラと標準ライブラリが大半を面倒見てくれます。
`malloc()` 系が返すメモリも、標準的な型に必要なアラインメントは満たすようになっています。

ただし、ページ境界や SIMD 向けのより大きい境界に揃えたい場合は、明示的な API が必要になります。

###### ８章の２の４の１の１　アラインメントされたメモリの割り当て

POSIX では `posix_memalign()` を使います。

```c
#include <stdlib.h>

int posix_memalign(void **memptr, size_t alignment, size_t size);
```

成功すると 0 を返し、`alignment` 境界に揃えた `size` バイトの領域を `*memptr` へ返します。

`alignment` には条件があります。

```text
2 の累乗であること

sizeof(void *) の倍数であること
```

重要なのは、`posix_memalign()` は `malloc()` と違い、失敗理由を戻り値で返す点です。
`errno` に頼らず、戻り値を直接見ます。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	void *buffer;
	int ret = posix_memalign(&buffer, 256, 1024);
	if (ret != 0) {
		fprintf(stderr, "posix_memalign: %s\n", strerror(ret));
		return EXIT_FAILURE;
	}

	free(buffer);
	return EXIT_SUCCESS;
}
```

`posix_memalign()` で確保したメモリも `free()` で解放できます。

古い `valloc()` や `memalign()` は歴史的な API です。
現在のLinuxでは基本的に新規コードで使う理由は薄く、`posix_memalign()` を優先すべきです。

###### ８章の２の４の１の２　他のアラインメント問題

アラインメント問題は、動的確保APIだけでは終わりません。
構造体の配置、パディング、共用体、そして危険なポインタキャストでも起こります。

整理すると次のようになります。

```text
構造体:
	最大アラインメント要求の強いメンバに引っ張られる
	必要ならコンパイラがパディングを入れる

共用体:
	最大サイズ・最大アラインメント要求のメンバに合わせる

配列:
	要素型のアラインメントに従う
```

特に危険なのが、「より弱いアラインメントの場所を、より強い型として読む」ことです。

```c
char greeting[] = "Ahoy Matey";
char *cursor = &greeting[1];
unsigned long bad_news = *(unsigned long *)cursor;
```

このコードは、`cursor` が `unsigned long` に必要な境界へ揃っている保証がありません。
環境によっては性能低下で済みますが、厳しいアーキテクチャでは `SIGBUS` の原因にもなります。

そのため、バイト列から別型を読む必要があるなら、安易なキャストより `memcpy()` を使う方が安全なことが多いです。

```c
unsigned long value;
memcpy(&value, cursor, sizeof(value));
```

もちろん、エンディアンやサイズ差の問題は別途考える必要がありますが、少なくともアラインメント違反は避けやすくなります。

### ８章の３　データセグメントの管理

Unix 系システムには、古くからデータセグメントを直接伸縮させるためのインタフェースがあります。
ただし、現在の一般的なアプリケーションでこれを直接使う場面はほとんどありません。

理由は単純で、`malloc()` 系の方がはるかに使いやすく、安全で、実装依存の詳細を隠してくれるからです。
それでも、メモリ割り当ての下層で何が起きているかを理解するには、この古い層を知っておく価値があります。

```c
#include <unistd.h>

int brk(void *addr);
void *sbrk(intptr_t increment);
```

これらの名前は、ヒープとスタックが近い形で管理されていた古い Unix の歴史に由来します。
ヒープが下から上へ、スタックが上から下へ伸び、その境界を break point と呼んでいました。

現在の Linux でも、`brk()` / `sbrk()` は「プロセスのデータセグメント末尾を調整する」ための歴史的インタフェースとして残っています。

```text
brk():
	データセグメント末尾を指定アドレスへ動かす

sbrk():
	現在位置から相対的に増減させる
```

`sbrk(0)` で現在の break point を確認する、という古典的な使い方もあります。

```c
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	printf("current break point = %p\n", sbrk(0));
	return 0;
}
```

ただし、これはあくまで観察用・学習用と考えた方がよいです。
現代の Linux / glibc 環境では、`malloc()` が内部で `brk()` 領域だけを使うとは限らず、`mmap()` も組み合わせます。
そのため、アプリケーション側が `sbrk()` を直接使って glibc の割り当て器と共存しようとすると、設計を壊しやすいです。

結論としては次の通りです。

```text
学習用:
	brk()/sbrk() を知っていてよい

実用コード:
	通常は使わない
	malloc() 系と混ぜない
```

### ８章の４　無名メモリマッピング

glibc のメモリ割り当ては、歴史的にはデータセグメントを強く利用してきました。
しかし、現在の実装はそれだけではなく、大きな割り当てや状況に応じて `mmap()` も併用します。

古い資料では、ヒープ管理アルゴリズムとして単純なバディシステムを中心に説明することがあります。
考え方の導入としては悪くありませんが、現在の glibc はもっと複雑で、アリーナやキャッシュを含む洗練された割り当て戦略を使います。

それでも、なぜ `mmap()` ベースの割り当てが必要になるのか、という点は重要です。

```text
大きな領域をヒープ上で扱うと:
	断片化しやすい
	返却しづらい場合がある

独立した mmap 領域で扱うと:
	不要時に切り離して返しやすい
	他のヒープ領域へ影響しにくい
```

このため、大きなメモリ要求では、独立した無名メモリマッピングが使われることがあります。

#### ８章の４の１　無名メモリマッピングの特徴

無名メモリマッピングは、ファイルを伴わない `mmap()` です。
ファイルの代わりに、カーネルがゼロ初期化されたページを提供します。

主な長所は次の通りです。

```text
独立した領域として確保できる

不要になれば munmap() で領域ごと返しやすい

保護属性変更やアドバイスがしやすい

ゼロ初期化済みページを効率よく得られる
```

一方で短所もあります。

```text
ページ単位での管理になる
	小さい要求には無駄が出やすい

毎回カーネルとのやり取りが発生しやすい
	小粒な確保ではヒープより重いことがある
```

したがって、「すべて `mmap()` にすればよい」わけではありません。
小さな確保は通常の割り当て器、大きな確保は `mmap()` 側、という使い分けに合理性があります。

古い資料では「128KB を超えると mmap()」のような固定値が説明されることがあります。
しかし、この閾値は glibc バージョンや実装方針で変わり得ます。
値を前提にコード設計するのではなく、「大きい割り当てでは `mmap()` 由来になることがある」と理解する方が安全です。

#### ８章の４の２　無名メモリマッピングの作成

無名メモリマッピングは `mmap()` と `munmap()` で扱います。

```c
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
```

無名マッピングでは通常、次のように使います。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

int main(void)
{
	void *mapping = mmap(NULL,
			    512 * 1024,
			    PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS,
			    -1,
			    0);
	if (mapping == MAP_FAILED) {
		perror("mmap");
		return EXIT_FAILURE;
	}

	if (munmap(mapping, 512 * 1024) == -1) {
		perror("munmap");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

各引数の意味は次のように整理できます。

```text
addr:
	通常は NULL を渡して配置場所をカーネルに任せる

length:
	必要サイズ

prot:
	通常は PROT_READ | PROT_WRITE

flags:
	MAP_ANONYMOUS | MAP_PRIVATE が基本

fd と offset:
	匿名マッピングでは実質無視される
```

無名マッピングの内容は、最初からゼロ初期化されたように見えます。
これは、カーネルがゼロページや COW を活用して効率よく実現しているためです。

そのため、巨大なゼロ初期化領域が必要な場面では、`malloc()` と `memset()` を組み合わせるより、状況によっては `mmap()` や `calloc()` の方が理にかなっています。

#### ８章の４の３　/dev/zero のマッピング

古い Unix 系では `MAP_ANONYMOUS` がない環境もあり、その代わりに `/dev/zero` を `mmap()` して同様の効果を得る方法が使われてきました。

Linux でも歴史的にはこの方法が使われていました。
現在でも互換性目的で理解しておく価値はありますが、新規コードでは通常 `MAP_ANONYMOUS` を使います。

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/dev/zero", O_RDWR);
	void *mapping;

	if (fd == -1) {
		perror("open");
		return EXIT_FAILURE;
	}

	mapping = mmap(NULL,
		       (size_t)sysconf(_SC_PAGESIZE),
		       PROT_READ | PROT_WRITE,
		       MAP_PRIVATE,
		       fd,
		       0);
	if (mapping == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return EXIT_FAILURE;
	}

	if (close(fd) == -1) {
		perror("close");
		munmap(mapping, (size_t)sysconf(_SC_PAGESIZE));
		return EXIT_FAILURE;
	}

	if (munmap(mapping, (size_t)sysconf(_SC_PAGESIZE)) == -1) {
		perror("munmap");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

この方法は追加の `open()` / `close()` が必要なので、`MAP_ANONYMOUS` より素直さでも性能でも劣ることが多いです。

### ８章の５　高度なメモリ割り当て

glibc には、内部割り当て器の挙動をある程度調整する仕組みがあります。
歴史的によく知られているのが `mallopt()` です。

```c
#include <malloc.h>

int mallopt(int param, int value);
```

古い資料では `M_MMAP_THRESHOLD`、`M_TRIM_THRESHOLD`、`M_TOP_PAD` などが一覧で紹介されます。
考え方としては「ヒープと `mmap()` の使い分け」や「未使用領域をどれだけ抱えるか」を調整するものです。

ただし、ここはかなり glibc 実装依存です。
現在の glibc は昔より内部構造が複雑で、tcache なども入っています。
そのため、古い本のパラメータ説明をそのまま性能チューニング指針として信じるのは危険です。

学習上の位置づけとしては次のように考えるとよいです。

```text
mallopt():
	glibc 固有の割り当て器チューニング入口

利点:
	挙動観察や実験に使える

注意:
	移植性が低い
	glibc の版差の影響を受ける
	実運用ではまず計測が先
```

例えば、しきい値を変える例は次のようになります。

```c
#include <malloc.h>
#include <stdio.h>

int main(void)
{
	if (mallopt(M_MMAP_THRESHOLD, 64 * 1024) == 0) {
		fprintf(stderr, "mallopt failed\n");
		return 1;
	}

	return 0;
}
```

ただし、こうした調整は「とりあえず速そうだから」で入れるものではありません。
ワークロードと計測結果をもとに、必要なときだけ検討すべきです。

#### ８章の５の１　malloc_usable_size() と malloc_trim()

glibc には、さらに内部寄りの補助関数もあります。

```c
#include <malloc.h>

size_t malloc_usable_size(void *ptr);
int malloc_trim(size_t pad);
```

`malloc_usable_size()` は、そのポインタに対して実際に確保されている利用可能サイズを返します。
割り当て器の都合で、要求サイズより大きい領域が取られている場合があるためです。

```c
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	char *buffer = malloc(21);
	if (buffer == NULL) {
		perror("malloc");
		return EXIT_FAILURE;
	}

	printf("usable size = %zu\n", malloc_usable_size(buffer));
	free(buffer);
	return EXIT_SUCCESS;
}
```

ただし、ここで返る「余ったサイズ」を前提に書き込む設計は推奨されません。
プログラムが使ってよいサイズは、原則として自分が要求したサイズだけと考えるべきです。

`malloc_trim()` は、返却可能な未使用ヒープ領域を可能な限りカーネルへ戻そうとします。

```c
#include <malloc.h>

int malloc_trim(size_t pad);
```

`pad` バイトだけ余裕を残し、それ以外を返しにいくイメージです。
ただし、これも glibc 内部の状態に大きく依存します。

```text
教育・観察用途:
	有用

本番コードの常用:
	かなり慎重に考えるべき
```

移植性も低いため、一般的なアプリケーション設計では、これらへ依存しない方が扱いやすいです。

### ８章の６　メモリ割り当てのデバッグ

動的メモリの不具合は、C プログラムで特に厄介です。
二重解放、領域外書き込み、use-after-free、メモリリークなどは、症状が遅れて出ることも多く、原因特定が難しくなりがちです。

glibc には、古くからメモリ割り当ての検査や統計取得のための仕組みがいくつかあります。
歴史的には `MALLOC_CHECK_` がよく知られています。

```text
MALLOC_CHECK_=0:
	追加検査を実質無効化

MALLOC_CHECK_=1:
	警告を出す方向の動作

MALLOC_CHECK_=2:
	致命的エラーで異常終了しやすくする
```

起動時に環境変数を付けるだけで有効化できるので、再コンパイル不要という利点があります。

```text
$ MALLOC_CHECK_=1 ./rudder
```

ただし、今の Linux 開発では、まず valgrind や AddressSanitizer、LeakSanitizer などを使うことの方が一般的です。
`MALLOC_CHECK_` は手軽ではありますが、検出範囲や挙動の分かりやすさでは、より新しいツール群に劣る場面があります。

また、セキュリティ上の理由から、setuid 系プログラムではこの種の環境変数が無視されることがあります。

#### ８章の６の１　メモリ割り当て統計情報

古い glibc には、`mallinfo()` により割り当て器の統計を取得する仕組みがあります。

```c
#include <malloc.h>

struct mallinfo mallinfo(void);
```

ただし、これは現在では古い API です。
構造体メンバが `int` ベースで、巨大メモリ環境では不十分になりやすいため、今は `mallinfo2()` がある環境ではそちらを優先した方がよいです。

学習のため、まず古い `mallinfo()` の考え方を簡単に押さえると、割り当て器が次のような情報を持っていることが見えてきます。

```text
ヒープ由来の領域サイズ

未使用チャンク数

mmap 由来の領域数

割り当て済み総量

未使用総量
```

現代的には次のようなコードの方が扱いやすいです。

```c
#include <malloc.h>
#include <stdio.h>

int main(void)
{
	struct mallinfo2 info = mallinfo2();

	printf("allocated bytes = %zu\n", (size_t)info.uordblks);
	printf("free bytes      = %zu\n", (size_t)info.fordblks);
	return 0;
}
```

`mallinfo2()` が使えない環境では `mallinfo()` しかない場合もありますが、どちらにしてもこれらは glibc 依存です。
移植性の高い一般 API ではありません。

glibc には、標準エラーへ統計を出す `malloc_stats()` もあります。

```c
#include <malloc.h>

void malloc_stats(void);
```

ただし、これも実運用の性能分析ツールというよりは、観察や学習向けの道具と考える方が適切です。

### ８章の７　スタック上のメモリ割り当て

ここまでの動的メモリ確保は、主にヒープや匿名マッピングを使うものでした。
一方で、一時的な小さな領域なら、スタックを使う方法もあります。

歴史的に知られているのが `alloca()` です。

```c
#include <alloca.h>

void *alloca(size_t size);
```

`alloca()` は、現在の関数のスタックフレームに領域を積み増すような形でメモリを確保します。
その領域は、関数から return した時点で自動的に無効になります。

```text
malloc():
	明示的に free() が必要

alloca():
	関数を抜けると自動的に消える
```

このため、短命な一時バッファには便利です。
ただし、良いことばかりではありません。

```text
非標準である

巨大確保でスタックを壊しやすい

失敗を通常の API のように扱いにくい

呼び出し元へ返して使い続ける設計に向かない
```

古い資料では Linux で `alloca()` を強く勧める調子の説明が出ることがありますが、今の実務では少し慎重に見る方がよいです。
小さい一時領域には便利でも、濫用するとスタック消費量の見通しが悪くなります。

たとえば、設定ディレクトリ配下のパスを一時的に組み立てて開く例なら、次のように書けます。

```c
#include <alloca.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SYSCONF_DIR "/etc/"

int open_sysconf(const char *file, int flags, mode_t mode)
{
	const char *etc = SYSCONF_DIR;
	size_t name_len = strlen(etc) + strlen(file) + 1U;
	char *name = alloca(name_len);

	if (snprintf(name, name_len, "%s%s", etc, file) < 0) {
		return -1;
	}

	return open(name, flags, mode);
}
```

ここで確保した `name` は、この関数の return と同時に使えなくなります。
そのため、戻り値としてそのポインタを外へ渡してはいけません。

また、`alloca()` を関数引数の途中で呼ぶ書き方は避けるべきです。

```c
/* 推奨しない */
ret = foo(x, alloca(10));
```

評価順序やスタック操作との絡みで、読みづらく、環境差も招きやすいからです。

#### ８章の７の１　スタック上への文字列コピー

`alloca()` の分かりやすい用途の1つは、文字列の一時コピーです。

```c
#include <alloca.h>
#include <string.h>

void use_copy(const char *song)
{
	char *copy = alloca(strlen(song) + 1U);
	strcpy(copy, song);

	/* copy を使った一時処理 */
}
```

ただし、ここでもサイズが大きくなりすぎない前提が必要です。
巨大入力をそのまま `alloca()` すると、スタックオーバーフローの危険があります。

GNU 拡張として `strdupa()`、`strndupa()` もあります。

```c
#define _GNU_SOURCE
#include <string.h>

char *strdupa(const char *s);
char *strndupa(const char *s, size_t n);
```

これらは `alloca()` ベースで文字列を複製する GNU 専用の補助です。
便利ではありますが、移植性は低いので、Linux 専用コードで使うかどうかを意識して選ぶべきです。

#### ８章の７の２　可変サイズ配列

C99 では、実行時に要素数が決まる可変サイズ配列、VLA が導入されました。

```c
for (int i = 0; i < n; ++i) {
	char buffer[i + 1];
	/* buffer を使う */
}
```

この `buffer` は、各ループ反復のスコープを抜けるたびに自動的に消えます。
その点で、関数を抜けるまで残る `alloca()` とは性質が違います。

```text
alloca():
	関数終了まで残る

VLA:
	ブロックスコープを抜けると消える
```

この違いにより、ループ内で一時バッファを繰り返し使う場合、VLA の方がメモリ消費を抑えやすいことがあります。

一方で、現在の C では VLA の扱いに注意が必要です。
C11 以降、VLA は実装上 optional な機能になっており、コンパイラやプロジェクト方針によっては無効化されます。
そのため、「C17 だからどこでも安心して使える」とは言えません。

実用上の整理は次のようになります。

```text
小さい一時領域で、対象コンパイラが対応している:
	VLA は便利

移植性を重視する:
	VLA へ依存しない方が安全

サイズが大きくなり得る:
	VLA も alloca() も避ける
	malloc()/calloc() を使う
```

前節の例も VLA で書き換えられます。

```c
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SYSCONF_DIR "/etc/"

int open_sysconf(const char *file, int flags, mode_t mode)
{
	const char *etc = SYSCONF_DIR;
	size_t name_len = strlen(etc) + strlen(file) + 1U;
	char name[name_len];

	if (snprintf(name, name_len, "%s%s", etc, file) < 0) {
		return -1;
	}

	return open(name, flags, mode);
}
```

古い資料には「同じ関数で `alloca()` と VLA を混在させると予測不能」と強い表現が出ることがあります。
今のコンパイラ実装では必ずしもそう断言すべきではありませんが、少なくともスタック消費の見通しが悪くなり、可読性も落ちます。
同じ関数では、どちらか一方へ寄せる方が無難です。





