---
title: "sizeof 演算子 型やオブジェクトの大きさを調べる"
date: 2026-04-03
---

# sizeof 演算子 — 型やオブジェクトの大きさを調べる

`sizeof` は、C言語で「型」や「オブジェクト」が何バイトあるかを調べるための演算子です。

見た目は `sizeof(x)` のように関数呼び出しに似ていますが、`sizeof` は関数ではありません。
したがって、標準ライブラリ関数でもなく、システムコールでもありません。

## まず結論：sizeof は何者か

- 分類：演算子
- 標準ライブラリ関数ではない
- システムコールではない
- ヘッダファイルは不要
- 型やオブジェクトの大きさを `size_t` 型の値として得る

## `sizeof` と `printf` / `write` の違い

### `sizeof` は演算子

- C言語の文法そのものに含まれる仕組み
- 値を計算する
- 多くの場合、コンパイル時に結果が決まる

### `printf` は標準ライブラリ関数

- `#include <stdio.h>` が必要
- 値を見やすく表示するための関数
- `sizeof` の結果を表示することはできるが、`sizeof` 自体ではない

### `write` はシステムコール系API

- fd に対してバイト列を書き込む低レベルI/O
- サイズを調べる役割ではない

つまり、

- `sizeof` は「大きさを調べる」
- `printf` は「表示する」
- `write` は「書き込む」

という役割分担です。

## 基本の書き方

`sizeof` には大きく2つの書き方があります。

### 1. オブジェクトに対して使う

```c
sizeof x
sizeof(x)
```

例えば：

```c
int value;
size_t n = sizeof(value);
```

### 2. 型に対して使う

```c
sizeof(int)
sizeof(double)
sizeof(struct sample)
```

型に対して使うときは、かっこが必要です。

## 戻り値の型

`sizeof` の結果は `size_t` 型です。

そのため、表示するときは普通 `printf` と組み合わせて `%zu` を使います。

```c
#include <stdio.h>

int main(void)
{
    printf("%zu\n", sizeof(int));
    return 0;
}
```

## 最小サンプル

```c
#include <stdio.h>

int main(void)
{
    int x = 0;

    printf("sizeof(x) = %zu\n", sizeof(x));
    printf("sizeof(int) = %zu\n", sizeof(int));

    return 0;
}
```

## 典型例

### 1. 配列全体の大きさを調べる

```c
#include <stdio.h>

int main(void)
{
    int values[10];

    printf("array bytes = %zu\n", sizeof(values));
    return 0;
}
```

`int` が4バイトの環境なら、`10 * 4 = 40` バイトになります。

### 2. 配列の要素数を求める

```c
#include <stdio.h>

int main(void)
{
    int values[10];
    size_t count = sizeof(values) / sizeof(values[0]);

    printf("count = %zu\n", count);
    return 0;
}
```

これは固定長配列の要素数を求める定番です。

### 3. メモリ確保のサイズを書く

```c
int *p = malloc(sizeof(int) * 10);
```

あるいは、型名を直接書く代わりに次の形もよく使います。

```c
int *p = malloc(sizeof(*p) * 10);
```

後者は、型名を書き換え忘れる事故を減らしやすいです。

## 注意点

### 1. 配列とポインタで結果が違う

次の2つは同じではありません。

```c
int values[10];
int *p = values;

sizeof(values)
sizeof(p)
```

- `sizeof(values)` は配列全体の大きさ
- `sizeof(p)` はポインタ自身の大きさ

ここは C 初学者がかなり混同しやすいところです。

### 2. 関数引数の配列は、実際にはポインタとして扱われる

```c
void f(int a[])
{
    printf("%zu\n", sizeof(a));
}
```

この `a` は関数の中では配列そのものではなく、ポインタとして扱われます。
そのため、配列全体の大きさは取れません。

### 3. 文字列長とは違う

```c
char s[] = "abc";
sizeof(s)
```

この結果は文字数3ではなく、終端の `\0` も含めた 4 になります。

一方、`strlen(s)` は文字列長を返すので 3 です。

### 4. 可変長配列では実行時評価になることがある

通常の `sizeof` はコンパイル時に決まることが多いですが、可変長配列（VLA）では実行時に評価されます。

つまり、「`sizeof` は必ずコンパイル時に決まる」と思い込むのは少し危険です。

## よくある混同

### `sizeof()` と書いてしまう問題

`sizeof(x)` という書き方をよく見るので、関数だと思いやすいです。
ですが本質的には演算子です。

つまり、

- `printf()` は関数呼び出し
- `sizeof()` のような見た目はしていても、`sizeof` は演算子

と理解すると整理しやすいです。

### `strlen` と混同する問題

- `sizeof` はオブジェクトや型のバイト数
- `strlen` は文字列の長さ

この2つは目的が違います。

## まず覚える形

最初は次の3つを覚えると十分です。

```c
sizeof(int)
sizeof(x)
sizeof(array) / sizeof(array[0])
```

## 関連API・関連機能

- `printf`
  - `sizeof` の結果を表示するときによく組み合わせる
- `strlen`
  - 文字列長を調べる関数
- `malloc`
  - 確保サイズを書くときに `sizeof` をよく使う
- 配列
  - 配列全体の大きさと要素数計算で重要
- ポインタ
  - 配列との違いを理解するうえで重要

## まとめ

- `sizeof` は関数ではなく演算子
- 標準ライブラリ関数でもシステムコールでもない
- 型やオブジェクトの大きさを `size_t` で返す
- 配列とポインタで結果が違う点は特に重要
- `printf` と組み合わせるときは `%zu` を使う