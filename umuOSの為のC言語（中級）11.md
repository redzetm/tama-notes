---
title: "UmuOSの為のC言語１１（中級）　付録　現代の Linux/C 設計で知っておく GCC/Clang 系拡張"
---

# UmuOSの為のC言語（中級）　１１

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結すると思います。

## 付録　現代の Linux/C 設計で知っておく GCC/Clang 系拡張

この付録は、古い GNU C 拡張一覧をそのまま写すものではありません。
現在の Linux ユーザ空間開発、C17 相当のコード、そして VSCode での gcc / clang ベースの開発を前提に、
今なお価値がある拡張だけを再整理したメモです。

重要なのは、次の順序で考えることです。

```text
第1優先:
	標準 C で書けるなら標準 C を使う

第2優先:
	安全性、可読性、診断品質が明確に向上する拡張だけ使う

第3優先:
	GCC 専用に見えても、実際には GCC/Clang で広く通るものを選ぶ
```

### 付録の１　GNU C と現代の位置づけ

かつて GNU C 拡張は、標準 C が足りなかった時代の重要な補助輪でした。
しかし現在は、`inline`、指定子付き初期化、`_Alignof` など、多くの便利機能が標準 C 側へ取り込まれています。

そのため、現代の書き方では「GNU C 拡張をたくさん覚える」のではなく、次のように分ける方が実用的です。

```text
すでに標準 C へ入ったもの:
	まず標準記法を使う

今も実務で便利な属性や built-in:
	必要な場面だけ使う

歴史的・特殊用途の拡張:
	既存コード読解用として知る
```

Linux カーネルや低レイヤライブラリでは GNU 系拡張が今も多用されます。
ただし、ユーザ空間アプリケーションでは「常に GNU 色を強くする」のが正解とは限りません。
gcc だけでなく clang でも通るか、可搬性をどこまで求めるか、診断をどこまで強めたいかで採用を決めるのがよいです。

### 付録の２　標準 C を優先した上で使う拡張

#### 付録の２の１　inline と always_inline

`inline` 自体は、もはや GCC 独自機能ではなく標準 C の一部です。

```c
static inline int max_int(int left, int right)
{
	return left > right ? left : right;
}
```

ここで重要なのは、`inline` は「必ず展開しろ」という命令ではなく、あくまで最適化ヒントに近いということです。

どうしても強制したい場面では、GNU 系属性を組み合わせられます。

```c
static inline __attribute__((always_inline)) int max_int(int left, int right)
{
	return left > right ? left : right;
}
```

ただし、これは多用すべきではありません。
現代の最適化器はかなり賢いため、真に必要な小関数だけに限定した方がよいです。

#### 付録の２の２　インライン展開の抑制

逆に、展開されると困る関数では `noinline` が有効です。

```c
__attribute__((noinline)) int trace_me(void)
{
	return 1;
}
```

デバッグ、スタック観測、プロファイラ支援、特殊なベンチマークでは意味があります。
ただし、通常のアプリケーションコードでは使用頻度は高くありません。

### 付録の３　今でも有用な属性

#### 付録の３の１　unused

シグナルハンドラやコールバックでは、未使用引数が出やすいです。

```c
static void sigint_handler(int signo __attribute__((unused)))
{
	/* ... */
}
```

これはかなり実用的です。
ただし、引数名を `(void)signo;` で明示的に消費する書き方も依然としてよく使われます。

#### 付録の３の２　warn_unused_result

戻り値を無視すると危険な関数には、有効な属性です。

```c
__attribute__((warn_unused_result)) int parse_config(const char *path);
```

`read()` や `write()`、自前の `parse_*()`、`open_*()`、`init_*()` のように、失敗を握り潰すと後で壊れる関数に向いています。

現在の C では C++ のような `[[nodiscard]]` が一般化していないため、Linux/C ではこの属性がまだ実用的です。

#### 付録の３の３　deprecated

古い API を残しつつ、新しい API へ移行したいときに使います。

```c
__attribute__((deprecated)) int old_init(void);
```

ライブラリや自作基盤コードでは、移行期間の通知として有用です。

#### 付録の３の４　noreturn

異常終了関数や、必ずプロセスを終わらせる関数では意味があります。

```c
__attribute__((noreturn)) void fatal(const char *message);
```

これにより、コンパイラの制御フロー解析が改善し、到達不能コードの扱いも明確になります。

#### 付録の３の５　malloc 属性

新しい別領域を返す関数に `malloc` 属性を付けると、エイリアス解析に役立つことがあります。

```c
__attribute__((malloc)) void *alloc_page(void);
```

ただし、現代では単に `malloc()` を包む程度なら、そこまで大きな効果を期待しすぎない方がよいです。
ライブラリ設計で意味が明確なときに限定するのが無難です。

#### 付録の３の６　pure と const

この 2 つは似ていますが、意味は少し違います。

```text
pure:
	外部状態を変更しない
	ただし読み取り専用の状態参照はあり得る

const:
	戻り値が引数だけで決まる
	メモリ状態への依存をさらに厳しく制限する
```

古い資料では `strlen()` を `pure` の例として出すことがありますが、実際にはこの種の最適化はコンパイラや libc 側でかなり賢く扱われます。
自前関数へ付ける場合は、意味を間違えないことの方が重要です。

付け間違えると、むしろ危険です。
「たぶん pure だろう」で付けるものではありません。

### 付録の４　レイアウト制御系の属性

#### 付録の４の１　packed

```c
struct __attribute__((packed)) packet_header {
	unsigned char type;
	unsigned short length;
};
```

`packed` は、パディングを抑えて詰め込む属性です。
ファイル形式、ネットワークプロトコル、ハードウェアレジスタ表現では必要になることがあります。

ただし、乱用は危険です。

```text
危険性:
	未整列アクセスになり得る
	性能低下や例外の原因になり得る
	普通のメモリ内データ構造には向かない
```

つまり、メモリ節約のために安易に付けるものではありません。

#### 付録の４の２　aligned

```c
int counter __attribute__((aligned(64))) = 0;
```

これは指定以上の境界へそろえる属性です。
キャッシュライン競合回避、SIMD、DMA、デバイス制約などで意味があります。

しかし、ユーザ空間アプリケーションでは毎回必要になるわけではありません。
必要性が明確なときだけ使うのが原則です。

#### 付録の４の３　alignof と標準 C

古い GNU 系コードには `__alignof__` が出てきます。
ただし現代の C では、まず標準の `_Alignof` を考える方が自然です。

```c
_Alignof(int)
```

GNU 記法を読む必要はありますが、新規コードで GNU 拡張を優先する理由は薄いです。

### 付録の５　分岐ヒントと built-in

#### 付録の５の１　likely / unlikely

Linux 系コードでは今でもよく見ます。

```c
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
```

エラー分岐を `unlikely()` にするような使い方は、低レイヤコードでは今もあります。

```c
int ret = close(fd);
if (unlikely(ret != 0)) {
	perror("close");
}
```

ただし、これは万能ではありません。

```text
使ってよい場面:
	本当に偏りが強いと分かっている分岐

避けたい場面:
	なんとなく速そうだから全部に付ける
```

予測が外れると、かえって悪化することがあります。

#### 付録の５の２　typeof と __auto_type

GNU 系では `typeof` が便利です。

```c
#define max_value(a, b) ({        \
	typeof(a) _a = (a);            \
	typeof(b) _b = (b);            \
	_a > _b ? _a : _b;             \
})
```

これは安全なマクロを書く文脈で強力です。
ただし、この例は statement expression も使っており、標準 C ではありません。

現代の GCC/Clang では `__auto_type` もあります。

```c
#define max_value(a, b) ({        \
	__auto_type _a = (a);           \
	__auto_type _b = (b);           \
	_a > _b ? _a : _b;              \
})
```

とはいえ、一般アプリケーションでは、マクロより `static inline` 関数で済むならそちらの方が読みやすいです。

#### 付録の５の３　offsetof

構造体メンバのオフセット取得は今も重要です。
ただし新規コードでは `<stddef.h>` の `offsetof()` を使えば十分です。

```c
#include <stddef.h>

size_t offset = offsetof(struct packet_header, length);
```

内部実装が `__builtin_offsetof` でも、利用者がそれを直接気にする必要は通常ありません。

### 付録の６　読解用に知っておくが、新規コードでは慎重なもの

#### 付録の６の１　範囲指定 switch-case

```c
switch (value) {
case 'A' ... 'Z':
	break;
default:
	break;
}
```

GNU 拡張としては便利ですが、標準 C ではありません。
既存コードでは見かけますが、可搬性重視なら通常の比較へ分解する方が安全です。

#### 付録の６の２　void ポインタ算術

GNU C では `void *` に対する加減算を 1 バイト単位として扱えます。
しかし、これは標準 C ではありません。

```c
pointer = (char *)pointer + 1;
```

今のコードでは、このように明示的に `char *` へ変換する方がよいです。

#### 付録の６の３　__builtin_return_address

デバッグや解析系では見かけますが、移植性も安定性も強くはありません。
最適化や ABI の影響を受けやすく、一般アプリケーションで気軽に使う対象ではありません。

#### 付録の６の４　グローバルレジスタ変数

これは歴史的には面白いものの、現代のユーザ空間アプリケーションで採用する場面はかなり限られます。
最適化器、ABI、デバッガ、スレッド、シグナル、可搬性の面で説明コストが高く、学習ノートの主力にはしない方がよいです。

### 付録の７　GCC/Clang 両対応の薄いラッパ

もし属性を多用するなら、ベタ書きより薄い互換マクロを置く方が見通しがよくなる場合があります。
ただし、昔の巨大な互換ヘッダをそのまま持ち込む必要はありません。

```c
#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if __has_attribute(warn_unused_result) || defined(__GNUC__)
#define UMU_NODISCARD __attribute__((warn_unused_result))
#else
#define UMU_NODISCARD
#endif

#if __has_attribute(unused) || defined(__GNUC__)
#define UMU_UNUSED __attribute__((unused))
#else
#define UMU_UNUSED
#endif

#if __has_attribute(noreturn) || defined(__GNUC__)
#define UMU_NORETURN __attribute__((noreturn))
#else
#define UMU_NORETURN
#endif
```

このくらいの薄さなら、意図も追いやすいです。
逆に、何十個も一度に定義し始めると、学習コストが一気に上がります。

### 付録の８　現代版としての結論

現代の Linux/C 開発では、GCC 拡張を「たくさん知っていること」自体が重要なのではありません。
重要なのは、標準 C で十分な部分と、拡張を使うことで本当に設計が良くなる部分を切り分けることです。

まず優先すべきなのは次のものです。

```text
実務で残りやすい:
	unused
	warn_unused_result
	deprecated
	noreturn
	packed
	aligned
	likely/unlikely

条件付きで有用:
	pure
	const
	malloc
	typeof
```

一方で、次のものは「読むための知識」として扱う方がよいです。

```text
読むための知識:
	グローバルレジスタ変数
	__builtin_return_address
	void ポインタ算術
	range case
```

### 付録の９　UmuOSでどう考えるか

UmuOS の観点では、この付録の価値は「GNU C の珍しい文法を覚える」ことではありません。
カーネル寄り、低レイヤ寄りのコードで、コンパイラへどこまで意図を伝えるべきかを学ぶ点にあります。

特に重要なのは次の整理です。

```text
安全性を高める属性:
	unused
	warn_unused_result
	noreturn

レイアウトや ABI に関わる属性:
	packed
	aligned
	offsetof

最適化のヒント:
	inline
	noinline
	likely/unlikely
	pure
	const
```

UmuOS でシェル、サーバ、低レイヤライブラリを書くなら、まずは診断品質を上げる属性から使うのがよいです。
その次に、バイナリ形式、ディスク構造、パケット構造、MMIO 風データなどで必要になったときに `packed` や `aligned` を慎重に使う、という順番が安全です。

逆に、最適化寄りの属性は、根拠なしに増やすべきではありません。
まず正しい設計、次に測定、その後で必要最小限のヒントを与える、という順序を守る方が長く保守できます。