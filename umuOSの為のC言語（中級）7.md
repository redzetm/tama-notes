---
title: "UmuOSの為のC言語７（中級）　7章　ファイル、ディレクトリの管理"
---

# UmuOSの為のC言語（中級）　７

このノートは、UmuOSを進化させるためにプロセス管理・メモリ管理・ファイルI/O・シェル・システムコール設計に
直接役立つ形で抽象化し、再構成することであります。
すなわち、UmuOSの構造へ還元するための実践ノートであります。
LinuxのAPIやシステムコールは、OSがどのようにふるまうかでもあり、その構造を理解することはUmuOSの設計力を
高めることに直結すると思います。

## ７章　ファイル、ディレクトリの管理

この章は、参考文献をもとに、自分用の研究ノートとして再構成していきます。
古い説明は、現在のLinuxやC17相当の書き方に寄せながら、UmuOSの設計へつながる形で整理します。

これまで 2章のファイルI/O、3章のI/Oバッファリング、4章の高度なファイルI/Oで、主に「どう読むか」「どう書くか」を扱ってきました。
この章では、読み書きそのものより一歩進んで、ファイルやディレクトリをどう管理し、その属性をどう見るか、どう変えるか、という視点で整理します。

### ７章の１　ファイルとメタデータ

Linuxでファイルへアクセスするとき、実際にはファイル名そのものではなく、その先にある inode と各種管理情報をたどって処理が進みます。
inode は、Unix系ファイルシステムでファイルの実体を表す中心的な概念です。

inode には、たとえば次のような情報が入っています。

```text
アクセス権
所有者と所属グループ
ファイルサイズ
タイムスタンプ
リンク数
ファイルシステム内での管理情報
```

ファイル名は、あくまで「ディレクトリから inode を引くための名前」です。
そのため、同じ inode を複数の名前で参照するハードリンクという仕組みも存在します。

inode 番号は `ls -i` で確認できます。

```text
$ ls -i
1689459 Kconfig
1689461 main.c
1680137 Makefile
1680138 console.c
```

この番号は、同じファイルシステム内では inode を識別するための番号です。
ただし、別ファイルシステムなら同じ番号が現れることはあります。
また、ハードリンクされた複数の名前は同じ inode 番号を共有します。

#### ７章の１の１　stat()システムコール群

ファイルのメタデータを取得する基本APIが `stat()` 系です。

```c
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int fstatat(int dirfd, const char *path, struct stat *buf, int flags);
```

役割は次のように分けて考えると分かりやすいです。

```text
stat():
	パスから対象をたどって情報を取る

fstat():
	すでに open 済みのファイルディスクリプタから情報を取る

lstat():
	シンボリックリンクをたどらず、リンク自身の情報を取る

fstatat():
	dirfd 基準やフラグ付きで柔軟に情報を取る
```

現代のLinuxでは、`openat()` 系と同様に `fstatat()` を軸に考えると、相対パスの基準を明確にできるので便利です。
ただし、研究の最初の段階では `stat()`、`fstat()`、`lstat()` の違いを押さえれば十分です。

取得した情報は `struct stat` に入ります。

```c
struct stat {
	dev_t     st_dev;
	ino_t     st_ino;
	mode_t    st_mode;
	nlink_t   st_nlink;
	uid_t     st_uid;
	gid_t     st_gid;
	dev_t     st_rdev;
	off_t     st_size;
	blksize_t st_blksize;
	blkcnt_t  st_blocks;
	time_t    st_atime;
	time_t    st_mtime;
	time_t    st_ctime;
};
```

実際の定義は環境により拡張されることがあります。
たとえば現在の glibc では `st_atim`、`st_mtim`、`st_ctim` のようなナノ秒精度のメンバも見えます。
ここでは、まず主要メンバの意味を押さえます。

```text
st_dev:
	そのファイルが属するファイルシステムのデバイスID
	「物理ディスクかどうか」を直接表す値ではない

st_ino:
	inode番号

st_mode:
	ファイル種別とパーミションを表すビット列

st_nlink:
	ハードリンク数
	通常は 1 以上だが、unlink 済みでまだ open 中のファイルでは 0 になり得る

st_uid:
	所有ユーザID

st_gid:
	所有グループID

st_rdev:
	キャラクタデバイスやブロックデバイス自身を表す special file のデバイス番号

st_size:
	ファイルサイズ

st_blksize:
	I/O の推奨ブロックサイズの目安
	必ずしも「これが最速」と断言できる値ではないが、バッファ設計の参考になる

st_blocks:
	実際に割り当てられたブロック数
	穴あきファイルでは st_size より小さく見えることがある

st_atime:
	最終アクセス時刻
	ただし mount オプションの relatime や noatime の影響を受ける

st_mtime:
	内容の最終更新時刻

st_ctime:
	メタデータ変更時刻
	作成時刻ではない
```

`st_ctime` は特に誤解されやすいです。
これは creation time ではなく change time です。
つまり、所有者変更、パーミション変更、リンク数変化などでも更新されます。

現在のLinuxでは、ファイル作成時刻に近い情報が必要な場合、より新しい `statx()` で birth time を扱えることがあります。
ただし、すべてのファイルシステムが常に提供するとは限りません。

`stat()` 系は成功時に 0、失敗時に -1 を返し、`errno` を設定します。
代表的なエラーは次の通りです。

```text
EACCES:
	パス探索権限がない

EBADF:
	fstat() の fd が無効

EFAULT:
	ポインタが無効

ELOOP:
	シンボリックリンクの解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	対象パスが存在しない

ENOMEM:
	必要メモリ不足

ENOTDIR:
	パス途中にディレクトリでない要素がある
```

次のサンプルコードは、コマンドライン引数で与えたファイルのサイズを表示します。

```c
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct stat stat_buf;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (stat(argv[1], &stat_buf) == -1) {
		perror("stat");
		return EXIT_FAILURE;
	}

	printf("%s is %" PRIuMAX " bytes\n",
	       argv[1],
	       (uintmax_t)stat_buf.st_size);

	return EXIT_SUCCESS;
}
```

古いサンプルでは `off_t` を `%ld` で表示していることがありますが、環境差を避けるならこのように `uintmax_t` などへ寄せて出す方が無難です。

もうひとつ、`st_dev` の見方を確認する短い例を載せます。
古い資料では、ここから「物理デバイス上かどうか」を判定する説明が出ることがありますが、現在はそう単純には考えない方がよいです。
`st_dev` は、主に「どのファイルシステム上にあるか」を識別するための値として見る方が自然です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct stat stat_buf;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (stat(argv[1], &stat_buf) == -1) {
		perror("stat");
		return EXIT_FAILURE;
	}

	printf("filesystem device: major=%u minor=%u\n",
	       major(stat_buf.st_dev),
	       minor(stat_buf.st_dev));

	return EXIT_SUCCESS;
}
```

この値を見ると、2つのファイルが同じファイルシステム上にあるかどうかを比較しやすくなります。

#### ７章の１の２　パーミション

`stat()` 系でもパーミションは見られますが、変更には別のAPIを使います。

```c
#include <sys/stat.h>
#include <sys/types.h>

int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
```

`chmod()` はパスで対象を指定し、`fchmod()` は open 済みのファイルディスクリプタに対して変更します。
`fchmodat()` は `openat()` 系と同様の発想で、基準ディレクトリ付きで扱える版です。

`mode` は単なる整数ではありますが、直接数値を書くより、定数マクロを組み合わせて表す方が明確です。

```text
S_IRUSR:
	所有者に読み取り許可

S_IWUSR:
	所有者に書き込み許可

S_IXUSR:
	所有者に実行許可
```

たとえば `S_IRUSR | S_IWUSR` は、所有者だけが読み書きできるモードを表します。
数値で言えば `0600` に相当します。

パーミション変更には、通常は次のいずれかが必要です。

```text
対象ファイルの所有者である

十分な権限を持つ
```

Linuxでは、細かく言えば `CAP_FOWNER` などの権限概念も関係します。
ただし研究の第一段階では、「自分のファイルなら変更できるが、他人のファイルは通常変更できない」と押さえると分かりやすいです。

成功時は 0、失敗時は -1 を返します。
代表的な `errno` は次の通りです。

```text
EACCES:
	パス探索権限がない

EBADF:
	fchmod() の fd が無効

EFAULT:
	ポインタが無効

EIO:
	ファイルシステム側のI/Oエラー

ELOOP:
	シンボリックリンク解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	対象が存在しない

ENOMEM:
	必要メモリ不足

ENOTDIR:
	パス途中にディレクトリでない要素がある

EPERM:
	変更権限がない

EROFS:
	読み取り専用ファイルシステム上で変更しようとした
```

まずは、パスを直接指定する `chmod()` の例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

int main(void)
{
	if (chmod("./map.png", S_IRUSR | S_IWUSR) == -1) {
		perror("chmod");
		return EXIT_FAILURE;
	}

	puts("updated permissions of ./map.png");
	return EXIT_SUCCESS;
}
```

これはシェルで言えば、だいたい次と同じ意味です。

```text
chmod 600 ./map.png
```

次は、すでに開いているファイルディスクリプタに対して `fchmod()` を使う例です。

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	int fd = open("./map.png", O_RDONLY);
	if (fd == -1) {
		perror("open");
		return EXIT_FAILURE;
	}

	if (fchmod(fd, S_IRUSR | S_IWUSR) == -1) {
		perror("fchmod");
		close(fd);
		return EXIT_FAILURE;
	}

	if (close(fd) == -1) {
		perror("close");
		return EXIT_FAILURE;
	}

	puts("updated permissions via file descriptor");
	return EXIT_SUCCESS;
}
```

`fchmod()` の利点は、すでに open 済みの対象をそのまま扱えることです。
パスをもう一度たどらないので、対象の取り違えを減らしたい場面で有利です。

現代のLinuxでは、`openat()` や `fstatat()` と同様に、ディレクトリ基準を明示して race を減らす設計が重要になることがあります。
その流れの中で `fchmodat()` も理解すると、より実践的です。
