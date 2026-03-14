---
title: "UmuOSの為のC言語３　マクロ・入出力・文字列 編"
---

# UmuOSの為のC言語 3 — マクロ・入出力・文字列 編

このシリーズは最終的に、UmuOS／ush／uim のソースコードを自力で読めるようになることを目標にします。  
ただし第3巻（第八章〜第九章）も UmuOS に一切依存しない形で、C言語の基本の基本を丁寧に説明します。

この巻の範囲：

- 第八章：いろいろなプログラムを作ってみよう（関数形式マクロ、ソート、列挙体、再帰、入出力と文字）
- 第九章：文字列の基本（文字列の読み書き、文字列配列、標準ライブラリ関数での操作）

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

## 第八章でやること：

- 関数形式マクロ（`#define F(x) ...`）の書き方と落とし穴（括弧、複数回評価）を避けられる
- バブルソートの最小実装を書ける（配列、交換、外側/内側ループ）
- 列挙体（`enum`）で「状態」や「種類」を名前で表せる
- 再帰（関数が自分自身を呼ぶ）を、停止条件と小さい問題への分解で書ける
- `getchar` と `EOF` を使って「入力の終わり」を扱える
- 文字（文字コード、数字文字、エスケープ表記）を扱い、簡単な集計プログラムを書ける

---

## いろいろなプログラムを作ってみよう

この章は「小さな道具箱」を増やす章です。
OSのコードやツールのコードを読むと、マクロ、列挙体、ビット操作、文字処理、簡単なソートなどが頻繁に出てきます。

---

### 関数形式マクロ

#### 関数形式マクロ

マクロはプリプロセッサ（コンパイル前の段階）で展開されます。
関数形式マクロは「引数を受け取るマクロ」です。

```c
/* file: macro_square_bad.c */
#include <stdio.h>

#define SQUARE(x) x * x

int main(void)
{
    printf("%d\n", SQUARE(2 + 3));
    return 0;
}
```

このプログラムの意図は `(2 + 3) * (2 + 3) = 25` ですが、実際は次のように展開されます。

- `SQUARE(2 + 3)` → `2 + 3 * 2 + 3`

演算子の優先順位で `3 * 2` が先に計算され、結果が意図と違う形になります。

対策は「引数にも全体にも括弧を付ける」です。

```c
/* file: macro_square_ok.c */
#include <stdio.h>

#define SQUARE(x) ((x) * (x))

int main(void)
{
    printf("%d\n", SQUARE(2 + 3));
    return 0;
}
```

ポイント：

- マクロはただの文字列置換に近いので、括弧で守る
- `((x) * (x))` のように二重括弧で囲っておくと、周囲の式とも混ざりにくい

#### 関数と関数形式マクロ

関数形式マクロは便利ですが、関数と決定的に違う点があります。

- 引数が複数回評価され得る
- 型チェックが弱い（コンパイラが「関数呼び出し」としてチェックするわけではない）

引数が複数回評価される例：

```c
/* file: macro_side_effect.c */
#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int main(void)
{
    int x = 5;
    int y = 7;

    /* x++ が複数回評価される可能性がある */
    int m = MAX(x++, y);

    printf("m=%d x=%d y=%d\n", m, x, y);
    return 0;
}
```

この手のコードは結果が読み取りづらく、バグの元になります。

対策：

- マクロの引数には `x++` のような副作用のある式を渡さない
- 可能なら関数にする（関数なら引数は1回だけ評価される）

関数版：

```c
/* file: max_func.c */
#include <stdio.h>

int max_int(int a, int b)
{
    return (a > b) ? a : b;
}

int main(void)
{
    int x = 5;
    int y = 7;
    int m = max_int(x++, y);

    printf("m=%d x=%d y=%d\n", m, x, y);
    return 0;
}
```

ここでは `x++` が1回だけ評価されます。

#### 引数のない関数形式マクロ

引数がないマクロは「関数形式」ではなく「オブジェクト形式マクロ」になります。

```c
#define BUFSZ 128
```

ただし「見た目だけ関数っぽい」引数なしマクロも書けます。

```c
/* file: no_arg_macro.c */
#include <stdio.h>

#define HELLO() puts("hello")

int main(void)
{
    HELLO();
    return 0;
}
```

この形は書けますが、括弧の有無で別物になるため、混乱しやすいところです。
基本方針としては次のようにすると読みやすくなります。

- 定数なら `#define NAME 123`
- 関数なら本当に関数にする
- どうしてもマクロで「処理」を書くなら、次の「文のようなマクロ」の型を使う

#### 文のようなマクロ（do-while(0)）

複数文を含むマクロは、`do { ... } while (0)` で包むと安全に使いやすくなります。

```c
/* file: macro_stmt.c */
#include <stdio.h>

#define TRACE_INT(x) do { \
    printf(#x "=%d\n", (x)); \
} while (0)

int main(void)
{
    int a = 10;
    TRACE_INT(a);

    if (a > 0)
        TRACE_INT(a + 1);

    return 0;
}
```

ポイント：

- `#x` は「引数を文字列にする」演算（文字列化）
- `do { ... } while (0)` にしておくと、`if` の直後に置いても崩れにくい

#### 関数形式マクロとコンマ演算子

コンマ演算子 `,` は「左を評価して捨て、右を評価してそれを値とする」演算子です。

```c
/* file: comma_op.c */
#include <stdio.h>

int main(void)
{
    int x = 0;
    int y = (x = 10, x + 3);
    printf("x=%d y=%d\n", x, y);
    return 0;
}
```

- `x = 10` を評価して `x` を更新
- その後 `x + 3` を評価し、それが全体の値になる

この性質を利用して「表示した上で値としても使う」マクロが作れます。

```c
/* file: trace_and_value.c */
#include <stdio.h>

#define TRACE_AND_VALUE(x) (printf(#x "=%d\n", (x)), (x))

int main(void)
{
    int a = 5;
    int b = TRACE_AND_VALUE(a) + 10;
    printf("b=%d\n", b);
    return 0;
}
```

注意：

- ここでも引数が複数回評価される可能性があるため、`TRACE_AND_VALUE(x++)` のような呼び出しは避ける
- マクロの目的が「デバッグ補助」なのか「プログラムの本体」なのかを分けて使う

---

### ソート

#### バブルソート

バブルソートは「隣り合う要素を比較して、順番が逆なら交換する」を繰り返す方法です。
理解しやすい一方で、要素数が増えると遅くなります。

まずは交換（swap）を関数にしてから、ソート関数を作ります。

```c
/* file: bubble_sort.c */
#include <stdio.h>

void swap_int(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void bubble_sort(int a[], int n)
{
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (a[j] > a[j + 1]) {
                swap_int(&a[j], &a[j + 1]);
            }
        }
    }
}

void print_array(const int a[], int n)
{
    for (int i = 0; i < n; i++) {
        printf("%d ", a[i]);
    }
    putchar('\n');
}

int main(void)
{
    int a[] = { 5, 1, 4, 2, 8 };
    int n = (int)(sizeof(a) / sizeof(a[0]));

    puts("before:");
    print_array(a, n);

    bubble_sort(a, n);

    puts("after:");
    print_array(a, n);

    return 0;
}
```

ポイント：

- 内側ループは `j` と `j+1` を比較して入れ替える
- 外側ループを1回回すたびに、末尾側に大きい値が確定していく
- `n - 1 - i` までにして「確定部分」を比較しないようにできる

注意：

- `swap_int` はアドレスを受け取る（`int *`）ので、呼び出しは `&a[j]` の形になる
- `&x` は「変数 `x` の場所（アドレス）」を取り出す
- `int *p` は「`int` の場所を入れておく変数」
- `*p` は「その場所に入っている値」を表す（`swap_int` の中の `*a` や `*b`）

---

### 列挙体

#### 列挙体

列挙体 `enum` は「取り得る値が決まっているもの」を名前で表す仕組みです。

```c
/* file: enum_basic.c */
#include <stdio.h>

enum Weekday {
    WD_SUN,
    WD_MON,
    WD_TUE,
    WD_WED,
    WD_THU,
    WD_FRI,
    WD_SAT,
};

int main(void)
{
    enum Weekday d = WD_MON;
    printf("d=%d\n", (int)d);
    return 0;
}
```

ポイント：

- 列挙定数は通常 0 から順に増える（`WD_SUN` が 0）
- 表示するときは `int` に変換して `%d` を使う（この巻ではそれで十分）

#### 列挙定数

値を明示することもできます。

```c
enum HttpStatus {
    HTTP_OK = 200,
    HTTP_NOT_FOUND = 404,
};
```

#### 名前空間

列挙定数（`WD_MON` など）は「同じ名前空間」に出てきます。
別の `enum` で同じ列挙定数名を使うと衝突します。

そのため、列挙定数には接頭辞を付けるのがよくある作法です。

- `WD_...`、`HTTP_...`、`COLOR_...` のように「用途が分かる接頭辞」を付ける

---

### 再帰的な関数

#### 関数と型

再帰は「関数が自分自身を呼ぶ」形です。
大切なのは次の2つです。

- 停止条件（これがないと終わらない）
- 問題を小さくする（必ず停止条件に近づく）

また、返り値の型は「結果の範囲」を考えて決めます。

#### 階乗値

階乗（`n!`）は再帰の典型例です。

- `0! = 1`
- `n! = n * (n-1)!`

```c
/* file: factorial.c */
#include <stdio.h>

unsigned long long factorial(unsigned int n)
{
    if (n == 0) {
        return 1ULL;
    }
    return (unsigned long long)n * factorial(n - 1);
}

int main(void)
{
    for (unsigned int n = 0; n <= 10; n++) {
        printf("%u! = %llu\n", n, factorial(n));
    }
    return 0;
}
```

ポイント：

- 停止条件は `n == 0`
- `n` が 1 ずつ小さくなって停止条件に近づく

注意：

- `n` が大きいと結果が型の範囲を超えてオーバーフローする
- 再帰は呼び出し回数が増えるとスタックを使う（深すぎる再帰は避ける）

---

### 入出力と文字

#### getchar関数とEOF

`getchar` は標準入力から1文字読みます。
返り値は `int` です。

理由：

- 文字は `unsigned char` の範囲（0〜255など）
- それに加えて「入力の終わり」を表す `EOF`（通常 -1）が必要

そのため、`getchar()` の返り値を受ける変数は `int` にします。

```c
/* file: getchar_eof.c */
#include <stdio.h>

int main(void)
{
    int ch = 0;
    while ((ch = getchar()) != EOF) {
        putchar(ch);
    }
    return 0;
}
```

このプログラムは「入力をそのまま出力へコピー」します。

#### 入力から出力へのコピー

上の例は最小のコピーです。次は行数を数えながらコピーします。

```c
/* file: copy_and_count_lines.c */
#include <stdio.h>

int main(void)
{
    int ch = 0;
    int lines = 0;

    while ((ch = getchar()) != EOF) {
        putchar(ch);
        if (ch == '\n') {
            lines++;
        }
    }

    fprintf(stderr, "lines=%d\n", lines);
    return 0;
}
```

ポイント：

- 出力先を `stderr` にすると、コピー結果（stdout）と混ざりにくい

#### 数字文字のカウント

入力中に出てきた数字文字 `'0'`〜`'9'` を数えます。

```c
/* file: count_digits.c */
#include <stdio.h>

int main(void)
{
    int counts[10] = { 0 };
    int ch = 0;

    while ((ch = getchar()) != EOF) {
        if (ch >= '0' && ch <= '9') {
            counts[ch - '0']++;
        }
    }

    for (int i = 0; i < 10; i++) {
        printf("%d: %d\n", i, counts[i]);
    }

    return 0;
}
```

#### 文字コードと数字文字

`'0'`〜`'9'` が「連続した値」として並んでいることは、Cの言語仕様で保証されています。
そのため `ch - '0'` で 0〜9 の数に変換できます。

#### 拡張表記

文字や文字列には「エスケープ表記」があります。

よく使うもの：

- `\n`：改行
- `\t`：タブ
- `\\`：バックスラッシュ
- `\"`：ダブルクォート
- `\'`：シングルクォート

数で表す表記：

- 8進：`"\101"`（`A`）
- 16進：`"\x41"`（`A`）

```c
/* file: escape_demo.c */
#include <stdio.h>

int main(void)
{
    puts("A");
    puts("\101");
    puts("\x41");
    puts("line1\nline2");
    return 0;
}
```

注意：

- `\x..` 形式は「どこまでが16進数字か」が続く限り伸びるため、区切りを意識する（例：`"\x41" "BC"` のように隣接文字列で分ける）

---

## 第八章まとめ

- 関数形式マクロは「括弧で守る」「副作用のある式を渡さない」が基本
- 文のようなマクロは `do { ... } while (0)` で包むと崩れにくい
- バブルソートは隣接比較と交換を繰り返し、外側ループごとに末尾が確定する
- `enum` は値に名前を付け、列挙定数の衝突を避けるには接頭辞が有効
- 再帰は停止条件と小さい問題への分解が必須で、深すぎる再帰やオーバーフローに注意する
- `getchar` の返り値は `int` で受け、`EOF` を扱う
- `'0'`〜`'9'` の連続性を使って数字文字を数値に変換できる

---

## 第九章でやること：

- 文字列の正体が「`'\0'` で終わる `char` の並び」であることを理解する
- 文字列リテラルと文字配列の違い（変更してはいけないもの、変更できるもの）を区別する
- `scanf` と `fgets` で文字列を読み込み、バッファサイズを超えない形で扱える
- 文字列の配列（2次元配列、配列の配列）を作り、複数の単語を扱える
- `strlen`、`puts`、`ctype.h` を使って、長さ・表示・大小変換・数字文字の数え上げを行える
- 文字列の配列を関数に渡す基本形を書ける

---

## 文字列の基本

### 文字列とは

#### 文字列リテラル

`"hello"` のようなものを文字列リテラルと言います。
文字列リテラルの中身は「変更してはいけない」ものとして扱います。

```c
/* file: string_literal.c */
#include <stdio.h>

int main(void)
{
    const char *p = "hello";
    puts(p);
    return 0;
}
```

ここでは `const char *` として受けています。

#### 文字列

Cの文字列は「`'\0'`（ヌル文字）で終わる `char` の並び」です。

例：`"hi"` は次の3文字が並んでいます。

- `'h'`
- `'i'`
- `'\0'`（終端）

#### 文字配列の初期化

文字列を「変更できる形」で持ちたいときは、文字配列にします。

```c
/* file: char_array_init.c */
#include <stdio.h>

int main(void)
{
    char s1[] = "hello";                 /* {'h','e','l','l','o','\0'} */
    char s2[6] = { 'h','e','l','l','o','\0' };

    puts(s1);
    puts(s2);

    s1[0] = 'H'; /* 変更できる */
    puts(s1);

    return 0;
}
```

#### 空文字列

空文字列 `""` は「最初から終端文字だけ」の文字列です。

```c
char empty[] = ""; /* {'\0'} */
```

#### 文字列の読み込み

文字列の読み込みは、まず次の2つを押さえます。

- `scanf("%s", buf)`：空白で区切られた1語を読む（スペースを含む行は読めない）
- `fgets(buf, size, stdin)`：行を読む（スペースを含められる）

安全のため、`scanf` では「最大幅」を指定します。

```c
/* file: read_word_scanf.c */
#include <stdio.h>

int main(void)
{
    char buf[16];

    puts("1語入力してください:");
    if (scanf("%15s", buf) != 1) {
        puts("read failed");
        return 1;
    }

    printf("buf='%s'\n", buf);
    return 0;
}
```

- `%15s` は「最大15文字まで（終端 `\0` の分は別）」の意味

行を読みたい場合は `fgets` を使います。

```c
/* file: read_line_fgets.c */
#include <stdio.h>

void chomp_newline(char s[])
{
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n') {
            s[i] = '\0';
            return;
        }
    }
}

int main(void)
{
    char buf[64];

    puts("行を入力してください:");
    if (fgets(buf, (int)sizeof(buf), stdin) == NULL) {
        puts("read failed");
        return 1;
    }

    chomp_newline(buf);
    printf("buf='%s'\n", buf);
    return 0;
}
```

ポイント：

- `fgets` は改行も読み込むことがあるため、必要なら取り除く

#### 文字列を初期化して表示

```c
/* file: init_and_print.c */
#include <stdio.h>

int main(void)
{
    char msg[] = "UmuOS";
    printf("%s\n", msg);
    return 0;
}
```

---

### 文字列の配列

#### 文字列の配列

複数の文字列をまとめて扱うときは「文字の2次元配列」を使う方法があります。

```c
/* file: array_of_strings_2d.c */
#include <stdio.h>

int main(void)
{
    char words[3][16] = {
        "apple",
        "banana",
        "cherry",
    };

    for (int i = 0; i < 3; i++) {
        printf("%s\n", words[i]);
    }

    return 0;
}
```

- `words[i]` が「i番目の文字列（先頭へのポインタのように使える）」になる

#### 文字列の配列への文字列の読み込み

2次元配列に複数の単語を読み込みます。

```c
/* file: read_words.c */
#include <stdio.h>

#define N 3
#define W 16

int main(void)
{
    char words[N][W];

    for (int i = 0; i < N; i++) {
        printf("word[%d]: ", i);
        if (scanf("%15s", words[i]) != 1) {
            puts("read failed");
            return 1;
        }
    }

    puts("result:");
    for (int i = 0; i < N; i++) {
        printf("%s\n", words[i]);
    }

    return 0;
}
```

注意：

- `scanf` は空白区切りの1語しか読めない
- 行全体を読みたいなら `fgets` を組み合わせる

---

### 文字列の操作

#### 文字列の長さ

`strlen` は `\0` までの文字数（終端は数えない）を返します。

```c
/* file: strlen_demo.c */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char s[] = "hello";
    printf("len=%zu\n", strlen(s));
    return 0;
}
```

#### 文字列の表示

- `puts(s)`：末尾に改行を付けて出力
- `printf("%s\n", s)`：書式付きで出力

#### 数字文字の出現回数

文字列の中に出てくる数字文字を数えます。

```c
/* file: count_digits_in_string.c */
#include <stdio.h>

void count_digits(const char s[], int counts[10])
{
    for (int i = 0; i < 10; i++) {
        counts[i] = 0;
    }

    for (int i = 0; s[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch >= '0' && ch <= '9') {
            counts[ch - '0']++;
        }
    }
}

int main(void)
{
    char s[] = "a1b22c333";
    int counts[10];

    count_digits(s, counts);

    for (int i = 0; i < 10; i++) {
        printf("%d: %d\n", i, counts[i]);
    }
    return 0;
}
```

#### 大文字・小文字の変換

`toupper` / `tolower` を使うと文字の大小変換ができます。
`<ctype.h>` の関数は、引数に `unsigned char` 相当の値か `EOF` を渡すのが基本です。

```c
/* file: case_convert.c */
#include <stdio.h>
#include <ctype.h>

void to_upper(char s[])
{
    for (int i = 0; s[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)s[i];
        s[i] = (char)toupper(ch);
    }
}

int main(void)
{
    char s[] = "Hello, umuos!";
    to_upper(s);
    puts(s);
    return 0;
}
```

注意：

- 日本語などの多バイト文字はこの方法では扱えない（ここではASCII相当の範囲を対象にする）

#### 文字列の配列の受け渡し

文字列の配列（2次元配列）を関数に渡すときは「列数（1要素の幅）」が必要です。

```c
/* file: pass_string_table.c */
#include <stdio.h>

#define W 16

void print_words(char words[][W], int n)
{
    for (int i = 0; i < n; i++) {
        puts(words[i]);
    }
}

int main(void)
{
    char words[3][W] = { "one", "two", "three" };
    print_words(words, 3);
    return 0;
}
```

別の形として「ポインタ配列」を渡す方法もあります。

```c
/* file: pass_string_ptrs.c */
#include <stdio.h>

void print_words(const char *words[], int n)
{
    for (int i = 0; i < n; i++) {
        puts(words[i]);
    }
}

int main(void)
{
    const char *words[] = { "red", "green", "blue" };
    int n = (int)(sizeof(words) / sizeof(words[0]));
    print_words(words, n);
    return 0;
}
```

---

## 第九章まとめ

- Cの文字列は `\0` で終わる `char` の並び
- 文字列リテラルは変更しない。変更したい場合は `char[]` にする
- `scanf` は1語、`fgets` は1行の読み込みに向く。バッファサイズを超えない形で使う
- 文字列の配列は2次元配列で持てる。読み込みは最大幅指定で安全側に倒す
- `strlen` で長さ、`ctype.h` で大小変換、数字文字の数え上げなどができる
- 文字列の配列を関数に渡すときは、2次元配列なら列数が必要になる
