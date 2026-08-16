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

### ７章の３　ファイルのリンク

ここまで見てきたように、ディレクトリは「名前」と「inode への参照」を管理しています。
この対応関係をリンクと呼びます。

1つの inode を、複数の名前から参照できることが、Unix系ファイルシステムの重要な特徴です。

```text
1つのファイル実体:
	1つの inode

その inode を指す複数の名前:
	複数のリンク
```

たとえば、同じファイルを `/etc/customs` と `/var/run/ledger` の2つの名前で参照できる場合があります。
ただし、これは同じファイルシステム内に限られます。
inode 番号はファイルシステムごとに意味を持つため、別ファイルシステムへそのまま張ることはできません。

この種のリンクをハードリンクと呼びます。
ハードリンクには「元の名前」と「コピー先」のような主従関係はありません。
どの名前も同じ inode を指しており、立場は対等です。

一方で、もう1種類、シンボリックリンクがあります。
こちらは inode を直接共有するのではなく、「別のパス名を指す特殊なファイル」です。

#### ７章の３の１　ハードリンク

既存ファイルに対して新しいハードリンクを作るには `link()` を使います。

```c
#include <unistd.h>

int link(const char *oldpath, const char *newpath);
int linkat(int olddirfd, const char *oldpath,
	   int newdirfd, const char *newpath, int flags);
```

成功すると、`newpath` という新しい名前が `oldpath` と同じ inode を指すようになります。
結果として、両者は完全に同じ実体を共有します。

```text
link("a", "b") の後:
	a と b は同じ inode を指す
	どちらから書いても同じ内容が見える
	どちらか一方だけが「本体」ということはない
```

ただし、ハードリンクには制約があります。

```text
同じファイルシステム内でしか作れない

通常はディレクトリには作れない

ファイルシステムごとのリンク数上限がある
```

リンク数は `stat` の `st_nlink` で確認できます。
通常のファイルは 1 ですが、ハードリンクを追加すると増えます。

リンク数が 0 になっても、まだそのファイルを open しているプロセスがいれば、実体はすぐには消えません。
Linux では、おおまかには「ディレクトリエントリからの参照」と「オープン中の参照」の両方がなくなったときに、実データが解放されます。

`link()` の代表的なエラーは次の通りです。

```text
EACCES:
	oldpath の探索権限、または newpath を作る権限がない

EEXIST:
	newpath が既に存在する

EFAULT:
	ポインタが無効

EIO:
	内部I/Oエラー

ELOOP:
	シンボリックリンク解決が深すぎる

EMLINK:
	リンク数上限に達している

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	oldpath が存在しない、または newpath の親が存在しない

ENOMEM:
	必要メモリ不足

ENOSPC:
	newpath 側の保存領域不足

ENOTDIR:
	途中要素にディレクトリでないものがある

EPERM:
	権限上作成できない、またはディレクトリへのハードリンクを作ろうとした

EROFS:
	newpath 側が読み取り専用ファイルシステム

EXDEV:
	別ファイルシステムをまたいでいる
```

次の例は、既存ファイル `privateer` と同じ inode を指す `pirate` を作成します。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	if (link("/home/kidd/privateer", "/home/kidd/pirate") == -1) {
		perror("link");
		return EXIT_FAILURE;
	}

	puts("created hard link /home/kidd/pirate");
	return EXIT_SUCCESS;
}
```

現在のLinuxでは、`linkat()` に `AT_SYMLINK_FOLLOW` を指定して、シンボリックリンクをたどるかどうかを明示することもできます。
このあたりは `link()` より `linkat()` の方が意図を表しやすい場合があります。

#### ７章の３の２　シンボリックリンク

シンボリックリンクは、ハードリンクとは異なり、別のパス名を内容として持つ特殊ファイルです。
参照先の inode を直接共有するわけではありません。

```text
ハードリンク:
	同じ inode を共有する

シンボリックリンク:
	別のパス名を記録しておき、利用時に解決する
```

そのため、シンボリックリンクには次の特徴があります。

```text
別ファイルシステムへ張れる

ディレクトリも参照できる

存在しない対象にも作れる
```

最後の性質は重要です。
`symlink()` は通常、参照先が本当に存在するかを検証しません。
単に「このパス名を指すリンク」を作るだけです。
したがって、参照先が存在しない壊れたシンボリックリンク、いわゆる dangling symlink も作れます。

作成には `symlink()` を使います。

```c
#include <unistd.h>

int symlink(const char *oldpath, const char *newpath);
int symlinkat(const char *oldpath, int newdirfd, const char *newpath);
```

ここでの `oldpath` は「既存ファイルを必ず指していなければならない実体」ではなく、リンク先として保存したいパス文字列です。

代表的なエラーは次の通りです。

```text
EACCES:
	newpath を作る場所への権限がない

EEXIST:
	newpath が既に存在する

EFAULT:
	ポインタが無効

EIO:
	内部I/Oエラー

ELOOP:
	newpath 側のパス解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	newpath の親ディレクトリが存在しない

ENOMEM:
	必要メモリ不足

ENOSPC:
	保存領域不足

ENOTDIR:
	newpath の途中要素にディレクトリでないものがある

EPERM:
	作成権限がない

EROFS:
	newpath 側が読み取り専用ファイルシステム
```

次の例は、`/home/kidd/privateer` を参照するシンボリックリンク `pirate` を作ります。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	if (symlink("/home/kidd/privateer", "/home/kidd/pirate") == -1) {
		perror("symlink");
		return EXIT_FAILURE;
	}

	puts("created symbolic link /home/kidd/pirate");
	return EXIT_SUCCESS;
}
```

相対パスを使ったシンボリックリンクもよく使われます。
その場合、解決基準は「リンクをたどる時点でのリンク自身の位置関係」です。
移動や配布を考えるなら、絶対パスと相対パスのどちらが適切かを意識した方がよいです。

#### ７章の３の３　ファイルのアンリンク/削除

リンク作成の反対は、名前を外すことです。
Unix系では、ファイルの削除は本質的に「ディレクトリエントリを1つ外す」操作として扱われます。

これを行う基本 API が `unlink()` です。

```c
#include <unistd.h>

int unlink(const char *pathname);
int unlinkat(int dirfd, const char *pathname, int flags);
```

成功すると、`pathname` という名前がディレクトリから消えます。
それが最後のリンクで、かつ open 中の参照もなければ、実体も最終的に解放されます。

ここで重要なのは、`unlink()` が消すのは「名前」である、という点です。

```text
最後のリンクを外した:
	新たにその名前からは開けない

しかし open 中のfdが残っている:
	そのプロセスは引き続き内容を使える
```

この性質は、一時ファイル処理でよく使われます。
作ってすぐ `unlink()` しておけば、名前は消えても、開いている間だけ実体を使い続けられます。

また、`pathname` がシンボリックリンクなら、削除されるのはリンク自身であって参照先ではありません。

代表的なエラーは次の通りです。

```text
EACCES:
	親ディレクトリへの権限または探索権限がない

EFAULT:
	ポインタが無効

EIO:
	内部I/Oエラー

EISDIR:
	pathname がディレクトリである

ELOOP:
	シンボリックリンク解決が深すぎる

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	対象が存在しない

ENOMEM:
	必要メモリ不足

ENOTDIR:
	途中要素にディレクトリでないものがある

EPERM:
	削除権限がない

EROFS:
	読み取り専用ファイルシステムである
```

ディレクトリは `unlink()` では削除しません。
ディレクトリ削除には前節の `rmdir()` を使います。
ただし `unlinkat()` では `AT_REMOVEDIR` を使ってディレクトリ削除を行うこともできます。

単純な例です。

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	if (unlink("./temporary.txt") == -1) {
		perror("unlink");
		return EXIT_FAILURE;
	}

	puts("removed ./temporary.txt");
	return EXIT_SUCCESS;
}
```

ファイルかディレクトリかを意識せず削除したいときは、標準Cライブラリの `remove()` が使えます。

```c
#include <stdio.h>

int remove(const char *path);
```

`remove()` は、対象が通常ファイルなら `unlink()` 相当、ディレクトリなら `rmdir()` 相当の処理を行います。
戻り値は成功で 0、失敗で -1 です。

ただし、挙動の本質を理解するには、まず `unlink()` と `rmdir()` を別物として捉える方が分かりやすいです。

### ７章の４　ファイルのコピーと移動

日常的なファイル操作として最も基本的なのが、コピーと移動です。
シェルでは普通 `cp` と `mv` を使いますが、内部で何をしているかはかなり違います。

```text
コピー:
	内容を読み出して別の新規ファイルへ書き込む
	結果は別 inode になる

移動:
	多くの場合は名前の付け替え
	同一ファイルシステム内なら内容コピーは不要
```

つまり、コピーは「似た内容の別ファイルを作る」ことであり、ハードリンクのように実体を共有するわけではありません。
そのため、コピー後は片方を書き換えても、もう片方には影響しません。

一方、移動は多くの場合ディレクトリエントリの更新です。
同じファイルシステム内なら、inode 自体はそのままで、名前や親ディレクトリだけが変わると考えると分かりやすいです。

#### ７章の４の１　ファイルコピー

Unix には、伝統的に「ファイル全体を丸ごとコピーする専用システムコール」はありません。
そのため、`cp` などのツールは、基本的には次の流れを組み立てます。

```text
1. コピー元を open する
2. コピー先を open または create する
3. コピー元から読む
4. コピー先へ書く
5. 終端まで繰り返す
6. 閉じる
```

ディレクトリコピーも、結局は各ディレクトリを作り、下のファイルを順に複写する再帰処理です。

古い説明としてはこれで本質を押さえられますが、現在のLinuxには補足があります。
現代の `cp` 実装は、状況に応じて `copy_file_range()`、`sendfile()`、`splice()`、あるいは CoW reflink などの最適化を使うことがあります。
ただし、学習の基本としては「読む/書くの繰り返し」で理解して問題ありません。

もっとも単純なコピーの例を示します。

```c
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum { COPY_BUFFER_SIZE = 8192 };

int main(int argc, char *argv[])
{
	char buffer[COPY_BUFFER_SIZE];
	int src_fd;
	int dst_fd;
	ssize_t bytes_read;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <src> <dst>\n", argv[0]);
		return EXIT_FAILURE;
	}

	src_fd = open(argv[1], O_RDONLY);
	if (src_fd == -1) {
		perror("open src");
		return EXIT_FAILURE;
	}

	dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (dst_fd == -1) {
		perror("open dst");
		close(src_fd);
		return EXIT_FAILURE;
	}

	while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
		ssize_t total_written = 0;
		while (total_written < bytes_read) {
			ssize_t bytes_written = write(dst_fd,
					      buffer + total_written,
					      (size_t)(bytes_read - total_written));
			if (bytes_written == -1) {
				perror("write");
				close(dst_fd);
				close(src_fd);
				return EXIT_FAILURE;
			}
			total_written += bytes_written;
		}
	}

	if (bytes_read == -1) {
		perror("read");
		close(dst_fd);
		close(src_fd);
		return EXIT_FAILURE;
	}

	if (close(dst_fd) == -1) {
		perror("close dst");
		close(src_fd);
		return EXIT_FAILURE;
	}

	if (close(src_fd) == -1) {
		perror("close src");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
```

ここでは短い書き込み、つまり 1 回の `write()` で全部書けない場合にも対応しています。
教材としては、この形を基本にしておく方が安全です。

#### ７章の４の２　ファイル移動

移動には `rename()` を使います。

```c
#include <stdio.h>

int rename(const char *oldpath, const char *newpath);
int renameat(int olddirfd, const char *oldpath,
	     int newdirfd, const char *newpath);
```

同じファイルシステム内なら、`rename()` は本質的に名前の付け替えです。
内容のコピーは発生せず、inode も変わりません。

ただし、別ファイルシステムをまたぐ移動はできません。
その場合 `rename()` は `EXDEV` で失敗し、`mv` などのツールは「コピーして元を消す」処理へ切り替えます。

成功時は 0、失敗時は -1 を返します。
失敗した場合、基本的には oldpath と newpath の状態を中途半端に壊さないよう扱われます。

代表的なエラーは次の通りです。

```text
EACCES:
	親ディレクトリへの権限や探索権限がない

EBUSY:
	マウントポイントなどで使用中

EFAULT:
	ポインタが無効

EINVAL:
	自分自身の下へ移動しようとするなど不正な指定

EISDIR:
	種類不一致で上書きできない

ELOOP:
	シンボリックリンク解決が深すぎる

EMLINK:
	リンク数上限などに達している

ENAMETOOLONG:
	パスが長すぎる

ENOENT:
	対象または親が存在しない

ENOMEM:
	必要メモリ不足

ENOSPC:
	必要な領域が不足

ENOTDIR:
	ディレクトリと非ディレクトリの不整合

ENOTEMPTY:
	対象ディレクトリが空でない

EPERM:
	sticky bit や権限の都合で許可されない

EROFS:
	読み取り専用ファイルシステム

EXDEV:
	別ファイルシステムをまたいでいる
```

単純な例です。

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	if (rename("./draft.txt", "./final.txt") == -1) {
		perror("rename");
		return EXIT_FAILURE;
	}

	puts("renamed ./draft.txt to ./final.txt");
	return EXIT_SUCCESS;
}
```

現在のLinuxでは `renameat2()` もあり、`RENAME_NOREPLACE` や `RENAME_EXCHANGE` などのフラグを使えます。
ただし、まずは `rename()` の「同一ファイルシステム内での原子的な名前変更」という性質を押さえる方が重要です。

### ７章の５　デバイスノード

デバイスノードは、ユーザ空間からデバイスドライバへアクセスするための特殊ファイルです。
見た目はファイルパスですが、普通のファイルのようにディスク上の内容を読むとは限りません。

アプリケーションがデバイスノードに対して `open()`、`read()`、`write()` などを行うと、カーネルは通常ファイルI/Oではなく、対応するデバイスドライバへ要求を渡します。

```text
通常ファイル:
	ファイルシステム上の内容を読む/書く

デバイスノード:
	対応デバイスやドライバに処理を渡す
```

この仕組みによって、Unix系OSでは多くのデバイスを「ファイルのように扱う」統一的な設計が成り立っています。

ただし、現代のLinuxでは、すべてのハードウェアが単純に `/dev/*` だけで完結するわけではありません。
ネットワークはソケットや netlink、sysfs、ioctl、`/proc`、`/sys` など、複数のインタフェースと組み合わせて扱うことが多いです。

デバイスノードには、どのドライバへ結び付けるかを示す番号が入っています。
これがメジャー番号とマイナー番号です。

```text
メジャー番号:
	どの種類のドライバか

マイナー番号:
	その中のどの個体・どの機能か
```

対応するドライバやデバイスが使えない場合、`open()` が `ENODEV` などで失敗することがあります。

#### ７章の５の１　特殊なデバイスノード

Linux には、学習や運用でよく使う特別なデバイスノードがあります。

```text
/dev/null:
	書いたデータを捨てる
	読むと EOF

/dev/zero:
	読むと 0x00 バイト列が続く
	書き込みは通常無視される

/dev/full:
	読むと 0x00 バイト列が続く
	書くと常に ENOSPC
```

`/dev/null` は不要な出力の捨て先として非常によく使われます。
`/dev/zero` はゼロ初期化されたデータ源として使えますが、現在はメモリ確保や初期化の別手段も多く、昔ほど前面には出ません。
`/dev/full` は「容量不足時の失敗」を試したいときに便利です。

これらはアプリケーションの異常系テストでも役立ちます。
たとえば「書き込み先が必ず失敗する状況」を作りたいなら `/dev/full` が使えます。

#### ７章の５の２　乱数ジェネレータ

Linux には乱数取得用として `/dev/random` と `/dev/urandom` があります。

```text
/dev/random:
	条件によってはブロックすることがある

/dev/urandom:
	通常はこちらが使われる
```

古い資料では、「暗号用途では `/dev/random`、それ以外では `/dev/urandom`」のように強く分けて説明されることがあります。
しかし、現在のLinuxではこの理解を少し更新した方がよいです。

現在は、多くの用途で `/dev/urandom`、あるいはそれより直接的に `getrandom()` を使うのが一般的です。
通常の暗号用途でも、起動直後の初期化が完了したシステムでは `/dev/urandom` や `getrandom()` で十分と考えられることが多いです。

`/dev/random` を特別視しすぎると、必要以上にブロックしてアプリケーションが止まる原因になります。
特にヘッドレス環境、組み込み環境、仮想環境ではその影響が見えやすいです。

そのため、今の実務感覚では次の整理が分かりやすいです。

```text
推奨の第一候補:
	getrandom()

次点:
	/dev/urandom

/dev/random:
	特殊な要件を理解したうえで使うもの
```

歴史的には `/dev/random` はエントロピー見積もりに応じてブロックし、`/dev/urandom` はブロックしないという違いが強調されてきました。
この違い自体は理解しておいてよいですが、「高品質な乱数が欲しいなら常に `/dev/random`」と覚えるのは今では適切ではありません。

もし乱数API自体を章の後半で使うなら、サンプルコードは `/dev/random` を直接読むより、`getrandom()` を中心にした方が現代的です。





