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

#### ７章の１の３　ファイルオーナ

ファイルの所有者と所有グループは、`struct stat` の `st_uid` と `st_gid` で確認できます。
これらを変更する API として、Linux では次のものを使います。

```c
#include <sys/types.h>
#include <unistd.h>

int chown(const char *path, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags);
```

基本的な違いは、対象をどう指定し、シンボリックリンクをどう扱うかです。

```text
chown():
	パスで指定し、通常はシンボリックリンクをたどる

lchown():
	パスで指定し、シンボリックリンク自身を対象にする

fchown():
	ファイルディスクリプタで指定する

fchownat():
	dirfd 基準やフラグ付きで扱える拡張版
```

`owner` または `group` に「変更しない」ことを表す特別値を渡すと、その項目だけ据え置けます。
古い本では `-1` をそのまま書いていますが、教材としては「未変更を意味する特別値」と理解しておく方が安全です。

権限の考え方は、現在のLinuxでも概ね次の通りです。

```text
所有者の変更:
	通常は強い権限が必要
	多くの場合 root 相当、または CAP_CHOWN が必要

グループの変更:
	ファイル所有者が、自分の所属グループへ変更できる場合がある
	それ以外は通常は強い権限が必要
```

処理が成功すると 0、失敗すると -1 を返して `errno` を設定します。
代表的なエラーは次の通りです。

```text
EACCES:
	パス探索権限がない

EBADF:
	fchown() の fd が無効

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
	指定した所有者やグループへ変更する権限がない

EROFS:
	読み取り専用ファイルシステムで変更しようとした
```

次の例は、`manifest.txt` の所有グループを `officers` に変更します。

```c
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	struct group *group_entry = getgrnam("officers");
	if (group_entry == NULL) {
		if (errno != 0) {
			perror("getgrnam");
		} else {
			fprintf(stderr, "group not found: officers\n");
		}
		return EXIT_FAILURE;
	}

	if (chown("./manifest.txt", (uid_t)-1, group_entry->gr_gid) == -1) {
		perror("chown");
		return EXIT_FAILURE;
	}

	puts("updated group of ./manifest.txt");
	return EXIT_SUCCESS;
}
```

ここでは所有者は変えず、グループだけを変更しています。
そのため、第2引数には「所有者を変更しない」値を渡しています。

次の例は、開いているファイルディスクリプタに対して `fchown()` を使う形です。

```c
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int make_root_owner(int fd)
{
	if (fchown(fd, 0, 0) == -1) {
		perror("fchown");
		return -1;
	}

	return 0;
}
```

ただし、これは通常ユーザが気軽に使える処理ではありません。
現実には root 権限や `CAP_CHOWN` が必要になる場面がほとんどです。

#### ７章の１の４　拡張属性

ファイルには、通常のメタデータとは別に拡張属性、つまり xattr を付けられることがあります。
これは「キーと値」の組で追加情報を持たせる仕組みです。

```text
通常のメタデータ:
	サイズ、所有者、パーミション、時刻など

拡張属性:
	アプリケーションやセキュリティ機構が独自に持たせる追加情報
```

たとえば、ラベル、補助的な識別情報、セキュリティ関連情報などをファイルに結び付けられます。
SELinux のラベルや ACL 関連情報も、この仕組みに関連して扱われます。

古い本では ext3 を前提に内部実装が説明されることがありますが、学習の主眼はそこではありません。
重要なのは、アプリケーション側からは「ファイルシステムごとの差をある程度隠した API で扱える」ことです。

ただし、注意点もあります。

```text
xattr のAPIは Linux や各Unix系で使われているが、POSIX本体の標準APIではない

ファイルシステムによって対応状況や上限が異なる

すべてのファイル種別・名前空間で自由に使えるわけではない
```

つまり、使い方の抽象化はされていても、完全にどこでも同じとは限りません。

##### ７章の１の４の１　キーと値

拡張属性は、完全修飾名のキーと、それに対応する値で表されます。

```text
例:
	user.mime_type
	user.checksum
	security.selinux
```

キーは一般に `namespace.name` という形を取ります。
値は単なる文字列に限らず、任意のバイト列です。
そのため、NULL 終端文字列である保証はなく、読み書きでは常にサイズを意識する必要があります。

ここで重要なのは、次の2つが別物だという点です。

```text
キーが存在しない

キーは存在するが、値が空である
```

後者は「長さ 0 の値を持つ定義済み属性」です。
削除とは意味が異なります。

Linux では、キー数や値サイズの理論上の説明よりも、実際にはファイルシステムごとの制約が効きます。
ext4、XFS、btrfs などで挙動や上限感は異なるため、大量データの保存場所として安易に使うものではありません。
通常は短いメタ情報を付ける用途に向いています。

##### ７章の１の４の２　拡張属性の名前空間

Linux では、拡張属性に名前空間という区切りがあります。
これは単なる名前の分類ではなく、アクセス制御にも関わります。

```text
system:
	ACL など、カーネルや周辺機能の実装で使われることがある

security:
	SELinux などのセキュリティ関連で使われる

trusted:
	強い権限を持つプロセス向け

user:
	一般アプリケーションが通常使う名前空間
```

ユーザ空間のアプリケーションが独自情報を付けるなら、まず `user.*` を使うと考えてよいです。
ただし、シンボリックリンクなどでは `user` 名前空間を期待どおり使えないことがあります。

##### ７章の１の４の３　拡張属性の操作

拡張属性で行う基本操作は次の4つです。

```text
値を読む

値を設定する

キー一覧を得る

キーを削除する
```

Linux では、それぞれに「パスをたどる版」「シンボリックリンク自身を扱う l 版」「ファイルディスクリプタで扱う f 版」があります。

拡張属性 API のヘッダは、現在のLinuxでは次の形で使うのが一般的です。

```c
#include <sys/xattr.h>
```

古い資料やディストリビューションでは `attr/xattr.h` が出てくることがありますが、今はまず `sys/xattr.h` を押さえる方が実践的です。

###### ７章の１の４の３の１　拡張属性の参照

値の読み取りには次を使います。

```c
#include <sys/types.h>
#include <sys/xattr.h>

ssize_t getxattr(const char *path, const char *key, void *value, size_t size);
ssize_t lgetxattr(const char *path, const char *key, void *value, size_t size);
ssize_t fgetxattr(int fd, const char *key, void *value, size_t size);
```

成功時の戻り値は、実際の値のサイズです。
`size` に 0 を渡せば、値そのものは受け取らず、必要サイズだけを調べられます。
この2段階取得は xattr では基本パターンです。

代表的なエラーは次の通りです。

```text
EACCES, EBADF, EFAULT, ELOOP, ENAMETOOLONG, ENOENT, ENOMEM, ENOTDIR:
	通常のパス解決やメモリエラー

ENODATA:
	指定した属性が存在しない
	古い資料では ENOATTR と書かれることがある

ENOTSUP または EOPNOTSUPP:
	ファイルシステムが拡張属性に対応していない

ERANGE:
	バッファが小さすぎる
```

`ENOATTR` は環境によっては別名として見えることがありますが、Linux では `ENODATA` と説明されることが多いです。

次の例は `user.mime_type` を読む最小例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/xattr.h>

int main(int argc, char *argv[])
{
	ssize_t size;
	char *value;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	size = getxattr(argv[1], "user.mime_type", NULL, 0);
	if (size == -1) {
		perror("getxattr(size)");
		return EXIT_FAILURE;
	}

	value = malloc((size_t)size + 1U);
	if (value == NULL) {
		perror("malloc");
		return EXIT_FAILURE;
	}

	if (getxattr(argv[1], "user.mime_type", value, (size_t)size) == -1) {
		perror("getxattr(value)");
		free(value);
		return EXIT_FAILURE;
	}

	value[size] = '\0';
	printf("user.mime_type=%s\n", value);
	free(value);
	return EXIT_SUCCESS;
}
```

なお、値は本来バイナリかもしれません。
ここでは「文字列で入っている」と分かっている属性を読む例として終端文字を足しています。

###### ７章の１の４の３の２　拡張属性の設定

値の設定には次を使います。

```c
#include <sys/types.h>
#include <sys/xattr.h>

int setxattr(const char *path, const char *key,
	     const void *value, size_t size, int flags);
int lsetxattr(const char *path, const char *key,
	      const void *value, size_t size, int flags);
int fsetxattr(int fd, const char *key,
	      const void *value, size_t size, int flags);
```

`flags` によって、作成のみ許すか、既存値の置換だけ許すかを制御できます。

```text
0:
	作成も更新も許す

XATTR_CREATE:
	新規作成のみ許す

XATTR_REPLACE:
	既存値の置換のみ許す
```

代表的なエラーは、読み取り側に加えて次のものがあります。

```text
EEXIST:
	XATTR_CREATE を指定したが既に存在する

EINVAL:
	flags が不正

ENOSPC:
	保存領域不足

EDQUOT:
	クォータ制限に達した
```

次の例は `user.mime_type` を設定します。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/xattr.h>

int main(int argc, char *argv[])
{
	const char *mime_type = "text/plain";

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (setxattr(argv[1],
		     "user.mime_type",
		     mime_type,
		     strlen(mime_type),
		     0) == -1) {
		perror("setxattr");
		return EXIT_FAILURE;
	}

	puts("updated user.mime_type");
	return EXIT_SUCCESS;
}
```

###### ７章の１の４の３の３　拡張属性の一覧

キー一覧を得るには次を使います。

```c
#include <sys/types.h>
#include <sys/xattr.h>

ssize_t listxattr(const char *path, char *list, size_t size);
ssize_t llistxattr(const char *path, char *list, size_t size);
ssize_t flistxattr(int fd, char *list, size_t size);
```

返ってくるデータは、NULL 終端文字列が連続した塊です。

```text
user.mime_type\0user.checksum\0security.selinux\0
```

そのため、1個ずつは通常のC文字列として読めますが、全体の終端は戻り値の総サイズで判断する必要があります。

次の例は一覧を表示します。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/xattr.h>

int main(int argc, char *argv[])
{
	char *list;
	char *cursor;
	ssize_t size;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	size = listxattr(argv[1], NULL, 0);
	if (size == -1) {
		perror("listxattr(size)");
		return EXIT_FAILURE;
	}

	list = malloc((size_t)size);
	if (list == NULL) {
		perror("malloc");
		return EXIT_FAILURE;
	}

	if (listxattr(argv[1], list, (size_t)size) == -1) {
		perror("listxattr(value)");
		free(list);
		return EXIT_FAILURE;
	}

	for (cursor = list; cursor < list + size; cursor += strlen(cursor) + 1U) {
		puts(cursor);
	}

	free(list);
	return EXIT_SUCCESS;
}
```

###### ７章の１の４の３の４　拡張属性の削除

最後に、キーを削除する API です。

```c
#include <sys/types.h>
#include <sys/xattr.h>

int removexattr(const char *path, const char *key);
int lremovexattr(const char *path, const char *key);
int fremovexattr(int fd, const char *key);
```

これは「空文字列を設定する」のとは違い、属性そのものを未定義に戻します。

代表的なエラーは読み取りや設定とほぼ同様で、属性が存在しないときは `ENODATA` が返ることがあります。

短い例を載せます。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/xattr.h>

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (removexattr(argv[1], "user.mime_type") == -1) {
		perror("removexattr");
		return EXIT_FAILURE;
	}

	puts("removed user.mime_type");
	return EXIT_SUCCESS;
}
```

拡張属性は便利ですが、ファイルシステム移動やアーカイブ方法によっては失われることがあります。
そのため、重要な永続データを xattr だけに頼る設計は慎重に考えるべきです。

### ７章の２　ディレクトリ

Unix系OSでディレクトリとは、概念的には「名前と inode を対応付ける表」です。
ファイルそのものの中身を持つというより、どの名前がどの実体を指しているかを管理する役割を持ちます。

```text
ディレクトリが持つもの:
	ファイル名
	対応する inode への参照
```

そのため、ディレクトリの内容とは、要するにその下にある名前の一覧です。
`ls` の出力は、まさにこの一覧を人間向けに整形して見せているものと考えられます。

プロセスがファイルを開くとき、カーネルはパスをたどりながら、各ディレクトリ内の名前を引いて次の inode を見つけます。
最終的に目的の inode に到達して、そこでファイルシステム実装が実データへ結び付けます。

ディレクトリの中には、通常ファイルだけでなく、別のディレクトリも含まれます。
この関係が階層構造、つまりディレクトリツリーを作ります。

```text
親ディレクトリ:
	あるディレクトリを含んでいる側

子ディレクトリ:
	その中に含まれている側
```

ツリーの最上位にある `/` をルートディレクトリと呼びます。
これは root ユーザのホームディレクトリである `/root` とは別物です。

パス名は、このディレクトリ階層をたどるための表現です。

```text
絶対パス:
	/ から始まる
	例: /usr/bin/ls

相対パス:
	現在位置を基準に解決する
	例: bin/ls
```

相対パスを解決する基準が、カレントディレクトリです。

ファイル名やディレクトリ名に使えない文字として、Unixでは基本的に `/` と NUL があります。
`/` は区切り文字、NUL はC文字列終端として使われるためです。
それ以外のバイト列は理論上かなり自由ですが、実務では可搬性や表示崩れを避けるため、扱いやすい文字集合に寄せることが多いです。

また、すべてのディレクトリには `.` と `..` があります。

```text
. :
	そのディレクトリ自身

.. :
	親ディレクトリ
```

たとえば `/home/tama/work/..` は `/home/tama` を指します。
ルートディレクトリでは `.` も `..` も自分自身を指します。

#### ７章の２の１　カレントディレクトリ

すべてのプロセスは、カレントワーキングディレクトリ、つまり cwd を持ちます。
相対パスはこの cwd を基準に解決されます。

たとえば cwd が `/home/blackbeard` のときに `parrot.jpg` を開けば、実際には `/home/blackbeard/parrot.jpg` が対象になります。
一方、`/usr/bin/mast` のように `/` で始まる絶対パスなら、cwd には影響されません。

cwd は親プロセスから引き継がれます。
そのため、シェルから起動したプログラムは、通常、シェルのいるディレクトリを引き継いで開始します。

##### ７章の２の１の１　カレントディレクトリの参照

cwd を調べる代表的な API は `getcwd()` です。

```c
#include <unistd.h>

char *getcwd(char *buf, size_t size);
```

成功すると、cwd の絶対パスを `buf` に書いて、そのポインタを返します。
失敗時は `NULL` を返し、`errno` を設定します。

代表的なエラーは次の通りです。

```text
EFAULT:
	buf が無効

EINVAL:
	size が不正

ENOENT:
	現在のディレクトリが既に削除されているなどで無効になっている

ERANGE:
	buf が小さすぎる
```

固定長バッファを自前で用意して使う形は、もっとも移植性の高い書き方です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	char buffer[4096];

	if (getcwd(buffer, sizeof(buffer)) == NULL) {
		perror("getcwd");
		return EXIT_FAILURE;
	}

	printf("cwd = %s\n", buffer);
	return EXIT_SUCCESS;
}
```

Linux の glibc では、`getcwd(NULL, 0)` によって必要サイズのバッファを内部確保して返す使い方もできます。
これは便利ですが、厳密には POSIX の必須動作だけに頼る書き方ではありません。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	char *cwd = getcwd(NULL, 0);
	if (cwd == NULL) {
		perror("getcwd");
		return EXIT_FAILURE;
	}

	printf("cwd = %s\n", cwd);
	free(cwd);
	return EXIT_SUCCESS;
}
```

さらに glibc には GNU 拡張として `get_current_dir_name()` もあります。

```c
#define _GNU_SOURCE
#include <unistd.h>

char *get_current_dir_name(void);
```

これは便利ですが GNU 拡張です。
教材としては、まず `getcwd()` を理解しておく方が基本になります。

古い資料では `getwd()` が出てくることがありますが、これは現在では使うべきではありません。
固定長前提で危険があり、非推奨と考えてよいです。

##### ７章の２の１の２　カレントディレクトリの移動

カレントディレクトリを変更するには、次の API を使います。

```c
#include <unistd.h>

int chdir(const char *path);
int fchdir(int fd);
```

`chdir()` はパスで、`fchdir()` はディレクトリを指すファイルディスクリプタで cwd を変更します。

成功時は 0、失敗時は -1 を返します。

`chdir()` の代表的なエラーは次の通りです。

```text
EACCES:
	検索権限がない

EFAULT:
	ポインタが無効

EIO:
	内部I/Oエラー

ELOOP:
	シンボリックリンク解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	対象ディレクトリが存在しない

ENOMEM:
	必要メモリ不足

ENOTDIR:
	途中または末尾がディレクトリではない
```

`fchdir()` では主に次を意識します。

```text
EACCES:
	そのディレクトリへ移動する権限がない

EBADF:
	fd が無効、またはディレクトリではない
```

重要なのは、cwd の変更はそのプロセス自身にしか効かないという点です。
そのため、シェルの `cd` は外部コマンドではなく、シェル自身が `chdir()` する内部コマンドとして実装されます。

まずは `chdir()` の単純な例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	if (chdir("/tmp") == -1) {
		perror("chdir");
		return EXIT_FAILURE;
	}

	puts("changed cwd to /tmp");
	return EXIT_SUCCESS;
}
```

元のディレクトリへ戻したいなら、パス文字列を保存するより、ディレクトリを open して `fchdir()` で戻す方が堅実です。

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	int saved_cwd_fd = open(".", O_RDONLY | O_DIRECTORY);
	if (saved_cwd_fd == -1) {
		perror("open");
		return EXIT_FAILURE;
	}

	if (chdir("/tmp") == -1) {
		perror("chdir");
		close(saved_cwd_fd);
		return EXIT_FAILURE;
	}

	/* /tmp で何か処理する */

	if (fchdir(saved_cwd_fd) == -1) {
		perror("fchdir");
		close(saved_cwd_fd);
		return EXIT_FAILURE;
	}

	if (close(saved_cwd_fd) == -1) {
		perror("close");
		return EXIT_FAILURE;
	}

	puts("restored original cwd");
	return EXIT_SUCCESS;
}
```

この方法は、途中でディレクトリ名が変わった場合にも比較的強い、という利点があります。

デーモンでは `chdir("/")` しておく設計がよく使われます。
一方、対話的なツールでは、ユーザのホームディレクトリや作業ディレクトリを基準にすることが多いです。

#### ７章の２の２　ディレクトリの作成

ディレクトリの新規作成には `mkdir()` を使います。

```c
#include <sys/stat.h>
#include <sys/types.h>

int mkdir(const char *path, mode_t mode);
int mkdirat(int dirfd, const char *pathname, mode_t mode);
```

成功時は 0、失敗時は -1 を返します。
`mode` はそのまま適用されるのではなく、現在の `umask` で制限されたうえで使われます。

つまり、たとえば `0777` を指定しても、実際の結果は `umask` によって削られます。

代表的なエラーは次の通りです。

```text
EACCES:
	親ディレクトリへの書き込み権限または探索権限がない

EEXIST:
	同名のものが既に存在する

EFAULT:
	ポインタが無効

ELOOP:
	シンボリックリンク解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	親ディレクトリが存在しない

ENOMEM:
	必要メモリ不足

ENOSPC:
	空き容量不足

ENOTDIR:
	途中要素にディレクトリでないものがある

EPERM:
	ファイルシステムやポリシー上、作成できない

EROFS:
	読み取り専用ファイルシステムである
```

単純な例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

int main(void)
{
	if (mkdir("./logs", 0755) == -1) {
		perror("mkdir");
		return EXIT_FAILURE;
	}

	puts("created ./logs");
	return EXIT_SUCCESS;
}
```

現代のLinuxでは、相対パスの基準を明示したい場面で `mkdirat()` が便利です。
`openat()` 系と同じ流れで理解するとよいです。

#### ７章の２の３　ディレクトリの削除

空のディレクトリを削除するには `rmdir()` を使います。

```c
#include <unistd.h>

int rmdir(const char *path);
```

成功時は 0、失敗時は -1 を返します。
削除対象は空でなければなりません。

`rm -r` のような再帰削除に対応する単一システムコールはありません。
再帰削除は、アプリケーション側が下位要素を順にたどって消していく処理です。

代表的なエラーは次の通りです。

```text
EACCES:
	親ディレクトリへの書き込み権限または探索権限がない

EBUSY:
	マウントポイントなどで使用中

EFAULT:
	ポインタが無効

EINVAL:
	不正なパス

ELOOP:
	シンボリックリンク解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	対象が存在しない

ENOMEM:
	必要メモリ不足

ENOTDIR:
	ディレクトリではない

ENOTEMPTY:
	空ではない

EPERM:
	sticky bit や権限の都合で削除できない

EROFS:
	読み取り専用ファイルシステムである
```

使用自体は単純です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	if (rmdir("./logs") == -1) {
		perror("rmdir");
		return EXIT_FAILURE;
	}

	puts("removed ./logs");
	return EXIT_SUCCESS;
}
```

#### ７章の２の４　ディレクトリの読み取り

ディレクトリ下のエントリ一覧を読むには、POSIX の `DIR *` ベースの API を使います。
これは `ls`、ファイルダイアログ、再帰処理、ファイル検索などの基礎になります。

まず `opendir()` でディレクトリストリームを開きます。

```c
#include <dirent.h>
#include <sys/types.h>

DIR *opendir(const char *name);
```

`DIR *` は、内部でディレクトリ読み取り位置やバッファなどを保持するオブジェクトです。

現在のPOSIXでは、対応するファイルディスクリプタを得る `dirfd()` も定義されています。

```c
#include <dirent.h>

int dirfd(DIR *dir);
```

ただし、`dirfd()` で取り出した fd に対して、勝手にファイル位置を動かすような操作を混ぜるのは避けるべきです。
`DIR *` の内部状態と食い違うおそれがあります。

##### ７章の２の４の１　ディレクトリストリームからの読み取り

エントリを1つずつ読むには `readdir()` を使います。

```c
#include <dirent.h>
#include <sys/types.h>

struct dirent *readdir(DIR *dir);
```

Linux では `struct dirent` に複数のメンバがありますが、移植性を考えるなら、まず `d_name` を中心に使うのが基本です。

```c
struct dirent {
	ino_t          d_ino;
	off_t          d_off;
	unsigned short d_reclen;
	unsigned char  d_type;
	char           d_name[256];
};
```

ただし、`d_type` は常に信頼できるとは限りません。
ファイルシステムによっては `DT_UNKNOWN` になることがあります。
そのため、型を厳密に知りたいなら `stat()` や `fstatat()` で確認する方が安全です。

`readdir()` は、次のエントリが取れたときはそのポインタを返し、終端またはエラー時は `NULL` を返します。
終端とエラーを区別したいなら、呼ぶ前に `errno = 0` にしておき、`NULL` が返ったあと `errno` を確認します。

次の例は、指定ディレクトリ内に特定ファイル名があるか調べます。

```c
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int find_file_in_dir(const char *path, const char *file)
{
	DIR *dir;
	struct dirent *entry;
	int found = 0;

	dir = opendir(path);
	if (dir == NULL) {
		perror("opendir");
		return -1;
	}

	errno = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, file) == 0) {
			found = 1;
			break;
		}
	}

	if (entry == NULL && errno != 0) {
		perror("readdir");
		closedir(dir);
		return -1;
	}

	if (closedir(dir) == -1) {
		perror("closedir");
		return -1;
	}

	return found ? 0 : 1;
}
```

##### ７章の２の４の２　ディレクトリストリームのクローズ

`opendir()` で開いたディレクトリストリームは、`closedir()` で閉じます。

```c
#include <dirent.h>
#include <sys/types.h>

int closedir(DIR *dir);
```

成功時は 0、失敗時は -1 を返します。
エラーとしては `EBADF` が代表的で、無効なディレクトリストリームを渡したことを表します。

ファイルと同様、開いたら閉じる、という基本は変わりません。

##### ７章の２の４の３　ディレクトリを読む低レベルの仕組み

ユーザ空間の通常のアプリケーションでは、ディレクトリ読み取りに `opendir()` / `readdir()` / `closedir()` を使えば十分です。

内部では Linux 固有の低レベル仕組みとして `getdents64` 系が使われています。
古い資料では `getdents()` や `_syscall3()` マクロが出てくることがありますが、これをアプリケーションから直接使う前提で学ぶ必要はありません。

重要なのは次の整理です。

```text
普段のアプリケーション:
	Cライブラリの opendir()/readdir()/closedir() を使う

Linux固有の低レベル実装:
	getdents64 系が下で使われることがある

結論:
	直接 syscall を叩く理由がない限り、高水準APIを使う
```

移植性、保守性、安全性のどれを取っても、通常は高水準 API を使う方がよいです。





