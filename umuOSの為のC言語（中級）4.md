---
title: "UmuOSの為のC言語４（中級）　４章　高度なファイルI/O"
---

# UmuOSの為のC言語（中級）　４

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結するとおもう。

## ４章　高度なファイルI/O

この章では、2章の低レベルI/O、3章の標準I/Oを土台にして、より高度なファイルI/Oを整理します。

2章では、`open()`、`read()`、`write()`、`close()`、`lseek()` といった基本を見ました。
3章では、`FILE *`、`fopen()`、`fread()`、`fwrite()`、`fflush()` など、標準I/Oによるユーザー空間バッファリングを見ました。

4章では、その先にある、より実践的でOS設計に近いI/Oの考え方を扱っていきます。
たとえば、ベクトルI/O、非同期I/O、多重化、メモリマップI/O、ファイルI/Oの性能や整合性に関わる話が中心になります。

UmuOSの視点では、この章はかなり重要です。
シェル、ログ、ファイルコピー、ネットワーク処理、デバイスI/O、ファイルシステム設計などでは、単純な `read()` / `write()` だけでは見通しが悪くなる場面が出てくるからです。

Ushのようなシェルを作る場合でも、リダイレクト、パイプ、標準入出力の制御、複数fdの監視などは、高度なI/Oの理解につながります。

この章も、古い説明や古いサンプルコードは、現在のLinux/POSIX/glibcの感覚に合わせて整理し直しながら進めます。

4章で扱う主な内容は、だいたい次のようなものになります。

scatter-gather I/O
複数のバッファを、一度のシステムコールでまとめて読み書きする仕組みです。
ヘッダ、本文、フッタのように、データが最初から複数の部品に分かれている場合に役立ちます。

epoll
複数のファイルディスクリプタを監視する仕組みです。
古くからある `select()` や `poll()` より、大量のfdを扱う用途に向いています。
現在のLinuxサーバープログラミングでは、ネットワークI/Oの多重化でかなり重要な位置にあります。

メモリマップI/O
ファイルをメモリ空間へ対応づけて、通常のメモリアクセスのようにファイルを扱う仕組みです。
`read()` / `write()` とは違う見方でファイルを扱えるため、特定のI/Oパターンでは便利です。

アクセスパターンのヒント、アドバイス
アプリケーションが「これから順番に読むと思います」「ランダムアクセスします」などの情報をカーネルへ伝える仕組みです。
カーネル側の先読みやキャッシュ戦略に影響するため、性能の話と関係してきます。

非同期I/O
I/Oの完了をその場で待たずに、あとから完了を受け取る考え方です。
古いLinux AIO、POSIX AIO、そして現在では `io_uring` なども含めて、時代ごとの違いを意識する必要があります。

この章では、単にAPIの使い方を覚えるというより、LinuxカーネルがI/Oをどう効率化しようとしているのか、そしてUmuOSで同じような仕組みを考えるなら
どの抽象化が必要になりそうか、という視点で見ていきます。

### ４章の１　scatter-gather I/O

scatter-gather I/Oとは、複数のバッファを一度のシステムコールで読み書きするI/O方式です。
日本語では、スキャッタ・ギャザI/O、または、ばらまき・まとめI/Oのように説明されます。

通常の `read()` や `write()` は、基本的には1つの連続したバッファを対象にします。
これをリニアI/Oと呼ぶことがあります。

一方、scatter-gather I/Oでは、複数のバッファを配列として渡します。
読み取りの場合は、1つのデータストリームから複数のバッファへ順番にデータをばらまきます。
書き込みの場合は、複数のバッファに分かれたデータを、1つのデータストリームへ順番にまとめて書き込みます。

このため、ベクタI/O、vectored I/Oとも呼ばれます。

イメージとしては、次のような感じです。

```text
通常の write():
	[ 1つの大きなバッファ ]  ->  fd

writev():
	[ ヘッダ ][ 本文 ][ 改行 ] ->  fd

readv():
	fd -> [ ヘッダ用バッファ ][ 本文用バッファ ][ 余り用バッファ ]
```

たとえば、ネットワークの応答やログ出力では、ヘッダ部分、本文部分、改行部分などが別々のメモリ領域にあることがあります。
このとき、いったん全部を1つの大きなバッファへコピーしてから `write()` することもできます。
しかし、それだとコピー用のメモリ確保や、データ結合の手間が増えます。

`writev()` を使えば、分かれているバッファを分かれたままカーネルへ渡して、一度のシステムコールで書き込めます。
ここが scatter-gather I/O の大きな利点です。

#### ４章の１の１　scatter-gather I/O の利点

scatter-gather I/Oには、主に次のような利点があります。

より自然な処理
データが最初から複数の部品に分かれている場合、その構造を保ったままI/Oできます。
たとえば、ヘッダ、本文、終端文字のように分かれているデータを、無理に1つへ結合しなくてもよくなります。

効率性
複数回の `write()` を、1回の `writev()` にまとめられます。
システムコールは普通の関数呼び出しより重いので、回数を減らせるのは重要です。

パフォーマンス
システムコール発行回数を減らせるだけでなく、カーネル内部のI/O処理にも合いやすい形になります。
Linuxカーネル内部では、I/Oがベクタ化された形で扱われる場面が多く、`read()` や `write()` も「要素が1つだけのベクタI/O」と見なせます。

UmuOSの視点では、これはかなり面白いです。
最初は `read()` / `write()` のような単純なI/Oだけで十分に見えますが、OS内部のI/O管理を考えると、実は「複数のメモリ領域をまとめて扱う」抽象化があると便利です。
ファイル、パイプ、ソケット、端末などを同じように扱いたい場合、内部表現としてベクタI/Oを持っておくと設計の見通しがよくなる可能性があります。

#### ４章の１の２　readv() と writev()

POSIXでは、scatter-gather I/Oを行うための関数として `readv()` と `writev()` が定義されています。
Linuxでも利用できます。

プロトタイプは次の通りです。

```c
#include <sys/uio.h>

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```

`readv()` は、ファイルディスクリプタ `fd` からデータを読み取り、`iov` が指す複数のバッファへ順番に格納します。
`writev()` は、`iov` が指す複数のバッファの内容を、ファイルディスクリプタ `fd` へ順番に書き込みます。

ここで出てくる `struct iovec` は、1つのバッファを表す構造体です。

```c
#include <sys/uio.h>

struct iovec {
	void  *iov_base;  // バッファの先頭アドレス
	size_t iov_len;   // バッファのバイト数
};
```

`iov_base` は、読み書きしたいメモリ領域の先頭アドレスです。
`iov_len` は、そのメモリ領域のサイズです。

`struct iovec` が1個なら、1つのバッファを表します。
これを配列にすると、複数のバッファをまとめたベクタになります。

```text
iov[0] -> バッファA, サイズA
iov[1] -> バッファB, サイズB
iov[2] -> バッファC, サイズC
```

`readv()` も `writev()` も、`iov[0]`、`iov[1]`、`iov[2]` のように、先頭から順番に処理します。
最後に処理されるのは `iov[iovcnt - 1]` です。

たとえば `writev()` の場合、次のように書き込まれます。

```text
iov[0] の内容 -> iov[1] の内容 -> iov[2] の内容
```

ファイル側から見ると、複数のバッファに分かれていたことは見えません。
結果としては、順番に連結された1つのデータとして書き込まれます。

#### ４章の１の３　戻り値とエラー

`readv()` と `writev()` は、成功すると処理したバイト数を返します。
戻り値の型は `ssize_t` です。

エラーが起きた場合は `-1` を返し、`errno` に原因が設定されます。
基本的なエラーは `read()` や `write()` と同じように考えればよいです。

ただし、ベクタI/O特有の注意点もあります。

まず、すべての `iov_len` の合計が `SSIZE_MAX` を超える場合はエラーになります。
戻り値の型が `ssize_t` なので、表現できる範囲を超えるI/O要求は扱えないためです。
この場合、Linuxでは `EINVAL` になる可能性があります。

次に、`iovcnt` の値にも上限があります。
POSIXでは、`iovcnt` は1以上 `IOV_MAX` 以下とされています。
Linuxでは、現在多くの環境で `IOV_MAX` は1024です。
ただし、固定値を決め打ちするより、`sysconf(_SC_IOV_MAX)` で確認できることを覚えておくとよいです。

```c
#include <limits.h>
#include <unistd.h>

long max_iov = sysconf(_SC_IOV_MAX);
```

`iovcnt` に0を渡した場合の扱いは、Unix系OSによって違いがあります。
Linuxでは0を返す動作になりますが、移植性を考えるなら、基本的には0を渡さない設計にしておくのが無難です。

また、`writev()` は常に全バイトを書き込むとは限りません。
通常ファイルでは全部書けることが多いですが、パイプ、ソケット、非ブロッキングI/O、シグナル割り込みなどが絡むと、途中までしか書けないことがあります。
これは `write()` と同じで、戻り値を見て「何バイト処理できたか」を確認する必要があります。

#### ４章の１の４　writev() のサンプルコード

次に、`writev()` の簡単なサンプルを見ます。
古い本のサンプルでは `open()` に `O_CREAT` を指定しているのに、パーミッションの引数を渡していないものがあります。
現在のCでは、これは避けるべきです。

`O_CREAT` を使う場合、`open()` の第3引数に作成時のモードを指定します。
たとえば `0644` なら、所有者は読み書き可能、グループとその他は読み取り可能です。

```c
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
	const char *parts[] = {
		"header: tama-log\n",
		"body: scatter-gather I/O example\n",
		"end\n"
	};
	struct iovec iov[3];

	for (size_t i = 0; i < 3; i++) {
		iov[i].iov_base = (void *)parts[i];
		iov[i].iov_len = strlen(parts[i]);
	}

	int fd = open("writev-example.txt", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd == -1) {
		perror("open");
		return 1;
	}

	ssize_t written = writev(fd, iov, 3);
	if (written == -1) {
		perror("writev");
		close(fd);
		return 1;
	}

	printf("wrote %zd bytes\n", written);

	if (close(fd) == -1) {
		perror("close");
		return 1;
	}

	return 0;
}
```

このコードでは、3つの文字列を1つのファイルへまとめて書き込んでいます。
ポイントは、文字列を1つの大きなバッファへコピーしていないことです。

```text
parts[0] -> "header: tama-log\n"
parts[1] -> "body: scatter-gather I/O example\n"
parts[2] -> "end\n"
```

この3つを `struct iovec` の配列として `writev()` に渡しています。
ファイルには、次のように連結された内容が書き込まれます。

```text
header: tama-log
body: scatter-gather I/O example
end
```

コンパイル例です。

```bash
gcc -Wall -Wextra -std=c17 writev_example.c -o writev_example
./writev_example
cat writev-example.txt
```

ここで、古いサンプルから少し直している点があります。

`main(void)` と書く
Cでは、引数を取らない `main` は `int main(void)` と書くのが明確です。

`O_CREAT` にはモードを渡す
`open()` でファイルを作成するなら、第3引数に `0644` などを指定します。

`strlen(parts[i]) + 1` にしない
テキストファイルへ文字列を書く場合、通常は末尾のヌル文字 `\0` まで書き込みません。
古いサンプルでは `+ 1` してヌル文字まで書くことがありますが、普通のテキストファイルとしては不要です。

`%zd` で `ssize_t` を表示する
`writev()` の戻り値は `ssize_t` なので、`printf()` では `%zd` を使います。

`O_CLOEXEC` を使う
`exec()` で別プログラムを起動したときに、意図せずfdを引き継がないようにする指定です。
シェルやサーバープログラムではfd漏れ対策として重要です。

UmuOSやUshの視点では、`O_CLOEXEC` はかなり大事です。
シェルが外部コマンドを起動するとき、不要なfdを子プロセスへ渡してしまうと、パイプが閉じない、ファイルが開きっぱなしになる、というバグにつながるからです。

#### ４章の１の５　writev() の部分書き込みについて

上のサンプルは通常ファイルへ小さいデータを書いているので、ほとんどの場合は一度で書き込めます。
しかし、実用コードでは、`writev()` の戻り値が要求した合計サイズより小さい場合があります。

たとえば、次のような場合です。

パイプへ書く場合
パイプのバッファに空きが少なければ、途中までしか書けないことがあります。

ソケットへ書く場合
ネットワーク送信バッファの状態によって、部分書き込みになることがあります。

非ブロッキングfdの場合
`O_NONBLOCK` が付いているfdでは、すぐに処理できる分だけ処理して戻ることがあります。

シグナルで割り込まれた場合
途中まで書いたところでシグナルが入り、戻ってくる可能性があります。

そのため、本当に全データを書きたい場合は、戻り値を見て残りを再度書く処理が必要になります。
ただし、`writev()` の部分書き込み処理は少し面倒です。
なぜなら、戻り値は「全体で何バイト書けたか」だけなので、どの `iovec` の途中まで進んだかを計算し直す必要があるからです。

イメージとしては、次のようになります。

```text
要求:
	iov[0] 10バイト
	iov[1] 20バイト
	iov[2] 30バイト

writev() の戻り値:
	25バイト

結果:
	iov[0] は10バイトすべて書けた
	iov[1] は15バイトだけ書けた
	iov[2] はまだ書けていない
```

この場合、次は `iov[1]` の残り5バイトと、`iov[2]` の30バイトを書けばよいことになります。

初心者段階では、まず `writev()` は複数バッファを一度に渡せる仕組みだと理解すれば十分です。
ただし、実用コードでは、戻り値を見て部分書き込みに対応する必要がある、という点は早めに意識しておくとよいです。

#### ４章の１の６　readv() のサンプルコード

次に、`readv()` のサンプルです。
先ほど `writev()` で作った `writev-example.txt` を、3つのバッファへ分けて読み取ります。

```c
#include <fcntl.h>
#include <stdio.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
	char header[17];
	char body[37];
	char tail[4];

	struct iovec iov[3] = {
		{ .iov_base = header, .iov_len = sizeof(header) },
		{ .iov_base = body,   .iov_len = sizeof(body) },
		{ .iov_base = tail,   .iov_len = sizeof(tail) }
	};

	int fd = open("writev-example.txt", O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		perror("open");
		return 1;
	}

	ssize_t nread = readv(fd, iov, 3);
	if (nread == -1) {
		perror("readv");
		close(fd);
		return 1;
	}

	printf("read %zd bytes\n", nread);

	for (size_t i = 0; i < 3; i++) {
		printf("iov[%zu]: %.*s", i, (int)iov[i].iov_len, (char *)iov[i].iov_base);
	}

	if (close(fd) == -1) {
		perror("close");
		return 1;
	}

	return 0;
}
```

このコードでは、読み取った内容を3つの配列へ順番に格納しています。

```text
fdから読むデータ:
	header: tama-log\nbody: scatter-gather I/O example\nend\n

readv() の格納先:
	header[]
	body[]
	tail[]
```

注意点として、`readv()` は文字列を作る関数ではありません。
つまり、読み込んだバッファの末尾に自動で `\0` を付けてくれるわけではありません。
これは `read()` と同じです。

そのため、サンプルでは `printf("%.*s", ...)` の形で、表示するバイト数を指定しています。
`%s` だけで表示すると、ヌル終端されていないメモリを読み続けてしまう危険があります。

ここはC初心者にはかなり重要です。
`read()` や `readv()` は「文字列を読む」のではなく、「バイト列を読む」関数です。
文字列として扱いたい場合は、自分で終端 `\0` を入れるか、表示時に長さを指定する必要があります。

#### ４章の１の７　素朴な writev() 実装イメージ

`writev()` は、ユーザー空間で素朴に書くなら、複数回の `write()` をループする形でも似たことはできます。

```c
#include <sys/uio.h>
#include <unistd.h>

ssize_t naive_writev(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t total = 0;

	for (int i = 0; i < iovcnt; i++) {
		ssize_t written = write(fd, iov[i].iov_base, iov[i].iov_len);
		if (written == -1) {
			return -1;
		}

		total += written;

		if ((size_t)written != iov[i].iov_len) {
			break;
		}
	}

	return total;
}
```

ただし、これは本物の `writev()` と同じではありません。
単に理解用のイメージです。

違いとして、まずシステムコール回数が増えます。
`iovcnt` が3なら `write()` を最大3回呼ぶため、`writev()` 1回より重くなります。

また、複数のバッファを1つのI/Oとして扱う意味も弱くなります。
特にパイプやソケットでは、途中に別プロセスの書き込みが挟まる可能性や、部分書き込みの扱いなども考える必要があります。

Linuxの `readv()` / `writev()` はシステムコールとして実装されていて、カーネル内部でベクタI/Oを扱います。
そのため、単なる便利関数というより、カーネルのI/O処理に直接関係するAPIと考えたほうがよいです。

#### ４章の１の８　Linuxカーネル内部の見方

Linuxでは、`read()` と `write()` だけが基本で、`readv()` と `writev()` があとから無理やり足されたもの、というより、内部的にはベクタI/Oの考え方がかなり自然です。

ざっくり言えば、`read()` や `write()` は、要素数1の `iovec` を使うI/Oのように見ることができます。

```text
write(fd, buf, len)

これは内部的には、かなり単純化して言えば、

iov[0].iov_base = buf
iov[0].iov_len  = len
writev(fd, iov, 1)

のような考え方に近いです。
```

もちろん、実際のカーネル実装は単純な置き換えではありません。
しかし、I/O対象を「1つ以上のメモリ領域」として抽象化する、という考え方はLinuxの内部設計と相性がよいです。

古い本では、カーネル内部の小さなベクタ数向け最適化として、スタック上の小規模配列を使う話が出てきます。
たとえば `UIO_FASTIOV` のような定数です。
ただし、このような内部実装の細部はカーネルバージョンによって変わります。

現在の学習では、固定で「8個以下なら必ず速い」と丸暗記するより、次のように理解しておく方がよいです。

```text
ベクタ数が少ない場合:
	カーネル内部で軽く扱える可能性が高い

ベクタ数が多すぎる場合:
	iovec配列の検査やコピーなどのオーバーヘッドが増える

実用上:
	必要な数だけ使う
	ただし、極端に細かく分けすぎない
```

つまり、`writev()` は便利だからといって、1バイトごとに `iovec` を分けるような使い方はよくありません。
ヘッダ、本文、改行、固定フッタなど、自然な単位で分けるのがよさそうです。

#### ４章の１の９　UmuOSでどう考えるか

UmuOSのような自作OSを考えるとき、scatter-gather I/Oは次のような設計課題につながります。

fdは何を指すのか
通常ファイル、パイプ、端末、ソケットなどを、同じfdインターフェースで扱えるかどうかが重要です。

I/O要求をどう表現するか
1つのバッファだけを前提にするのか、複数バッファをまとめて扱えるようにするのかで、内部設計が変わります。

コピーを減らせるか
ユーザー空間の複数バッファを、カーネルがどのように扱うかは性能に影響します。
ただし、ユーザー空間のポインタをカーネルが触る場合は、安全性チェックも必要になります。

部分読み書きをどう返すか
I/Oは常に全部成功するとは限りません。
戻り値として「何バイト処理できたか」を返す設計は、LinuxのI/O APIでも一貫して重要です。

Ushのようなシェルを作る場合、最初から `readv()` / `writev()` 相当まで必要とは限りません。
しかし、ログ出力、パイプ処理、複数fdの扱い、将来のソケット通信などを考えると、ベクタI/Oの考え方を知っておく価値は大きいです。

特に、シェルは文字列をたくさん扱います。
プロンプト、コマンド名、引数、改行、エラーメッセージなどを、毎回1つのバッファへ結合してから出すのではなく、複数の部品としてまとめて出す設計も考えられます。

たとえば、エラーメッセージなら次のような部品に分けられます。

```text
"ush: "
コマンド名
": command not found\n"
```

これらを `writev()` で標準エラーへまとめて出せば、余計な文字列結合を減らせます。
LinuxのAPIを学ぶと、こういう小さな設計の選択肢が増えていく感じです。

次は、複数のファイルディスクリプタを効率よく監視する仕組みとして、`epoll` を見ていきます。

### ４章の２　Event Pollインターフェース

`epoll` は、複数のファイルディスクリプタを効率よく監視するためのLinux独自のI/O多重化インターフェースです。

2章で見た `select()` や `poll()` でも、複数のfdを監視できます。
たとえば、標準入力、ソケット、パイプなどのうち、どれが読み取り可能になったかを待つ、ということができます。

しかし、`select()` や `poll()` には大きな弱点があります。
監視したいfdの一覧を、システムコールを呼ぶたびに毎回カーネルへ渡す必要があることです。

fdが数個なら問題は小さいです。
しかし、ネットワークサーバーのように、数百、数千、あるいはそれ以上の接続を扱う場合、毎回すべてのfdをカーネルへ渡して、カーネル側でもすべてを調べるのは重くなります。

イメージとしては、次のような違いです。

```text
poll()/select():
	毎回、監視したいfd一覧を渡す
	毎回、カーネルがfd一覧を調べる
	毎回、結果をユーザー空間へ返す

epoll:
	先に監視したいfdをepollコンテキストへ登録する
	変更があるときだけ追加、変更、削除する
	待つときは「発生したイベント」だけを受け取る
```

`epoll` では、監視対象のfd集合をカーネル側に保持します。
そのため、イベント待ちのたびに全fd一覧を渡し直す必要がありません。
この設計により、大量のfdを扱うサーバープログラムでスケールしやすくなります。

現在のLinuxでは、Webサーバー、プロキシ、イベント駆動型のネットワークプログラムなどで、`epoll` は非常によく使われます。
ただし、POSIX標準ではなくLinux固有APIです。
移植性を重視するプログラムでは、BSD系の `kqueue`、WindowsのIOCP、または抽象化ライブラリを使うこともあります。

UmuOSの視点では、`epoll` は「fdをたくさん監視するには、fd一覧を毎回走査する設計だけでは限界がある」ということを教えてくれます。
自作OSで最初から `epoll` 相当まで作る必要はないかもしれません。
しかし、パイプ、端末、ソケット、プロセス通知などを扱うようになると、イベント待ちの設計は必ず重要になります。

#### ４章の２の１　epollの基本構造

`epoll` は、主に3つの操作で使います。

```text
1. epollコンテキストを作る
	   epoll_create1()

2. 監視したいfdを追加、変更、削除する
	   epoll_ctl()

3. イベントが発生するまで待つ
	   epoll_wait()
```

ここでいう epollコンテキスト とは、監視対象fdの集合をカーネル側で管理するためのオブジェクトです。
Linuxでは、このepollコンテキスト自体もファイルディスクリプタとして表現されます。

つまり、`epoll_create1()` を呼ぶと、epoll用のfdが返ってきます。
このfdは普通のファイルを指しているわけではありません。
`epoll_ctl()` や `epoll_wait()` に渡すためのハンドルです。

イメージとしては、次のようになります。

```text
epfd = epoll_create1(...)

epfd が指すもの:
	カーネル内のepollコンテキスト
		監視対象fd 4
		監視対象fd 5
		監視対象fd 8
```

この `epfd` もfdなので、使い終わったら `close(epfd)` で閉じます。
ここは地味ですが大事です。

#### ４章の２の２　epollコンテキストの作成

古い本では、`epoll_create()` が紹介されていることが多いです。
プロトタイプは次のような形です。

```c
#include <sys/epoll.h>

int epoll_create(int size);
```

`epoll_create()` は、epollコンテキストを作成し、それに対応するfdを返します。
エラーの場合は `-1` を返し、`errno` に原因が入ります。

ただし、現在のLinuxプログラミングでは、基本的には `epoll_create1()` を使う方がよいです。

```c
#include <sys/epoll.h>

int epoll_create1(int flags);
```

`epoll_create1()` は Linux 2.6.27 以降で利用できます。
現在の一般的なLinux環境では、こちらを使うのが自然です。

よく使う指定は `EPOLL_CLOEXEC` です。

```c
int epfd = epoll_create1(EPOLL_CLOEXEC);
if (epfd == -1) {
	perror("epoll_create1");
	return 1;
}
```

`EPOLL_CLOEXEC` は、作成したepoll fdに close-on-exec を設定します。
つまり、`exec()` で別プログラムを起動したときに、このfdを引き継がないようにします。

Ushのようなシェルでは、これは特に重要です。
シェルは `fork()` して `exec()` する処理をたくさん行います。
不要なfdを子プロセスへ渡してしまうと、パイプが閉じない、リダイレクトが変な形で残る、監視用fdが外部コマンドへ漏れる、などの原因になります。

古い `epoll_create()` の `size` 引数は、もともと「このくらいのfdを監視する予定です」というヒントでした。
しかし、Linux 2.6.8以降では実質的に意味を持たず、正の値かどうかの確認に使われる程度です。
そのため、現在は `epoll_create1()` を使うほうがすっきりします。

`epoll_create1()` で起こりうる主なエラーは次のようなものです。

EINVAL
`flags` に無効な値を渡した場合です。

EMFILE
そのプロセスが開けるfd数の上限に達している場合です。

ENFILE
システム全体で開けるファイル数の上限に達している場合です。

ENOMEM
カーネルが必要なメモリを確保できなかった場合です。

#### ４章の２の３　epollコンテキストの制御

epollコンテキストへfdを追加したり、監視イベントを変更したり、削除したりするには `epoll_ctl()` を使います。

```c
#include <sys/epoll.h>

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
```

引数の意味は次の通りです。

`epfd`
`epoll_create1()` などで作成したepollコンテキストのfdです。

`op`
追加、変更、削除のどれを行うかを指定します。

`fd`
監視対象にしたいファイルディスクリプタです。

`event`
どのイベントを監視するか、イベント発生時にどのデータを返すかを指定する構造体です。

`op` に渡せる主な値は次の3つです。

EPOLL_CTL_ADD
fdをepollコンテキストへ追加します。

EPOLL_CTL_MOD
すでに追加されているfdの監視イベントを変更します。

EPOLL_CTL_DEL
fdをepollコンテキストから削除します。

`struct epoll_event` は、現在のLinuxではおおむね次のような構造です。

```c
#include <stdint.h>
#include <sys/epoll.h>

typedef union epoll_data {
	void     *ptr;
	int       fd;
	uint32_t  u32;
	uint64_t  u64;
} epoll_data_t;

struct epoll_event {
	uint32_t     events;
	epoll_data_t data;
};
```

実際にはヘッダで定義済みなので、自分でこの構造体を定義する必要はありません。
ここではイメージをつかむために載せています。

`events` には、監視したいイベントをビットORで指定します。
よく使うものは次の通りです。

EPOLLIN
読み取り可能になったことを表します。
次の `read()` がブロックせずに進められる可能性があります。

EPOLLOUT
書き込み可能になったことを表します。
次の `write()` がブロックせずに進められる可能性があります。

EPOLLERR
エラーが発生したことを表します。
これは明示的に指定しなくても通知されます。

EPOLLHUP
ハングアップが発生したことを表します。
相手側が閉じたパイプやソケットなどで見かけます。
これも明示的に指定しなくても通知されます。

EPOLLPRI
緊急データ、または高優先データがあることを表します。
普通のファイルI/Oではあまり使いません。

EPOLLET
エッジトリガで監視します。
これを指定しない場合、通常はレベルトリガです。

EPOLLONESHOT
一度イベントが発生したら、そのfdの監視を一時的に無効化します。
再び監視したい場合は `EPOLL_CTL_MOD` で再設定します。

`data` には、ユーザーが自由に使える値を入れられます。
イベントが発生したとき、`epoll_wait()` の結果として同じ `data` が返ってきます。

一番分かりやすい使い方は、`event.data.fd = fd` として、イベント発生時にfdを取り出せるようにする方法です。

```c
struct epoll_event event;

event.events = EPOLLIN;
event.data.fd = fd;

if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == -1) {
	perror("epoll_ctl: add");
}
```

すでに登録したfdの監視イベントを変更する場合は、`EPOLL_CTL_MOD` を使います。

```c
struct epoll_event event;

event.events = EPOLLIN | EPOLLOUT;
event.data.fd = fd;

if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event) == -1) {
	perror("epoll_ctl: mod");
}
```

削除する場合は、現在のLinuxでは `event` に `NULL` を渡せます。

```c
if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
	perror("epoll_ctl: del");
}
```

古いLinuxカーネルでは、`EPOLL_CTL_DEL` でも `NULL` を嫌う時期がありました。
しかし、現在のLinuxを前提にするなら、削除時は `NULL` で問題ありません。
古い互換性を本気で考える場合だけ、ダミーの `struct epoll_event` を渡す、という理解でよいと思います。

`epoll_ctl()` で起こりうる主なエラーも整理しておきます。

EBADF
`epfd` または `fd` が無効なファイルディスクリプタです。

EEXIST
`EPOLL_CTL_ADD` しようとしたfdが、すでに登録されています。

EINVAL
`epfd` がepoll fdではない、`op` が無効、または `epfd` と `fd` が同じ、などです。

ENOENT
`EPOLL_CTL_MOD` や `EPOLL_CTL_DEL` を指定したのに、そのfdが登録されていません。

ENOMEM
カーネルが必要なメモリを確保できませんでした。

EPERM
そのfdがepollで監視できない種類のfdです。

#### ４章の２の４　epoll_wait()でイベントを待つ

epollコンテキストに登録したfdのイベントを待つには、`epoll_wait()` を使います。

```c
#include <sys/epoll.h>

int epoll_wait(int epfd,
			   struct epoll_event *events,
			   int maxevents,
			   int timeout);
```

引数の意味は次の通りです。

`epfd`
epollコンテキストのfdです。

`events`
発生したイベントを受け取る配列です。

`maxevents`
一度に受け取る最大イベント数です。1以上である必要があります。

`timeout`
待ち時間をミリ秒で指定します。

`timeout` の指定は重要です。

```text
timeout = -1
	イベントが発生するまでずっと待つ

timeout = 0
	待たずにすぐ返る
	イベントがなければ0を返す

timeout > 0
	指定ミリ秒だけ待つ
	その間にイベントがなければ0を返す
```

`epoll_wait()` は、成功すると発生したイベント数を返します。
0を返した場合は、タイムアウトしたという意味です。
エラーの場合は `-1` を返し、`errno` に原因が入ります。

よくある使い方は次のようになります。

```c
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>

#define MAX_EVENTS 64

int wait_events(int epfd)
{
	struct epoll_event events[MAX_EVENTS];

	for (;;) {
		int nready = epoll_wait(epfd, events, MAX_EVENTS, -1);
		if (nready == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("epoll_wait");
			return -1;
		}

		for (int i = 0; i < nready; i++) {
			printf("event=%u on fd=%d\n",
				   events[i].events,
				   events[i].data.fd);

			/*
			 * 実際のプログラムでは、events[i].events を見て、
			 * events[i].data.fd に対して read() や write() を行います。
			 */
		}
	}
}
```

ここでは `malloc()` を使わず、固定長配列をスタック上に置いています。
イベントを一度にいくつ受け取るかが分かっている小さなサンプルなら、これで十分です。

実用コードでは、監視対象fdの数、イベント処理の設計、スレッド構成などによって、配列サイズやメモリ確保方法を決めます。

`epoll_wait()` で起こりうる主なエラーは次の通りです。

EBADF
`epfd` が無効なfdです。

EFAULT
`events` が書き込み可能なメモリを指していません。

EINTR
イベント待ち中にシグナルで割り込まれました。
実用コードでは、必要に応じて再試行します。

EINVAL
`epfd` がepoll fdではない、または `maxevents` が0以下です。

#### ４章の２の５　小さなepollサンプル

ここでは、標準入力を `epoll` で監視する小さなサンプルを見ます。
標準入力、つまり fd 0 が読み取り可能になったら、入力された行を読み取って表示します。

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#define MAX_EVENTS 8
#define BUF_SIZE 4096

int main(void)
{
	int epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd == -1) {
		perror("epoll_create1");
		return 1;
	}

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = STDIN_FILENO;

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) {
		perror("epoll_ctl");
		close(epfd);
		return 1;
	}

	puts("type something. Ctrl-D exits.");

	for (;;) {
		struct epoll_event events[MAX_EVENTS];
		int nready = epoll_wait(epfd, events, MAX_EVENTS, -1);

		if (nready == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("epoll_wait");
			close(epfd);
			return 1;
		}

		for (int i = 0; i < nready; i++) {
			if (events[i].data.fd == STDIN_FILENO &&
				(events[i].events & EPOLLIN)) {
				char buf[BUF_SIZE];
				ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));

				if (nread == -1) {
					perror("read");
					close(epfd);
					return 1;
				}

				if (nread == 0) {
					close(epfd);
					return 0;
				}

				if (write(STDOUT_FILENO, buf, (size_t)nread) == -1) {
					perror("write");
					close(epfd);
					return 1;
				}
			}
		}
	}
}
```

コンパイル例です。

```bash
gcc -Wall -Wextra -std=c17 epoll_stdin.c -o epoll_stdin
./epoll_stdin
```

このサンプルでは標準入力だけを監視しているので、`epoll` のありがたみはまだ小さいです。
しかし、fdを複数登録すれば、標準入力、パイプ、ソケットなどを同じ待ちループで扱えるようになります。

Ushで考えるなら、将来的に次のようなものを同時に待つ設計が考えられます。

```text
標準入力からのコマンド入力
ジョブ制御に関係する通知
パイプや疑似端末からの出力
内部イベント用のfd
```

小さなシェルではここまで必要ないかもしれません。
しかし、イベントループという考え方は、OSやシェルを大きくしていくときにかなり重要になります。

#### ４章の２の６　レベルトリガとエッジトリガ

`epoll` には、レベルトリガとエッジトリガという2つのイベント通知方式があります。

デフォルトはレベルトリガです。
`EPOLLET` を指定するとエッジトリガになります。

まず、レベルトリガは「状態」を見ます。
読み取り可能なデータが残っている間は、何度でも「読み取り可能です」と通知されます。

一方、エッジトリガは「変化」を見ます。
読み取り不可能な状態から読み取り可能な状態へ変わった瞬間など、状態が変化したときに通知されます。

パイプを例にすると分かりやすいです。

```text
1. 書き込み側がパイプへ1KBのデータを書く
2. 読み取り側が epoll_wait() する
3. 読み取り側が512バイトだけ読む
4. 読み取り側がもう一度 epoll_wait() する
```

ステップ2では、レベルトリガでもエッジトリガでも通知されます。
パイプにデータが入って、読み取り可能になったからです。

違いが出るのはステップ4です。

レベルトリガの場合、まだ512バイト残っているので、もう一度通知されます。
つまり「まだ読める状態ですよ」と教えてくれます。

エッジトリガの場合、ステップ3のあとに新しいデータが追加されていなければ、通知されない可能性があります。
なぜなら、読み取り可能という状態への新しい変化が起きていないからです。

図にすると、ざっくり次のようになります。

```text
レベルトリガ:
	データが残っている間は通知される

	パイプ内: 1024B -> 512B -> 512B
			  通知     通知     通知

エッジトリガ:
	状態が変わったときに通知される

	パイプ内: 0B -> 1024B -> 512B
			 変化   通知     通知なしの場合がある
```

このため、エッジトリガを使う場合は、基本的にノンブロッキングI/Oと組み合わせます。
そして、`read()` や `write()` を、`EAGAIN` または `EWOULDBLOCK` になるまで繰り返す、という書き方をします。

```text
エッジトリガの基本方針:
	fdをノンブロッキングにする
	イベントが来たら読めるだけ読む
	read() が EAGAIN になるまで読む
	そこで初めて「今はもう読めない」と判断する
```

これをしないと、データが残っているのに次の通知が来なくて、処理が止まったように見えることがあります。

初心者段階では、まずレベルトリガを使うのが安全です。
`select()` や `poll()` に近い感覚で扱えるためです。

エッジトリガは高性能なイベント駆動サーバーで使われることがありますが、書き方を間違えるとバグが見つけにくくなります。
特に、ノンブロッキングI/O、部分読み書き、`EAGAIN` の扱いを理解してから使うのがよいと思います。

#### ４章の２の７　epollで注意すること

`epoll` は強力ですが、いくつか注意点があります。

まず、`epoll` はLinux固有です。
POSIX標準ではありません。
Linux専用プログラムなら問題ありませんが、他のUnix系OSへ移植したい場合は別の仕組みが必要です。

次に、`epoll` で監視できるfdと、できないfdがあります。
ソケット、パイプ、端末などはよく使われます。
一方、普通の通常ファイルは、常に読み書き可能と見なされることが多く、`epoll` で待つ意味が薄いです。
通常ファイルのディスクI/O完了を非同期に待つ、という用途には、`epoll` は基本的に向いていません。
そのあたりは、後で非同期I/Oや `io_uring` の話に関係してきます。

また、`EPOLLERR` と `EPOLLHUP` は、明示的に指定していなくても返ることがあります。
そのため、イベント処理では `EPOLLIN` だけを見るのではなく、エラーや切断も考慮します。

```c
if (events[i].events & (EPOLLERR | EPOLLHUP)) {
	/* エラーまたは切断として扱う */
}
```

さらに、`epoll_wait()` はシグナルで割り込まれて `EINTR` を返すことがあります。
シェルのようにシグナルを扱うプログラムでは、これはかなり現実的な問題です。
`Ctrl-C`、ジョブ制御、子プロセス終了通知などと絡むため、`EINTR` をどう扱うかは丁寧に考える必要があります。

#### ４章の２の８　UmuOSでどう考えるか

UmuOSの設計として `epoll` を見ると、重要なのはAPIそのものよりも、イベント待ちの抽象化です。

単純なOSでは、プロセスが `read()` を呼んだら、データが来るまでそのプロセスを寝かせる、という設計から始められます。
しかし、1つのプロセスが複数の入力元を同時に待ちたい場合は、それだけでは足りません。

たとえば、シェルが次のようなものを同時に気にしたい場合です。

```text
キーボードから入力が来たか
子プロセスが終了したか
パイプからデータが来たか
端末状態が変わったか
タイマーが切れたか
```

これらを全部、単純なブロッキング `read()` だけで扱うのは難しいです。
そこで、イベントを登録し、発生したイベントだけを受け取る仕組みが欲しくなります。

UmuOSで最初に作るなら、Linuxの `epoll` を完全再現する必要はありません。
まずは、次のような小さな抽象化でもよいと思います。

```text
watch(fd, event_mask)
wait_event(timeout)
unwatch(fd)
```

このような仕組みがあると、シェル、端末、パイプ、将来のソケットなどをイベント駆動で扱いやすくなります。

Linuxの `epoll` は、そのかなり実用的で高性能な完成形の1つです。
UmuOSでは、まず「イベントを待つ対象を登録する」「発生したイベントだけ受け取る」「fdとイベントを結びつける」という考え方を吸収すると良さそうです。

次は、ファイルをメモリへ対応づけて扱う、メモリマップI/Oを見ていきます。

### ４章の３　ファイルをメモリへマッピングする
