---
title: "UmuOSの為のC言語（現代ポインタ）　6章　構造体とポインタ"
---

# UmuOSの為のC言語（現代ポインタ）

このノートは、UmuOSを進化させるために必要となるC言語のポインタ理解を、現代のC17相当の視点で抽象化し、再構成することであります。
すなわち、UmuOSの実装・読解・デバッグへ還元するための実践ノートであります。
ポインタは、単なる文法項目ではなく、メモリ、配列、文字列、関数呼び出し、所有権、未定義動作と深く結びついています。
この構造を理解することは、UmuOSの設計力を高めることに直結すると思います。

## 6章　構造体とポインタ

構造体は、C で複数の関連データをひとまとまりとして扱うための基本手段です。
そしてポインタは、その構造体を動的に確保し、関数間で受け渡しし、互いに連結して、より大きいデータ構造を作るための要になります。

低レイヤでは、構造体とポインタの組み合わせはほぼ避けて通れません。
ファイル情報、プロセス状態、バッファ記述子、デバイス状態、リンクリスト、木構造、キュー、スタックなど、実装の実体はたいてい「構造体 + ポインタ」でできています。
したがって、この章の本質は単に `struct` の文法を覚えることではなく、**構造体がどう配置され、所有権と寿命をどう管理し、複数ノードをどうつなぐか**を理解することです。

この章では、まず構造体宣言とメンバアクセスを確認し、ついでメモリ配置とパディング、構造体内部にポインタを持つ場合の初期化と解放、
`malloc` と `free` のオーバーヘッドを抑える再利用の考え方を整理します。
後半では、リンクリスト、キュー、スタック、二分探索木を通して、ポインタがどのようにデータ構造の「糊」として働くかを見ていきます。

### 6章の1　イントロダクション

構造体宣言にはいくつかの流儀がありますが、ここでは実務で最もよく出る 2 つを見ます。

#### 6章の1の1　`struct` をそのまま使う

```c
struct person {
	char *first_name;
	char *last_name;
	char *title;
	unsigned int age;
};
```

この形では型名として `struct person` を使います。

```c
struct person someone;
```

#### 6章の1の2　`typedef` を使う

実務では `typedef` を併用して短く書くことも多いです。

```c
typedef struct person {
	char *first_name;
	char *last_name;
	char *title;
	unsigned int age;
} Person;
```

これで次のように書けます。

```c
Person person;
```

構造体自体をスタック上に置くこともできますし、ポインタ経由でヒープ上へ置くこともできます。

```c
#include <stdlib.h>

Person *ptr_person = malloc(sizeof(*ptr_person));
```

現代的な C では、`malloc` の戻り値を C で明示キャストしない書き方が一般的です。
`sizeof(*ptr_person)` と書くと、型名を書き換えたときにも追従しやすくなります。

#### 6章の1の3　メンバアクセス

構造体変数そのものにはドット演算子を使います。

```c
Person person;
person.age = 23;
```

構造体へのポインタには `->` を使います。

```c
Person *ptr_person = malloc(sizeof(*ptr_person));
if (ptr_person != NULL) {
	ptr_person->age = 23;
	free(ptr_person);
}
```

これは次の書き方と等価です。

```c
(*ptr_person).age = 23;
```

ただし可読性の点で通常は `->` を使います。

### 6章の2　構造体のメモリ割り当て

構造体の大きさは、単純に各メンバの `sizeof` を足した値とは限りません。
多くの処理系では、アラインメント制約を満たすために**パディング**が挿入されます。

#### 6章の2の1　パディングとは何か

```c
typedef struct person {
	char *first_name;
	char *last_name;
	char *title;
	unsigned int age;
} Person;

typedef struct alternate_person {
	char *first_name;
	char *last_name;
	char *title;
	short age;
} AlternatePerson;
```

`AlternatePerson` では `age` が `short` なので、一見すると小さくなりそうです。
しかし実際には末尾パディングにより `sizeof(AlternatePerson)` が `sizeof(Person)` と同じになることがあります。

```c
#include <stdio.h>

int main(void) {
	printf("sizeof(Person) = %zu\n", sizeof(Person));
	printf("sizeof(AlternatePerson) = %zu\n", sizeof(AlternatePerson));
	return 0;
}
```

この値は環境依存ですが、重要なのは「構造体には見えない隙間が入りうる」という点です。

#### 6章の2の2　なぜ重要か

パディングが重要になるのは次の場面です。

```text
バイナリレイアウトを前提にするとき:
	ファイル形式、ネットワークパケット、デバイスレジスタ写像など

構造体配列を扱うとき:
	要素間にもパディング込みの sizeof(struct) ぶん離れる

手書きのバイトコピーやシリアライズを行うとき:
	メンバごとの意味と実メモリ配置が一致するとは限らない
```

たとえば次の配列では、各要素は `sizeof(AlternatePerson)` ぶんずつ並びます。

```c
AlternatePerson people[30];
```

つまり `people[1]` は「見た目のメンバ合計」ではなく「コンパイラが決めた構造体サイズ」だけ先にあります。
構造体配列をポインタ演算で辿るときは、この `sizeof` が単位になります。

#### 6章の2の3　レイアウトを観察する

レイアウトを確かめたいときは `offsetof` が有効です。

```c
#include <stddef.h>
#include <stdio.h>

int main(void) {
	printf("offset first_name = %zu\n", offsetof(Person, first_name));
	printf("offset last_name  = %zu\n", offsetof(Person, last_name));
	printf("offset title      = %zu\n", offsetof(Person, title));
	printf("offset age        = %zu\n", offsetof(Person, age));
	return 0;
}
```

これは低レイヤで ABI やバイナリ互換性を確認するときに役立ちます。

### 6章の3　構造体のメモリを解放する時の問題

構造体そのものを確保したからといって、その内部のポインタ先まで自動で確保・解放されるわけではありません。
ここは C の所有権で非常に重要です。

#### 6章の3の1　内部ポインタは別管理

次の構造体を考えます。

```c
typedef struct person {
	char *first_name;
	char *last_name;
	char *title;
	unsigned int age;
} Person;
```

`Person person;` と宣言しても、3 つのポインタメンバはまだどこも有効に指していません。
自動変数なら未初期化、静的記憶域期間なら `NULL` になります。

したがって、使う前に明示的な初期化が必要です。

#### 6章の3の2　初期化関数

```c
#include <stdlib.h>
#include <string.h>

int initialize_person(Person *person,
			 const char *first_name,
			 const char *last_name,
			 const char *title,
			 unsigned int age) {
	person->first_name = NULL;
	person->last_name = NULL;
	person->title = NULL;
	person->age = age;

	person->first_name = malloc(strlen(first_name) + 1);
	person->last_name = malloc(strlen(last_name) + 1);
	person->title = malloc(strlen(title) + 1);

	if (person->first_name == NULL || person->last_name == NULL || person->title == NULL) {
		free(person->first_name);
		free(person->last_name);
		free(person->title);
		person->first_name = NULL;
		person->last_name = NULL;
		person->title = NULL;
		return 0;
	}

	strcpy(person->first_name, first_name);
	strcpy(person->last_name, last_name);
	strcpy(person->title, title);
	return 1;
}
```

原書風の単純な例では失敗時処理が省かれがちですが、現代的には途中失敗時の後始末まで含めて初期化関数にしておく方が安全です。

#### 6章の3の3　解放関数

```c
#include <stdlib.h>

void deallocate_person(Person *person) {
	free(person->first_name);
	free(person->last_name);
	free(person->title);
	person->first_name = NULL;
	person->last_name = NULL;
	person->title = NULL;
}
```

解放後に `NULL` を戻しておくと、二重解放や解放後使用の一部を気づきやすくできます。

#### 6章の3の4　スタック上の構造体とヒープ上の文字列

```c
void process_person(void) {
	Person person;

	if (!initialize_person(&person, "Peter", "Underwood", "Manager", 36)) {
		return;
	}

	/* person を使う */

	deallocate_person(&person);
}
```

ここで `person` 自体は自動変数なので関数終了時に消えますが、内部のヒープ文字列は自動では消えません。
したがって `deallocate_person` を呼ばなければリークです。

#### 6章の3の5　構造体自体もヒープにある場合

```c
void process_person_heap(void) {
	Person *ptr_person = malloc(sizeof(*ptr_person));
	if (ptr_person == NULL) {
		return;
	}

	if (!initialize_person(ptr_person, "Peter", "Underwood", "Manager", 36)) {
		free(ptr_person);
		return;
	}

	deallocate_person(ptr_person);
	free(ptr_person);
}
```

この場合は 2 段階の解放が必要です。

```text
1. 内部ポインタ先を解放する
2. 構造体本体を解放する
```

### 6章の4　malloc/free のオーバーヘッドを最小化する

構造体の生成と破棄を大量に繰り返すと、割り当てコストやヒープ断片化が無視できなくなることがあります。
低レイヤや高頻度処理では、再利用プールを持つ設計が使われます。

#### 6章の4の1　固定長プールの考え方

```c
#define LIST_SIZE 10

static Person *list[LIST_SIZE];
```

空きスロットへ未使用インスタンスを戻し、必要になったら取り出す方式です。

```c
void initialize_list(void) {
	for (size_t index = 0; index < LIST_SIZE; index++) {
		list[index] = NULL;
	}
}
```

#### 6章の4の2　取り出す

```c
Person *get_person(void) {
	for (size_t index = 0; index < LIST_SIZE; index++) {
		if (list[index] != NULL) {
			Person *ptr = list[index];
			list[index] = NULL;
			return ptr;
		}
	}

	return malloc(sizeof(Person));
}
```

#### 6章の4の3　返却する

```c
Person *return_person(Person *person) {
	for (size_t index = 0; index < LIST_SIZE; index++) {
		if (list[index] == NULL) {
			list[index] = person;
			return person;
		}
	}

	deallocate_person(person);
	free(person);
	return NULL;
}
```

この例は発想を示すには十分ですが、実戦では注意点があります。

```text
再利用前に状態を必ず初期化すること:
	前回のポインタやフラグを残したまま使うと破綻する

内部所有権をどう扱うか明確にすること:
	返却時に内部文字列も保持するのか、解放してから戻すのかを決める

スレッド安全ではないこと:
	複数スレッドで使うなら別途同期が必要

固定長なのであふれること:
	大きすぎても小さすぎても非効率になる
```

低レイヤでは、ここからさらにメモリプール、スラブ、フリーリストなどへ発展します。

### 6章の5　データ構造とポインタ

ポインタの本領は、構造体ノードどうしを動的に結び付けられることです。
ここからは単方向リンクリスト、キュー、スタック、二分探索木を通して見ていきます。

#### 6章の5の1　汎用比較関数と表示関数

まず、例に使う従業員データです。

```c
typedef struct employee {
	char name[32];
	unsigned char age;
} Employee;
```

ここでは説明を単純にするために `name` を固定長配列にしています。
実務では動的文字列にすることもありますが、そのぶん所有権管理が増えます。

```c
#include <stdio.h>
#include <string.h>

int compare_employee(const Employee *left, const Employee *right) {
	return strcmp(left->name, right->name);
}

void display_employee(const Employee *employee) {
	printf("%s\t%u\n", employee->name, (unsigned int)employee->age);
}
```

原書では `void *` ベースの関数ポインタを使いますが、比較関数本体は型付きで書いておいた方が安全です。
ラッパや変換は境界で行います。

```c
typedef void (*DISPLAY)(void *);
typedef int (*COMPARE)(const void *, const void *);
```

### 6章の6　単方向リンクリスト

#### 6章の6の1　ノードとリスト本体

```c
typedef struct node {
	void *data;
	struct node *next;
} Node;

typedef struct linked_list {
	Node *head;
	Node *tail;
	Node *current;
} LinkedList;
```

`data` を `void *` にすると汎用リストになりますが、使う側は正しい型へ戻す責任を持ちます。

#### 6章の6の2　初期化

```c
void initialize_list_nodes(LinkedList *list) {
	list->head = NULL;
	list->tail = NULL;
	list->current = NULL;
}
```

#### 6章の6の3　先頭へ追加

```c
#include <stdlib.h>

int add_head(LinkedList *list, void *data) {
	Node *node = malloc(sizeof(*node));
	if (node == NULL) {
		return 0;
	}

	node->data = data;
	if (list->head == NULL) {
		list->tail = node;
		node->next = NULL;
	} else {
		node->next = list->head;
	}
	list->head = node;
	return 1;
}
```

#### 6章の6の4　末尾へ追加

```c
int add_tail(LinkedList *list, void *data) {
	Node *node = malloc(sizeof(*node));
	if (node == NULL) {
		return 0;
	}

	node->data = data;
	node->next = NULL;

	if (list->head == NULL) {
		list->head = node;
	} else {
		list->tail->next = node;
	}

	list->tail = node;
	return 1;
}
```

#### 6章の6の5　ノード検索

```c
Node *get_node(LinkedList *list, COMPARE compare, const void *data) {
	Node *node = list->head;

	while (node != NULL) {
		if (compare(node->data, data) == 0) {
			return node;
		}
		node = node->next;
	}

	return NULL;
}
```

ここで関数ポインタを使っているので、リスト本体は `Employee` 専用に固定されません。

#### 6章の6の6　削除

```c
void delete_node(LinkedList *list, Node *node) {
	if (list == NULL || node == NULL) {
		return;
	}

	if (node == list->head) {
		if (list->head->next == NULL) {
			list->head = NULL;
			list->tail = NULL;
		} else {
			list->head = list->head->next;
		}
	} else {
		Node *tmp = list->head;

		while (tmp != NULL && tmp->next != node) {
			tmp = tmp->next;
		}

		if (tmp == NULL) {
			return;
		}

		tmp->next = node->next;
		if (list->tail == node) {
			list->tail = tmp;
		}
	}

	free(node);
}
```

ここで解放しているのはノードだけです。
`node->data` が指す実データの寿命は別問題であり、呼び出し側の所有権設計次第です。

#### 6章の6の7　表示

```c
#include <stdio.h>

void display_linked_list(LinkedList *list, DISPLAY display) {
	Node *current = list->head;

	printf("\nLinked List\n");
	while (current != NULL) {
		display(current->data);
		current = current->next;
	}
}
```

### 6章の7　キューとポインタ

キューは FIFO、つまり先に入れたものを先に取り出す構造です。
ここではリンクリストを土台にします。

```c
typedef LinkedList Queue;
```

```c
void initialize_queue(Queue *queue) {
	initialize_list_nodes(queue);
}

int enqueue(Queue *queue, void *data) {
	return add_head(queue, data);
}
```

取り出しは末尾側から行います。

```c
void *dequeue(Queue *queue) {
	Node *tmp = queue->head;
	void *data;

	if (queue->head == NULL) {
		return NULL;
	}

	if (queue->head == queue->tail) {
		queue->head = NULL;
		queue->tail = NULL;
		data = tmp->data;
		free(tmp);
		return data;
	}

	while (tmp->next != queue->tail) {
		tmp = tmp->next;
	}

	queue->tail = tmp;
	tmp = tmp->next;
	queue->tail->next = NULL;
	data = tmp->data;
	free(tmp);
	return data;
}
```

この実装は分かりやすい一方で、`dequeue` が末尾直前まで毎回走査するので $O(n)$ です。
高頻度用途なら、enqueue/dequeue の向きを変えるか、双方向リストや別設計を検討します。

### 6章の8　スタックとポインタ

スタックは FILO、つまり後から積んだものを先に取り出す構造です。
単方向リンクリストでは先頭側だけで完結するので、キューより実装が素直です。

```c
typedef LinkedList Stack;
```

```c
void initialize_stack(Stack *stack) {
	initialize_list_nodes(stack);
}

int push(Stack *stack, void *data) {
	return add_head(stack, data);
}
```

```c
void *pop(Stack *stack) {
	Node *node = stack->head;
	void *data;

	if (node == NULL) {
		return NULL;
	}

	if (node == stack->tail) {
		stack->head = NULL;
		stack->tail = NULL;
	} else {
		stack->head = stack->head->next;
	}

	data = node->data;
	free(node);
	return data;
}
```

これに加えて、先頭要素を見るだけで取り除かない `peek` もよく使われます。

### 6章の9　ツリーとポインタ

ツリーは親子関係を持つデータ構造で、特に二分探索木はポインタ学習の定番です。

#### 6章の9の1　ノード構造

```c
typedef struct tree_node {
	void *data;
	struct tree_node *left;
	struct tree_node *right;
} TreeNode;
```

#### 6章の9の2　挿入

```c
int insert_node(TreeNode **real_root, COMPARE compare, void *data) {
	TreeNode *node = malloc(sizeof(*node));
	TreeNode *root = *real_root;

	if (node == NULL) {
		return 0;
	}

	node->data = data;
	node->left = NULL;
	node->right = NULL;

	if (root == NULL) {
		*real_root = node;
		return 1;
	}

	for (;;) {
		if (compare(root->data, data) > 0) {
			if (root->left != NULL) {
				root = root->left;
			} else {
				root->left = node;
				break;
			}
		} else {
			if (root->right != NULL) {
				root = root->right;
			} else {
				root->right = node;
				break;
			}
		}
	}

	return 1;
}
```

ここで `TreeNode **` を受けているのが重要です。
根そのものが `NULL` から新ノードへ変わる可能性があるため、ポインタの値を関数内で更新する必要があります。
これは二重ポインタの典型例です。

#### 6章の9の3　走査

```c
void in_order(TreeNode *root, DISPLAY display) {
	if (root != NULL) {
		in_order(root->left, display);
		display(root->data);
		in_order(root->right, display);
	}
}

void pre_order(TreeNode *root, DISPLAY display) {
	if (root != NULL) {
		display(root->data);
		pre_order(root->left, display);
		pre_order(root->right, display);
	}
}

void post_order(TreeNode *root, DISPLAY display) {
	if (root != NULL) {
		post_order(root->left, display);
		post_order(root->right, display);
		display(root->data);
	}
}
```

二分探索木では `in_order` で昇順走査になります。

```text
pre-order:
	ノード、左、右

in-order:
	左、ノード、右

post-order:
	左、右、ノード
```

再帰で書くと自然ですが、深い木では再帰の深さにも注意が必要です。

### 6章の10　まとめ

この章では、構造体とポインタを組み合わせることで、単なる固定レコードから動的データ構造まで作れることを見ました。
まず重要なのは、構造体の実メモリ配置にはパディングが入りうること、そして構造体本体とその内部ポインタ先は別々に所有権を管理しなければならないことです。

また、初期化関数と解放関数を対で設計すること、必要に応じて再利用プールで割り当てコストを抑えること、ノード構造を `void *` と関数ポインタで汎用化できることも確認しました。
後半では、単方向リンクリストを土台にキューとスタックを作り、さらに二重ポインタを使って二分探索木へノードを挿入する方法を見ました。

UmuOS や低レイヤの観点では、次の点を特に意識すると読みやすくなります。

```text
構造体本体と内部ポインタ先を分けて考える:
	free(struct_ptr) だけでは内部文字列や内部バッファは消えない

実レイアウトを sizeof と offsetof で見る:
	パディング前提で ABI や配列間隔を考える

ノード所有権を明確にする:
	リストが data まで所有するのか、ノードだけ所有するのかを切り分ける

head、tail、next、left、right の更新順に注目する:
	ポインタ更新の順番を誤ると簡単に構造が壊れる

二重ポインタが必要な場面を見抜く:
	呼び出し先で「根ポインタそのもの」を差し替えるなら T ** が必要になる

再帰とスタック消費も意識する:
	木走査や深い探索は概念的に簡単でも、実行時コストは別問題
```

低レイヤの実コードでは、構造体は単なるデータ箱ではありません。
状態、リンク、所有権、寿命、バイナリ配置のすべてがそこに集約されます。
この章の内容が腹に落ちると、シェル、エディタ、カーネル寄りコード、ライブラリ内部実装の読解がかなり前へ進みます。