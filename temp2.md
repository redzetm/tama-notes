3 章
システムプログラミング
本章ではシステムプログラミングに必要な概念、知識を解説します。まず取り上げるのはシステ
ムコールです。その動作を順を追って細部まで解説します。次にライブラリ関数を取り上げ、シス
テムコールとの差異を述べ、さらに（GNU）Cライブラリへと進みます。
システムコールやライブラリ関数を実行した場合は、正常終了/異常終了を表す戻り値を必ず確認
します。この確認方法、および本書の大半のサンプルプログラムが使用するエラー処理関数につい
ても述べます。
章末では、可搬性を向上させるプログラミング、特に機能検査マクロの利用とSUSv3が定義する
標準システムデータ型を解説します。
3.1 システムコール
システムコールとはある方式により管理されたカーネルへの入口です。プロセスはシステムコー
ルによりカーネルに対しなんらかの処理を代行するよう依頼します。カーネルはシステムコールア
プリケーションプログラミングインタフェース（API）としてさまざまな機能をプログラムに提供し
ます。新規プロセスの作成、I/Oの実行、プロセス間通信用にパイプの作成などです（syscalls(2)
のマニュアルページにはLinuxシステムコールの一覧があります）。
システムコールの詳細へ踏み込む前に、一般的な要点をまとめておきます。
• システムコールはプロセスの状態をユーザモードからカーネルモードへ移行させる。その結
果、CPUは保護されたカーネルメモリへもアクセス可能となる。
• システムコールセットは固定されており、すべてのシステムコールには一意な番号が割り振
られている（この番号は通常は参照できず、プログラムからはシステムコール名を使用する）。
• システムコールは必要な情報をユーザ空間（プロセスの仮想アドレス空間）から得るための、
またカーネル空間からユーザ空間へ情報を返すための引数を実装できる。
プログラミング上の観点からは、システムコールの実行は一般的C関数コールとほとんど差異が
ありませんが、内部では多くの処理が実行されます。x86-32を例にこの内部処理を1ステップずつ
挙げます。
46
｜3章システムプログラミング
1. アプリケーションプログラムがCライブラリ内のラッパ関数経由でシステムコールを実行
する。
2. ラッパ関数は、システムコールに必要な引数をシステムコールトラップルーチン（後述）へ
渡す。アプリケーションからラッパ関数へ引数を渡す際にはスタックを使用するが、カーネ
ルへは特定のレジスタ経由で引数を渡す。このためラッパ関数は引数をレジスタへ代入する。
3. システムコールがカーネルへ入る方法はすべて共通のため、カーネル側からはシステムコー
ルを識別する方法が必要になる。このため、ラッパ関数はシステムコール番号を特定のレジ
スタへ代入する（%eax）。
4. ラッパ関数はトラップ命令を実行し（int 0x80）、プロセッサをユーザモードからカーネル
モードへ移行させ、システムトラップベクタの0x80（10進数では128）の位置にあるコード
を実行する。
比較的新しいx86-32アーキテクチャではsysenter命令を実装しており、従来の
int 0x80 トラップ命令よりも高速にカーネルモードへ移行できる。Linuxでは
カーネルバージョン2.6およびglibcバージョン2.3.2より、sysenter命令を使
用している。
5. 0x80へのトラップにより、カーネルは内部のsystem_call()ルーチンをコールする（アセ
ンブラファイルarch/x86/kernel/entry.Sが定義するプロシージャ）。system_call()
は次の処理を実行する。
a）レジスタをカーネルスタックへ退避する（「6.5 スタックとスタックフレーム」）。
b）システムコール番号を確認する。
c）全システムコールサービスルーチンの一覧表（カーネル内の変数sys_call_table）か
らシステムコール番号に対応するシステムコールサービスルーチンを特定し、これを実
行する。システムコールサービスルーチンが引数を必要とする場合は、まず引数の有効
性を確認する。例えば、ユーザメモリ空間内で正当なアドレスか否かなどである。サー
ビスルーチンは要求された処理を実行する。引数に指定されたアドレスの値を変更する
場合や、ユーザメモリからカーネルメモリへデータを転送する場合もある（I/O処理な
ど）。処理終了後にサービスルーチンは戻り値をsystem_call()ルーチンへ返す。
d）カーネルスタックへ退避しておいたレジスタを復元し、システムコールサービスルーチ
ンからの戻り値をスタックへ置く。
e）ラッパ関数へリターンする。同時にプロセスはユーザモードへ復帰する。
6. システムコールサービスルーチンからの戻り値がエラーを表す場合は、ラッパ関数はその値
をグローバル変数errnoへ代入する（「3.4 システムコールとライブラリ関数のエラー」）。
ラッパ関数はシステムコールの正常終了、異常終了を表す値を戻り値とし、コール元へリター
ンする。
Linux のシステムコールサービスルーチンには正常終了時の戻り値は非負とするという規
約があります。エラーの場合はエラーを表す定数（errnoへ代入される値）を負数に符号
変換した値を返します。Cライブラリ内のラッパ関数はシステムコールサービスルーチン
の戻り値が負数の場合は、これを正数へ符号変換しerrnoへ代入します。エラーを表す
3.1 システムコール｜ 47
ラッパ関数の戻り値は1です。
この符号変換は、システムコールサービスルーチンは正常終了時には負数を返さないとい
う前述の規約の上に成り立ちますが、中にはこの規約に沿わないシステムコールサービス
ルーチンも存在します。しかし、正常な負数の戻り値は符号変換したerrnoと値が重な
らないため、通常は問題になりません。唯一問題になるのはfcntl()システムコールの
F_GETOWN 操作です。詳細は「63.3 シグナルドリブンI/O」で解説します。
execve() システムコールを例に、上記処理内容を図3-1に示します。
ユーザモード
アプリケーション
プログラム
glibcラッパ関数
 (sysdeps/unix/sysv/linux/execve.c)
...
execve(path,
    argv, envp);
...
execve(path, argv, envp)
{
  ...
  int 0x80
}
(引数 :  __NR_execve,
                     path, argv, envp)
  ...
  return;
カーネルモード
システムコール
サービスルーチン
(arch/x86/kernel/
process.c)
sys_execve()
{
  ...
trapハンドラ
(arch/x86/kernel/entry_32.S)
system_call:
  ...
  call sys_call_table
        [__NR_execve]
  ...
  return error;
}
図3-1 システムコールの内部処理
カーネルモードへ移行
ユーザモードへ移行
Linux/x86-32 の execve() のシステムコール番号は 11 であり（__NR_execve）、sys_call_
table 内の11番目のエントリが、このシステムコールのサービスルーチンであるsys_execve()
を指します（Linuxではシステムコールサービスルーチンの名前はsys_xyz()の形式を採ります。
ここでxyzが対象のシステムコール名です）。
本書の内容を理解するのに図3-1の詳細は必ずしも必要ではありません。この図で重要なこと
は、単純なシステムコールでも内部では多くの処理が実行され、このためシステムコールには、大
48
｜3章システムプログラミング
きくはないものの、必ずオーバヘッドを伴うという点です。
システムコールのオーバヘッドの例として、getppid()システムコールを考えてみましょ
う。親プロセスのIDを返す単純なシステムコールです。手元のLinux2.6.25x86-32シス
テムでgetppid()を1000万回実行すると、およそ2.2秒かかりました。平均して1シス
テムコールあたり0.3マイクロ秒かかったことになります。しかし同じシステムで、整数
を返すだけの単純なC関数をやはり1000万回実行しても0.11秒しかかかりませんでし
た。およそ1/12です。言うまでもないことですが、ほとんどのシステムコールの処理量は
getppid() よりもずっと多いのです。
C言語プログラムから見ればCライブラリ内のラッパ関数のコールが、システムコールサービス
ルーチンの実行と同義であるため、本書で「システムコールを実行する」と表現した場合は「内部
でシステムコールxyz()を実行するラッパ関数をコールする」を意味します。
「付録A システムコールのトレース」では、デバッグまたは単にプログラムの動作を観察する際
に、プログラムが実行するシステムコールをトレースするstraceコマンドを解説します。
Linuxシステムコールの仕組みに関する詳細は［Love-2010］、［BovetandCesati-2005］、［Maxwell
1999］を参照してください。
3.2 ライブラリ関数
ライブラリ関数とは標準Cライブラリを構成する多数の関数の1つを意味します（本書では特定
のライブラリ関数を述べる際は、ライブラリ関数と表現せず単に関数と表現します）。ライブラリ関
数の機能はファイルをオープンする、時刻を可読形式へ変換する、2つの文字列を比較するなど、非
常に多岐に渡ります。
内部でシステムコールを実行しないライブラリ関数も多数あります（文字列操作関数など）。しか
し、一部のライブラリ関数はシステムコールを実行するためのものです。例えば、fopen()ライブ
ラリ関数は内部でopen()システムコールを実行し、実際にファイルをオープンします。ライブラ
リ関数を設計する際には、システムコールよりも使用しやすいインタフェースが考慮されることが
多くあり、例えばprintf()関数は内部でwrite()システムコールを実行しますが、出力フォー
マットを指定でき、また内部でデータをバッファリングします（write()システムコールだけでは
単純なデータブロックの出力しかできません）。同様に、malloc()、free()関数も、内部で実行
するbrk()システムコールに加え、メモリ割り当て/解放を簡便に実行するための仕組みを実装し
ています。
3.3 標準Cライブラリ：GNUCライブラリ（glibc）
さまざまなUNIXシステムが存在するように、標準Cライブラリの実装も1つだけではありま
せん。Linuxでもっとも広く使用されるのはGNUCライブラリです（glibc、http://www.gnu.
org/software/libc/）。
GNUC ライブラリの主要開発者、メンテナは元々RolandMcGrathでしたが、近年は
Ulrich Drepper が引き継いでいます。
3.3 標準Cライブラリ：GNUCライブラリ（glibc）｜ 49
Linux で使用できるCライブラリは他にもあり、組み込みデバイスのアプリケーション
用に必要メモリ量が少ないuClibc（http://www.uclibc.org/）、diet libc（http:
//www.fefe.de/dietlibc/）などがあります。本書では Linux でアプリケーションを
開発する際にもっとも使用されるglibcを対象とします。
システムにインストールされているglibcのバージョン
それほど頻繁ではありませんが、システムにインストールされているglibcのバージョンを確認
しなければならない場合があります。シェルからはglibc共有ライブラリファイルをプログラムと
して実行するとバージョンを確認できます。実行形式としてglibcライブラリを実行するとバー
ジョン番号を含む次のような情報が表示されます。
$ /lib/libc.so.6
GNU C Library stable release version 2.10.1, by Roland McGrath et al.
Copyright (C) 2009 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC version 4.4.0 20090506 (Red Hat 4.4.0-4).
Compiled on a Linux >>2.6.18-128.4.1.el5<< system on 2009-08-19.
Available extensions:
The C stubs add-on version 2.1.2.
crypt add-on version 2.1 by Michael Glad and others
GNU Libidn by Simon Josefsson
Native POSIX Threads Library by Ulrich Drepper et al
BIND-8.2.3-T5B
RT using linux kernel aio
For bug reporting instructions, please see:
<http://www.gnu.org/software/libc/bugs.html>.
Linux ディストリビューションには/lib/libc.so.6以外のパスへGNUCライブラリをイン
ストールするものもあります。ライブラリパスを確認する方法にはlddコマンドがあります（list
dynamic dependencies）。glibc とリンクされたプログラムをパラメータに与えlddコマンドを
実行すると（ほとんどのプログラムがglibcとリンクされています）、そのプログラムが依存する
ライブラリの一覧を表示し、glibc共有ライブラリのパスも表示されます。
$ ldd myprog | grep libc
libc.so.6 => /lib/tls/libc.so.6 (0x4004b000)
アプリケーションプログラムから、システムにインストールされているGNUCライブラリのバー
ジョンを確認する方法は2つあります。定義済みの定数を検査する方法とライブラリ関数コールに
より検査する方法です。バージョン2.0以降のglibcでは__GLIBC__と__GLIBC_MINOR__とい
う定数を2つ定義しており、コンパイル時に検査できます（#ifなど）。例えばglibc2.12をイン
ストールしてある場合は定数の値はそれぞれ2、12になります。しかし、この方法はプログラムを
50
｜3章システムプログラミング
コンパイルしたシステムでしか通用せず、異なるバージョンのglibcがインストールされたシステ
ム上でコンパイル済みプログラムを実行する場合に不具合が生じる恐れがあります。この問題を解
決するには、プログラムからgnu_get_libc_version()を実行し、実行時のglibcのバージョ
ンを取得します。
#include <gnu/libc-version.h>
const char *gnu_get_libc_version(void);
内部でスタティックに割り当てられ、NULLで終端する、
GNUCライブラリのバージョン番号の文字列を返す
gnu_get_libc_version() は "2.12" のような文字列へのポインタを返します。
方法はもう1つあります。コンフィギュレーション変数の値を取得するconfstr()へ、
glibc固有の_CS_GNU_LIBC_VERSIONを指定してもバージョン情報を得られます。この
場合は"glibc 2.12"のような文字列が返されます。
3.4 システムコールとライブラリ関数のエラー
システムコール、ライブラリ関数が返す戻り値は、正常終了/異常終了を問わず、ほぼすべての場
合で共通です。戻り値は常に検査すべきものですし、エラーを表す場合は対応が必要です。少なく
ともエラーメッセージや警告を表示するべきでしょう。
キー入力時間を節約するなどの理由からエラー検査を省略したくなることもありますが（特にエ
ラー検査を省略したサンプルプログラムを見た後では）、誤った節約です。「エラーを返すことなど
ないだろう」などと誤った思い込みからエラー検査を怠ると、デバッグに何時間もかかってしまい
ます（節約したキー入力時間の何倍ものつけを払うことになります）。
決してエラーとなることがないシステムコールもごく一部に存在します。getpid()は常
に成功し自プロセスIDを返しますし、_exit()は常にプロセスを終了させます。このよ
うなシステムコールの戻り値についてはエラーを検査する必要はありません。
システムコールのエラー
システムコールのマニュアルページでは発生する可能性があるエラーを、その戻り値とともに記
載しています。エラーを表す戻り値は通常1です。次のようなコーディングによりエラーを検査
できます。
fd = open(pathname, flags, mode);
/* system call to open a file */
if (fd ==-1) {
/* Code to handle the error */
ファイルをオープンするシステムコール
エラー処理
}
...
if (close(fd) ==-1) {
/* Code to handle the error */
エラー処理
}
3.4 システムコールとライブラリ関数のエラー｜ 51
システムコールでエラーが発生した場合は、グローバル変数errnoへその原因を表す正の整数が
代入されます。また、ヘッダファイル<errno.h>をインクルードすると、errnoおよびエラーの
値を定義した定数が宣言されます。定数名はいずれもEから始まり、システムコールのマニュアル
ページのエラー（ERRORS）のセクションでは、そのシステムコールが代入する可能性があるerrno
をすべて挙げています。errnoを用いたエラー検査の例を挙げます。
cnt = read(fd, buf, numbytes);
if (cnt ==-1) {
if (errno == EINTR)
fprintf(stderr, "read was interrupted by a signal\n");
else {
/* Some other error occurred */
他のエラー
}
}
システムコール、ライブラリ関数が正常終了してもerrnoは0へリセットされません。そのため
以前のエラーの値を保持したままかもしれません。さらにSUSv3では正常終了の場合でもerrno
へ0以外の値を代入することを許容しています（実際にそのように動作する関数はほとんどありま
せんが）。このため、エラーを検査する場合は、まず戻り値がエラーを表すかを確認し、エラーの場
合にのみerrnoを参照し、原因を特定します。
getpriority() などごく一部のシステムコールは正常終了の場合でも1を返します。この場
合エラーか否かを確認するには、システムコール実行前にerrnoへ0を代入しておき、実行後に
errno の変化を検査します。システムコールが1を返し、errnoが0以外ならばエラーを意味し
ます（一部のライブラリ関数も正常終了時に1を返すため、同様の方法をとります）。
エラー発生時にはエラーメッセージに加え、errnoの値を表示することもよくあります。この表
示にはperror()やstrerror()ライブラリ関数が使用できます。
perror() は引数のmsgに続き、現在のerrnoの値に対応するエラーメッセージ文字列を表示
します。
#include <stdio.h>
void perror(const char *msg);
システムコールのエラー対応の単純な例を挙げます。
fd = open(pathname, flags, mode);
if (fd ==-1) {
perror("open");
exit(EXIT_FAILURE);
}
strerror()はerrnumに指定されたエラー番号に対応するエラーメッセージ文字列を返します。
#include <string.h>
char *strerror(int errnum);
errnum に対応するエラーメッセージ文字列へのポインタを返す
52
｜3章システムプログラミング
strerror() が返す文字列は内部でスタティックに割り当てられている場合があり、この場合以
降のstrerror()実行により内容は上書きされます。
errnum が認識できないエラー番号の場合は、Unknown error nnnという文字列を返します*1。
戻り値をNULLとするシステムもあります。
perror()、strerror() はlocale（ローケール）に対応しているため（「10.4 locale」）、表示さ
れるエラーメッセージ文字列の言語は環境により変化します。
ライブラリ関数のエラー
ライブラリ関数の戻り値の型はさまざまです。エラーを表す値も多種多様です（それぞれのマ
ニュアルページを眺めてみてください）。本書の観点からはライブラリ関数を次のように分類でき
ます。
• システムコールと同様の方法でエラーを表すもの。
すなわち、戻り値を1とし、原因をerrnoへ代入する。ファイル、ディレクトリを削除す
るremove()（内部でunlink()システムコールまたはrmdir()システムコールを実行す
る）などが該当する。システムコールの場合と同様にエラー処理可能である。
• 戻り値は1以外だが、やはりerrnoへエラー原因を代入するもの。
例えばfopen()はエラー時にはNULLを返し、内部で実行したシステムコールが代入した
errno は維持する。エラーメッセージの表示にはperror()およびstrerror()がそのま
ま使用可能である。
• 他のライブラリ関数はerrnoを一切使用しない。
この場合のエラー検査には専用の関数があり、マニュアルページに明記されている。この種
のライブラリ関数の場合には、errno、perror()、strerror()を使用できない。
3.5 本書のサンプルプログラムについて
本節では、本書に提示するサンプルプログラムに共通する機能や、規約に相当する決め事につい
て述べます。
3.5.1 コマンドラインオプションとパラメータ
サンプルプログラムの多くはコマンドラインオプション、パラメータにより動作が変化します。
伝統的にUNIXのコマンドラインオプションはハイフンと英数字1文字の形式を採ります。パ
ラメータを取るオプションもあります（GNUユーティリティは2つのハイフンから始まるlong
option にも対応しており、この場合英数字1文字ではなく英単語を使用します）。コマンドライン
オプションの解析には標準のgetopt()ライブラリ関数を用います（「付録B コマンドラインの解
析」で解説します）。
*1 訳者注：日本語環境では「未知のエラーnnn」。
3.5 本書のサンプルプログラムについて｜ 53
コマンドライン構文が単純ではないサンプルプログラムには、読者の便宜を図り、簡単なヘルプ
機能も実装してあります。--helpオプションを与えると、コマンドラインオプション、パラメータ
の説明をプログラムが表示します。
3.5.2 共通関数とヘッダファイル
サンプルプログラムの多くは、必要とする宣言、定義が共通しており、これをまとめたヘッダファ
イルをインクルードします。本節では共通ヘッダファイル、共通関数について述べます。
共通ヘッダファイル
ほとんどのサンプルプログラムがインクルードする共通ヘッダファイルを例3-1に挙げます。
例3-1 サンプルプログラム共通ヘッダファイル
#ifndef TLPI_HDR_H
#define TLPI_HDR_H
/* Prevent accidental double inclusion */*2
lib/tlpi_hdr.h
#include <sys/types.h> /* Type definitions used by many programs */
#include <stdio.h>
/* Standard I/O functions */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "get_num.h"
/* Prototypes of commonly used library functions,
plus EXIT_SUCCESS and EXIT_FAILURE constants */
/* Prototypes for many system calls */
/* Declares errno and defines error constants */
/* Commonly used string-handling functions */
/* Declares our functions for handling numeric
arguments (getInt(), getLong()) */
#include "error_functions.h" /* Declares our error-handling functions */
typedef enum { FALSE, TRUE } Boolean;
#define min(m,n) ((m) < (n) ? (m) : (n))
#define max(m,n) ((m) > (n) ? (m) : (n))
#endif
lib/tlpi_hdr.h
このヘッダファイルでは多くのサンプルプログラムに必要な他のヘッダファイルをインクルード
し、データ型としてBooleanを定義し、2つの数値から最大値、最小値を求めるマクロ*3を定義し
ます。サンプルプログラムを多少なりとも簡略化するためのヘッダファイルです。
*2 訳者注：ソースコードコメント量が多く、日本語訳を挿入すると紙面の都合上かえって見にくくなるため、コメント
の日本語訳は欄外に記載します。
「二重インクルード事故防止」、「型の定義」、「標準I/O関数」、「一般ライブラリ関数のプロトタイプ宣言および
EXIT_SUCCESS、EXIT_FAILURE の定義」、「システムコール全般のプロトタイプ宣言」、「errnoおよびエラー定数の
宣言」、「文字列処理ライブラリ関数の宣言」、「本書の数値パラメータ処理関数の宣言（getInt()、getLong()）」、
「本書のエラー処理関数の宣言」。
*3 訳者注：glibcでもBSD互換の一環として同様のマクロMIN()、MAX()をsys/param.hで定義しています。
54
｜3章システムプログラミング
エラー処理関数
サンプルプログラムではエラー処理を簡略化するため、例3-2に挙げるエラー処理関数を使用し
ます。
例3-2 エラー処理関数の宣言
#ifndef ERROR_FUNCTIONS_H
#define ERROR_FUNCTIONS_H
void errMsg(const char *format, ...);
#ifdef __GNUC__
lib/error_functions.h
/* This macro stops ’gcc-Wall’ complaining that "control reaches
end of non-void function" if we use the following functions to
terminate main() or some other non-void function. */
以下の関数によりmain()など非void関数を終了する場合
’gcc-Wall’が無用の警告を発するのを抑制するマクロ
#define NORETURN __attribute__ ((__noreturn__))
#else
#define NORETURN
#endif
void errExit(const char *format, ...) NORETURN ;
void err_exit(const char *format, ...) NORETURN ;
void errExitEN(int errnum, const char *format, ...) NORETURN ;
void fatal(const char *format, ...) NORETURN ;
void usageErr(const char *format, ...) NORETURN ;
void cmdLineErr(const char *format, ...) NORETURN ;
#endif
lib/error_functions.h
システムコール、ライブラリ関数が返すエラーの処理には本書が定義する errMsg()、
errExit()、err_exit()、errExitEN() を使用します。
#include "tlpi_hdr.h"
void errMsg(const char *format, ...);
void errExit(const char *format, ...);
void err_exit(const char *format, ...);
void errExitEN(int errnum, const char *format, ...);
3.5 本書のサンプルプログラムについて｜ 55
errMsg() はメッセージを標準エラー出力へ出力します。引数はprintf()と同様ですが、末尾
へ自動的に改行文字を付加します。errMsg()は現在のerrnoに応じたEPERMなどのエラー名、
strerror() が返すエラーメッセージ文字列、続いて引数に指定された文字列をフォーマットし出
力します。
errExit() は errMsg() と同様ですが、出力後に exit() を実行するか、または環境変数
EF_DUMPCORE に空文字列以外が設定されていた場合はabort()を実行し、デバッガが使用する
コアダンプファイルを生成し、その後プログラムを終了させます（コアダンプファイルについては
「22.1 コアダンプ」）で解説します。
err_exit() はerrExit()と同様ですが、次の2点が異なります。
• エラーメッセージを出力前に標準出力をフラッシュしない。
• exit()ではなく、_exit()により終了する。すなわち、stdioバッファをフラッシュせず、
またexitハンドラも実行しない。
err_exit() の動作が異なる上記の点が意味するところは、_exit() と exit() の相違点、
stdio バッファ、fork()により作成した子プロセスのexitハンドラを解説する「25章 プロセス
の終了」で明らかになります。今のところは子プロセスを作成する自作ライブラリ関数内でエラー
が発生した場合に、err_exit()は有用であると述べるに留めます。この終了方法は親プロセスか
ら受け継いだ子プロセス内のstdioバッファをフラッシュせず、また親プロセスが設定したexitハ
ンドラを実行しません。
errExitEN() は errExit() と同様ですが、errnoの現在値に対応するエラーメッセージの代
わりに、引数のerrnumに対応するエラーメッセージを出力します（関数名に加えたENはこの違い
を表します）。
本書のPOSIXスレッドAPIを使用するサンプルプログラムでは主にerrExitEN()を使用しま
す。エラー時に1を返す従来のUNIXシステムコールとは異なり、POSIXスレッドAPIはエラー
番号を戻り値とします（通常はerrnoへ代入される値。正常終了時の戻り値は0）。
POSIX スレッドAPIの場合は、次のようにエラーを検査します。
errno = pthread_create(&thread, NULL, func, &arg);
if (errno != 0)
errExit("pthread_create");
しかし、上記の方法では不十分です。マルチスレッドアプリケーションではerrnoはマクロ化さ
れており、代入可能な左辺値を返す関数コールに展開されるためです。このためerrnoの参照はす
べて関数コールに展開されます。errExitEN()を用いると上記と等価な内容を効率良くコーディ
ングできます。
int s;
s = pthread_create(&thread, NULL, func, &arg);
if (s != 0)
errExitEN(s, "pthread_create");
56
｜3章システムプログラミング
C言語の左辺値（lvalue）とはメモリ上のなんらかの領域を意味します。左辺値の代表例は
整数の変数です。演算子も左辺値を採ります。例えば、pをなんらかの領域を指すポイン
タと宣言した場合、*pも左辺値となります。POSIXスレッドAPIではerrnoをスレッド
固有データへのポインタを返す関数に再定義しています（「31.3 スレッド固有データ」を
参照）。
本書では他の種類のエラー処理用にfatal()、usageErr()、cmdLineErr()も定義しています。
#include "tlpi_hdr.h"
void fatal(const char *format, ...);
void usageErr(const char *format, ...);
void cmdLineErr(const char *format, ...);
fatal() は汎用のエラー処理関数です。errnoを代入しないライブラリ関数での異常終了時に
も使用できます。引数はprintf()と同様ですが、出力する文字列の末尾へ自動的に改行文字を付
加します。フォーマットした文字列を標準エラー出力へ出力し、errExit()同様にプログラムを終
了させます。
usageErr() はコマンドラインパラメータに関するエラーを処理します。printf()同様の引数
を取り、"Usage:" という文字列に続けて、フォーマットした文字列を標準エラー出力へ出力し、
exit() によりプログラムを終了させます（サンプルプログラムの中にはusageErr()を拡張する
ものもあり、この場合の関数名はusageError()とします）。
cmdLineErr() は usageErr() と同様ですが、プログラムが指定したコマンドラインパラメー
タエラーに特化した関数です。
本書のエラー処理関数の実装を例3-3に挙げます。
例3-3 サンプルプログラムが使用するエラー処理関数
#include <stdarg.h>
#include "error_functions.h"
#include "tlpi_hdr.h"
#include "ename.c.inc"
lib/error_functions.c
/* Defines ename and MAX_ENAME */
ename およびMAX_ENAMEの定義
#ifdef __GNUC__
__attribute__ ((__noreturn__))
#endif
static void
terminate(Boolean useExit3)
{
char *s;
/* Dump core if EF_DUMPCORE environment variable is defined and
is a nonempty string; otherwise call exit(3) or _exit(2),
depending on the value of ’useExit3’. */
環境変数EF_DUMPCOREを空文字以外に設定した場合はコアダンプファイル
を生成する。それ以外の場合はexit(3)または_exit(2)により終了する。
どちらを実行するかは’useExit3’により決定する。
3.5 本書のサンプルプログラムについて｜ 57
s = getenv("EF_DUMPCORE");
if (s != NULL && *s != ’\0’)
abort();
else if (useExit3)
exit(EXIT_FAILURE);
else
_exit(EXIT_FAILURE);
}
static void
outputError(Boolean useErr, int err, Boolean flushStdout,
const char *format, va_list ap)
{
#define BUF_SIZE 500
char buf[BUF_SIZE], userMsg[BUF_SIZE], errText[BUF_SIZE];
vsnprintf(userMsg, BUF_SIZE, format, ap);
if (useErr)
snprintf(errText, BUF_SIZE, " [%s %s]",
(err > 0 && err <= MAX_ENAME) ?
ename[err] : "?UNKNOWN?", strerror(err));
else
snprintf(errText, BUF_SIZE, ":");
snprintf(buf, BUF_SIZE, "ERROR%s %s\n", errText, userMsg);
if (flushStdout)
fflush(stdout);
fputs(buf, stderr);
fflush(stderr);
/* Flush any pending stdout */
stdout のフラッシュ
/* In case stderr is not line-buffered */
}
void
errMsg(const char *format, ...)
{
va_list argList;
int savedErrno;
savedErrno = errno;
va_start(argList, format);
stderr が行バッファリングでない場合に備える
/* In case we change it here */
本関数がerrnoを上書きする恐れがあるため
outputError(TRUE, errno, TRUE, format, argList);
va_end(argList);
errno = savedErrno;
}
58
｜3章システムプログラミング
void
errExit(const char *format, ...)
{
va_list argList;
va_start(argList, format);
outputError(TRUE, errno, TRUE, format, argList);
va_end(argList);
terminate(TRUE);
}
void
err_exit(const char *format, ...)
{
va_list argList;
va_start(argList, format);
outputError(TRUE, errno, FALSE, format, argList);
va_end(argList);
terminate(FALSE);
}
void
errExitEN(int errnum, const char *format, ...)
{
va_list argList;
va_start(argList, format);
outputError(TRUE, errnum, TRUE, format, argList);
va_end(argList);
terminate(TRUE);
}
void
fatal(const char *format, ...)
{
va_list argList;%
va_start(argList, format);
outputError(FALSE, 0, TRUE, format, argList);
va_end(argList);
terminate(TRUE);
}
3.5 本書のサンプルプログラムについて｜ 59
void
usageErr(const char *format, ...)
{
va_list argList;
fflush(stdout);
/* Flush any pending stdout */
fprintf(stderr, "Usage: ");
va_start(argList, format);
vfprintf(stderr, format, argList);
va_end(argList);
fflush(stderr);
stdout のフラッシュ
/* In case stderr is not line-buffered */
exit(EXIT_FAILURE);
}
void
cmdLineErr(const char *format, ...)
{
va_list argList;
fflush(stdout);
stderr が行バッファリングでない場合に備える
/* Flush any pending stdout */
fprintf(stderr, "Command-line usage error: ");
va_start(argList, format);
vfprintf(stderr, format, argList);
va_end(argList);
fflush(stderr);
stdout のフラッシュ
/* In case stderr is not line-buffered */
exit(EXIT_FAILURE);
}
stderr が行バッファリングでない場合に備える
lib/error_functions.c
例3-3がインクルードするファイルename.c.incを例3-4に挙げます。
例3-4 Linuxのエラー名（x86-32の場合）
static char *ename[] = {
/*
0 */ "",
/*
lib/ename.c.inc
1 */ "EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", "E2BIG",
/*
8 */ "ENOEXEC", "EBADF", "ECHILD", "EAGAIN/EWOULDBLOCK", "ENOMEM",
/* 13 */ "EACCES", "EFAULT", "ENOTBLK", "EBUSY", "EEXIST", "EXDEV",
/* 19 */ "ENODEV", "ENOTDIR", "EISDIR", "EINVAL", "ENFILE", "EMFILE",
/* 25 */ "ENOTTY", "ETXTBSY", "EFBIG", "ENOSPC", "ESPIPE", "EROFS",
/* 31 */ "EMLINK", "EPIPE", "EDOM", "ERANGE", "EDEADLK/EDEADLOCK",
/* 36 */ "ENAMETOOLONG", "ENOLCK", "ENOSYS", "ENOTEMPTY", "ELOOP", "",
/* 42 */ "ENOMSG", "EIDRM", "ECHRNG", "EL2NSYNC", "EL3HLT", "EL3RST",
/* 48 */ "ELNRNG", "EUNATCH", "ENOCSI", "EL2HLT", "EBADE", "EBADR",
/* 54 */ "EXFULL", "ENOANO", "EBADRQC", "EBADSLT", "", "EBFONT", "ENOSTR",
/* 61 */ "ENODATA", "ETIME", "ENOSR", "ENONET", "ENOPKG", "EREMOTE",
60
｜3章システムプログラミング
/* 67 */ "ENOLINK", "EADV", "ESRMNT", "ECOMM", "EPROTO", "EMULTIHOP",
/* 73 */ "EDOTDOT", "EBADMSG", "EOVERFLOW", "ENOTUNIQ", "EBADFD",
/* 78 */ "EREMCHG", "ELIBACC", "ELIBBAD", "ELIBSCN", "ELIBMAX",
/* 83 */ "ELIBEXEC", "EILSEQ", "ERESTART", "ESTRPIPE", "EUSERS",
/* 88 */ "ENOTSOCK", "EDESTADDRREQ", "EMSGSIZE", "EPROTOTYPE",
/* 92 */ "ENOPROTOOPT", "EPROTONOSUPPORT", "ESOCKTNOSUPPORT",
/* 95 */ "EOPNOTSUPP/ENOTSUP", "EPFNOSUPPORT", "EAFNOSUPPORT",
/* 98 */ "EADDRINUSE", "EADDRNOTAVAIL", "ENETDOWN", "ENETUNREACH",
/* 102 */ "ENETRESET", "ECONNABORTED", "ECONNRESET", "ENOBUFS", "EISCONN",
/* 107 */ "ENOTCONN", "ESHUTDOWN", "ETOOMANYREFS", "ETIMEDOUT",
/* 111 */ "ECONNREFUSED", "EHOSTDOWN", "EHOSTUNREACH", "EALREADY",
/* 115 */ "EINPROGRESS", "ESTALE", "EUCLEAN", "ENOTNAM", "ENAVAIL",
/* 120 */ "EISNAM", "EREMOTEIO", "EDQUOT", "ENOMEDIUM", "EMEDIUMTYPE",
/* 125 */ "ECANCELED", "ENOKEY", "EKEYEXPIRED", "EKEYREVOKED",
/* 129 */ "EKEYREJECTED", "EOWNERDEAD", "ENOTRECOVERABLE", "ERFKILL"
};
#define MAX_ENAME 132
lib/ename.c.inc
ename.c.inc は文字列配列enameを定義し、各要素がerrnoへ代入され得る値の名前を表し
ます。本書のエラー処理関数ではこの配列を用い、エラー番号に対応するエラー名を出力します。
この実装は苦肉の策とも言えます。strerror()が返す文字列にはエラー名を含まない一方で、マ
ニュアルページが記載するエラーではすべてエラー名を使用しているためです。エラー名も出力す
ることで、マニュアルページに記載されたエラー原因を特定しやすくなります。
ename.c.inc の内容はアーキテクチャに依存します。errnoの値はLinuxハードウェア
アーキテクチャごとに若干の差異があるためです。例3-4に挙げたものはLinux2.6/x86
32 システム用です。また、ename.c.incは本書のウェブサイトで公開しているソース
コード一式に含めてあるスクリプト（lib/Build_ename.sh）により自動的に生成しまし
た。このスクリプトを使用すれば、他のハードウェアプラットフォーム、異なるカーネル
バージョン用のename.c.incも生成可能です。
配列ename中、空文字列がいくつかある点に注意してください。これは未使用のエラー番号が
存在するためです。さらにスラッシュで区切った2つのエラーを表す文字列もあります。1つのエ
ラー番号に2つのエラー名を定義してあるためです。
ename.c.inc では EAGAIN と EWOULDBLOCK が同じエラー番号となっています（SUSv3
では明示的にこの重複を認めており、実際、すべてではありませんが多くのUNIXシステ
ムでこの2つの値は同じです）。このエラーは、通常ならばブロック（処理の完了を必ず待
つ動作）するシステムコールへ、ブロックせずにその場でエラーを返すようにコール側が
明示的に指定した場合に返されます。EAGAINはSystemV発祥のエラー名で、I/O、セマ
フォ操作、メッセージキュー操作、ファイルロック（fcntl()）のシステムコールが返す
エラーです。EWOULDBLOCKはBSD発祥のエラー名で、ファイルロック（flock()）、ソ
ケット操作のシステムコールが返すエラーです。
SUSv3 がEWOULDBLOCK について定義しているのは、ソケット関連のインタフェースにつ
3.5 本書のサンプルプログラムについて｜ 61
いてのみです。ノンブロッキングコールの場合にはEAGAINまたはEWOULDBLOCKを返す
ことを認めていますが、ソケット以外のノンブロッキングコールではEAGAINを返すと定
義しています。
コマンドライン整数パラメータ解析関数
本書のサンプルプログラムでは例3-5のヘッダファイルが宣言する、コマンドラインに与えら
れた整数を解析する2関数getInt()、getLong()を頻繁に使用します。上記2関数が標準の
atoi()、atol()、strtol() よりも勝る点は、引数の有効性を確認することです。
#include "tlpi_hdr.h"
int getInt(const char *arg, int flags, const char *name);
long getLong(const char *arg, int flags, const char *name);
いずれもargを数値へ変換し返す
getInt() およびgetLong()はargに指定された文字列をそれぞれint、longへ変換します。
arg が無効な文字列の場合は（数字、+、以外の文字を含む）、エラーメッセージを表示しプログ
ラムを終了させます。
name にはNULL、またはargを特定するための文字列を指定し、getInt()、getLong()が表示
するエラーメッセージの一部となります。
flagsにはgetInt()、getLong()の動作を制御するフラグを指定します。ディフォルトでは符
号付き10進数の文字列を処理しますが、例3-5が定義するGN_*マクロをOR演算（｜）し、flags
へ指定すると、基数を変更できます。正数または自然数に制限することも可能です。
例3-5 get_num.c用のヘッダファイル
#ifndef GET_NUM_H
#define GET_NUM_H
#define GN_NONNEG
#define GN_GT_0
01
02
/* Value must be >= 0 */*4
/* Value must be > 0 */
lib/get_num.h
/* By default, integers are decimal */
#define GN_ANY_BASE 0100
#define GN_BASE_8
#define GN_BASE_16
0200
0400
/* Can use any base- like strtol(3) */
/* Value is expressed in octal */
/* Value is expressed in hexadecimal */
long getLong(const char *arg, int flags, const char *name);
*4 訳者注：ソースコードコメント量が多く、日本語訳を挿入すると紙面の都合上かえって見にくくなるため、コメント
の日本語訳は欄外に記載します。
「自然数に制限する」、「正数に制限する」、「ディフォルトでは10進数表記」、「strtol(3)同様に任意の基数に対
応」、「8進数表記」、「16進数表記」。
62
｜3章システムプログラミング
int getInt(const char *arg, int flags, const char *name);
#endif
getInt()、getLong() の実装を例3-6に挙げます。
例3-6 コマンドライン整数パラメータ解析関数
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include "get_num.h"
static void
lib/get_num.h
lib/get_num.c
gnFail(const char *fname, const char *msg, const char *arg, const char *name)
{
fprintf(stderr, "%s error", fname);
if (name != NULL)
fprintf(stderr, " (in %s)", name);
fprintf(stderr, ": %s\n", msg);
if (arg != NULL && *arg != ’\0’)
fprintf(stderr, "
offending text: %s\n", arg);
exit(EXIT_FAILURE);
}
static long
getNum(const char *fname, const char *arg, int flags, const char *name)
{
long res;
char *endptr;
int base;
if (arg == NULL || *arg == ’\0’)
gnFail(fname, "null or empty string", arg, name);
base = (flags & GN_ANY_BASE) ? 0 : (flags & GN_BASE_8) ? 8 :
(flags & GN_BASE_16) ? 16 : 10;
errno = 0;
res = strtol(arg, &endptr, base);
if (errno != 0)
gnFail(fname, "strtol() failed", arg, name);
if (*endptr != ’\0’)
gnFail(fname, "nonnumeric characters", arg, name);
3.6 可搬性｜ 63
if ((flags & GN_NONNEG) && res < 0)
gnFail(fname, "negative value not allowed", arg, name);
if ((flags & GN_GT_0) && res <= 0)
gnFail(fname, "value must be > 0", arg, name);
return res;
}
long
getLong(const char *arg, int flags, const char *name)
{
return getNum("getLong", arg, flags, name);
}
int
getInt(const char *arg, int flags, const char *name)
{
long res;
res = getNum("getInt", arg, flags, name);
if (res > INT_MAX || res < INT_MIN)
gnFail("getInt", "integer out of range", arg, name);
return (int) res;
}
lib/get_num.c
本文で述べたようにflagsにより値の範囲を制限することは可能ですが、サンプルプログ
ラムでは制限することが論理的に妥当でも、この機能を使用しないこともあります。例え
ば、例47-1では引数のinit-valueを検査しません。すなわち、ユーザはセマフォの初期
値として負数も指定可能です。しかし、セマフォは負数になることがないため、semctl()
システムコールがエラーを返します（ERANGE）。このようにパラメータの値の検査を省略
することで、システムコールやライブラリの正常時以外の動作、すなわち不正な引数を指
定した場合の動作も確認できるようにしています。現実のアプリケーションではコマンド
ラインパラメータをもっと厳密に検査するのが通例です。
3.6 可搬性
本節では可搬性を備えたシステムプログラム開発を考察します。まずSUSv3が定義する機能検査
マクロ（featuretestmacro）および標準システムデータ型を解説し、その後可搬性に関する留意点
を解説します。
64
｜3章システムプログラミング
3.6.1 機能検査マクロ
さまざまな標準仕様がシステムコール、ライブラリ関数の動作を定義しています（「1.3 標準化」
を参照）。TheOpenGroup（SingleUNIXSpecification）から派生した標準仕様もあれば、UNIX
システムの歴史上重要な2つのシステム、BSDとSystemV Release4（およびSystemV Interface
Definition）から派生したものもあります。
可搬性を意識したアプリケーションを開発する際に、特定の標準仕様のみが定義する定数、関数、
プロトタイプ宣言などが必要になる場面がたびたびあります。この場合で有用になるものが機能検
査マクロです。プログラムコンパイル時に1つまたは複数の機能検査マクロを定義しますが、すべ
てのヘッダファイルに先立ち定義する必要があります。プログラムソースコード内に記述する場合
は次のようになります。
#define _BSD_SOURCE 1
Cコンパイラに-Dオプションを与える方法もあります。
$ cc-D_BSD_SOURCE prog.c
機能検査マクロという用語はやや混乱するかもしれませんが、実装する観点から見れば納
得がいきます。ヘッダファイル内である「機能」を実装する場合は、どの「マクロ」が定
義されているかを「検査」することで、そのコードの有効/無効を切り替えます（プリプロ
セッサの#if）。
標準仕様により次の機能検査マクロが標準化されています。すなわち、標準仕様に準拠する全シ
ステムで使用（検査）可能なマクロです。
_POSIX_SOURCE
定義されていれば（値は問わない）POSIX.1-1990およびISO C(1990)に準拠する定義を
有効にする。_POSIX_C_SOURCEにより取って代わられた。
_POSIX_C_SOURCE
• 値を1と定義した場合は_POSIX_SOURCEと等価である。
• 199309 以上の値を定義した場合は、さらにPOSIX.1b（リアルタイム拡張）に準拠する定義
を有効にする。
• 199506 以上の値を定義した場合は、さらにPOSIX.1c（POSIXスレッド）に準拠する定義を
有効にする。
• 200112 以上の値を定義した場合は、さらにPOSIX.1-2001のベース仕様に準拠する定義を
有効にする（XSI拡張は含まない。バージョン2.3.3未満のglibcのヘッダファイルは値が
200112 の_POSIX_C_SOURCE を解釈しない）。
• 200809 以上の値を定義した場合は、さらに POSIX.1-2008 のベース仕様に準拠する定
義を有効にする（バージョン2.10 未満の glibc のヘッダファイルは値が 200809 の
_POSIX_C_SOURCE を解釈しない）。
3.6 可搬性｜ 65
_XOPEN_SOURCE
定義されていれば（値は問わない）POSIX.1、POSIX.2、X/Open（XPG4）に準拠する定義
を有効にする。
• 500 以上の値を定義した場合は、さらにSUSv2（UNIX98およびXPG5）に準拠す
る定義を有効にする。
• 600 以上の値を定義した場合は、さらにSUSv3XSI（UNIX03）拡張およびC99に
準拠する定義を有効にする（バージョン2.2未満のglibcのヘッダファイルは値が
600 の_XOPEN_SOURCE を解釈しない）。
• 700 以上の値を定義した場合は、さらにSUSv4XSI拡張に準拠する定義を有効にす
る（バージョン2.10未満のglibcのヘッダファイルは値が700の_XOPEN_SOURCE
を解釈しない）。
上記の値、500、600、700はSUSv2、SUSv3、SUSv4がX/Open仕様書のそれぞれ第5巻、
第6巻、第7巻である点に由来する*5。
以下に挙げる機能検査マクロはglibc固有です。
_BSD_SOURCE
定義されていれば（値は問わない）BSD固有の定義を有効にする。明示的に_BSD_SOURCE
のみを定義することで、標準仕様から逸脱するBSD固有の定義が優先される。
_SVID_SOURCE
定義されていれば（値は問わない）SVID（SystemV InterfaceDefinition）に準拠する定義
を有効にする。
_GNU_SOURCE
定義されていれば（値は問わない）上記すべてのマクロを包含し、さらにGNU拡張も有効
にする。
どのマクロも定義せずGNUCコンパイラを実行すると、ディフォルトで_POSIX_SOURCE、
_POSIX_C_SOURCE=200809（glibc バージョン2.5から2.9では200112、バージョン2.4未満で
は199506）、_BSD_SOURCE、_SVID_SOURCE が定義されます。
いずれかのマクロを定義した場合、またはC言語標準を指定するオプション（cc-ansiやcc-std=c99 など）を与えた場合は、指定したマクロのみが有効となります。しかし例外が1つだけ
あり、_POSIX_C_SOURCE を定義せず、かつC言語標準を指定するオプションも与えない場合は、
_POSIX_C_SOURCE を 200809 と定義したものと見なされます（glibcバージョン2.5から2.9で
は200112、バージョン2.4未満では199506）。
複数のマクロを定義することも可能です。準拠する標準仕様を追加する意味になり、例えばディ
フォルトと同じ内容を明示的に定義すると次のようになります。
*5 訳者注：自明かもしれませんが_POSIX_C_SOURCEに定義する値は年月を表します。
66
｜3章システムプログラミング
$ cc-D_POSIX_SOURCE-D_POSIX_C_SOURCE=199506-D_BSD_SOURCE-D_SVID_SOURCE
prog.c
機能検査マクロそれぞれに定義する値の詳細についてはヘッダファイル<features.h>および
feature_test_macros(7) のマニュアルページに記載されています*6。
_POSIX_C_SOURCE、_XOPEN_SOURCE、POSIX.1/SUS
POSIX.1-2001/SUSv3 が標準化した機能検査マクロは _POSIX_C_SOURCE および _XOPEN_
SOURCE のみです。POSIX.1-2001/SUSv3 に準拠するアプリケーションではそれぞれ200112、600
に定義します。_POSIX_C_SOURCE を200112 と定義するとPOSIX.1-2001 のベース仕様に準拠し
た定義が有効になります（XSI拡張を含まないPOSIX準拠）。_XOPEN_SOURCEを600と定義する
とSUSv3 に準拠した定義が有効になります（ベース仕様に加えXSI拡張も含む。すなわちXSI準
拠）。それぞれ200809、700に定義するとPOSIX.1-2008/SUSv4に準拠します。
SUSv3 では_XOPEN_SOURCEを600と定義した場合は、_POSIX_C_SOURCEが200112の場合を
すべて包含すると定めています。このため、SUSv3（XSI）に準拠するアプリケーションが定義する
必要があるのは_XOPEN_SOURCEのみです。SUSv4も同様に_XOPEN_SOURCEを700と定義すれ
ば_POSIX_C_SOURCE が200809の場合を包含するとしています。
本書のプロトタイプ宣言とサンプルプログラムでの機能検査マクロ
ヘッダファイルが定義する定数や関数宣言を有効にするための機能検査マクロはマニュアルペー
ジに明記されています。
本書のサンプルプログラムはすべて、GNUCコンパイラのディフォルトの状態か、または次のオ
プションを与えた場合でコンパイルできます。
$ cc-std=c99-D_XOPEN_SOURCE=600
また本書に提示するプロトタイプ宣言でも、必要な機能検査マクロを明記しています。関数のマ
ニュアルページでは機能検査マクロについてさらに詳細に記載しています。
3.6.2 システムデータ型
システムが定義するプロセスID、ユーザID、ファイルオフセットなどの型はC言語標準の型を
基に定義されています。int、longなどのC言語標準の基本データ型のままでも上記データを表現
できますが、次の理由によりUNIXシステム間での可搬性を損ないます。
• 基本データ型のサイズはUNIXシステム間で異なる（例えばlongのサイズはあるシステム
では4バイトだが、別のシステムでは8バイトの可能性がある）。また、同一システム上でも
コンパイル環境が異なると、基本データ型も変化する場合がある。さらに、同じ情報を表現
*6 訳者注：ディストリビューションによってはftm(7)という名前を短くしたマニュアルページもあります。内容は同
じです。
3.6 可搬性｜ 67
する場合でも、システムが異なると別の型を使用する場合もある。例えばあるシステムでは
プロセスIDをintで表現するが、別のシステムではlongを用いるなどである。
• 同一UNIXシステムでも、同じ情報を表現する型がリリースにより差異がある場合がある。
Linux のユーザID、グループIDがそうである。Linux2.2までは16ビットだったが、Linux
2.4 以降では32ビットに変更された。
上記の問題を解消するため、SUSv3ではさまざまなシステムデータ型を標準化しており、シス
テムが適切に型を定義、使用することと定めています。標準仕様のシステムデータ型はC言語の
typedef により定義されます。例えばプロセスIDを表現する型、pid_tは、Linux/x86-32では次
のように定義されます。
typedef int pid_t;
大半のシステムデータ型の名前は_tで終わり、またヘッダファイル<sys/types.h>で定義さ
れます。一部のシステムデータ型は別ヘッダファイルが定義します。
アプリケーションでは可搬性を確保するため、システムデータの変数を宣言する場合にはシステ
ムデータ型を使用すべきです。例えば、次のように宣言すれば、SUSv3に準拠する限り、どのシス
テムでもプロセスIDを正しく表現できます。
pid_t mypid;
本書で使用するシステムデータ型を表3-1にまとめます。
表3-1 システムデータ型（一部）
データ型
SUSv3 の要件
説明
blkcnt_t
blksize_t
cc_t
符号付き整数型
ファイルブロック数（15.1節）
符号付き整数型
ファイルブロックサイズ（15.1節）
符号なし整数型
clock_t
整数または
浮動小数点実数型
clockid_t
算術型
端末特殊文字（62.4節）
クロックティック単位のシステム時間（10.7節）
POSIX.1b が定めるクロック/タイマ関数が使用するク
ロック識別子（23.6節）
comp_t
dev_t
SUSv3 にはない
圧縮クロックティック（28.1節）
算術型
DIR
型要件なし
fd_set
fsblkcnt_t
fsfilcnt_t
構造体
メジャー、マイナを含むデバイス番号（15.1節）
ディレクトリストリーム（18.8節）
select() が使用するファイルディスクリプタセット
（63.2.1 節）
符号なし整数型
ファイルシステムブロック数（14.11節）
符号なし整数型
gid_t
整数型
id_t
in_addr_t
整数型
32 ビット
符号なし整数型
ファイル数（14.11節）
グループID（8.3節）
ID を表現する汎用型。少なくともpid_t、uid_t、
gid_t のサイズを持つ
IPv4 アドレス（59.4節）
（続く）
68｜3章システムプログラミング
（表3-1続き）
in_port_t 16ビット
符号なし整数型IPポート番号（59.4節）
ino_t符号なし整数型ファイルi-node番号（15.1節）
key_t算術型SystemVIPCキー（45.2節）
mode_t整数型ファイルパーミションおよびファイル種類（15.1節）
mqd_t型は任意、配列以外POSIXメッセージキューディスクリプタ
msglen_t符号なし整数型SystemVメッセージキューサイズのバイト数
（46.4節）
msgqnum_t符号なし整数型SystemVメッセージキューのメッセージ数（46.4節）
nfds_t符号なし整数型poll()が使用するファイルディスクリプタ数（63.2.2
節）
nlink_t整数型ファイルの（ハード）リンク数（15.1節）
off_t符号付き整数型ファイルオフセットまたはファイルサイズ（4.7節、
15.1節）
pid_t符号付き整数型プロセスID、プロセスグループID、セッションID
（6.2節、34.2節、34.3節）
ptrdiff_t符号付き整数型2ポインタ変数の差
rlim_t符号なし整数型リソース消費制限（36.2節）
sa_family_t符号なし整数型ソケットアドレスファミリ（56.4節）
shmatt_t符号なし整数型SystemV共有メモリセグメントにアタッチしたプロセ
ス数（48.8節）
sig_atomic_t整数型アトミックにアクセス可能なデータ（21.1.3節）
siginfo_t構造体シグナル送信元の情報（21.4節）
sigset_t整数型または構造体シグナルセット（20.9節）
size_t符号なし整数型バイト単位のオブジェクトのサイズ
socklen_t 32ビット以上
整数型
バイト単位のソケットアドレス構造体のサイズ
（56.3節）
speed_t符号なし整数型端末回線速度（62.7節）
ssize_t符号付き整数型バイト数またはエラー（負数）
stack_t構造体シグナル処理専用スタック（21.3節）
suseconds_t [ 1,1000000]の範囲
の符号付き整数型マイクロ秒単位の間隔（10.1節）
tcflag_t符号なし整数型端末モードフラグビットマスク（62.2節）
time_t整数または
浮動小数点実数型Epochからの秒単位の経過カレンダ時間（10.1節）
timer_t算術型POSIX.1bインターバルタイマのタイマID（23.6節）
uid_t整数型ユーザID（8.1節）
SUSv3では表中一部の型について算術型（arithmetictype）と定義しており、整数でも浮動小数
点数（実数または複素数）でも構わないことを意味します。
本書で表3-1のシステムデータ型を参照する際に、「この型は（SUSv3が定義する）整数型であ
る」と表現することが多くあります。これはSUSv3では整数であることを要件としているが、ネイ
ティブな整数型（short、int、longなど）である必要はないということを意味します（また原則
としてLinuxでのシステムデータ型を表現する実際のネイティブなデータ型は明記しません。可搬
3.6 可搬性｜ 69
性を意識したアプリケーションではネイティブなデータ型を意識すべきではないためです）。
システムデータ型の値の出力
表3-1 のシステムデータ型（pid_tやuid_t）の変数の値を出力する場合、printf()が実装
に依存しないよう注意しなければなりません。C言語での引数の暗黙の型変換規則ではshortを
int へ変換しますが、intやlongの値はそのままです。すなわち、システムデータ型の変数を
printf() へ渡すと、暗黙にintやlongへ変換されます。しかし、printf()では実行時に引数
の型を判断する術がないため、コール側が%dや%ldなどの変換文字を明示的に指定しなければな
りません。この点を意識せずprintf()コールをコーディングすると、実装依存の問題が発生して
します。一般的な解決策は値をlongでキャスト演算し、%ldを指定することです。
pid_t mypid;
mypid = getpid();
/* Returns process ID of calling process */
printf("My PID is %ld\n", (long) mypid);
自プロセスのID
上記の解決策には1つ例外があります。データ型off_tは、コンパイル環境によりlong long
のサイズを持つため、「5.10 largefileのI/O」で述べるようoff_tの変数はlong longへキャス
ト演算し、%lldを指定します。
C99 言語標準ではprintf()へsize_t、ssize_t型に対応するサイズ修飾子zを追加し
ました。すなわち、size_t、ssize_t型の変数に対してはキャスト演算と%ldの組み合
わせではなく、キャスト演算せず%zdを使用できます。また、size_t型の変数に対して
は%zuを使用できます。glibcではzを実装していますが、すべてのUNIXシステムで使
用可能ではないため、本書ではzを使用しません。
またC99言語標準では、すべての整数型を表現可能なことが保証されているintmax_t
（またはuintmax_t）に対応するサイズ修飾子jも定義しています。前述の（long）キャ
スト演算と%ld変換の組み合わせではlong long型やint128_tなどの拡張された整
数型も別途対応する必要があるため、システムデータ型の値を出力する際には、最終的に
（intmax_t）キャスト演算と%jd変換の組み合わせに置き換えるのが最良ということにな
ります。しかし（ここでも）サイズ修飾子jはすべてのUNIXシステムで使用可能ではな
いため、本書では使用しません。
3.6.3 可搬性に関するその他の事柄
本節ではシステムプログラムを開発する際に遭遇する可能性がある、可搬性に関するその他の事
柄を解説します。
構造体の初期化
すべてのUNIXシステムでは、システムコール/ライブラリ関数に使用する標準仕様の構造体を多
数定義しています。semop()システムコールのセマフォ操作を指定するsembuf構造体を例にとり
70
｜3章システムプログラミング
ます。
struct sembuf {
unsigned short sem_num;
short
short
};
sem_op;
sem_flg;
/* セマフォ番号 */
/* 操作種類 */
/* 操作フラグ */
SUSv3 でも確かにsembuf構造体を定義していますが、次の点に注意する必要があります。
• フィールドの順序については、一般になんら定義されない。
• 実装依存のフィールドが追加されることがある。
このため、構造体の次のような初期化は可搬性を失います。
struct sembuf s = { 3,-1, SEM_UNDO };
Linux では上記の初期化処理も期待通り動作しますが、sembuf構造体のフィールドの順序が異
なるようなシステムでは正しく動作しません。初期化処理で可搬性を確保するには、次のように
フィールドへ明示的に代入します。
struct sembuf s;
s.sem_num = 3;
s.sem_op =-1;
s.sem_flg = SEM_UNDO;
C99 を使用する場合は、構造体を初期化する新しい文法を用い、次のように記述できます*7。
struct sembuf s = { .sem_num = 3, .sem_op =-1, .sem_flg = SEM_UNDO };
標準仕様が定義する構造体をファイルへ書き出す場合も、フィールドの順序を考慮する必要が
ありますが、この場合に可搬性を維持しつつバイナリなデータを書き込む単純な方法はありませ
ん。順序を固定し、構造体フィールドを個別に（恐らくはテキスト形式で）書き込むことになるで
しょう。
実装されていない恐れがあるマクロ
マクロがすべてのUNIXシステムで実装されていないこともあります。例えば、WCOREDUMP()マ
クロは広く実装されていますが（子プロセスがコアダンプファイルを生成したか否かを判定する）、
SUSv3 では定義されていません。このためWCOREDUMP()を実装しないUNIXシステムが存在する
可能性があります。このようなシステムに対処しつつ可搬性を維持するには、Cプリプロセッサの
#ifdef ディレクティブを次のように利用します。
*7 訳者注：この方法では、明示的に初期化しないフィールドは自動的に0に初期化されます。想定外にフィールドが追
加されていた場合などにフィールドが不定値となることを防ぐ効果があります。
3.7 章のまとめ｜ 71
#ifdef WCOREDUMP
/* Use WCOREDUMP() macro */
#endif
システム間のヘッダファイルの差異
WCOREDUMP() マクロを使用する
システムコールやライブラリ関数が必要とするヘッダファイルが、UNIXシステムにより異なる
場合もあります。本書ではLinuxで必要なヘッダファイルを記述し、SUSv3の定義とは異なるシス
テムが存在する場合はその旨を明記します。
本書が提示する一部のプロトタイプ宣言ではインクルードするヘッダファイルにFor porta
bility（可搬性確保）とコメントを添えてあります。これは、そのヘッダファイルはLinuxや
SUSv3 では必要とされていないが、他のシステム（特に古いシステム）では必要になる場合がある
ため、可搬性を意識する場合はインクルードすべきであるという意味です。
POSIX.1-1990 では多くの関数について、すべてのヘッダファイルより先に <sys/
types.h> をインクルードすることと定義していましたが、この要件は冗長でした。現代
のほとんどのUNIXシステムではアプリケーションが、その関数用に<sys/types.h>を
インクルードする必要はなく、SUSv1でこの要件を削除されました。しかし、可搬性を確
保したプログラムを開発するには、まず<sys/types.h>をインクルードした方が賢明で
す（Linux では不要のため、またサンプルプログラムを1行分短くするため、本書のサン
プルプログラムではインクルードしません）。
3.7 章のまとめ
プロセスはシステムコールを介しカーネル内の機能を利用します。システムコールを実行する場
合、システムは一時的にカーネルモードへ切り替える必要があり、またカーネルは引数を検査し、
さらにユーザメモリとカーネルメモリ間でデータを転送するため、ユーザ空間の関数コールと比較
すると、もっとも単純なシステムコールでも相当量のオーバヘッドを伴います。
標準Cライブラリは多種多様な機能を持つライブラリ関数を豊富に実装しています。一部のライ
ブラリ関数は内部でシステムコールを実行しますが、ユーザ空間内でのみ完結するものも多くあり
ます。Linuxでは標準Cライブラリにglibcを使用するのが一般的です。
大半のシステムコール、ライブラリ関数は処理に成功したか、エラーが発生したかを表す情報を
戻り値とします。この戻り値は常に確認するべき情報です。
本書のサンプルプログラム用に実装したいくつかの関数について述べました。エラー処理、コマ
ンドライン解析などの関数です。
標準仕様に準拠するどのシステムでも動作する、可搬性を確保したシステムプログラムを開発す
る際のガイドラインや方法についてもさまざまに述べました。
アプリケーションコンパイル時には機能検査マクロを用い、ヘッダファイル内の定義を有効にで
きます。開発するプログラムが公式の、またはシステムが追加した標準仕様に準拠することを保証
する方法です。
72
｜3章システムプログラミング
C言語ネイティブなデータ型を使用せず、代わりに各種標準仕様が定義するシステムデータ型を
使用することでもシステムプログラムの可搬性を向上させられます。SUSv3ではさまざまな種類の
システムデータ型を定義しています。システムはこの型を実装すべきですし、またアプリケーショ
ンもこの型を使用すべきです。