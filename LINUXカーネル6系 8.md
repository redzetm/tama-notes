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

- 原文対応: 8.1「ページフレームの管理」、8.1.1「ページディスクリプタ」、8.1.2「不均等メモリアクセスアーキテクチャ（NUMA）」、8.1.3「メモリゾーン」、8.1.4「空きページフレームの予約」、8.1.5「ゾーンごとのページフレームアロケータ」、8.1.6「高位メモリのページフレームのカーネルマッピング」、8.1.7「バディシステム」、8.1.7.1〜8.1.7.3、8.1.8「CPUごとのページフレームキャッシュ」、8.1.8.1〜8.1.8.2、8.1.9「ゾーンアロケータ」、8.1.9.1「ページフレームの解放」、8.2「メモリ領域の管理」、8.2.1「スラブアロケータ」、8.2.2「キャッシュディスクリプタ」、8.2.3「スラブディスクリプタ」、8.2.4「汎用キャッシュと特定用途のキャッシュ」、8.2.5「スラブアロケータとゾーンごとのページフレームアロケータ間のインタフェース」、8.2.6「キャッシュへのスラブの割り当て」
- 今回の主題: 空きobjectの探索からCPU partial・node partial・新規slabへ至るSLUB割り当て経路とNUMA/GFP制約
- 現在の進捗: 8.1〜8.2.6を作成済み。8.2.7以降は未着手

## 第8章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 8.1、8.1.1 | ページフレーム、`struct page` | 作成済み |
| 8.1.2〜8.1.3 | NUMA、メモリゾーン | 作成済み |
| 8.1.4 | 空きページのwatermarkとGFP allocation context | 作成済み |
| 8.1.5 | ページフレームの割り当て・解放API | 作成済み |
| 8.1.6 | HIGHMEMとkernel temporary mapping | 作成済み |
| 8.1.7、8.1.7.1〜8.1.7.3 | buddy systemの構造、割り当て、解放 | 作成済み |
| 8.1.8、8.1.8.1〜8.1.8.2 | CPUごとのページキャッシュ、割り当て・解放経路 | 作成済み |
| 8.1.9、8.1.9.1 | ページ割り当て判断とページ解放 | 作成済み |
| 8.2、8.2.1 | 小さな領域の管理とSLUBの基本モデル | 作成済み |
| 8.2.2 | cache descriptorとその管理情報 | 作成済み |
| 8.2.3 | slab descriptorとslab単位の状態 | 作成済み |
| 8.2.4 | 汎用cache・特定用途cacheとcache merge | 作成済み |
| 8.2.5 | slab allocatorとpage allocatorの確保・返却連携 | 作成済み |
| 8.2.6 | cacheへのslab割り当てとSLUBのfallback | 作成済み |
| 8.2.7〜8.2.15 | slab回収、object配置・割り当て・解放、mempool | 未着手 |
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

### 8.1.8 CPUごとのページフレームキャッシュ

Linux 6.18は、頻繁に使われるページの確保・解放でzoneのbuddy free areaを毎回直接操作しなくてよいよう、CPUごとのPer-CPU Pageset (PCP)を持ちます。各zoneには`per_cpu_pageset`があり、現在CPU向けの小さなページリストを介してページを受け渡します。

```text
CPU 0: PCP lists --+
CPU 1: PCP lists --+--> zoneごとのbuddy free areas
CPU 2: PCP lists --+        (zone lockで共有管理)
        ...         |

order-0中心の一般経路:
確保: local PCPから取り出す -> 空ならbuddyからbatch補充
解放: local PCPへ置く       -> 上限を超えた分をbuddyへbatch返却
```

PCPの主な狙いは、zoneの共有lock競合を減らし、よくあるorder-0確保・解放をCPU localに処理することです。これは別の物理メモリ領域や別のallocatorを作る仕組みではなく、zoneのbuddy free areaからページをまとめて受け取り、後でまとめて返す一時的なcacheです。

#### 8.1.8.1 per-CPU pagesetからのページ確保

order-0の割り当てでは、適切なzoneとmigratetypeに対応するPCP listからページを取り出せる場合、buddyのzone lockを毎回取得する経路を避けられます。listが空なら、ページアロケータはbuddyから複数ページをまとめて取り出してPCPへ補充し、そのうち一つを要求元へ渡します。

現行の処理は、第3版に登場する`buffered_rmqueue()`とhot/cold cacheの単純な説明とは異なります。Linux 6.18では`rmqueue_pcplist()`、`__rmqueue_pcplist()`等がper-CPU listからの取り出しを扱い、必要なとき`rmqueue_bulk()`でbuddyからbatch単位の補充を行います。orderや構成によってPCPを使える範囲が異なり、PCPを利用できない要求はbuddy側の経路へ進みます。

PCP listは単純な「CPU cacheにデータが載っていそうなページ／載っていなさそうなページ」の二分管理ではありません。現行構造ではorderとmigratetypeなどに応じたlistが使われます。第3版の`__GFP_COLD`やhot/cold page listを、現行のCPU data-cacheのhotness制御として読み替えないようにします。

#### 8.1.8.2 per-CPU pagesetへのページ解放

解放されたページがPCP対象orderであれば、通常は現在のCPUに対応するPCP listへ追加されます。PCPのページ数が上限付近に達すると、一定量を`free_pcppages_bulk()`等でbuddyへ戻します。batchと上限値は固定の定数だけで決まるわけではなく、zoneの規模や設定、実行時の調整に応じて変化します。

```text
ページ解放
    |
    +-- PCP対象orderか?
    |       |
    |       +-- yes --> CPU-local PCP listへ追加
    |       |                |
    |       |                +-- PCPが上限付近 --> batchでbuddyへdrain
    |       |
    |       +-- no --> buddy allocatorへ直接解放・併合
    |
    v
ページは再利用可能なfree状態として管理される
```

PCPにページを保持すると、buddyの共有lockを取る頻度を抑えられます。一方、ページが各CPUに分散して一時保持されるため、zone全体の統計やbuddyだけを見たときの空き量と即時に一致しない場合があります。CPU hotplug、watermark調整、memory pressureなどではPCPをdrainする処理が関係します。

#### 構造と観測のポイント

`struct per_cpu_pages`は、list群、ページ数、batch、high/low相当の上限管理値などを持ちます。Linux 6.18の実際のメンバ名は`count`、`high`、`high_min`、`high_max`、`batch`、`lists[]`等であり、第3版の`low`/`hot`/`cold`構成と同じレイアウトではありません。

| 観点 | Linux 6.18での読み方 |
|---|---|
| page list | `lists[]`にorder・migratetypeに対応するページが保持される |
| `count` | PCPに現在保持するページ数を表す |
| `batch` | buddyとのページ受け渡し量を決める基準の一つ |
| `high`、`high_min`、`high_max` | PCPの保持量やdrain条件を調整する値 |
| drain | PCPに保持したページをzoneのbuddy free areaへ返す処理 |
| hot/cold | 現行PCP listの基本的な分類としてそのまま存在するわけではない |

**Linux 6.18のソースを追う入口**

- `struct per_cpu_pages`とzoneの`per_cpu_pageset`: `include/linux/mmzone.h`
- PCPからの確保、batch補充、buddy fallback: `mm/page_alloc.c` (`rmqueue_pcplist()`、`__rmqueue_pcplist()`、`rmqueue_bulk()`)
- PCPへの解放とbatch drain: `mm/page_alloc.c` (`free_frozen_page_commit()`、`free_pcppages_bulk()`、`drain_zone_pages()`)
- pagesetの初期化とzoneごとの設定: `mm/page_alloc.c` (`setup_zone_pageset()`、`per_cpu_pages_init()`)
- PCPとbuddyの関係: `Documentation/mm/physical_memory.rst` のZones説明

### 8.1.9 ゾーンアロケータ

zone allocatorは、`alloc_pages()`等のページ要求を受け取り、GFP flags、order、preferred node、zonelist、watermark、ページ移動特性などを使って、適切なページを探します。Linux 6.18の割り当て処理は一回のzone走査だけではなく、まず短いfast pathを試し、要求条件が許す場合にだけ回収・compactionを含むslow pathへ進みます。

```text
alloc_pages(gfp, order)
        |
        v
要求条件を整理
  zone上限 / nodemask / cpuset / NUMA policy / migratetype
        |
        v
fast path: zonelistのzoneを走査
  watermark・予約・fragmentation条件を確認
        |
        +-- PCPまたはbuddyから取得できた --> pageを返す
        |
        +-- 取得できない
                |
                +-- sleep/reclaim不可 --> 利用可能な限定reserveを試す場合がある
                |                         それでも取得できなければNULL
                |
                +-- reclaim可能
                       kswapd wakeup / direct reclaim / compaction
                       条件に応じてretry、OOM処理、またはNULL
```

この図は概念経路です。実際にはNUMA zonelist、cpuset、allocation policyや割り当てのorderにより候補が変わり、回収やcompactionを行わずに失敗する要求もあります。たとえばinterrupt contextではsleepしてdirect reclaimを待つことはできません。逆に`GFP_KERNEL`は通常sleep可能な文脈で使い、メモリ圧迫時にはdirect reclaimが起き得ます。

#### zone選択とwatermark判定

`prepare_alloc_pages()`はGFP flagsからzone上限やmigratetype等を整理し、対象nodeのzonelistとnodemaskを準備します。`get_page_from_freelist()`は候補zoneを走査し、cpuset等の制約を確認してからwatermarkを判定し、条件を満たすzoneでPCPまたはbuddyからblockを取得します。

zoneは「空きページが要求数以上あるか」だけで選ばれません。要求orderの連続blockがあるか、watermarkとlowmem reserveを維持できるか、指定されたzone/node/migratetypeの条件を満たすかが関係します。DMAなど制約の強いzoneを不用意に消費しないように、より高位の適合zoneを優先する候補順も使われます。

| 判定の観点 | allocatorでの意味 |
|---|---|
| zone/node候補 | GFP mask、zonelist、NUMA nodemask、cpuset等で制約される |
| watermark | 割り当て後に最低限の空きとreserveを保てるかを確認する |
| `order` | 必要な連続blockがfree area等に存在するかを確認する |
| migratetype | mobilityやCMA/highatomic等の扱いとfallback可否に関係する |
| 割り当てcontext | sleep、I/O、filesystem、回収・再試行を行えるかを決める |

#### fast pathからslow pathへ

Linux 6.18の通常のページ要求では、最初に`__alloc_pages_noprof()`から割り当ての中心処理へ入り、watermarkを含むfast pathで候補zoneを走査します。そこで失敗した場合、GFP flagsが許可すればslow pathが次の処理を条件に応じて行います。

1. `kswapd`を起こし、node単位のbackground reclaimを促す
2. 高order要求などで有効性がある場合、compactionを試みる
3. direct reclaimが許される場合、呼び出し元のcontextで回収し、再度zoneを走査する
4. 進捗・retry flags・order・cpusetやzonelistの変更を踏まえて、再試行か失敗を決める
5. 必要な条件を満たす場合はOOM処理に進むことがある

この順序は単純な固定ステップ列ではなく、`__GFP_NORETRY`、`__GFP_RETRY_MAYFAIL`、`__GFP_NOFAIL`、compaction可能性、呼び出し文脈などで分岐します。`__GFP_NOFAIL`も無制限にあらゆる要求で利用できる保証ではなく、許容されるorderやsleep/reclaim条件に制約があります。通常の呼び出し側は、どのGFP maskでも返却値を確認し、失敗時の処理を用意します。

#### 8.1.9.1 ページフレームの解放

ページ解放は割り当てより経路が短いものの、解放呼び出しをした時点で常にbuddyへ直行するわけではありません。order-0等のPCP対象ページはper-CPU pagesetへ一時保持され、対象外orderやdrain処理ではbuddy allocatorへ返されます。buddyに返ると、隣接する空きbuddyとの併合が試みられます（8.1.7.3）。

```text
__free_pages(page, order)
        |
        +-- 参照がまだ残る --> 物理ページは解放しない
        |
        +-- 最後の参照を解放
                |
                +-- PCP対象 --> per-CPU listへ。後でbatch drainの場合もある
                |
                +-- PCP対象外/drain --> buddyへ戻し、併合を試す
```

Linux 6.18の`__free_pages()`は、ページ参照の解放を行い、最後の参照が外れた場合にfree pathへ進みます。compound pageと非compoundの高order allocationでは解放の扱いが異なるため、同じorder・所有権規約に対応する解放APIを使います。割り当て時のorderと異なるorderを`__free_pages()`へ渡すと、メモリ破損や警告の原因になり得ます。

```c
/* alloc_pages(GFP_KERNEL, order)で得たblockの解放例 */
static void release_page_block(struct page *page, unsigned int order)
{
        if (page)
                __free_pages(page, order);
}
```

上の例は、呼び出し側が当該blockの所有権を持ち、他の利用者へ参照を渡していない場合に限ります。共有参照を管理する場合は、ページ／folioの参照APIとcompound pageの規約に従います。`__free_pages()`はorderが割り当て時と一致することを一般に検査しないため、orderを正しく保持する設計が必要です。

**Linux 6.18のソースを追う入口**

- `alloc_pages()`からの実装入口: `include/linux/gfp.h`、`mm/page_alloc.c` (`__alloc_pages_noprof()`、`__alloc_frozen_pages_noprof()`)
- 要求条件とzonelist走査: `mm/page_alloc.c` (`prepare_alloc_pages()`、`get_page_from_freelist()`)
- watermark判定: `mm/page_alloc.c` (`zone_watermark_fast()`、`__zone_watermark_ok()`)
- slow path、direct reclaim、compaction、retry/OOM: `mm/page_alloc.c` (`__alloc_pages_slowpath()`、`__alloc_pages_direct_reclaim()`、`__alloc_pages_direct_compact()`)
- 解放・PCP/buddyへの経路: `mm/page_alloc.c` (`__free_pages()`、`___free_pages()`、`__free_frozen_pages()`)
- allocatorの設計説明: `Documentation/core-api/memory-allocation.rst`、`Documentation/mm/physical_memory.rst`

## 8.2 メモリ領域の管理

8.1で扱ったページアロケータは、ページサイズ単位の物理メモリを管理します。しかしカーネル内のデータ構造には、ページよりずっと小さい数十〜数百バイト程度のオブジェクトが多くあります。それぞれにページを割り当てると、オブジェクト本体に使われない領域が大きくなり、ページの内部断片化が生じます。

この粒度の違いを埋めるのがslab allocatorです。ページアロケータから得たページをアロケータ内部で管理し、その中を複数のオブジェクト用スロットに分けます。したがって、slab allocatorはページアロケータを置き換えるものではなく、ページの上に構築されたオブジェクト単位の層です。

```text
ページアロケータ
  物理ページを確保
        |
        v
slab allocator
  同種・同サイズのオブジェクト用領域を管理
        |
        +-- object 0
        +-- object 1
        +-- object 2
        +-- ...
```

### 8.2.1 スラブアロケータ

スラブアロケータの基本的な考え方は、同じ種類のオブジェクトを繰り返し作成・破棄するカーネルの利用形態を、専用のcacheとして扱うことです。cacheから空きobjectを返し、解放されたobjectを同じcacheで再利用することで、個々の小さな要求ごとにページを確保・解放する処理を避けます。

```text
kmem_cache (同種のobjectを管理するcache)
        |
        +-- slab/page群
                |
                +-- object: 使用中
                +-- object: 空き
                +-- object: 使用中
```

この「cache」はCPUのL1/L2 cacheとは異なり、カーネルオブジェクトを供給・再利用するための管理単位です。たとえば構造体型専用のcacheでは、同じサイズ・同じレイアウトのobjectを繰り返し確保できます。別のAPIである`kmalloc()`は、サイズクラスに対応した汎用cache群から要求サイズを満たす領域を返します。

| 層 | Linux 6.18での役割 |
|---|---|
| page allocator | slab用のページを物理ページ単位で供給する |
| slab allocator (SLUB) | ページをobject slot群として管理し、cache単位で再利用する |
| 呼び出し側 | `kmem_cache_alloc()`や`kmalloc()`でobjectを確保し、対応するAPIで解放する |

要求サイズと実際のobject slotのサイズは常に一致するわけではありません。slot管理情報やアラインメント、デバッグ機能などのための領域も必要で、サイズクラスに切り上げられる場合にはその差が内部断片化になります。一方、cacheを再利用することで、同じ種類のobjectを作るたびに初期化やページアロケータ処理を一から繰り返す必要を減らせます。cache固有のconstructorを設定することもできますが、一般のobject初期化・破棄をすべてconstructor/destructorに任せる仕組みではありません。objectごとの値の初期化と資源解放は、呼び出し側の寿命管理として明示します。

Linux 6.18では、通常のslab allocator実装はSLUBです。SLUBはper-CPUの高速経路とslab単位の空きobject管理を組み合わせます。これは第3版の古典的なslab構造や、旧実装で使われた管理リストをそのまま現在の内部構造とみなせないことを意味します。ここではcache/slab/objectという概念階層は有効な道具として使い、具体的なdescriptorやfreelistの構造は後続節で現行ソースに照らして確認します。

#### APIの使い分け

| 目的 | 主なAPI | 解放API |
|---|---|---|
| 特定の構造体型を繰り返し割り当てる | `kmem_cache_create()`、`kmem_cache_alloc()` | `kmem_cache_free()` |
| 任意の小さなbyte数を割り当てる | `kmalloc()`、`kzalloc()` | `kfree()` |
| ページ単位の領域が必要 | `alloc_pages()`等 | 対応するpage解放API |

```c
struct sample_object {
        unsigned long state;
        void *payload;
};

static struct kmem_cache *sample_cache;

static int __init sample_cache_init(void)
{
        sample_cache = kmem_cache_create("sample_object",
                                         sizeof(struct sample_object),
                                         0, 0, NULL);
        return sample_cache ? 0 : -ENOMEM;
}

static struct sample_object *sample_alloc(gfp_t gfp)
{
        if (!sample_cache)
                return NULL;
        return kmem_cache_alloc(sample_cache, gfp);
}

static void sample_free(struct sample_object *obj)
{
        if (obj)
                kmem_cache_free(sample_cache, obj);
}

static void __exit sample_cache_exit(void)
{
        kmem_cache_destroy(sample_cache);
}
```

この例では、cacheの作成に成功してからobjectを割り当てます。終了処理で`kmem_cache_destroy()`を呼ぶ前に、そのcacheから確保したobjectをすべて返しておく必要があります。確保したobjectのフィールド初期化は必要に応じて呼び出し側で行い、`kmem_cache_free()`後にそのポインタを使い続けてはいけません。`gfp`はobject確保時の実行文脈と回収可否を表すため、interrupt等のatomic contextで`GFP_KERNEL`を使う、といったsleep可能性の不整合を避けます。

**Linux 6.18のソースを追う入口**

- allocator選択とSLUB構成: `mm/Kconfig` (`CONFIG_SLUB`、`CONFIG_SLUB_TINY`)
- 公開API: `include/linux/slab.h` (`kmem_cache_create()`、`kmem_cache_alloc()`、`kmem_cache_free()`、`kmalloc()`系)
- cache/SLUB内部構造: `mm/slab.h` (`struct kmem_cache`、`struct slab`)
- 共通cache管理と汎用cache: `mm/slab_common.c`
- SLUB実装: `mm/slub.c`
- slab allocatorの利用者向け説明と観測: `Documentation/admin-guide/mm/slab.rst`
- GFP flagsと割り当てAPIの選択: `Documentation/core-api/memory-allocation.rst`

### 8.2.2 キャッシュディスクリプタ

各`kmem_cache`は、同じ種類のobjectをどのように配置・割り当てるかを表すcache descriptorです。ただし、descriptorだけに全objectや全slabの状態を詰め込むわけではありません。Linux 6.18のSLUBでは、cache共通の設定に加え、CPUごとの高速経路、NUMA nodeごとのslab管理、slab単位の状態に情報が分散しています。

```text
struct kmem_cache
  cache共通の設定・サイズ・名前
  CPUごとの状態への参照 ──> struct kmem_cache_cpu 等
  nodeごとの状態への参照 ──> struct kmem_cache_node
                                  |
                                  +-- partial slab群
  各slabの状態 ───────────────> struct slab
```

#### cache共通の属性

`mm/slab.h`の`struct kmem_cache`には、object配置やcacheの動作を決める情報があります。主要な項目を役割別に読むと次のようになります。

| 情報のまとまり | Linux 6.18の主なメンバ | 読み方 |
|---|---|---|
| cache識別・公開属性 | `name`、`flags`、`list` | 名前、属性、全cache管理リストとの関係を表す |
| object寸法 | `object_size`、`size`、`reciprocal_size`、`align` | 利用者が要求したobject寸法、内部metadata等を含むslot寸法、index計算、配置境界に関係する |
| freelist配置 | `offset`、`inuse`、`red_left_pad` | freelist metadataやdebug用領域の位置・object配置に関係する |
| slabの割り当て形状 | `oo`、`min`、`min_partial` | slabのorder/object数の選択やnode側partial slab保持に関係する |
| node/CPU状態への参照 | `node[]`、`cpu_slab`、`cpu_sheaves` | NUMA node別、CPU別の割り当て状態を引く入口。利用可能な項目は構成に依存する |
| 初期化・制約 | `ctor`、`allocflags`、`useroffset`、`usersize` | constructor、slab用ページ確保flags、usercopy可能範囲などを表す |

これらのメンバすべてが全kernel configurationで同じ形で存在するわけではありません。CPU partialやdebug、KASAN、NUMA等に関係する項目には構成条件があり、`CONFIG_SLUB_TINY`では省略・簡略化される状態もあります。また、`size`は利用者向けの要求サイズそのものではなく、metadataやalignmentなどを反映した内部slot寸法です。実際のobject数はslabのorderやslabごとの状態と併せて決まります。

#### 一元的なcache descriptorではなく、粒度ごとの状態

第3版の表8-8では、CPUごとの`array`、`batchcount`、`limit`や、複数のslab listを持つ`kmem_list3`が`kmem_cache_t`のメンバとして説明されています。Linux 6.18のSLUBには同名・同配置のdescriptorはありません。CPUの高速pathは`struct kmem_cache_cpu`等、NUMA nodeのpartial slab管理は`struct kmem_cache_node`、slab自身のfreelistやinuse/object countは`struct slab`で表現されます。したがって、旧表の各メンバを現在の`struct kmem_cache`へ一対一対応させず、「cache設定」「CPUローカル状態」「node単位状態」「個別slab状態」に役割を分けて読むのが適切です。

代表例として、`struct kmem_cache_cpu`は現在CPUのfreelistと割り当て元slabを保持し、構成によりpartial slabも持ちます。`struct kmem_cache_node`はnodeごとのlockとpartial slab管理情報を持ち、SLUB debug構成ではfull slab一覧なども持ちます。個々のslabのobject数、inuse count、freelistは`struct slab`側の状態です。こうして高速なCPUローカル処理と、node上のslab集合管理が分かれています。

| 状態の粒度 | 代表構造 | 主な役割 |
|---|---|---|
| cache全体 | `struct kmem_cache` | サイズ、配置、flags、constructor、状態配列への参照 |
| CPU単位 | `struct kmem_cache_cpu`、per-CPU sheaf | CPUローカルのfreelistや一時保持を用いた高速化 |
| NUMA node単位 | `struct kmem_cache_node` | partial slab等をnode別に管理 |
| slab単位 | `struct slab` | slabに属するcache、freelist、object数と使用状態 |

#### 設定値と実行時状態の見分け方

cache名や一部の属性は`/sys/kernel/slab/<cache名>/`から観測でき、`slabinfo`もcache単位の統計や状態を確認する手段です。ただし、sysfsの属性はdescriptor全体の写しではなく、公開対象として選ばれた項目に限られます。構造体の値を直接読んで設計を推測するだけでなく、該当する更新・参照箇所を`mm/slub.c`で追います。

**Linux 6.18のソースを追う入口**

- `struct kmem_cache`、`struct slab`、cache全体リスト: `mm/slab.h`
- CPU/nodeごとの状態とSLUBの処理: `mm/slub.c` (`struct kmem_cache_cpu`、`struct kmem_cache_node`)
- cacheの生成、登録、merge処理: `mm/slab_common.c`
- 公開APIと生成引数: `include/linux/slab.h` (`struct kmem_cache_args`、`kmem_cache_create()`)
- runtime観測と`/sys/kernel/slab/`: `Documentation/admin-guide/mm/slab.rst`

### 8.2.3 スラブディスクリプタ

slab descriptorは、1つのcacheから供給されるobject群の配置領域と、その時点の利用状態を記録します。Linux 6.18のSLUBでは、この役割を`struct slab`が担います。slabは物理ページ（正確にはslab用に確保されたfolio）から成り、そこにcacheのobject slotが並びます。

```text
struct slab (ページ管理用metadata側)
  slab_cache ─────> 所属する struct kmem_cache
  freelist ───────> 次に使えるfree object
  inuse / objects ─> 使用中数 / slab内のobject総数
        |
        v
slab用folioの仮想アドレス範囲
  [ object ][ object ][ object ] ... [ object ]
```

ここで重要なのは、descriptorがslabのobject領域に埋め込まれているという意味ではないことです。Linuxのページ管理metadataである`struct page`の領域を、slabとして使うfolioでは`struct slab`として解釈する設計です。`struct slab`と`struct page`は先頭部分のlayoutを共有し、コンパイル時assertで対応関係やサイズが検証されます。したがって、第3版で説明される「内部／外部slab descriptor」の選択を、そのままLinux 6.18 SLUBの実装モデルとして当てはめることはできません。

#### slab descriptorが表す状態

| `struct slab`の情報 | 意味 |
|---|---|
| `slab_cache` | slabが属する`kmem_cache` |
| `freelist` | slab内の空きobject群の先頭。free pointerをたどって次の空きを得る |
| `inuse` | 使用中object数 |
| `objects` | slab内のobject slot総数 |
| `slab_list`等 | node側のpartial listや、deferred処理などに使う状態。union・構成に応じて用途が変わる |
| `flags`等のpage metadata | folio/pageとの連携やページ属性に関する情報 |

基本的な空き数は`objects - inuse`で把握できます。ただし、objectの確保・解放はCPUローカルfreelistとslab側freelistの間で状態を移すことがあり、複数CPUからの操作やdebug構成も関係します。単純なカウンタ差分だけで、すべての瞬間的な割り当て状態やlist所属を推測するのは避け、更新経路と同期方法を合わせて読みます。

slabを新しく作るとき、SLUBはcacheが選んだorderでページアロケータからfolioを得て、slab単位の`objects`、`inuse`、所属cacheなどを初期化します。空きobjectをfreelistとして連結し、割り当てでは先頭free objectを取り出して使用中数を進めます。object数やorderはcacheのslot size、debug metadata、割り当て可能性などに依存するため、すべてのslabが同じobject数になるとは限りません。

#### slabとnode管理リスト

`struct kmem_cache_node`はNUMA nodeごとのpartial slab管理を担います。空きobjectを持つslabはnodeのpartial管理対象となり、objectがすべて使用中になれば通常のallocation pathではpartialから外れます。空になったslabを保持するかページアロケータへ返すかは、cache設定やSLUBの回収処理に左右されます。旧来の「free/partial/fullの3リストが常にcache descriptorにある」という図式は現行構造そのものではありません。特にfull slabを追跡するlistはdebug構成に依存します。

| 状態の見方 | 現行SLUBでの理解 |
|---|---|
| partial | 空きobjectを持ち、再利用候補となるslab。node単位の管理が関係する |
| full | `inuse == objects`の状態。常時すべてのfull slabを通常構成でlist追跡するとは限らない |
| empty | 使用中objectがない状態。保持・再利用・ページ返却の判断はallocator経路と保持数に依存する |

**Linux 6.18のソースを追う入口**

- `struct slab`と`struct page`/folioの対応: `mm/slab.h` (`struct slab`、`folio_slab()`、`slab_folio()`、`slab_order()`)
- slab用folioのmetadata初期化とfreelist構築: `mm/slub.c` (`allocate_slab()`)
- objectをslab freelistから取り出す処理: `mm/slub.c` (`alloc_single_from_partial()`およびfastpath関連関数)
- node単位のpartial管理: `mm/slub.c` (`struct kmem_cache_node`、partial slabの追加・除去処理)

### 8.2.4 汎用キャッシュと特定用途のキャッシュ

slab cacheは、利用者から見た役割で大きく二つに分けて考えられます。

- **汎用cache**: サイズを指定する`kmalloc()`等のためのsize-class cache群。呼び出し側は型専用cacheを用意せず、要求サイズやGFP属性に応じたcacheを利用する。
- **特定用途cache**: `kmem_cache_create()`等で作り、特定の構造体型・object用途に使うcache。object size、alignment、flags、constructor等をcacheごとに設定できる。

```text
kmalloc(size, flags)
        |
        +--> flagsとsize classに対応するkmalloc cache
                    |
                    +--> object

kmem_cache_alloc(type_cache, flags)
        |
        +--> 構造体型に対応したcache
                    |
                    +--> object
```

第3版で列挙される`cache_cache`や固定個数の`malloc_sizes`、DMA用／通常用の二重配列は、当時の実装の説明です。Linux 6.18のSLUBでは汎用cacheは`kmalloc_caches`等として管理され、サイズクラスやcache typeは構成、GFP flags、accounting、DMA zone等に応じて選択されます。小さなサイズの刻みも含め、古い表の個数・サイズを現行の固定仕様として扱いません。要求サイズからどのcacheが使われるかは、`kmalloc_index()`やcache type選択の実装で追います。

#### 型専用cacheを使う理由と寿命

同じ型のobjectを繰り返し確保する場合、専用cacheによりobject slotのサイズ・alignment等をその型に合わせられます。cache nameや専用flagsによって診断・観測もしやすくなります。一方で、cacheを作るだけでobjectの寿命管理が自動化されるわけではありません。各objectは`kmem_cache_alloc()`で確保し、利用終了時に`kmem_cache_free()`で同じcacheへ返します。cacheを破棄する際は、そのcacheから確保したobjectの使用が終わっていることが前提です。

```c
struct request *req = kmem_cache_alloc(request_cache, GFP_KERNEL);
if (!req)
        return -ENOMEM;

initialize_request(req);
/* requestを利用 */

kmem_cache_free(request_cache, req);
```

一回限りの小さなbufferや、型専用cacheを必要としない用途では、適切なサイズの`kmalloc()`／`kzalloc()`と`kfree()`を使います。いずれの場合もGFP flagsは、現在の実行文脈でsleepやreclaimが許されるかに合わせます。

#### cache merge

Linux 6.18のSLUBは、属性が互換なcacheを同じ実体へmergeし、別名として扱うことがあります。これにより似たcacheごとに別のslabを抱える必要が減り、メモリ利用やcache localityに利点があります。mergeは単にobject sizeが同じであれば成立するわけではなく、alignment、flags、constructor、usercopy設定、merge禁止属性などの条件を調べます。constructorを持つcacheや`SLAB_NEVER_MERGE`属性のcacheなどはmerge対象から外れます。

mergeは運用・安全性にも関わります。同じcache実体を複数用途で共有すると、ある用途のobject overrun等が、同じslab内の別用途objectへ影響しやすくなります。カーネルの`CONFIG_SLAB_MERGE_DEFAULT`や起動時の`slab_nomerge`／`slub_nomerge`設定でmerge方針を変えられます。mergeの有無を調べる際は`slabinfo -a`や`/sys/kernel/slab/`だけでなく、対象cacheの作成条件と`find_mergeable()`の判定も確認します。

| 観点 | Linux 6.18での確認点 |
|---|---|
| 汎用cache | `kmalloc()`のサイズclassとcache type選択 |
| 型専用cache | `kmem_cache_create()`のobject size、alignment、flags、constructor |
| cache merge | 同一sizeだけでなく、alignment・flags等の互換条件 |
| merge抑止 | config、boot parameter、`SLAB_NO_MERGE`等 |
| 調査手段 | `slabinfo -a`、sysfs、`mm/slab_common.c`のmerge判定 |

**Linux 6.18のソースを追う入口**

- `kmalloc()`がcache typeとsize classを選ぶ仕組み: `include/linux/slab.h` (`kmalloc_type()`、`kmalloc_caches`)、`mm/slab_common.c` (`kmalloc_info[]`、`create_kmalloc_caches()`)
- 汎用cacheの初期化: `mm/slub.c` (`kmem_cache_init()`)、`mm/slab_common.c` (`new_kmalloc_cache()`)
- cache生成時のalias/merge: `mm/slab_common.c` (`__kmem_cache_create_args()`、`find_mergeable()`、`slab_unmergeable()`)
- merge制御とconfig: `mm/slab_common.c` (`slab_nomerge`)、`mm/Kconfig` (`CONFIG_SLAB_MERGE_DEFAULT`)
- merge状況の観測: `Documentation/admin-guide/mm/slab.rst` (`slabinfo -a`)

### 8.2.5 スラブアロケータとページアロケータ間のインタフェース

slab allocatorが新しいslabを必要とするとき、下位のページアロケータから物理的に連続したページ群を得ます。SLUBではslabのorderに応じたfolioを確保し、それをslabとして印付け・初期化してobject slotを構成します。slabが空になり回収対象になれば、metadataを解除して同じページ群をページアロケータへ返します。

```text
kmem_cacheに空きobjectがない
        |
        v
SLUBが選んだorderでfolioを確保
        |  alloc_frozen_pages()系
        v
folioをslabとして識別・初期化
  cache / object数 / freelist / accounting
        |
        v
objectをcache利用者へ供給
        |
        v
slabを破棄するとき
  slab属性とaccountingを解除
        |
        v
free_frozen_pages()系でページアロケータへ返却
```

確保時の中心経路は`allocate_slab()`から`alloc_slab_page()`です。`alloc_slab_page()`は通常のページ確保APIではなく、現行SLUBがfolio/page allocatorを呼ぶ内部helperです。要求orderはcacheの`oo`等から選ばれます。初期の高order確保が失敗した場合、SLUBはcacheが定めた最小orderへfallbackすることがあります。そのため、あるcacheのすべてのslabが常に同じorderとは限らず、利用可能なobject数もslabごとに異なる場合があります。

GFP flagsには呼び出し側の制約に加え、cache側のallocation flagsが反映されます。大きなorderの初回要求では、メモリ圧迫時にそれを早めに失敗させて低orderを試せるよう、GFP制約を調整する経路があります。これはobject単位の小さな確保で高order pageを過度に要求しないための設計です。再claimできるか、sleepできるかなどは元のGFP contextに従います。

#### slabとしての識別とaccounting

確保されたfolioは`__folio_set_slab()`によりslabとして識別され、`struct slab`から所属cacheやfreelist等を参照できる状態になります。SLUBはobject数や使用数、freelistを初期化し、node単位の統計を更新します。ページ管理側がslabとして扱えるようにするこのmetadataは、object本体とは別の管理情報です。

`SLAB_RECLAIM_ACCOUNT`は、cache内のページがreclaim可能なslab memoryとして会計されるべきことを示すcache属性です。Linux 6.18では、単純な旧式のグローバルpage counterを唯一の指標として見るのではなく、node単位のVM統計などに反映される経路を追います。このflagは「ページが常に直ちに回収可能」という保証ではなく、reclaim分類・accounting上の意味を持ちます。

#### 解放とreclaim accounting

SLUBがslabを破棄するときは`__free_slab()`がslab属性を解除し、slab object extensionsやnode accounting等を整理してからfolioをページアロケータへ返します。現行経路では`free_frozen_pages()`が使われます。メモリ回収処理中にページを解放する場合、`mm_account_reclaimed_pages()`を通じて回収したページ数をreclaim側へ通知します。ページを返す時点でも、呼び出し中のreclaim stateやslab flagsに応じた会計処理が必要です。

| 方向 | Linux 6.18の代表経路 | 主な処理 |
|---|---|---|
| page allocator → SLUB | `allocate_slab()` → `alloc_slab_page()` → `alloc_frozen_pages()`系 | folio確保、slab識別、object/freelist初期化、node統計更新 |
| SLUB → page allocator | `free_slab()` → `__free_slab()` → `free_frozen_pages()` | slab識別解除、accounting解除、folio返却 |
| reclaimとの接続 | `__free_slab()`内の`mm_account_reclaimed_pages()`等 | reclaim中に解放されたページ数を回収処理へ反映 |

したがって、slab allocatorはページアロケータから独立したメモリ源ではありません。SLUB内に空きobjectや空きslabを保持している間は、そのページ群はページアロケータのfree listには戻っていません。memory pressure時にslabをどのように縮小できるかは、cache利用者が保持するobjectの寿命とreclaim経路にも依存します。

**Linux 6.18のソースを追う入口**

- slab用folioの確保: `mm/slub.c` (`allocate_slab()`、`alloc_slab_page()`)
- folioとslab metadataの関係: `mm/slab.h` (`struct slab`、`folio_slab()`、`slab_folio()`)
- cache flagsに基づくpage accounting: `mm/slub.c` (`account_slab()`、`unaccount_slab()`)
- slabの破棄とページ返却: `mm/slub.c` (`free_slab()`、`__free_slab()`)
- 下位のページ確保・解放: `mm/page_alloc.c` (`__alloc_frozen_pages_noprof()`、`free_frozen_pages()`)

### 8.2.6 キャッシュへのスラブの割り当て

cacheに空きobjectが必要になるたびに新しいslabを作るわけではありません。Linux 6.18のSLUBは、まず現在CPUのfreelist等からobjectを得ようとし、空ならCPU partialやnode partialから既存slabを再利用します。それでも使えるobjectがなければ、新しいslab用folioをページアロケータから確保します。

```text
kmem_cache_alloc(cache, gfp)
        |
        v
CPU-local freelist / sheafからobjectを確保
        |
        +-- success --> objectを返す
        |
        +-- miss
              |
              v
        CPU partial slabを確認
              |
              +-- hit --> slabをCPU側へactivateしてobjectを得る
              |
              +-- miss
                    v
             node partial slabを探索
               preferred nodeを優先し、条件に応じ他nodeも探索
                    |
                    +-- hit --> CPU側へactivateしてobjectを得る
                    |
                    +-- miss --> new_slab()
                                   |
                                   v
                            page allocatorからfolio
```

この図は概念経路で、細部は構成・context・NUMA policyにより変わります。CPU sheavesを使うcacheではそこから始まる場合があります。また、debug cacheではfreelistを一括してCPU側へ移さず、objectを個別に検証しながら割り当てる経路を使うことがあります。

#### partial slabの再利用

新しいslabを作る前に、SLUBは使用可能なpartial slabを探します。現在nodeのpartial管理から候補を得るほか、NUMA構成では距離や割り当て条件に従い別nodeのpartial slabを探索することがあります。`__GFP_THISNODE`等でnodeが強く制約される場合はfallbackできる範囲が狭くなります。得たslabのfreelistをCPU-local側へactivateすると、以降の複数object確保を高速pathで処理できます。

CPU partialはnode partialとは異なり、CPU近傍に一時保持されるslabです。候補がnodeや`pfmemalloc`条件に合わない場合、deactivateや別のpartial管理への返却が必要になることもあります。よって、cache全体にfree objectが存在していても、特定の要求から直ちに利用可能とは限りません。NUMA node制約、GFP context、CPU-local stateや同期状態が実際の探索を左右します。

#### 新しいslabの作成とorder fallback

既存slabから割り当てられない場合に`new_slab()`が呼ばれ、8.2.5で見た`allocate_slab()`経由で新しいslab用folioを確保します。成功するとSLUBは`struct slab`の所属cache、object数、inuse、freelistなどを初期化し、初回objectを取り出した後の状態をCPU側またはpartial管理へ接続します。

cacheの通常orderでページ確保に失敗した場合、SLUBは最小orderを試すことがあります。orderが下がるとslab内のobject総数も減るため、確保回数や管理効率とのtradeoffが生じます。order選択はcacheのsize/alignment、debug metadata、kernel設定および実際のfragmentationに左右され、古典的なslabの`cache_grow()`が常に固定数ページを補充するモデルとは異なります。

| 条件・段階 | SLUBでの動き |
|---|---|
| CPU localに空きがある | page allocatorやnode listへ行かずobjectを返す |
| CPU localが空 | CPU partial、次にnode partial等から再利用を試みる |
| partial slabの条件不一致／候補なし | GFP・NUMA制約に従い新しいslabを要求する |
| 通常orderのfolio確保に失敗 | 最小order fallbackを試す場合がある |
| fallbackも失敗 | 適切な失敗処理を経てallocation失敗を呼び出し側へ返す |

**Linux 6.18のソースを追う入口**

- `kmem_cache_alloc()`からSLUBへ: `mm/slub.c` (`slab_alloc_node()`、`__slab_alloc_node()`)
- CPU-local fast/slow allocationとmiss後の経路: `mm/slub.c` (`___slab_alloc()`)
- CPU partial・node partial探索: `mm/slub.c` (`get_partial()`、`get_partial_node()`、`get_any_partial()`)
- 新規slab確保と初期化: `mm/slub.c` (`new_slab()`、`allocate_slab()`、`alloc_slab_page()`)
- ページ確保・order fallbackの前提: `mm/page_alloc.c` (`__alloc_frozen_pages_noprof()`)、8.2.5
