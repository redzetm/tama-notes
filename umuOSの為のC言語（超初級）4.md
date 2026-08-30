---
title: "UmuOSの為のC言語（超初級）4　ポインタ・文字列とポインタ 編"
---

# UmuOSの為のC言語（超初級）4 — ポインタ・文字列とポインタ 編

このシリーズは最終的に、UmuOS／ush／uim のソースコードを自力で読めるようになることを目標にします。  
ただし第4巻（第十章〜第十一章）も UmuOS に一切依存しない形で、C言語の基本の基本を丁寧に説明します。

この巻の範囲：

- 第十章：ポインタ（アドレス、`&` と `*`、ポインタと関数、ポインタと配列）
- 第十一章：文字列とポインタ（配列とポインタでの文字列、ポインタでの操作、標準ライブラリ関数）

前提：

- OS：Linux想定
- コンパイラ：`gcc`（`clang`でも基本同じ）
- 規格：`-std=c17`

コンパイルの基本形（例）：

```bash
gcc -Wall -Wextra -std=c17 -O0 sample.c -o sample
./sample
```

- `-Wall -Wextra`：警告をたくさん出す
- `-O0`：最適化なし（挙動を追いやすい）

---

## 第十章でやること：

- 「オブジェクト」「アドレス」「アドレス演算子 `&`」の関係を、実際のコードで確認する
- ポインタ（`int *p` など）が「アドレスを入れる変数」であることを理解する
- 間接演算子 `*` が「そのアドレスの先の値」を表すことを理解する
- ポインタを関数の引数にして、関数から複数の結果を返したり、呼び出し元の値を書き換えられる
- `scanf` が「書き込み先の場所」が必要な理由を、ポインタとして説明できる
- 空ポインタ（`NULL`）を使って「指していない」ことを表現できる
- 配列とポインタの関係（添字は `*(p+i)` と結び付く）を理解し、相違点（`sizeof`、再代入など）で迷わない

---

## ポインタ

ポインタは、C言語を読む上で避けて通れない要素です。
最初は「難しい記号に見える」かもしれませんが、この章では言葉と手を動かす確認を積み重ねて、混乱しやすい点を潰します。

### ポインタ

#### 関数の引数

第2巻で「引数は値渡し」と整理しました。
値渡しは分かりやすい一方で、「関数の中で呼び出し元の変数を書き換える」はできません。

書き換えられない例：

```c
/* file: swap_bad.c */
#include <stdio.h>

void swap_bad(int a, int b)
{
    int tmp = a;
    a = b;
    b = tmp;
}

int main(void)
{
    int x = 10;
    int y = 20;

    swap_bad(x, y);

    printf("x=%d y=%d\n", x, y);
    return 0;
}
```

このプログラムでは `x` と `y` は入れ替わりません。
`swap_bad` の `a` と `b` は「値のコピー」を受け取っているだけだからです。

ここで必要になるのが「変数そのものの場所（アドレス）を渡す」考え方です。

#### オブジェクトとアドレス

Cでは、変数の中身（値）だけでなく、その変数が置かれている場所（アドレス）も扱えます。

- 値：例 `x` の中身（10 など）
- アドレス：例 `x` が置かれている場所（番地）

この「値を持つ実体」のことを、ここではオブジェクトと呼びます。

#### アドレス演算子&

アドレス演算子 `&` は「オブジェクトのアドレスを取り出す」演算子です。

```c
/* file: address_of.c */
#include <stdio.h>

int main(void)
{
    int x = 123;

    printf("x=%d\n", x);
    printf("&x=%p\n", (void *)&x);

    return 0;
}
```

ポイント：

- `%p` はポインタ（アドレス）を表示する指定
- `printf` の `%p` には `(void *)` を渡すのが基本なので、`(void *)&x` と書く

注意：

- アドレスは環境や実行ごとに変わることがある（固定値として扱わない）

#### ポインタ

ポインタは「アドレスを入れる変数」です。
たとえば `int *p` は「`int` を指す（`int` のアドレスを入れる）ポインタ変数」です。

```c
/* file: pointer_basic.c */
#include <stdio.h>

int main(void)
{
    int x = 10;
    int *p = NULL;

    p = &x;

    printf("x=%d\n", x);
    printf("&x=%p\n", (void *)&x);
    printf("p =%p\n", (void *)p);

    return 0;
}
```

読み方：

- `int *p`：`int` のアドレスを入れる変数 `p`
- `p = &x;`：`x` のアドレスを `p` に入れる

この時点では「`p` と `&x` が同じ値になる」が確認できれば十分です。

#### ポインタ宣言の読み方（つまずきやすい所）

ポインタは、書き方より「読み方」を先に固めると事故が減ります。

基本：

- `int *p;` は「`int` を指す `p`」

よくある誤解：

```c
/* file: pointer_decl_pitfall.c */
#include <stdio.h>

int main(void)
{
    int *p, q;
    /* p は int* だが、q は int（ポインタではない） */
    (void)p;
    (void)q;
    return 0;
}
```

ポイント：

- `*` は「型」ではなく「宣言子」に結びつく
- 読み間違いを避けたいなら、1行に1変数で宣言するのが安全

```c
int *p;
int q;
```

もう一段：

- `int **pp;` は「`int *`（ポインタ）を指すポインタ」

後で「配列の配列」「文字列の配列」を扱うと `char **` が出ます。
ここで「ポインタのポインタ」という言い方に慣れておきます。

#### 間接演算子*

間接演算子 `*`（デリファレンス）は「そのアドレスの先にある値」を表します。

```c
/* file: deref_basic.c */
#include <stdio.h>

int main(void)
{
    int x = 10;
    int *p = &x;

    printf("x=%d\n", x);
    printf("*p=%d\n", *p);

    *p = 99; /* アドレスの先を書き換える */

    printf("x=%d\n", x);
    printf("*p=%d\n", *p);

    return 0;
}
```

ポイント：

- `p` はアドレス（場所）
- `*p` はその場所にある値

注意：

- `p` が「正しいアドレス」を持っていない状態で `*p` を使うのは危険
- まずは `p = &x;` のように、必ず指す先を決めてから使う

#### 未初期化ポインタ／寿命切れポインタ（未定義動作の王道）

ポインタで一番危ないのは「指す先が正しくないのに `*p` してしまう」ことです。
これは未定義動作になり得ます。

未初期化の例（危険）：

```c
/* file: wild_pointer.c */
#include <stdio.h>

int main(void)
{
    int *p;     /* 初期化していない */
    *p = 123;   /* どこを書き換えるか分からない（未定義動作） */
    return 0;
}
```

安全側の基本：

- まず `NULL` で初期化しておく
- 使う直前に、必ず「指す先」を決める

```c
int *p = NULL;
/* ... */
p = &x;
```

寿命切れ（ダングリング）の例（危険）：

```c
/* file: dangling.c */
#include <stdio.h>

int *bad(void)
{
    int x = 123;
    return &x; /* x は関数終了で寿命が切れる */
}

int main(void)
{
    int *p = bad();
    printf("%d\n", *p); /* 未定義動作 */
    return 0;
}
```

ポイント：

- 「ポインタを返す」＝「指す先の寿命が関数の外まで続く必要がある」

（文字列の章でも同じ落とし穴が出てきます。）

---

### ポインタと関数

#### 関数の引数としてのポインタ

ポインタを引数にすると「呼び出し元の変数の場所」を渡せます。
その結果、関数の中から呼び出し元の値を書き換えられます。

#### 和と差を求める関数

1つの関数で「和と差」を同時に返したい場合、返り値だけでは足りません。
そのときに「出力先の場所」を引数で渡します。

```c
/* file: sum_diff.c */
#include <stdio.h>

void sum_diff(int a, int b, int *sum, int *diff)
{
    *sum = a + b;
    *diff = a - b;
}

int main(void)
{
    int a = 10;
    int b = 3;
    int s = 0;
    int d = 0;

    sum_diff(a, b, &s, &d);

    printf("a=%d b=%d\n", a, b);
    printf("sum=%d diff=%d\n", s, d);
    return 0;
}
```

読み方：

- `sum_diff` の `sum` と `diff` は「書き込み先の場所」
- `*sum = ...;` は「呼び出し元の変数 `s` に書き込む」

注意：

- 呼び出し側で `&s` のようにアドレスを渡している

#### 2値の交換

先ほどの `swap_bad` を、ポインタを使って直します。

```c
/* file: swap_ok.c */
#include <stdio.h>

void swap_int(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

int main(void)
{
    int x = 10;
    int y = 20;

    swap_int(&x, &y);

    printf("x=%d y=%d\n", x, y);
    return 0;
}
```

ポイント：

- `swap_int` は「値」ではなく「場所」を受け取る
- `tmp = *a;` の `*a` は「`a` が指す先の値」

#### 2値のソート

2つの値を昇順に並べる関数も同じ考え方で作れます。

```c
/* file: sort2.c */
#include <stdio.h>

void swap_int(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void sort2(int *a, int *b)
{
    if (*a > *b) {
        swap_int(a, b);
    }
}

int main(void)
{
    int x = 20;
    int y = 10;

    sort2(&x, &y);

    printf("x=%d y=%d\n", x, y);
    return 0;
}
```

#### scanf関数とポインタ

`scanf` は「変数に値を書き込む」関数です。
そのため、`scanf` は「書き込み先の場所（アドレス）」が必要です。

```c
/* file: scanf_addr.c */
#include <stdio.h>

int main(void)
{
    int x = 0;

    puts("整数を1つ入力してください:");

    if (scanf("%d", &x) != 1) {
        puts("read failed");
        return 1;
    }

    printf("x=%d\n", x);
    return 0;
}
```

ポイント：

- `&x` を渡すのは「`x` の場所に書いてほしい」から
- `scanf("%d", x)` のように `&` を付けないと、未定義動作になり得る

補足：文字列入力では `&` を付けない

`int` では `&x` が必要でしたが、`char buf[16];` のような配列では事情が違います。
配列名 `buf` は多くの場面で「先頭要素へのポインタ」のように扱われるため、`scanf` には `buf` をそのまま渡します。

```c
/* file: scanf_string.c */
#include <stdio.h>

int main(void)
{
    char buf[16];

    puts("単語を入力してください:");

    /* 最大 15 文字 + 終端 '\0' */
    if (scanf("%15s", buf) != 1) {
        puts("read failed");
        return 1;
    }

    puts(buf);
    return 0;
}
```

注意：

- `scanf("%s", buf)` と幅指定をしないと、長い入力でバッファを壊しやすい
- `scanf("%15s", &buf)` のように `&` を付けるのは通常不要（型が合わず混乱の元）

#### 空ポインタ

「どこも指していない」状態を表す特別な値が `NULL` です。

```c
/* file: null_pointer.c */
#include <stdio.h>

int main(void)
{
    int *p = NULL;

    if (p == NULL) {
        puts("p is NULL");
    }

    return 0;
}
```

注意：

- `NULL` のまま `*p` を使うと危険（クラッシュなど）
- 「まだ指す先が決まっていない」ことを表すために `NULL` を使う

#### スカラ値

Cの型は大まかに次のように分けて考えると整理しやすくなります。

- スカラ値：1つの値として扱えるもの（`int`、`double`、ポインタなど）
- 配列：複数の要素が並ぶもの（`int a[10]` など）

ポインタはスカラ値です。
ただし「指す先」が配列や構造体などのまとまりであることがあります。

---

### ポインタと配列

この節は、UmuOS／ush／uim を読む上で特に重要です。
配列・文字列・バッファ操作の正体は、突き詰めると「ポインタ＋要素数（または終端）」です。

#### ポインタと配列

配列とポインタは「近い」概念です。
特に、配列名は多くの場面で「先頭要素を指すポインタのように扱われる」ことがあります。

まず、配列の各要素のアドレスを見てみます。

```c
/* file: array_addresses.c */
#include <stdio.h>

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };

    for (int i = 0; i < 5; i++) {
        printf("&a[%d]=%p value=%d\n", i, (void *)&a[i], a[i]);
    }

    return 0;
}
```

多くの環境では、要素のアドレスが一定の間隔で増えていくことが確認できます。

#### ポインタ演算（p+i が "iバイト" ではない理由）

`p + 1` は「1バイト進む」ではありません。
「指している型の要素 1個分」進みます。

```c
/* file: pointer_arith.c */
#include <stdio.h>

int main(void)
{
    int a[3] = { 10, 20, 30 };
    int *p = &a[0];

    printf("p     =%p\n", (void *)p);
    printf("p+1   =%p\n", (void *)(p + 1));
    printf("*(p+1)=%d\n", *(p + 1));

    return 0;
}
```

注意：

- ポインタ演算が意味を持つのは、基本的に「同じ配列の中」を指しているとき
- `p` が配列の外まで進んでしまうと危険

補足：

- `p` は「配列の末尾の1つ先」までなら作ってよい（one-past-end）
- ただし、その“1つ先”を `*p` で参照してはいけない

#### 間接演算子と添字演算子

添字（`a[i]`）は、ポインタの表現に直すと次と同じ意味になります。

- `a[i]` は `*(a + i)`

確認用の例：

```c
/* file: subscript_is_deref.c */
#include <stdio.h>

int main(void)
{
    int a[3] = { 10, 20, 30 };

    printf("a[1]=%d\n", a[1]);
    printf("*(a+1)=%d\n", *(a + 1));

    return 0;
}
```

ポイント：

- `a` が「先頭要素を指すもの」として扱われ、`a + 1` は「次の要素を指す」
- `*(a + 1)` が「次の要素の値」

注意：

- `a + 1` の `+ 1` は「1バイト」ではなく「要素1個分」進む（`int` なら `sizeof(int)` ぶん進む）

#### 配列とポインタの相違点

配列とポインタが似ているため、違いを明確にしておくのが重要です。

- 配列は「要素が連続した入れ物」そのもの
- ポインタは「どこかを指すスカラ値（アドレス）」

違いが出る例：

```c
/* file: sizeof_array_pointer.c */
#include <stdio.h>

int main(void)
{
    int a[5] = { 0 };
    int *p = a; /* 多くの場面で a は &a[0] のように扱われる */

    printf("sizeof(a)=%zu\n", sizeof(a));
    printf("sizeof(p)=%zu\n", sizeof(p));

    return 0;
}
```

- `sizeof(a)` は配列全体のバイト数（`5 * sizeof(int)`）
- `sizeof(p)` はポインタ変数そのもののバイト数（環境により 8 など）

さらに、配列は「再代入できない」点も大きな違いです。

```c
/* a = p; のような代入はできない */

#### &a と a の違い（配列へのポインタ）

`a` は多くの場面で「先頭要素へのポインタ」のように扱われますが、
`&a` は「配列そのもののアドレス」で、型が違います。

```c
/* file: pointer_to_array.c */
#include <stdio.h>

int main(void)
{
    int a[5] = { 0 };

    int *p = a;        /* &a[0] と同じ意味に近い */
    int (*pa)[5] = &a; /* "要素5個の int 配列" へのポインタ */

    printf("p  =%p\n", (void *)p);
    printf("pa =%p\n", (void *)pa);

    /* どちらも見た目の値は同じように見えることが多い */
    /* しかし、+1 の意味が違う */
    printf("p+1 =%p\n", (void *)(p + 1));
    printf("pa+1=%p\n", (void *)(pa + 1));

    return 0;
}
```

ポイント：

- `p + 1` は `int` 1個ぶん進む
- `pa + 1` は「配列5個分（a全体のサイズぶん）」進む

この違いは、低レイヤのコードで“型”を頼りにしている場面（バッファ操作など）で効いてきます。
```

#### 配列の受け渡し

関数に配列を渡すとき、関数側の引数 `int a[]` は見た目が配列ですが、実質的にはポインタとして扱われます。
そのため、関数側で `sizeof(a)` によって要素数を求めることは期待どおりになりません。

基本は「配列と一緒に要素数も渡す」です。

```c
/* file: sum_array_ptr.c */
#include <stdio.h>

int sum_array(const int *p, int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += p[i];
    }
    return sum;
}

int main(void)
{
    int a[] = { 10, 20, 30, 40 };
    int n = (int)(sizeof(a) / sizeof(a[0]));

    printf("sum=%d\n", sum_array(a, n));
    return 0;
}
```

ここでは `const int *p` として受けています。
これは「この関数は配列の中身を書き換えない」意図を表します。

#### const とポインタ（読み方で混乱を潰す）

`const` はポインタと一緒に出てくると混乱しやすいです。
結論から言うと「どれを変えてはいけないのか」を見分けます。

よく出る3つ：

```c
const int *p;       /* p が指す先の int を変更しない（読み取り専用） */
int * const p2 = 0; /* p2 自体（ポインタ変数）を別の場所に向け直さない */
const int * const p3 = 0; /* 指す先も、ポインタ変数も変えない */
```

読み方のコツ：

- `const` は「直後の型（または左側の型）」を修飾すると考える
- `const int *p` は「int は const、p はポインタ（向き先の値を変えない）」

最小の確認例：

```c
/* file: const_pointer_read.c */
#include <stdio.h>

int main(void)
{
    int x = 10;
    int y = 20;

    const int *p = &x;
    /* *p = 99; */ /* これは禁止：指す先の値を変えない約束 */
    p = &y;        /* これはOK：どこを指すかは変えてよい */

    int * const q = &x;
    *q = 99;       /* これはOK：指す先の値は変えてよい */
    /* q = &y; */  /* これは禁止：ポインタ変数を向け直さない約束 */

    printf("x=%d y=%d\n", x, y);
    return 0;
}
```

UmuOS系のコードでは「書き換えないポインタ引数」を `const` で表していることが多いので、
`const` は“安全のための情報”として積極的に読み取るのがコツです。

---

## 第十章まとめ

- `&x` は「`x` のアドレス（場所）」を取り出す
- ポインタは「アドレスを入れる変数」で、`int *p` は `int` のアドレスを入れる
- `*p` は「`p` が指す先の値」で、代入すると指す先を書き換えられる
- ポインタ宣言は読み間違えやすいので、`int *p, q;` の罠（q は int）を避ける
- 未初期化ポインタ／寿命切れポインタの `*p` は未定義動作になり得る（まず `NULL`、寿命を意識）
- ポインタを関数に渡すと、呼び出し元の変数を書き換えたり、複数の結果を返せる
- `scanf` が `&x` を必要とするのは「書き込み先の場所」が必要だから
- 文字列入力の `scanf("%s", buf)` は「配列名をそのまま渡す」（幅指定も重要）
- `NULL` は「どこも指していない」を表し、`NULL` のまま `*p` を使うのは危険
- 添字 `a[i]` は `*(a+i)` と同じ意味で、ポインタと配列は関係が深い
- ただし配列とポインタは別物で、`sizeof` や再代入の可否などで違いが出る
- `&a` は「配列へのポインタ」で型が違い、`+1` の意味も変わる

---

## 第十一章でやること：

- 文字列が「`\0` で終わる `char` の並び」であることを、ポインタの視点で捉え直す
- 配列による文字列（`char s[]`）と、ポインタによる文字列（`const char *p`）の違いを区別する
- 文字列の配列を2つの形（2次元配列、ポインタ配列）で扱える
- ポインタで文字列を走査し、長さ・コピーなどの基本操作を書ける
- 「ポインタを返す関数」で、文字列の中の位置を返す設計に慣れる
- 標準ライブラリ関数（`strlen`/`strcpy`/`strcat`/`strcmp`/`atoi` など）を安全側に倒して使う

---

## 文字列とポインタ

第3巻で文字列の基本を扱いました。
この章では、文字列を「ポインタと配列」の視点で整理し直します。

### 文字列とポインタ

#### 配列による文字列とポインタによる文字列

配列による文字列：

```c
/* file: string_array_vs_ptr.c */
#include <stdio.h>

int main(void)
{
    char s[] = "hello";
    s[0] = 'H';
    puts(s);
    return 0;
}
```

- `s` は配列で、内容を変更できる

ポインタによる文字列：

```c
/* file: string_ptr.c */
#include <stdio.h>

int main(void)
{
    const char *p = "hello";
    puts(p);
    return 0;
}
```

- `p` は「文字列リテラル」を指すポインタ
- 文字列リテラルは変更してはいけないものとして扱う

注意：

- `char *p = "hello";` のように `const` を外してもコンパイルできる環境があるが、書き換えは危険
- 文字列リテラルは `const char *` で受けるのが基本

#### 配列による文字列とポインタによる文字列の違い

違いが出やすい点：

- `sizeof`：配列は配列全体、ポインタはポインタ変数のサイズ
- 再代入：配列名は再代入できないが、ポインタ変数は別の文字列を指せる

```c
/* file: sizeof_string_forms.c */
#include <stdio.h>

int main(void)
{
    char s[] = "hi";
    const char *p = "hi";

    printf("sizeof(s)=%zu\n", sizeof(s));
    printf("sizeof(p)=%zu\n", sizeof(p));

    return 0;
}
```

`"hi"` は `'h' 'i' '\0'` の3文字なので、多くの環境で `sizeof(s)` は 3 になります。

#### 文字列の配列

複数の文字列を扱う2つの代表的な形です。

2次元配列：

```c
/* file: words_2d.c */
#include <stdio.h>

#define N 3
#define W 16

int main(void)
{
    char words[N][W] = { "apple", "banana", "cherry" };

    for (int i = 0; i < N; i++) {
        puts(words[i]);
    }

    return 0;
}
```

ポインタ配列：

```c
/* file: words_ptrs.c */
#include <stdio.h>

int main(void)
{
    const char *words[] = { "red", "green", "blue" };
    int n = (int)(sizeof(words) / sizeof(words[0]));

    for (int i = 0; i < n; i++) {
        puts(words[i]);
    }

    return 0;
}
```

ポイント：

- 2次元配列は「全ての文字列の入れ物」をまとめて確保する形
- ポインタ配列は「文字列を指すポインタを並べる」形

---

### ポインタによる文字列の操作

#### 文字列の長さを調べる

`strlen` を使う前に「ポインタで走査する形」を作ってみます。

```c
/* file: my_strlen.c */
#include <stdio.h>
#include <stddef.h>

size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0') {
        p++;
    }
    return (size_t)(p - s);
}

int main(void)
{
    const char *s = "hello";
    printf("len=%zu\n", my_strlen(s));
    return 0;
}
```

読み方：

- `p` を先頭に置く
- `\0` に当たるまで `p++` で進める
- 最後に `p - s` で「進んだ文字数」が出る

注意：

- `s` が `NULL` の場合はこの関数は使えない（呼び出し側で守る）

#### 文字列のコピー

コピーは「終端 `\0` まで」1文字ずつコピーします。

```c
/* file: my_strcpy.c */
#include <stdio.h>

char *my_strcpy(char *dst, const char *src)
{
    char *p = dst;

    while (*src != '\0') {
        *p = *src;
        p++;
        src++;
    }
    *p = '\0';

    return dst;
}

int main(void)
{
    char buf[16];
    my_strcpy(buf, "hi");
    puts(buf);
    return 0;
}
```

注意：

- `dst` のサイズが足りないとバッファオーバーランになり得る
- コピー元の長さに対して、コピー先の配列の大きさを必ず確保する

#### ポインタを返す関数

「文字列の中の位置」を返したいとき、ポインタを返す形が便利です。

例：最初に一致した文字の位置を返す

```c
/* file: find_char.c */
#include <stdio.h>

const char *find_char(const char *s, int ch)
{
    while (*s != '\0') {
        if ((unsigned char)*s == (unsigned char)ch) {
            return s;
        }
        s++;
    }
    return NULL;
}

int main(void)
{
    const char *s = "banana";
    const char *p = find_char(s, 'n');

    if (p != NULL) {
        printf("found: %s\n", p);
    } else {
        puts("not found");
    }

    return 0;
}
```

`found: %s` の部分では、「見つかった位置から後ろ」を文字列として表示できます。

注意：ポインタを返す関数の落とし穴

ポインタを返す関数は便利ですが、次のように「関数内のローカル変数（自動変数）」のアドレスを返してはいけません。

```c
/* file: bad_return_local.c */
#include <stdio.h>

const char *bad(void)
{
    char buf[] = "hello";
    return buf; /* 関数終了と同時に buf の寿命が切れる */
}

int main(void)
{
    puts(bad());
    return 0;
}
```

この例は未定義動作です。
「返したいデータの寿命が関数の外まで続くか」を常に意識します。

ポイント：

- 見つからない場合は `NULL` を返す
- 返ってくるポインタは「元の文字列の中」を指す（新しく確保しているわけではない）

---

### 文字列を扱うライブラリ関数

文字列関連は `<string.h>` にまとまっています。

- `strlen`：長さ
- `strcpy` / `strncpy`：コピー
- `strcat` / `strncat`：連結
- `strcmp` / `strncmp`：比較

数値変換は `<stdlib.h>` にあります。

- `atoi` / `atol` / `atoll` / `atof`

注意：

- これらは便利ですが「コピー先のサイズ不足」などで事故が起きやすい
- まずは「どの関数が何を前提にしているか」を明確にしてから使う

#### strlen関数：文字列の長さを調べる

```c
/* file: strlen_lib.c */
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *s = "hello";
    printf("len=%zu\n", strlen(s));
    return 0;
}
```

#### strcpy関数/strncpy関数：文字列をコピーする

`strcpy` は終端までコピーします。コピー先が十分大きいことが前提です。

`strncpy` は最大文字数を指定できますが、終端 `\0` が必ず付くとは限らない点に注意が必要です。

```c
/* file: strncpy_pitfall.c */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char dst[4];

    /* "hello" は長すぎる。終端が付かない可能性がある */
    strncpy(dst, "hello", sizeof(dst));

    /* dst が \0 終端されている保証がないため、表示は危険になり得る */
    dst[sizeof(dst) - 1] = '\0';

    puts(dst);
    return 0;
}
```

#### strcat関数/strncat関数：文字列を連結する

`strcat` は dst の末尾に src をつなぎます。
こちらも dst のサイズが十分であることが前提です。

`strncat` は「追加する最大文字数」を指定できますが、dst 側の残り容量計算が必要です。

#### strcmp関数/strncmp関数：文字列の大小関係を求める

`strcmp(a, b)` は次のような値を返します。

- 0：等しい
- 負：a が b より小さい
- 正：a が b より大きい

```c
/* file: strcmp_demo.c */
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("%d\n", strcmp("abc", "abc"));
    printf("%d\n", strcmp("abc", "abd"));
    printf("%d\n", strcmp("abd", "abc"));
    return 0;
}
```

#### atoi関数/atol関数/atoll関数/atof関数：文字列を数値に変換

`atoi` 系は最小の例としては使いやすいですが、変換に失敗したことを厳密に検出しにくい弱点があります。

```c
/* file: atoi_demo.c */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("%d\n", atoi("123"));
    printf("%d\n", atoi("12x"));
    printf("%f\n", atof("3.14"));
    return 0;
}
```

注意：

- 失敗時の扱いが分かりにくいので、厳密に扱いたい場合は後の巻で `strtol` 系を使う

---

## 第十一章まとめ

- 文字列は `\0` で終わる `char` の並びで、ポインタで1文字ずつ走査できる
- `char s[]` は配列（変更できる文字列）で、`const char *p` は文字列リテラルを指す形として扱う
- 2次元配列とポインタ配列で「文字列の配列」を表せる
- ポインタでの長さ計算は「`\0` まで進める」ことで書け、`p - s` で文字数を得られる
- コピーや連結は「コピー先の容量」を常に意識しないと事故になる
- `find_char` のように「文字列内の位置」をポインタで返す設計がよく出てくる
- 標準ライブラリ関数は便利だが前提があるため、サイズ不足や終端の扱いに注意して使う
