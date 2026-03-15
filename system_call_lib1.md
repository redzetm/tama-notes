---
title: "UmuOSの為のC言語　Linuxシステムコール、低レベルライブラリ関数１（ファイルI/O）"
---

# UmuOSの為のC言語 — Linuxシステムコール／低レベルライブラリ関数 1（ファイルI/O）

このシリーズは最終的に、UmuOS／ush／uim のソースコードを自力で読めるようになることを目標にします。  
ただしこの「system_call_lib」シリーズは UmuOS に一切依存しない形で、Linuxのシステムコールと、Cライブラリが提供する低レベル寄りの入出力関数を、できる限りやさしく・丁寧に説明します。

この第1冊は「ファイルI/O」を扱います。

前提：

- OS：Linux想定
- コンパイラ：`gcc`（`clang`でも基本同じ）
- 規格：`-std=c17`

コンパイルの基本形（例）：

```bash
gcc -Wall -Wextra -std=c17 -O0 sample.c -o sample
./sample
```

このテキストで扱う「2系統のI/O」：

- システムコール系：`open` / `read` / `write` / `close` / `lseek` / `pread` / `pwrite` / ...
  - 典型ヘッダ：`<unistd.h>` / `<fcntl.h>` / `<sys/stat.h>`
  - 「ファイルディスクリプタ（fd）」を使う
- 標準I/O（stdio）系：`fopen` / `fread` / `fwrite` / `fprintf` / `fgets` / ...
  - 典型ヘッダ：`<stdio.h>`
  - 「ストリーム（FILE*）」を使う

どちらが正しい／間違いではなく、目的で使い分けます。

---

## 第一章でやること：

- LinuxのファイルI/Oを「fd（ファイルディスクリプタ）」の考え方で理解する
- `open` / `read` / `write` / `close` を最小の安全な形で使える
- 戻り値と `errno` によるエラー処理の基本を身につける
- `lseek` によるシーク（ファイル位置の移動）を理解する
- `pread` / `pwrite`（ファイル位置指定I/O）を理解する
- `truncate` / `ftruncate`（ファイルトランケート）を理解する
- 同期I/O（`O_SYNC` / `fsync` など）と、なぜ必要かを説明できる
- ダイレクトI/O（`O_DIRECT`）が何を狙うかと、制約（アラインメント等）を理解する
- I/O多重化（`select`/`poll`/`epoll`）の位置づけを理解し、最小の `poll` を書ける
- カーネル内で何が起きるか（VFS、ページキャッシュ、ユーザー空間とのコピー）を「雰囲気で」説明できる
- 標準I/Oのバッファリングと、システムコールI/Oとの関係を整理できる

---

## 第一章　ファイルI/O

この章は「Linuxでファイルを読む・書く」の土台です。
最初に “道具” の前提（fd、戻り値、errno）を揃え、次に各APIを積み上げます。


### 1. ファイルI/O（システムコール）

#### ファイルのオープン

LinuxでファイルI/Oをする最初の入口が `open` です。

- `open` は「ファイルを開いて、fd（小さな整数）を返す」
- fd は「カーネル側の開かれたファイル」を参照するための番号

```c
/* file: open_min.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("hello.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    close(fd);
    return 0;
}
```

ポイント：

- 成功：fd は 0以上の整数
- 失敗：-1 を返す（この例では `< 0` で判定）
- 失敗理由は `errno` に入るので、`perror` で最小の表示ができる

よく使うフラグ：

- `O_RDONLY`：読み込み
- `O_WRONLY`：書き込み
- `O_RDWR`：読み書き
- `O_CREAT`：なければ作る（このときパーミッション指定が必要）
- `O_TRUNC`：開いたときに長さを0にする（上書き）
- `O_APPEND`：追記（末尾に追加）

作成を伴う例：

```c
/* file: open_create.c */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    close(fd);
    return 0;
}
```

注意：

- `O_CREAT` を付けた `open` は引数が1つ増える（作成時のモード）
- 実際のパーミッションは `umask` の影響を受ける（指定がそのままにはならないことがある）


#### ファイル読み取り：read()

`read` は「fdからバイト列を読む」関数です。

- 成功：読めたバイト数（0以上）
- 0：EOF（終端。これ以上読めない）
- -1：失敗（`errno` に理由）

```c
/* file: read_all.c */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("hello.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char buf[256];

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                /* シグナルで中断された。やり直す */
                continue;
            }
            perror("read");
            close(fd);
            return 1;
        }
        if (n == 0) {
            /* EOF */
            break;
        }

        /* 読んだぶんだけ標準出力へ書く（writeで） */
        ssize_t m = write(STDOUT_FILENO, buf, (size_t)n);
        if (m < 0) {
            perror("write");
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}
```

ポイント：

- `read` の戻り値は `ssize_t`（負を取れる `size_t` みたいな型）
- `read` は「要求したサイズより少なく返す」ことがある（特にパイプやソケット）

注意（重要）：

- `write` も同様に「全部書けない」ことがある（後で `write_all` を作る）


#### ファイル書き込み：write()

`write(fd, buf, len)` は「fdにバイト列を書く」関数です。

- 成功：書けたバイト数（0以上）
- -1：失敗

典型的には「全部書くまでループ」します。

```c
/* file: write_all.c */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t left = len;

    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (n == 0) {
            /* 通常のファイルで 0 はあまり起きないが、安全のため */
            return 0;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }

    return 1;
}

int main(void)
{
    int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    const char *msg = "hello write()\n";
    if (!write_all(fd, msg, strlen(msg))) {
        perror("write");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
```

ポイント：

- 「全部書く」関数を作っておくと、ソースを読むときも書くときも楽になる


#### 同期I/O

「書いたはずのデータが、電源断で消える」などの問題を避けたい場面があります。
このときに関係するのが同期I/Oです。

整理：

- `write` が成功しても、「ストレージに確実に永続化した」とは限らない
- 多くの場合、カーネルのページキャッシュやデバイスのキャッシュに溜まり、後でまとめて書かれる

同期の代表：

- `fsync(fd)`：fdに紐づく内容をストレージに反映するよう要求
- `fdatasync(fd)`：データ中心（メタデータを最小限）
- `open(..., O_SYNC)`：書き込みごとに同期を強める

最小例：

```c
/* file: fsync_demo.c */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    int fd = open("sync.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    const char *msg = "must be durable\n";
    if (write(fd, msg, strlen(msg)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    if (fsync(fd) != 0) {
        perror("fsync");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
```

注意：

- 同期を強めるほど遅くなることが多い
- どこまでの耐障害性が必要かで選ぶ（ログ、DBなどは重要）


#### ダイレクトI/O

`O_DIRECT` は「ページキャッシュをなるべく使わずにI/Oする」方向のフラグです。
狙い：

- 大容量の読み書きでページキャッシュを汚したくない
- アプリ側でバッファ管理をしたい

ただし制約が強く、最初は概念だけ押さえます。

よくある制約（環境による）：

- バッファアドレスが特定の境界に揃っている必要がある（アラインメント）
- 書き込みサイズやオフセットがブロックサイズの倍数である必要がある

注意：

- `O_DIRECT` は「速い魔法」ではない
- 条件が合わないと `EINVAL`（不正引数）で失敗する


#### ファイルクローズ

`close(fd)` は「fdを閉じる」関数です。

```c
if (close(fd) != 0) {
    perror("close");
}
```

ポイント：

- 閉じ忘れは資源リーク
- 書き込み時は `close` でエラーが出ることもある（遅延していた書き込みがここで失敗する等）


#### ファイルシーク：lseek()

`lseek` は「ファイル位置（オフセット）を動かす」関数です。

- `lseek(fd, off, SEEK_SET)`：先頭から off
- `lseek(fd, off, SEEK_CUR)`：現在位置から off
- `lseek(fd, off, SEEK_END)`：末尾から off

最小例：

```c
/* file: lseek_demo.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("out.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    off_t end = lseek(fd, 0, SEEK_END);
    if (end == (off_t)-1) {
        perror("lseek");
        close(fd);
        return 1;
    }

    printf("size=%lld\n", (long long)end);
    close(fd);
    return 0;
}
```

注意：

- `lseek` は「位置を動かす」だけで、読み書きはしない
- パイプやソケットなど “seekできない” fd では失敗する


#### ファイルポジション指定I/O

`read`/`write` は「fdに紐づくファイル位置」を進めながら読み書きします。
複数スレッドが同じfdを共有すると、位置が競合して読み書きが混ざることがあります。

これを避けたい場面で便利なのが `pread`/`pwrite` です。

- `pread(fd, buf, len, offset)`：指定offsetから読む（fdの現在位置は変えない）
- `pwrite(fd, buf, len, offset)`：指定offsetに書く（現在位置は変えない）

最小例：

```c
/* file: pread_demo.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("hello.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char buf[6] = { 0 };

    /* 先頭から5バイトだけ読む */
    ssize_t n = pread(fd, buf, 5, 0);
    if (n < 0) {
        perror("pread");
        close(fd);
        return 1;
    }

    printf("read: %s\n", buf);

    close(fd);
    return 0;
}
```

ポイント：

- `pread`/`pwrite` はファイル位置を共有しても混ざりにくい
- 低レイヤの実装でよく出る（ログ、DB、ファイルシステム系など）


#### ファイルトランケート

「ファイルの長さを変える」操作がトランケートです。

- `truncate(path, len)`：パス指定
- `ftruncate(fd, len)`：fd指定

使いどころ：

- 大きくなったログを切り詰める
- 事前にサイズを確保する／縮める

最小例：

```c
/* file: ftruncate_demo.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("out.txt", O_WRONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ftruncate(fd, 3) != 0) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
```

注意：

- 切り詰めると、末尾のデータは失われる
- 伸ばすと、増えた部分は読み出すと0に見えることが多い（穴あきファイル等の概念が絡む）


#### I/Oの多重化

「複数のfdを同時に扱いたい」場面があります。

- 複数のソケット（多数クライアント）
- stdin とソケットの両方を監視

こういうとき、1つの `read` にブロックしていると他を見られません。
そこで「どれが読み書き可能か」をまとめて待つ仕組みが I/O多重化です。

代表：

- `select`
- `poll`
- `epoll`（Linux固有で大規模向け）

ここでは最小の `poll` の形だけを見ます。

```c
/* file: poll_stdin.c */
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    puts("入力待ち（5秒）...");

    int r = poll(fds, 1, 5000);
    if (r < 0) {
        perror("poll");
        return 1;
    }
    if (r == 0) {
        puts("timeout");
        return 0;
    }

    if (fds[0].revents & POLLIN) {
        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            return 1;
        }
        buf[n] = '\0';
        printf("got: %s", buf);
    }

    return 0;
}
```

ポイント：

- `poll` は「イベントが来るまで待てる」
- タイムアウトを指定できる

注意：

- 大規模（fdが大量）になると `select`/`poll` は効率が落ちやすく、`epoll` が使われる


#### カーネル内の動作（雰囲気）

ここは“理解の軸”を作るための説明です。
細部は後の巻や、実際にUmuOS／Linuxの実装を読む段階で深掘りします。

典型的な流れ（雰囲気）：

- ユーザー空間の `read`/`write` 呼び出し
- システムコール境界を越えてカーネルへ
- VFS（仮想ファイルシステム）層で「どのファイルシステムでも同じ形」に見せる
- ページキャッシュを使って、ディスクI/Oをまとめたり再利用したりする
- 必要ならデバイスドライバ層へ（実際のストレージへ）
- 読み書きデータは、ユーザー空間とカーネル空間の間でコピーされる（概念として）

ポイント：

- `read`/`write` は「すぐディスクに触る」とは限らない
- キャッシュやバッファが間に挟まって性能と安全性を両立している

---

### 2. I/Oバッファリング

「同じファイルI/O」でも、`read`/`write` を直接使うのと、stdio（`fgets` 等）を使うのでは感覚が違います。
違いの主因は「ユーザー空間側のバッファリング」です。

#### ユーザー空間のI/Oバッファリング

stdioは多くの場合、

- まず大きめに読み込む
- その中から必要な分を返す

という形で、システムコールの回数を減らして高速化します。

一方で、

- どこまで読んだか
- 書いたつもりがまだ出ていない

のような「見えない状態」が出るので、概念として押さえておきます。


#### 標準I/O

標準I/O（stdio）は「ストリーム（`FILE *`）」を扱います。

- `fopen`/`fclose`：開閉
- `fread`/`fwrite`：バイト列の読み書き
- `fgets`/`fputs`：行（文字列）の読み書き
- `fprintf`/`fscanf`：書式付き


#### ストリームのオープン

```c
/* file: fopen_min.c */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("hello.txt", "r");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    fclose(fp);
    return 0;
}
```

ポイント：

- `FILE *` は “ストリーム” を指す
- 失敗時は `NULL`


#### ファイルディスクリプタを介したストリームオープン

すでにfdを持っている場合、`fdopen` で `FILE *` に変換できます。

```c
/* file: fdopen_demo.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("hello.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    FILE *fp = fdopen(fd, "r");
    if (fp == NULL) {
        perror("fdopen");
        close(fd);
        return 1;
    }

    /* fclose は fd も閉じる（ここが重要） */
    fclose(fp);
    return 0;
}
```

注意：

- `fdopen` したら、基本は `fclose(fp)` で閉じる（`close(fd)` と二重に閉じない）


#### ストリームのクローズ

`fclose` はバッファを吐き出してから閉じます。

- `fclose` が失敗することもある（書き込み失敗がここで見える等）


#### ストリームの読み込み

行単位の読み込み（テキストファイル）では `fgets` が扱いやすいです。

```c
/* file: fgets_demo.c */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("hello.txt", "r");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        fputs(line, stdout);
    }

    fclose(fp);
    return 0;
}
```

ポイント：

- `fgets` はバッファ長を渡せるので、読み過ぎを防ぎやすい


#### ストリームの書き込み

```c
/* file: fputs_demo.c */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("out.txt", "w");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    fputs("hello\n", fp);
    fputs("world\n", fp);

    if (fclose(fp) != 0) {
        perror("fclose");
        return 1;
    }

    return 0;
}
```


#### ストリームのシーク

- `fseek(fp, off, whence)`：位置移動
- `ftell(fp)`：現在位置
- `rewind(fp)`：先頭へ


#### ストリームのフラッシュ

- `fflush(fp)`：書き込みバッファを吐き出す

注意：

- `stdout` は「端末向け」だと行バッファ、「ファイル向け」だと全バッファ、など挙動が違うことがある


#### エラーとEOF

stdioには「エラー」と「EOF」を区別するための関数があります。

- `feof(fp)`：EOFに達したか
- `ferror(fp)`：エラーが起きたか
- `clearerr(fp)`：状態クリア

`fgets` ループで抜けたときに、エラーかEOFかを確認できます。


#### ストリームとファイルディスクリプタ

- `fileno(fp)` で `FILE*` から fd を取り出せます。
- 逆に `fdopen` で fd から `FILE*` を作れます。

注意：

- stdioがバッファを持つため、同じファイルを fd と `FILE*` の両方から同時に触ると混乱しやすい


#### バッファリングの制御

`setvbuf` でバッファリング方式を指定できます。

- `_IONBF`：無バッファ
- `_IOLBF`：行バッファ
- `_IOFBF`：全バッファ

最小の概念：

- ログをすぐ出したいならフラッシュ（または行バッファ）を意識する

---

### 3. 高度なファイルI/O

ここでは「よく名前は出るが、最初は使いどころが分かりにくい」ものを、位置づけ中心で整理します。

#### scatter-gather I/O

`readv`/`writev` は「複数のバッファ」を一回の呼び出しで読み書きする仕組みです。

- 例：ヘッダと本文を別バッファで持っている
- 例：小さなバッファをたくさん連結せずに送る

最小例（writev）：

```c
/* file: writev_demo.c */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
    const char *a = "hello ";
    const char *b = "writev\n";

    struct iovec iov[2];
    iov[0].iov_base = (void *)a;
    iov[0].iov_len = strlen(a);
    iov[1].iov_base = (void *)b;
    iov[1].iov_len = strlen(b);

    ssize_t n = writev(STDOUT_FILENO, iov, 2);
    if (n < 0) {
        perror("writev");
        return 1;
    }

    return 0;
}
```


#### Event Pollインターフェイス

I/O多重化で `epoll` が出てきました。
`epoll` は「監視するfdが多い」場面で効率が出やすいインターフェイスです。

最小のイメージ：

- `epoll_create1`：監視集合を作る
- `epoll_ctl`：fdを登録／削除
- `epoll_wait`：イベントが来るまで待つ

この第1冊では概念までに留め、
次の冊以降で「ソケット＋epoll」を具体的に扱う方が自然です。


#### ファイルをメモリにマッピングする

`mmap` は「ファイルの内容をメモリとして見せる」仕組みです。

狙い：

- 読み込みを `read` ループではなく、メモリ参照で行える
- OSがページ単位で必要なところだけ読み込む（ページフォールト）

最小の“雰囲気”例（読み取り専用）：

```c
/* file: mmap_read.c */
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    int fd = open("hello.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return 1;
    }

    if (st.st_size == 0) {
        puts("empty");
        close(fd);
        return 0;
    }

    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    /* ここでは最小として先頭だけ表示 */
    write(STDOUT_FILENO, p, (size_t)st.st_size);

    munmap(p, (size_t)st.st_size);
    close(fd);
    return 0;
}
```

注意：

- `mmap` は便利だが、ファイルサイズ変更やエラー処理など設計上の注意点も多い


#### ファイルI/Oのヒント/アドバイス

- 「テキストとして扱う」なら `fgets`（行単位）が安全に組みやすい
- 「バイト列として確実に扱う」なら `read`/`write`（必要なら `write_all`）
- エラー処理を省かない（戻り値、`errno`）
- “サイズ” を意識する（`size_t`／`ssize_t`／`off_t`）


#### 同期/非同期

ここでの“非同期”はまず「ブロックしない」意味として押さえます。

- ブロッキング：`read` がデータが来るまで止まる
- ノンブロッキング：データがなければすぐ戻る（`EAGAIN` など）

ノンブロッキングは、ソケットなどで `O_NONBLOCK` と組み合わせて使われます。

本格的な非同期I/O（POSIX AIO、Linuxの `io_uring` など）は別の冊で扱うのが自然です。


#### I/OスケジューラとI/Oパフォーマンス

性能の話は“観測”が大事なので、この第1冊では考え方の軸だけ書きます。

- システムコール回数が多いと遅くなりやすい（バッファリングの価値）
- 同期を強めると遅くなりやすい（安全性とのトレードオフ）
- 連続アクセスとランダムアクセスで速度が大きく変わることがある
- 何がボトルネックか（CPU、ディスク、待ち）を意識する

---

## 第一章まとめ

- システムコールI/Oは fd（小さな整数）で扱い、基本は `open`/`read`/`write`/`close`
- 戻り値が最重要：`read` は 0 がEOF、-1が失敗
- エラーは `errno` に入るので `perror` 等で確認する
- `lseek` は位置を動かすだけ、`pread`/`pwrite` は位置指定で安全に扱えることが多い
- `truncate`/`ftruncate` はファイル長を変える（切り詰め・拡張）
- 同期I/O（`fsync` 等）は耐障害性のためだが遅くなりやすい
- ダイレクトI/O（`O_DIRECT`）は制約が強く、まず概念として理解する
- 多重化（`poll`/`epoll`）は多数fdやイベント駆動で重要
- stdioはユーザー空間バッファでシステムコール回数を減らすが、状態が見えにくくなる

---

## I/Oバッファリングまとめ

- stdioは“ユーザー空間のバッファ”で性能と使いやすさを出す
- `fdopen`/`fileno` で fd と `FILE*` を行き来できる
- `fflush` と `setvbuf` でバッファの吐き出しや方式を制御できる
- エラーとEOFは `ferror` と `feof` で区別できる

---

## 高度なファイルI/Oまとめ

- `readv`/`writev` は複数バッファをまとめて扱う（scatter-gather）
- `epoll` は多数fdの監視で重要（この冊は概念まで）
- `mmap` はファイルをメモリとして扱うが設計上の注意点も多い
- 同期は安全性、バッファリングは性能、用途ごとに選ぶ

---

## 全体まとめ

この第1冊では、LinuxのファイルI/Oの土台として

- システムコール（fd）
- stdio（ストリーム）

の2系統を、つながりが分かる形で整理しました。

次の冊（system_call_lib2 以降）では、

- ソケットI/O（ネットワーク）
- epollを使ったイベント駆動
- ノンブロッキング

など、ush/uim の読解に直結する方向へ進めるのが自然です。
