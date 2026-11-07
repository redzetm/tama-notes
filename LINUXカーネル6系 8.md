---
title: "LINUXカーネル6系の研究８　メモリ管理"
---

# LINUXカーネル6系の研究８　メモリ管理

このノートでは、Linux 6.18 LTS がカーネル自身の動的メモリをどのように管理するかを、物理ページの管理から小さなオブジェクトの割り当て、非連続領域の利用へとつないで研究します。
『詳解 Linux カーネル 第三版』第8章の節番号を道しるべにしますが、2.6当時のデータ構造やアロケータを現行実装と混同せず、説明の中心をLinux 6.18 LTSに置きます。

第8章は、管理対象の粒度と物理的な連続性を分けて読むと整理しやすくなります。

```text
物理メモリ上のページフレーム
        |
        +-- ページ単位の確保・解放
        |       buddy allocator / per-CPU page lists
        |
        +-- カーネルオブジェクト単位の確保・解放
        |       SLUB などの slab allocator
        |
        +-- 物理的に連続しない領域の仮想アドレスへの割り当て
                vmalloc 系など
```

上の各層は独立しているわけではありません。オブジェクト用アロケータもページフレームを基盤として使い、非連続領域の仮想マッピングにもページ単位の管理が関係します。APIを読むときは、要求しているサイズだけでなく、物理連続性、DMA制約、寿命、割り込み文脈、失敗時の扱いを併せて確認します。

## 今回扱う範囲

- 原文対応: 8.1「ページフレームの管理」、8.1.1「ページディスクリプタ」、8.1.2「不均等メモリアクセスアーキテクチャ（NUMA）」、8.1.3「メモリゾーン」、8.1.4「空きページフレームの予約」
- 今回の主題: ベースページ、`struct page`、NUMAとzone、zone watermark、割り当て文脈とGFP flags
- 現在の進捗: 8.1〜8.1.4を作成済み。8.1.5以降は未着手

## 第8章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 8.1、8.1.1 | ページフレーム、`struct page` | 作成済み |
| 8.1.2〜8.1.3 | NUMA、メモリゾーン | 作成済み |
| 8.1.4 | 空きページのwatermarkとGFP allocation context | 作成済み |
| 8.1.5〜8.1.9 | ページ割り当て、highmem、buddy、per-CPU cache、zone allocator | 未着手 |
| 8.2、8.2.1〜8.2.15 | slab系アロケータとオブジェクト管理 | 未着手 |
| 8.3、8.3.1〜8.3.4 | 非連続メモリ領域の管理 | 未着手 |

## 8.1 ページフレームの管理

物理メモリは、アーキテクチャが定めるページサイズを単位とするページフレームとして扱われます。カーネルがページ単位でメモリを割り当てる処理は、より大きなカーネル領域や多数の小さなオブジェクトを支える基礎になります。

第3版のx86説明では4 KiBを標準単位として扱いますが、Linux 6.18全体に対して「ページサイズは常に4 KiB」と一般化してはいけません。ベースページサイズはアーキテクチャやカーネル構成に依存します。また、huge pageやTHPのような大きな単位もありますが、それらはベースページの管理と区別して考えます。

ページに関する用語を整理します。

| 用語 | 意味 |
|---|---|
| 物理アドレス | 物理メモリ上のバイト単位のアドレス |
| PFN (Page Frame Number) | 物理アドレスをベースページ単位で表した番号 |
| ページフレーム | 物理メモリのページサイズ分の領域 |
| `struct page` | Linuxがページフレーム等の管理対象に関連付けるメタデータ |
| 仮想アドレス | CPUがロード・ストア命令で扱い、ページテーブルを通じて物理アドレスへ変換するアドレス |

概念上、物理アドレスからPFNを得る関係は次のとおりです。

```text
物理アドレス
    |
    |  PAGE_SHIFT 分だけ右シフト
    v
   PFN  --------------------> 対応する struct page
                                   pfn_to_page()
```

`struct page`はページのデータそのものではなく、ページの用途や状態をカーネルが管理するための記述子です。ページデータは物理ページフレームにあり、`struct page`はその管理メタデータとして別に扱われます。

### 8.1.1 ページディスクリプタ

Linux 6.18では、ページフレーム等の管理情報は主に`struct page`で表されます。ただし、第3版にあるような「全ページの記述子が常に一つの`mem_map`配列に並び、その要素が固定メンバを持つ」というモデルは、すべての現行構成に当てはまりません。

カーネルは構成に応じたメモリモデルを使い、PFNから`struct page`への変換方法も異なります。たとえばFLATMEMではPFNを`mem_map`の添字として扱い、SPARSEMEMでは物理メモリをsectionに分けます。SPARSEMEM_VMEMMAPでは、仮想的に連続した`vmemmap`領域を通じてPFNと記述子を対応付けます。物理メモリの穴やデバイスメモリがあるため、任意のPFNを有効な通常RAMとして扱えるわけではありません。

```text
物理メモリのPFN
    |
    +-- メモリモデルが有効性・sectionを判断
    |
    +-- pfn_to_page(pfn)
            |
            v
       対応する struct page
```

カーネルコードでPFNから記述子を得る場合は、PFNが有効であることと、対象のメモリ種別にその操作が適切であることを確認します。`pfn_to_page()`は任意の数値を安全なページ記述子へ変換する検証APIではありません。

実際のコードでは、有効性判定だけでなく、ページの所有権・参照・ロックやメモリ種別に応じたAPIを使います。ページ参照カウントを直接読み書きするのではなく、`get_page()`、`folio_get()`など用途に合ったヘルパーを選びます。`virt_to_page()`も任意の仮想アドレスに使える万能変換ではなく、適用できるアドレス範囲・アーキテクチャ上の条件を確認します。

#### `struct page`の構成を読む

`struct page`のメンバは、カーネル構成やアーキテクチャに依存し、同じ記憶領域をページの種類に応じてunionで共有する箇所があります。したがって、次の表は主要な役割を把握するためのものであり、全メンバを固定レイアウトとして暗記するための一覧ではありません。

| 構成要素 | Linux 6.18での役割 |
|---|---|
| `flags` | ページ状態を表すビット群。ページロック、dirty、LRU状態などは対応するページ／folio helperを通じて調べる |
| ページキャッシュ・匿名ページ用union | `lru`、`mapping`、folio index、`private`などを保持する。フィールドの意味はページの用途に依存する |
| `buddy_list`、`pcp_list`等 | 同じunion領域を空きページのbuddy管理やper-CPU page listが再利用する |
| `_mapcount` / `page_type` | ページテーブルからのマッピング数、またはページ種別などに使われる領域。状態に応じて解釈が変わる |
| `_refcount` | ページの利用参照を管理する。直接アクセスせず、参照管理用APIと規約に従う |
| 条件付きフィールド | memcg、page pool、KMSAN、アーキテクチャ機能など、構成に応じて存在する |

`struct page`の一つの要素が、単純に「一つのユーザープロセスが使うページ」を表すとは限りません。ページキャッシュ、匿名メモリ、compound page/folio、buddy allocatorの空きブロック、デバイスメモリなど、用途に応じて意味が変わります。大きな連続領域は複数のベースページから成り、folioのような抽象化によって複数ページをまとめて操作する経路もあります。

#### ページ状態とフラグ

ページの状態には、ロック中、書き込み中、dirty、LRU管理中、予約済みなどがあります。Linux 6.18では、フラグの名前や配置、利用条件が構成やページ種別に依存します。第3版の表にあるフラグ一覧を現行カーネルの固定仕様として転記するのではなく、`include/linux/page-flags.h`と各helperの利用箇所を追います。

例として、`PG_locked`はページ／folioへの並行アクセスを調整する状態であり、`PG_dirty`や`PG_writeback`は書き込み処理に関係します。LRU管理ではページ単位だけでなくfolio単位の操作も多く使われます。フラグは独立した真偽値の寄せ集めではなく、状態遷移・ロック・参照規約と組み合わせて解釈する必要があります。

#### 空きページと参照カウントを混同しない

第3版の`_count`の説明にある「特定の値なら空きページ」という判定を、Linux 6.18へそのまま持ち込んではいけません。現行の`struct page`には`_refcount`がありますが、これはページ利用の参照を管理するための内部情報であり、buddy allocatorやper-CPU page listsが管理する空き状態を単独で決める汎用フラグではありません。

ページの確保・解放は、割り当て元、参照の所有者、ページ種別、folioの状態などを保ったAPIと手順で行います。参照カウントがゼロになったことと、任意のコードがそのページを直ちに再利用してよいことも同義ではありません。確保経路・解放経路を対にして追い、所有権と寿命を確認します。

#### PFNとページ記述子を結ぶ短いコード例

```c
/*
 * カーネル内部の概念例:
 * pfnが有効かを確かめてから、対応するページ記述子を得る。
 */
#include <linux/mm.h>

static struct page *page_for_valid_pfn(unsigned long pfn)
{
        if (!pfn_valid(pfn))
                return NULL;

        return pfn_to_page(pfn);
}
```

この例は変換の考え方を示すだけで、あらゆるPFNに適用できる汎用APIではありません。デバイスが管理するページ、hotplugの遷移中の範囲、アーキテクチャ固有の物理領域では、追加の条件や専用APIが必要です。現行ソースでは、`pfn_valid()`の実装、メモリモデルごとの変換、呼び出し側のメモリ所有権を合わせて確認します。

#### Linux 6.18のソースを追う入口

- `struct page`と関連型: `include/linux/mm_types.h`
- PFNとページ記述子の変換: `include/asm-generic/memory_model.h`、`include/linux/mm.h`
- ページフラグとhelper: `include/linux/page-flags.h`、`include/linux/page_ref.h`
- FLATMEM/SPARSEMEM/VMEMMAPの概念: `Documentation/mm/memory-model.rst`
- ページフレームの割り当てと解放: `mm/page_alloc.c`

### 8.1.2 不均等メモリアクセスアーキテクチャ（NUMA）

NUMA (Non-Uniform Memory Access) は、CPUとメモリの物理的な位置関係によってアクセス遅延や帯域が変わる構成です。Linuxはハードウェアの資源配置をnodeとして表し、各nodeにCPU、メモリ、またはその両方を関連付けます。node間の距離は一律ではなく、CPUから見て近いメモリと遠いメモリがあります。

```text
                 CPU 0                         CPU 1
                   |                              |
             local access                   local access
                   v                              v
          +------------------+  interconnect +------------------+
          | node 0           |<------------->| node 1           |
          | CPU 0 + memory   |                | CPU 1 + memory   |
          +------------------+                +------------------+
              remote access from either CPU may cost more
```

Linuxのnodeは物理的なsocketやメモリバンクと必ず一対一になるわけではありません。アーキテクチャによっては、メモリを持たないhardware cellのCPUを別nodeへ関連付けたり、NUMA emulationで一つの物理メモリ領域を複数nodeとして見せたりします。したがって、node IDだけから物理配線や実際のアクセス時間を断定せず、firmware情報、`/sys/devices/system/node/`、距離情報などを確認します。

Linuxのメモリ管理では、nodeごとに`struct pglist_data`（typedef名`pg_data_t`）があり、そのnodeのzone群、zonelist、PFN範囲、メモリ量などを記述します。UMA構成でもnodeを一つ持つ形で扱うため、共通のメモリ管理コードはnodeという単位を利用できます。

```text
pg_data_t (node)
    |
    +-- node_zones[]      node内のzone descriptor
    +-- node_zonelists[]  割り当て候補zoneの優先順
    +-- node_start_pfn / node_present_pages
    +-- kswapdなどnode単位の回収情報
```

#### local allocationとfallback

NUMAで通常のページ割り当てを行うとき、Linuxは原則として実行元CPUに近いnodeのメモリを優先します。近いメモリを使えば、割り当て後に同じCPUや近いnode上で処理される場合に、remote accessを減らせます。ただし、メモリを使うtaskが別nodeへ移動する、またはlocal nodeの空きが不足する場合には、期待したlocalityが維持されないことがあります。

割り当て要求はzonelistをたどります。候補の順序はzone制約・node距離・policyなどに関わり、local nodeで確保できない場合には条件を満たす別nodeへfallbackすることがあります。一方、指定node以外からの確保を許さない要求もあります。どの動作になるかはGFP flags、NUMA memory policy、cpuset等を含む呼び出し条件で決まり、「常にlocal」「常に別nodeへfallback」と固定的に理解しないことが重要です。

```text
割り当て要求
    |
    v
要求が許すzone/nodeをzonelistから選ぶ
    |
    +-- local nodeに適合する空きがある --> そのnodeから確保
    |
    +-- local nodeでは不足
            |
            +-- policyが許す候補nodeへfallback
            +-- fallback禁止／候補なしなら失敗や回収処理へ
```

メモリ配置とtask配置は別々の仕組みです。メモリpolicyはページをどのnodeから割り当てるかに関わり、CPU affinityやschedulerはtaskがどのCPUで実行されるかに関わります。taskが移動すれば、そのtaskが以前に確保したページがremote memoryになる場合があります。NUMA localityを調べる際はCPU配置だけでなく、ページ配置とmemory policyも併せて観測します。

NUMAの概念を確認するカーネル内部の例です。これはユーザー空間アプリケーションとして実行するコードではなく、nodeごとの統計にアクセスするカーネルコードの形を示します。

```c
#include <linux/mmzone.h>
#include <linux/numa.h>
#include <linux/printk.h>

static void inspect_online_nodes(void)
{
        int nid;

        for_each_online_node(nid) {
                pg_data_t *pgdat = NODE_DATA(nid);

                pr_info("node %d: present pages=%lu\n",
                        nid, pgdat->node_present_pages);
        }
}
```

実際のカーネルコードでは初期化時期、memory hotplugとの同期、統計値の意味を確認します。`node_present_pages`はnodeに存在するページ数で、予約済み領域やallocatorが管理するページ数と同じ意味ではありません。

**ソースと観測の入口**

- node descriptorとnode iteration: `include/linux/mmzone.h`、`include/linux/numa.h`
- nodeとzoneの設計: `Documentation/mm/physical_memory.rst`
- NUMAのLinux model、zonelistとfallback: `Documentation/mm/numa.rst`
- memory policy: `Documentation/admin-guide/mm/numa_memory_policy.rst`
- zonelist構築とページ割り当て: `mm/page_alloc.c`
- 実行環境のnode情報: `/sys/devices/system/node/`、`numactl --hardware`（導入済みの場合）

### 8.1.3 メモリゾーン

node内の物理メモリは、アドレス範囲や利用上の制約に応じてzoneに分けられます。zoneは単なる「メモリの用途別ラベル」ではありません。DMA可能範囲、kernelが常時直接アクセスできる範囲、移動可能ページとして管理する範囲などの制約を、allocatorが選択に反映するための単位です。

第3版のx86-32説明に出てくる「DMAは先頭16 MiB」「NORMALは896 MiBまで」「それより上はHIGHMEM」という境界は、当時の特定構成に結び付いた値です。Linux 6.18の一般仕様として扱えません。zoneの有無と範囲はarchitecture、kernel config、物理アドレス空間、起動引数、memory hotplug等で変わります。

| zone | Linux 6.18での意味 | 読むときの注意 |
|---|---|---|
| `ZONE_DMA` | ごく限られたDMA addressabilityしか持たないdevice向けの制約メモリ | 存在や範囲はarchitecture/config依存。古いISAの16 MiB境界を一般化しない |
| `ZONE_DMA32` | 32-bit DMA maskで到達可能なメモリが必要な場合のzone | 一部の64-bit architectureで使われる。DMA APIで要件を指定することとzone名を直接選ぶことは別 |
| `ZONE_NORMAL` | kernelが通常のkernel mappingを通じてアクセスできる通常メモリ | 常時有効な基本zone。通常のkernel allocationの中心 |
| `ZONE_HIGHMEM` | kernelのpermanent mappingに全体を含められず、一時mappingが必要なメモリ | 主に一部の32-bit構成に存在。64-bitなら必ず空、と一般化するよりconfig/architectureで判断する |
| `ZONE_MOVABLE` | 主に移動可能なページを集め、memory offliningなどを行いやすくするzone | 全ページが無条件に移動可能とは限らず、pinned page等の制約がある |
| `ZONE_DEVICE` | device memoryに`struct page`等のmemory mapサービスを提供するzone | 通常RAMやbuddy管理の通常zoneと同一視しない |

```text
物理アドレス空間の概念図（zoneの境界は構成依存）

node 0:  +-------------------+-------------------+------------------+
         | DMA / DMA32       | NORMAL            | MOVABLE          |
         +-------------------+-------------------+------------------+
                    node 1:  +-------------------+------------------+
                             | NORMAL            | MOVABLE          |
                             +-------------------+------------------+

別途: HIGHMEM（構成依存） / DEVICE memory（通常RAMとは異なる属性）
```

#### zoneのdescriptorとページ数

各`struct zone`は、開始PFN、範囲、存在するページ数、buddy allocatorが管理するページ数、watermark、per-CPU page listなどを保持します。これらの値は同義ではありません。

| `struct zone`の情報 | 意味 |
|---|---|
| `zone_start_pfn` | zoneの開始位置をPFNで表す |
| `spanned_pages` | zoneが覆うPFN範囲の大きさ。範囲内のmemory holeも含む |
| `present_pages` | zone範囲内に実在する物理ページ数。holeを除く |
| `managed_pages` | page allocatorが管理するページ数。予約や未管理領域等を含めない |
| `_watermark[]` | 割り当て可否や回収判断で用いる空きページのしきい値 |
| `lowmem_reserve[]` | 低位zoneの枯渇を防ぐためのzone間reserve情報 |
| `per_cpu_pageset` | CPUごとのページキャッシュを保持し、共有zone lockへの競合を抑える |
| `free_area[]` | buddy allocatorがorderごとに管理するfree block情報 |
| `zone_pgdat` | zoneが属するnode descriptorへの参照 |

特に、`spanned_pages`と`present_pages`の差はmemory holeを含む構成で意味を持ち、`present_pages`と`managed_pages`の差には予約・allocator管理外のページ等が関係します。したがって、zoneの容量を一つのフィールドだけで代表させず、何を数えているかを読み分けます。

#### `zonelist`がzoneとnodeの候補順を表す

メモリ割り当て要求は、必要なzone上限や割り当て制約を踏まえてzonelistを走査します。`struct zonelist`は単なる全zoneの固定配列ではなく、割り当ての優先候補とfallback候補をたどるための構造です。Linux 6.18では通常のfallback用と、`__GFP_THISNODE`向けのfallbackを制限したzonelistが用意されます。

たとえばDMA制約を満たさない高位zoneのページを、DMA可能であるかのように返すことはできません。逆に、制約が許せば要求元node以外のzoneへfallbackできる場合があります。実際の候補選択はzone index、GFP flags、NUMA policy、cpuset、watermarkや回収状態を含むため、ゾーン名だけで割り当て結果を断定しないようにします。

**Linux 6.18のソースを追う入口**

- zone typeと`struct zone`、`struct zonelist`、`struct pglist_data`: `include/linux/mmzone.h`
- GFP flagsからzone/node候補を選ぶhelper: `include/linux/gfp.h`
- zonelist構築、watermark判定、allocator経路: `mm/page_alloc.c`
- zone/nodeの概念と構成例: `Documentation/mm/physical_memory.rst`
- DMA bufferの確保要件: `Documentation/core-api/dma-api.rst`

### 8.1.4 空きページフレームの予約

ページアロケータは、単に「空きページが1枚でもあれば割り当てる」のではありません。各zoneにwatermark（空きページのしきい値）を設定し、通常割り当て、回収の開始・進行、低メモリ予約などの判断に使います。要求のGFP flags、order、zone/nodeの制約によって判定は変わります。

```text
空きページが多い
    |
    |  WMARK_HIGH  … kswapdが回収を止める目安
    |  WMARK_LOW   … 非同期回収を起こす目安
    |  WMARK_MIN   … zoneの最低watermark。通常割り当ての保護基準
    v
空きページが少ない
```

watermarkは、一定数のページを固定的に専用保管する単純な予約箱ではありません。通常の割り当てがzoneをどこまで使えるか、ページ回収をいつ促すか、どの割り当てがreserveへ近づけるかを制御するしきい値です。Linux 6.18では`WMARK_MIN`、`WMARK_LOW`、`WMARK_HIGH`に加えて、NUMA balancing向けの`WMARK_PROMO`も定義されています。

#### `min_free_kbytes`とzone watermark

`/proc/sys/vm/min_free_kbytes`は、watermark計算の基礎となる値です。起動時のLinux 6.18ではlowmemの量を基に概ね次の式で初期値を計算し、128 KiBから256 MiBの範囲に収めます。

```text
min_free_kbytes ≈ sqrt(lowmem_kbytes × 16)
```

ここでの値は、空きページの固定予約量をそのまま示すものではありません。`mm/page_alloc.c`はzoneごとのmanaged pagesなどを使って最低watermarkを配分し、low/high watermarkとの距離も設定します。HIGHMEMやMOVABLE zoneの最低watermarkには別の上限処理があります。また、`watermark_scale_factor`、`lowmem_reserve_ratio`、watermark boostなども実際の割り当て可能性に影響します。

`min_free_kbytes`を極端に大きくすると、通常の割り当てに使えるメモリが減り、早期の回収やOOMを招くことがあります。小さくし過ぎると、カーネルが必要とする低メモリ領域の余裕を損なう可能性があります。理由なく調整する値ではなく、変更前後のwatermark・回収・割り当て失敗を測定して判断します。

#### GFP flagsは割り当て時の実行条件を伝える

GFP flagsは、どのzoneを使えるかだけでなく、呼び出し元がsleepできるか、直接回収やI/Oを許すか、失敗時にどこまで再試行するかをallocatorへ伝えます。

| 要求の例 | 呼び出し側が示す条件 | 重要な注意 |
|---|---|---|
| `GFP_KERNEL` | sleep可能。直接回収が起きる可能性がある | interrupt contextやspinlock保持中など、sleepできない文脈では使わない |
| `GFP_NOWAIT` | 直接回収でsleepしない割り当て | reserveを通常利用するものではなく、メモリ圧迫時は失敗し得る |
| `GFP_ATOMIC` | sleepできない文脈で、必要に応じて一部のreserveを利用する割り当て | 成功保証ではない。割り当てサイズを小さくし、失敗時の経路を用意する |
| `GFP_NOIO` / `GFP_NOFS` | 回収中の再入やdeadlockを避けるため、I/Oやfilesystem経路を制約する | 必要な範囲に限り、現行のscope APIも含めて設計する |

第3版の「不可分な要求には`GFP_ATOMIC`を使う」という記述は入口としては有用ですが、Linux 6.18でも`GFP_ATOMIC`が成功を保証するわけではありません。通常は直接回収を行わず、状況によってreserveへアクセスできるものの、枯渇時には失敗します。interrupt handlerで大きな領域を都度確保するのではなく、事前確保、pool、deferred workなどが適切か検討します。

```c
#include <linux/errno.h>
#include <linux/slab.h>

struct request {
        unsigned int id;
};

static int create_request(unsigned int id)
{
        struct request *req;

        req = kmalloc(sizeof(*req), GFP_ATOMIC);
        if (!req) {
                /* 実際の呼び出し側では適切な代替経路を選ぶ */
                return -ENOMEM;
        }

        req->id = id;
        kfree(req);
        return 0;
}
```

一方、sleep可能な通常経路で`GFP_KERNEL`を使う場合も、全ての割り当てが必ず成功するとは限らないため、戻り値を検査します。`__GFP_NOFAIL`は安易な代替策ではなく、呼び出し箇所の保証条件やkernel API規約を調べずに付けてはいけません。

**Linux 6.18のソースを追う入口**

- watermarkの定義: `include/linux/mmzone.h` (`enum zone_watermarks`)
- `min_free_kbytes`からwatermarkを設定: `mm/page_alloc.c` (`calculate_min_free_kbytes()`、`setup_per_zone_wmarks()`)
- zone watermarkを使う割り当て判定: `mm/page_alloc.c`
- GFP flagsと割り当て文脈: `include/linux/gfp.h`、`Documentation/core-api/memory-allocation.rst`
- 実行環境の値: `/proc/sys/vm/min_free_kbytes`、`/proc/zoneinfo`
