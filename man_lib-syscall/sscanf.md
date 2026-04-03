---
title: "sscanf(3) 文字列を入力元として書式解析する"
date: 2026-04-03
---

# sscanf(3) — 文字列を入力元として書式解析する

`sscanf` は、C言語の標準ライブラリが提供する入力関数です。
標準入力ではなく、すでにメモリ上にある文字列を入力元として、書式文字列に従って値を取り出します。

`scanf` と似ていますが、「どこから読むか」が違います。
`scanf` は標準入力、`sscanf` は文字列です。

## まず結論：sscanf は何者か

- 分類：標準ライブラリ関数
- `man` の分類：セクション 3 相当
- ヘッダファイル：`<stdio.h>`
- 文字列を入力元として書式解析する関数
- システムコールではない
- 演算子ではない

## `sscanf` と `scanf` の違い

### `scanf`

- 標準入力から読む
- 対話入力でよく使う
- 入力バッファの食い残しを意識する必要がある

### `sscanf`

- 文字列から読む
- すでに `fgets` などで受け取った行を解析するのに向く
- 入力元が文字列なので、テストや検証がしやすい

つまり、

- `scanf` は「端末や標準入力から直接読む」
- `sscanf` は「文字列を後から解析する」

という違いです。

## SYNOPSIS

```c
#include <stdio.h>

int sscanf(const char *s, const char *format, ...);
```

## ヘッダファイル

```c
#include <stdio.h>
```

## 引数

- `s`
  - 入力元となる文字列
- `format`
  - 読み取り方を指定する書式文字列
- `...`
  - 結果を書き込む変数のアドレス

## 戻り値

- 成功時
  - 代入できた項目数を返す
- 失敗時
  - 期待した形式で読めなかった時点までの代入数を返す
- 入力がない/失敗時
  - `EOF` を返すことがある

基本的な読み方は `scanf` と同じで、「代入できた項目数」を見るのが重要です。

## 最小サンプル

```c
#include <stdio.h>

int main(void)
{
    const char *line = "123 456";
    int a;
    int b;

    if (sscanf(line, "%d %d", &a, &b) != 2) {
        printf("parse error\n");
        return 1;
    }

    printf("a=%d b=%d\n", a, b);
    return 0;
}
```

## 典型例

### 1. `fgets` で読んだ行を解析する

```c
#include <stdio.h>

int main(void)
{
    char line[64];
    int age;

    if (fgets(line, sizeof(line), stdin) == NULL) {
        return 1;
    }

    if (sscanf(line, "%d", &age) != 1) {
        printf("parse error\n");
        return 1;
    }

    printf("age = %d\n", age);
    return 0;
}
```

この組み合わせは、`scanf` 単独より入力処理を整理しやすいです。

### 2. 定型文字列から値を抜き出す

```c
const char *line = "x=10 y=20";
int x;
int y;

if (sscanf(line, "x=%d y=%d", &x, &y) == 2) {
    printf("x=%d y=%d\n", x, y);
}
```

ログや設定風の文字列から値を取り出すときに便利です。

### 3. 余分な文字が残っていないか確認する

```c
const char *line = "123abc";
int value;
char extra;

if (sscanf(line, "%d%c", &value, &extra) == 1) {
    printf("clean integer\n");
}
```

「整数だけを受け付けたいのに、後ろへゴミ文字が付いていないか」を調べたいときに考え方として使えます。

## 注意点

### 1. `scanf` と同じく、アドレスを渡すのが基本

```c
int x;
sscanf("10", "%d", &x);   /* 正しい */
```

### 2. `%s` を使うときは幅制限が必要

```c
char name[8];
sscanf("verylongname", "%7s", name);
```

文字列解析でもバッファサイズを超えないようにする必要があります。

### 3. 書式が厳密すぎると少しのズレで失敗する

```c
sscanf(line, "x=%d y=%d", &x, &y);
```

この形式を期待していると、空白や記号の違いで失敗することがあります。
入力フォーマットをどこまで厳密にしたいかを考える必要があります。

### 4. 解析成功と、文字列全体が妥当かは別問題

例えば `%d` だけで読むと、先頭だけ読めて後ろに不要文字が残っていても成功扱いになることがあります。
必要に応じて「最後まで正しく読めたか」を追加で確認します。

## `sscanf` と `strtol` の関係

整数1個だけを厳密に読みたいなら、`strtol` のほうがエラー判定を細かく行いやすい場面もあります。

一方で、複数項目をまとめて簡潔に抜きたいときは `sscanf` が便利です。

## まず覚える形

最初はこれで十分です。

```c
if (sscanf(line, "%d", &value) != 1) {
    /* parse error */
}
```

`fgets` で 1 行読み、その文字列を `sscanf` で解釈する、という流れで覚えると整理しやすいです。

## 関連API

- `scanf`
  - 標準入力から直接読む
- `fgets`
  - まず 1 行受け取る
- `printf`
  - 解析した結果を表示する
- `strtol`
  - 整数変換をより厳密に扱いたいときに有力

## まとめ

- `sscanf` は標準ライブラリ関数
- 入力元は標準入力ではなく文字列
- 戻り値は代入できた項目数
- `fgets` と組み合わせると入力処理を整理しやすい
- `%s` では幅制限が重要