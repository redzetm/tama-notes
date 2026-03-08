---
title: "initramfs /init 実装（init.c）"
date: 2026-03-08
---

# initramfs `/init` 実装（C）

元ファイル：[img/init.c](img/init.c)

```c
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * このファイルは「initramfs の /init」として動く C プログラムです。
 *
 * 用語（超重要）
 * - initramfs: カーネルが最初に使う一時的なルートファイルシステム。
 *   まだ永続 rootfs（disk.img）に到達していない、最小の環境。
 * - /init: initramfs 内で最初に実行されるユーザーランドプログラム。
 * - pid=1: 最初のプロセス番号。ここが落ちると OS 起動は先へ進めません。
 *
 * ここでの責務
 * - /proc/cmdline の root=UUID=... を読み取り
 * - /dev のブロックデバイス候補を走査して
 * - ext4 superblock の UUID が一致するデバイス（rootfs）を見つけ
 * - /newroot へ mount し
 * - switch_root で永続 rootfs の /sbin/init へバトンタッチする
 *
 * どれか1つでも失敗すると「どこで何が失敗したか」をログに出して停止します。
 * （研究/観測用途なので、勝手に再起動してログを流さない方針）
 */

/*
 * UmuOS-0.1.7 initramfs /init (C)
 *
 * 目的:
 * - /proc/cmdline の root=UUID=... を読み取る
 * - /dev 配下の候補デバイスを走査し、ext4 superblock の UUID が一致するデバイスを探す
 * - /newroot に mount して /bin/switch_root で /sbin/init へ移行する
 *
 * 方針（0.1.1）:
 * - 観測性: 失敗理由と走査内容を必ず出力する
 * - 依存を減らす: udev や /dev/disk/by-uuid に依存しない
 * - initramfs 段階では永続領域（/newroot 配下）へ書かない
 */

enum {
	CMDLINE_MAX = 4096,
	UUID_BIN_LEN = 16,
	UUID_STR_MAX = 64,
};

/*
 * log_printf:
 * - initramfs では syslog 等が無い/未初期化なことが多いので、最小のログ関数を自前で用意。
 * - printf(3) をそのまま使わず、write(2) で fd=2（stderr）へ直接書く。
 *   → 標準入出力のバッファリングや tty の状態に影響されにくく、確実に出したい。
 * - 末尾改行を保証して、ログが1行単位で読めるようにする。
 */
static void log_printf(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/* 末尾改行を保証 */
	size_t len = strnlen(buf, sizeof(buf));
	if (len == 0 || buf[len - 1] != '\n') {
		if (len + 1 < sizeof(buf)) {
			buf[len] = '\n';
			buf[len + 1] = '\0';
			len++;
		}
	}

	/* stderr に集約（/dev/console と二重出力になることがあるため） */
	(void)write(2, buf, len);
	fflush(stderr);
}

/*
 * emergency_loop:
 * - ここに入ったら「致命的に先へ進めない」という意味。
 * - 1秒 sleep を繰り返して CPU を無駄に使わない。
 * - 観測用途として「停止した状態を維持し、ログを消さない」ため。
 */
static void emergency_loop(void)
{
	log_printf("[init] entering emergency loop (sleep)");
	for (;;) {
		sleep(1);
	}
}

/*
 * ensure_dir:
 * - mount 先のディレクトリ（/proc, /sys, /dev, /newroot など）を確実に作る。
 * - すでに存在する場合（EEXIST）は成功扱い。
 */
static int ensure_dir(const char *path, mode_t mode)
{
	if (mkdir(path, mode) == 0) {
		return 0;
	}
	if (errno == EEXIST) {
		return 0;
	}
	log_printf("[init] mkdir failed: path=%s errno=%d (%s)", path, errno, strerror(errno));
	return -1;
}

/*
 * mount_if_needed:
 * - initramfs で必要なファイルシステムを mount する共通関数。
 * - EBUSY は「すでに mount 済み」の代表的な errno なので、失敗にしない。
 *   （複数回呼ばれる/カーネル側で自動マウントされている等のケースに強くする）
 */
static int mount_if_needed(const char *source, const char *target, const char *fstype, unsigned long flags, const char *data)
{
	if (ensure_dir(target, 0755) != 0) {
		return -1;
	}

	if (mount(source, target, fstype, flags, data) == 0) {
		log_printf("[init] mount ok: %s on %s type=%s", source ? source : "(none)", target, fstype);
		return 0;
	}

	if (errno == EBUSY) {
		log_printf("[init] mount skip (busy): %s on %s type=%s", source ? source : "(none)", target, fstype);
		return 0;
	}

	log_printf("[init] mount failed: %s on %s type=%s errno=%d (%s)",
		  source ? source : "(none)", target, fstype, errno, strerror(errno));
	return -1;
}

/*
 * read_file_to_buf:
 * - 小さなテキストファイル（ここでは /proc/cmdline）を読み込むための関数。
 * - buf の末尾に '\0'（C文字列の終端）を必ず付ける。
 *
 * C初心者メモ:
 * - char *buf は「文字配列の先頭アドレス」。
 * - size_t buf_size は「配列のサイズ（バイト数）」。
 */
static int read_file_to_buf(const char *path, char *buf, size_t buf_size)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		log_printf("[init] open failed: %s errno=%d (%s)", path, errno, strerror(errno));
		return -1;
	}

	ssize_t n = read(fd, buf, buf_size - 1);
	int saved_errno = errno;
	close(fd);

	if (n < 0) {
		errno = saved_errno;
		log_printf("[init] read failed: %s errno=%d (%s)", path, errno, strerror(errno));
		return -1;
	}

	buf[n] = '\0';
	return 0;
}

/*
 * 16進数1文字（0-9,a-f,A-F）を 0〜15 の数値に変換。
 * - それ以外は -1（不正）を返す。
 */
static int hex_value(int c)
{
	if ('0' <= c && c <= '9') {
		return c - '0';
	}
	if ('a' <= c && c <= 'f') {
		return 10 + (c - 'a');
	}
	if ('A' <= c && c <= 'F') {
		return 10 + (c - 'A');
	}
	return -1;
}

/*
 * UUID 文字列を 16byte に変換する。
 * - "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"（36文字）を想定
 * - ハイフンは無視
 */
static int parse_uuid_string(const char *s, uint8_t out[UUID_BIN_LEN])
{
	/*
	 * 実装方針:
	 * - UUID は「16バイト（128bit）」の値。
	 * - 文字列としては 32個の16進数 + ハイフン、という表現が多い。
	 * - ここでは、ハイフンを飛ばしつつ 16進数2文字を1バイトに変換して out[] に詰める。
	 *
	 * high:
	 * - 16進数は 1文字で 4bit。
	 * - 2文字で 1byte になるので、最初の4bit（上位 nibble）を一旦 high に保持する。
	 */
	int out_pos = 0;
	int high = -1;

	for (size_t i = 0; s[i] != '\0'; i++) {
		unsigned char ch = (unsigned char)s[i];
		if (ch == ' ' || ch == '\n' || ch == '\t') {
			/* 末尾（空白区切り）までを UUID とみなす */
			break;
		}
		if (ch == '-') {
			/* ハイフンは単に見た目用なので無視 */
			continue;
		}

		int v = hex_value(ch);
		if (v < 0) {
			return -1;
		}

		if (high < 0) {
			/* 1文字目（上位4bit）を保持 */
			high = v;
			continue;
		}

		if (out_pos >= UUID_BIN_LEN) {
			return -1;
		}
		out[out_pos] = (uint8_t)((high << 4) | v);
		out_pos++;
		high = -1;
	}

	if (high != -1) {
		return -1;
	}
	if (out_pos != UUID_BIN_LEN) {
		return -1;
	}
	return 0;
}

/*
 * uuid_to_string:
 * - 16バイトの UUID を、一般的な 8-4-4-4-12 形式の文字列へ整形する。
 * - out のサイズは UUID_STR_MAX とし、snprintf で安全側に書く。
 */
static void uuid_to_string(const uint8_t uuid[UUID_BIN_LEN], char out[UUID_STR_MAX])
{
	snprintf(out, UUID_STR_MAX,
		 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		 uuid[0], uuid[1], uuid[2], uuid[3],
		 uuid[4], uuid[5],
		 uuid[6], uuid[7],
		 uuid[8], uuid[9],
		 uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

/*
 * /proc/cmdline から root=UUID=... を抜き出す。
 *
 * - /proc/cmdline は「カーネル起動引数」が入ったテキスト。
 * - GRUB の linux 行で渡した文字列が、ここで見える。
 * - ここで UUID を取り出せないと、永続 rootfs を特定できないので致命的。
 */
static int parse_root_uuid_from_cmdline(uint8_t out_uuid[UUID_BIN_LEN])
{
	char cmdline[CMDLINE_MAX];
	if (read_file_to_buf("/proc/cmdline", cmdline, sizeof(cmdline)) != 0) {
		return -1;
	}

	log_printf("[init] /proc/cmdline: %s", cmdline);

	const char *key = "root=UUID=";
	char *p = strstr(cmdline, key);
	if (p == NULL) {
		log_printf("[init] root=UUID= not found in cmdline");
		return -1;
	}
	p += strlen(key);

	if (parse_uuid_string(p, out_uuid) != 0) {
		log_printf("[init] invalid UUID string in cmdline: %s", p);
		return -1;
	}

	char uuid_str[UUID_STR_MAX];
	uuid_to_string(out_uuid, uuid_str);
	log_printf("[init] cmdline parsed: root=UUID=%s", uuid_str);
	log_printf("[init] want root UUID: %s", uuid_str);
	return 0;
}

/*
 * ext4 superblock
 * - superblock offset: 1024 bytes
 * - s_magic offset: 0x38 (2 bytes) must be 0xEF53
 * - s_uuid  offset: 0x68 (16 bytes)
 */
static int read_ext4_uuid_from_device(const char *dev_path, uint8_t out_uuid[UUID_BIN_LEN])
{
	/*
	 * ここは「そのブロックデバイスが ext4 か？」を quick check しつつ、UUID を抜く部分。
	 *
	 * 理屈:
	 * - ext4 には superblock というメタ情報があり、先頭から1024バイトの位置にある。
	 * - superblock には magic(0xEF53) と UUID が入っている。
	 * - udev や /dev/disk/by-uuid が無くても、直接読めば UUID がわかる。
	 */
	int fd = open(dev_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return -1;
	}

	uint8_t sb[1024];
	ssize_t n = pread(fd, sb, sizeof(sb), (off_t)1024);
	int saved_errno = errno;
	close(fd);
	if (n != (ssize_t)sizeof(sb)) {
		errno = saved_errno;
		return -1;
	}

	uint16_t magic = (uint16_t)sb[0x38] | ((uint16_t)sb[0x39] << 8);
	if (magic != 0xEF53) {
		/* ext4 ではない（または superblock が読めていない） */
		return -1;
	}

	memcpy(out_uuid, &sb[0x68], UUID_BIN_LEN);
	return 0;
}

/* 文字列が prefix で始まるかの判定。例: starts_with("vda", "vd") == true */
static bool starts_with(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool is_candidate_dev_name(const char *name)
{
	/*
	 * 最低限の候補
	 * - vd[a-z], vd[a-z][0-9]*
	 * - sd[a-z], sd[a-z][0-9]*
	 * - nvme0n1, nvme0n1p1 など
	 *
	 * なぜ候補を絞る？
	 * - /dev には tty や null など大量のデバイスがある。
	 * - 全部読みに行くと遅い・ノイズが多い。
	 * - 研究用の最小環境では「起動に必要なブロックデバイスだけ」を見たい。
	 */
	size_t len = strlen(name);
	if (len >= 3 && starts_with(name, "vd") && isalpha((unsigned char)name[2])) {
		return true;
	}
	if (len >= 3 && starts_with(name, "sd") && isalpha((unsigned char)name[2])) {
		return true;
	}
	if (starts_with(name, "nvme")) {
		return true;
	}
	return false;
}

/*
 * path が「ブロックデバイスファイル」かどうか。
 * - /dev/vda のようなディスクはブロックデバイス。
 * - /dev/ttyS0 のような端末は別種。
 */
static bool is_block_device(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return false;
	}
	return S_ISBLK(st.st_mode);
}

/*
 * dump_ext4_candidate_uuids:
 * - root UUID が見つからないときの“デバッグ用ダンプ”。
 * - /dev の候補を走査し、ext4 と判定できたものだけ uuid を表示。
 * - want_uuid と一致するか（match=1/0）も一緒に出す。
 */
static void dump_ext4_candidate_uuids(const uint8_t want_uuid[UUID_BIN_LEN])
{
	DIR *d = opendir("/dev");
	if (d == NULL) {
		log_printf("[init] opendir /dev failed: errno=%d (%s)", errno, strerror(errno));
		return;
	}

	struct dirent *ent;
	int printed = 0;
	while ((ent = readdir(d)) != NULL) {
		const char *name = ent->d_name;
		if (name[0] == '.') {
			continue;
		}
		if (!is_candidate_dev_name(name)) {
			continue;
		}

		char dev_path[512];
		int n = snprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
		if (n < 0 || (size_t)n >= sizeof(dev_path)) {
			continue;
		}
		if (!is_block_device(dev_path)) {
			continue;
		}

		uint8_t got_uuid[UUID_BIN_LEN];
		if (read_ext4_uuid_from_device(dev_path, got_uuid) != 0) {
			continue;
		}

		char got_str[UUID_STR_MAX];
		uuid_to_string(got_uuid, got_str);
		log_printf("[init] ext4: dev=%s uuid=%s match=%d", dev_path, got_str,
			  memcmp(got_uuid, want_uuid, UUID_BIN_LEN) == 0 ? 1 : 0);
		printed++;
	}

	closedir(d);
	log_printf("[init] ext4 candidates printed: %d", printed);
}

/*
 * find_device_by_uuid:
 * - want_uuid と一致する ext4 デバイスを探す。
 * - 見つかったら out_path に "/dev/vda" のようなパス文字列を書き込み、0を返す。
 * - 見つからなければ -1。
 *
 * C初心者メモ:
 * - out_path は「呼び出し元が用意した文字配列」。ここに結果を書き込む。
 * - out_path_size は配列サイズ。strncpy + 終端 '\0' で安全側にする。
 */
static int find_device_by_uuid(const uint8_t want_uuid[UUID_BIN_LEN], char *out_path, size_t out_path_size)
{
	DIR *d = opendir("/dev");
	if (d == NULL) {
		log_printf("[init] opendir /dev failed: errno=%d (%s)", errno, strerror(errno));
		return -1;
	}

	struct dirent *ent;
	int scanned = 0;
	while ((ent = readdir(d)) != NULL) {
		const char *name = ent->d_name;
		if (name[0] == '.') {
			continue;
		}
		if (!is_candidate_dev_name(name)) {
			continue;
		}

		char dev_path[512];
		int n = snprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
		if (n < 0 || (size_t)n >= sizeof(dev_path)) {
			continue;
		}
		if (!is_block_device(dev_path)) {
			continue;
		}

		scanned++;
		log_printf("[init] scan: %s", dev_path);

		uint8_t got_uuid[UUID_BIN_LEN];
		if (read_ext4_uuid_from_device(dev_path, got_uuid) != 0) {
			continue;
		}

		if (memcmp(got_uuid, want_uuid, UUID_BIN_LEN) == 0) {
			/* 一致！ out_path にコピーして呼び出し元へ返す */
			strncpy(out_path, dev_path, out_path_size);
			out_path[out_path_size - 1] = '\0';

			char uuid_str[UUID_STR_MAX];
			uuid_to_string(got_uuid, uuid_str);
			log_printf("[init] matched: dev=%s uuid=%s", dev_path, uuid_str);

			closedir(d);
			return 0;
		}
	}

	closedir(d);
	log_printf("[init] device scan done: scanned=%d (no match)", scanned);
	return -1;
}

int main(void)
{
	log_printf("[init] UmuOS initramfs init start");

	/*
	 * 1) 最低限のFSをマウント
	 *
	 * ここで mount する理由:
	 * - /proc: /proc/cmdline を読むために必要
	 * - /sys : 一部のデバイス列挙やカーネル情報（将来拡張/観測）に必要
	 * - /dev : ブロックデバイス（/dev/vda 等）を見つけるために必要
	 * - /dev/pts: getty/telnet 等の端末が絡むケースの土台（最小でも mount しておく）
	 */
	if (mount_if_needed("proc", "/proc", "proc", 0, "") != 0) {
		emergency_loop();
	}
	if (mount_if_needed("sysfs", "/sys", "sysfs", 0, "") != 0) {
		emergency_loop();
	}
	if (mount_if_needed("devtmpfs", "/dev", "devtmpfs", 0, "") != 0) {
		emergency_loop();
	}
	log_printf("[init] devtmpfs mounted");
	if (mount_if_needed("devpts", "/dev/pts", "devpts", 0, "") != 0) {
		emergency_loop();
	}

	/*
	 * 2) root UUID を取得
	 *
	 * - GRUB が渡した kernel cmdline（/proc/cmdline）から root=UUID=... を探す。
	 * - ここで UUID が取れないと、どのディスクが rootfs か決められない。
	 */
	uint8_t want_uuid[UUID_BIN_LEN];
	if (parse_root_uuid_from_cmdline(want_uuid) != 0) {
		emergency_loop();
	}

	/*
	 * 3) UUID一致のデバイスを探して mount
	 *
	 * - 起動直後は virtio-blk 等のデバイスがまだ出揃っていないことがある。
	 * - udev のような自動デバイス管理に依存しない代わりに、自前でリトライして待つ。
	 */
	const int max_tries = 120;          /* 120 * 0.25s = 30s */
	const useconds_t wait_us = 250000;  /* 250ms */

	char dev_path[512];
	memset(dev_path, 0, sizeof(dev_path));
	bool mounted = false;

	for (int i = 1; i <= max_tries; i++) {
		if (find_device_by_uuid(want_uuid, dev_path, sizeof(dev_path)) != 0) {
			log_printf("[init] retry %d/%d (device not found yet)", i, max_tries);
			usleep(wait_us);
			continue;
		}

		log_printf("[init] root device found: %s", dev_path);

		log_printf("[init] mounting root: dev=%s -> /newroot", dev_path);
		if (ensure_dir("/newroot", 0755) != 0) {
			emergency_loop();
		}

		/*
		 * 設計通り rw（MS_RDONLY は付けない）。data は空で安全側。
		 *
		 * - flags=0 なので通常は read-write で mount を試みる。
		 * - 成功したら、以降の / は /newroot にある ext4 rootfs になる。
		 */
		if (mount(dev_path, "/newroot", "ext4", 0, "") == 0) {
			log_printf("[init] mount root ok (rw): %s", dev_path);
			log_printf("[init] mounted /newroot");

			/*
			 * 混線防止: 永続側へは書かない。存在だけ確認してログに出す。
			 *
			 * - initramfs 段階のプログラムが rootfs を書き換えると、
			 *   「起動を試しただけなのにディスク内容が変わる」事故が起きる。
			 * - ここでは最小限として /sbin/init 等があるかを見て、無ければ warn を出す。
			 */
			if (access("/newroot/sbin/init", F_OK) != 0) {
				log_printf("[init] warn: /newroot/sbin/init not found: errno=%d (%s)", errno, strerror(errno));
			}
			if (access("/newroot/etc/inittab", F_OK) != 0) {
				log_printf("[init] warn: /newroot/etc/inittab not found: errno=%d (%s)", errno, strerror(errno));
			}
			if (access("/newroot/etc/init.d/rcS", F_OK) != 0) {
				log_printf("[init] warn: /newroot/etc/init.d/rcS not found: errno=%d (%s)", errno, strerror(errno));
			}
			if (access("/bin/switch_root", X_OK) != 0) {
				log_printf("[init] warn: /bin/switch_root not executable in initramfs: errno=%d (%s)", errno,
					  strerror(errno));
			}

			mounted = true;
			break;
		}
		log_printf("[init] mount root failed: dev=%s errno=%d (%s)", dev_path, errno, strerror(errno));

		log_printf("[init] retry %d/%d", i, max_tries);
		usleep(wait_us);
	}

	if (!mounted) {
		/* タイムアウト: 一致する rootfs が見つからなかった */
		log_printf("[init] root mount not completed (timeout): dev=%s", dev_path[0] ? dev_path : "(none)");
		char uuid_str[UUID_STR_MAX];
		uuid_to_string(want_uuid, uuid_str);
		log_printf("[init] want root UUID: %s", uuid_str);
		dump_ext4_candidate_uuids(want_uuid);
		emergency_loop();
	}

	/*
	 * 4) switch_root
	 *
	 * - ここから先は永続 rootfs（/newroot）が主役。
	 * - /bin/switch_root は initramfs 側にある必要がある（BusyBox applet のことが多い）。
	 * - execv は成功すると戻ってこない。
	 */
	log_printf("[init] switching root");
	log_printf("[init] exec: /bin/switch_root /newroot /sbin/init");

	char *const argv[] = {
		"switch_root",
		"/newroot",
		"/sbin/init",
		NULL,
	};

	execv("/bin/switch_root", argv);
	/* ここに来た=execv 失敗（成功ならプロセスが置き換わるので戻らない） */
	log_printf("[init] execv switch_root failed: errno=%d (%s)", errno, strerror(errno));
	log_printf("[init] note: ensure /bin/switch_root exists in initramfs (BusyBox applet)");
	log_printf("[init] note: ensure ext4 root has /sbin/init (symlink to /bin/busybox) and /etc/inittab");

	emergency_loop();
	return 1;
}

```
