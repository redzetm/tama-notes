---
title: "UmuOSの為のC言語２　配列・関数・基本型 編"
---

# UmuOSの為のC言語 2 — 配列・関数・基本型 編

このシリーズは最終的に、UmuOS／ush／uim のソースコードを自力で読めるようになることを目標にします。  
ただし第2巻（第五章〜第七章）も UmuOS に一切依存しない形で、C言語の基本の基本を丁寧に説明します。

この巻の範囲：

- 第五章：配列（多次元配列を含む）
- 第六章：関数（宣言と定義、配列の受け渡し、スコープと記憶域期間）
- 第七章：基本型（整数/文字/浮動小数点、ビット演算、型変換、演算子）

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

補足：第七章では `<math.h>` を使う例があります。環境によってはリンク時に `-lm` が必要です。

```bash
gcc -Wall -Wextra -std=c17 -O0 math_demo.c -o math_demo -lm
./math_demo
```

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

---

## 第七章でやること：

- Cの「基本型」を、数の表し方（10進/16進など）と結び付けて整理する
- 整数型と文字型の関係（`char` は整数型）を理解し、使い分けの軸を持つ
- `<limits.h>` と `CHAR_BIT`、`sizeof` と `size_t` を使って「環境依存」を確認できる
- 配列の要素数の求め方と、なぜ関数に渡すとできなくなるかを再確認する
- 整数の内部表現（ビット）を入口として、ビット演算・シフト演算・フラグ操作を書ける
- 整数定数の書き方（基数/接尾辞）と、定数の型が意図せず変わる落とし穴を避ける
- 整数の表示（`printf`）を型に合わせて行い、符号なし/符号付きの違いで迷わない
- オーバーフローや0除算など「例外が出ない」Cの振る舞いを把握し、危険な書き方を避ける
- 浮動小数点型と `<math.h>` を扱い、丸め誤差や比較の注意点を理解する
- 演算子の優先順位・結合性と、型変換（暗黙変換）の規則を押さえる

---

## 基本型

### 基本型と数

#### 算術型と基本型

「算術型」は数の計算に使う型のまとまりで、次の2つに分かれます。

- 整数型（例：`int`、`unsigned int`、`char` など）
- 浮動小数点型（例：`float`、`double`、`long double`）

この章では、まず整数型と文字型を中心に「型が違うと何が違うか」を整理し、次に浮動小数点型へ進みます。

#### 基数

「基数」は数の表し方（桁の進み方）です。Cのコードやデバッガ出力では 2/8/10/16 進がよく出てきます。

- 2進数（基数2）：`0` と `1`
- 8進数（基数8）：`0`〜`7`
- 10進数（基数10）：`0`〜`9`
- 16進数（基数16）：`0`〜`9` と `a`〜`f`（または `A`〜`F`）

Cの整数定数では、次の接頭辞で基数が決まります。

- 10進：接頭辞なし（例：`123`）
- 8進：先頭が `0`（例：`077`）
- 16進：先頭が `0x` または `0X`（例：`0xff`、`0XFF`）

注意：先頭 `0` の整数定数は8進です。`010` は10ではなく8です（値は8）。

#### 基数変換

「基数変換」は、同じ値を別の基数で表すことです。

まず最小の確認として、`printf` の表示指定で 10/8/16 進を出してみます。

```c
/* file: print_bases.c */
#include <stdio.h>

int main(void)
{
    unsigned int x = 255;
    printf("dec: %u\n", x);
    printf("oct: %o\n", x);
    printf("hex: %x\n", x);
    printf("hex (0x): 0x%x\n", x);
    return 0;
}
```

`%o` が8進、`%x` が16進です。

次に「任意の基数（2〜16）へ変換する」手続きをコードで表します。
やりたいことは、割り算の余りを桁として積み上げることです。

```c
/* file: to_base.c */
#include <stdio.h>

/*
 * value を base 進の文字列にして buf に入れる。
 * base は 2〜16 を想定。
 * 返り値は buf。
 */
char *to_base(unsigned int value, unsigned int base, char buf[], int buf_size)
{
    const char digits[] = "0123456789abcdef";
    int i = 0;

    if (buf_size <= 0) {
        return buf;
    }

    if (base < 2 || base > 16) {
        buf[0] = '\0';
        return buf;
    }

    /* 0 は特別扱い（割り算ループに入ると何も書けないため） */
    if (value == 0) {
        if (buf_size >= 2) {
            buf[0] = '0';
            buf[1] = '\0';
        } else {
            buf[0] = '\0';
        }
        return buf;
    }

    /* 下の桁から作るので、いったん逆順に入れる */
    while (value != 0 && i < buf_size - 1) {
        unsigned int r = value % base;
        buf[i++] = digits[r];
        value /= base;
    }
    buf[i] = '\0';

    /* 逆順を反転する */
    for (int l = 0, r = i - 1; l < r; l++, r--) {
        char tmp = buf[l];
        buf[l] = buf[r];
        buf[r] = tmp;
    }
    return buf;
}

int main(void)
{
    unsigned int x = 1234;
    char buf[64];

    printf("dec: %u\n", x);
    printf("bin: %s\n", to_base(x, 2, buf, (int)sizeof(buf)));
    printf("oct: %s\n", to_base(x, 8, buf, (int)sizeof(buf)));
    printf("hex: %s\n", to_base(x, 16, buf, (int)sizeof(buf)));
    return 0;
}
```

このコードで重要なのは次の点です。

- 「割った余り」がその基数での下の桁
- 下の桁から出るので、いったん逆順に貯めて最後に反転する
- 0 はループに入らないので特別扱いが必要

---

### 整数型と文字型

#### 整数型と文字型

`char` は「文字」のための型として使われますが、Cでは `char` も整数型です。
そのため、文字は内部的には数（ビット列）として扱われます。

```c
/* file: char_is_int.c */
#include <stdio.h>

int main(void)
{
    char c = 'A';
    printf("c as char: %c\n", c);
    printf("c as int : %d\n", (int)c);
    return 0;
}
```

`%c` で文字、`%d` で数として表示できます。

注意点として、`char` が符号付きか符号なしかは処理系依存です。
文字の数値を扱う必要がある場面では、`unsigned char` を使うと混乱を避けやすくなります。

#### 整数型の使い分け

整数型には多くの種類がありますが、使い分けの基本は次のとおりです。

- 通常の計算や添字：まずは `int`
- 個数やサイズ（負にならない値）：`size_t`
- ビット演算やフラグ：`unsigned` 系

「何ビットか」を決め打ちする必要が出てきたら、`<stdint.h>` の `uint32_t` など（固定幅整数型）を検討します。

#### <limits.h>ヘッダ

型の範囲は環境で違います。`<limits.h>` にある定数で確認できます。

```c
/* file: limits_demo.c */
#include <stdio.h>
#include <limits.h>

int main(void)
{
    printf("CHAR_BIT=%d\n", CHAR_BIT);
    printf("INT_MIN=%d\n", INT_MIN);
    printf("INT_MAX=%d\n", INT_MAX);
    printf("UINT_MAX=%u\n", UINT_MAX);
    return 0;
}
```

ここで `CHAR_BIT` は「1バイトが何ビットか」です。多くの環境では 8 ですが、決め打ちは避けます。

#### 文字型

文字型には主に次が登場します。

- `char`：1文字分のバイト列を扱うために使うことが多い
- `signed char` / `unsigned char`：符号を明示したいときに使う

文字コード（ASCII など）は「数と文字の対応表」です。
数として扱えることを前提にしているため、比較（`c >= '0' && c <= '9'`）のような処理ができます。

#### ビットとCHAR_BIT

ビットは 0/1 の単位で、整数はビットの並びとして表現されます。
`CHAR_BIT` は「`char` が何ビットか」で、`sizeof(char)` は常に 1（1バイト）です。

#### sizeof演算子

`sizeof` は「その型や式が何バイトか」を返します。

- `sizeof(int)`：型のサイズ
- `sizeof x`：式のサイズ

返り値の型は `size_t` です。

```c
/* file: sizeof_demo.c */
#include <stdio.h>

int main(void)
{
    printf("sizeof(char)=%zu\n", sizeof(char));
    printf("sizeof(int)=%zu\n", sizeof(int));
    printf("sizeof(double)=%zu\n", sizeof(double));
    return 0;
}
```

`%zu` は `size_t` を表示するための指定です。

#### size_t型とtypedef宣言

`size_t` は「サイズや個数」を表すための型です（典型的には符号なし整数）。
実際にどの基本型を別名にしているかは環境で異なります。

このような「別名を付ける」仕組みが `typedef` です。

```c
/* file: typedef_demo.c */
#include <stdio.h>

typedef unsigned int uint; /* unsigned int に別名 uint を付ける */

int main(void)
{
    uint x = 10;
    printf("x=%u\n", x);
    return 0;
}
```

`size_t` も同じ発想で定義されています。

#### 配列の要素数の求め方

配列 `a` の要素数は、同じスコープで「本物の配列」として見えているときに限り、次で求められます。

```c
int n = (int)(sizeof(a) / sizeof(a[0]));
```

```c
/* file: array_count.c */
#include <stdio.h>

int main(void)
{
    int a[] = { 10, 20, 30, 40, 50 };
    int n = (int)(sizeof(a) / sizeof(a[0]));
    printf("n=%d\n", n);
    return 0;
}
```

関数に渡した後はこの方法が使えません。
関数の引数で受け取る `int a[]` は、配列そのものではなく別の扱いになるためです。
そのため「配列と一緒に要素数を渡す」が基本になります（第六章の `sum_array` 参照）。

#### 整数型の内部表現

整数はビット列で表現されます。ここでは「まず符号なし」「次に符号付き」の順に押さえます。

#### 符号なし整数の内部表現

符号なし整数（`unsigned` 系）は、0 から始まって最大値まで数が増えていく表現です。
計算結果が範囲からはみ出したときは、$2^N$（Nはビット数）で割った余りになる形で循環します。

```c
/* file: unsigned_wrap.c */
#include <stdio.h>
#include <limits.h>

int main(void)
{
    unsigned int x = UINT_MAX;
    printf("x=%u\n", x);
    x = x + 1;
    printf("x+1=%u\n", x);
    return 0;
}
```

この挙動は定義されていますが、意図して使うのは慎重にします。

#### 符号付き整数の内部表現

符号付き整数（`int` など）は負の数も表します。
実際の内部表現としては2の補数が広く使われていますが、C言語の規格としては表現方法の細部が処理系依存の部分もあります。

特に重要なのは次の点です。

- 符号付き整数のオーバーフロー（例：`INT_MAX + 1`）は未定義動作になり得る
- そのため「オーバーフローするかもしれない式」は避けるか、事前に範囲チェックを行う

#### ビット単位の論理演算

ビット演算は「ビット列を直接いじる」演算です。

- `&`（AND）：両方が1のとき1
- `|`（OR）：どちらかが1なら1
- `^`（XOR）：違うとき1
- `~`（NOT）：0/1を反転

```c
/* file: bit_ops.c */
#include <stdio.h>

int main(void)
{
    unsigned int a = 0x0f; /* 0000 1111 */
    unsigned int b = 0x33; /* 0011 0011 */

    printf("a & b = 0x%x\n", a & b);
    printf("a | b = 0x%x\n", a | b);
    printf("a ^ b = 0x%x\n", a ^ b);
    printf("~a    = 0x%x\n", (unsigned int)(~a));
    return 0;
}
```

表示は16進にするとビットの塊が追いやすくなります。

#### シフト演算

シフト演算はビット列を左右にずらします。

- `x << k`：左にkビット
- `x >> k`：右にkビット

```c
/* file: shift_demo.c */
#include <stdio.h>

int main(void)
{
    unsigned int x = 1;
    printf("x      = 0x%x\n", x);
    printf("x<<1   = 0x%x\n", x << 1);
    printf("x<<4   = 0x%x\n", x << 4);
    printf("0x80>>1= 0x%x\n", 0x80u >> 1);
    return 0;
}
```

注意点：

- 符号付き整数の右シフトがどうなるか（符号ビットを埋めるか等）は処理系依存になり得る
- ビット演算やシフトは `unsigned` 系で行うと安全に書きやすい
- ビット幅以上のシフト（例：32ビット幅で `x << 32`）は未定義動作になり得る

#### ビット単位の論理演算の応用

ビットを「フラグ（オン/オフ）」として使うと、1つの整数で多くの状態を表せます。

```c
/* file: bit_flags.c */
#include <stdio.h>

#define FLAG_READ   (1u << 0)
#define FLAG_WRITE  (1u << 1)
#define FLAG_EXEC   (1u << 2)

int main(void)
{
    unsigned int flags = 0;

    /* 立てる（set） */
    flags |= FLAG_READ;
    flags |= FLAG_WRITE;

    /* 確認する（test） */
    if (flags & FLAG_READ) {
        puts("READ on");
    }

    /* 下ろす（clear） */
    flags &= ~FLAG_WRITE;

    /* 反転する（toggle） */
    flags ^= FLAG_EXEC;

    printf("flags=0x%x\n", flags);
    return 0;
}
```

この形はOSやネットワークなどのコードで頻出です。

#### 整数定数

整数定数は「基数」と「接尾辞（サフィックス）」で意味が変わります。

- 10進：`123`
- 8進：`077`
- 16進：`0xff`
- 符号なし：`123u`、`0xffU`
- long：`123l`、`123L`
- long long：`123ll`、`123LL`

#### 整数定数の型

整数定数の型は、値の大きさや接尾辞で決まります。
規則は細かいですが、実用上は次の点を強く意識します。

- 期待する型を明示したいときは接尾辞を付ける（例：`1u`、`1ULL`）
- ビット演算のマスクは `1u << k` のように `u` を付けて符号なしで計算する
- `0x80000000` のような値は環境によって符号付きになったり符号なしになったりし、比較や表示で事故になりやすい

#### 整数の表示

`printf` の表示指定は「型に合ったもの」を使います。

- `int`：`%d`
- `unsigned int`：`%u`
- `long`：`%ld`
- `unsigned long`：`%lu`
- `size_t`：`%zu`

固定幅整数型（`uint32_t` など）を表示するときは、`<inttypes.h>` のマクロを使うと安全です。

```c
/* file: print_uint32.c */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

int main(void)
{
    uint32_t x = 4000000000u;
    printf("x=%" PRIu32 "\n", x);
    printf("x=0x%" PRIx32 "\n", x);
    return 0;
}
```

#### オーバーフローと例外

Cには「例外（exception）」で自動的に止めてくれる仕組みは基本的にありません。
そのため、オーバーフローや0除算などは「起きないように書く」方針が重要です。

- 符号なし整数のオーバーフロー：規格上は循環する（$2^N$ での剰余）
- 符号付き整数のオーバーフロー：未定義動作になり得る（処理系・最適化で結果が変わる）
- 整数の0除算：未定義動作

安全に書くための最初のルール：

- 境界（最小値/最大値）が関係する式は、先に `<limits.h>` を使って範囲チェックをする
- ビット演算は符号なしで行う（`unsigned` 系に寄せる）
- `-Wall -Wextra` を常に付け、警告を消してから次へ進む

---

### 浮動小数点型

#### 浮動小数点型

浮動小数点型は「小数を扱うための型」です。

- `float`
- `double`
- `long double`

サイズや精度は環境依存です。`sizeof` で確認できます。

#### 浮動小数点定数

浮動小数点定数は次のように書きます。

- `1.0`（`double`）
- `1.0f`（`float`）
- `1.0L`（`long double`）
- `1e-3`（科学的記法。$1 \times 10^{-3}$）

#### <math.h>ヘッダ

数学関数は `<math.h>` にあります。
環境によってはリンク時に `-lm` が必要です。

```c
/* file: math_demo.c */
#include <stdio.h>
#include <math.h>

int main(void)
{
    double x = 2.0;
    printf("sqrt(%f)=%f\n", x, sqrt(x));
    printf("sin(%f)=%f\n", x, sin(x));
    return 0;
}
```

#### 繰り返しの制御

浮動小数点は丸め誤差があるため、ループの条件に直接使うと意図しない回数になることがあります。
典型例として、次のような「0.1ずつ増やして1.0まで」は危険です。

```c
/* file: float_loop_bad.c */
#include <stdio.h>

int main(void)
{
    for (double x = 0.0; x <= 1.0; x += 0.1) {
        printf("x=%.17f\n", x);
    }
    return 0;
}
```

対策としては、回数を整数で管理します。

```c
/* file: float_loop_good.c */
#include <stdio.h>

int main(void)
{
    for (int i = 0; i <= 10; i++) {
        double x = i / 10.0;
        printf("x=%.1f\n", x);
    }
    return 0;
}
```

---

### 演算と演算子

#### 演算子の優先順位と結合性

演算子には「優先順位（どれから計算するか）」と「結合性（同順位が並んだときの結び方）」があります。
覚え切るより、次の方針で事故を避けます。

- 迷ったら括弧を付ける
- `&&` と `||` の混在、ビット演算と比較の混在は括弧で明示する

よく出る順に、上ほど強い（先に計算されやすい）ものを並べます。

- 後置：`x++` `x--` `f(...)` `a[i]` `.` `->`
- 単項：`++x` `--x` `+x` `-x` `!x` `~x` `(型)式` `sizeof`
- 乗除：`*` `/` `%`
- 加減：`+` `-`
- シフト：`<<` `>>`
- 関係：`<` `<=` `>` `>=`
- 等価：`==` `!=`
- ビットAND：`&`
- ビットXOR：`^`
- ビットOR：`|`
- 論理AND：`&&`
- 論理OR：`||`
- 条件：`?:`
- 代入：`=` `+=` `-=` `*=` `/=` など

例：ビット演算と比較が混ざる場合

```c
if ((flags & FLAG_READ) != 0) {
    /* ... */
}
```

`flags & FLAG_READ` の結果を明示的に括弧でくくると、読み間違いが減ります。

#### 型変換の規則

型が混ざった式では、計算の前に暗黙の型変換が起きます。
規則は細かいですが、まず次の現象を押さえます。

- 小さい整数型（`char` や `short`）は、式の中で `int` に拡張されることがある（整数拡張）
- `int` と `double` を混ぜると、`int` が `double` に変換されて計算されることが多い
- 符号付きと符号なしを混ぜると、符号なし側に寄って比較が起き、意図と違う結果になり得る

```c
/* file: int_division.c */
#include <stdio.h>

int main(void)
{
    printf("3/2 = %d\n", 3 / 2);
    printf("3/2 = %f\n", 3 / 2.0);
    return 0;
}
```

`3 / 2` は整数同士なので 1 になります。
小数が必要なら、どちらかを浮動小数点にします。

次は符号付き/符号なしの混在で起きやすい例です。

```c
/* file: signed_unsigned.c */
#include <stdio.h>

int main(void)
{
    int a = -1;
    unsigned int b = 1;

    if (a < (int)b) {
        puts("a < (int)b");
    } else {
        puts("a >= (int)b");
    }

    if ((unsigned int)a < b) {
        puts("(unsigned)a < b");
    } else {
        puts("(unsigned)a >= b");
    }
    return 0;
}
```

意図が「数として大小比較」なら、比較の前にどの型で比較するかを明確にし、必要なら明示的に変換します。

---

## 第七章まとめ

- 算術型は「整数型」と「浮動小数点型」に分かれる
- 整数定数は基数（10/8/16）と接尾辞（`u`/`l`/`ll`）で意味が変わり、先頭 `0` は8進になる
- `<limits.h>` と `CHAR_BIT` で環境依存の範囲やビット数を確認できる
- `sizeof` の結果は `size_t` で、`printf` では `%zu` を使う
- 配列の要素数は同じスコープの「本物の配列」に対して `sizeof(a)/sizeof(a[0])` で求められる
- ビット演算（`& | ^ ~`）とシフト（`<< >>`）はOS系コードで頻出で、符号なしで扱うと安全に書きやすい
- ビットフラグは `|`（立てる）、`&`（確認）、`& ~`（下ろす）、`^`（反転）で扱える
- 符号なしのオーバーフローは循環するが、符号付きのオーバーフローや0除算は未定義動作になり得る
- 浮動小数点は丸め誤差があるため、ループ回数は整数で管理すると安定する
- 演算子の優先順位・結合性と暗黙の型変換を意識し、迷う式は括弧で明示する
