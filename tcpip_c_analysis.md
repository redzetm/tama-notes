---
title: "TCP/IP通信解析の為のC言語"
---

# TCP/IP通信解析の為のC言語

このノートは、TCP/IP通信を「C言語で読む」ための入門です。

目的は、いきなり高機能なパケットキャプチャツールを作ることではありません。
まずは `tcpdump` や `Wireshark` で見える通信が、Cのプログラムからはどのような **バイト列** として見えるのかを理解します。

このノートで重視すること：

- パケットを `unsigned char` の配列として読む
- 16進数とビット演算に慣れる
- ネットワークバイトオーダーを理解する
- IPv4 / TCP / UDP / ICMP のヘッダーをCで取り出す
- `struct` で直接読む場合の落とし穴を理解する
- 解析プログラムで「どこまで読んでよいか」を必ず確認する

前提：

- OS：Linux想定
- コンパイラ：`gcc`
- 規格：`-std=c17`
- まずは実パケット取得ではなく、固定のバイト列を解析する

コンパイルの基本形：

```bash
gcc -Wall -Wextra -std=c17 -O0 sample.c -o sample
./sample
```

---

## 第一章　通信解析でC言語が見るもの

### パケットは最初から「構造体」ではない

TCP/IPの説明では、よく次のように表現します。

- Ethernetヘッダー
- IPv4ヘッダー
- TCPヘッダー
- HTTPメッセージ

しかし、ネットワークから受け取った直後のデータは、C言語から見るとただのバイト列です。

```c
unsigned char packet[] = {
    0x45, 0x00, 0x00, 0x28,
    0x12, 0x34, 0x40, 0x00
};
```

この `0x45` や `0x00` を、仕様に従って「IPv4ヘッダーの1バイト目」「全長フィールド」などとして読んでいきます。

つまり通信解析とは、かなり乱暴に言えば次の作業です。

1. バイト列を受け取る
2. プロトコル仕様に従って位置を決める
3. 必要なビットや数値を取り出す
4. 人間が読める形に表示する

### `char` ではなく `unsigned char` で読む

バイト列を扱うときは、基本的に `unsigned char` を使います。

```c
unsigned char b = 0xff;
printf("%u\n", b);
```

理由は、通信データの1バイトは `0x00` から `0xff` までの値をそのまま扱いたいからです。
普通の `char` は環境によって符号付きになることがあり、`0x80` 以上の値が負数として扱われる可能性があります。

通信解析では、値を勝手に「文字」や「符号付き整数」として解釈されると困ります。
そのため、まずは `unsigned char`、または `<stdint.h>` の `uint8_t` を使うと考えておくと安全です。

```c
#include <stdint.h>

uint8_t packet[1500];
```

`uint8_t` は「8ビット符号なし整数」です。
通信データを読むコードでは、意味がはっきりしているのでよく使われます。

---

## 第二章　16進数とビットを見る

### 16進数表示

パケット解析では、10進数よりも16進数で見る場面が多いです。

```c
/* file: dump_bytes.c */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

static void dump_bytes(const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        printf("%02x ", data[i]);

        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }

    if (len % 16 != 0) {
        printf("\n");
    }
}

int main(void)
{
    uint8_t packet[] = {
        0x45, 0x00, 0x00, 0x28,
        0x12, 0x34, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00
    };

    dump_bytes(packet, sizeof(packet));
    return 0;
}
```

ポイント：

- `%02x` は「2桁の16進数で表示。不足分は0で埋める」
- `size_t` は配列サイズや長さを扱う型
- `sizeof(packet)` は配列全体のバイト数

表示例：

```text
45 00 00 28 12 34 40 00 40 06 00 00
```

### 1バイトの中のビットを読む

IPv4ヘッダーの先頭1バイトは、次の2つの値に分かれています。

- 上位4ビット：IPバージョン
- 下位4ビット：IHL（IPv4ヘッダー長）

例えば `0x45` は2進数で見ると次のようになります。

```text
0x45 = 0100 0101
       ^^^^ ^^^^
       |    |
       |    +-- IHL = 5
       +------- version = 4
```

Cでは、シフト演算とマスクを使って取り出します。

```c
/* file: ipv4_first_byte.c */
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    uint8_t first = 0x45;

    uint8_t version = first >> 4;
    uint8_t ihl = first & 0x0f;

    printf("version=%u\n", version);
    printf("ihl=%u\n", ihl);
    printf("header length=%u bytes\n", ihl * 4);

    return 0;
}
```

ポイント：

- `first >> 4` で上位4ビットを右へずらす
- `first & 0x0f` で下位4ビットだけ残す
- IPv4のIHLは「4バイト単位」なので、実際のヘッダー長は `ihl * 4` バイト

ここで大事なのは、`0x45` という1バイトの中に **2つの情報が同居している** ことです。

```text
0x45 = 0100 0101
       ---- ----
       |    |
       |    +-- 下位4ビット：IHL
       +------- 上位4ビット：version
```

コンピュータにとって `0x45` は、最初はただの1バイトの数値です。
そのまま10進数で読むと69です。
しかしIPv4ヘッダーの約束では、この1バイトを次のように分けて読むことになっています。

- 上位4ビットは `version`
- 下位4ビットは `IHL`

そのため、Cのコードでは「欲しい場所だけを取り出す」処理が必要になります。

### なぜ `&` するのか

`&` はビット単位ANDです。
両方のビットが1の場所だけ、結果も1になります。

```text
    0100 0101   first = 0x45
& 0000 1111   0x0f
------------
    0000 0101   結果 = 5
```

`0x0f` は2進数で `0000 1111` です。
つまり、上位4ビットは全部0、下位4ビットは全部1です。

ANDの性質は次のように考えると分かりやすいです。

- `1 & 1` は `1`
- `1 & 0` は `0`
- `0 & 1` は `0`
- `0 & 0` は `0`

つまり、`& 0x0f` はこういう意味になります。

- 上位4ビットには `0000` を当てるので、強制的に消える
- 下位4ビットには `1111` を当てるので、元の値がそのまま残る

このように、特定のビットだけを残すための値を **マスク** と呼びます。
`0x0f` は「下位4ビットだけ残すマスク」です。

もし `first & 0x0f` をしないで `first` をそのままIHLとして読んでしまうと、IHLは5ではなく69になってしまいます。
するとヘッダー長も `69 * 4 = 276` バイトというあり得ない値になります。
だから、IPv4ヘッダーの先頭バイトを読むときは、`version` と `IHL` を分離する必要があります。

逆に、`version` は上位4ビットに入っています。
そのため `first >> 4` で右へ4ビットずらします。

```text
0100 0101  >> 4
0000 0100
```

これで `version = 4` だけが取り出せます。

---

## 第三章　ネットワークバイトオーダー

### 2バイト以上の数値は並び順に注意する

TCP/IPでは、2バイトや4バイトの整数がよく出ます。

例：IPv4ヘッダーの Total Length は2バイトです。

```text
00 28
```

これは10進数で40です。

```text
0x0028 = 40
```

TCP/IPの世界では、複数バイトの整数は **ネットワークバイトオーダー** で送られます。
これはビッグエンディアン、つまり大きい桁のバイトが先に来る並びです。

```text
00 28
|  |
|  +-- 下位バイト
+----- 上位バイト
```

### なぜビッグエンディアンにするのか

ネットワークでは、いろいろなCPUやOSの機械が通信します。
ところが、CPUによってメモリ上の整数の並べ方が違うことがあります。

代表的には次の2種類があります。

- ビッグエンディアン：大きい桁のバイトから並べる
- リトルエンディアン：小さい桁のバイトから並べる

例えば、16進数の `0x1234` は2バイトの数値です。
人間が普通に読むと、左の `12` が大きい桁、右の `34` が小さい桁です。

```text
0x1234
    ^^  ^^
    |   |
    |   +-- 下位バイト
    +------ 上位バイト
```

ビッグエンディアンでは、バイト列は次のようになります。

```text
12 34
```

リトルエンディアンでは、メモリ上では次のように逆に並ぶことがあります。

```text
34 12
```

もしネットワーク上で「各コンピュータの好きな並びで送ってよい」としてしまうと、受け取った側は困ります。
同じ2バイト列でも、どちらの並びとして読むかで値が変わるからです。

```text
12 34 をビッグエンディアンとして読む   -> 0x1234
12 34 をリトルエンディアンとして読む   -> 0x3412
```

そこでTCP/IPでは、ネットワーク上を流れる複数バイト整数の並びを **ビッグエンディアンに統一** しています。
これがネットワークバイトオーダーです。

意味は単純です。

- 送る側は、ネットワークに出す前にビッグエンディアンの形にする
- 受け取る側は、ネットワークから来たバイト列をビッグエンディアンとして読む
- 自分のCPU内部でどう並べるかは、その後でホスト側の都合に直す

この共通ルールがあるので、リトルエンディアンのPCとビッグエンディアンの機械が通信しても、TCP/IPヘッダーの数値を同じ意味で解釈できます。

なお、「ビッグエンディアンの方がCPUとして優れている」という意味ではありません。
TCP/IPで大切なのは、どちらが優秀かではなく、通信相手同士が同じ約束で読むことです。
ネットワークバイトオーダーは、そのための共通語です。

### 自分で2バイト整数を読む

固定バイト列から2バイト整数を読むだけなら、次のように書けます。

```c
static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
```

`be` は Big Endian の略としてよく使われます。

分解すると、次の意味です。

```text
p[0] = 0x00
p[1] = 0x28

(p[0] << 8) = 0x0000
p[1]        = 0x0028
OR結果       = 0x0028
```

ここでやっていることは、「2つの1バイトを、1つの16ビット整数に組み立てる」ことです。

`p[0]` は上位バイトです。
上位バイトは16ビット整数の上半分に置きたいので、左へ8ビットずらします。

```text
p[0] = 0x00

0000 0000            元の1バイト
0000 0000 0000 0000  16ビットに広げた状態
```

今回の `0x00` では分かりにくいので、`0x12 0x34` の例でも見てみます。

```text
p[0] = 0x12 = 0001 0010
p[1] = 0x34 = 0011 0100
```

ビッグエンディアンでは、`p[0]` が上位バイトです。
だから `p[0]` を左へ8ビットずらします。

```text
(uint16_t)p[0] << 8

0001 0010          0x12
0001 0010 0000 0000 0x1200
```

次に、下位バイトである `p[1]` をORで合体します。

```text
    0001 0010 0000 0000   0x1200
| 0000 0000 0011 0100   0x0034
---------------------
    0001 0010 0011 0100   0x1234
```

ここで使っている `|` はビット単位ORです。
どちらか片方でも1なら、結果は1になります。
上位バイトと下位バイトは置かれる場所が重ならないので、ORで自然に合体できます。

`(uint16_t)p[0]` とキャストしているのは、左シフトする前に16ビット以上の幅で計算させたいからです。
`uint8_t` のままでは、環境や整数昇格の理解が絡んで読みづらくなります。
「上位バイトを16ビット整数の上半分に置く」と明示するためにも、先に `uint16_t` にしています。

4バイト整数なら次のようになります。

```c
static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}
```

### `ntohs` と `ntohl`

LinuxのソケットAPIでは、ネットワークバイトオーダーをホスト側の整数に直す関数があります。

```c
#include <arpa/inet.h>

uint16_t host_value = ntohs(network_value);
uint32_t host_addr = ntohl(network_addr);
```

名前の意味：

- `ntohs`：network to host short
- `ntohl`：network to host long
- `htons`：host to network short
- `htonl`：host to network long

ただし、固定の `uint8_t` 配列を解析する練習では、まず `read_be16()` のように自分で読むと理解しやすいです。

---

## 第四章　IPv4ヘッダーを読む

### IPv4ヘッダーの基本形

IPv4ヘッダーは、通常20バイトです。
オプションがある場合は20バイトより長くなります。

よく見るフィールド：

- Version：IPv4なら4
- IHL：IPv4ヘッダー長
- Total Length：IPパケット全体の長さ
- TTL：ルーターを通過できる残り回数
- Protocol：上位プロトコル。TCPは6、UDPは17、ICMPは1
- Source Address：送信元IPv4アドレス
- Destination Address：宛先IPv4アドレス

### IPv4だけを解析するサンプル

```c
/* file: parse_ipv4.c */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void print_ipv4_addr(const uint8_t *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static void parse_ipv4(const uint8_t *packet, size_t len)
{
    uint8_t version;
    uint8_t ihl;
    size_t header_len;
    uint16_t total_len;
    uint8_t ttl;
    uint8_t protocol;

    if (len < 20) {
        printf("IPv4 header is too short: %zu bytes\n", len);
        return;
    }

    version = packet[0] >> 4;
    ihl = packet[0] & 0x0f;
    header_len = (size_t)ihl * 4;

    if (version != 4) {
        printf("not IPv4: version=%u\n", version);
        return;
    }

    if (header_len < 20) {
        printf("invalid IPv4 header length: %zu bytes\n", header_len);
        return;
    }

    if (len < header_len) {
        printf("packet is shorter than IPv4 header: len=%zu header=%zu\n", len, header_len);
        return;
    }

    total_len = read_be16(&packet[2]);
    ttl = packet[8];
    protocol = packet[9];

    printf("IPv4\n");
    printf("  header length : %zu bytes\n", header_len);
    printf("  total length  : %u bytes\n", total_len);
    printf("  ttl           : %u\n", ttl);
    printf("  protocol      : %u\n", protocol);

    printf("  src           : ");
    print_ipv4_addr(&packet[12]);
    printf("\n");

    printf("  dst           : ");
    print_ipv4_addr(&packet[16]);
    printf("\n");
}

int main(void)
{
    uint8_t packet[] = {
        0x45, 0x00, 0x00, 0x28,
        0x12, 0x34, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x0a,
        0xc0, 0xa8, 0x01, 0x14,
        0x30, 0x39, 0x00, 0x50,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02, 0x72, 0x10,
        0x00, 0x00, 0x00, 0x00
    };

    parse_ipv4(packet, sizeof(packet));
    return 0;
}
```

このサンプルの `protocol` は6なので、IPv4の中身はTCPです。

重要なのは、いきなり `packet[12]` などを読む前に長さを確認していることです。
通信解析プログラムでは、壊れたパケット、短いパケット、途中で切れたキャプチャを読むことがあります。
そのため「読んでよい長さか」を毎回確認します。

---

## 第五章　TCPヘッダーを読む

### IPv4ヘッダーの後ろにTCPヘッダーがある

IPv4の `Protocol` が6なら、IPv4ヘッダーの直後にTCPヘッダーがあります。

```text
+-------------+-------------+----------------+
| IPv4 header | TCP header  | TCP payload    |
+-------------+-------------+----------------+
```

ただしIPv4ヘッダー長は必ず20バイトとは限りません。
IHLを読んで、そこからTCPヘッダーの開始位置を決めます。

```c
const uint8_t *tcp = packet + ip_header_len;
size_t tcp_len = packet_len - ip_header_len;
```

### TCPでよく見るフィールド

- Source Port：送信元ポート
- Destination Port：宛先ポート
- Sequence Number：シーケンス番号
- Acknowledgment Number：確認応答番号
- Data Offset：TCPヘッダー長
- Flags：SYN、ACK、FIN、RSTなど
- Window Size：受信可能なサイズ

### TCP解析サンプル

```c
/* file: parse_ipv4_tcp.c */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define IPPROTO_TCP_VALUE 6

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void print_ipv4_addr(const uint8_t *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static void print_tcp_flags(uint8_t flags)
{
    if ((flags & 0x01) != 0) printf(" FIN");
    if ((flags & 0x02) != 0) printf(" SYN");
    if ((flags & 0x04) != 0) printf(" RST");
    if ((flags & 0x08) != 0) printf(" PSH");
    if ((flags & 0x10) != 0) printf(" ACK");
    if ((flags & 0x20) != 0) printf(" URG");
    if ((flags & 0x40) != 0) printf(" ECE");
    if ((flags & 0x80) != 0) printf(" CWR");
}

static void parse_tcp(const uint8_t *tcp, size_t len)
{
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset;
    size_t tcp_header_len;
    uint8_t flags;

    if (len < 20) {
        printf("TCP header is too short: %zu bytes\n", len);
        return;
    }

    src_port = read_be16(&tcp[0]);
    dst_port = read_be16(&tcp[2]);
    seq = read_be32(&tcp[4]);
    ack = read_be32(&tcp[8]);

    data_offset = tcp[12] >> 4;
    tcp_header_len = (size_t)data_offset * 4;
    flags = tcp[13];

    if (tcp_header_len < 20) {
        printf("invalid TCP header length: %zu bytes\n", tcp_header_len);
        return;
    }

    if (len < tcp_header_len) {
        printf("packet is shorter than TCP header: len=%zu header=%zu\n", len, tcp_header_len);
        return;
    }

    printf("TCP\n");
    printf("  src port      : %u\n", src_port);
    printf("  dst port      : %u\n", dst_port);
    printf("  seq           : %u\n", seq);
    printf("  ack           : %u\n", ack);
    printf("  header length : %zu bytes\n", tcp_header_len);
    printf("  flags         : 0x%02x", flags);
    print_tcp_flags(flags);
    printf("\n");
    printf("  payload length: %zu bytes\n", len - tcp_header_len);
}

static void parse_ipv4(const uint8_t *packet, size_t len)
{
    uint8_t version;
    uint8_t ihl;
    size_t ip_header_len;
    uint16_t total_len;
    uint8_t protocol;

    if (len < 20) {
        printf("IPv4 header is too short: %zu bytes\n", len);
        return;
    }

    version = packet[0] >> 4;
    ihl = packet[0] & 0x0f;
    ip_header_len = (size_t)ihl * 4;

    if (version != 4) {
        printf("not IPv4: version=%u\n", version);
        return;
    }

    if (ip_header_len < 20 || len < ip_header_len) {
        printf("invalid IPv4 header length\n");
        return;
    }

    total_len = read_be16(&packet[2]);
    protocol = packet[9];

    printf("IPv4\n");
    printf("  total length  : %u bytes\n", total_len);

    printf("  src           : ");
    print_ipv4_addr(&packet[12]);
    printf("\n");

    printf("  dst           : ");
    print_ipv4_addr(&packet[16]);
    printf("\n");

    if (protocol == IPPROTO_TCP_VALUE) {
        parse_tcp(packet + ip_header_len, len - ip_header_len);
    } else {
        printf("unsupported protocol: %u\n", protocol);
    }
}

int main(void)
{
    uint8_t packet[] = {
        0x45, 0x00, 0x00, 0x28,
        0x12, 0x34, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x0a,
        0xc0, 0xa8, 0x01, 0x14,
        0x30, 0x39, 0x00, 0x50,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02, 0x72, 0x10,
        0x00, 0x00, 0x00, 0x00
    };

    parse_ipv4(packet, sizeof(packet));
    return 0;
}
```

このパケットのTCP部分は、送信元ポート `12345` から宛先ポート `80` への SYN パケットとして読めます。

```text
30 39 = 0x3039 = 12345
00 50 = 0x0050 = 80
```

`flags` が `0x02` なので SYN です。

ここでも `&` が出てきます。
TCPのFlagsも、1バイトの中に複数のON/OFF情報が入っています。

```text
0x02 = 0000 0010
       7654 3210  ← ビット番号
            ^
            +-- 右から2番目、つまりbit 1がSYN
```

`flags & 0x02` は、「SYNの場所だけを残して、ほかのビットを消す」という意味です。

```text
    0000 0010   flags
& 0000 0010   SYNを調べるマスク
------------
    0000 0010   0ではないので、SYNが立っている
```

もしSYNが立っていない場合は、結果が0になります。

```text
    0001 0000   ACKだけが立っている例
& 0000 0010   SYNを調べるマスク
------------
    0000 0000   0なので、SYNは立っていない
```

だからコードでは次のように書いています。

```c
if ((flags & 0x02) != 0) printf(" SYN");
```

`&` は「足し算」ではなく、「指定したビットが立っているか調べる道具」として使っています。
TCPフラグのように、1バイトの中に複数の意味を詰め込む形式では、とてもよく使う書き方です。

---

## 第六章　UDPヘッダーを読む

UDPはTCPよりヘッダーが短く、固定で8バイトです。

UDPヘッダー：

- Source Port：2バイト
- Destination Port：2バイト
- Length：2バイト
- Checksum：2バイト

```c
/* file: parse_udp.c */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void parse_udp(const uint8_t *udp, size_t len)
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t udp_len;
    uint16_t checksum;

    if (len < 8) {
        printf("UDP header is too short: %zu bytes\n", len);
        return;
    }

    src_port = read_be16(&udp[0]);
    dst_port = read_be16(&udp[2]);
    udp_len = read_be16(&udp[4]);
    checksum = read_be16(&udp[6]);

    printf("UDP\n");
    printf("  src port      : %u\n", src_port);
    printf("  dst port      : %u\n", dst_port);
    printf("  udp length    : %u bytes\n", udp_len);
    printf("  checksum      : 0x%04x\n", checksum);

    if (udp_len < 8) {
        printf("  warning       : invalid UDP length\n");
        return;
    }

    if (len < udp_len) {
        printf("  warning       : captured data is shorter than UDP length\n");
        return;
    }

    printf("  payload length: %u bytes\n", (unsigned)(udp_len - 8));
}

int main(void)
{
    uint8_t udp[] = {
        0xd9, 0x03,
        0x00, 0x35,
        0x00, 0x0c,
        0x00, 0x00,
        0x12, 0x34, 0x56, 0x78
    };

    parse_udp(udp, sizeof(udp));
    return 0;
}
```

宛先ポート `53` はDNSでよく使われます。
UDPの解析では、TCPのようなSYN/ACKなどの接続状態は見ません。
その代わり、DNS、NTP、DHCPなど、UDPの上に乗っているアプリケーションプロトコルを見ることが多くなります。

---

## 第七章　ICMPを読む

`ping` で使われる代表的なプロトコルがICMPです。

IPv4ヘッダーの `Protocol` が1ならICMPです。

ICMP Echo Request / Echo Reply の先頭は次のようになっています。

- Type：1バイト
- Code：1バイト
- Checksum：2バイト
- Identifier：2バイト
- Sequence Number：2バイト

```c
/* file: parse_icmp.c */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void parse_icmp(const uint8_t *icmp, size_t len)
{
    uint8_t type;
    uint8_t code;
    uint16_t checksum;

    if (len < 4) {
        printf("ICMP header is too short: %zu bytes\n", len);
        return;
    }

    type = icmp[0];
    code = icmp[1];
    checksum = read_be16(&icmp[2]);

    printf("ICMP\n");
    printf("  type     : %u\n", type);
    printf("  code     : %u\n", code);
    printf("  checksum : 0x%04x\n", checksum);

    if ((type == 0 || type == 8) && len >= 8) {
        printf("  id       : %u\n", read_be16(&icmp[4]));
        printf("  sequence : %u\n", read_be16(&icmp[6]));
    }
}

int main(void)
{
    uint8_t icmp[] = {
        0x08, 0x00,
        0x00, 0x00,
        0x12, 0x34,
        0x00, 0x01
    };

    parse_icmp(icmp, sizeof(icmp));
    return 0;
}
```

ICMP Type の代表例：

- `0`：Echo Reply
- `3`：Destination Unreachable
- `8`：Echo Request
- `11`：Time Exceeded

`ping` の往復を見るときは、Echo Request と Echo Reply を見ることになります。

---

## 第八章　`struct` で読む場合の注意点

### ヘッダー構造体を作りたくなる

IPv4ヘッダーを見ていると、次のような構造体を作りたくなります。

```c
struct ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t src[4];
    uint8_t dst[4];
};
```

これ自体は悪くありません。
ただし、通信データをそのまま構造体ポインタへ変換する書き方には注意が必要です。

危険な例：

```c
const struct ipv4_header *ip = (const struct ipv4_header *)packet;
printf("%u\n", ip->total_length);
```

問題点：

- 構造体のメンバの間にパディングが入る可能性がある
- 2バイト以上の整数はネットワークバイトオーダーのまま
- アライメントが合わないアドレスから読むと環境によって問題になる
- パケット長が構造体サイズより短い場合に範囲外を読む

学習段階では、まず `packet[0]` や `read_be16(&packet[2])` のように、位置を明示して読むほうが安全で理解しやすいです。

### `memcpy` してから `ntohs` する方法

どうしても構造体を使いたい場合でも、範囲確認をしたうえで `memcpy` し、複数バイト整数は変換します。

```c
#include <string.h>
#include <arpa/inet.h>

struct ipv4_header ip;

if (len >= sizeof(ip)) {
    memcpy(&ip, packet, sizeof(ip));
    printf("total_length=%u\n", ntohs(ip.total_length));
}
```

ただしIPv4ヘッダーはオプションにより長さが変わるため、構造体だけで完全に読めるわけではありません。
「固定部分を構造体で読む」くらいに考えるのが現実的です。

---

## 第九章　解析プログラムの基本方針

### 解析は段階的に行う

TCP/IPパケットは、下の層から順に読んでいくと混乱しにくいです。

```text
Ethernet
  -> IPv4 / IPv6 / ARP
       -> TCP / UDP / ICMP
            -> HTTP / DNS / SSH / TLS ...
```

たとえばEthernetフレームから読む場合は、EtherTypeを見ます。

- `0x0800`：IPv4
- `0x0806`：ARP
- `0x86dd`：IPv6

IPv4なら `Protocol` を見ます。

- `1`：ICMP
- `6`：TCP
- `17`：UDP

TCP/UDPならポート番号を見ます。

- `22`：SSH
- `53`：DNS
- `80`：HTTP
- `443`：HTTPS

このように、上位プロトコルへ進むための番号を順番に確認します。

### 必ず長さを確認する

通信解析で最も大事な癖は、読む前に長さを見ることです。

悪い例：

```c
uint16_t port = read_be16(&tcp[2]);
```

このコードは、`tcp` に最低4バイトあることを前提にしています。
しかし、壊れたデータや短いキャプチャでは4バイトないかもしれません。

よい例：

```c
if (tcp_len < 4) {
    return;
}

uint16_t port = read_be16(&tcp[2]);
```

解析対象が外部から来るデータである以上、信用しすぎないのが基本です。

---

## 第十章　tcpdumpの16進出力をCで読む考え方

`tcpdump` では、パケットの中身を16進数で確認できます。

例：

```bash
sudo tcpdump -i enp1s0 -nn -xx tcp port 80
```

注意：

- 実インターフェース名は環境により異なる
- `sudo` が必要な場合が多い
- 本番環境や他人の通信を勝手に取得しない

`-xx` を付けると、リンク層ヘッダーを含む16進ダンプが表示されます。
Ethernetヘッダーは通常14バイトなので、IPv4ヘッダーはその後ろから始まります。

```text
Ethernet header: 14 bytes
IPv4 header    : packet[14] から始まる
```

つまり、Ethernetフレーム全体を `frame` として受け取った場合は次のように進めます。

```c
if (frame_len < 14) {
    return;
}

uint16_t ethertype = read_be16(&frame[12]);

if (ethertype == 0x0800) {
    parse_ipv4(frame + 14, frame_len - 14);
}
```

ここまで理解できれば、`tcpdump` の16進表示、Cの配列、プロトコル仕様がつながります。

---

## まとめ

TCP/IP通信解析のためのC言語では、派手なAPIよりも先に、次の基本が重要です。

- パケットはまずバイト列として扱う
- 1バイトは `uint8_t` または `unsigned char` で読む
- 16進数表示に慣れる
- ビット演算でフィールドを取り出す
- 2バイト以上の整数はネットワークバイトオーダーを意識する
- IPv4のIHL、TCPのData Offsetのような「ヘッダー長」を必ず読む
- `struct` で直接キャストする前に、パディング、エンディアン、アライメント、長さ不足を疑う
- 外部から来たデータは信用せず、読む前に長さを確認する

この土台ができると、次は実際のキャプチャデータ、raw socket、libpcap、pcapファイル解析へ進めます。
ただし、どの方法を使っても基本は同じです。

**バイト列を受け取り、仕様に従って、範囲を確認しながら読む。**

これがC言語によるTCP/IP通信解析の出発点です。