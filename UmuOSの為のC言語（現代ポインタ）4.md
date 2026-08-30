---
title: "UmuOSの為のC言語（現代ポインタ）　4章　ポインタと配列"
---

# UmuOSの為のC言語（現代ポインタ）

このノートは、UmuOSを進化させるために必要となるC言語のポインタ理解を、現代のC17相当の視点で抽象化し、再構成することであります。
すなわち、UmuOSの実装・読解・デバッグへ還元するための実践ノートであります。
ポインタは、単なる文法項目ではなく、メモリ、配列、文字列、関数呼び出し、所有権、未定義動作と深く結びついています。
この構造を理解することは、UmuOSの設計力を高めることに直結すると思います。

## 4章　ポインタと配列

配列は C 言語の中核にある基本データ構造です。
しかも C では、配列は単独で完結した概念ではなく、ポインタ、`sizeof`、関数引数、文字列、動的メモリ確保、多次元データ構造と密接に結びついています。

この章で最も重要なのは、**配列とポインタは深く関係しているが、同じものではない**、という点です。
配列名は多くの式で先頭要素へのポインタのように振る舞いますが、配列そのものが「書き換え可能なポインタ変数」になるわけではありません。
この違いを曖昧なままにすると、関数引数での `sizeof` の誤用、`int **` と 2 次元配列の混同、範囲外アクセス、誤った `realloc` の扱いなどに直結します。

また、配列は低レイヤでも非常によく現れます。
システムコールへ渡すバッファ、固定長の作業領域、文字列処理、ソケット受信バッファ、デバイス I/O 用の領域、テーブル駆動の設定データなど、実務では「配列をどう確保し、どこまで有効で、何個の要素を持つか」を追えることが重要です。

この章では、まず配列そのものを復習し、その後で配列記法とポインタ記法の対応関係を整理します。
次に、`malloc` と `realloc` を使った動的配列、関数へ配列を渡す方法、多次元配列とその関数引数、2 次元配列の動的確保、ジャグ配列まで扱います。
最終的には、UmuOS や Linux の低レイヤコードを読むときに「これは本物の配列か、先頭要素へのポインタか、行列の形は何か、連続領域か」を判断できる状態を目指します。

### 4章の1　配列の復習

配列とは、**同じ型の要素が連続して並んだオブジェクト**です。
連続しているとは、要素と要素のあいだに配列の都合による隙間がなく、`&array[i + 1]` が `&array[i]` の次の要素を指すことを意味します。

たとえば `int vector[5];` は、`int` が 5 個並んだ 1 個の配列オブジェクトです。
要素へは 0 始まりの添字でアクセスします。

```c
#include <stddef.h>

void example(void) {
	int vector[5] = {1, 2, 3, 4, 5};

	/* 有効な添字は 0 から 4 までです。 */
	int first = vector[0];
	int last = vector[4];

	(void)first;
	(void)last;
}
```

ここで大事なのは、C は添字の範囲を自動で検査しないという点です。
`vector[5]` や `vector[-1]` のようなアクセスは未定義動作です。
たまたま動くことがあっても、それは正しいことの証明にはなりません。

#### 4章の1の1　1次元配列

1 次元配列は、1 個の添字で各要素に到達する最も基本的な形です。
典型的な宣言と初期化は次のようになります。

```c
int vector[5] = {1, 2, 3, 4, 5};
```

配列オブジェクト自体は、自分で「要素数 5 です」と保持してくれるわけではありません。
ただし、**同じスコープで本物の配列として見えているあいだ**は `sizeof` を使って大きさを計算できます。

```c
#include <stdio.h>

int main(void) {
	int vector[5] = {1, 2, 3, 4, 5};
	size_t count = sizeof(vector) / sizeof(vector[0]);

	printf("bytes=%zu\n", sizeof(vector));
	printf("count=%zu\n", count);
	return 0;
}
```

この例では、`sizeof(vector)` は配列全体のバイト数です。
`int` が 4 バイトなら 20 になります。
`sizeof(vector[0])` は 1 要素分のバイト数なので、要素数は 5 と分かります。

ただしこの手法は、**配列そのものが見えている場所でしか使えません**。
関数引数に渡った後は事情が変わります。これは後の 4章の5 で扱います。

#### 4章の1の2　2次元配列

2 次元配列は、配列の配列です。
たとえば次の宣言は、2 行 3 列の `int` 配列を表します。

```c
int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
```

概念的には表のように見えますが、実メモリ上では 1 本の連続領域として並びます。
C では通常、先頭行の全要素、次の行の全要素、という順に配置されます。

```c
#include <stdio.h>

int main(void) {
	int matrix[2][3] = {
		{1, 2, 3},
		{4, 5, 6}
	};

	for (size_t row = 0; row < 2; row++) {
		printf("&matrix[%zu] = %p, sizeof(matrix[%zu]) = %zu\n",
		       row,
		       (void *)&matrix[row],
		       row,
		       sizeof(matrix[row]));
	}

	return 0;
}
```

`matrix[row]` は、その行全体を表す配列です。
式の中では先頭要素へのポインタとして振る舞うため、行ポインタのように見えます。
ただし型の本質は「要素数 3 の `int` 配列」です。
ここを `int *` と `int (*)[3]` で混同しないことが重要です。

#### 4章の1の3　多次元配列

3 次元以上でも考え方は同じです。
次の例は 3 行 2 列 4 要素の 3 次元配列です。

```c
int arr3d[3][2][4] = {
	{{1, 2, 3, 4}, {5, 6, 7, 8}},
	{{9, 10, 11, 12}, {13, 14, 15, 16}},
	{{17, 18, 19, 20}, {21, 22, 23, 24}}
};
```

`arr3d[1]` は 2 次元配列、`arr3d[1][0]` は 1 次元配列、`arr3d[1][0][2]` は単一の `int` です。
低レイヤコードでは、こうした「ある添字の段階ではまだ配列であり、次でスカラーになる」という構造がよく出ます。

### 4章の2　ポインタ記法と配列

配列とポインタが似て見える最大の理由は、**多くの式で配列名が先頭要素へのポインタへ変換される**からです。
次のコードを見ます。

```c
int vector[5] = {1, 2, 3, 4, 5};
int *pv = vector;
```

この代入は有効です。
`vector` はこの式で `&vector[0]` とほぼ同じ意味になり、`pv` は先頭要素を指します。

```c
#include <stdio.h>

int main(void) {
	int vector[5] = {1, 2, 3, 4, 5};

	printf("vector     = %p\n", (void *)vector);
	printf("&vector[0] = %p\n", (void *)&vector[0]);
	printf("&vector    = %p\n", (void *)&vector);
	return 0;
}
```

表示される数値は同じに見えることが多いですが、意味は同じではありません。

```text
vector:
	多くの式で先頭要素へのポインタとして振る舞う
	型の見方としては int * 相当になる場面が多い

&vector[0]:
	先頭要素そのもののアドレス
	型は int *

&vector:
	配列全体へのアドレス
	型は int (*)[5]
```

`&vector` が指しているのは配列全体です。
したがって `&vector + 1` は「`int` 1 個分」ではなく「配列全体 1 個分」だけ進みます。

#### 4章の2の1　`array[i]` と `*(array + i)`

配列記法とポインタ記法は、多くの場面で等価です。

```c
int vector[5] = {10, 20, 30, 40, 50};

int a = vector[2];
int b = *(vector + 2);
```

この `a` と `b` は同じ値になります。
同様に、ポインタ変数でも同じことができます。

```c
int *pv = vector;

int c = pv[2];
int d = *(pv + 2);
```

ここで意識すべきは、`[]` は特別な「配列専用構文」というより、**ポインタ加算して間接参照するための記法**だということです。

```text
pv[i]
	=
*(pv + i)
```

ただし、等価だからといってどちらでも常に同じ可読性になるわけではありません。
配列として扱う意図が明確なら `arr[i]` の方が読みやすいことが多く、ポインタ走査の意味を強調したいなら `*pv++` のような書き方が自然です。

#### 4章の2の2　配列とポインタは同一ではない

最重要ポイントを整理します。

```text
配列:
	固定個数の要素を持つ 1 個のオブジェクト
	配列名に別のアドレスを再代入できない
	sizeof は配列全体のバイト数を返す

ポインタ変数:
	アドレス値を保持する独立した変数
	別の場所を指すよう再代入できる
	sizeof はポインタ変数自身のバイト数を返す
```

```c
int vector[5] = {1, 2, 3, 4, 5};
int *pv = vector;

pv = pv + 1;          /* 可能 */
/* vector = vector + 1; */
```

最後の代入はできません。
配列名はポインタ変数ではないからです。
「式の中でポインタのように使えることがある」のであって、「可変なポインタ変数そのもの」ではありません。

#### 4章の2の3　ポインタ演算の有効範囲

ポインタに整数を足すと、アドレス値へそのまま足し算するのではなく、指している型の大きさ単位で進みます。

```c
int vector[5] = {1, 2, 3, 4, 5};
int *pv = vector;

pv = pv + 1; /* 次の int 要素へ進む */
```

ただし、ポインタ演算が意味を持つのは、**同じ配列オブジェクトの範囲内か、末尾の 1 つ先まで**です。
それを超えたポインタ値を作ったり、その先を参照したりすると未定義動作になります。

```c
int vector[5] = {1, 2, 3, 4, 5};
int *end = vector + 5;      /* これは作れる */
/* int value = *end; */     /* これは未定義動作 */
```

### 4章の3　malloc を使った 1 次元配列の作成

固定長配列は簡潔ですが、要素数を実行時に決めたい場面では動的確保が必要です。
そのときは `malloc` を使って連続領域を確保し、それを配列として扱います。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	size_t count = 5;
	int *vector = malloc(count * sizeof(*vector));

	if (vector == NULL) {
		return 1;
	}

	for (size_t index = 0; index < count; index++) {
		vector[index] = (int)(index + 1);
	}

	for (size_t index = 0; index < count; index++) {
		printf("%d\n", vector[index]);
	}

	free(vector);
	return 0;
}
```

現代的な C では、`malloc` の戻り値を C でわざわざキャストしない書き方が一般的です。
`sizeof(*vector)` と書いておくと、型を `int` から別のものへ変えたときにも追従しやすくなります。

この `vector` は「本物の配列宣言」ではありません。
型はあくまで `int *` です。
それでも `vector[index]` と書けるのは、ポインタに対して配列記法が使えるからです。

```c
for (size_t index = 0; index < count; index++) {
	*(vector + index) = (int)(index + 1);
}
```

この書き方も等価ですが、通常は添字記法の方が読みやすいです。

### 4章の4　realloc による配列サイズの変更

`malloc` で得た領域は、`realloc` でサイズ変更できます。
これは「入力長が事前に読めないバッファ」や「段階的に大きくしていく作業配列」で特に有効です。

#### 4章の4の1　安全な `realloc` の基本

`realloc` は、元の領域をそのまま拡張できることもあれば、別の場所へ移して内容をコピーし、旧領域を内部で解放することもあります。
したがって戻り値は必ず受け取り直さなければなりません。

```c
int *tmp = realloc(vector, new_count * sizeof(*vector));
if (tmp == NULL) {
	free(vector);
	return NULL;
}
vector = tmp;
```

ここで一時変数 `tmp` を使うのが重要です。
もし `vector = realloc(vector, ...);` と直接代入して失敗した場合、元のポインタを失って `free` できなくなります。

#### 4章の4の2　可変長入力バッファの例

次の例は、標準入力から 1 行読み取り、必要に応じてバッファを拡張する関数です。

```c
#include <stdio.h>
#include <stdlib.h>

char *get_line_dynamic(FILE *stream) {
	const size_t chunk = 16;
	size_t capacity = chunk;
	size_t length = 0;
	char *buffer = malloc(capacity);

	if (buffer == NULL) {
		return NULL;
	}

	for (;;) {
		int ch = fgetc(stream);

		if (ch == EOF || ch == '\n') {
			break;
		}

		if (length + 1 >= capacity) {
			size_t new_capacity = capacity + chunk;
			char *tmp = realloc(buffer, new_capacity);

			if (tmp == NULL) {
				free(buffer);
				return NULL;
			}

			buffer = tmp;
			capacity = new_capacity;
		}

		buffer[length] = (char)ch;
		length++;
	}

	buffer[length] = '\0';
	return buffer;
}
```

この関数の本質は次の通りです。

```text
buffer:
	所有している動的メモリ

capacity:
	安全に書き込める総バイト数

length:
	現在までに格納した文字数

length + 1 >= capacity:
	終端の NUL 文字分も含めて足りるかどうかを見ている
```

古い説明では「C99 なら可変長配列がある」と流れがちですが、現代では少し補足が必要です。
可変長配列（VLA）は C99 で導入されましたが、C11 以降では実装が任意になり、処理系によっては使えません。
また、VLA は自動記憶域期間の配列であり、`realloc` の代替ではありません。
関数を抜けたあとも保持したいなら、動的確保が必要です。

#### 4章の4の3　縮小もできる

`realloc` は拡張だけでなく縮小にも使えます。
ただし、縮小しても「メモリ節約になるはず」と決めつける必要はありません。
実装によっては内部最適化が異なるため、縮小のコストや効果は状況依存です。

文字列先頭の空白を詰める例は、次のように書けます。

```c
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *trim_left(char *text) {
	char *read_ptr = text;
	char *write_ptr = text;

	while (*read_ptr != '\0' && isspace((unsigned char)*read_ptr)) {
		read_ptr++;
	}

	while (*read_ptr != '\0') {
		*write_ptr = *read_ptr;
		write_ptr++;
		read_ptr++;
	}

	*write_ptr = '\0';

	/* 失敗時は元の領域をそのまま返す方が呼び出し側で扱いやすいです。 */
	char *tmp = realloc(text, strlen(text) + 1);
	return tmp != NULL ? tmp : text;
}
```

### 4章の5　1 次元配列を関数に渡す

1 次元配列を関数に渡すとき、実際に渡るのは配列全体のコピーではなく、先頭要素を指す値です。
したがって関数側では、元の配列の要素へ直接アクセスできます。

ただし、**要素数は自動では伝わりません**。
ここが最も多い誤解です。

#### 4章の5の1　配列記法の関数引数

```c
#include <stdio.h>

void display_array(const int arr[], size_t size) {
	for (size_t index = 0; index < size; index++) {
		printf("%d\n", arr[index]);
	}
}
```

呼び出し側は次のようになります。

```c
int main(void) {
	int vector[5] = {1, 2, 3, 4, 5};
	display_array(vector, 5);
	return 0;
}
```

ここで `arr[]` と書いてありますが、関数引数では本物の配列を受け取っているわけではありません。
関数の意味としては `const int *arr` とほぼ同じです。

`const` を付けているのは、この関数が表示だけを行い、要素を書き換えない契約を明示するためです。

#### 4章の5の2　ポインタ記法の関数引数

同じ関数は次のようにも書けます。

```c
void display_array(const int *arr, size_t size) {
	for (size_t index = 0; index < size; index++) {
		printf("%d\n", arr[index]);
	}
}
```

あるいは、ポインタ演算を前面に出して次のようにも書けます。

```c
void display_array(const int *arr, size_t size) {
	for (size_t index = 0; index < size; index++) {
		printf("%d\n", *(arr + index));
	}
}
```

どれも意味は同じです。
ただし保守性の観点では、単に走査したいだけなら `arr[index]` の方が読みやすい場面が多いです。

#### 4章の5の3　`sizeof` の落とし穴

関数引数に渡った `arr` に対して `sizeof(arr)` を使うと、配列サイズは分かりません。
そこにあるのはポインタだからです。

```c
void broken(const int arr[]) {
	/* ここでの arr は配列ではなくポインタとして扱われます。 */
	size_t wrong = sizeof(arr);
	(void)wrong;
}
```

したがって、要素数が必要なら呼び出し側から別途渡す必要があります。
文字列だけは NUL 終端を使って長さを探索できますが、それでも線形走査が必要ですし、一般の配列には使えません。

### 4章の6　ポインタの 1 次元配列

ここで扱うのは「配列の中身がポインタ」である構造です。
たとえば `int *arr[5];` は、`int *` が 5 個並んだ配列です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	int *arr[5] = {NULL};

	for (size_t index = 0; index < 5; index++) {
		arr[index] = malloc(sizeof(*arr[index]));
		if (arr[index] == NULL) {
			for (size_t cleanup = 0; cleanup < index; cleanup++) {
				free(arr[cleanup]);
			}
			return 1;
		}

		*arr[index] = (int)index;
	}

	for (size_t index = 0; index < 5; index++) {
		printf("%d\n", *arr[index]);
		free(arr[index]);
	}

	return 0;
}
```

この構造では、`arr[index]` は `int *` です。
値そのものを得たいなら `*arr[index]` と 1 回間接参照します。

```c
*(arr + index) = malloc(sizeof(**(arr + index)));
**(arr + index) = (int)index;
```

このようなポインタ記法も等価ですが、かなり読みにくくなります。
重要なのは、**配列の要素がポインタなのか、配列の要素が実データなのか**を見失わないことです。

この構造は、後に出てくるジャグ配列や `argv`、関数ポインタ配列などの理解にもつながります。

### 4章の7　多次元配列とポインタ

多次元配列では、「1 段階添字を進めても、まだ配列である」という点が重要です。

```c
int matrix[2][5] = {
	{1, 2, 3, 4, 5},
	{6, 7, 8, 9, 10}
};
```

このとき各式の見え方は概ね次のようになります。

```text
matrix:
	多くの式で「要素数 5 の int 配列」へのポインタになる

matrix[0]:
	1 行目の配列
	多くの式で int * になる

matrix[0][0]:
	単一の int
```

ポインタとして受けるなら、次のような宣言になります。

```c
int (*pmatrix)[5] = matrix;
```

これは「要素数 5 の `int` 配列へのポインタ」です。
括弧が重要です。

```c
int *wrong[5];
```

これは「`int *` が 5 個入った配列」であり、まったく別物です。

#### 4章の7の1　`matrix + 1` は何を指すか

`matrix` は先頭行を指すポインタとして振る舞います。
したがって `matrix + 1` は 2 番目の `int` を指すのではなく、**次の行**を指します。

```c
#include <stdio.h>

int main(void) {
	int matrix[2][5] = {
		{1, 2, 3, 4, 5},
		{6, 7, 8, 9, 10}
	};

	printf("matrix     = %p\n", (void *)matrix);
	printf("matrix + 1 = %p\n", (void *)(matrix + 1));
	printf("matrix[0] + 1 = %p\n", (void *)(matrix[0] + 1));
	printf("value = %d\n", *(matrix[0] + 1));
	return 0;
}
```

`matrix + 1` は 1 行分だけ進み、`matrix[0] + 1` は 1 要素分だけ進みます。
この違いは、低レイヤのテーブルや固定長バッファ群を扱うときに非常に重要です。

### 4章の8　多次元配列を関数に渡す

多次元配列を関数に渡す場合、関数側は「行の先に何個並んでいるか」を知らなければ正しく添字計算できません。
そのため、最初の次元以外のサイズ指定が必要です。

#### 4章の8の1　列数を固定した受け取り方

```c
#include <stdio.h>

void display_2d_array(size_t rows, const int arr[][5]) {
	for (size_t row = 0; row < rows; row++) {
		for (size_t column = 0; column < 5; column++) {
			printf("%d ", arr[row][column]);
		}
		printf("\n");
	}
}
```

同じ意味をポインタ記法で書くと次の通りです。

```c
void display_2d_array(size_t rows, const int (*arr)[5]) {
	for (size_t row = 0; row < rows; row++) {
		for (size_t column = 0; column < 5; column++) {
			printf("%d ", arr[row][column]);
		}
		printf("\n");
	}
}
```

どちらも「1 行は 5 要素の `int` 配列」という情報を持っています。

誤りやすい宣言は次です。

```c
void wrong(size_t rows, int *arr[5]);
```

これは 2 次元配列を受ける宣言ではなく、「`int *` が 5 個ある配列を受ける」ように見える宣言です。
実際の意味はポインタ引数へ調整されるため、期待する 2 次元固定配列とは一致しません。

#### 4章の8の2　形が実行時に決まる場合

固定列数で受けられない場合、1 次元の連続領域として受けて、自前で添字計算する方法があります。

```c
void display_2d_flat(const int *arr, size_t rows, size_t columns) {
	for (size_t row = 0; row < rows; row++) {
		for (size_t column = 0; column < columns; column++) {
			printf("%d ", *(arr + row * columns + column));
		}
		printf("\n");
	}
}
```

呼び出し側は次のようにします。

```c
int matrix[2][5] = {
	{1, 2, 3, 4, 5},
	{6, 7, 8, 9, 10}
};

display_2d_flat(&matrix[0][0], 2, 5);
```

`matrix` をそのまま渡すと、型が「要素数 5 の `int` 配列へのポインタ」なので、`const int *` とは互換ではありません。
先頭要素のアドレスを明示するために `&matrix[0][0]` を渡す方が筋が通ります。

#### 4章の8の3　3 次元配列を渡す

3 次元でも同じで、先頭次元以外は指定が必要です。

```c
#include <stdio.h>

void display_3d_array(size_t layers, const int (*arr)[2][4]) {
	for (size_t layer = 0; layer < layers; layer++) {
		for (size_t row = 0; row < 2; row++) {
			printf("{");
			for (size_t column = 0; column < 4; column++) {
				printf("%d ", arr[layer][row][column]);
			}
			printf("}");
		}
		printf("\n");
	}
}
```

### 4章の9　2 次元配列の動的割り当て

2 次元配列を動的に扱う方法は 1 つではありません。
どの方法を選ぶかで、メモリが連続か、添字記法が使いやすいか、`free` が何回必要か、コピーしやすいかが変わります。

#### 4章の9の1　行ごとに別々に確保する方法

```c
#include <stdlib.h>

int **allocate_matrix_separate(size_t rows, size_t columns) {
	int **matrix = malloc(rows * sizeof(*matrix));
	if (matrix == NULL) {
		return NULL;
	}

	for (size_t row = 0; row < rows; row++) {
		matrix[row] = malloc(columns * sizeof(*matrix[row]));
		if (matrix[row] == NULL) {
			for (size_t cleanup = 0; cleanup < row; cleanup++) {
				free(matrix[cleanup]);
			}
			free(matrix);
			return NULL;
		}
	}

	return matrix;
}
```

この方法の特徴は次の通りです。

```text
利点:
	matrix[row][column] と自然に書ける
	各行を別々に確保・解放できる

注意点:
	行どうしが連続配置される保証はない
	free が複数回必要
	全体を 1 回の memcpy() で丸ごと扱えない
```

これは `int **` が必要な場面では便利ですが、**本物の 2 次元配列そのものではありません**。
「行ポインタの集まり」です。

#### 4章の9の2　本体だけ連続領域にする方法

行ポインタ配列と、本体となる連続領域を分けて確保する方法もあります。

```c
int **allocate_matrix_contiguous_rows(size_t rows, size_t columns) {
	int **matrix = malloc(rows * sizeof(*matrix));
	if (matrix == NULL) {
		return NULL;
	}

	matrix[0] = malloc(rows * columns * sizeof(*matrix[0]));
	if (matrix[0] == NULL) {
		free(matrix);
		return NULL;
	}

	for (size_t row = 1; row < rows; row++) {
		matrix[row] = matrix[0] + row * columns;
	}

	return matrix;
}
```

この場合、`matrix[row][column]` と書けるうえに、データ本体は連続しています。
ただし `matrix` 自体と `matrix[0]` の確保は別なので、解放も 2 回必要です。

```c
void free_matrix_contiguous_rows(int **matrix) {
	if (matrix == NULL) {
		return;
	}

	free(matrix[0]);
	free(matrix);
}
```

#### 4章の9の3　完全に 1 本の連続領域として扱う方法

もっとも単純にメモリ配置を見るなら、1 次元領域としてまとめて確保する方法があります。

```c
int *matrix = malloc(rows * columns * sizeof(*matrix));
```

この場合、添字は自分で計算します。

```c
for (size_t row = 0; row < rows; row++) {
	for (size_t column = 0; column < columns; column++) {
		matrix[row * columns + column] = (int)(row * column);
	}
}
```

あるいはポインタ記法で次のようにも書けます。

```c
*(matrix + row * columns + column) = (int)(row * column);
```

全体コピーや I/O の都合では有利ですが、`matrix[row][column]` と自然に書けないぶん、意味が見えにくくなることがあります。

#### 4章の9の4　可変列数付きポインタを使う方法

現代の C で比較的きれいなのは、「列数を型に持つポインタ」を使う方法です。

```c
int (*matrix)[columns] = malloc(rows * sizeof(*matrix));
```

これで `matrix[row][column]` と自然に書け、メモリ本体も連続になります。
`sizeof(*matrix)` は「1 行分の大きさ」なので、行数ぶん掛ければ必要量を確保できます。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
	size_t rows = 2;
	size_t columns = 5;
	int (*matrix)[columns] = malloc(rows * sizeof(*matrix));

	if (matrix == NULL) {
		return 1;
	}

	for (size_t row = 0; row < rows; row++) {
		for (size_t column = 0; column < columns; column++) {
			matrix[row][column] = (int)(row * column);
		}
	}

	free(matrix);
	return 0;
}
```

ただし、この書き方は VLA 由来の型を使うため、処理系やコーディング規約によっては避けることがあります。
Linux/GCC/Clang 系では読めるようにしておく価値がありますが、移植性重視の場面では方針確認が必要です。

### 4章の10　ジャグ配列とポインタ

ジャグ配列は、各行の列数が異なる 2 次元風データです。
C では本物の長方形 2 次元配列では表現できないため、通常は「各行へのポインタ」と「各行の長さ」を組み合わせて管理します。

#### 4章の10の1　コンパウンドリテラルによる例

読み取り専用に近い説明用コードなら、コンパウンドリテラルで行ごとの配列を作れます。

```c
int *arr2[] = {
	(int[]){0, 1, 2, 3},
	(int[]){4, 5},
	(int[]){6, 7, 8}
};

size_t arr2_lengths[] = {4, 2, 3};
```

これで `arr2[0]` は 4 要素配列の先頭、`arr2[1]` は 2 要素配列の先頭、`arr2[2]` は 3 要素配列の先頭を指します。

```c
#include <stdio.h>

int main(void) {
	int *arr2[] = {
		(int[]){0, 1, 2, 3},
		(int[]){4, 5},
		(int[]){6, 7, 8}
	};
	size_t lengths[] = {4, 2, 3};

	for (size_t row = 0; row < 3; row++) {
		for (size_t column = 0; column < lengths[row]; column++) {
			printf("arr2[%zu][%zu] = %d\n", row, column, arr2[row][column]);
		}
	}

	return 0;
}
```

ここで行長を別配列で管理している点が重要です。
ジャグ配列は、長方形配列と違って「全行共通の列数」がありません。

#### 4章の10の2　動的ジャグ配列

実用上は、各行を個別に `malloc` して長さ配列を持つ形が一般的です。

```c
typedef struct {
	int **rows;
	size_t *lengths;
	size_t row_count;
} jagged_int_array;
```

この形なら各行の長さが違っていても扱えますが、解放手順も複雑になります。
また、全体を 1 本の連続領域として扱えないため、I/O やバイナリ表現では不利になることがあります。

低レイヤでは「本当にジャグ配列が必要か」をまず疑った方が良いです。
固定長で済むなら長方形の連続配列の方が単純で速く、バグも減ります。

### 4章の11　まとめ

この章では、配列とポインタの関係を、単なる構文対応ではなく、メモリ配置と型の観点から整理しました。
最も重要なのは、**配列名は多くの式で先頭要素へのポインタのように振る舞うが、配列そのものはポインタ変数ではない**、という理解です。

また、1 次元配列では `arr[i]` と `*(arr + i)` が対応し、多次元配列では「1 段階進めてもまだ配列」であるため、
`int **` と `int (*)[N]` がまったく別物になることも確認しました。
この違いを理解すると、関数引数でなぜ列数が必要になるのか、なぜ `matrix + 1` が次の要素ではなく次の行を指すのかが自然に見えてきます。

動的確保については、`malloc` により 1 次元配列相当の連続領域を作れること、`realloc` で安全に拡張・縮小するには一時変数を使うべきこと、
2 次元配列は「行ごとに別確保」「行ポインタ + 連続本体」「完全に 1 本の連続領域」「列数付きポインタ」など複数の設計があることを見ました。

UmuOS や低レイヤのコードを読むときは、次の観点で配列まわりを点検すると理解が進みます。

```text
これは本物の配列か、先頭要素へのポインタか:
	宣言で [] が付いているのか、* が付いているのかをまず見る

要素数はどこで管理されているか:
	固定長か、別引数か、構造体フィールドか、NUL 終端か

メモリは連続か:
	memcpy() や write() でまとめて扱えるかに直結する

多次元の形は何か:
	int ** なのか、int (*)[N] なのかで意味が変わる

寿命と所有権は誰が持つか:
	スタック配列か、静的配列か、malloc() した領域かを区別する
```

配列は見た目が単純な一方で、C の型システム、ポインタ演算、メモリ管理の要点が凝縮された題材です。
この章を土台にしておくと、文字列、構造体配列、I/O バッファ、システムコール引数、テーブル駆動設計の理解がかなり安定します。