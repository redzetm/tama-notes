---
title: "UmuOSの為のC言語３（中級）　３章　I/Oのバッファリング"
---

# UmuOSの為のC言語（中級）　３

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結すると思います。。

## ３章　I/Oのバッファリング

2章では、`open()`、`read()`、`write()`、`close()`、`lseek()` など、低レベルファイルI/Oの基本を見ました。
この3章では、そのI/Oをどのくらいの大きさで読み書きするべきか、なぜバッファリングが必要なのか、標準I/Oライブラリが何をしているのかを整理します。

ファイルI/Oでは、1バイトずつ読み書きすることもできます。
しかし、できるからといって、それが効率的とは限りません。
ディスク、SSD、ファイルシステム、カーネルのページキャッシュは、内部ではある程度まとまった単位でデータを扱います。
そのため、小さすぎるI/Oを大量に発行すると、システムコール回数が増え、カーネルとの行き来も増え、全体として遅くなります。

```text
小さいI/Oを大量に行う
    システムコール回数が増える
    カーネルへ入る回数が増える
    関数呼び出しやコピーの overhead が増える

ある程度まとまったI/Oを行う
    システムコール回数を減らせる
    カーネルやストレージの得意な単位に近づけられる
```

ここで出てくる大事な考え方が、ブロックとバッファです。

ブロックは、ストレージやファイルシステムが扱いやすいまとまりの単位です。
バッファは、読み書きのために一時的にデータをためておくメモリ領域です。

UmuOSの視点では、この章もかなり重要です。
なぜなら、ファイルI/Oをただ実装するだけなら1バイトずつ読んでも動きますが、実用的なOSやシェルとして使いやすくするには、
どこで、どの単位で、どの層がバッファリングするかを設計する必要があるからです。

```text
ユーザー空間
    標準I/Oライブラリやアプリケーション独自のバッファ

カーネル空間
    ページキャッシュ
    writeback
    readahead

ストレージ側
    デバイス内部のキャッシュ
    セクタやブロック単位のI/O
```

この章では、まずユーザー空間のI/Oバッファリングから見ていきます。

### ３章の１　ユーザー空間のI/Oバッファリング

サイズの小さいI/Oを大量に発行するアプリケーションでは、ユーザー空間でバッファリングすることがよくあります。
これをユーザーバッファリングI/O、または user buffered I/O と呼びます。

ユーザー空間のバッファリングとは、カーネルへすぐ `read()` や `write()` を投げるのではなく、
アプリケーションやCライブラリ側のメモリにデータをためておき、ある程度まとまったところで実際のI/Oを行う方法です。

```text
バッファなしに近い書き込み
    1文字ごとに write()
    システムコール回数が多い

ユーザー空間バッファリング
    文字や行をユーザー空間のバッファへためる
    バッファが満たされたらまとめて write()
```

2章で見たように、カーネルもページキャッシュ、遅延書き込み、readahead、writeback などで内部的にバッファリングします。
しかし、ここで扱うのはカーネル内のバッファリングではありません。
アプリケーションやCライブラリが、ユーザー空間で行うバッファリングです。

```text
カーネルのバッファリング
    ページキャッシュ
    readahead
    writeback

ユーザー空間のバッファリング
    stdio の FILE バッファ
    アプリケーション独自のバッファ
```

#### ３章の１の１　なぜ1バイトずつのI/Oは遅いのか

たとえば、2MiBのデータをコピーするとします。
1バイトずつ読むなら、約200万回の読み取りが必要になります。
同じように1バイトずつ書くなら、約200万回の書き込みも必要になります。

```text
2MiB
    2,097,152バイト

1バイトずつ読む
    read() が約2,097,152回

1バイトずつ書く
    write() が約2,097,152回
```

一方、1024バイトずつ処理すれば、読み取り回数も書き込み回数も約2048回で済みます。

```text
2MiBを1024バイトずつ処理
    2,097,152 / 1024 = 2048回
```

この違いはかなり大きいです。
システムコールは普通の関数呼び出しより重いです。
ユーザー空間からカーネル空間へ入り、カーネルがfdやバッファを確認し、必要な処理を行い、またユーザー空間へ戻る必要があります。

そのため、1バイトずつ `read()` / `write()` するプログラムは、データ量そのものよりも、システムコール回数の多さで遅くなります。

#### ３章の１の２　ddで見るブロックサイズ

古い書籍では、`dd` コマンドを使ってブロックサイズの違いを説明することがあります。
たとえば、次のようなコマンドです。

```sh
dd bs=1 count=2097152 if=/dev/zero of=pirate
```

これは、`/dev/zero` から `pirate` というファイルへ、1バイトずつ2,097,152回コピーする例です。
合計では2MiBですが、I/Oの単位が小さすぎます。

次は、同じ2MiBを1024バイト単位でコピーする例です。

```sh
dd bs=1024 count=2048 if=/dev/zero of=pirate
```

合計サイズは同じでも、処理回数は大幅に減ります。
このため、多くの環境でこちらの方がかなり速くなります。

ただし、注意点があります。
古い本に載っている実行時間の表は、その当時のカーネル、ディスク、CPU、ファイルシステム、キャッシュ状態に強く依存します。
現代のLinux、SSD、NVMe、仮想環境、WSL、コンテナでは、同じ数字にはなりません。
そのため、このノートでは固定の秒数を暗記するのではなく、次の構造を理解することを重視します。

```text
小さすぎるbs
    システムコール回数が増えすぎる
    遅くなりやすい

適切なbs
    システムコール回数を減らせる
    カーネルやページキャッシュの得意な単位に近づく

中途半端なbs
    アラインメントや内部処理の都合で最適ではない場合がある
```

実際に自分の環境で試すなら、キャッシュの影響、ストレージの種類、ファイルシステム、`conv=fsync` の有無などで結果が変わります。
ベンチマークとして厳密に扱うより、「I/Oサイズが性能に影響する」ことを体感する実験として見るのがよいです。

#### ３章の１の３　ブロックサイズ

ブロックサイズとは、ストレージやファイルシステムが扱うデータのまとまりの大きさです。
古い説明では、現実のブロックサイズは 512、1024、2048、4096 バイトのいずれか、と説明されることがあります。

現在でも、4KiBは非常によく出てきます。
ただし、現代のストレージでは、論理セクタサイズ、物理セクタサイズ、ファイルシステムのブロックサイズ、ページサイズなど、
似た概念がいくつかあります。
そのため、単純に「ディスクのブロックサイズは常に512バイト」と覚えるのは危険です。

```text
論理セクタサイズ
    デバイスが論理的に見せる最小単位
    512バイトや4096バイトなど

物理セクタサイズ
    実際の物理的な書き換え単位に近いもの
    Advanced Format HDDやSSDでは4096バイトが関係しやすい

ファイルシステムのブロックサイズ
    ext4やXFSなどがファイルデータを管理する単位
    4KiBがよく使われる

ページサイズ
    CPU/MMUとカーネルのメモリ管理単位
    x86-64 Linuxでは通常4KiB
```

アプリケーションを書くとき、これらすべてを毎回調べる必要はありません。
普通のファイルI/Oでは、4KiB、8KiB、16KiB、64KiBのような、よく使われるまとまったサイズで読み書きすれば、極端に悪い選択にはなりにくいです。

```text
避けたい例
    1バイトずつ read()/write()
    1130バイトのような中途半端な固定サイズを理由なく使う

よくある選択
    4096バイト
    8192バイト
    16384バイト
    65536バイト
```

もちろん、最適なサイズはワークロードによって変わります。
大量のファイルを順番に読む、ネットワークへ流す、端末へ表示する、SSDへ書く、パイプへ渡す、という処理では、それぞれ事情が違います。
まずは「小さすぎるI/Oを避け、ある程度まとまった単位でI/Oする」と理解すれば十分です。

#### ３章の１の４　ブロックサイズを調べる

処理対象のファイルやファイルシステムに関係する推奨I/Oサイズは、`stat()` や `fstat()` で得られることがあります。
コマンドなら `stat` コマンドでも確認できます。

```sh
stat pirate
```

環境によって表示は異なりますが、`IO Block` や `Block size` のような情報が出ることがあります。

Cプログラムでは、`struct stat` の `st_blksize` が、効率的なI/Oのためのブロックサイズの目安として使えます。
ただし、これは物理デバイスのセクタサイズそのものではなく、ファイルシステムが推奨するI/Oサイズの目安と考える方が安全です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void) {
    struct stat st;

    if (stat("pirate", &st) < 0) {
        perror("stat");
        exit(1);
    }

    printf("recommended block size: %ld\n", (long)st.st_blksize);

    return 0;
}
```

このコードは、`pirate` というファイルに対する推奨I/Oブロックサイズの目安を表示します。
ただし、値はファイルシステムや環境によって変わります。
固定の出力例は書かず、自分の環境で確認するのがよいです。

#### ３章の１の５　ユーザー空間バッファリングの仕組み

アプリケーションは、必ずしもブロック単位でデータを扱いたいわけではありません。
実際には、1文字、1行、1レコード、1フィールドといった単位で処理することが多いです。

```text
アプリケーションが扱いたい単位
    1文字
    1行
    CSVの1フィールド
    構造化された1レコード

OSやストレージが得意な単位
    ページ
    ブロック
    ある程度まとまったバイト列
```

このギャップを埋めるのが、ユーザー空間のバッファリングです。

書き込みの場合、アプリケーションが1文字ずつ出力しても、ライブラリはすぐに毎回 `write()` するのではなく、内部バッファへためます。
バッファがいっぱいになったり、改行が来たり、明示的に flush されたりしたタイミングで、まとめて `write()` します。

```text
アプリケーション
    putchar('A')
    putchar('B')
    putchar('C')

標準I/Oライブラリ
    FILE内部のバッファへためる

必要なタイミング
    write(fd, buffer, len) でまとめて出す
```

読み取りの場合も同じです。
アプリケーションが1文字だけ欲しい場合でも、ライブラリは内部的にまとまったサイズで `read()` し、その中から1文字を返すことがあります。
次にまた1文字要求されたら、すでに読み込んであるバッファから返せます。

```text
アプリケーション
    getchar()

標準I/Oライブラリ
    内部ではまとめて read()
    そのうち1文字だけ返す

次の getchar()
    すでにあるバッファから返す
```

この仕組みによって、アプリケーションは1文字や1行といった自然な単位で処理しつつ、実際のシステムコール回数を減らせます。

#### ３章の１の６　標準I/Oへつながる話

ユーザー空間のバッファリングは、自分で実装することもできます。
実際、データベース、ログ基盤、高性能サーバーなどでは、アプリケーション独自のバッファ管理を持つことがあります。

しかし、多くのCプログラムでは、標準I/Oライブラリを使います。
標準I/Oとは、`FILE *`、`fopen()`、`fread()`、`fwrite()`、`fgets()`、`fprintf()`、`fflush()` などの関数群です。

```text
低レベルI/O
    fd
    open()
    read()
    write()
    close()

標準I/O
    FILE *
    fopen()
    fread()
    fwrite()
    fgets()
    fprintf()
    fflush()
```

標準I/Oは、Cライブラリが提供するユーザー空間バッファリングの代表です。
次の節では、この標準I/Oを詳しく見ていきます。

UmuOSやUshの視点では、最初は低レベルI/Oだけでも実装できます。
しかし、ユーザー空間のCライブラリを育てる段階では、`FILE` 相当の構造体、内部バッファ、flush、行バッファリングなどが
必要になります。
この章は、UmuOS上で将来Cライブラリ的なものを作るときの土台にもなります。

### ３章の２　標準I/O

標準Cライブラリは、標準I/Oライブラリを提供しています。
一般には stdio と呼ばれます。
C言語のヘッダでは `<stdio.h>` が関係します。

標準I/Oは、低レベルI/Oの `open()`、`read()`、`write()`、`close()` を直接使うのではなく、`FILE *` というストリームを通して
ファイルを扱う仕組みです。
この `FILE *` の内側に、ユーザー空間のバッファや、対応するファイルディスクリプタなどが隠れています。

```text
低レベルI/O
    fd
    open()
    read()
    write()
    close()

標準I/O
    FILE *
    fopen()
    fgetc()
    fgets()
    fread()
    fprintf()
    fclose()
```

C言語そのものは、ファイルI/Oを言語文法として直接持っているわけではありません。
`if`、`for`、関数、演算子、型などはC言語の機能ですが、ファイルを開いたり、文字列を表示したりする処理は、
標準Cライブラリが提供する関数によって行います。

```text
C言語そのもの
    制御構文
    型
    式
    関数呼び出しの仕組み

標準Cライブラリ
    printf()
    fopen()
    fread()
    malloc()
    time() など
```

歴史的には、C言語の利用が広がるにつれて、文字列操作、メモリ操作、時刻処理、I/O処理などの標準的な関数群が整備されました。
1989年のANSI C、つまりC89で標準Cライブラリが正式に規格化され、標準I/Oもその中核として残り続けています。

現在のLinuxでは、標準I/Oは多くの場合 glibc によって実装されています。
musl libc のような別のCライブラリでも同じ標準I/O APIは提供されますが、内部実装や細かい拡張機能は異なることがあります。

このノートでは、基本はPOSIXと標準Cの範囲を意識しつつ、Linux/glibcでよく見る挙動も補足します。

#### ３章の２の１　標準I/Oを使うか、低レベルI/Oを使うか

標準I/Oは便利です。
`fgets()` で1行を読めますし、`fprintf()` で整形出力もできます。
内部でバッファリングしてくれるため、1文字ずつ処理するコードでも、毎回システムコールを発行せずに済みます。

一方で、低レベルI/Oの方が向いている場面もあります。
たとえば、パイプ、ソケット、ノンブロッキングI/O、`poll()`、`epoll()`、ファイルディスクリプタの複製やリダイレクトを細かく扱う場合です。

```text
標準I/Oが向く場面
    テキストファイルを行単位で読む
    printf系で整形して出力する
    普通の設定ファイルやログを扱う

低レベルI/Oが向く場面
    fdを直接操作したい
    pipe()、dup2()、fork()、exec() と組み合わせたい
    poll()、epoll()、ノンブロッキングI/Oを使いたい
    バッファリングを自分で厳密に制御したい
```

Ushのようなシェルを作る場合、内部のリダイレクトやパイプ処理では低レベルI/Oが中心になります。
一方、シェルの補助ツールや設定ファイル読み込みでは標準I/Oが便利です。

### ３章の３　ファイルポインタとストリーム

標準I/Oでは、ファイルディスクリプタを直接使うのではなく、ファイルポインタを使います。
ここでいうファイルポインタとは、`FILE *` のことです。

```c
#include <stdio.h>

FILE *stream;
```

注意したいのは、この「ファイルポインタ」という言葉です。
ファイル内の現在位置を指す「ファイルポジション」とは別物です。

```text
FILE *
    標準I/Oで開いたストリームを表すポインタ

ファイルポジション
    次に読み書きするファイル内の位置
```

標準I/Oの世界では、開いたファイルをストリームと呼びます。
ストリームは、入力、出力、入出力のいずれかの用途で開かれます。

```text
入力ストリーム
    読み取り用

出力ストリーム
    書き込み用

入出力ストリーム
    読み書き両用
```

`FILE` 型の中身は、プログラムから直接触るものではありません。
glibcでは内部にバッファ、fd、状態フラグ、エラー状態、EOF状態などを持っていますが、これらは実装詳細です。
ユーザープログラムは `FILE *` を `fopen()`、`fread()`、`fclose()` などへ渡して操作します。

```text
ユーザーが触るもの
    FILE *

ライブラリ内部で管理されるもの
    fd
    バッファ
    EOF状態
    エラー状態
    現在の読み書き位置に関係する情報
```

### ３章の４　ストリームのオープン: fopen()

標準I/Oでファイルを開くには `fopen()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

FILE *fopen(const char *path, const char *mode);
```

ここで出てくる `FILE *fopen(...)` は、実行コードではなくプロトタイプ宣言です。
`fopen()` は、`path` で指定したファイルを `mode` の指定に従って開き、新しいストリームを返します。

```text
成功
    FILE * を返す

失敗
    NULL を返す
    errno に理由が入る
```

簡単な例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;

    stream = fopen("memo.txt", "r");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

古いサンプルでは `/etc/manifest` や存在しないユーザーのホームディレクトリが例に使われることがあります。
このノートでは、実際に試しやすいように `memo.txt` のようなファイル名を使います。

#### ３章の４の１　fopen() のモード

`fopen()` の第2引数 `mode` には、文字列で開き方を指定します。

```text
"r"
    読み取り専用で開く
    ファイルは存在している必要がある
    ファイルポジションは先頭

"r+"
    読み書き両用で開く
    ファイルは存在している必要がある
    ファイルポジションは先頭

"w"
    書き込み専用で開く
    既存ファイルはサイズ0に切り詰める
    なければ作成する

"w+"
    読み書き両用で開く
    既存ファイルはサイズ0に切り詰める
    なければ作成する

"a"
    追記専用で開く
    なければ作成する
    書き込みはファイル末尾へ行われる

"a+"
    読み書き両用の追記モードで開く
    なければ作成する
    書き込みはファイル末尾へ行われる
```

`"w"` と `"w+"` は既存ファイルを空にします。
ここはかなり危険です。
重要なファイルを開くときに間違って `"w"` を使うと、中身が消えます。

```text
"r"
    読むだけ

"w"
    空にしてから書く

"a"
    末尾へ追記する
```

`"a"` と `"a+"` はアペンドモードです。
低レベルI/Oの `O_APPEND` と同じく、ログのように末尾へ追記したい場合に使います。

#### ３章の４の２　bモード

`mode` には `b` を含めることもできます。

```c
stream = fopen("image.bin", "rb");
stream = fopen("out.bin", "wb");
```

Windowsのようにテキストモードとバイナリモードを区別する環境では、`b` は重要です。
たとえば、テキストモードでは改行コード変換が関係することがあります。

一方、LinuxやPOSIX系OSでは、テキストファイルとバイナリファイルをI/Oモードとして区別しません。
そのため、Linuxでは `b` は実質的に無視されます。

```text
Linux/POSIX
    "r" と "rb" は実質的に同じ
    "w" と "wb" も実質的に同じ

Windowsなど
    テキストモードとバイナリモードに差がある
```

移植性を考えるなら、バイナリデータを扱うときは `"rb"` や `"wb"` と書くのが無難です。
Linuxでは意味がほぼ同じでも、他の環境で意図が明確になります。

### ３章の５　ファイルディスクリプタからストリームを作る: fdopen()

すでに開いているファイルディスクリプタを、標準I/Oのストリームとして扱いたい場合があります。
このとき使うのが `fdopen()` です。

宣言は次のようになります。

```c
#include <stdio.h>

FILE *fdopen(int fd, const char *mode);
```

`fdopen()` は、既存の `fd` に標準I/Oの `FILE *` を対応付けます。
新しくファイルを開くのではなく、すでに開いているfdをストリームとして包む、というイメージです。

```text
open()
    fdを得る

fdopen()
    そのfdをFILE *として扱えるようにする
```

例です。

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    FILE *stream;
    int fd;

    fd = open("memo.txt", O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        perror("open");
        exit(1);
    }

    stream = fdopen(fd, "r");

    if (stream == NULL) {
        perror("fdopen");
        close(fd);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

ここで重要なのは、`fdopen()` はfdを複製するわけではない、という点です。
`FILE *` は元のfdに対応付けられます。
そのため、`fclose(stream)` すると、対応するfdも閉じられます。

```text
fdopen(fd, "r")
    fdをFILE *で包む

fclose(stream)
    streamを閉じる
    対応するfdも閉じる
```

`fdopen()` した後に、同じfdへ `read()` などの低レベルI/Oを直接混ぜることは避ける方が安全です。
標準I/O側が内部バッファを持っているため、現在位置や読み書き済みデータの見え方が分かりにくくなるからです。

```text
避けたい混在
    FILE *stream = fdopen(fd, "r");
    fgets(buf, sizeof(buf), stream);
    read(fd, rawbuf, sizeof(rawbuf));

理由
    stdio側が先にまとめて読んでいる可能性がある
    fdの位置とstdio内部バッファの関係が分かりにくくなる
```

Ushのようなシェルでは、fdを直接扱う場面が多いです。
そのfdを一時的に `FILE *` として便利に読むことはできますが、混在の危険を理解して使う必要があります。

### ３章の６　ストリームのクローズ: fclose()

標準I/Oで開いたストリームを閉じるには `fclose()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

int fclose(FILE *stream);
```

`fclose()` は、まず未書き込みのバッファをフラッシュし、その後ストリームを閉じます。
対応するファイルディスクリプタも閉じられます。

```text
fclose(stream)
    未書き込みデータをflushする
    streamを閉じる
    対応するfdも閉じる
```

戻り値は次の通りです。

```text
成功
    0

失敗
    EOF
    errno に理由が入る
```

`fclose()` のエラー確認は重要です。
標準I/Oはバッファリングしているため、`fprintf()` が成功したように見えても、実際の書き込みエラーが `fclose()` のタイミングで
見えることがあります。

```c
if (fclose(stream) == EOF) {
    perror("fclose");
    exit(1);
}
```

これは2章で見た `close()` のエラー確認と似ています。
保存結果が重要な場合、最後のクローズ処理のエラーを無視しない方がよいです。

#### ３章の６の１　fcloseall()

glibcには、プロセスが開いているすべてのストリームを閉じる `fcloseall()` があります。

```c
#define _GNU_SOURCE
#include <stdio.h>

int fcloseall(void);
```

ただし、`fcloseall()` はGNU拡張です。
標準CやPOSIXの移植性が必要なコードでは使わない方がよいです。

また、`fcloseall()` は標準入力、標準出力、標準エラー出力も含めて閉じます。
そのため、安易に使うと、その後の `printf()` や `fprintf(stderr, ...)` が使えなくなります。

```text
fcloseall()
    glibc/GNU拡張
    すべてのストリームを閉じる
    stdin / stdout / stderr も対象

通常のアプリケーション
    基本は個別に fclose() する
```

最初の研究段階では、`fcloseall()` は「Linux/glibcにある特殊な便利関数」くらいで十分です。
普通のコードでは、開いたストリームを自分で管理し、必要なタイミングで `fclose()` します。

### ３章の７　ストリームの読み取り

標準I/Oには、ストリームから読み取るための関数が複数あります。
この節では、よく使う次の3種類を見ます。

```text
文字単位
    fgetc()

行単位
    fgets()

バイナリデータ
    fread()
```

ストリームから読み取るには、読み取り可能なモードで開いておく必要があります。
たとえば `"r"`、`"r+"`、`"w+"`、`"a+"` などです。
`"w"` や `"a"` のような書き込み専用ストリームからは、基本的に読み取れません。

#### ３章の７の１　文字単位の読み取り: fgetc()

ストリームから1文字読み取るには `fgetc()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

int fgetc(FILE *stream);
```

`fgetc()` は、ストリームから1文字を読み取り、その文字を `unsigned char` として扱える値にして、`int` として返します。
なぜ戻り値が `char` ではなく `int` なのかが重要です。

`fgetc()` は、通常の文字だけでなく、EOFやエラーも返す必要があります。
そのため、戻り値を `char` に入れてしまうと、文字とEOFを区別できなくなる可能性があります。

```text
正しい
    int c;
    c = fgetc(stream);

避けたい
    char c;
    c = fgetc(stream);
```

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;
    int c;

    stream = fopen("memo.txt", "r");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    c = fgetc(stream);

    if (c == EOF) {
        if (ferror(stream)) {
            perror("fgetc");
            fclose(stream);
            exit(1);
        }

        printf("EOF\n");
    } else {
        printf("c=%c\n", (char)c);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

ここでは `ferror()` が出てきます。
`fgetc()` が `EOF` を返した場合、それが本当のファイル終端なのか、エラーなのかを区別するには `feof()` や `ferror()` を使います。
この話は後のエラーとEOFの節でもう一度整理します。

#### ３章の７の２　読み取った文字を戻す: ungetc()

標準I/Oには、読み取った文字をストリームへ戻す `ungetc()` があります。
少し先を見て、不要なら戻す、という使い方ができます。

宣言は次のようになります。

```c
#include <stdio.h>

int ungetc(int c, FILE *stream);
```

`ungetc()` は、文字 `c` をストリームへ押し戻します。
次に同じストリームから読み取ると、その文字が返ります。

```text
fgetc()
    1文字読む

ungetc()
    その文字を戻す

次のfgetc()
    戻した文字が読める
```

簡単な例です。

```c
int c;

c = fgetc(stream);

if (c != EOF) {
    if (ungetc(c, stream) == EOF) {
        perror("ungetc");
    }
}
```

POSIXでは、読み取りを挟まない連続した `ungetc()` について、少なくとも1文字は確実に戻せると考えます。
glibcではより多く戻せることがありますが、移植性を考えるなら「確実なのは1文字」と見ておく方が安全です。

また、`ungetc()` で戻した文字は、シーク操作によって失われることがあります。
同じストリームを複数スレッドで共有している場合も、読み取り位置やバッファ状態が絡むため注意が必要です。

#### ３章の７の３　行単位の読み取り: fgets()

ストリームから1行読み取るには `fgets()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

char *fgets(char *str, int size, FILE *stream);
```

`fgets()` は、最大で `size - 1` 文字を読み取り、最後にヌル文字 `\0` を付けます。
改行を読み取った場合は、その改行文字 `\n` もバッファに入ります。

```text
fgets(str, size, stream)
    最大 size - 1 文字を読む
    最後に \0 を付ける
    改行を読んだら \n も入る
```

戻り値は次の通りです。

```text
成功
    str を返す

EOFまたはエラー
    NULL を返す
```

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char buf[1024];
    FILE *stream;

    stream = fopen("memo.txt", "r");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (fgets(buf, sizeof(buf), stream) == NULL) {
        if (ferror(stream)) {
            perror("fgets");
            fclose(stream);
            exit(1);
        }

        printf("EOF\n");
    } else {
        printf("line: %s", buf);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

古い資料では `LINE_MAX` を使う例が出てきます。
`LINE_MAX` はPOSIXで行長に関係する値として出てきますが、現代Linux/glibcの実用では、入力行が固定上限に収まるとは限らない、
と考える方が安全です。

設定ファイルのように1行の最大長を自分で決められるなら、固定長バッファでもよいです。
しかし、任意の長さの行を扱うなら、後で出てくる `getline()` のように、必要に応じてバッファを拡張できる関数を使う方が便利です。

```text
固定長で十分な場合
    設定ファイルの1行を最大1024文字に制限する

任意長を扱いたい場合
    getline() を検討する
```

#### ３章の７の４　任意の区切りで読み取る

`fgets()` は行単位、つまり改行まで読む用途に向いています。
しかし、改行以外の区切り文字まで読みたい場合もあります。
たとえば、カンマ、コロン、ヌル文字、独自の区切りなどです。

その場合、`fgetc()` を使って1文字ずつ読み、区切り文字に到達したら止める、という実装もできます。

```c
#include <stdio.h>

char *read_until(FILE *stream, char *buf, size_t size, int delimiter) {
    size_t pos;
    int c;

    if (size == 0) {
        return NULL;
    }

    pos = 0;

    while (pos + 1 < size) {
        c = fgetc(stream);

        if (c == EOF) {
            break;
        }

        if (c == delimiter) {
            break;
        }

        buf[pos] = (char)c;
        pos++;
    }

    buf[pos] = '\0';

    if (pos == 0 && c == EOF) {
        return NULL;
    }

    return buf;
}
```

この関数は、指定した区切り文字まで読み取り、区切り文字自体はバッファへ入れません。
ただし、これは研究用の簡単な例です。
実用では、EOFとエラーの区別、長すぎる入力、区切り文字が見つからなかった場合の扱いなどをもっと丁寧に設計する必要があります。

また、`fgetc()` を何度も呼ぶため、関数呼び出しのオーバーヘッドはあります。
しかし、標準I/Oが内部でバッファリングしているため、低レベルの `read()` を1バイトずつ大量に呼ぶ場合とは違います。
ここは混同しない方がよいです。

```text
fgetc()を何度も呼ぶ
    stdio内部バッファから読むことが多い
    システムコールが毎回発生するとは限らない

read(fd, &c, 1)を何度も呼ぶ
    低レベルread()を毎回呼ぶ
    システムコール回数が増える
```

#### ３章の７の５　バイナリデータの読み取り: fread()

文字単位や行単位ではなく、まとまったバイナリデータを読みたい場合は `fread()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
```

`fread()` は、`size` バイトの要素を `nmemb` 個読み取り、`ptr` が指すバッファへ格納します。

```text
ptr
    読み取り先バッファ

size
    1要素のサイズ

nmemb
    読み取りたい要素数

stream
    読み取り元ストリーム
```

戻り値は、読み取ったバイト数ではありません。
読み取れた要素数です。

```text
fread(ptr, 64, 1, stream)
    64バイトの要素を1個読む
    成功なら1を返す

fread(ptr, 1, 64, stream)
    1バイトの要素を64個読む
    64バイト読めれば64を返す
```

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned char buf[64];
    FILE *stream;
    size_t nr;

    stream = fopen("data.bin", "rb");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    nr = fread(buf, sizeof(buf), 1, stream);

    if (nr != 1) {
        if (ferror(stream)) {
            perror("fread");
            fclose(stream);
            exit(1);
        }

        fprintf(stderr, "short read or EOF\n");
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

この例では、64バイトの塊を1個読む形にしています。
`fread()` が `0` を返した場合、それだけではEOFなのかエラーなのか分かりません。
`feof()` や `ferror()` で確認する必要があります。

#### ３章の７の６　バイナリデータと構造体の注意

`fread()` を使うと、Cの構造体をそのままファイルから読み書きしたくなります。
しかし、これは注意が必要です。

構造体には、アラインメントやパディングが入ることがあります。
また、CPUによってバイトオーダ、つまりエンディアンが違うこともあります。

```text
構造体をそのまま保存するときの注意

アラインメント
    型ごとに自然な境界へ配置される

パディング
    構造体の隙間に未使用バイトが入ることがある

バイトオーダ
    little endian / big endian の違いがある

型のサイズ
    int や long のサイズが環境で違うことがある
```

たとえば、ある環境で `struct record` をそのまま `fwrite()` して、
別のCPUや別のコンパイラ設定の環境で `fread()` すると、正しく読めない可能性があります。

そのため、永続化するファイル形式やネットワークプロトコルでは、構造体をそのまま保存するよりも、明確な形式を決める方が安全です。

```text
安全寄りの考え方
    ファイル形式を明確に定義する
    整数サイズを固定する
    バイトオーダを決める
    必要なら1フィールドずつエンコードする
```

Cでは、コンパイラが通常の変数を適切にアラインメントしてくれます。
しかし、自前でメモリを管理する場合、バイナリファイルを直接読む場合、ネットワークから受け取ったバイト列を構造体として解釈する場合は、
アラインメント問題が表に出てきます。

UmuOSの視点では、これはかなり重要です。
ファイルシステム上のinode、ディレクトリエントリ、実行ファイルヘッダ、ネットワークパケットなどは、すべてバイナリ形式を持ちます。
それらをCの構造体そのままに頼るのか、明示的なエンコード/デコードを行うのかは、OS設計上の大事な判断になります。

### ３章の８　ストリームの書き込み

標準I/Oには、読み取り関数と同じように、書き込み用の関数も用意されています。
ここでは、よく使う次の3種類を見ます。

```text
文字単位
    fputc()

文字列単位
    fputs()

バイナリデータ
    fwrite()
```

ストリームへ書き込むには、書き込み可能なモードで開いておく必要があります。
たとえば `"w"`、`"a"`、`"r+"`、`"w+"`、`"a+"` などです。
`"r"` のような読み取り専用ストリームには、基本的に書き込めません。

```text
書き込み可能
    "w"
    "a"
    "r+"
    "w+"
    "a+"

読み取り専用
    "r"
```

標準I/Oの書き込みでは、データがすぐにカーネルへ渡されるとは限りません。
多くの場合、まずCライブラリ内部のユーザー空間バッファへ書き込まれ、必要なタイミングでまとめて `write()` 相当の処理が行われます。

```text
プログラム
    fputc()
    fputs()
    fwrite()

標準I/O内部バッファ
    ユーザー空間にある

カーネル
    write() によって渡される

ストレージ
    fsync() などで永続化を強める
```

このため、書き込み関数が成功したように見えても、最終的なエラーが `fflush()` や `fclose()` のタイミングで見えることがあります。
重要な出力では、最後の `fclose()` の戻り値も確認します。

#### ３章の８の１　文字単位の書き込み: fputc()

`fgetc()` と反対に、ストリームへ1文字を書き込む関数が `fputc()` です。

宣言は次のようになります。

```c
#include <stdio.h>

int fputc(int c, FILE *stream);
```

`fputc()` は、`c` を `unsigned char` として扱える値に変換し、`stream` へ書き込みます。
成功すると書き込んだ文字を `int` として返します。
失敗すると `EOF` を返し、`errno` に理由が入ります。

```text
成功
    書き込んだ文字を返す

失敗
    EOF を返す
    errno に理由が入る
```

簡単な例です。

```c
if (fputc('p', stream) == EOF) {
    perror("fputc");
}
```

この例では、文字 `p` を `stream` へ書き込みます。
`stream` は、あらかじめ書き込み可能なモードで開かれている必要があります。

実行可能な形にすると、次のようになります。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;

    stream = fopen("memo.txt", "a");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (fputc('p', stream) == EOF) {
        perror("fputc");
        fclose(stream);
        exit(1);
    }

    if (fputc('\n', stream) == EOF) {
        perror("fputc");
        fclose(stream);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

1文字ずつ `fputc()` を呼んでも、標準I/Oが内部でバッファリングするため、必ずしも毎回システムコールが発生するわけでは
ありません。
ただし、関数呼び出しの回数は増えるので、大量のデータを扱う場合は `fwrite()` なども検討します。

#### ３章の８の２　文字列の書き込み: fputs()

ヌル終端文字列をストリームへ書き込むには `fputs()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

int fputs(const char *str, FILE *stream);
```

`fputs()` は、`str` が指すヌル終端文字列を `stream` へ書き込みます。
文字列終端の `\0` 自体は書き込みません。

```text
"hello\n" を fputs() する
    h e l l o \n を書く
    最後の \0 は書かない
```

戻り値は次の通りです。

```text
成功
    非負の値

失敗
    EOF
    errno に理由が入る
```

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;

    stream = fopen("journal.txt", "a");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (fputs("today: stdio write test\n", stream) == EOF) {
        perror("fputs");
        fclose(stream);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

`fputs()` は `printf()` 系と違い、書式指定を解釈しません。
`%d` や `%s` のような変換指定を使いたい場合は、`fprintf()` を使います。

```text
fputs()
    文字列をそのまま書く

fprintf()
    書式付きで書く
```

ログや固定メッセージを書くなら `fputs()` は単純で便利です。
数値や変数を埋め込みたいなら `fprintf()` の方が自然です。

#### ３章の８の３　バイナリデータの書き込み: fwrite()

文字列ではなく、バイト列や構造体のようなデータをまとめて書き込むには `fwrite()` を使います。

現代の宣言は次のようになります。

```c
#include <stdio.h>

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
```

古い資料では第1引数が `void *buf` のように書かれていることがありますが、書き込み元データは変更されないため、現在の宣言では `const void *` です。

`fwrite()` は、`ptr` が指すメモリから、`size` バイトの要素を `nmemb` 個、`stream` へ書き込みます。

```text
ptr
    書き込み元バッファ

size
    1要素のサイズ

nmemb
    書き込みたい要素数

stream
    書き込み先ストリーム
```

戻り値は、書き込んだバイト数ではありません。
書き込めた要素数です。

```text
fwrite(buf, 64, 1, stream)
    64バイトの要素を1個書く
    成功なら1を返す

fwrite(buf, 1, 64, stream)
    1バイトの要素を64個書く
    64バイト書ければ64を返す
```

戻り値が `nmemb` より小さい場合は、途中までしか書けていないということです。
その場合は `ferror()` でエラー状態を確認します。

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned char data[4] = {0x55, 0xaa, 0x00, 0xff};
    FILE *stream;
    size_t nw;

    stream = fopen("data.bin", "wb");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    nw = fwrite(data, 1, sizeof(data), stream);

    if (nw != sizeof(data)) {
        if (ferror(stream)) {
            perror("fwrite");
        } else {
            fprintf(stderr, "short write\n");
        }

        fclose(stream);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

ここでは `size` を `1`、`nmemb` を `sizeof(data)` にしています。
この書き方だと、戻り値を「書けたバイト数」として扱いやすくなります。
一方、構造体1個を書きたい場合は、`sizeof(struct record)` を `size` にして、`nmemb` を `1` にする書き方もあります。

```text
バイト列として扱う
    fwrite(buf, 1, len, stream)

構造体1個として扱う
    fwrite(&record, sizeof(record), 1, stream)
```

ただし、前節で見た通り、構造体をそのままファイルに保存する方法は、移植性の面で注意が必要です。

### ３章の９　サンプルコード: ユーザー空間のバッファリング

ここまでに出てきた標準I/O関数を組み合わせて、小さなサンプルを見てみます。
このプログラムは、構造体を1個ファイルへ書き込み、その後同じファイルから読み戻して表示します。

ただし、このサンプルは「標準I/Oで読み書きする流れ」を見るためのものです。
構造体をそのまま永続化する設計を推奨する例ではありません。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct note_record {
    char title[64];
    unsigned int page;
    unsigned int flags;
};

static void die(const char *message) {
    perror(message);
    exit(1);
}

int main(void) {
    const char *path = "note-record.bin";
    struct note_record input;
    struct note_record output;
    FILE *stream;

    memset(&input, 0, sizeof(input));
    snprintf(input.title, sizeof(input.title), "stdio buffering");
    input.page = 76;
    input.flags = 1;

    stream = fopen(path, "wb");

    if (stream == NULL) {
        die("fopen");
    }

    if (fwrite(&input, sizeof(input), 1, stream) != 1) {
        if (ferror(stream)) {
            perror("fwrite");
        } else {
            fprintf(stderr, "short write\n");
        }

        fclose(stream);
        return 1;
    }

    if (fclose(stream) == EOF) {
        die("fclose");
    }

    stream = fopen(path, "rb");

    if (stream == NULL) {
        die("fopen");
    }

    if (fread(&output, sizeof(output), 1, stream) != 1) {
        if (ferror(stream)) {
            perror("fread");
        } else {
            fprintf(stderr, "short read or EOF\n");
        }

        fclose(stream);
        return 1;
    }

    if (fclose(stream) == EOF) {
        die("fclose");
    }

    printf("title=\"%s\" page=%u flags=%u\n",
           output.title, output.page, output.flags);

    return 0;
}
```

実行すると、次のような出力になります。

```text
title="stdio buffering" page=76 flags=1
```

このプログラムでは、`fopen()`、`fwrite()`、`fclose()`、`fread()` を使っています。
書き込み時は `"wb"`、読み取り時は `"rb"` を使っています。
Linuxでは `b` は実質的に無視されますが、バイナリファイルであることを明示でき、他環境への移植性も少し良くなります。

重要なのは、ファイルに書かれる形式です。
この例では、`struct note_record` のメモリ表現がそのままファイルへ出ます。
つまり、次の情報に依存します。

```text
依存するもの
    unsigned int のサイズ
    構造体のパディング
    エンディアン
    ABI
    コンパイラ設定
```

同じプログラム、同じCPU、同じABIで一時ファイルとして扱う程度なら問題になりにくいです。
しかし、長期保存するファイル形式、別のOSやCPUで読むファイル形式、ネットワークで交換する形式としては危険です。

より堅い設計では、固定幅整数型を使い、バイトオーダを決め、1フィールドずつエンコードします。

```text
研究用サンプル
    structをそのままfwrite()/fread()

実用的なファイル形式
    フィールドごとに明示的にエンコード/デコード
```

UmuOSでファイルシステムや実行ファイル形式を設計する場合も同じです。
カーネル内部の構造体をそのままディスクへ保存すると、将来の変更や別環境との互換性が難しくなります。
ディスク上の形式と、メモリ上の構造体は、分けて考えるのが基本です。

### ３章の１０　ストリームのシーク

標準I/Oでも、ファイル内の現在位置を移動できます。
低レベルI/Oでは `lseek()` を使いましたが、標準I/Oでは `fseek()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

int fseek(FILE *stream, long offset, int whence);
```

`fseek()` は、`stream` のストリームポジションを移動します。
ここでいうストリームポジションは、低レベルI/Oのファイルポジションに対応するものですが、標準I/Oの内部バッファも関係します。

`whence` には、次の値を指定します。

```text
SEEK_SET
    ファイル先頭から offset バイトの位置へ移動する

SEEK_CUR
    現在位置から offset バイト移動する

SEEK_END
    ファイル末尾から offset バイトの位置へ移動する
```

戻り値は次の通りです。

```text
成功
    0

失敗
    -1
    errno に理由が入る
```

`fseek()` が成功すると、ストリームのEOF状態はリセットされます。
また、`ungetc()` で押し戻した文字がある場合、その効果は失われます。

```text
fseek() 成功時の影響
    ストリーム位置が移動する
    EOF状態がリセットされる
    ungetc() で戻した文字は無効になる
```

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;
    int c;

    stream = fopen("memo.txt", "r");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (fseek(stream, 0, SEEK_SET) != 0) {
        perror("fseek");
        fclose(stream);
        exit(1);
    }

    c = fgetc(stream);

    if (c == EOF) {
        if (ferror(stream)) {
            perror("fgetc");
        } else {
            printf("EOF\n");
        }
    } else {
        printf("first char: %c\n", (char)c);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

#### ３章の１０の１　fsetpos()

標準I/Oには、`fsetpos()` もあります。

宣言は次のようになります。

```c
#include <stdio.h>

int fsetpos(FILE *stream, const fpos_t *pos);
```

`fsetpos()` は、`fpos_t` で表現された位置へストリームポジションを移動します。
位置を取得する側には、後で出てくる `fgetpos()` を使います。

```text
fgetpos()
    現在位置を fpos_t として保存する

fsetpos()
    保存した fpos_t の位置へ戻す
```

`fpos_t` は、単なる整数とは限りません。
これは、Unix以外の環境や、マルチバイト文字の状態などを含めて位置を表す必要がある場合を考慮した型です。

Linux上の通常のバイナリファイルやテキストファイルを扱うだけなら、`fseek()` と `ftell()` で足りる場面が多いです。
ただし、標準Cとしてより移植性を意識するなら、`fgetpos()` / `fsetpos()` の組み合わせも知っておく価値があります。

#### ３章の１０の２　rewind()

ストリーム位置を先頭へ戻すだけなら、`rewind()` が使えます。

宣言は次のようになります。

```c
#include <stdio.h>

void rewind(FILE *stream);
```

`rewind(stream)` は、概念的には次の処理に近いです。

```c
fseek(stream, 0, SEEK_SET);
```

ただし、`rewind()` はストリームのエラー状態もクリアします。
また、戻り値がありません。
そのため、エラーを直接戻り値で確認できません。

```text
rewind()
    ストリーム位置を先頭へ戻す
    EOF状態とエラー状態をクリアする
    戻り値がない
```

エラーを確認したい場合は、`fseek(stream, 0, SEEK_SET)` を使う方が分かりやすいです。
古い資料では、`errno` を0にしてから `rewind()` し、呼び出し後の `errno` を見る例があります。
しかし、現代の実用コードでは、エラー処理が必要なら戻り値のある `fseek()` を選ぶ方が読みやすいです。

```c
if (fseek(stream, 0, SEEK_SET) != 0) {
    perror("fseek");
}
```

### ３章の１１　ストリームポジション

低レベルI/Oの `lseek()` は、新しいファイルポジションを戻り値として返します。
一方、`fseek()` は成功時に `0` を返すだけで、現在位置は返しません。

標準I/Oで現在位置を知るには `ftell()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

long ftell(FILE *stream);
```

戻り値は次の通りです。

```text
成功
    現在のストリーム位置

失敗
    -1L
    errno に理由が入る
```

例です。

```c
long pos;

pos = ftell(stream);

if (pos == -1L) {
    perror("ftell");
} else {
    printf("position=%ld\n", pos);
}
```

ただし、`long` でファイル位置を表すインタフェースは、巨大なファイルを扱う場合に気になることがあります。
現代Linuxで大きなファイルをPOSIX的に扱うなら、`fseeko()` と `ftello()` も覚えておくとよいです。

```c
#include <stdio.h>

int fseeko(FILE *stream, off_t offset, int whence);
off_t ftello(FILE *stream);
```

`off_t` は、低レベルI/Oの `lseek()` でも出てきたファイルオフセット用の型です。
Linuxでファイル位置を本格的に扱うなら、`long` よりも `off_t` の方が自然な場面があります。

#### ３章の１１の１　fgetpos()

`fsetpos()` と対になる関数が `fgetpos()` です。

宣言は次のようになります。

```c
#include <stdio.h>

int fgetpos(FILE *stream, fpos_t *pos);
```

`fgetpos()` は、現在のストリーム位置を `pos` へ保存します。
成功すると `0`、失敗すると `-1` を返し、`errno` に理由が入ります。

```c
fpos_t pos;

if (fgetpos(stream, &pos) != 0) {
    perror("fgetpos");
}

/* ここで何か読み書きする */

if (fsetpos(stream, &pos) != 0) {
    perror("fsetpos");
}
```

貼り付け元の古い資料では `<stdioh.h>` のような表記ゆれが見えることがありますが、正しくは `<stdio.h>` です。
このような小さなヘッダ名の誤植は、実際にコンパイルするとすぐ分かります。

### ３章の１２　ストリームのフラッシュ

標準I/Oは、ユーザー空間にバッファを持ちます。
そのため、`fputs()` や `fwrite()` を呼んでも、データがすぐにカーネルへ渡されるとは限りません。

このユーザー空間バッファを明示的にフラッシュする関数が `fflush()` です。

宣言は次のようになります。

```c
#include <stdio.h>

int fflush(FILE *stream);
```

`fflush(stream)` は、その出力ストリームに残っている未書き込みデータを、Cライブラリのバッファからカーネルへ渡します。

戻り値は次の通りです。

```text
成功
    0

失敗
    EOF
    errno に理由が入る
```

`stream` に `NULL` を渡すと、プロセス内で開かれているすべての出力ストリームをフラッシュします。

```c
if (fflush(stream) == EOF) {
    perror("fflush");
}

if (fflush(NULL) == EOF) {
    perror("fflush");
}
```

ここで非常に重要なのは、`fflush()` は「ディスクへ物理的に保存する」関数ではないという点です。

```text
fflush()
    Cライブラリのユーザー空間バッファをカーネルへ渡す

fsync()
    カーネル側の dirty page をストレージへ反映させる

fdatasync()
    主にファイルデータの永続化を強める
```

つまり、`fflush()` の後でも、データはまだカーネルのページキャッシュ上にあるだけかもしれません。
電源断やカーネルクラッシュまで考えるなら、`fsync()` や `fdatasync()` が必要になります。

標準I/Oの `FILE *` から対応するファイルディスクリプタを得るには、POSIXの `fileno()` を使います。

```c
#include <stdio.h>

int fileno(FILE *stream);
```

重要なファイルを保存する例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    FILE *stream;
    int fd;

    stream = fopen("important.txt", "w");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (fputs("important data\n", stream) == EOF) {
        perror("fputs");
        fclose(stream);
        exit(1);
    }

    if (fflush(stream) == EOF) {
        perror("fflush");
        fclose(stream);
        exit(1);
    }

    fd = fileno(stream);

    if (fd < 0) {
        perror("fileno");
        fclose(stream);
        exit(1);
    }

    if (fsync(fd) != 0) {
        perror("fsync");
        fclose(stream);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

この順番が大事です。

```text
1. fputs() / fwrite()
    stdioのバッファへ書く

2. fflush()
    stdioのバッファをカーネルへ渡す

3. fsync()
    カーネル側のデータをストレージへ反映する

4. fclose()
    ストリームを閉じ、最後のエラーも確認する
```

`fflush()` せずに `fsync(fileno(stream))` だけを呼ぶと、まだCライブラリのバッファ内に残っているデータはカーネルへ渡されていない可能性があります。
その場合、`fsync()` はその未送信データを保存できません。

```text
危ない理解
    fwrite() したから fsync() すれば全部保存される

正しい理解
    fwrite() のデータは stdio バッファに残ることがある
    先に fflush() してから fsync() する
```

また、`fflush()` は入力ストリームに対して使うものではありません。
標準Cで意味が明確なのは出力ストリーム、または直前の操作が出力である更新ストリームです。
glibc/POSIXでは入力ストリームに対する拡張的な挙動もありますが、移植性を考えるなら、入力の読み捨て目的で `fflush(stdin)` のように書くのは避けます。

```text
避ける
    fflush(stdin)

理由
    標準Cとして移植性のある使い方ではない
    入力の掃除は別の方法で明示的に行う
```

UmuOSの視点では、`fflush()` と `fsync()` の違いは重要です。
`fflush()` はユーザー空間のCライブラリの責任範囲です。
`fsync()` はカーネルとファイルシステムの責任範囲です。

```text
UmuOSで分けて考える層

ユーザー空間Cライブラリ
    FILE構造体
    stdioバッファ
    fflush()

カーネル
    ファイルディスクリプタ
    page cache
    dirty page
    fsync()

デバイス/ストレージ
    実際の永続化
```

この層の違いを理解しておくと、「printfしたのにファイルへ出ない」「fwriteしたのに電源断で消えた」「fsyncしたのにstdioバッファの分が残っていた」といった混乱を避けやすくなります。

### ３章の１３　エラーとEOF

標準I/Oの関数の中には、戻り値だけでは「エラーが起きた」のか「EOFに達した」のか分かりにくいものがあります。
代表例は `fread()`、`fgets()`、`fgetc()` です。

```text
fgetc()
    EOFを返す
    それが本当のEOFなのか、エラーなのかは戻り値だけでは分からない

fgets()
    NULLを返す
    それがEOFなのか、エラーなのかは戻り値だけでは分からない

fread()
    要素数が足りない
    それがEOFなのか、エラーなのかは戻り値だけでは分からない
```

このため、標準I/Oではストリームの状態を確認する関数が用意されています。
エラー状態を見るには `ferror()`、EOF状態を見るには `feof()` を使います。

#### ３章の１３の１　エラー状態を調べる: ferror()

`ferror()` は、ストリームにエラー状態がセットされているかを調べます。

宣言は次のようになります。

```c
#include <stdio.h>

int ferror(FILE *stream);
```

戻り値は次の通りです。

```text
エラー状態がある
    非0

エラー状態がない
    0
```

エラー状態は、`fgetc()`、`fgets()`、`fread()`、`fwrite()` など、他の標準I/O関数が失敗したときにストリームへ記録します。
`ferror()` 自体が新しくI/Oするわけではありません。
すでにストリームに記録されている状態を見るだけです。

#### ３章の１３の２　EOF状態を調べる: feof()

`feof()` は、ストリームにEOF状態がセットされているかを調べます。

宣言は次のようになります。

```c
#include <stdio.h>

int feof(FILE *stream);
```

戻り値は次の通りです。

```text
EOF状態がある
    非0

EOF状態がない
    0
```

ここで大事なのは、`feof()` は「次に読むとEOFになるか」を予言する関数ではない、ということです。
実際に読み取りを行い、その結果としてファイル終端に到達したときにEOF状態がセットされます。

```text
誤解しやすい考え方
    読む前に feof() でEOFか確認する

正しい考え方
    まず読む
    読めなかったら feof() / ferror() で理由を見る
```

このため、次のような書き方は避けます。

```c
while (!feof(stream)) {
    /* 読む */
}
```

この書き方は、最後の読み取り処理を誤って扱いやすいです。
標準I/Oでは、「読み取り関数の戻り値でループし、失敗したら理由を確認する」という形にします。

#### ３章の１３の３　状態をクリアする: clearerr()

`clearerr()` は、ストリームのエラー状態とEOF状態をクリアします。

宣言は次のようになります。

```c
#include <stdio.h>

void clearerr(FILE *stream);
```

`clearerr()` に戻り値はありません。
また、状態をクリアすると、あとから「どんな状態だったか」を復元することはできません。
そのため、`ferror()` や `feof()` で必要な確認を済ませた後に使います。

```text
clearerr(stream)
    エラー状態をクリアする
    EOF状態もクリアする
    戻り値はない
```

例です。

```c
#include <stdio.h>

void print_stream_state(FILE *stream) {
    if (ferror(stream)) {
        printf("error state is set\n");
    }

    if (feof(stream)) {
        printf("EOF state is set\n");
    }

    clearerr(stream);
}
```

#### ３章の１３の４　読み取りでの典型パターン

`fgetc()` の典型的な読み取りパターンは次のようになります。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;
    int c;

    stream = fopen("memo.txt", "r");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    while ((c = fgetc(stream)) != EOF) {
        putchar(c);
    }

    if (ferror(stream)) {
        perror("fgetc");
        fclose(stream);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

このコードでは、`feof()` をループ条件に使っていません。
まず `fgetc()` で読み、`EOF` が返ったらループを抜け、最後に `ferror()` でエラーだったかを確認しています。

`fread()` でも考え方は同じです。

```c
size_t nr;

nr = fread(buf, 1, sizeof(buf), stream);

if (nr < sizeof(buf)) {
    if (ferror(stream)) {
        perror("fread");
    } else if (feof(stream)) {
        printf("EOF\n");
    }
}
```

Ushで設定ファイルや履歴ファイルを読むときにも、この考え方は役に立ちます。
「読めなかった」という事実だけで終わらせず、EOFなのか、権限エラーやI/Oエラーなのかを分けて扱うと、ユーザーに出すエラーメッセージも正確になります。

### ３章の１４　ストリームとファイルディスクリプタ

標準I/Oの `FILE *` は、内部的には多くの場合ファイルディスクリプタに対応しています。
この対応するfdを取り出したい場合は、POSIXの `fileno()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

int fileno(FILE *stream);
```

戻り値は次の通りです。

```text
成功
    対応するファイルディスクリプタ

失敗
    -1
    errno に理由が入る
```

`fileno()` が必要になるのは、標準I/Oだけでは足りない操作をしたい場合です。
たとえば、`fsync()`、`fcntl()`、`poll()` など、fdを要求するAPIと組み合わせたいときです。

```text
FILE * が必要な関数
    fgets()
    fprintf()
    fread()
    fwrite()

fd が必要な関数
    read()
    write()
    fsync()
    fcntl()
    poll()
```

ただし、標準I/Oと低レベルI/Oを同じ対象に混ぜるのは注意が必要です。
標準I/Oはユーザー空間に内部バッファを持っているため、fdの現在位置やデータの見え方が直感とずれることがあります。

```text
混在で起きやすい混乱
    stdioが先読みしている
    stdioの書き込みがまだfflush()されていない
    fdの位置とstdio内部バッファの位置がずれる
```

低レベルI/Oをどうしても混ぜる場合は、少なくとも出力ストリームでは事前に `fflush()` します。
入力ストリームの場合は、標準I/Oがどれだけ先読みしているかが絡むため、設計として混ぜない方が安全です。

```c
int fd;

if (fflush(stream) == EOF) {
    perror("fflush");
    return;
}

fd = fileno(stream);

if (fd < 0) {
    perror("fileno");
    return;
}

if (fsync(fd) != 0) {
    perror("fsync");
}
```

UmuOSの設計視点では、`FILE *` とfdの境界は大事です。
fdはカーネルが提供する抽象であり、`FILE *` はユーザー空間Cライブラリが提供する抽象です。

```text
fd
    カーネルが管理する
    open file descriptionにつながる
    read/write/lseek/fsyncなどで使う

FILE *
    Cライブラリが管理する
    内部バッファを持つ
    fgets/fprintf/fread/fwriteなどで使う
```

Ushのリダイレクトやパイプはfd中心で考える方が自然です。
一方、設定ファイルや履歴ファイルを行単位で読む処理は `FILE *` が便利です。
この2つを同じ対象で安易に混ぜない、というのが基本方針です。

### ３章の１５　バッファリングの制御

標準I/Oのバッファリングには、大きく3種類あります。

```text
バッファリングなし
    ユーザー空間でほぼ溜めずにカーネルへ渡す

ラインバッファリング
    改行文字が出るタイミングなどでフラッシュする

フルバッファリング
    バッファが一杯になるまで溜めてからまとめて渡す
```

古い資料では「ブロックバッファリング」と呼ぶこともあります。
標準I/Oの用語では、`_IOFBF` の full buffering、つまりフルバッファリングと呼ばれます。

典型的なLinux/glibcの挙動は次のようになります。

```text
標準入力
    端末ならラインバッファリングに近い挙動で扱われることが多い

標準出力
    端末ならラインバッファリング
    ファイルやパイプならフルバッファリング

標準エラー出力
    通常はバッファリングなし
```

このため、端末へ `printf("hello\n")` するとすぐ表示されるのに、ファイルへリダイレクトするとまとめて出る、という違いが起きます。

```text
端末
    ./a.out
    改行で表示されやすい

ファイルへリダイレクト
    ./a.out > log.txt
    バッファに溜まってから書かれやすい
```

#### ３章の１５の１　setvbuf()

バッファリングの種類やバッファサイズを変更するには `setvbuf()` を使います。

宣言は次のようになります。

```c
#include <stdio.h>

int setvbuf(FILE *stream, char *buf, int mode, size_t size);
```

`mode` には次の値を指定します。

```text
_IONBF
    バッファリングなし

_IOLBF
    ラインバッファリング

_IOFBF
    フルバッファリング
```

`buf` に自分で用意したバッファを渡すと、標準I/Oはその領域をバッファとして使います。
`buf` に `NULL` を渡すと、Cライブラリ側がバッファを用意します。

```text
buf != NULL
    ユーザーが用意したバッファを使う

buf == NULL
    Cライブラリがバッファを確保する
```

`setvbuf()` は、ストリームを開いた後、他のI/O操作をする前に呼びます。
すでに読み書きした後でバッファリング方式を変えようとすると、移植性のある正しい使い方ではなくなります。

```text
正しい順番
    fopen()
    setvbuf()
    fgets()/fputs()/fread()/fwrite()
    fclose()
```

例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *stream;

    stream = fopen("log.txt", "w");

    if (stream == NULL) {
        perror("fopen");
        exit(1);
    }

    if (setvbuf(stream, NULL, _IOFBF, BUFSIZ) != 0) {
        fprintf(stderr, "setvbuf failed\n");
        fclose(stream);
        exit(1);
    }

    if (fputs("hello\n", stream) == EOF) {
        perror("fputs");
        fclose(stream);
        exit(1);
    }

    if (fclose(stream) == EOF) {
        perror("fclose");
        exit(1);
    }

    return 0;
}
```

ここでは `buf` に `NULL` を渡しているため、Cライブラリにバッファ管理を任せています。
通常はこの形で十分です。

#### ３章の１５の２　自前バッファの寿命

`setvbuf()` に自分で用意したバッファを渡す場合、そのバッファはストリームを閉じるまで有効でなければなりません。
ここはよくある落とし穴です。

たとえば、次のようなコードは危険です。

```c
#include <stdio.h>

static void setup_stdout(void) {
    char buf[BUFSIZ];

    setvbuf(stdout, buf, _IOFBF, sizeof(buf));
}

int main(void) {
    setup_stdout();
    printf("hello\n");
    return 0;
}
```

`buf` は `setup_stdout()` のローカル変数です。
関数から戻ると、この配列の寿命は終わります。
しかし、`stdout` はその後もこのバッファを使おうとする可能性があります。

```text
危険な流れ
    ローカル変数bufをsetvbuf()へ渡す
    関数から戻る
    bufの寿命が終わる
    stdioが無効な領域をバッファとして使う可能性がある
```

安全寄りにするなら、`static` な領域を使うか、ストリームを閉じるまで寿命が続く領域を使います。

```c
#include <stdio.h>

static char stdout_buf[BUFSIZ];

int main(void) {
    if (setvbuf(stdout, stdout_buf, _IOFBF, sizeof(stdout_buf)) != 0) {
        return 1;
    }

    printf("hello\n");
    fflush(stdout);

    return 0;
}
```

ただし、一般的には `setvbuf()` 自体を使う機会は多くありません。
ほとんどの場合、標準I/Oのデフォルトバッファリングは十分に良い選択です。

#### ３章の１５の３　BUFSIZについて

`BUFSIZ` は `<stdio.h>` で定義される定数です。
標準I/Oのバッファサイズとしてよく使われます。

```c
#include <stdio.h>

char buf[BUFSIZ];
```

ただし、`BUFSIZ` は「どの用途でも必ず最適」という意味ではありません。
実装が選んだ標準的なサイズであり、普通のファイルI/Oでは十分なことが多い、くらいに考えるとよいです。

大量データのI/O性能を本気で詰める場合は、ファイルシステム、デバイス、ページサイズ、アクセスパターン、CPUキャッシュなども関係します。
最終的には実測が必要です。

UmuOSを作る視点では、`setvbuf()` は「ユーザー空間Cライブラリがバッファ戦略を持つ」という具体例です。
カーネルのpage cacheとは別に、ユーザー空間にもI/Oをまとめる層がある、ということを意識しておくとよいです。

### ３章の１６　スレッドセーフ

スレッドは、1つのプロセス内で実行される複数の処理の流れです。
同じアドレス空間を共有するため、同じ変数、同じヒープ、同じ `FILE *` に複数のスレッドから触れることができます。

この共有は便利ですが、同時に競合の原因にもなります。
2つのスレッドが同じストリームへ同時に書き込むと、出力の順序やまとまりが問題になります。

POSIX環境の標準I/O関数は、多くの場合スレッドセーフです。
glibcでも、ストリームごとにロックを持ち、1つの標準I/O関数呼び出しの内部でデータ構造が壊れないようにしています。

```text
標準I/Oのスレッド安全性
    FILEごとにロックを持つ
    fputs() 1回などの内部状態は保護される
    ただし複数回の関数呼び出し全体までは自動で1まとまりにならない
```

たとえば、`fputs("A\n", stream)` 1回の内部状態は守られても、次のような複数行の出力全体が他スレッドの出力と混ざらない保証はありません。

```text
thread 1
    fputs("begin\n", stream)
    fputs("data\n", stream)
    fputs("end\n", stream)

thread 2
    fputs("other\n", stream)
```

「begin、data、endをひとまとまりとして出したい」なら、より広い範囲を自分でロックする必要があります。

#### ３章の１６の１　ストリームの手動ロック

標準I/Oには、ストリームを手動でロックする関数があります。

```c
#include <stdio.h>

void flockfile(FILE *stream);
int ftrylockfile(FILE *stream);
void funlockfile(FILE *stream);
```

`flockfile()` は、ストリームのロックを取得します。
すでに他のスレッドがロックしている場合は、解放されるまで待ちます。

`funlockfile()` は、ロックを解放します。
同じスレッドで複数回 `flockfile()` した場合は、同じ回数だけ `funlockfile()` する必要があります。

`ftrylockfile()` は、ブロックしない版です。
ロックできれば `0`、できなければ非0を返します。

```text
flockfile()
    ロックできるまで待つ

ftrylockfile()
    ロックできなければ待たずに戻る

funlockfile()
    ロックを解放する
```

例です。

```c
flockfile(stream);

fputs("begin\n", stream);
fputs("data\n", stream);
fputs("end\n", stream);

funlockfile(stream);
```

この範囲では、他のスレッドが同じストリームを使う標準I/O関数を呼ぼうとしても、通常はロックが解放されるまで待つことになります。
複数の `fputs()` を1つのまとまりとして扱いやすくなります。

実用コードでは、途中で `return` したりエラー処理へ飛んだりしても `funlockfile()` を忘れないように設計します。
CではRAIIがないため、`goto out_unlock;` のような後始末経路を作ることがあります。

```c
flockfile(stream);

if (fputs("begin\n", stream) == EOF) {
    goto out_unlock;
}

if (fputs("data\n", stream) == EOF) {
    goto out_unlock;
}

if (fputs("end\n", stream) == EOF) {
    goto out_unlock;
}

out_unlock:
funlockfile(stream);
```

#### ３章の１６の２　ロックしないストリーム操作

標準I/Oには、内部ロックを省略する `_unlocked` 系の関数があります。
これらは通常の関数より軽い場合がありますが、ロックを自分で管理する責任が出ます。

代表例です。

```c
#include <stdio.h>

int getc_unlocked(FILE *stream);
int putc_unlocked(int c, FILE *stream);
int getchar_unlocked(void);
int putchar_unlocked(int c);
```

glibcでは、さらに多くの `_unlocked` 系関数も提供されています。

```c
#define _GNU_SOURCE
#include <stdio.h>

char *fgets_unlocked(char *str, int size, FILE *stream);
size_t fread_unlocked(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fputs_unlocked(const char *str, FILE *stream);
size_t fwrite_unlocked(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush_unlocked(FILE *stream);
int feof_unlocked(FILE *stream);
int ferror_unlocked(FILE *stream);
void clearerr_unlocked(FILE *stream);
```

古い資料では、これらをまとめてLinux固有と説明していることがあります。
現在はPOSIXで定義されているものと、glibc/GNU拡張のものが混在しています。
移植性が必要なコードでは、使う関数がどの規格や実装に属するかを確認する必要があります。

`_unlocked` 系を使う典型的な形は、手動でロックしてから、その範囲内でロックなし関数を使う形です。

```c
flockfile(stream);

for (;;) {
    int c;

    c = getc_unlocked(stream);

    if (c == EOF) {
        break;
    }

    putchar_unlocked(c);
}

funlockfile(stream);
```

ただし、現代の通常のアプリケーションでは、まず普通の `fgetc()`、`fputs()`、`fread()` などを使えば十分です。
`_unlocked` 系は、性能上の理由が明確で、かつスレッド安全性を自分で説明できる場合にだけ使う、と考える方が安全です。

UmuOSの視点では、この話はCライブラリ内部のロック設計につながります。
将来 `FILE` 相当を作るなら、単にバッファを持つだけでなく、マルチスレッド環境でストリームをどう保護するかも設計課題になります。

### ３章の１７　標準I/Oライブラリに対する批判

標準I/Oは非常に広く使われています。
`printf()`、`fgets()`、`fread()`、`fwrite()` は、Cプログラムを書く上で避けて通れないほど基本的な関数です。

しかし、標準I/Oにも批判があります。
代表的な論点は次のようなものです。

```text
標準I/Oへの批判
    エラーとEOFの扱いが分かりにくい
    バッファリングが見えにくい
    fdとの混在が危険
    二重コピーが発生する
    古い関数には危険なものがある
```

特に有名なのが、古いCライブラリにあった `gets()` です。
`gets()` は入力先バッファのサイズを指定できないため、バッファオーバーフローを避けられません。
この関数はC11で標準から削除されました。
現代のコードでは使ってはいけません。

```text
使わない
    gets()

代わりに検討する
    fgets()
    getline()
```

ただし、`fgets()` も万能ではありません。
固定長バッファを渡すため、長い行をどう扱うかはプログラム側で決める必要があります。
任意長の行を扱いたい場合は、POSIXの `getline()` が便利です。

#### ３章の１７の１　二重コピーの問題

標準I/Oは、ユーザー空間にバッファを持ちます。
これはシステムコール回数を減らす効果がありますが、一方でコピー回数が増える場合があります。

読み取りでは、概念的には次のような流れになります。

```text
1. カーネル
    ファイルからpage cacheへ読む

2. read()相当
    カーネル空間からstdio内部バッファへコピーする

3. fgetc()/fread()など
    stdio内部バッファからアプリケーションの変数やバッファへ渡す
```

書き込みでは逆方向です。

```text
1. アプリケーション
    自分のバッファにデータを持つ

2. fwrite()/fputs()など
    stdio内部バッファへコピーする

3. write()相当
    stdio内部バッファからカーネルへコピーする
```

このように、標準I/Oは便利さと引き換えに、コピーが1段増えることがあります。
普通の設定ファイル、ログ、テキスト処理では問題にならないことが多いです。
しかし、大量データを高速に処理する場合や、ネットワーク・ストレージの性能を詰める場合は、無視できないことがあります。

#### ３章の１７の２　scatter-gather I/Oとwritev()

標準I/Oの二重コピーを避ける考え方の1つに、scatter-gather I/Oがあります。
複数のバッファを1回のI/Oでまとめて扱う方法です。

Linux/POSIXでは、書き込み側に `writev()` があります。

```c
#include <sys/uio.h>

ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```

`writev()` を使うと、複数のメモリ領域をまとめてfdへ書き込めます。

```text
通常のwrite()
    1つの連続したバッファを書く

writev()
    複数のバッファをまとめて書く
```

たとえば、ヘッダ、本文、改行を別々のバッファに持ったまま、まとめて書けます。

```text
iov[0]
    header

iov[1]
    body

iov[2]
    newline
```

このような仕組みは、次章以降で低レベルI/Oをさらに深く見るときに重要になります。
UshやUmuOSでログ出力、パケット生成、ファイル形式の出力を考える場合にも、複数のバッファをどうまとめるかは設計テーマになります。

#### ３章の１７の３　標準I/Oをどう使うべきか

批判があるからといって、標準I/Oを避けるべき、という話ではありません。
標準I/Oは、今でも非常に有用です。

```text
標準I/Oが向くもの
    設定ファイル
    ログ
    テキスト処理
    研究用プログラム
    単純なバイナリファイル処理

低レベルI/Oを検討するもの
    シェルのリダイレクトやパイプ
    ソケット
    ノンブロッキングI/O
    poll()/epoll() と組み合わせる処理
    大量データの性能が重要な処理
```

大事なのは、どちらが偉いかではありません。
`FILE *` とfdの層の違い、バッファリングの位置、エラー確認の方法、永続化の境界を理解して使い分けることです。

### ３章の１８　３章のまとめ

この章では、I/Oのバッファリングを整理しました。
2章の低レベルI/Oでは、fd、`open()`、`read()`、`write()`、`close()`、`lseek()` を見ました。
3章では、その上に乗る標準I/O、つまり `FILE *` とストリームの世界を見ました。

重要な整理は次の通りです。

```text
低レベルI/O
    カーネルに近い
    fdを使う
    read()/write()/lseek()/fsync() など

標準I/O
    Cライブラリが提供する
    FILE *を使う
    fopen()/fgets()/fread()/fprintf()/fclose() など
    ユーザー空間バッファを持つ
```

標準I/Oは便利ですが、見えないバッファを持つため、次の点に注意が必要です。

```text
注意点
    fgetc()などはEOFとエラーを戻り値だけで区別しにくい
    ferror()/feof()/clearerr()で状態を見る
    fwrite()の戻り値はバイト数ではなく要素数
    fflush()はディスク永続化ではない
    fsync()する前にfflush()が必要な場面がある
    FILE *とfdの混在は慎重に行う
    構造体のバイナリ保存は移植性に注意する
```

UmuOSの視点では、この章はユーザー空間Cライブラリの設計そのものです。
`FILE` 相当の構造体、内部バッファ、flush、EOF状態、エラー状態、fdとの対応、ロック、バッファリング制御をどう作るかは、OS上で動くC環境を育てるときの大きなテーマになります。

Ushの視点では、シェル本体のパイプやリダイレクトはfd中心、設定ファイルや履歴ファイルの読み書きは標準I/O中心、という使い分けが現実的です。

```text
Ushでの使い分け

fd中心
    pipe
    dup2
    fork/exec前後のリダイレクト
    poll/epollを使う処理

stdio中心
    設定ファイルの読み込み
    履歴ファイルの読み書き
    人間向けログの出力
```

これで、3章のI/Oバッファリングは一区切りです。
次に進むときは、低レベルI/Oと標準I/Oの違いを頭に置いたまま、より複雑なI/O、ベクトルI/O、非同期I/O、メモリマップI/Oなどへつなげていくと理解しやすくなります。












