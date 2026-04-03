---
title: "fgets(3) 1行を安全寄りに読み取る"
date: 2026-04-03
---

# fgets(3) — 1行を安全寄りに読み取る

`fgets` は、C言語の標準ライブラリが提供する入力関数です。
ストリームから 1 行ぶん、または指定した上限文字数までを読み取り、文字列としてバッファへ格納します。

`scanf` よりも入力長を制御しやすいため、特に文字列入力では `fgets` のほうが安全に扱いやすい場面が多いです。

## まず結論：fgets は何者か

- 分類：標準ライブラリ関数
- `man` の分類：セクション 3 相当
- ヘッダファイル：`<stdio.h>`
- ストリームから文字列を読み込む関数
- システムコールではない
- 演算子ではない

## `fgets` と `scanf` の違い

### `fgets` が向く場面

- 1 行まるごと読みたい
- 入力長の上限を明確にしたい
- 空白を含む入力を扱いたい
- 文字列入力を比較的安全に処理したい

### `scanf` が向く場面

- 決まった書式の整数や値をすぐ読みたい
- 空白区切り入力を手早く処理したい

つまり、

- `fgets` は「まず1行を安全寄りに取る」
- `scanf` は「読みながら書式解釈する」

という違いがあります。

## SYNOPSIS

```c
#include <stdio.h>

char *fgets(char *s, int size, FILE *stream);
```

## ヘッダファイル

```c
#include <stdio.h>
```

## 引数

- `s`
  - 読み込んだ文字列を書き込むバッファ
- `size`
  - `s` の大きさ
  - 最大で `size - 1` 文字まで読み込み、末尾に `\0` を付ける
- `stream`
  - 入力元のストリーム
  - 標準入力なら `stdin`

## 戻り値

- 成功時
  - `s` を返す
- 失敗時または EOF 時
  - `NULL` を返す

## 最小サンプル

```c
#include <stdio.h>

int main(void)
{
    char line[64];

    printf("name? ");
    if (fgets(line, sizeof(line), stdin) == NULL) {
        printf("input error\n");
        return 1;
    }

    printf("line = %s", line);
    return 0;
}
```

## 典型例

### 1. 標準入力から1行読む

```c
char line[128];

if (fgets(line, sizeof(line), stdin) != NULL) {
    printf("%s", line);
}
```

空白を含んでいても、その行をまとめて受け取れます。

### 2. 末尾の改行を取り除く

`fgets` は改行文字も読み込むことがあります。

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[64];

    if (fgets(line, sizeof(line), stdin) == NULL) {
        return 1;
    }

    line[strcspn(line, "\n")] = '\0';
    printf("[%s]\n", line);
    return 0;
}
```

この形はかなり定番です。

### 3. 読んだあとで数値解析する

```c
#include <stdio.h>

int main(void)
{
    char line[64];
    int value;

    if (fgets(line, sizeof(line), stdin) == NULL) {
        return 1;
    }

    if (sscanf(line, "%d", &value) == 1) {
        printf("value = %d\n", value);
    }

    return 0;
}
```

`fgets` で 1 行取り、`sscanf` で解釈する流れはかなり扱いやすいです。

## 注意点

### 1. 改行文字が残ることがある

`fgets` は行末の改行 `\n` を、バッファに収まる場合はそのまま含めます。

そのため、比較や連結をする前に、必要なら改行を削る必要があります。

### 2. 長すぎる行は途中までしか読めない

```c
char line[8];
fgets(line, sizeof(line), stdin);
```

長い入力に対しては、1回で 1 行全部が入らないことがあります。
このとき、残りは次回の読み取りへ持ち越されます。

### 3. `size` はバッファの実サイズに合わせる

```c
char line[32];
fgets(line, sizeof(line), stdin);
```

`sizeof(line)` を使うのが基本です。
手で数字を書くと、変更時のズレを起こしやすくなります。

### 4. EOF と入力エラーは `NULL` で判定する

戻り値は `NULL` かどうかをまず確認します。
必要なら `feof` や `ferror` でさらに詳細を調べます。

## `fgets` と `read` の違い

- `fgets` は標準ライブラリ関数
- `read` は低レベルのシステムコール系 API
- `fgets` は文字列として扱いやすい形で読む
- `read` は生のバイト列をそのまま扱う

文字列入力や対話入力では `fgets`、低レベル I/O 制御では `read` が向きます。

## まず覚える形

最初はこの形を覚えると十分です。

```c
char line[128];

if (fgets(line, sizeof(line), stdin) == NULL) {
    /* エラー処理 */
}
```

そして、必要なら改行を消します。

```c
line[strcspn(line, "\n")] = '\0';
```

## 関連API

- `scanf`
  - 書式に従って直接読み取る
- `sscanf`
  - 文字列を入力元として解釈する
- `read`
  - 低レベル I/O
- `strcspn`
  - 改行除去でよく使う

## まとめ

- `fgets` は標準ライブラリ関数
- 1 行入力を比較的安全に扱いやすい
- 入力長を制御しやすい
- 改行が残ることがあるので注意
- `fgets` と `sscanf` を組み合わせると扱いやすい