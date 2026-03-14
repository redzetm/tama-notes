---
title: "UmuOSの為のC言語２　配列編"
---

# UmuOSの為のC言語 2 — 配列編（配列と多次元配列）

このシリーズは最終的に、UmuOS／ush／uim のソースコードを**自力で読める**ようになることを目標にします。  
ただし **第2巻もUmuOSに一切依存しない**「C言語の基本の基本」を、できる限りやさしく・丁寧に説明します。

前提：

- OS：Linux想定
- コンパイラ：`gcc`（`clang`でも基本同じ）
- 規格：`-std=c17`

コンパイルの基本形（例）：

```bash
gcc -Wall -Wextra -std=c17 -O0 array_basic.c -o array_basic
./array_basic
```

- `-Wall -Wextra`：警告をたくさん出す
- `-O0`：最適化なし（挙動を追いやすい）

---

## 第五章でやること：

- 配列を「同じ型の値を並べて持つ入れ物」として理解する
- 添字（インデックス）を使って、配列の要素にアクセスできる
- `for` で配列を走査し、表示・合計・最大/最小などの基本処理が書ける
- 初期化の基本形（全要素0、列挙初期化、要素数の自動決定）を使い分けられる
- 入力を配列に読み込み、範囲外アクセスを避ける考え方を身に付ける
- 配列の並びを反転したり、別の配列へコピーできる
- 2次元配列を「行×列」として扱い、2重ループで走査できる

前提：配列はC言語の中心的な要素です。配列を押さえると、文字列（`char[]`）や、構造体の配列、バッファ処理などが読みやすくなります。

---

## 配列

### 配列

配列は「同じ型の値を、順番に並べて持つ」仕組みです。

例：`int` を5個並べて持つ配列

```c
/* file: array_basic.c */
#include <stdio.h>

int main(void)
{
    int a[5];

    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    a[3] = 40;
    a[4] = 50;

    printf("a[0]=%d\n", a[0]);
    printf("a[4]=%d\n", a[4]);

    return 0;
}
```

ここで重要な決まり：

- `a[0]` が先頭（0から始まる）
- `a[4]` が最後（要素数5なら、最後の添字は4）

注意：範囲外アクセスは危険です。

- `a[5]` のように、存在しない要素に触ると未定義動作になり得ます
- 未定義動作は「たまたま動く」こともあるため、バグが見えにくくなります

### 配列の走査

配列の全要素を順に処理することを **走査** と呼びます。
走査は `for` と相性がよいです。

```c
/* file: array_traverse.c */
#include <stdio.h>

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };

    for (int i = 0; i < 5; i++) {
        printf("a[%d]=%d\n", i, a[i]);
    }

    return 0;
}
```

読み方：

- `i` を 0 から始める
- `i < 5` の間だけ繰り返す
- `i++` で 1 ずつ進める

この形は配列を読むコードで頻出です。

### 配列の初期化

配列は宣言と同時に初期化できます。

```c
/* file: array_init.c */
#include <stdio.h>

int main(void)
{
    int a[5] = { 1, 2, 3, 4, 5 };
    int b[5] = { 1, 2 };  /* 残りは 0 になる */
    int c[5] = { 0 };     /* 全要素が 0 になる */

    for (int i = 0; i < 5; i++) {
        printf("a[%d]=%d b[%d]=%d c[%d]=%d\n", i, a[i], i, b[i], i, c[i]);
    }

    return 0;
}
```

要素数を省略して「初期化子の個数で決める」書き方もあります。

```c
/* file: array_init_auto.c */
#include <stdio.h>

int main(void)
{
    int a[] = { 10, 20, 30 };

    for (int i = 0; i < 3; i++) {
        printf("a[%d]=%d\n", i, a[i]);
    }

    return 0;
}
```

### 配列の要素に値を読み込む

ここでは「配列に値を入れる」を、入力と組み合わせて確認します。

ポイントは次の2つです。

- 配列の要素は `a[i]` の形で取り出せる
- `scanf` で書き込むには、`&a[i]` の形で「場所」を渡す

```c
/* file: array_read.c */
#include <stdio.h>

int main(void)
{
    int a[5] = { 0 };

    puts("整数を5個入力してください（スペース区切りでOK）:");

    for (int i = 0; i < 5; i++) {
        while (scanf("%d", &a[i]) != 1) {
            puts("数字を入力してください");

            /* 入力の残り（改行まで）を捨てる */
            int ch = 0;
            while ((ch = getchar()) != '\n' && ch != EOF) {
            }
        }
    }

    puts("入力された値:");
    for (int i = 0; i < 5; i++) {
        printf("a[%d]=%d\n", i, a[i]);
    }

    return 0;
}
```

注意：この例は「数字でない入力が混じったときにやり直す」だけを扱っています。
入力を行単位で厳密に扱う方法はありますが、まずは配列への格納と走査を固めます。

### 配列の要素の並びを反転する

配列の前後を入れ替えて、並びを反転します。
典型的には「左右から寄っていって交換する」形になります。

```c
/* file: array_reverse.c */
#include <stdio.h>

int main(void)
{
    int a[6] = { 10, 20, 30, 40, 50, 60 };
    int n = 6;

    puts("before:");
    for (int i = 0; i < n; i++) {
        printf("%d ", a[i]);
    }
    putchar('\n');

    for (int i = 0; i < n / 2; i++) {
        int tmp = a[i];
        a[i] = a[n - 1 - i];
        a[n - 1 - i] = tmp;
    }

    puts("after:");
    for (int i = 0; i < n; i++) {
        printf("%d ", a[i]);
    }
    putchar('\n');

    return 0;
}
```

ポイント：

- 交換は必ず一時変数 `tmp` を使う
- `n / 2` 回の交換で反転が終わる（全部を交換する必要はない）

### オブジェクト形式マクロ

「配列の要素数」などを、名前付きの定数として扱いたいことがあります。
そのときに **オブジェクト形式マクロ** を使えます。

```c
/* file: object_like_macro.c */
#include <stdio.h>

#define N 5

int main(void)
{
    int a[N] = { 1, 2, 3, 4, 5 };

    for (int i = 0; i < N; i++) {
        printf("a[%d]=%d\n", i, a[i]);
    }

    return 0;
}
```

注意：

- `#define N 5` にセミコロンは付けません
- `N` を変えたら、配列サイズとループ回数が一緒に変わります

### 配列要素の最大値と最小値

最大値と最小値は「今の最大/最小」を更新しながら走査します。

```c
/* file: array_minmax.c */
#include <stdio.h>

#define N 6

int main(void)
{
    int a[N] = { 7, 2, 9, 4, 1, 8 };

    int minv = a[0];
    int maxv = a[0];

    for (int i = 1; i < N; i++) {
        if (a[i] < minv) {
            minv = a[i];
        }
        if (a[i] > maxv) {
            maxv = a[i];
        }
    }

    printf("min=%d max=%d\n", minv, maxv);
    return 0;
}
```

ポイント：

- 最初に `a[0]` を入れておく（未初期化を避ける）
- ループは `i = 1` から始める

### 配列の要素数

配列の要素数は、配列そのものに対してなら `sizeof` で求められます。

```c
/* file: array_length.c */
#include <stdio.h>

int main(void)
{
    int a[] = { 10, 20, 30, 40 };

    int n = (int)(sizeof(a) / sizeof(a[0]));
    printf("n=%d\n", n);

    return 0;
}
```

注意：これは「a が本当に配列である場所」でだけ成り立ちます。
関数に渡した配列は、見かけが同じでも別の扱いになるため、後の巻で改めて整理します。

### 配列のコピー

配列は `=` で丸ごと代入できません。要素ごとにコピーします。

```c
/* file: array_copy.c */
#include <stdio.h>

#define N 5

int main(void)
{
    int src[N] = { 1, 2, 3, 4, 5 };
    int dst[N] = { 0 };

    for (int i = 0; i < N; i++) {
        dst[i] = src[i];
    }

    for (int i = 0; i < N; i++) {
        printf("src[%d]=%d dst[%d]=%d\n", i, src[i], i, dst[i]);
    }

    return 0;
}
```

---

## 多次元配列

### 多次元配列

多次元配列は「配列の中に配列がある」形です。
2次元配列は「行×列」として扱うのが基本です。

```c
/* file: array_2d_basic.c */
#include <stdio.h>

int main(void)
{
    int m[2][3] = {
        { 1, 2, 3 },
        { 4, 5, 6 },
    };

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            printf("%d ", m[r][c]);
        }
        putchar('\n');
    }

    return 0;
}
```

ポイント：

- 外側の添字が「行」、内側の添字が「列」になることが多い
- 2重ループで走査する

2次元配列は、盤面（グリッド）や簡単な画像、表形式のデータを扱うときに頻出です。

---

## まとめ

- 配列は「同じ型の値を並べて持つ」仕組みで、添字は 0 から始まる
- `for` で走査し、表示・合計・最大/最小などの基本処理が書ける
- 初期化は `{ ... }` で行え、`{ 0 }` は全要素を 0 にできる
- 入力は `&a[i]` の形で要素の場所を渡して読み込める
- 反転は左右を交換していき、`n / 2` 回で終わる
- 要素数は `sizeof(a) / sizeof(a[0])` で求められる（使える場所に注意がある）
- 配列のコピーは要素ごとに行う
- 2次元配列は「行×列」として扱い、2重ループで走査できる

---

## 第六章でやること：

- `main` とライブラリ関数（`printf` など）の関係から「関数」を捉える
- 関数定義と関数呼び出しの最小形を書ける
- 返り値（戻り値）と引数の役割を理解し、返り値を次の関数へ渡せる
- 「値渡し」を基本として、関数を使った分割を安全に行える
- 宣言と定義、関数原型宣言（プロトタイプ）を整理し、複数ファイル構成の入口に立つ
- 配列や多次元配列を関数に渡すときのルール（配列は丸ごと渡らない、列数が必要）を押さえる
- 有効範囲（スコープ）と記憶域期間（いつ存在するか）を整理し、変数の見え方で迷わない

---

## 関数

### 関数とは

#### main関数とライブラリ関数

これまで書いてきたプログラムは、`main` 関数から始まりました。

```c
int main(void)
{
    printf("hi\n");
    return 0;
}
```

ここでの関係は次のとおりです。

- `main`：自身が作る関数（プログラムの入口）
- `printf`：既に用意されている関数（ライブラリ関数）

つまりCのプログラムは「関数を呼び出して処理を積み上げる」形になっています。

#### 関数とは

関数は「入力（引数）を受け取り、処理をし、必要なら結果（返り値）を返す」部品です。
大きなプログラムを読みやすくするために、処理を小さく分けるために使います。

#### 関数定義

関数を自分で作るには **定義** が必要です。

```c
/* file: func_define.c */
#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    printf("%d\n", add(10, 20));
    return 0;
}
```

読み方：

- `int add(int a, int b)` が関数の見出し部分
- `{ ... }` が関数の本体
- `return` で `int` の値を返す

#### 関数呼び出し

関数呼び出しは `関数名(引数...)` です。

```c
/* file: func_call.c */
#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int x = 3;
    int y = 4;
    int z = add(x, y);
    printf("z=%d\n", z);
    return 0;
}
```

ポイント：

- `add(x, y)` の結果（返り値）が、式として使える
- `add` の中身を知らなくても「使い方」が分かれば使える

#### 3値の最大値を求める関数

関数にすると「どこでも使える部品」になります。

```c
/* file: max3_func.c */
#include <stdio.h>

int max2(int a, int b)
{
    return (a > b) ? a : b;
}

int max3(int a, int b, int c)
{
    return max2(max2(a, b), c);
}

int main(void)
{
    printf("max3=%d\n", max3(10, 4, 7));
    return 0;
}
```

この例で出てきたように、関数の中から別の関数を呼び出せます。

#### 関数の返却値を引数として関数に渡す

返り値は値なので、そのまま次の関数の引数にできます。

```c
/* file: return_to_arg.c */
#include <stdio.h>

int add1(int x)
{
    return x + 1;
}

int mul2(int x)
{
    return x * 2;
}

int main(void)
{
    int x = 10;
    int y = mul2(add1(x));
    printf("y=%d\n", y);
    return 0;
}
```

読み方：

- まず `add1(x)` を評価する
- その結果を `mul2(...)` に渡す

#### 自作の関数を呼び出す関数

「入出力」などの周辺処理を `main` に集めすぎないのがコツです。

```c
/* file: call_custom.c */
#include <stdio.h>

int max2(int a, int b)
{
    return (a > b) ? a : b;
}

void show_max2(int a, int b)
{
    printf("a=%d b=%d max=%d\n", a, b, max2(a, b));
}

int main(void)
{
    show_max2(3, 9);
    show_max2(10, -5);
    return 0;
}
```

ここで `show_max2` は値を返しません（`void`）。

#### 値渡し

関数の引数は基本的に **値渡し** です。
呼び出し側の変数そのものが関数内で書き換わるわけではありません。

```c
/* file: pass_by_value.c */
#include <stdio.h>

void try_change(int x)
{
    x = 999;
}

int main(void)
{
    int a = 10;
    try_change(a);
    printf("a=%d\n", a);
    return 0;
}
```

この例では `a` は 10 のままです。
「呼び出し側の変数を関数で変更したい」ときは、別の仕組み（アドレスを渡す、など）が必要になります。

### 関数の設計

#### 値を返さない関数（void）

値を返す必要がない処理は `void` にします。

```c
/* file: void_func.c */
#include <stdio.h>

void print_line(void)
{
    puts("----------------");
}

int main(void)
{
    print_line();
    puts("hello");
    print_line();
    return 0;
}
```

`print_line(void)` の `(void)` は「引数を受け取らない」意味です。

#### 関数の汎用性

関数は「特定の値に依存しない形」にすると使い回しやすくなります。

- 良い方向：引数で渡せるものは引数にする
- 避けたい方向：関数の中で勝手に固定値を使い続ける

#### 引数を受け取らない関数

引数が不要な場合は `(void)` にします。

```c
/* file: no_arg.c */
#include <stdio.h>

int answer(void)
{
    return 42;
}

int main(void)
{
    printf("%d\n", answer());
    return 0;
}
```

#### ブロック有効範囲（ブロックスコープ）

変数は「宣言された場所から見える範囲」が決まります。
ブロック `{ ... }` の中で宣言した変数は、そのブロックの外から見えません。

```c
/* file: block_scope.c */
#include <stdio.h>

int main(void)
{
    int x = 1;

    {
        int y = 2;
        printf("x=%d y=%d\n", x, y);
    }

    printf("x=%d\n", x);
    return 0;
}
```

#### ファイル有効範囲（ファイルスコープ）

関数の外で宣言した変数（グローバル変数）は、そのファイル全体から見えます。

```c
/* file: file_scope.c */
#include <stdio.h>

int g_counter = 0; /* ファイルスコープ */

void inc(void)
{
    g_counter++;
}

int main(void)
{
    inc();
    inc();
    printf("g_counter=%d\n", g_counter);
    return 0;
}
```

グローバル変数は便利ですが、どこからでも書き換えられて追いにくくなることがあります。
まずは「引数と返り値でつなぐ」設計を基本にします。

#### 宣言と定義

言葉を揃えます。

- **宣言**：名前と型（使い方）を知らせる
- **定義**：実体を作る（関数の本体を書く／変数の実体を作る）

例：関数の宣言（プロトタイプ）と定義

```c
/* file: decl_def.c */
#include <stdio.h>

int add(int a, int b); /* 宣言（原型宣言） */

int main(void)
{
    printf("%d\n", add(1, 2));
    return 0;
}

int add(int a, int b) /* 定義 */
{
    return a + b;
}
```

ポイント：

- `main` より後に `add` の本体があっても、宣言があれば呼び出せる
- 大きなプログラムでは「宣言をヘッダに置く」形へ発展する

#### 関数原型宣言（プロトタイプ）

原型宣言は「この関数はこの形で呼び出す」という約束です。
引数の個数や型が違うと、コンパイル警告やバグの原因になります。

#### ヘッダとインクルード

複数ファイルに分けるときは、宣言を `.h` に置き、使う側は `#include` します。

```c
/* file: add.h */
#ifndef ADD_H
#define ADD_H

int add(int a, int b);

#endif
```

```c
/* file: add.c */
#include "add.h"

int add(int a, int b)
{
    return a + b;
}
```

```c
/* file: main.c */
#include <stdio.h>
#include "add.h"

int main(void)
{
    printf("%d\n", add(10, 20));
    return 0;
}
```

この形にすると、`main.c` は `add` の中身を知らなくても使えます。

#### 配列の値渡し

配列は「丸ごと値として渡る」のではなく、関数に渡すときに別の扱いになります。
まずは実務で頻出の書き方を押さえます。

```c
/* file: sum_array.c */
#include <stdio.h>

int sum_array(const int a[], int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += a[i];
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

この例の形が重要です。

- 関数側は「配列そのものの要素数」を知れないので、`n` を一緒に渡す
- 関数側で `sizeof(a)` を使って要素数を出すのは期待どおりにならない（この理由は後の巻で整理します）

#### 配列の値渡しとconst型修飾子

`const` を付けると「この関数は配列の中身を書き換えない」意図を表せます。

- 読み取り専用の入力：`const int a[]`
- 書き換える必要がある：`int a[]`

`const` を付けると、間違って `a[i] = ...;` と書いたときにコンパイルエラーになり、事故を防げます。

#### 線形探索（逐次探索）

配列から値を探す最小の方法が線形探索です。
先頭から順に比較して、見つかったらその位置（添字）を返します。

```c
/* file: linear_search.c */
#include <stdio.h>

int linear_search(const int a[], int n, int key)
{
    for (int i = 0; i < n; i++) {
        if (a[i] == key) {
            return i;
        }
    }
    return -1;
}

int main(void)
{
    int a[] = { 3, 7, 2, 9, 1 };
    int n = (int)(sizeof(a) / sizeof(a[0]));

    int key = 9;
    int idx = linear_search(a, n, key);

    if (idx >= 0) {
        printf("found: idx=%d\n", idx);
    } else {
        puts("not found");
    }

    return 0;
}
```

#### 多次元配列の受け渡し

2次元配列を関数に渡すときは「列数（内側の要素数）」が必要になります。

```c
/* file: pass_2d.c */
#include <stdio.h>

#define COLS 3

void print_matrix(int m[][COLS], int rows)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < COLS; c++) {
            printf("%d ", m[r][c]);
        }
        putchar('\n');
    }
}

int main(void)
{
    int m[2][COLS] = {
        { 1, 2, 3 },
        { 4, 5, 6 },
    };
    print_matrix(m, 2);
    return 0;
}
```

ポイント：

- `m[][COLS]` のように「列数」を固定して受け取る
- 行数は引数で渡す

### 有効範囲と記憶域期間

#### 有効範囲と識別子の可視性

有効範囲（スコープ）は「その名前がどこから見えるか」です。

- ブロックスコープ：ブロック `{ ... }` の中だけ
- ファイルスコープ：そのファイル全体

同じ名前が別のスコープで出てくることがあります。
読み間違いを防ぐために「どのブロックの変数か」を意識して追います。

#### 記憶域期間

記憶域期間は「いつ存在するか（いつ確保され、いつ消えるか）」です。

- **自動記憶域期間**：関数に入ると作られ、関数を抜けると消える（通常のローカル変数）
- **静的記憶域期間**：プログラムの開始から終了まで存在する（グローバル変数、`static` 変数）

```c
/* file: storage_duration.c */
#include <stdio.h>

void f(void)
{
    int a = 0;          /* 自動記憶域期間 */
    static int s = 0;   /* 静的記憶域期間 */

    a++;
    s++;
    printf("a=%d s=%d\n", a, s);
}

int main(void)
{
    f();
    f();
    f();
    return 0;
}
```

この例では `a` は毎回 1 から始まり、`s` は前回の値を覚え続けます。

---

## 第六章まとめ

- 関数は「引数を受け取って処理し、必要なら返り値を返す」部品
- `main` は入口の関数で、`printf` のようなライブラリ関数を呼び出して処理を組み立てる
- 関数は定義して使う。返り値は式として扱え、別の関数の引数にも渡せる
- 引数は基本的に値渡しで、呼び出し側の変数そのものは書き換わらない
- `void` 関数、引数なし関数、汎用性（引数で渡す）を意識して設計すると読みやすい
- 宣言と定義を区別し、原型宣言（プロトタイプ）で「使い方」を先に知らせられる
- `.h` に宣言、`.c` に定義を置く形が複数ファイル構成の基本
- 配列は関数に渡すときに要素数も一緒に渡すのが基本で、読み取り専用なら `const` を付ける
- 線形探索は先頭から順に比較し、見つかった位置（添字）を返す
- 2次元配列は列数が必要になり、`m[][COLS]` のように受け取る
- スコープ（見える範囲）と記憶域期間（いつ存在するか）を意識すると変数で迷いにくい
