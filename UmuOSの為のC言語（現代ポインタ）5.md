---
title: "UmuOSの為のC言語（現代ポインタ）　5章　ポインタと文字列"
---

# UmuOSの為のC言語（現代ポインタ）

このノートは、UmuOSを進化させるために必要となるC言語のポインタ理解を、現代のC17相当の視点で抽象化し、再構成することであります。
すなわち、UmuOSの実装・読解・デバッグへ還元するための実践ノートであります。
ポインタは、単なる文法項目ではなく、メモリ、配列、文字列、関数呼び出し、所有権、未定義動作と深く結びついています。
この構造を理解することは、UmuOSの設計力を高めることに直結すると思います。

## 5章　ポインタと文字列

文字列は C で最も日常的に触るデータの 1 つですが、同時に最も事故を起こしやすい対象でもあります。
原因は単純で、C の文字列は専用の高級オブジェクトではなく、**NUL 文字で終端された `char` の並び**として表現されるからです。
つまり、文字列を安全に扱えるかどうかは、結局のところ「どのメモリを誰が所有しているか」「終端は保証されているか」「どこまで書いてよいか」を正しく追えるかにかかっています。

この章では、文字列リテラル、`char` 配列、`char *`、`const char *` の違いから始めます。
続いて、文字列の初期化、比較、コピー、連結、関数への受け渡し、関数から返す方法、`argv`、そして文字列比較関数を引数に取るソートまで扱います。

重要なのは、古い C の説明にありがちな「文字列定数を `char *` で受けてもだいたい動く」「`scanf("%s", ...)` を気軽に使う」「`sprintf` で十分」といった感覚をそのまま持ち込まないことです。
現代の C17 相当で考えるなら、`const` による契約、境界長の明示、`snprintf`、所有権、未定義動作の回避を常に意識する必要があります。

### 5章の1　文字列の基礎

C のバイト文字列は、`
` ではなく `\0` で終わる `char` 配列です。
したがって「文字列である」と言えるためには、単に `char` が並んでいるだけでなく、**どこかに終端の NUL 文字が存在する**必要があります。

```c
#include <stdio.h>

int main(void) {
	char ok[] = "cat";
	char raw[3] = {'c', 'a', 't'};

	printf("%s\n", ok);
	/* raw は NUL 終端されていないため、文字列として扱ってはいけません。 */
	return 0;
}
```

`ok` は文字列ですが、`raw` は単なる 3 バイトの配列です。
`printf("%s", raw);` のような使い方は未定義動作になります。

#### 5章の1の1　バイト文字列とワイド文字列

古典的な C では「文字列」と言うと `char` 列を指すことが多いですが、標準ライブラリには別系統としてワイド文字列もあります。

```text
バイト文字列:
	char の列
	string.h の関数群で扱う

ワイド文字列:
	wchar_t の列
	wchar.h の関数群で扱う
```

ただし、現代の Linux で UTF-8 を主に扱う文脈では、「国際化したいからとりあえず `wchar_t`」とは限りません。
端末、ファイル、プロトコル、システムコール境界では UTF-8 のバイト列として扱う設計も多いです。
この章では、UmuOS や低レイヤコードで最も頻出する `char` ベースの文字列を中心に扱います。

#### 5章の1の2　NUL と NULL は別物

これは非常によく混同されます。

```text
NUL:
	文字列終端を表す文字
	通常は '\0'

NULL:
	無効なポインタ値を表すマクロ
	ポインタが何も指していないことを示す
```

`'\0'` は文字であり、`NULL` はポインタです。
意味も型も違います。

#### 5章の1の3　文字定数と文字列リテラル

`'A'` と `"A"` は別物です。

```text
'A':
	単一文字の文字定数

"A":
	'A' と '\0' を含む文字列リテラル
```

そのため、次のようなコードは正しくありません。

```c
char *prefix = '+';
```

必要なのが 1 文字だけでも、文字列として使いたいなら終端が必要です。

```c
char prefix[] = "+";
```

あるいは動的に作るなら次のようにします。

```c
#include <stdlib.h>

char *make_plus_prefix(void) {
	char *prefix = malloc(2);
	if (prefix == NULL) {
		return NULL;
	}

	prefix[0] = '+';
	prefix[1] = '\0';
	return prefix;
}
```

### 5章の2　文字列の宣言と配置

同じ見た目の文字列でも、どこに置かれているかで扱い方は変わります。
ここでは、文字列リテラル、配列、ポインタ、動的確保の違いを整理します。

#### 5章の2の1　`char` 配列として持つ

```c
char header[] = "Media Player";
```

これは文字列リテラルの内容を配列へコピーして保持します。
`header` は書き換え可能な配列であり、サイズは終端を含めて自動決定されます。

```c
#include <stdio.h>

int main(void) {
	char header[] = "Media Player";

	header[0] = 'm';
	printf("%s\n", header);
	printf("bytes=%zu\n", sizeof(header));
	return 0;
}
```

この場合の `sizeof(header)` は配列全体のバイト数です。

#### 5章の2の2　`const char *` として文字列リテラルを指す

```c
const char *header = "Media Player";
```

ここで `header` 自体はポインタ変数です。
したがって別の文字列を指すよう再代入できますが、指している先を書き換えてはいけません。

```c
const char *header = "Media Player";
header = "Audio Mixer";   /* これは可能 */
/* header[0] = 'm'; */     /* これは不可 */
```

現代の C では、文字列リテラルは**読み取り専用のものとして扱う**のが前提です。
処理系によっては書き換えで即座に異常終了しますし、書き換え可能に見える環境でも未定義動作です。
そのため、文字列リテラルを受ける変数は `const char *` にするべきです。

#### 5章の2の3　文字列リテラルの共有とリテラルプール

多くの処理系では、同じ内容の文字列リテラルが内部で共有されることがあります。
昔の説明ではリテラルプールという言い方をよくします。
ただし、これは**実装上の最適化**であって、同じリテラルが必ず同じアドレスになると期待してはいけません。

したがって次のような比較は避けるべきです。

```c
const char *a = "Quit";
const char *b = "Quit";

if (a == b) {
	/* 内容が同じことの判定には使えません。 */
}
```

文字列内容を比較したいなら `strcmp` を使います。

#### 5章の2の4　どこに置かれるか

同じ文字列でも、実体の配置場所は宣言の仕方で変わります。

```c
#include <stdlib.h>
#include <string.h>

const char *global_header = "Chapter";
char global_array_header[] = "Chapter";

void display_header(void) {
	static const char *static_header = "Chapter";
	static char static_array_header[] = "Chapter";
	const char *local_header = "Chapter";
	char local_array_header[] = "Chapter";
	char *heap_header = malloc(strlen("Chapter") + 1);

	if (heap_header != NULL) {
		strcpy(heap_header, "Chapter");
		free(heap_header);
	}

	(void)static_header;
	(void)static_array_header;
	(void)local_header;
	(void)local_array_header;
}
```

ここで見分けるべきなのは次の点です。

```text
文字列リテラル:
	プログラム中に埋め込まれた読み取り専用相当の文字列

配列として持つ文字列:
	その配列オブジェクト自身が文字列本体を持つ

ポインタ変数:
	文字列そのものではなく、どこかの文字列を指すだけ

ヒープ文字列:
	malloc() で確保され、free() まで生存する
```

### 5章の3　文字列の初期化

#### 5章の3の1　配列の初期化

配列なら、最も自然なのは文字列リテラルによる初期化です。

```c
char header[] = "Media Player";
char fixed[13] = "Media Player";
```

いずれも終端の `\0` を含めて格納されます。
後者では、サイズ指定が不足すると終端が入らない場合があるので注意が必要です。

```c
char broken[3] = "cat";   /* 終端が入らない */
```

これは 3 文字の配列であり、文字列として安全に扱えません。

#### 5章の3の2　ポインタの初期化

ポインタを文字列へ向ける方法は 2 通りあります。

```text
文字列リテラルを指す:
	const char *header = "Media Player";

動的に確保してコピーする:
	char *header = malloc(...);
```

後者の安全な書き方は次のようになります。

```c
#include <stdlib.h>
#include <string.h>

char *copy_media_player(void) {
	const char *source = "Media Player";
	char *header = malloc(strlen(source) + 1);

	if (header == NULL) {
		return NULL;
	}

	strcpy(header, source);
	return header;
}
```

ここで `header` は書き換え可能なヒープ文字列です。
使い終わったら呼び出し側が `free` する必要があります。

#### 5章の3の3　入力で初期化する

古い例では `scanf("%s", command)` がよく出ますが、そのままでは危険です。
入力長を制限しないからです。

```c
char command[16];
scanf("%15s", command);
```

このように幅指定を入れれば配列境界を超えにくくなりますが、空白を含む入力や切り詰め検出の扱いはまだ不十分です。
現代的には `fgets` を使って行単位で読み、その後で改行除去や解析を行う方が安全です。

```c
#include <stdio.h>
#include <string.h>

int read_command(char *buffer, size_t size) {
	if (fgets(buffer, size, stdin) == NULL) {
		return 0;
	}

	buffer[strcspn(buffer, "\n")] = '\0';
	return 1;
}
```

入力長が不定なら、4章で扱ったような `realloc` ベースの可変バッファを使う方が筋が良いです。

### 5章の4　標準的な文字列操作

#### 5章の4の1　文字列の比較

文字列内容の比較に `==` は使えません。
比較しているのはアドレスであって中身ではないからです。

```c
#include <stdio.h>
#include <string.h>

int main(void) {
	char command[16] = "Quit";

	if (strcmp(command, "Quit") == 0) {
		printf("The command was Quit\n");
	}

	return 0;
}
```

誤りの典型は次の 2 つです。

```c
if (command == "Quit") {
	/* アドレス比較になってしまう */
}

if (command = "Quit") {
	/* 代入であり、しかも配列には代入できない */
}
```

`strcmp` の戻り値は、単に等しいかどうかだけではなく、辞書順の前後関係も表します。
そのためソートにも使えます。

#### 5章の4の2　文字列のコピー

文字列コピーでは、宛先に十分な大きさがあることが前提です。

```c
#include <stdlib.h>
#include <string.h>

char *duplicate_name(const char *name) {
	char *copy = malloc(strlen(name) + 1);
	if (copy == NULL) {
		return NULL;
	}

	strcpy(copy, name);
	return copy;
}
```

これは本章全体で繰り返し使う基本形です。

```text
1. strlen() で必要長を測る
2. +1 して NUL 終端ぶんを確保する
3. NULL を確認する
4. strcpy() などでコピーする
```

複数の名前を保持するなら、ポインタ配列と組み合わせる形になります。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
	char input[32] = "Sam";
	char *names[30] = {NULL};
	size_t count = 0;

	names[count] = malloc(strlen(input) + 1);
	if (names[count] == NULL) {
		return 1;
	}

	strcpy(names[count], input);
	count++;

	printf("%s\n", names[0]);
	free(names[0]);
	return 0;
}
```

ここでコピーされるのは**文字列内容**であり、単なるポインタ代入とは違います。

#### 5章の4の3　ポインタのコピーと別名

```c
const char *page_headers[300] = {NULL};

page_headers[12] = "Amorphous Compounds";
page_headers[13] = page_headers[12];
```

このとき、`page_headers[12]` と `page_headers[13]` は同じ文字列を指しています。
文字列内容は複製されていません。
これは別名の一種です。

ヒープ文字列でも同じで、ポインタだけ複製して 2 回 `free` すると二重解放になります。
文字列をコピーしたいのか、参照を共有したいのかは明確に分ける必要があります。

#### 5章の4の4　文字列の連結

連結で重要なのは、結果を書き込む先に十分な容量があることです。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
	const char *prefix = "ERROR: ";
	const char *message = "Not enough memory";
	size_t length = strlen(prefix) + strlen(message) + 1;
	char *buffer = malloc(length);

	if (buffer == NULL) {
		return 1;
	}

	strcpy(buffer, prefix);
	strcat(buffer, message);

	printf("%s\n", buffer);
	free(buffer);
	return 0;
}
```

文字列リテラルに対して `strcat` してはいけません。

```c
const char *prefix = "ERROR: ";
const char *message = "Not enough memory";

/* strcat(prefix, message); */
```

これは読み取り専用領域を変更しようとするため不正です。

また、スタック上の固定配列に連結する場合でも、容量不足なら同じく壊れます。
現代的には `snprintf` でまとめて組み立てる方が安全で意図も明確です。

```c
char buffer[64];
snprintf(buffer, sizeof(buffer), "%s%s", prefix, message);
```

#### 5章の4の5　1 文字連結の落とし穴

次はよくある誤りです。

```c
/* strcat(path, '\\'); */
```

`strcat` の第 2 引数は文字列です。
`'\\'` は単一文字であり、文字列ポインタではありません。

1 文字だけ足したいなら、最初からフォーマットで組む方が単純です。

```c
char current_path[64];
snprintf(current_path, sizeof(current_path), "%s\\", "C:");
```

### 5章の5　関数へ文字列を渡す

文字列を関数へ渡すとき、最も一般的な型は `const char *` です。
関数が読み取り専用なら、これで契約が明確になります。

#### 5章の5の1　長さを数える関数

```c
#include <stddef.h>

size_t string_length(const char *string) {
	size_t length = 0;

	while (*string != '\0') {
		length++;
		string++;
	}

	return length;
}
```

`while (*(string++))` という古典的な書き方もありますが、学習段階では上のように分けた方が読みやすいです。
低レイヤを読むときには両方の形を読める必要があります。

呼び出しは次のようになります。

```c
char simple_array[] = "simple string";
const char *simple_ptr = "simple string";

size_t a = string_length(simple_array);
size_t b = string_length(simple_ptr);
size_t c = string_length(&simple_array[0]);
```

`&simple_array` は型が `char (*)[14]` のようになるため、`const char *` を受ける関数へはそのまま渡すべきではありません。
見た目が似ていても型は違います。

#### 5章の5の2　`const` を付ける意味

```c
size_t string_length(const char *string) {
	/* *string = 'A'; */
	return 0;
}
```

このように、書き換えを禁止したいとき `const` は重要です。
文字列リテラルも安全に渡しやすくなります。

#### 5章の5の3　呼び出し側バッファへ書き込む

出力文字列を作りたい関数では、呼び出し側がバッファを渡す設計が堅実です。

```c
#include <stdio.h>

char *format_item(char *buffer,
			size_t size,
			const char *name,
			size_t quantity,
			size_t weight) {
	snprintf(buffer,
		 size,
		 "Item: %s  Quantity: %zu  Weight: %zu",
		 name,
		 quantity,
		 weight);
	return buffer;
}
```

この設計では、呼び出し側が領域の確保と解放を管理します。

```c
char buffer[128];
printf("%s\n", format_item(buffer, sizeof(buffer), "Axle", 25, 45));
```

#### 5章の5の4　必要なら関数内で確保する

呼び出し側が大きさを事前に決めにくいなら、関数側で動的確保して返す設計もあります。

```c
#include <stdio.h>
#include <stdlib.h>

char *format_item_alloc(const char *name, size_t quantity, size_t weight) {
	int needed = snprintf(NULL,
			      0,
			      "Item: %s  Quantity: %zu  Weight: %zu",
			      name,
			      quantity,
			      weight);
	if (needed < 0) {
		return NULL;
	}

	char *buffer = malloc((size_t)needed + 1);
	if (buffer == NULL) {
		return NULL;
	}

	snprintf(buffer,
		 (size_t)needed + 1,
		 "Item: %s  Quantity: %zu  Weight: %zu",
		 name,
		 quantity,
		 weight);
	return buffer;
}
```

このときの所有権は呼び出し側へ移ります。
使い終わったら `free` が必要です。

### 5章の6　プログラムにパラメータを渡す

`main` の `argv` は、文字列ポインタの配列として理解できます。

```c
#include <stdio.h>

int main(int argc, char *argv[]) {
	for (int index = 0; index < argc; index++) {
		printf("argv[%d] = %s\n", index, argv[index]);
	}

	return 0;
}
```

これは次と同義です。

```c
int main(int argc, char **argv) {
	return argc + (argv != NULL ? 0 : 0);
}
```

ただし、実用上は `char *argv[]` の方が「配列として使う」意図が見えやすいことがあります。

`argv` はしばしば「文字列の配列の先頭」だと説明されますが、正確には文字列を指すポインタ群です。
そのため、`argv[i]` は 1 個の文字列、`argv[i][j]` はその文字列の j 文字目という見方ができます。

### 5章の7　関数から文字列を返す

関数から返せるのは文字列そのものではなく、その先頭アドレスです。
ここで重要なのは、**返した先がまだ有効か**です。

#### 5章の7の1　文字列リテラルを返す

文字列リテラルを返すのは安全です。
ただし戻り値型は `const char *` にするべきです。

```c
const char *processing_center_name(int code) {
	switch (code) {
	case 100:
		return "Boston Processing Center";
	case 200:
		return "Denver Processing Center";
	case 300:
		return "Atlanta Processing Center";
	case 400:
		return "San Jose Processing Center";
	default:
		return "Unknown Processing Center";
	}
}
```

#### 5章の7の2　静的配列を返す

静的配列を返すこと自体は可能です。
ただし、1 つの共有バッファになる点に注意が必要です。

```c
#include <stdio.h>

const char *static_format_item(const char *name, size_t quantity, size_t weight) {
	static char buffer[64];

	snprintf(buffer,
		 sizeof(buffer),
		 "Item: %s  Quantity: %zu  Weight: %zu",
		 name,
		 quantity,
		 weight);
	return buffer;
}
```

この関数は連続して呼ぶと前回結果が上書きされます。
再入不能であり、スレッド安全でもありません。
低レイヤの補助関数として使うなら、用途をかなり限定する必要があります。

#### 5章の7の3　動的確保した文字列を返す

こちらは柔軟性が高い方法です。

```c
#include <stdlib.h>

char *blanks(size_t count) {
	char *spaces = malloc(count + 1);
	if (spaces == NULL) {
		return NULL;
	}

	for (size_t index = 0; index < count; index++) {
		spaces[index] = ' ';
	}

	spaces[count] = '\0';
	return spaces;
}
```

呼び出し側は次のように扱います。

```c
char *tmp = blanks(5);
if (tmp != NULL) {
	printf("[%s]\n", tmp);
	free(tmp);
}
```

次のように戻り値をそのまま使って捨てると、解放機会を失います。

```c
/* printf("[%s]\n", blanks(5)); */
```

短い例では見落としやすいですが、これはメモリリークです。

#### 5章の7の4　局所配列を返してはいけない

これは典型的な誤りです。

```c
char *broken_blanks(size_t count) {
	char spaces[32];
	size_t limit = count < 31 ? count : 31;

	for (size_t index = 0; index < limit; index++) {
		spaces[index] = ' ';
	}
	spaces[limit] = '\0';
	return spaces;
}
```

`spaces` は自動記憶域期間の局所配列です。
関数を抜けた時点で寿命が終わるため、そのアドレスを返して使うのは未定義動作です。

### 5章の8　関数ポインタと文字列

3章で扱った関数ポインタは、文字列処理でも有用です。
特に比較関数を切り替えてソート方針を変更する設計は、C らしい柔軟な書き方です。

#### 5章の8の1　大文字小文字を区別する比較

```c
#include <string.h>

int compare_case_sensitive(const char *left, const char *right) {
	return strcmp(left, right);
}
```

#### 5章の8の2　大文字小文字を無視する比較

文字列を小文字化して比較する例は次のように書けます。

```c
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *string_to_lower_copy(const char *string) {
	size_t length = strlen(string);
	char *copy = malloc(length + 1);
	if (copy == NULL) {
		return NULL;
	}

	for (size_t index = 0; index < length; index++) {
		copy[index] = (char)tolower((unsigned char)string[index]);
	}
	copy[length] = '\0';
	return copy;
}

int compare_ignore_case(const char *left, const char *right) {
	char *lower_left = string_to_lower_copy(left);
	char *lower_right = string_to_lower_copy(right);
	int result;

	if (lower_left == NULL || lower_right == NULL) {
		free(lower_left);
		free(lower_right);
		return strcmp(left, right);
	}

	result = strcmp(lower_left, lower_right);
	free(lower_left);
	free(lower_right);
	return result;
}
```

`tolower` に渡す値は `unsigned char` へ変換してから使うのが重要です。
負の `char` 値をそのまま渡すと未定義動作になることがあります。

#### 5章の8の3　比較関数を受け取るソート

```c
#include <stdio.h>

typedef int (*string_compare_fn)(const char *, const char *);

void sort_strings(char *array[], size_t size, string_compare_fn compare) {
	int swapped = 1;

	while (swapped) {
		swapped = 0;
		for (size_t index = 0; index + 1 < size; index++) {
			if (compare(array[index], array[index + 1]) > 0) {
				char *tmp = array[index];
				array[index] = array[index + 1];
				array[index + 1] = tmp;
				swapped = 1;
			}
		}
	}
}

void display_names(char *const names[], size_t size) {
	for (size_t index = 0; index < size; index++) {
		printf("%s   ", names[index]);
	}
	printf("\n");
}
```

使用例は次の通りです。

```c
char *names[] = {"Bob", "Ted", "Carol", "Alice", "alice"};

sort_strings(names, 5, compare_case_sensitive);
display_names(names, 5);

sort_strings(names, 5, compare_ignore_case);
display_names(names, 5);
```

実運用では標準ライブラリの `qsort` を使うことも多いですが、ここでは関数ポインタの考え方を明確にするため、単純なソートで構造を見せています。

### 5章の9　まとめ

この章では、C の文字列が高級な専用型ではなく、NUL 終端された `char` 列として表現されることを出発点にして、ポインタとの関係を整理しました。
特に重要なのは、文字列リテラル、`char` 配列、`char *`、`const char *`、ヒープ文字列がそれぞれ別の性質を持つことです。
見た目が似ていても、書き換え可能か、サイズをその場で知れるか、誰が解放責任を持つかは一致しません。

文字列操作では、比較に `strcmp` を使うこと、コピーや連結の前に十分な容量を確保すること、`snprintf` のような境界付き API を優先すること、
そして関数へ文字列を渡すときには `const char *` を基本にすることが重要です。
また、関数から文字列を返す際には、文字列リテラル、静的バッファ、動的確保文字列の違いを理解し、局所配列のアドレスを返してはいけないことも確認しました。

最後に、UmuOS や低レイヤの観点で、文字列まわりを見るときの着眼点をまとめます。

```text
その文字列はどこにあるか:
	リテラル、スタック配列、静的配列、ヒープのどれかをまず区別する

書き換えてよいか:
	const char * なのか、char * なのか、実体が読み取り専用かを確認する

長さはどう管理されているか:
	NUL 終端任せか、別途サイズを持つか、固定長バッファかを見る

境界超過の可能性はないか:
	strcpy()、strcat()、scanf("%s", ...) のような箇所は特に注意する

所有権は誰にあるか:
	malloc() した文字列を誰が free() するかを追跡する

比較しているのは中身かアドレスか:
	== ではなく strcmp() 系で比較すべき場面を見極める

一時バッファの寿命は十分か:
	関数ローカル配列を返していないか、静的バッファの使い回しで破綻しないかを見る
```

UmuOS のシェル、コマンド解析、設定値、パス処理、ログ出力、システムコールへ渡す引数列では、文字列はほぼ常に登場します。
そのたびに「この `char *` はどこを指しているのか」「終端は保証されているか」「今この関数は読むだけか書くのか」「解放責任はどこか」を追えるようになると、
低レイヤのコード読解力はかなり安定します。