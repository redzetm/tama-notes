---
title: "UmuOSの為のC言語５（中級）　５章　プロセス管理"
---

# UmuOSの為のC言語（中級）　５

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結するとおもう。

## ５章　プロセス管理

4章までは、ファイルI/Oを中心に見てきました。
`open()`、`read()`、`write()`、`close()`、標準I/O、`epoll`、`mmap()`、I/Oスケジューラなどを通して、Linuxではfdを中心に入出力を扱うことを整理しました。

5章では、いよいよプロセス管理へ進みます。
プロセスは、Linuxでプログラムが実行される基本単位です。
シェルからコマンドを実行するときも、Cプログラムから別のプログラムを起動するときも、裏側ではプロセスの作成、実行、終了、待ち合わせが行われます。

UmuOSの視点では、この章はかなり重要です。
自作OSでシェルを動かすには、単にファイルを読めるだけでは足りません。
コマンドを実行するためのプロセス生成、親子関係、終了ステータス、プロセスID、スケジューリング、シグナル、標準入出力の引き継ぎなどを理解する必要があります。

Ushのようなシェルでは、たとえば次のような処理が必要になります。

```text
コマンド行を読む
コマンドを解析する
子プロセスを作る
子プロセスで標準入出力を設定する
子プロセスで外部コマンドを実行する
親プロセスが子プロセスの終了を待つ
終了ステータスを受け取る
```

この流れを理解するために、5章ではLinuxのプロセス管理APIを、UmuOSへ還元できる形で見ていきます。

この章も、古い説明や古いサンプルコードは、現在のLinux/POSIX/glibcの感覚に合わせて整理し直しながら進めます。

Unix系OSにおいて、プロセスはファイルと並ぶくらい基本的な概念です。
ファイルが「データやデバイスへアクセスする入口」だとすれば、プロセスは「プログラムが実行されている実体」と言えます。

ただし、プロセスは単に機械語命令が動いているだけのものではありません。
プロセスには、コード、データ、ヒープ、スタック、開いているファイルディスクリプタ、シグナル状態、ユーザーID、グループID、カレントディレクトリ、環境変数、仮想アドレス空間など、たくさんの情報がまとまっています。

イメージとしては、次のようなものです。

```text
プロセス:
	実行中のプログラム本体
	仮想アドレス空間
	レジスタ状態
	開いているfd
	カレントディレクトリ
	環境変数
	シグナル設定
	権限情報
	親子関係
```

この章では、プロセスがどのように識別され、どのように作られ、どのように別のプログラムへ置き換わるのかを見ていきます。

Unixのプロセス管理で特に面白いのは、プロセスを作る処理と、別のプログラムを実行する処理が分かれていることです。

多くのOSや高水準APIでは、「このプログラムを新しく起動する」という1つの操作として見えます。
しかしUnixでは、基本的に次の2段階で考えます。

```text
fork()
	現在のプロセスを複製して、子プロセスを作る

exec()
	現在のプロセスの中身を、別のプログラムで置き換える
```

つまり、シェルが外部コマンドを起動するときは、まず自分自身を `fork()` で複製し、子プロセス側で `exec()` して別のプログラムへ変身させる、という流れになります。

これはUshを作るうえで、まさに中心になる仕組みです。

### ５章の１　プロセスID

すべてのプロセスには、プロセスIDがあります。
英語では process ID、短く pid と呼ばれます。

pidは、ある時点でプロセスを識別するための番号です。
同じ瞬間に、同じpidを持つプロセスが2つ存在することはありません。

ただし、pidは永久に一意ではありません。
あるプロセスが終了したあと、しばらくして同じpidが別のプロセスに再利用されることがあります。

```text
時刻 t0:
	pid 770 -> プロセスA

プロセスA終了

時刻 t1:
	pid 770 -> プロセスB
```

そのため、pidだけを長期間保存して「これは必ず同じプロセスだ」と考えるのは危険です。
実用上は、pidの再利用はすぐには起こりにくいですが、設計としては再利用される可能性を意識しておく必要があります。

#### ５章の１の１　pid 0 と pid 1

Linuxでは、特別なプロセスIDがあります。

pid 0
伝統的には idle プロセス、または swapper などと呼ばれる、カーネル内部の特別な実行単位です。
通常のユーザープロセスとして見たり操作したりするものではありません。
CPUが実行する通常のプロセスがないとき、カーネルはアイドル処理を行います。

pid 1
ユーザー空間で最初に起動されるプロセスです。
伝統的には init プロセスと呼ばれます。
現在の多くのLinuxディストリビューションでは、このpid 1は `systemd` であることが多いです。

古いUnixや古いLinuxの説明では、pid 1のプログラム名として `/sbin/init` がよく出てきます。
現在でも `/sbin/init` は存在することがありますが、実体は `systemd` へのシンボリックリンクになっている環境も多いです。

確認するには、たとえば次のようにできます。

```bash
ps -p 1 -o pid,comm,args
```

pid 1は、システム起動後のユーザー空間の親玉のような存在です。
サービスを起動したり、ログイン環境を整えたり、孤児プロセスを引き取ったりします。

UmuOSの視点では、pid 1相当のプロセスはとても重要です。
自作OSでも、カーネルが最初に起動するユーザー空間プログラムを何にするか、そしてそのプロセスが他のプロセスをどう管理するかは、OS全体の形に関わります。

#### ５章の１の２　initの探索と現在の読み替え

古いLinuxの説明では、カーネルが最初に起動するプログラムとして、次のような候補を順番に探すと説明されます。

```text
/sbin/init
/etc/init
/bin/init
/bin/sh
```

これは歴史的には重要な説明です。
ただし、現在の実際のLinuxでは、initramfs、systemd、カーネルコマンドライン、ディストリビューションの構成などが絡むため、単純にこの4つだけを見ればよいとは限りません。

カーネルコマンドラインで `init=/path/to/program` を指定すると、最初に実行するユーザー空間プログラムを明示できます。
これはレスキュー用途や、自作rootfsの実験でもよく使われます。

UmuOSの実験で小さなユーザーランドを作るなら、最初は `/init` や `/bin/sh` のような単純なプログラムをpid 1として起動する設計が分かりやすいと思います。

#### ５章の１の３　プロセスIDの割り振り

Linuxカーネルは、新しいプロセスにpidを割り振ります。
pidは基本的には増加していき、上限に達すると空いている番号を探して再利用します。

古い説明では、pidの最大値は32768がデフォルトとされることがあります。
これは古いUnixとの互換性を意識した値です。
しかし、現在のLinuxでは環境によってもっと大きな値が使われます。

実際の上限は、次のファイルで確認できます。

```bash
cat /proc/sys/kernel/pid_max
```

変更もできますが、通常の学習では変える必要はありません。
pidの最大値を不用意に変更すると、古いプログラムや監視ツールが想定している範囲とずれる可能性もあります。

大事なのは、pidは「今存在するプロセスを識別する番号」であり、永遠に一意なIDではない、という点です。

#### ５章の１の４　プロセス階層

プロセスは、基本的に親子関係を持ちます。

新しいプロセスを作った側を親プロセス、作られた側を子プロセスと呼びます。
子プロセスは、自分の親プロセスID、つまり ppid を持ちます。

```text
親プロセス:
	pid = 1000

子プロセス:
	pid  = 1001
	ppid = 1000
```

シェルからコマンドを実行すると、一般的にはシェルが親プロセス、実行されたコマンドが子プロセスになります。

```text
ush
 └── ls
```

パイプラインの場合は、複数の子プロセスが作られます。

```bash
cat input.txt | grep tama | wc -l
```

この場合、シェルは `cat`、`grep`、`wc` のために複数の子プロセスを作ります。
さらに、これらを同じプロセスグループにまとめることで、パイプライン全体を1つのジョブとして扱いやすくします。

#### ５章の１の５　ユーザーID、グループID、プロセスグループ

プロセスには、所有者としてのユーザーIDとグループIDがあります。
Linuxカーネルから見ると、ユーザーやグループは基本的に整数です。

```text
ユーザーID:
	uid

グループID:
	gid
```

`root` や `tama` のような名前は、主にユーザー空間で解釈されます。
たとえば `/etc/passwd` や `/etc/group` が、名前と数値IDの対応を持っています。

また、プロセスにはプロセスグループID、pgid もあります。
これはUnixシェルのジョブ制御で重要です。

プロセスグループは、ユーザーやグループとは別物です。
名前が似ているので注意です。

```text
ユーザー/グループ:
	権限のための分類

プロセスグループ:
	複数プロセスをジョブとしてまとめるための分類
```

シェルがパイプラインを実行するとき、パイプライン内の各プロセスを同じプロセスグループに入れることがあります。
すると、`Ctrl-C` のような端末からのシグナルを、パイプライン全体へ送れます。

Ushでジョブ制御を実装するなら、プロセスグループは避けて通れない概念になります。

#### ５章の１の６　pid_t

Cプログラムでは、プロセスIDは `pid_t` 型で扱います。

```c
#include <sys/types.h>
```

`pid_t` の実体は環境依存です。
Linuxでは多くの場合 `int` として扱えますが、C標準で決まっているわけではありません。

そのため、サンプルコードでは表示するときに `long` へキャストして `%ld` で出す形にしておくと、古い `%d` 決め打ちよりは安全です。

```c
printf("pid=%ld\n", (long)getpid());
```

完全に万能というわけではありませんが、実用的なサンプルとしてはこの形がよく使われます。

#### ５章の１の７　getpid() と getppid()

自分のプロセスIDを取得するには `getpid()` を使います。
親プロセスIDを取得するには `getppid()` を使います。

```c
#include <sys/types.h>
#include <unistd.h>

pid_t getpid(void);
pid_t getppid(void);
```

どちらもエラーを返しません。
簡単なサンプルです。

```c
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	printf("my pid = %ld\n", (long)getpid());
	printf("parent pid = %ld\n", (long)getppid());
	return 0;
}
```

コンパイル例です。

```bash
gcc -Wall -Wextra -std=c17 show_pid.c -o show_pid
./show_pid
```

実行するたびにpidは変わることがあります。
また、どのシェルや端末から実行したかによって、親プロセスIDも変わります。

### ５章の２　新規プロセスの起動

Unixでは、新しいプログラムを実行する処理が2つに分かれています。

```text
fork()
	プロセスを複製する

exec()
	現在のプロセスイメージを別のプログラムで置き換える
```

この分離がUnixらしいところです。

たとえばシェルが `ls` を起動する場合、シェル自身がいきなり `ls` に変わってしまうと困ります。
シェルはコマンド実行後も残って、次の入力を受け付ける必要があるからです。

そこで、シェルはまず `fork()` で子プロセスを作ります。
子プロセス側だけが `exec()` で `ls` に変わります。
親であるシェルはそのまま残り、必要なら子プロセスの終了を待ちます。

```text
親プロセス: shell
	fork()

親プロセス: shell のまま
子プロセス: shell のコピー
	exec("ls")

親プロセス: shell
子プロセス: ls
```

#### ５章の２の１　execファミリ

`exec` は、現在のプロセスの中身を別のプログラムで置き換えるための仕組みです。

重要なのは、`exec()` は新しいプロセスを作らないという点です。
現在のプロセスのpidはそのままです。
ただし、アドレス空間の中身、実行するコード、データ、スタックなどは新しいプログラムのものに置き換わります。

たとえるなら、プロセスという入れ物は残したまま、中身のプログラムを入れ替える感じです。

```text
exec前:
	pid 1234 -> 自作プログラム

exec後:
	pid 1234 -> /bin/ls
```

もっとも単純な形の1つが `execl()` です。

```c
#include <unistd.h>

int execl(const char *path, const char *arg, ...);
```

`path` には実行したいプログラムのパスを渡します。
その後に、実行されるプログラムへ渡す引数を並べます。
最後は必ず `(char *)NULL` で終わらせます。

```c
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	execl("/bin/ls", "ls", "-l", (char *)NULL);

	perror("execl");
	return 1;
}
```

`execl()` に成功すると、このプログラムは `/bin/ls` に置き換わります。
そのため、成功した場合は `execl()` から戻ってきません。

戻ってきた場合は、失敗したという意味です。
そのため、`execl()` の直後にある `perror()` は、失敗時だけ実行されます。

#### ５章の２の２　argv[0] の意味

`execl("/bin/ls", "ls", "-l", NULL)` の2番目の `"ls"` は、実行されるプログラムから見ると `argv[0]` になります。

Cプログラムの `main()` は、よく次の形で書きます。

```c
int main(int argc, char *argv[])
```

この `argv[0]` には、通常、プログラム名が入ります。
シェルから `ls -l` と実行すると、`ls` の `argv` はだいたい次のようになります。

```text
argv[0] = "ls"
argv[1] = "-l"
argv[2] = NULL
```

Unixでは、`argv[0]` は必ずしも実行ファイルのフルパスと一致する必要はありません。
プログラムによっては、`argv[0]` の名前を見て動作を変えるものもあります。

たとえば、同じバイナリに複数の名前でリンクを張り、呼ばれた名前によって動作を変える、という設計があります。

#### ５章の２の３　execファミリの種類

`exec` には複数の関数があります。
名前の末尾を見ると、だいたい意味が分かります。

```c
#include <unistd.h>

int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *path, const char *arg, ..., char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execve(const char *pathname, char *const argv[], char *const envp[]);
```

名前の見方です。

`l`
list の意味です。
引数をリストとして、可変長引数で渡します。

`v`
vector の意味です。
引数を配列で渡します。

`p`
PATHを検索します。
`ls` のようなコマンド名を渡すと、環境変数 `PATH` に従って実行ファイルを探します。

`e`
environment の意味です。
新しい環境変数配列を明示的に渡します。

実用上、シェルっぽい処理では `execvp()` がよく使いやすいです。
なぜなら、コマンド名を `PATH` から探してくれるからです。

```c
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char *const argv[] = { "ls", "-l", NULL };

	execvp("ls", argv);

	perror("execvp");
	return 1;
}
```

このコードは、`PATH` の中から `ls` を探して実行します。
Ushで外部コマンドを実行する場合、最初は `execvp()` を使うと分かりやすいです。

一方、`execve()` はLinuxで実際のシステムコールに対応する中心的なインターフェースです。
他の `exec` 関数は、glibcなどのCライブラリが提供するラッパ関数として、内部で `execve()` を呼ぶ形になります。

#### ５章の２の４　exec後に残るもの、消えるもの

`exec()` に成功すると、プロセスの中身は新しいプログラムに置き換わります。

消えるもの、リセットされるものの例です。

```text
古いプログラムのコード
古いプログラムのデータ
ヒープ
スタック
mmap() していたメモリ領域
atexit() に登録した処理
多くのシグナルハンドラ
```

一方、残るものもあります。

```text
pid
ppid
カレントディレクトリ
ルートディレクトリ
ユーザーID / グループID
環境変数、ただしexec関数の種類による
開いているファイルディスクリプタ
```

特に重要なのが、ファイルディスクリプタです。
通常、開いているfdは `exec()` 後も引き継がれます。

これはシェルにとって非常に便利です。
なぜなら、子プロセスで標準入力や標準出力を設定してから `exec()` すれば、実行されたプログラムはそのfd設定を引き継ぐからです。

```text
子プロセス:
	標準出力を output.txt に差し替える
	exec("grep")

grep:
	自分では何も知らなくても、標準出力が output.txt になっている
```

ただし、不要なfdまで引き継がれるとバグや情報漏れの原因になります。
そのため、`O_CLOEXEC` や `FD_CLOEXEC` が重要です。

ファイルを開くときに `O_CLOEXEC` を付けると、`exec()` 時にそのfdは自動で閉じられます。

```c
int fd = open("data.txt", O_RDONLY | O_CLOEXEC);
```

既存のfdに後から close-on-exec を設定する場合は、`fcntl()` を使います。

```c
#include <fcntl.h>

int flags = fcntl(fd, F_GETFD);
if (flags == -1) {
	/* エラー処理 */
}

if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
	/* エラー処理 */
}
```

Ushでは、パイプやリダイレクトのfdをどれだけ子プロセスへ残すか、どれを閉じるかがとても大事になります。

#### ５章の２の５　execのerrno

`exec()` は成功すると戻ってきません。
失敗した場合だけ `-1` を返し、`errno` に原因が入ります。

代表的なエラーを整理します。

E2BIG
引数や環境変数の合計サイズが大きすぎます。

EACCES
実行権限がない、パスの途中にアクセスできない、実行ファイルでない、ファイルシステムが `noexec` でマウントされている、などです。

ENOENT
指定したファイルが存在しません。
または、必要な動的リンカや共有ライブラリが見つからない場合にも関係することがあります。

ENOEXEC
実行形式として認識できません。
別アーキテクチャ用のバイナリや、不正な形式のファイルなどです。

ENOMEM
新しいプログラムを実行するためのメモリが不足しています。

ENOTDIR
パスの途中の要素がディレクトリではありません。

ELOOP
シンボリックリンクをたどりすぎました。

ETXTBSY
実行しようとしたファイルが、他のプロセスにより書き込み用に開かれている場合などです。

シェルを作るときは、`execvp()` が失敗した場合に、ユーザーへ分かりやすいエラーメッセージを出す必要があります。
たとえば、コマンドが見つからなければ `command not found`、権限がなければ `permission denied` のように出す設計です。

#### ５章の２の６　fork()

`fork()` は、現在実行中のプロセスを複製して、新しい子プロセスを作るシステムコールです。

```c
#include <sys/types.h>
#include <unistd.h>

pid_t fork(void);
```

`fork()` を呼ぶと、親プロセスとほぼ同じ内容を持つ子プロセスが作られます。
親と子は、`fork()` の次の行から、それぞれ別々に実行を続けます。

ここが最初は不思議に感じるところです。
`fork()` は1回しか呼んでいないのに、戻ったあとには親と子の2つの流れが存在します。

戻り値で親と子を区別します。

```text
親プロセス:
	fork() の戻り値は、子プロセスのpid

子プロセス:
	fork() の戻り値は 0

エラー:
	fork() は -1 を返す
	子プロセスは作られない
```

簡単なサンプルです。

```c
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		printf("child: pid=%ld, ppid=%ld\n", (long)getpid(), (long)getppid());
	} else {
		printf("parent: pid=%ld, child=%ld\n", (long)getpid(), (long)pid);
	}

	return 0;
}
```

このプログラムを実行すると、親と子の両方が `printf()` します。
表示順はスケジューリング次第なので、常に同じとは限りません。

#### ５章の２の７　fork()で異なるもの

`fork()` 後の子プロセスは、親プロセスによく似ています。
しかし、完全に同じではありません。

主な違いです。

```text
子プロセスには新しいpidが割り振られる
子プロセスのppidは親プロセスのpidになる
リソース使用量の統計は子プロセス側でリセットされる
保留中のシグナルは子プロセスへ引き継がれない
一部のロックは引き継がれない
```

一方で、開いているファイルディスクリプタは引き継がれます。
ここはシェルにとって非常に重要です。

親と子は同じオープンファイル記述を共有するため、ファイルオフセットを共有する場合があります。
このあたりは、2章で見たfdとオープンファイル記述の話につながります。

#### ５章の２の８　fork() のエラー

`fork()` が失敗すると、`-1` を返し、子プロセスは作られません。

代表的なエラーです。

EAGAIN
プロセス数の上限、ユーザーごとのプロセス数制限、pid割り当て、その他の一時的なリソース不足などで作れない場合です。

ENOMEM
カーネルが必要なメモリを確保できない場合です。

実用コードでは、`fork()` の戻り値を必ず確認します。
シェルで `fork()` に失敗した場合は、コマンドを実行できないため、親シェル側でエラーを表示して次の入力へ戻るようにします。

#### ５章の２の９　fork/exec の基本形

シェルで一番大事なのは、`fork()` と `exec()` の組み合わせです。

次のサンプルは、子プロセスで `ls -l` を実行し、親プロセスはそのまま残る例です。
子プロセスの終了待ちは次の節以降で詳しく扱うので、ここではまず起動の形だけ見ます。

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		char *const argv[] = { "ls", "-l", NULL };

		execvp("ls", argv);

		perror("execvp");
		_exit(127);
	}

	printf("parent: started child pid=%ld\n", (long)pid);
	return 0;
}
```

ここで、子プロセスの `execvp()` 失敗後に `exit()` ではなく `_exit()` を使っている点が重要です。

`exit()` はCライブラリの終了処理を行います。
標準I/Oのバッファをflushしたり、`atexit()` に登録された関数を呼んだりします。

しかし、`fork()` 後の子プロセスでは、親からコピーされたstdioバッファなどが残っています。
ここで `exit()` を使うと、親側で出すつもりだったバッファを子が重複してflushしてしまうことがあります。

そのため、`fork()` 後の子プロセスで `exec()` に失敗して終了する場合は、基本的に `_exit()` を使います。

```c
execvp(command, argv);
perror("execvp");
_exit(127);
```

シェルでは、`127` は「コマンドが見つからない」系の終了コードとして使われることが多いです。
厳密にはエラーの種類によって `126` と `127` を使い分けることもあります。

#### ５章の２の１０　copy-on-write

昔のUnixで `fork()` を実装する場合、親プロセスのメモリを子プロセスへ丸ごとコピーする必要がありました。
これは非常に重い処理です。

しかし、現代のLinuxでは copy-on-write、COW、コピーオンライトという仕組みを使います。

copy-on-writeは、直訳すると「書き込み時にコピーする」という意味です。

`fork()` 直後、親と子は同じメモリページを共有します。
ただし、そのページは読み取り専用のように扱われます。
どちらかが書き込もうとしたとき、ページフォルトが発生し、カーネルがそのページだけをコピーします。

```text
fork直後:
	親 -> ページA
	子 -> ページA
	共有している

子がページAへ書き込み:
	ページフォルト
	カーネルがページAをコピー

書き込み後:
	親 -> ページA
	子 -> ページAのコピー
```

これにより、`fork()` はかなり高速になります。

特にシェルのように、`fork()` の直後にすぐ `exec()` する場合、子プロセスの古いアドレス空間はすぐ破棄されます。
もし最初から全ページをコピーしていたら、ほとんど無駄になります。

copy-on-writeのおかげで、`fork()` の時点ではページテーブルなど必要な管理情報を用意し、実際のメモリコピーは必要になるまで遅らせられます。

UmuOSの視点では、copy-on-writeはかなり高度です。
まずは単純にメモリをコピーする `fork()` 風の実装から始めてもよいと思います。
その後、仮想メモリ、ページテーブル、ページフォルト処理が整ってきたら、copy-on-writeを考える流れが現実的です。

#### ５章の２の１１　vfork()

`vfork()` は、`fork()` の古い最適化として作られた仕組みです。

```c
#include <sys/types.h>
#include <unistd.h>

pid_t vfork(void);
```

`vfork()` は、子プロセスがすぐに `exec()` または `_exit()` することを前提にしています。
その間、親プロセスは停止し、子プロセスは親のアドレス空間を一時的に共有します。

そのため、`vfork()` 後の子プロセスで普通のCコードをいろいろ実行するのは危険です。
ローカル変数を書き換えたり、関数を呼びすぎたり、`return` したりすると、親プロセスの状態を壊す可能性があります。

現在のLinuxでは、copy-on-writeにより `fork()` が十分に効率化されています。
そのため、普通のアプリケーションや学習用コードでは、基本的に `fork()` を使えばよいです。

さらに、単に新しいプログラムを起動したい場合は、`posix_spawn()` という選択肢もあります。
これは `fork()` と `exec()` の中間のようなAPIで、環境によっては効率よく実装されます。
ただし、シェルの仕組みを理解する目的では、まず `fork()` と `exec()` をしっかり理解するのが大事です。

```text
学習順序としては:
	fork()
	exec()
	wait()
	パイプとリダイレクト
	ジョブ制御
	必要に応じて posix_spawn()
```

Ushでは、まず `fork()` + `execvp()` + `waitpid()` の形を作るのが分かりやすいと思います。
`vfork()` は、仕組みとして知っておく程度で十分です。

### ５章の３　プロセスの終了

プロセスは、いつか必ず終了します。
正常に仕事を終えて終了することもあれば、エラーで終了することもあります。
また、シグナルによって強制的に終了させられることもあります。

Cプログラムで現在のプロセスを終了する代表的な関数は `exit()` です。

```c
#include <stdlib.h>

void exit(int status);
```

`exit()` は、Cライブラリ側の終了処理を行ったあと、最終的にカーネルへ「このプロセスは終了します」と通知します。
`exit()` は戻りません。
そのため、`exit()` の後に普通の処理を書いても実行されません。

`status` は終了ステータスです。
シェルや親プロセスは、この終了ステータスを見て、子プロセスが成功したのか失敗したのかを判断できます。

一般的には、0が成功、0以外が失敗です。

```c
#include <stdlib.h>

exit(EXIT_SUCCESS);
exit(EXIT_FAILURE);
```

`EXIT_SUCCESS` と `EXIT_FAILURE` は、移植性を意識したマクロです。
Linuxでは多くの場合、`EXIT_SUCCESS` は0、`EXIT_FAILURE` は1です。

シェルで直前のコマンドの終了ステータスを見るには、普通は `$?` を使います。

```bash
./some_command
echo $?
```

Ushを作る場合も、直前のコマンドの終了ステータスを保存して、将来的には `$?` 相当で参照できるようにする設計が考えられます。

#### ５章の３の１　exit() が行う処理

`exit()` は、単にカーネルへ終了を伝えるだけではありません。
その前に、Cライブラリ側の後始末を行います。

代表的には次のような処理です。

```text
atexit() に登録された関数を実行する
標準I/Oストリームをflushする
tmpfile() で作成した一時ファイルを削除する
最終的に _exit() 相当でカーネルへ終了を伝える
```

3章で見たように、標準I/Oにはユーザー空間バッファがあります。
そのため、`printf()` した内容がまだ実際のfdへ書き出されていないことがあります。
`exit()` は、通常このバッファをflushします。

```c
printf("hello");
exit(EXIT_SUCCESS);
```

この場合、改行がなくても `exit()` の終了処理で標準出力がflushされ、`hello` が出力されることがあります。

ただし、`fork()` 後の子プロセスで `exec()` に失敗した場合などは、前節で見たように `exit()` ではなく `_exit()` を使うことが重要です。
親からコピーされたstdioバッファを、子が二重にflushしてしまう可能性があるからです。

#### ５章の３の２　_exit() と _Exit()

`_exit()` は、Cライブラリの通常の終了処理をほとんど行わず、直接カーネルへプロセス終了を通知します。

```c
#include <unistd.h>

void _exit(int status);
```

`_exit()` は、stdioのflushや `atexit()` 関数の実行を行いません。
そのため、出力バッファに残っている内容は失われることがあります。

```c
printf("hello");
_exit(0);
```

この場合、`hello` が表示されない可能性があります。

一方で、`fork()` 後の子プロセスで `exec()` に失敗したときや、`vfork()` 後の子プロセスでは `_exit()` が必要になります。

ISO Cでは `_Exit()` も定義されています。

```c
#include <stdlib.h>

void _Exit(int status);
```

考え方としては `_exit()` とほぼ同じで、Cライブラリの通常の終了処理を行わずに終了します。

整理すると、次のようになります。

```text
普通のプログラム終了:
	return 0;
	exit(EXIT_SUCCESS);

fork後の子プロセスでexec失敗:
	_exit(127);

vfork後の子プロセス:
	exec系関数 または _exit()
```

#### ５章の３の３　main() から return する

Cプログラムでは、`main()` から `return` してもプロセスは終了します。

```c
int main(void)
{
	return 0;
}
```

これは、だいたい `exit(0)` に近い意味になります。
`main()` の戻り値が終了ステータスになります。

C99以降では、`main()` の最後に到達した場合は、`return 0;` と同じ扱いになります。
ただ、学習用や明確なコードでは、終了ステータスを明示するほうが分かりやすいです。

```c
int main(void)
{
	/* 処理 */
	return 0;
}
```

Ushのようなシェルでは、外部コマンドの終了ステータスを扱うため、終了コードの意味をかなり意識することになります。

#### ５章の３の４　シグナルによる終了

プロセスは、`exit()` 以外でも終了します。
代表的なのがシグナルによる終了です。

たとえば、次のようなシグナルがあります。

SIGTERM
通常の終了要求です。
`kill` コマンドがデフォルトで送るシグナルです。

SIGKILL
強制終了です。
プロセスは捕捉も無視もできません。

SIGSEGV
不正なメモリアクセス、いわゆるセグメンテーション違反で発生します。

SIGABRT
`abort()` によって送られることが多いシグナルです。

シグナルについては後の章で詳しく扱います。
ここでは、子プロセスの終了状態を調べるときに、「普通に `exit()` したのか」「シグナルで終了したのか」を区別できる、という点を押さえれば大丈夫です。

#### ５章の３の５　atexit()

`atexit()` は、プロセスが通常終了するときに呼び出してほしい関数を登録するための関数です。

```c
#include <stdlib.h>

int atexit(void (*function)(void));
```

登録する関数は、引数も戻り値も持ちません。

```c
void cleanup(void)
{
	/* 終了時の後始末 */
}
```

サンプルです。

```c
#include <stdio.h>
#include <stdlib.h>

static void cleanup(void)
{
	puts("cleanup called");
}

int main(void)
{
	if (atexit(cleanup) != 0) {
		fputs("atexit failed\n", stderr);
		return 1;
	}

	puts("main end");
	return 0;
}
```

このプログラムでは、`main()` から戻るときに `cleanup()` が呼ばれます。

複数の関数を登録した場合、実行順序は登録した順番の逆です。
最後に登録した関数が最初に呼ばれます。

```text
atexit(A)
atexit(B)
atexit(C)

終了時:
	C
	B
	A
```

`exec()` に成功した場合、古いプロセスイメージは消えるため、登録済みの `atexit()` 関数も消えます。
また、シグナルで強制終了した場合は、通常 `atexit()` に登録した関数は実行されません。

`atexit()` に登録した関数の中で `exit()` を呼ぶのは避けるべきです。
終了処理が再帰的になり、ややこしい動作になります。

#### ５章の３の６　on_exit()

glibcには `on_exit()` という関数もあります。

```c
#include <stdlib.h>

int on_exit(void (*function)(int, void *), void *arg);
```

`on_exit()` で登録する関数には、終了ステータスと任意の引数が渡されます。

```c
void handler(int status, void *arg);
```

ただし、`on_exit()` は標準的なCやPOSIXの移植性あるAPIではありません。
Linux/glibcでは使えますが、他のUnix系OSでは使えないことがあります。

現在の学習やUmuOS向けの移植性を考えるなら、基本は `atexit()` を使うと考えておけばよいです。

#### ５章の３の７　SIGCHLD

子プロセスが終了すると、カーネルは親プロセスへ `SIGCHLD` シグナルを送ります。

`SIGCHLD` は、子プロセスの状態が変化したことを知らせるシグナルです。
終了だけでなく、停止や再開でも関係することがあります。

シェルにとって、`SIGCHLD` はかなり重要です。
バックグラウンドジョブが終了したことを知るために使えるからです。

ただし、シグナルは非同期に届きます。
つまり、親プロセスがどの処理をしている最中でも割り込んでくる可能性があります。

そのため、`SIGCHLD` ハンドラの中で複雑な処理をするのは危険です。
実用的には、ハンドラではフラグを立てるだけにして、メインループ側で `waitpid()` する設計などがよく使われます。

シグナルについては9章で詳しく見ます。
ここでは、子プロセスが終了しても、それだけでは親が終了ステータスを受け取ったことにはならない、という点が重要です。

親は `wait()` や `waitpid()` で、子プロセスの終了状態を回収する必要があります。

### ５章の４　子プロセスの終了を待つ

子プロセスが終了したとき、親プロセスはその終了状態を取得できます。
このために使う基本的な関数が `wait()` です。

```c
#include <sys/types.h>
#include <sys/wait.h>

pid_t wait(int *status);
```

`wait()` は、終了した子プロセスがあれば、そのpidを返します。
終了した子プロセスがまだなければ、通常はブロックして待ちます。

`status` が `NULL` でなければ、子プロセスの終了状態が格納されます。
この値はビットを直接読むのではなく、専用のマクロで調べます。

#### ５章の４の１　ゾンビプロセス

子プロセスが終了した瞬間に、カーネルがその情報を完全に消してしまうと、親プロセスは終了ステータスを取得できなくなります。

そこでUnixでは、終了した子プロセスを一時的にゾンビ状態として残します。

ゾンビ状態のプロセスは、もう実行されません。
メモリ空間など大部分のリソースは解放済みです。
しかし、pid、終了ステータス、リソース使用量など、親が回収するために必要な最小限の情報だけが残ります。

親プロセスが `wait()` や `waitpid()` を呼ぶと、その情報を受け取り、カーネルはゾンビを完全に破棄できます。

```text
子プロセス終了
	-> ゾンビ状態になる

親プロセスが wait()
	-> 終了ステータスを受け取る
	-> ゾンビが消える
```

シェルやサーバープログラムが子プロセスを作る場合、終了した子をきちんと回収しないと、ゾンビが溜まります。
Ushでも、外部コマンドを起動したら、フォアグラウンドなら `waitpid()` で待つ、バックグラウンドなら `SIGCHLD` と組み合わせて後で回収する、という設計が必要になります。

親プロセスが子プロセスより先に終了した場合、子プロセスは別の親へ引き取られます。
伝統的にはpid 1のinitが引き取る、と説明されます。
現在のLinuxでは、PID名前空間やsubreaperの仕組みもあり、必ずしもグローバルなpid 1だけとは限りません。
ただし、基本的な理解としては「孤児になったプロセスは適切な親へつなぎ直され、最終的には回収される」と考えればよいです。

#### ５章の４の２　wait() と終了状態マクロ

`wait()` の `status` は、そのまま整数として読むものではありません。
次のようなマクロで確認します。

```c
#include <sys/wait.h>

WIFEXITED(status)
WEXITSTATUS(status)
WIFSIGNALED(status)
WTERMSIG(status)
WIFSTOPPED(status)
WSTOPSIG(status)
WIFCONTINUED(status)
```

よく使うものを整理します。

WIFEXITED(status)
子プロセスが `exit()`、`_exit()`、または `main()` からのreturnで通常終了した場合に真になります。

WEXITSTATUS(status)
`WIFEXITED(status)` が真の場合に、終了ステータスを取り出します。

WIFSIGNALED(status)
子プロセスがシグナルで終了した場合に真になります。

WTERMSIG(status)
`WIFSIGNALED(status)` が真の場合に、終了させたシグナル番号を取り出します。

WIFSTOPPED(status)
子プロセスが停止した場合に真になります。
ジョブ制御やデバッガで重要です。

WIFCONTINUED(status)
停止していた子プロセスが再開した場合に真になります。
こちらもジョブ制御で関係します。

`WCOREDUMP(status)` というマクロも多くのUnix系OSで使えますが、POSIX標準ではありません。
使う場合は、環境によっては `#ifdef WCOREDUMP` で囲むとよいです。

#### ５章の４の３　wait() のサンプル

次のサンプルは、子プロセスを作り、子が終了するのを親が待ち、終了ステータスを表示します。

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_status(int status)
{
	if (WIFEXITED(status)) {
		printf("normal exit: status=%d\n", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		printf("killed by signal: signal=%d", WTERMSIG(status));
#ifdef WCOREDUMP
		if (WCOREDUMP(status)) {
			printf(" (core dumped)");
		}
#endif
		putchar('\n');
	} else if (WIFSTOPPED(status)) {
		printf("stopped by signal: signal=%d\n", WSTOPSIG(status));
#ifdef WIFCONTINUED
	} else if (WIFCONTINUED(status)) {
		puts("continued");
#endif
	}
}

int main(void)
{
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		_exit(42);
	}

	int status;
	pid_t waited;

	do {
		waited = wait(&status);
	} while (waited == -1 && errno == EINTR);

	if (waited == -1) {
		perror("wait");
		return 1;
	}

	printf("child pid=%ld\n", (long)waited);
	print_status(status);

	return 0;
}
```

ここでは、`wait()` が `EINTR` で中断された場合に再試行しています。
シグナルを扱うプログラムでは、このような処理が必要になることがあります。

子プロセス側では `_exit(42)` を使っています。
これは、`fork()` 後の子プロセスで余計なCライブラリ終了処理を避けるためです。

#### ５章の４の４　waitpid()

複数の子プロセスがある場合、任意の子ではなく、特定の子だけを待ちたいことがあります。
その場合は `waitpid()` を使います。

```c
#include <sys/types.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *status, int options);
```

`pid` の指定方法は少し特殊です。

```text
pid > 0
	そのpidの子プロセスを待つ

pid == -1
	任意の子プロセスを待つ
	wait() とほぼ同じ

pid == 0
	呼び出し元と同じプロセスグループに属する子を待つ

pid < -1
	-pid に等しいプロセスグループIDを持つ子を待つ
```

`options` には、主に次のような値を指定できます。

WNOHANG
まだ状態変化した子プロセスがいない場合、ブロックせずに0を返します。

WUNTRACED
停止した子プロセスも報告します。
ジョブ制御で重要です。

WCONTINUED
再開した子プロセスも報告します。
これもジョブ制御で重要です。

`waitpid()` の戻り値は次のようになります。

```text
> 0
	状態変化した子プロセスのpid

0
	WNOHANG指定時、まだ状態変化した子がいない

-1
	エラー
```

#### ５章の４の５　waitpid() のサンプル

特定の子プロセスを待つ例です。

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		char *const argv[] = { "sh", "-c", "exit 7", NULL };
		execvp("sh", argv);
		perror("execvp");
		_exit(127);
	}

	int status;
	pid_t ret;

	do {
		ret = waitpid(pid, &status, 0);
	} while (ret == -1 && errno == EINTR);

	if (ret == -1) {
		perror("waitpid");
		return 1;
	}

	if (WIFEXITED(status)) {
		printf("child exited: %d\n", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		printf("child killed by signal: %d\n", WTERMSIG(status));
	}

	return 0;
}
```

シェルでフォアグラウンドコマンドを実行する場合、基本形はこのようになります。

```text
fork()
子プロセスでexecvp()
親プロセスでwaitpid(child_pid, ...)
終了ステータスを保存
```

バックグラウンドジョブの場合は、親がすぐにプロンプトへ戻るため、`WNOHANG` や `SIGCHLD` と組み合わせて後から回収します。

#### ５章の４の６　waitid()

より細かい情報が必要な場合は `waitid()` もあります。

```c
#include <sys/wait.h>

int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
```

`waitid()` は、`siginfo_t` に詳しい情報を返します。
たとえば、子プロセスのpid、uid、終了理由、終了ステータスまたはシグナル番号などです。

対象の指定には `idtype` と `id` を使います。

```text
P_PID
	指定したpidの子プロセス

P_PGID
	指定したプロセスグループIDの子プロセス

P_ALL
	任意の子プロセス
```

`options` には、`WEXITED`、`WSTOPPED`、`WCONTINUED`、`WNOHANG`、`WNOWAIT` などを指定します。

`WNOWAIT` を指定すると、状態を取得しても子プロセスを回収しません。
つまり、ゾンビ状態のまま残します。
あとで改めて回収する必要があります。

`waitid()` は強力ですが、普通のシェルや学習用コードでは、まず `waitpid()` を使えるようになるのが先でよいと思います。

#### ５章の４の７　wait3() と wait4()

BSD系由来のAPIとして、`wait3()` と `wait4()` もあります。

```c
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t wait3(int *status, int options, struct rusage *rusage);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);
```

これらは、子プロセスの終了状態に加えて、リソース使用量を `struct rusage` で取得できます。

ただし、POSIX標準の中心的なAPIではありません。
移植性を考えるなら、基本は `wait()` / `waitpid()` / 必要なら `waitid()` と覚えておくのがよいです。

リソース使用量を取りたい場合は、Linuxでは `wait4()` や `getrusage()` が関係します。
このあたりは次章のリソース管理ともつながります。

#### ５章の４の８　system()

`system()` は、コマンド文字列をシェルに渡して実行し、その終了を待つ関数です。

```c
#include <stdlib.h>

int system(const char *command);
```

内部的には、おおまかに次のようなことをします。

```text
/bin/sh -c command
```

たとえば次のコードは、シェル経由で `ls -l` を実行します。

```c
#include <stdlib.h>

int main(void)
{
	int status = system("ls -l");
	return status == -1 ? 1 : 0;
}
```

`system()` は手軽ですが、注意点があります。

まず、シェルを経由します。
そのため、ユーザー入力をそのまま文字列連結して渡すと、コマンドインジェクションの危険があります。

```c
/* 危険な例: user_input が "file; rm -rf ..." のような文字列なら問題になる */
system(user_input);
```

外部コマンドを安全に起動したい場合は、`fork()` + `execvp()` のように、引数を配列として渡す形のほうが安全です。

また、`system()` の戻り値は、実行したコマンドの終了コードそのものではなく、`wait()` 系のstatus形式です。
終了コードを取り出すには、`WIFEXITED()` と `WEXITSTATUS()` を使います。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

int main(void)
{
	int status = system("sh -c 'exit 3'");

	if (status == -1) {
		perror("system");
		return 1;
	}

	if (WIFEXITED(status)) {
		printf("exit status=%d\n", WEXITSTATUS(status));
	}

	return 0;
}
```

`system(NULL)` を呼ぶと、シェルが利用可能かどうかを確認できます。

```c
if (system(NULL)) {
	puts("shell is available");
}
```

ただし、Ushを作る立場では、`system()` を使ってコマンド実行を済ませてしまうと、シェルの中身を学べません。
Ushでは、`fork()`、`execvp()`、`waitpid()` を自分で組み合わせるのが本筋です。

#### ５章の４の９　my_system() の実装イメージ

`system()` の仕組みを理解するために、簡単な `my_system()` を考えると勉強になります。
本物の `system()` はシグナルの扱いなどがもっと複雑ですが、基本形は次のようになります。

```c
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int my_system(const char *command)
{
	if (command == NULL) {
		return 1;
	}

	pid_t pid = fork();
	if (pid == -1) {
		return -1;
	}

	if (pid == 0) {
		char *const argv[] = { "sh", "-c", (char *)command, NULL };
		execv("/bin/sh", argv);
		_exit(127);
	}

	int status;

	for (;;) {
		if (waitpid(pid, &status, 0) == -1) {
			if (errno == EINTR) {
				continue;
			}

			return -1;
		}

		break;
	}

	return status;
}
```

この実装では、子プロセスで `/bin/sh -c command` を実行し、親プロセスが `waitpid()` で待っています。

本物の `system()` とは違い、`SIGINT`、`SIGQUIT`、`SIGCHLD` の扱いを省略しています。
そのため、完全な互換実装ではありません。
しかし、`fork()`、`exec()`、`waitpid()` のつながりを見る教材としては分かりやすいです。

#### ５章の４の１０　UmuOSでどう考えるか

UmuOSやUshの視点では、この節はかなり重要です。

シェルが外部コマンドを実行するには、次の処理が必要になります。

```text
1. fork() で子プロセスを作る
2. 子プロセスでリダイレクトやパイプのfdを設定する
3. 子プロセスで execvp() する
4. 親プロセスは必要なら waitpid() する
5. 終了ステータスを保存する
6. バックグラウンドジョブなら後で回収する
```

フォアグラウンド実行なら、親シェルは子プロセスの終了を待ちます。
バックグラウンド実行なら、親シェルはすぐにプロンプトへ戻り、後で `SIGCHLD` や `waitpid(..., WNOHANG)` を使って終了した子を回収します。

UmuOSで最初に実装するなら、まずはフォアグラウンド実行だけで十分です。

```text
最初のUsh:
	コマンドを読む
	fork()
	子でexec()
	親でwaitpid()
```

これが動くと、シェルとして一気にそれらしくなります。

その次に、リダイレクト、パイプ、バックグラウンド実行、ジョブ制御へ進むのがよさそうです。

