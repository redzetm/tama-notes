---
title: "UmuOSの為のC言語（現代ポインタ）　7章　セキュリティの問題と不適切なポインタの使用"
---

# UmuOSの為のC言語（現代ポインタ）

このノートは、UmuOSを進化させるために必要となるC言語のポインタ理解を、現代のC17相当の視点で抽象化し、再構成することであります。
すなわち、UmuOSの実装・読解・デバッグへ還元するための実践ノートであります。
ポインタは、単なる文法項目ではなく、メモリ、配列、文字列、関数呼び出し、所有権、未定義動作と深く結びついています。
この構造を理解することは、UmuOSの設計力を高めることに直結すると思います。

## 7章　セキュリティの問題と不適切なポインタの使用

セキュリティや信頼性を気にしなくてよい C プログラムは、実際にはかなり少数です。
たとえ学習用の小さなプログラムであっても、配列範囲外アクセス、未初期化ポインタ、解放後使用、書式文字列の誤用のような問題は、
そのまま本番コードで重大事故につながる種類の欠陥です。

C は低レイヤを直接扱える強力な言語ですが、その分だけ安全性の大部分を言語処理系ではなく開発者自身が担います。
配列境界の自動検査は基本的にありませんし、ポインタが本当に有効なオブジェクトを指しているかをランタイムが面倒見てくれるわけでもありません。
したがって、C のセキュリティ問題は「特別な防御技術の話」というより、**普段のポインタ操作をどれだけ厳密に扱うか**の延長にあります。

この章では、ポインタ宣言と初期化の落とし穴、配列境界や `sizeof` の誤用、文字列処理、構造体と関数ポインタの誤った扱い、
解放後のポインタ管理、機密データの消去、そしてコンパイラ警告や静的解析の活用までを扱います。
また最後に、UmuOS や低レイヤ実装の観点から、どこを重点的に観察すべきかも整理します。

### 7章の1　ポインタの宣言と初期化

ポインタのバグは、派手なアルゴリズム部分ではなく、宣言や初期化のような地味な箇所で入り込むことが少なくありません。
特に「見た目では正しそうだが、実際は違う」コードに注意が必要です。

#### 7章の1の1　ポインタ宣言の誤読を避ける

次の宣言を見てください。

```c
int *ptr1, ptr2;
```

これは `ptr1` が `int *`、`ptr2` が `int` です。
人間の目には両方ポインタのように見えることがありますが、`*` は型全体ではなく**各宣言子**に結びつきます。

両方をポインタにしたいなら、次のように書く必要があります。

```c
int *ptr1, *ptr2;
```

ただし、実務では 1 行 1 変数の方が事故を減らせます。

```c
int *ptr1;
int *ptr2;
```

この方がレビュー時にも読み違えにくく、型変更にも強いです。

#### 7章の1の2　マクロより typedef を優先する

次のようなマクロは一見便利ですが、危険です。

```c
#define PINT int *

PINT ptr1, ptr2;
```

これも実際には次のように展開されます。

```c
int *ptr1, ptr2;
```

したがって `ptr2` はポインタではありません。

型の別名が必要なら `typedef` を使います。

```c
typedef int *IntPtr;

IntPtr ptr1;
IntPtr ptr2;
```

`typedef` はプリプロセッサの単純置換ではなく、型として扱われるので読みやすさと保守性が上がります。

#### 7章の1の3　未初期化ポインタは即座に危険になる

```c
#include <stdio.h>

void print_value(void) {
	int *ptr;
	printf("%d\n", *ptr);
}
```

この `ptr` はどこも指していません。
自動変数の未初期化値は不定であり、そこを間接参照すると未定義動作です。
セグメンテーションフォールトになることもあれば、たまたま読めてしまって余計に発見が遅れることもあります。

#### 7章の1の4　基本方針は NULL 初期化

ポインタは、すぐ有効なアドレスを入れられないなら `NULL` で初期化するのが基本です。

```c
int *ptr = NULL;

if (ptr != NULL) {
	printf("%d\n", *ptr);
}
```

これで「まだ有効なオブジェクトを指していない」という状態を明示できます。
もちろん `NULL` チェックだけで全問題が消えるわけではありませんが、未初期化の不定値よりはるかに扱いやすいです。

#### 7章の1の5　assert は契約の破れを早く見つける

```c
#include <assert.h>

void store_value(int *ptr, int value) {
	assert(ptr != NULL);
	*ptr = value;
}
```

`assert` はデバッグ時に「ここへ来る時点で `ptr` は有効であるはず」という前提を確認する手段です。
ただし `NDEBUG` 定義時は無効になるので、公開入力の検証や本番防御を `assert` だけに任せてはいけません。

### 7章の2　ポインタ使用時の問題

ポインタの誤用は、配列境界違反、型不整合、解放後使用、文字列破壊、関数呼び出しの破壊など、さまざまな形で現れます。
その多くはバッファオーバーフローと未定義動作に帰着します。

#### 7章の2の1　確保直後の NULL を必ず確認する

```c
#include <stdlib.h>

float *vector = malloc(20 * sizeof(*vector));
if (vector == NULL) {
	/* メモリ確保失敗 */
} else {
	/* vector を使う */
	free(vector);
}
```

メモリ確保関数の戻り値を無条件で使うのは危険です。
特に低メモリ環境や異常系テストでは、失敗経路を現実に通ります。

#### 7章の2の2　間接演算子の誤用

正しい初期化は次の通りです。

```c
int number = 0;
int *ptr = &number;
```

次は誤りです。

```c
int number = 0;
int *ptr;
*ptr = &number;
```

最後の行では `ptr` 自体へアドレスを入れるのではなく、`ptr` が指している先へ `&number` を書こうとしています。
しかも `ptr` は未初期化です。これは典型的な破壊です。

#### 7章の2の3　ぶら下がりポインタ

```c
#include <stdlib.h>

int *make_dangling(void) {
	int *buffer = malloc(sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}

	*buffer = 42;
	free(buffer);
	return buffer;
}
```

`free` 後にそのアドレスを保持し続けると、ぶら下がりポインタになります。
見た目は非 `NULL` でも、参照先はもはや自分のものではありません。
解放後使用は、最近の実運用でも依然として重大脆弱性の常連です。

#### 7章の2の4　配列境界外アクセス

```c
#include <stdio.h>

void corrupt_arrays(void) {
	char first_name[8] = "1234567";
	char middle_name[8] = "1234567";
	char last_name[8] = "1234567";

	middle_name[0] = 'X';
	/* ここから先は未定義動作の例であり、実運用コードで行ってはいけない */
	middle_name[-2] = 'X';
	middle_name[10] = 'X';

	printf("%s\n", first_name);
	printf("%s\n", middle_name);
	printf("%s\n", last_name);
}
```

C は `array[index]` が有効範囲内かを自動検査しません。
負の添字も大きすぎる添字も、そのまま別メモリ破壊になりえます。

#### 7章の2の5　配列長を渡す設計は重要

文字列や配列を受け取る関数は、可能なら長さも一緒に受け取るべきです。

```c
#include <stddef.h>

void replace(char buffer[], char replacement, size_t size) {
	size_t index = 0;

	while (index < size && buffer[index] != '\0') {
		buffer[index] = replacement;
		index++;
	}
}
```

呼び出し側では、コピー元の長さだけでなく、コピー先の容量が本当に足りるかを意識する必要があります。

```c
#include <stdio.h>
#include <string.h>

void demo_replace(void) {
	char name[8] = "Alex";
	replace(name, '+', sizeof(name));
	printf("%s\n", name);

	/* こちらは危険な例: 容量 8 に 9 文字 + 終端は入らない */
	/* strcpy(name, "Alexander"); */
}
```

現代的には、危険例をそのまま実行するより「なぜ危険か」を明示し、`snprintf` など容量を知る API を優先する方がよいです。

#### 7章の2の6　sizeof の誤用

次のコードは誤りです。

```c
int buffer[20];
int *ptr = buffer;

for (size_t index = 0; index < sizeof(buffer); index++) {
	*(ptr++) = 0;
}
```

`sizeof(buffer)` は要素数ではなく**バイト数**です。
`int` が 4 バイトなら 80 回ループしてしまいます。

正しくは次のように書きます。

```c
for (size_t index = 0; index < sizeof(buffer) / sizeof(buffer[0]); index++) {
	buffer[index] = 0;
}
```

さらに単純に、全ゼロ初期化なら `memset` も候補です。

```c
#include <string.h>

memset(buffer, 0, sizeof(buffer));
```

ただし `memset` は「ゼロにする」用途には向きますが、任意の整数値の配列初期化には使えません。

#### 7章の2の7　ポインタ型は合わせる

```c
#include <stdio.h>

void type_mismatch_demo(void) {
	int number = 2147483647;
	int *int_ptr = &number;
	short *short_ptr = (short *)int_ptr;

	printf("int_ptr:   %p value=%d\n", (void *)int_ptr, *int_ptr);
	printf("short_ptr: %p value=%hd\n", (void *)short_ptr, *short_ptr);
}
```

異なる型で同じメモリを読むと、エンディアン、サイズ、アラインメント、エイリアシング規則の影響を受けます。
「たまたま期待値が出た」からといって正しいとは限りません。
特に strict aliasing を破るアクセスは最適化下で深刻な不具合になります。

#### 7章の2の8　文字列処理は容量中心で考える

古い資料では `strcpy` や `strcat` が多用されますが、現代の C では安全側へ寄せて考えるべきです。
標準 C で広く使える実用的な選択肢は `snprintf` です。

```c
#include <stdio.h>

int build_path(char *buffer, size_t size, const char *dir, const char *file) {
	int written = snprintf(buffer, size, "%s/%s", dir, file);
	if (written < 0) {
		return 0;
	}

	if ((size_t)written >= size) {
		return 0;
	}

	return 1;
}
```

入力関数についても `gets` は既に標準から削除されています。
文字列入力には、長さを指定できる `fgets` を使うのが基本です。

```c
#include <stdio.h>

char line[128];
if (fgets(line, sizeof(line), stdin) != NULL) {
	/* line を使う */
}
```

#### 7章の2の9　書式文字列攻撃

次のコードは危険です。

```c
#include <stdio.h>

int main(int argc, char *argv[]) {
	if (argc > 1) {
		printf(argv[1]);
	}
	return 0;
}
```

利用者入力をそのまま書式文字列として渡しているため、`%x` や `%n` を悪用される余地があります。

安全側は次です。

```c
printf("%s", argv[1]);
```

`printf`、`fprintf`、`snprintf`、`syslog` など「書式文字列を取る関数」すべてで同じ発想が必要です。

#### 7章の2の10　構造体内部にポインタ演算を持ち込まない

構造体メンバに対して、連続配置を勝手に仮定してポインタ演算するのは危険です。

```c
typedef struct employee {
	char name[10];
	int age;
} Employee;
```

次のようなコードは避けるべきです。

```c
Employee employee = {"Alice", 30};
char *ptr = employee.name;
ptr += sizeof(employee.name);
```

この `ptr` は `age` を正しく指す保証がありません。
パディングが入る可能性があるからです。

構造体メンバには素直にメンバ名でアクセスします。

```c
printf("%d\n", employee.age);
```

あるいは、必要なら対象メンバのアドレスを明示的に取ります。

```c
int *age_ptr = &employee.age;
printf("%d\n", *age_ptr);
```

#### 7章の2の11　関数ポインタの誤用

関数呼び出しの括弧忘れは、見た目以上に危険です。

```c
int get_system_status(void) {
	return 0;
}
```

正しい判定は次です。

```c
if (get_system_status() == 0) {
	puts("Status is 0");
}
```

次は誤りです。

```c
if (get_system_status == 0) {
	puts("Status is 0");
}
```

これは戻り値ではなく、関数アドレスと 0 を比較しています。

さらに次も誤りです。

```c
if (get_system_status) {
	puts("always true");
}
```

関数名はポインタへ変換されるので、この条件は常に真になります。

シグネチャ不一致の関数ポインタも未定義動作です。

```c
int add_three(int left, int middle, int right) {
	return left + middle + right;
}

int (*compute)(int, int);
compute = (int (*)(int, int))add_three;
```

このようなキャストは、警告を黙らせても安全にはなりません。
型を正しく一致させるべきです。

### 7章の3　メモリ解放に関わる問題

`free` を呼んだから終わり、ではありません。
解放後のポインタ管理と、そこに入っていたデータの扱いまで考える必要があります。

#### 7章の3の1　二重解放

```c
#include <stdlib.h>

void broken_free(void) {
	char *name = malloc(32);
	if (name == NULL) {
		return;
	}

	free(name);
	free(name);
}
```

二重解放は未定義動作であり、運が悪ければヒープ管理情報を壊します。

最低限の対策として、解放後に `NULL` を代入します。

```c
free(name);
name = NULL;
```

ただし本質は「所有権の重複」を設計段階で防ぐことです。
複数のポインタが同じ確保領域を共有しているなら、誰が 1 回だけ解放するのかを決めておかなければなりません。

#### 7章の3の2　重要データのクリア

パスワード、トークン、秘密鍵断片、認証質問、セッション情報などを保持したメモリは、不要になったら消去を検討すべきです。

```c
#include <string.h>

void clear_sensitive_stack_data(void) {
	char password[32] = "example-password";
	unsigned int user_id = 1001;

	memset(password, 0, sizeof(password));
	user_id = 0;
}
```

ヒープ領域でも同様です。

```c
void clear_and_free(char *buffer, size_t size) {
	if (buffer == NULL) {
		return;
	}

	memset(buffer, 0, size);
	free(buffer);
}
```

ただし最適化により `memset` が消される可能性がある点には注意が必要です。
本当に消去保証が必要な場面では、環境が提供する専用 API を優先します。
Linux/glibc では環境差がありますが、利用可能なら `explicit_bzero`、他環境なら `memset_s` などを検討します。

### 7章の4　静的解析ツールを使う

コンパイラ警告と静的解析は、ポインタの危険な使い方を早期に炙り出す重要な防波堤です。
「コンパイルが通ったから安全」という発想は捨てた方がよいです。

#### 7章の4の1　警告を増やす

GCC や Clang なら、少なくとも次を基本にするとよいです。

```sh
cc -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wpointer-arith source.c
```

プロジェクトによってはさらに `-Werror` を加え、警告を放置できないようにすることもあります。

#### 7章の4の2　サニタイザを使う

静的解析だけでなく、実行時検査も非常に有効です。

```sh
cc -std=c17 -Wall -Wextra -fsanitize=address,undefined -g source.c
```

AddressSanitizer は解放後使用、境界外アクセス、二重解放などを見つけやすく、
UndefinedBehaviorSanitizer は規格上危険な操作を炙り出しやすいです。

#### 7章の4の3　典型警告を読めるようにする

たとえば次のようなコードです。

```c
if (get_system_status) {
	puts("always true");
}
```

これには「関数アドレスは常に真と評価される」系の警告が出ます。

また、未初期化ポインタ使用では次のような診断が出ます。

```c
char *security_question;
strcpy(security_question, "Name of your home town");
```

この種の診断を見たら「コンパイラがうるさい」ではなく、「未定義動作の入口を示してくれている」と受け止めるべきです。

### 7章の5　まとめ

この章では、ポインタの誤用がセキュリティと信頼性にどのように直結するかを見てきました。
重要なのは、危険なバグの多くが特殊な攻撃コードではなく、普段の C コードにある小さな油断から生まれるという点です。

未初期化ポインタ、`NULL` 未確認、解放後使用、二重解放、配列境界外アクセス、`sizeof` の誤用、文字列関数の不注意な利用、
関数ポインタのシグネチャ不一致、書式文字列の誤用は、どれも低レイヤ実装で現実に起こります。
そしてそれらは、クラッシュだけでなく、権限逸脱、情報漏えい、DoS、制御フロー破壊へつながる可能性があります。

現代の C17 相当の実務感覚では、次の方針が基本になります。

```text
ポインタは未初期化のまま使わず、まず NULL か有効アドレスにする:
	不定値を持ったポインタより、意味のある無効値の方がずっと扱いやすい

配列や文字列には必ず容量の概念を持ち込む:
	長さを一緒に渡し、snprintf や fgets のような API を優先する

free の前後を 1 つの操作として考える:
	解放後に NULL 化し、所有権の所在を明確にする

構造体・関数・型変換では「たまたま動く」を信用しない:
	パディング、型不整合、シグネチャ不一致は未定義動作の温床になる

警告と解析ツールを常用する:
	Wall、Wextra、サニタイザ、静的解析は学習時点から使う
```

### 7章の6　UmuOS の観点

UmuOS やその周辺の低レイヤコードを読むときは、セキュリティ問題を「攻撃手法の知識」としてではなく、
**ポインタ設計の破綻がどこで起きるかを見抜く視点**として使うと効果的です。

```text
バッファと長さが常に対になっているか見る:
	read、write、getline 風処理、コマンド解析、行編集では、ポインタ単体より容量情報の有無が重要

所有権の移動点を追う:
	malloc した領域を誰が free するのか、途中で別ポインタへ渡した後も責任主体が曖昧でないか確認する

解放後も参照が残っていないか見る:
	リスト削除、履歴破棄、トークン列の再構築、再読込処理では dangling pointer が入りやすい

構造体メンバ更新の順序を見る:
	head、tail、next、prev、buffer、size をどの順序で更新しているかを追うと、破綻点が見えやすい

文字列入力と出力の境界を見る:
	固定長配列へコピーしていないか、printf 系へ利用者入力をそのまま渡していないかを確認する

関数ポインタやコールバックの型を確認する:
	ディスパッチテーブル、シグナル処理、イベント処理でシグネチャ不一致がないかを見る

警告ゼロを品質の最低線と考える:
	低レイヤでは「今は動く」より、「未定義動作の入口が消えているか」の方が重要
```

UmuOS を育てる作業では、ポインタの理解は機能実装のためだけでなく、壊れにくい設計を作るためにも必要です。
この章の内容が身につくと、コードを読むときに「このポインタはどこから来て、どこまで有効で、どこで壊れうるか」を自然に追えるようになります。