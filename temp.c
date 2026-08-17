#include <poll.h>
#include <sys/stat.h>


struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd *fds, nfds_t nfds, int timeout);


struct stat {
    dev_t     st_dev;     /* デバイス番号 */
    ino_t     st_ino;     /* inode番号 */
    mode_t    st_mode;    /* ファイルタイプ + パーミッション */
    nlink_t   st_nlink;   /* ハードリンク数 */
    uid_t     st_uid;     /* 所有者UID */
    gid_t     st_gid;     /* 所有グループGID */
    dev_t     st_rdev;    /* 特殊ファイルのデバイス番号 */
    off_t     st_size;    /* ファイルサイズ */
    blksize_t st_blksize; /* 最適ブロックサイズ */
    blkcnt_t  st_blocks;  /* 割り当てブロック数 */

    struct timespec st_atim; /* 最終アクセス時刻 */
    struct timespec st_mtim; /* 最終更新時刻 */
    struct timespec st_ctim; /* 最終ステータス変更時刻 */
};


