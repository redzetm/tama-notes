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

- 原文対応: 8.1「ページフレームの管理」、8.1.1「ページディスクリプタ」、8.1.2「不均等メモリアクセスアーキテクチャ（NUMA）」、8.1.3「メモリゾーン」、8.1.4「空きページフレームの予約」、8.1.5「ゾーンごとのページフレームアロケータ」、8.1.6「高位メモリのページフレームのカーネルマッピング」、8.1.7「バディシステム」、8.1.7.1〜8.1.7.3
- 今回の主題: `alloc_pages()`系API、高位メモリmapping、buddy allocatorのorder別free area、block split/merge、migratetype
- 現在の進捗: 8.1〜8.1.7.3を作成済み。8.1.8以降は未着手

## 第8章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 8.1、8.1.1 | ページフレーム、`struct page` | 作成済み |
| 8.1.2〜8.1.3 | NUMA、メモリゾーン | 作成済み |
| 8.1.4 | 空きページのwatermarkとGFP allocation context | 作成済み |
| 8.1.5 | ページフレームの割り当て・解放API | 作成済み |
| 8.1.6 | HIGHMEMとkernel temporary mapping | 作成済み |
| 8.1.7、8.1.7.1〜8.1.7.3 | buddy systemの構造、割り当て、解放 | 作成済み |
| 8.1.8〜8.1.9 | per-CPU page cache、zone allocator | 未着手 |
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

### 8.1.5 ゾーンごとのページフレームアロケータ

Linuxのページアロケータは、ページ単位の要求に対して、GFP flagsで表された制約と割り当て文脈を使い、適合するzone・nodeからページを探します。要求は「どのzoneがよいか」だけでなく、「何ページ必要か」「物理的に連続している必要があるか」「sleepや回収が許されるか」も含みます。

```text
alloc_pages(GFP flags, order)
        |
        v
要求に合うzonelist / node / zoneを決める
        |
        v
fast path: watermarkを確認し、per-CPU page list等から確保
        |
        +-- 成功 --> struct page * を返す
        |
        +-- 不足 --> GFP条件が許す範囲で回収・compaction等を試す
                         |
                         +-- 成功 --> struct page * を返す
                         +-- 失敗 --> NULL
```

実際の経路は上図より複雑で、割り当てのorder、cpuset、NUMA memory policy、watermark、fragmentation、GFP modifiersなどによって分岐します。per-CPU page listとbuddy allocatorの役割は8.1.7〜8.1.8で、watermarkと回収の関係は8.1.4および後続章で扱います。

#### `order`が表す連続ページ数

`alloc_pages(gfp, order)`は、`2^order`個の連続したベースページから成るblockを要求します。成功時に返るのは先頭ページの`struct page *`で、失敗時は`NULL`です。`alloc_page(gfp)`は`alloc_pages(gfp, 0)`に相当し、1ページを要求します。

高いorderほど大きな物理連続領域が必要になり、空きページの総量が十分でもfragmentationのため確保できないことがあります。単に大きなメモリ領域が必要なだけで物理連続性が要らない場合、`vmalloc()`や`kvmalloc()`など別のAPIが適切か検討します。DMA用途では、物理アドレスを仮定せずDMA APIを使います。

| API | 成功時の戻り値 | 物理的な連続性 | 対応する解放 |
|---|---|---|---|
| `alloc_pages(gfp, order)` | 先頭の`struct page *` | `2^order`ページが連続 | `__free_pages(page, order)` |
| `alloc_page(gfp)` | 1ページの`struct page *` | 1ページ | `__free_page(page)` |
| `__get_free_pages(gfp, order)` | 先頭ページのkernel virtual address | `2^order`ページが連続し、返す範囲をkernelから直接参照できる必要がある | `free_pages(addr, order)` |
| `get_zeroed_page(gfp)` | zeroed pageのkernel virtual address | 1ページ | `free_page(addr)` |
| `alloc_pages_exact(size, gfp)` | 指定sizeのkernel virtual address | 要求sizeの範囲を連続して確保 | `free_pages_exact(addr, size)` |

`alloc_pages()`の戻り値は仮想アドレスではありません。ページ内容へアクセスする場合は、ページのzoneやarchitecture上のmapping条件に合うAPIでkernel addressを得ます。特にHIGHMEMがある構成では、`struct page *`を得られても常時使えるkernel virtual addressがあるとは限りません（8.1.6参照）。

#### GFP flagsの現行の読み方

第3版の表にある`__GFP_WAIT`、`__GFP_COLD`、`__GFP_REPEAT`などの名前と組み合わせを、Linux 6.18のAPIとしてそのまま使わないでください。現行のGFP flagsは、zone選択、ページmobility、回収許可、再試行方針などの複数の観点を表し、公開されている組み合わせ名として`GFP_KERNEL`、`GFP_NOWAIT`、`GFP_ATOMIC`などを使います。

たとえば`GFP_KERNEL`は通常のsleep可能なkernel allocationで、直接回収が起きる可能性があります。`GFP_NOWAIT`は呼び出し元での直接回収を避ける要求であり、失敗時の代替経路が必要です。`GFP_ATOMIC`は一部reserveの利用を許す場合がありますが、確保成功は保証しません。DMA制約は古い`GFP_DMA`を安易に選ぶのではなく、deviceのDMA maskに従ってDMA APIでバッファを確保・mapします。

#### ページの確保と解放を対にする

次のカーネル内部の概念例は、order 0で1ページを取得し、同じorderで解放します。通常の関数文脈で`GFP_KERNEL`を使う例です。割り当てが失敗する可能性を必ず処理します。

```c
#include <linux/errno.h>
#include <linux/gfp.h>

static int allocate_one_page(void)
{
        struct page *page;

        page = alloc_page(GFP_KERNEL);
        if (!page)
                return -ENOMEM;

        /* pageの利用処理をここに置く */
        __free_page(page);
        return 0;
}
```

複数ページを`alloc_pages(gfp, order)`で確保した場合は、確保時と同じorderを使って解放します。ページを別のsubsystemへ渡した場合は、そのsubsystemの所有権・参照管理規約に従い、呼び出し側が直ちに解放してはいけません。

#### 原文の関数一覧を現行APIへ読み替える

| 第3版に登場する考え方 | Linux 6.18での読み替え |
|---|---|
| ページ記述子を返す`alloc_pages()` / `alloc_page()` | 現在もページアロケータの基本API。folio APIや用途別allocatorも併せて使われる |
| kernel linear addressを返す`__get_free_pages()` | 現在も存在するが、常時mappingされないページには使えない。戻り値型と解放APIを確認する |
| zeroed pageの取得 | `get_zeroed_page()`または適切なAPIのzero flagを使う |
| `__GFP_WAIT`などの旧フラグ | `GFP_KERNEL`等の現行組み合わせと、回収・sleep条件へ読み替える |
| `__GFP_DMA`でDMA用ページを直接確保 | DMA deviceの制約をDMA APIへ伝える方法を優先する |
| 「zone allocatorがzoneを探す」 | `prepare_alloc_pages()`が要求条件を整理し、`get_page_from_freelist()`やslow pathが候補を試す現行経路として追う |

**Linux 6.18のソースを追う入口**

- `alloc_pages()`、`alloc_page()`、free API: `include/linux/gfp.h`
- GFP flagsの定義・組み合わせ: `include/linux/gfp_types.h`
- 要求条件の準備とfast path: `mm/page_alloc.c` (`prepare_alloc_pages()`、`get_page_from_freelist()`)
- 回収を含むslow path: `mm/page_alloc.c` (`__alloc_pages_slowpath()`)
- allocation APIの使い分け: `Documentation/core-api/memory-allocation.rst`
- DMA buffer: `Documentation/core-api/dma-api.rst`

### 8.1.6 高位メモリのページフレームのカーネルマッピング

HIGHMEMは、物理ページの全てをkernelの恒久的な仮想mappingに置けない構成で、必要なページだけを一時的にkernel address spaceへmappingする仕組みです。これは主に一部の32-bit architectureの制約に関係し、Linux 6.18の全環境に存在する一般的なメモリ領域ではありません。x86-32でのカーネル仮想空間の分割や、896 MiBといった境界は特定構成の歴史的な例であり、現在の全architectureへ適用できません。

```text
物理ページ
    |
    +-- permanent/direct mappingがあるページ
    |       kernel addressから継続してアクセス可能
    |
    +-- HIGHMEM page（CONFIG_HIGHMEM等の構成時）
            |
            +-- kmap_local_page()で短時間mapping
                    |
                    +-- アクセス終了後にkunmap_local()
```

HIGHMEMのページでも`struct page`を通じた管理は可能ですが、ページ内容を指すkernel virtual addressが常に存在するわけではありません。そのため、ページ記述子を返す`alloc_pages()`と、kernel virtual addressを返す`__get_free_pages()`は区別します。後者は返すべき直接参照可能なアドレスが必要なので、HIGHMEM pageを使う方法にはなりません。

#### 短期アクセスには`kmap_local_page()`

Linux 6.18では、ページを短時間だけkernelから参照する場合、`kmap_local_page()`またはfolioの一部を扱う`kmap_local_folio()`を使います。mappingしたアドレスは、同じ実行context内で利用し、使い終えたら対応する`kunmap_local()`へ渡します。

```c
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/string.h>

static int copy_page_prefix(struct page *page, void *dst, size_t len)
{
        void *addr;

        if (!dst || len > PAGE_SIZE)
                return -EINVAL;

        addr = kmap_local_page(page);
        memcpy(dst, addr, len);
        kunmap_local(addr);
        return 0;
}
```

この例は1ページの先頭から指定範囲を読み出し、mappingを得ている間だけページ内容に触れます。`page`の所有権・参照を呼び出し側が適切に保持していることが前提です。また、`dst`が指す領域を呼び出し側が書き込み可能であることも必要です。

`kmap_local_page()`のmappingはthread-localで、返されたアドレスを別threadへ渡したり、mappingを取得したcontext以外から参照したりしてはいけません。複数のlocal mappingをネストする場合は、stack状に順序を守ってunmapします。mapping中にpreemptionが無効になる、あるいはpage faultが常に無効になると仮定しないでください。これらの制御が別途必要な処理では、明示的に適切な同期・context制御を行います。

`kmap_atomic()`と`kmap()`は既存コードで見かけますが、Linux 6.18のdocumentationではdeprecatedとされ、新しいコードでは`kmap_local_page()`等を優先します。過去の`kmap_atomic()`にはpreemptionやpage faultを制御する副作用があったため、単純置換でその前提を失わないよう、呼び出し元のcontext要件も調べます。

#### 一時mappingと`vmalloc` mappingは別の用途

`kmap_local_page()`は単一ページ等の短期アクセスに適したlocal mappingです。複数ページをまとめた連続kernel virtual rangeが必要な場合は、物理ページが非連続でも仮想的に連続なmappingを作る`vmap()`等が関係します。これは8.3の非連続メモリ領域の話題につながります。どちらも「全物理メモリを恒久的に直接mappingする」仕組みではありません。

| 手段 | 主な用途 | mappingの性質 |
|---|---|---|
| direct/permanent mapping | 常時kernelからアクセスする通常ページ | 既存のkernel mappingを利用 |
| `kmap_local_page()` / `kmap_local_folio()` | ページ内容への短時間アクセス | 取得したthread/context内でのみ有効 |
| `vmap()` | 複数の物理ページを連続kernel virtual rangeへmap | mappingの作成・解放に共有管理が関係する |

64-bit architectureでは全RAMを恒久mappingできる構成が多く、`kmap_local_page()`がdirect mapping addressを返して実際のtemporary mapを必要としない場合があります。それでも、コードをHIGHMEM対応に保つためにAPIを一貫して使うことが有効です。mapping APIがno-opになる可能性は、そのアドレスの寿命やcontext制約まで無くすことを意味しません。

**Linux 6.18のソースを追う入口**

- `kmap_local_page()`、`kmap_local_folio()`、deprecated APIの注記: `include/linux/highmem.h`
- HIGHMEMの概念とmappingの制約: `Documentation/mm/highmem.rst`
- `CONFIG_HIGHMEM`のarchitecture依存: `arch/*/Kconfig`、`include/linux/gfp_types.h`
- 非連続ページ群のvirtual mapping: `mm/vmalloc.c`、`Documentation/core-api/mm-api.rst`

### 8.1.7 バディシステム

buddy allocatorは、zone内の空き物理ページを2のべき乗個のblockとして管理するページアロケータです。連続したページが必要な要求に対して、十分な大きさのfree blockを分割して渡し、解放時には空いている相方blockを段階的に併合します。

この管理は外部断片化を抑え、異なるorderの連続ページ要求を扱いやすくしますが、断片化を完全になくすわけではありません。空きページの合計が大きくても、要求order以上の連続blockがなければ確保できないことがあります。GFP条件が許す場合はmemory compactionなどが高order確保を支援しますが、それでも失敗し得ます。

#### orderとblockの大きさ

ページアロケータの`order`は、blockに含まれるベースページ数を`2^order`で表します。

| order | block内のベースページ数 | blockの大きさ |
|---:|---:|---|
| 0 | 1 | `PAGE_SIZE` |
| 1 | 2 | `2 × PAGE_SIZE` |
| 2 | 4 | `4 × PAGE_SIZE` |
| 3 | 8 | `8 × PAGE_SIZE` |
| k | `2^k` | `2^k × PAGE_SIZE` |

第3版の表にある「11個の固定リスト」「最大1,024ページ」は、その時代・構成における上限を反映しています。Linux 6.18では`MAX_PAGE_ORDER`と`NR_PAGE_ORDERS`によりorder範囲が定まり、architecture configurationで最大orderが変わり得ます。ソース既定値が10の構成ではorder 0〜10の配列ですが、値を全構成共通のABIと見なさないようにします。

#### 8.1.7.1 データ構造

buddyのfree blockはzoneごとの`struct zone`が持つ`free_area[]`で管理されます。配列の各要素は一つのorderを表し、その中にmigratetypeごとのfree listとfree block数があります。空きblockの先頭`struct page`には、buddyとしての状態やorderを識別する情報が設定されます。

```text
struct zone
    |
    +-- free_area[0] -- order 0 lists[移動種別ごと]
    +-- free_area[1] -- order 1 lists[移動種別ごと]
    +-- free_area[2] -- order 2 lists[移動種別ごと]
    +-- ...
    +-- free_area[MAX_PAGE_ORDER]
```

Linux 6.18のmigratetypeには、代表的な`MIGRATE_UNMOVABLE`、`MIGRATE_MOVABLE`、`MIGRATE_RECLAIMABLE`のほか、`MIGRATE_HIGHATOMIC`、構成に応じた`MIGRATE_CMA`や`MIGRATE_ISOLATE`があります。割り当て時のGFP flags等から求めた移動特性を使い、できるだけ異なる寿命・移動性のページを分けておくことで、大きな連続blockを作りやすくします。一部のtype間ではfallbackが許されますが、すべてのfree listを無条件に混ぜるわけではありません。

#### 8.1.7.2 ブロックの割り当て

要求orderと同じorderのfree blockがなければ、buddy allocatorはより大きなorderを探します。大きなblockを見つけると、必要なblockを取り出し、余った半分を小さいorderのfree listへ戻す操作を繰り返します。

```text
order 3: [                 8 pages                 ]
                         split
                         /    \
order 2: [   4 pages   ]       [   4 pages   ]  <- free list
           split
           /    \
order 1: [2 pages] [2 pages]  <- requested block + free buddy
```

たとえばorder 3の8ページblockからorder 1の2ページを要求した場合、まず8ページを4+4へ分け、必要側をさらに2+2へ分けます。要求された2ページを返し、余ったorder 2の4ページとorder 1の2ページはそれぞれ対応するfree listへ戻します。Linux 6.18ではこのような分割処理に`expand()`や`page_del_and_expand()`が関わります。

free listから要求order以上のblockを検索する代表的な入口は`__rmqueue_smallest()`です。実際にはmigratetype、fallback policy、pageblockの状態や割り当て制約も確認されます。

#### 8.1.7.3 ブロックの解放とbuddy併合

blockを解放すると、allocatorは同じzone・同じorderで隣り合うbuddy blockが空いているか調べます。PFNで表すと、候補buddyの位置は概念的に次式で求められます。

```text
buddy_pfn = pfn XOR (1 << order)
```

order番目のbitを反転することで、同じ大きさの相方blockを求めます。相方がfreeで、zone境界やmigratetypeなどの併合条件を満たせば、二つのblockを一つにまとめ、orderを一つ上げてさらに併合可能か調べます。

```text
解放されたorder 0 block
          |
          +-- buddyもfree? -- yes --> order 1へ併合
                                  |
                                  +-- そのbuddyもfree? -- yes --> order 2へ
          |
          +-- 条件を満たさない --> 現orderのfree listへ登録
```

併合は、同じ大きさで隣接して見えるだけでは成立しません。blockがbuddyとして整列していること、同じzoneに属すること、pageblock isolationやCMA等のmigratetype条件を破らないことが必要です。Linux 6.18の`__free_one_page()`はこれらを確認しながらorderを上げ、最終的なfree blockを適切なlistへ登録します。

**読み取り上の要点**

- `free_area[order]`はzoneごとのorder別free block管理を表す
- `order`は要求した連続ページ数の対数で、ページ数は`1 << order`
- 大きなblockは分割され、解放時に条件を満たすbuddy同士が併合される
- 空き総量と高orderの連続blockの有無は別の情報
- migratetypeとcompactionは、将来の連続block確保可能性に関わる
- DMA device向けの物理連続性・addressabilityは、buddyだけで解決せずDMA APIの要件を確認する

**Linux 6.18のソースを追う入口**

- `MAX_PAGE_ORDER`、`NR_PAGE_ORDERS`、`struct free_area`、migratetype: `include/linux/mmzone.h`
- zone内order別free area: `include/linux/mmzone.h` (`struct zone::free_area`)
- free blockの検索・split: `mm/page_alloc.c` (`__rmqueue_smallest()`、`expand()`)
- buddyの探索とblock merge: `mm/page_alloc.c` (`find_buddy_page_pfn()`、`__free_one_page()`)
- fragmentationとcompactionの観測: `/proc/buddyinfo`、`/proc/pagetypeinfo`、`mm/compaction.c`
