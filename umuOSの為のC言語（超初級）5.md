---
title: "UmuOSの為のC言語（超初級）5　構造体・ファイル処理編"
---

# UmuOSの為のC言語（超初級）5 — 構造体・ファイル処理 編

このシリーズは最終的に、UmuOS／ush／uim のソースコードを自力で読めるようになることを目標にします。  
ただし第5巻（第十二章〜第十三章）も UmuOS に一切依存しない形で、C言語の基本をやさしく・丁寧に説明します。


## 第十二章でやること：

- 「関連するデータをまとめて扱う」必要性を、具体例で理解する
- 構造体（`struct`）の定義と、メンバアクセス（`.`）が書ける
- 構造体の初期化が書ける（通常の初期化／指名初期化）
- 構造体へのポインタを使い、`->` でメンバにアクセスできる
- `typedef` を使って、構造体型を読みやすくできる
- 構造体を使った小さなプログラム（配列＋関数）を組み立てられる
- 集成体型（配列／構造体）の「初期化・コピー・受け渡し」の特徴を押さえる
- 構造体を値で返す関数が書ける
- Cの「名前空間（同じ文字列でも別物になり得る）」の基本を押さえる
- 構造体の配列（`struct T a[N]`）を安全に扱える（要素数、走査、関数への受け渡し）
- 派生型（ポインタ／配列／関数）の位置づけを整理する

前提：

- OS：Linux想定
- コンパイラ：`gcc`（`clang`でも基本同じ）
- 規格：`-std=c17`

コンパイルの基本形（例）：

```bash
gcc -Wall -Wextra -std=c17 -O0 sample.c -o sample
./sample
```

---

## 第十二章　構造体

### 構造体

#### データの関連性

プログラムでは「関連する複数の値」をひとまとまりとして扱いたい場面がよくあります。

例：学生の情報

- 学籍番号（整数）
- 名前（文字列）
- 点数（整数）

これらは「同じ学生」に属するデータなので、まとめて扱いたいです。

もし「別々の配列」で管理すると、次のような事故が起きやすくなります。

- `id[i]` と `name[i]` と `score[i]` の `i` を揃え忘れる
- 並べ替え（ソート）で、配列同士の入れ替え漏れが起きる

この「関連するデータのまとまり」を表現するのが構造体です。

#### 構造体

構造体は「複数のメンバ（member）」を1つにまとめた型です。

最小の例：点（座標）

```c
/* file: struct_point.c */
#include <stdio.h>

struct Point {
    int x;
    int y;
};

int main(void)
{
    struct Point p; /* これで p は "Point 型の変数" */

    p.x = 10;
    p.y = 20;

    printf("p=(%d,%d)\n", p.x, p.y);
    return 0;
}
```

ポイント：

- `struct Point { ... };` が型（構造体型）の定義
- `struct Point p;` で、その型の変数を作る

#### 構造体のメンバと.演算子

`.` は「構造体のメンバにアクセスする」演算子です。

- `p.x`：`p` の `x` メンバ
- `p.y`：`p` の `y` メンバ

`p.x = 10;` は「`p` の `x` に 10 を入れる」です。

#### メンバの初期化

構造体変数は、宣言と同時に初期化できます。

```c
/* file: struct_init.c */
#include <stdio.h>

struct Point {
    int x;
    int y;
};

int main(void)
{
    struct Point a = { 1, 2 }; /* 並び順で初期化 */

    /* 指名初期化（どのメンバか明示） */
    struct Point b = { .y = 20, .x = 10 };

    printf("a=(%d,%d)\n", a.x, a.y);
    printf("b=(%d,%d)\n", b.x, b.y);
    return 0;
}
```

ポイント：

- `{ 1, 2 }` は「定義順に対応」する
- 指名初期化（`.x = ...`）は「どこに入るか」が明確で事故が減る

注意：

- 定義順の初期化は、後からメンバを追加したときに意図が崩れやすい
- 最初は指名初期化を多用してもよい（読みやすさが勝つ）

#### 構造体のメンバと->演算子

`->` は「構造体へのポインタ」からメンバにアクセスするときに使います。

まずは状況を分解します。

- `&p`：`p` のアドレス
- `struct Point *pp = &p;`：`pp` は `p` を指すポインタ

```c
/* file: struct_arrow.c */
#include <stdio.h>

struct Point {
    int x;
    int y;
};

int main(void)
{
    struct Point p = { .x = 10, .y = 20 };
    struct Point *pp = &p;

    /* . は構造体変数に対して */
    printf("p.x=%d p.y=%d\n", p.x, p.y);

    /* -> は構造体へのポインタに対して */
    printf("pp->x=%d pp->y=%d\n", pp->x, pp->y);

    /* (*pp).x と pp->x は同じ意味 */
    printf("(*pp).x=%d\n", (*pp).x);

    return 0;
}
```

ポイント：

- `pp->x` は `(*pp).x` の省略形
- `*` と `.` の優先順位の都合で `(*pp).x` と括弧が要る（`*pp.x` は別の意味になる）

#### 構造体とtypedef

`struct Point` は毎回 `struct` が付くので、長く感じることがあります。
`typedef` を使うと、型名の別名を付けられます。

```c
/* file: typedef_point.c */
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

int main(void)
{
    Point p = { .x = 1, .y = 2 };
    printf("(%d,%d)\n", p.x, p.y);
    return 0;
}
```

この書き方では「構造体のタグ名」は付きません。
タグ名も欲しい場合は次の形がよく使われます。

```c
/* file: typedef_with_tag.c */
#include <stdio.h>

typedef struct Point {
    int x;
    int y;
} Point;

int main(void)
{
    Point p = { .x = 3, .y = 4 };
    struct Point q = { .x = 5, .y = 6 };

    printf("p=(%d,%d)\n", p.x, p.y);
    printf("q=(%d,%d)\n", q.x, q.y);
    return 0;
}
```

ポイント：

- `Point` は typedef 名
- `struct Point` は構造体タグ名

最初は、よく出る形として「typedef 名だけで使える」状態にしておくのが読みやすいです。

#### 構造体とプログラム

構造体は「データのまとまり」を作るだけで終わりません。
構造体と関数を組み合わせると、プログラムの見通しが大きく良くなります。

ここでは「学生情報を配列で持ち、平均点を出す」だけの小さな例を作ります。

```c
/* file: students.c */
#include <stdio.h>
#include <string.h>

#define NAME_MAX 16
#define N_MAX 5

typedef struct {
    int id;
    char name[NAME_MAX];
    int score;
} Student;

static void print_student(const Student *s)
{
    /* s は読み取り専用として扱う（書き換えない意図） */
    printf("id=%d name=%s score=%d\n", s->id, s->name, s->score);
}

static int sum_scores(const Student *a, int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += a[i].score;
    }
    return sum;
}

int main(void)
{
    Student st[N_MAX] = {
        { .id = 1, .name = "alice", .score = 80 },
        { .id = 2, .name = "bob",   .score = 90 },
        { .id = 3, .name = "carol", .score = 70 },
    };

    int n = 3;

    for (int i = 0; i < n; i++) {
        print_student(&st[i]);
    }

    int sum = sum_scores(st, n);
    printf("avg=%.2f\n", (double)sum / (double)n);

    /* name の変更例：配列なので中身を書き換えられる */
    strncpy(st[0].name, "ALICE", sizeof(st[0].name));
    st[0].name[sizeof(st[0].name) - 1] = '\0';

    print_student(&st[0]);
    return 0;
}
```

ポイント：

- `Student st[N_MAX]` は「構造体の配列」
- `print_student` は `const Student *` を受け、`->` でメンバにアクセス
- `sum_scores` は配列と要素数 `n` を受け取って合計を作る

注意：

- `strncpy` は終端 `\0` が必ず付くとは限らないので、最後を `\0` にする
- 配列を関数に渡すときは「配列＋要素数」が基本（第2巻の配列のルールと同じ）

#### 集成体型

Cには「集成体型（aggregate type）」と呼ばれるものがあります。
ここでは難しい定義を避けて、実用上の感覚だけ整理します。

- 配列：同じ型の要素が連続して並ぶまとまり
- 構造体：異なる型の要素（メンバ）を1つにまとめたまとまり

集成体型の特徴：

- 初期化が「まとめて」書ける（`{ ... }`）
- 多くの場合、代入で「中身がコピー」される（構造体の代入）

構造体の代入（コピー）の例：

```c
/* file: struct_copy.c */
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

int main(void)
{
    Point a = { .x = 1, .y = 2 };
    Point b = a; /* メンバごとにコピーされる */

    b.x = 100;

    printf("a=(%d,%d)\n", a.x, a.y);
    printf("b=(%d,%d)\n", b.x, b.y);
    return 0;
}
```

ポイント：

- `b = a;` で a と b は別物になる（片方を書き換えてももう片方は変わらない）

#### 構造体のメモリ配置（padding とアラインメント）

構造体は「メンバを並べたもの」ですが、メモリ上ではメンバの間に“すき間”が入ることがあります。
この“すき間”を padding（パディング）と呼びます。

理由（感覚）：

- CPUが扱いやすい位置（アラインメント）にメンバを置くため

最小例：

```c
/* file: struct_padding.c */
#include <stdio.h>

typedef struct {
    char c;
    int x;
} S;

int main(void)
{
    printf("sizeof(S)=%zu\n", sizeof(S));
    return 0;
}
```

多くの環境では、`sizeof(S)` は `sizeof(char) + sizeof(int)` より大きくなります。

注意：

- 構造体をバイナリとしてファイルに保存する場合、padding も一緒に保存される
- 別の環境（コンパイラ設定、CPU）では `sizeof` や配置が変わる可能性がある

このシリーズでは「まず読み書きできる」状態を作り、
UmuOSのコードを読む段階で必要になったところで、より深く扱います。

#### 構造体の値を返却する関数

構造体は「値として返す」こともできます。

```c
/* file: return_struct.c */
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

static Point make_point(int x, int y)
{
    Point p = { .x = x, .y = y };
    return p; /* 値で返す */
}

static Point add_point(Point a, Point b)
{
    return (Point){ .x = a.x + b.x, .y = a.y + b.y };
}

int main(void)
{
    Point p = make_point(1, 2);
    Point q = make_point(10, 20);
    Point r = add_point(p, q);

    printf("r=(%d,%d)\n", r.x, r.y);
    return 0;
}
```

ポイント：

- `Point` のような小さな構造体なら「値で返す」方が読みやすいことが多い

注意：

- 大きい構造体や頻繁なコピーが性能に効く場面では、ポインタ引数で返す設計もある
- ただし「まず正しく安全に動く」設計を優先する

#### 名前空間

Cでは「同じ文字列でも、文脈によって別物」になり得ます。
この性質を、ここでは“名前空間”として整理します。

最初に押さえるポイント：

- `struct タグ名` のタグ名は「タグ名の名前空間」
- `typedef` 名や変数名は「通常の識別子の名前空間」

そのため、次のようなコードは読みにくいですが、成立することがあります。

```c
/* file: namespaces.c */
#include <stdio.h>

typedef int Point; /* これは typedef 名 */

struct Point {      /* これは struct タグ名 */
    int x;
};

int main(void)
{
    Point Point = 123;      /* typedef 名 Point と変数名 Point が同名でややこしい */
    struct Point p = { .x = 1 };

    printf("Point=%d\n", Point);
    printf("p.x=%d\n", p.x);
    return 0;
}
```

注意：

- 実務では、同名をわざと作って読みづらくするのは避ける
- ソースを読むときは「`struct Point` は型で、`Point` は typedef や変数の可能性がある」と切り分ける

#### 構造体の配列

構造体も配列にできます。

```c
/* file: array_of_struct.c */
#include <stdio.h>

typedef struct {
    int id;
    int score;
} Item;

static void print_items(const Item *a, int n)
{
    for (int i = 0; i < n; i++) {
        printf("id=%d score=%d\n", a[i].id, a[i].score);
    }
}

int main(void)
{
    Item a[] = {
        { .id = 1, .score = 10 },
        { .id = 2, .score = 30 },
        { .id = 3, .score = 20 },
    };

    int n = (int)(sizeof(a) / sizeof(a[0]));
    print_items(a, n);
    return 0;
}
```

ポイント：

- 配列の要素数は `sizeof(a) / sizeof(a[0])` で求められる（配列がスコープ内にある場合）
- 関数に渡すときは `a` と `n` をセットで渡す

注意：

- 関数側で `sizeof(a)` をやっても、`a` はポインタとして扱われるため期待どおりにならない

#### 派生型

Cの型は、元になる型（`int` など）から「派生」して作られるものがあります。

代表例：

- ポインタ型：`int *`、`Point *`
- 配列型：`int a[10]`、`Point pts[5]`
- 関数型：`int f(int)` のようなもの（関数ポインタの土台になる）

構造体自体は「まとまり（集成体）」ですが、

- `Point *`（構造体へのポインタ）
- `Point pts[10]`（構造体の配列）

のように、派生型がすぐ出てきます。

---

### メンバとしての構造体

#### 座標を表す構造体

座標を表す `Point` は今後の例でもよく使います。

```c
/* file: point.c */
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

int main(void)
{
    Point p = { .x = 10, .y = 20 };
    printf("(%d,%d)\n", p.x, p.y);
    return 0;
}
```

#### 構造体のメンバを持つ構造体

構造体のメンバとして、別の構造体を持つこともできます。

例：長方形（左上と右下の点で表す）

```c
/* file: rect.c */
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point left_top;
    Point right_bottom;
} Rect;

static int width(Rect r)
{
    return r.right_bottom.x - r.left_top.x;
}

static int height(Rect r)
{
    return r.left_top.y - r.right_bottom.y;
}

int main(void)
{
    Rect r = {
        .left_top = { .x = 10, .y = 50 },
        .right_bottom = { .x = 40, .y = 10 },
    };

    printf("w=%d h=%d\n", width(r), height(r));
    return 0;
}
```

ポイント：

- `Rect` の中に `Point` をそのまま埋め込める
- 初期化では「入れ子の指名初期化」が書ける

注意：

- 幅や高さが負になる表現もあり得るので、前提（座標系）を決めてから使う

---

### まとめ

- 構造体は「関連するデータ」をまとめて扱うための型
- `.` は構造体変数のメンバアクセス、`->` は構造体へのポインタのメンバアクセス
- 初期化は `{ ... }` ででき、指名初期化は読み間違いを減らす
- `typedef` で `struct` を省略でき、型名が読みやすくなる
- 構造体の代入は基本的にメンバごとのコピー
- 構造体は値で返せる（設計として分かりやすい場面が多い）
- 構造体の配列は「要素数の管理」が重要（`sizeof` で求める／関数へは配列＋要素数）

---

## 第十三章でやること：

- 「ファイル」と「ストリーム」の違いを言葉で説明できる
- 標準ストリーム（`stdin`/`stdout`/`stderr`）を整理し、`FILE` 型を扱える
- `fopen`/`fclose` でファイルを開閉でき、失敗時の分岐を書ける
- ファイルから読み、集計して表示できる
- 日付・時刻を書き出せる（ログの基本形）
- 「前回実行時の情報」をファイルに保存し、次回起動時に読み戻せる
- ファイルの中身を表示できる（行単位）
- ファイルをコピーできる（テキスト／バイナリの両方の考え方）
- テキスト形式とバイナリ形式の違い（見える／見えない、互換性、サイズ）を理解する
- 実数値をテキスト／バイナリに保存し、読み戻して差を確認できる
- ファイルのダンプ（16進表示）の最小版を作れる
- `printf`/`scanf` の「書式」の読み方と、よくある事故（幅指定、型の不一致）を回避できる

---

## 第十三章　ファイル処理

### ファイルとストリーム

#### ファイルとストリーム

- ファイル：ディスク（など）に保存され、プログラムが終わっても残るデータ
- ストリーム：プログラムが「順番に読み書きする」ための流れ（入出力の抽象化）

Cの標準入出力ライブラリ（`<stdio.h>`）は、ファイルもキーボード入力も画面出力も、
ストリームとして同じような形で扱えるようにします。

#### 標準ストリーム

最初に出てくる3つのストリームです。

- `stdin`：標準入力（通常はキーボード）
- `stdout`：標準出力（通常は画面）
- `stderr`：標準エラー出力（エラー表示用。stdout と分けると便利）

`printf` は本質的に「stdout に書く」関数です。
ファイルに書きたい場合は `fprintf`（file printf）を使います。

#### FILE型

`FILE` は「ストリームを表す構造体型」です。

- `FILE *fp;` のように「`FILE` へのポインタ」として扱う

実装の中身は処理系ごとに違うので、直接触らず、標準関数で操作します。

#### ファイルのオープン

`fopen` でファイルを開きます。

```c
FILE *fp = fopen("data.txt", "r");
```

- 第1引数：ファイル名
- 第2引数：モード（読み書きの方法）

代表的なモード：

- `"r"`：読み込み（既存ファイルが必要）
- `"w"`：書き込み（新規作成／既存なら上書き）
- `"a"`：追記（末尾に追加）
- `"rb"`：読み込み（バイナリ）
- `"wb"`：書き込み（バイナリ）

注意：

- `fopen` は失敗すると `NULL` を返す
- 失敗理由は「存在しない」「権限がない」「パスが違う」など

#### ファイルのクローズ

使い終わったら `fclose` で閉じます。

```c
fclose(fp);
```

- バッファに溜まったデータを反映するためにも重要
- 閉じ忘れると、データが書き出されない／資源が枯渇することがある

#### オープンとクローズの例

```c
/* file: open_close.c */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("out.txt", "w");
    if (fp == NULL) {
        /* エラーは stderr に出すと便利 */
        fprintf(stderr, "fopen failed\n");
        return 1;
    }

    fprintf(fp, "hello file\n");

    if (fclose(fp) != 0) {
        fprintf(stderr, "fclose failed\n");
        return 1;
    }

    return 0;
}
```

ポイント：

- ファイルへの出力は `fprintf(fp, ...)`
- 失敗時は `NULL` チェックと `fclose` の戻り値チェックをする

---

#### ファイルデータの集計

「整数が並んだファイル」を読み、合計を出します。

ファイル `nums.txt`（例）：

```
10
20
30
```

プログラム：

```c
/* file: sum_file.c */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("nums.txt", "r");
    if (fp == NULL) {
        fprintf(stderr, "cannot open nums.txt\n");
        return 1;
    }

    int x = 0;
    long long sum = 0;
    int count = 0;

    /* fscanf は scanf のファイル版 */
    while (fscanf(fp, "%d", &x) == 1) {
        sum += x;
        count++;
    }

    fclose(fp);

    printf("count=%d sum=%lld\n", count, sum);
    return 0;
}
```

注意：

- `fscanf` の戻り値は「読み取れた項目数」
- `== 1` を確認しながらループする

#### 日付と時刻の書き込み

ログを書く最小例です。

```c
/* file: write_time.c */
#include <stdio.h>
#include <time.h>

int main(void)
{
    FILE *fp = fopen("log.txt", "a");
    if (fp == NULL) {
        fprintf(stderr, "cannot open log.txt\n");
        return 1;
    }

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fprintf(stderr, "time failed\n");
        fclose(fp);
        return 1;
    }

    struct tm *t = localtime(&now);
    if (t == NULL) {
        fprintf(stderr, "localtime failed\n");
        fclose(fp);
        return 1;
    }

    /* YYYY-MM-DD HH:MM:SS の形に整える */
    char buf[64];
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t) == 0) {
        fprintf(stderr, "strftime failed\n");
        fclose(fp);
        return 1;
    }

    fprintf(fp, "%s\n", buf);
    fclose(fp);
    return 0;
}
```

ポイント：

- `<time.h>` の `time` / `localtime` / `strftime` で時刻文字列を作れる
- `"a"` モードで追記していく（ログに向く）

#### 前回実行時の情報を取得

「前回起動時刻」をファイルに保存し、次回起動時に読み戻します。

考え方：

- 起動時：`last.txt` を読めたら表示
- 終了前：今回の時刻を書いておく

```c
/* file: last_run.c */
#include <stdio.h>
#include <time.h>

static int write_now(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return 0;
    }

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fclose(fp);
        return 0;
    }

    /* ここでは最小の例として UNIX 時刻（秒）を保存する */
    fprintf(fp, "%lld\n", (long long)now);

    if (fclose(fp) != 0) {
        return 0;
    }
    return 1;
}

static int read_last(const char *path, long long *out)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }

    long long v = 0;
    if (fscanf(fp, "%lld", &v) != 1) {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    *out = v;
    return 1;
}

int main(void)
{
    const char *path = "last.txt";

    long long last = 0;
    if (read_last(path, &last)) {
        printf("last run: %lld (unix time)\n", last);
    } else {
        puts("no last run info");
    }

    if (!write_now(path)) {
        fprintf(stderr, "write failed\n");
        return 1;
    }

    return 0;
}
```

ポイント：

- ファイルに保存しておけば、プログラム終了後も情報が残る
- 「読めない」場合（初回など）を分岐で吸収する

#### ファイルの中身の表示

テキストファイルは「行単位」で読むと扱いやすいです。

```c
/* file: cat_like.c */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("log.txt", "r");
    if (fp == NULL) {
        fprintf(stderr, "cannot open log.txt\n");
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        /* そのまま標準出力に書く */
        fputs(line, stdout);
    }

    fclose(fp);
    return 0;
}
```

ポイント：

- `fgets` は「1行（またはバッファが埋まるまで）」読む
- `fputs` は文字列を出力する（末尾改行は自動では付けない）

#### ファイルのコピー

ファイルコピーは「バイナリでも確実に動く」形にしておくと安全です。

```c
/* file: copy_file.c */
#include <stdio.h>

int main(void)
{
    FILE *in = fopen("in.bin", "rb");
    if (in == NULL) {
        fprintf(stderr, "cannot open in.bin\n");
        return 1;
    }

    FILE *out = fopen("out.bin", "wb");
    if (out == NULL) {
        fprintf(stderr, "cannot open out.bin\n");
        fclose(in);
        return 1;
    }

    unsigned char buf[4096];
    size_t nread;

    while ((nread = fread(buf, 1, sizeof(buf), in)) > 0) {
        size_t nwritten = fwrite(buf, 1, nread, out);
        if (nwritten != nread) {
            fprintf(stderr, "write failed\n");
            fclose(in);
            fclose(out);
            return 1;
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}
```

ポイント：

- `fread`/`fwrite` は「バイト列」を扱うので、テキスト／バイナリどちらでもコピーできる

---

### テキストとバイナリ

#### テキストファイルへの実数値の保存

実数（`double`）をテキストとして保存する例です。

```c
/* file: save_double_text.c */
#include <stdio.h>

int main(void)
{
    double x = 0.1;

    FILE *fp = fopen("x.txt", "w");
    if (fp == NULL) {
        fprintf(stderr, "cannot open\n");
        return 1;
    }

    /* できるだけ情報を落としにくい形で保存する */
    fprintf(fp, "%.17g\n", x);
    fclose(fp);
    return 0;
}
```

ポイント：

- テキストは人間が読める
- `%.17g` は `double` をある程度正確に文字列化しやすい

#### テキストファイルとバイナリファイル

違いの感覚：

- テキスト：人間が読める／移植しやすい／サイズが増えがち
- バイナリ：人間は読みにくい／速い・小さいことが多い／互換性（エンディアン等）に注意

Linuxでは「テキストとバイナリの扱いが同じ」ことが多いですが、
他の環境も含めて考えると、`rb`/`wb` を付けておくのが安全側です。

#### バイナリファイルへの実数値の保存

```c
/* file: save_double_bin.c */
#include <stdio.h>

int main(void)
{
    double x = 0.1;

    FILE *fp = fopen("x.bin", "wb");
    if (fp == NULL) {
        fprintf(stderr, "cannot open\n");
        return 1;
    }

    if (fwrite(&x, sizeof(x), 1, fp) != 1) {
        fprintf(stderr, "write failed\n");
        fclose(fp);
        return 1;
    }

    fclose(fp);
    return 0;
}
```

注意：

- このバイナリは「この環境の double 表現」をそのまま保存している
- 別の環境（エンディアン、浮動小数点の表現）ではそのまま読めない可能性がある

#### 読み戻して差を確認する

同じ `0.1` を「テキスト」と「バイナリ」で保存し、読み戻して表示すると、
浮動小数点の表現の都合が見えます（0.1 は2進で有限桁にならないため）。

```c
/* file: load_double_compare.c */
#include <stdio.h>

static int load_text(const char *path, double *out)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }

    double x;
    if (fscanf(fp, "%lf", &x) != 1) {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    *out = x;
    return 1;
}

static int load_bin(const char *path, double *out)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    double x;
    if (fread(&x, sizeof(x), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    *out = x;
    return 1;
}

int main(void)
{
    double a = 0.0;
    double b = 0.0;

    if (!load_text("x.txt", &a)) {
        fprintf(stderr, "load x.txt failed\n");
        return 1;
    }
    if (!load_bin("x.bin", &b)) {
        fprintf(stderr, "load x.bin failed\n");
        return 1;
    }

    printf("text  = %.17g\n", a);
    printf("bin   = %.17g\n", b);
    printf("diff  = %.17g\n", a - b);
    return 0;
}
```

ポイント：

- バイナリは「同じ環境なら」かなり忠実に保存しやすい
- テキストは読みやすいが、桁数や丸め方で情報が落ちることがある

#### ファイルのダンプ

バイナリの中身を確認するには「16進で表示」すると見やすいです。

```c
/* file: dump.c */
#include <stdio.h>

static void dump_hex(FILE *fp)
{
    unsigned char buf[16];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; i++) {
            printf("%02x ", buf[i]);
        }
        putchar('\n');
    }
}

int main(void)
{
    FILE *fp = fopen("x.bin", "rb");
    if (fp == NULL) {
        fprintf(stderr, "cannot open x.bin\n");
        return 1;
    }

    dump_hex(fp);
    fclose(fp);
    return 0;
}
```

ポイント：

- `%02x` は「2桁の16進で表示（足りないときは 0 で埋める）」
- これだけでも「どのくらいのサイズのデータか」が見える

---

### printf関数とscanf関数

#### printf関数：書式付きの出力

`printf` は「書式文字列」と「値」を組み合わせて出力します。

```c
printf("i=%d\n", 123);
printf("x=%.2f\n", 3.14159);
```

よく使う変換指定（最小セット）：

- `%d`：`int`
- `%lld`：`long long`
- `%zu`：`size_t`
- `%f`：`double`（表示）
- `%c`：`char`
- `%s`：文字列（`char *`／`const char *`）
- `%p`：ポインタ（`(void *)` を渡す）

注意：

- 型と指定がズレると表示が壊れたり、未定義動作になり得る

幅と精度の例：

- `%8d`：最低8桁の幅で右寄せ
- `%.3f`：小数点以下3桁

#### scanf関数：書式付きの入力

`scanf` は「入力を指定の型に変換して、変数に書き込む」関数です。

基本形：

```c
int x;
if (scanf("%d", &x) != 1) {
    /* 失敗 */
}
```

ポイント：

- `&x` の `&` は「書き込み先の場所」を渡す
- 戻り値は「読み取れた項目数」

文字列の読み取りの注意：

`%s` は空白で区切られるため、長い入力でバッファを壊す可能性があります。
幅を指定します。

```c
/* file: scan_string_safe.c */
#include <stdio.h>

int main(void)
{
    char buf[16];

    /* 最大 15 文字 + 終端 '\0' */
    if (scanf("%15s", buf) != 1) {
        puts("read failed");
        return 1;
    }

    puts(buf);
    return 0;
}
```

---

### まとめ

- ファイルは「残るデータ」、ストリームは「順番に読み書きする流れ」
- `stdin`/`stdout`/`stderr` は最初に覚える標準ストリーム
- `FILE *` を `fopen` で得て、終わったら `fclose` する
- 読み込みは `fscanf`/`fgets`、書き込みは `fprintf`、バイト列は `fread`/`fwrite`
- テキストは読みやすく移植しやすいが大きくなりがち、バイナリは小さく速いことが多いが互換性に注意
- `printf`/`scanf` は書式が重要で、戻り値チェックや幅指定で事故を減らす
