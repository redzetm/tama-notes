---
title: "LINUXカーネル6系の研究７　プロセススケジューリング"
---

# LINUXカーネル6系の研究７　プロセススケジューリング

このノートでは、Linux 6.18 LTS が実行可能な task の中から次にCPUを使うものをどう選び、CPU間でどのように実行機会を調整するかを研究します。
『詳解 Linux カーネル 第三版』第7章の節番号を道しるべにしますが、2.6当時のアルゴリズムや構造を現行実装と混同せず、説明の中心をLinux 6.18 LTSに置きます。

スケジューリングを読むときは、次の三つを分けます。

- **方針 (policy)**: taskを通常、リアルタイム、deadlineなど、どの規則で扱うか
- **優先度・重み**: 同じクラスや方針の中で、CPU時間をどう配分するか
- **選択と切り替え**: taskが実行可能になったときや再スケジュール要求が立ったとき、いつ実際に次のtaskへ切り替えるか

ユーザー空間で「プロセス」と呼ぶ実行主体の多くは、スケジューラからは `task_struct` を持つtaskとして扱われます。スレッドも個別のtaskであり、CPUごとの実行キューに登録され、個別に実行・プリエンプトされます。

## UmuOS との接続

UmuOS-0.1.7-base-stable は Linux 6.18.1 を QEMU/KVM 上で動かす観測環境です。スケジューリングの実装はゲストLinuxカーネルにあり、ホストのスケジューラが直接ゲスト内taskを選んでいるわけではありません。ホストは仮想CPUを実行し、ゲストカーネルはその仮想CPU上でゲストtaskをスケジュールします。

実行中の方針・優先度や切り替えを調べる際は、`ps` の表示だけで内部状態を断定せず、`chrt`、`taskset`、`/proc/<pid>/sched`、sched trace eventなどを必要に応じて照合します。観測できる項目はユーザー権限、カーネル構成、仮想化設定によって異なります。

## 今回扱う範囲

- 原文対応: 7.1「スケジューリング方針」、7.1.1「プロセスプリエンプション」、7.1.2「最適なクォンタムの長さ」、7.2「スケジューリングアルゴリズム」、7.2.1「従来型プロセスのスケジュール」、7.2.2「リアルタイムプロセスのスケジュール」、7.3「スケジューラが使用するデータ構造」、7.3.1「runqueueデータ構造」、7.3.2「プロセスディスクリプタ」、7.4「スケジューラが使用する関数」、7.4.1〜7.4.4、7.5「マルチプロセッサにおける実行キューの負荷均衡化処理」、7.5.1〜7.5.4、7.6「スケジューリング関連のシステムコール」、7.6.1〜7.6.4
- 今回の主題: Linux 6.18のスケジューリングクラスとpolicy、通常taskの公平性、nice値、リアルタイム・deadline方針、再スケジュール要求、固定クォンタムという見方の限界、EEVDF・FIFO/RR・EDF/CBSの選択規則、CPUごとのrunqueueとtask_struct内のスケジューリング情報、tick・起床・sleep・scheduleの経路、scheduler domainによるCPU topology-aware負荷分散、スケジューリングpolicy・nice・affinityのユーザーAPI
- 現在の進捗: 7.1〜7.6.4を作成。第7章の全節を作成済み

## 7章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 7.1、7.1.1〜7.1.2 | 方針、プリエンプション、実行時間配分 | 作成済み |
| 7.2、7.2.1〜7.2.2 | 通常taskとリアルタイムtaskのスケジューリング | 作成済み |
| 7.3、7.3.1〜7.3.2 | 実行キューとtaskのスケジューリング情報 | 作成済み |
| 7.4、7.4.1〜7.4.4 | tick、起床、選択、scheduleの経路 | 作成済み |
| 7.5、7.5.1〜7.5.4 | SMPの負荷均衡とtask移動 | 作成済み |
| 7.6、7.6.1〜7.6.4 | nice、affinity、リアルタイム関連API | 作成済み |

## 7.1 スケジューリング方針

Linux 6.18 LTSは、一つの優先度計算式だけで全taskを並べるのではなく、スケジューリングクラスごとに異なる実行規則を持ちます。クラスは選択規則の大枠を定め、その内部でpolicyや優先度・重みが使われます。

### Linux 6.18のpolicyとスケジューリングクラス

代表的なユーザー向けpolicyと、その大まかな位置付けは次のとおりです。

| policy | スケジューリング上の位置付け | 読むときの観点 |
|---|---|---|
| `SCHED_NORMAL` (`SCHED_OTHER`) | 通常のtask。fair scheduling classでCPU時間を公平に配分する | nice値は同じクラス内の相対的な重みとして働く |
| `SCHED_BATCH` | fair classのtask。対話的な応答性よりバッチ処理向けの扱い | 「専用の固定長タイムスライス」ではない |
| `SCHED_IDLE` | fair class内で非常に低い重みを持つpolicy | CPUが空いているときの実行を意図し、内部のidle taskとは別物 |
| `SCHED_FIFO`、`SCHED_RR` | real-time classの静的優先度に基づく | 通常taskより高い優先度で実行され得る。CPUを占有し続けない設計が必要 |
| `SCHED_DEADLINE` | deadline class。runtime、deadline、periodの契約に基づく | 単純なnice値やRT優先度とは別の予約・実行規則 |

スケジューリングクラスの優先順位は、クラス間で次に選択する対象を決めるために使われます。通常taskのnice値が高くても、実行可能な高優先度RT taskやdeadline taskを追い越すものではありません。内部にはユーザー向けpolicyとは別に、CPUごとのidle taskなどを扱う仕組みもあります。

Linux 6.18の通常taskについて、古い説明にある「対話型かCPUバウンド型かを判定し、動的優先度を頻繁に上げ下げする」モデルをそのまま当てはめないことが重要です。fair classはtaskの実行量を重み付きで会計し、公平なCPU配分を目指します。Linux 6.18ではEEVDFに基づく選択が導入されており、単純に「最も長く待ったtask」や「nice値だけ」で次のtaskが決まるわけではありません。

I/O待ちが多いかCPUを使い続けるかは、taskの動作を説明する分類としては有用です。しかし、それだけで現行スケジューラが固定の「対話型優先度」を割り当てると考えないようにします。応答性にはfair class内の実行時間会計と選択、起床時の処理、プリエンプション、CPU負荷などが関係します。

### policyと優先度を読み取るCコード

次の例は、呼び出し元taskのpolicy、リアルタイム優先度、nice値を読み取ります。設定を変更しないため、現在のスケジューリング状態を調べる入口として使えます。

```c
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

static const char *policy_name(int policy)
{
        switch (policy) {
        case SCHED_OTHER:    return "SCHED_OTHER";
        case SCHED_BATCH:    return "SCHED_BATCH";
        case SCHED_IDLE:     return "SCHED_IDLE";
        case SCHED_FIFO:     return "SCHED_FIFO";
        case SCHED_RR:       return "SCHED_RR";
        case SCHED_DEADLINE: return "SCHED_DEADLINE";
        default:             return "unknown";
        }
}

int main(void)
{
        struct sched_param param;
        int policy = sched_getscheduler(0);
        int saved_errno;
        int nice_value;

        if (policy == -1) {
                perror("sched_getscheduler");
                return 1;
        }
        if (sched_getparam(0, &param) == -1) {
                perror("sched_getparam");
                return 1;
        }

        errno = 0;
        nice_value = getpriority(PRIO_PROCESS, 0);
        saved_errno = errno;
        if (nice_value == -1 && saved_errno != 0) {
                errno = saved_errno;
                perror("getpriority");
                return 1;
        }

        printf("policy=%s, rt_priority=%d, nice=%d\n",
               policy_name(policy), param.sched_priority, nice_value);
        return 0;
}
```

`sched_getscheduler(0)` と `sched_getparam(0, ...)` は呼び出し元taskのpolicyとpolicy固有の優先度パラメータを取得します。通常policyでは `sched_priority` は通常0であり、nice値は別の属性です。`getpriority()` は成功値として `-1` を返す場合もあるため、戻り値だけでは失敗と区別できず、呼び出し前に `errno` を0にしています。

この例は「表示された数値がスケジューラ内部の全て」を意味するものではありません。実際の選択には実行可能状態、クラス、CPU affinity、CPUごとの負荷、taskの実行会計なども関係します。

**ソースコードの入口**

- スケジューラ共通処理とpolicy設定: `kernel/sched/core.c`
- fair classとEEVDFを含む通常taskの処理: `kernel/sched/fair.c`
- real-time class: `kernel/sched/rt.c`
- deadline class: `kernel/sched/deadline.c`
- taskのスケジューリング属性: `include/linux/sched.h`
- schedulerの設計資料: `Documentation/scheduler/`

### 7.1.1 プロセスプリエンプション

プリエンプションは、実行中taskが自発的に待ちへ入るのを待たず、別taskへCPUを渡すことです。ただし、次の二つの段階を区別します。

1. 実行可能になったtaskを、適切なCPUのrunqueueへ登録する
2. 現在の実行を再スケジュールすべきと判断した場合に、実行中taskへ再スケジュール要求を立て、適切な安全な地点でschedulerを実行する

新しいtaskが実行可能になったからといって、任意の命令の途中で即座にcontext switchするわけではありません。schedulerはクラスの優先順位と各クラスの規則に基づいて再スケジュールの必要性を判断し、カーネルのpreemption modelや実行中のクリティカルセクションなどに従って切り替え可能な地点で処理します。CPU上のtaskに再スケジュールが必要であることは、現行実装では `TIF_NEED_RESCHED` などの状態を通して扱われます。

```text
I/O完了などでtaskが起床
          |
          v
taskを実行可能にし、runqueueへ登録
          |
          v
現在の実行taskを続けるか、再スケジュールするか判定
          |
     再スケジュール要求
          |
          v
安全なscheduler実行地点で次のtaskを選択
```

原文の「起床したtaskの動的優先度と現在taskの動的優先度を比較する」という説明は、Linux 2.6のスケジューラの流れを理解するためのものです。Linux 6.18では、通常task、RT task、deadline taskで比較規則が異なり、単一の動的優先度だけで全ての選択を説明できません。

プリエンプトされたtaskは通常、実行可能状態のままです。これはI/Oなどを待つために自分からsleepへ入った状態とは異なります。また、SMPでは、別CPU上で実行可能taskが追加されたときに、そのCPUへ再スケジュールを促す必要が生じることがあります。起床、CPU選択、再スケジュール、実際のcontext switchは関連しますが、それぞれ別の段階です。

### 7.1.2 最適なクォンタムの長さ

Linux 2.6の説明にあるような「通常taskに一定のクォンタムを順番に割り当て、その期限切れで次へ交代する」という像は、Linux 6.18のfair schedulingを理解する基準にはなりません。通常taskは実行時間を会計され、nice値に応じた重みとEEVDFの選択規則を通じてCPU時間を分配されます。したがって、全taskに共通する固定のタイムスライスを前提にしません。

このことは、実行時間やプリエンプションの粒度が重要でないという意味ではありません。task数、nice値、スケジューラの粒度、CPU負荷、待ちからの起床、スケジューリングクラスによって、実際にCPUを使える長さや応答時間は変わります。短い実行単位は応答性を高める余地がある一方、切り替えやキャッシュ局所性に影響します。長い実行単位は処理効率に寄与し得ますが、ほかのtaskが実行機会を得るまでの時間を延ばす場合があります。

fair classの調整値を「各taskに保証されるクォンタム」と解釈してはいけません。リアルタイムtaskではFIFO/RRの規則と優先度、deadline taskではruntime/deadline/periodの設定が関係します。いずれも、期限時刻に必ず実行されることや、指定した時間内の完了を一般に保証するものではありません。

### 7.1で区別しておきたいこと

- Linux 6.18は異なる規則を持つスケジューリングクラスを使い、単一の優先度値だけで全taskを並べない
- 通常taskのnice値はfair class内の重みに関係し、RT優先度やdeadlineの設定とは別の尺度
- `SCHED_IDLE` policyとCPUごとのidle taskは別の概念
- taskの起床、runqueueへの登録、再スケジュール要求、context switchは一つの瞬間に起こる同義語ではない
- 通常taskに一律の固定クォンタムが配られるという前提でLinux 6.18のfair classを読まない

## 7.2 スケジューリングアルゴリズム

Linux 6.18のschedulerは、全taskを一つの巨大なリストから同じ数値で比較するのではなく、CPUごとのrunqueueとスケジューリングクラスを使って次のtaskを選びます。実行可能taskが存在しないCPUでは、そのCPUのidle taskが実行されます。idle taskは「通常taskがいない状態」でもCPUを表現するために必要ですが、ユーザーが設定する `SCHED_IDLE` policyとは異なります。

原文のLinux 2.6説明では、通常task向けのactive/expired array、動的優先度、クォンタムと、リアルタイムtask向けFIFO/RRを対比します。このうちFIFO/RRの基本的な違いは現行Linuxにも残りますが、active/expired arrayや通常taskの動的優先度計算は6.18のfair schedulerを示すものではありません。

```text
CPUごとのrunqueue
       |
       +--> stop class:     stop task（通常のユーザーpolicyではない）
       +--> deadline class: SCHED_DEADLINE
       +--> RT class:       SCHED_FIFO / SCHED_RR
       +--> fair class:     SCHED_NORMAL / SCHED_BATCH / SCHED_IDLE
       +--> idle task:      他に実行可能taskがない場合
```

上図はクラス選択の考え方を示す概念図です。stop classはCPU停止などカーネル内部の用途で使われ、通常のユーザーpolicyとして設定するものではありません。また、クラス選択の後には各クラス固有のrunqueue・優先度規則が働きます。

### 7.2.1 従来型プロセスのスケジュール

ここでいう「従来型プロセス」は原文の用語であり、Linux 6.18の専用scheduler class名ではありません。現行の通常taskはfair classで扱われ、`SCHED_NORMAL`、`SCHED_BATCH`、`SCHED_IDLE` などのpolicyがこのクラスに対応します。

#### fair classとEEVDF

fair classは、実行可能なtask間でCPU時間を重み付きに配分しようとします。通常policyではnice値から得られる重みが配分に関係し、nice値が低い（優先度が高い）taskは、同じ条件のnice値が高いtaskより大きなCPU shareを受けます。ただしnice値は、別クラスのtaskを追い越すための全体優先度ではありません。

Linux 6.18のfair classではEEVDF (Earliest Eligible Virtual Deadline First) に基づく選択が使われます。概念的には、taskが公平なCPU shareに対して実行不足かどうかをlagで表し、実行可能と判断されたtaskの仮想deadlineを比較して選択します。短いservice sliceを要求するtaskは仮想deadlineが早くなり得るため、応答性を意識した選択にもつながります。

```text
fair class内の概念的な選択
実行量と重みを会計
        |
        +--> 公平なshareに対するlagを評価
        |
        +--> eligibleなtaskのvirtual deadlineを比較
                    |
                    +--> 次に実行するtaskを選択
```

これは選択原理を示す図で、`vruntime`、lag、virtual deadlineを単純な一つの値として同一視するものではありません。具体的な更新・木構造・起床taskの扱いは `kernel/sched/fair.c` とEEVDF設計資料で追います。

したがって、原文の次の要素を現行の通常taskの実装として読み替えないようにします。

- 静的優先度100〜139をそのまま内部の優先度範囲として使う説明
- 動的優先度を平均休止時間から算出し、対話型taskを判定する説明
- 基礎クォンタムの計算式
- 実行可能taskをactive/expiredの二つのarrayに分け、epoch切替で管理する説明

nice値自体はLinux 6.18でもユーザー向け設定として残ります。しかし、nice値から2.6時代の静的優先度や基礎クォンタムを計算し、それをそのまま現行の選択順序とみなすことはできません。

### 7.2.2 リアルタイムプロセスのスケジュール

LinuxのRT classは `SCHED_FIFO` と `SCHED_RR` を扱い、通常のfair taskより高いscheduler class優先順位で選択されます。RT priorityは1〜99の範囲で、値が大きいほど高優先度です。RT class内では高いRT priorityの実行可能taskが先に選ばれます。同一RT priority内の並び方はpolicyによって異なります。

| policy | 同一RT priorityのtask間の基本動作 |
|---|---|
| `SCHED_FIFO` | 実行中taskは、より高いRT priorityのtaskにプリエンプトされるか、自分でblock/yieldするなどの理由がない限り実行を続ける。同じpriorityの後続taskだけを理由に通常のタイムスライスで交代しない |
| `SCHED_RR` | FIFOの規則を基礎に、同じpriorityの実行可能task間でRR timeslice満了時に実行機会を回す |

`SCHED_RR` のtimesliceはRT priorityそのものではなく、同一priorityのRT task間で交代するための量子です。Linuxでは `sched_rr_get_interval()` による照会や、構成に応じた `sched_rr_timeslice_ms` 設定が関係します。これはfair classの通常taskに一律に配られる固定クォンタムとは別の仕組みです。

#### `SCHED_DEADLINE` は別のclass

Linux 6.18の `SCHED_DEADLINE` はRT classのpriority 1〜99を使うpolicyではなく、deadline classで扱われます。taskはruntime、deadline、periodを指定し、schedulerはEDF (Earliest Deadline First) とCBS (Constant Bandwidth Server) を組み合わせて実行可能性と帯域を管理します。

```text
runtime: 1 period内に割り当てられる実行予算
deadline: period開始から相対的に期限を置く時間
period:   実行予算を補充する周期
```

概念的には、期限の早いdeadline taskが先に選ばれ、runtimeを使い切ったtaskは次の補充まで実行を制限されます。実際に期限を守れるかはCPU容量、task間の要求、affinity、admission controlなどに依存します。パラメータを指定しただけで、任意の負荷下における実時間完了が保証されるわけではありません。

#### RT taskを使うときの注意

高優先度のFIFO taskがCPU上で実行可能なまま回り続けると、通常taskや低優先度taskが実行機会を得にくくなることがあります。RT bandwidth throttlingなどの保護機構はありますが、RT schedulingはシステム全体の応答性に影響します。特権の必要性、実行時間、block/yield条件、CPU affinity、watchdogやRT bandwidth設定を確認せず、実機で安易に優先度を上げるべきではありません。

deadline policyにも利用可能な帯域や受け入れ可能性の制約があります。schedulerの期限計算は、ハードリアルタイム保証を無条件に作り出すものではなく、カーネル構成・CPU数・負荷と要求パラメータを含めて検討する必要があります。

**ソースコードの入口**

- class間の選択とpolicy設定: `kernel/sched/core.c`
- fair/EEVDF: `kernel/sched/fair.c`、`Documentation/scheduler/sched-eevdf.rst`
- FIFO/RR: `kernel/sched/rt.c`
- EDF/CBSとdeadline bandwidth: `kernel/sched/deadline.c`、`Documentation/scheduler/sched-deadline.rst`
- RT bandwidth設定: `Documentation/scheduler/sched-rt-group.rst`

### 7.2で区別しておきたいこと

- active/expired arrayはLinux 2.6の通常task管理構造であり、Linux 6.18のfair classの構造ではない
- `SCHED_FIFO` / `SCHED_RR` はRT class、`SCHED_DEADLINE` は別のdeadline classに属する
- RT priorityの大小とnice値は別の尺度であり、数値を直接比較しない
- `SCHED_RR` のtimesliceと通常taskのCPU share・実行粒度は別の概念
- deadline schedulingは設定だけで期限達成を保証せず、帯域の admission controlと実システム条件が関係する

## 7.3 スケジューラが使用するデータ構造

スケジューラのデータ構造を読むときは、システム全体のtask一覧と、CPUが次に実行する候補を管理するrunqueueを区別します。taskが存在することと、今CPU上で実行可能であることは同じではありません。sleep中のtaskも `task_struct` として存在しますが、実行待ちのqueueに登録されているとは限りません。

Linux 6.18では、taskごとの状態やスケジューリング属性を `task_struct` が持ち、CPUごとの `struct rq` がそのCPUの実行状態と各classのqueueをまとめます。fair、RT、deadlineはそれぞれ固有のentityとqueue情報を使い、共通の `rq` がクラス間の選択やCPUごとの会計を支えます。

### 7.3.1 runqueueデータ構造

SMPシステムでは、各CPUに対応するrunqueueがあります。`kernel/sched/sched.h` ではCPUごとのrunqueue配列が `runqueues` として宣言され、`cpu_rq(cpu)` や `this_rq()` などの内部macroから参照されます。taskがrunnableとしてqueueされると、原則として実行可能CPUのうち一つのrunqueueに所属し、そのCPUで実行されます。負荷分散やwake-up placementなどにより、条件を満たすCPUへ移動することがあります。

```text
CPU 0: struct rq
  ├─ cfs_rq: fair classのtask / scheduling entity
  ├─ rt_rq:  FIFO・RR task
  └─ dl_rq:  deadline task

CPU 1: struct rq
  ├─ cfs_rq
  ├─ rt_rq
  └─ dl_rq
```

`struct rq`は、単なるtaskの連結リストではありません。Linux 6.18の実装では、たとえば実行可能数 `nr_running`、runqueue lock、現在の実行task `curr`、idle/stop taskへの参照、CPU番号、スケジューラ時計、負荷・utilization情報、各クラスの `cfs_rq`・`rt_rq`・`dl_rq` などを持ちます。設定によりNUMA、NO_HZ、core scheduling、sched_extなどに関係するメンバも含まれるため、全ビルドで同一の構造体レイアウトとは限りません。

各クラスのqueueも同じ構造ではありません。

- fair classの `cfs_rq` は、fair scheduling entityの重み・実行会計・選択用timelineなどを扱う
- RT classの `rt_rq` は、優先度ごとのqueueや、最優先度・push可能taskなどを扱う
- deadline classの `dl_rq` は、deadline順のtreeや実行帯域情報を扱う

fair group schedulingやcgroupなどが有効な場合は、taskだけでなくtask groupを表すentityと階層的なrunqueueが関係します。したがって「CPUごとに一つの単純なリストがあり、そこからtaskを取り出す」という図は、全体の入口としては使えても現行データ構造の説明としては不十分です。

runqueueの更新やtask移動は並行するCPUから同時に操作され得ます。そのため、queueを保護するlockと、複数CPUのrunqueue lockを取得する順序が重要です。lockの保持条件や関数の前提はソース内のコメントとlockdepの情報を合わせて読み、構造体メンバだけから同期規則を推測しないようにします。

### 7.3.2 プロセスディスクリプタ

Linux 6.18の `task_struct` は、taskの実行状態・優先度・所属CPU・affinityなどを管理する大きな構造体です。スケジューラ関連フィールドの一部を抜き出すと、次のような関係になります。

```c
struct task_struct {
        unsigned int __state;
        int on_cpu;
        int on_rq;

        int prio;
        int static_prio;
        int normal_prio;
        unsigned int rt_priority;

        struct sched_entity se;
        struct sched_rt_entity rt;
        struct sched_dl_entity dl;
        const struct sched_class *sched_class;

        unsigned int policy;
        int nr_cpus_allowed;
        const cpumask_t *cpus_ptr;
        cpumask_t cpus_mask;
        /* ほかのtask状態・資源への参照など */
};
```

これはLinux 6.18の構造を読むための抜粋であり、完全な定義や固定ABIではありません。設定によって追加のfieldがあり、field配置もカーネルのビルドで変わり得ます。実際の定義は `include/linux/sched.h` を確認します。

主なfieldの読み方は次のとおりです。

- `__state`: sleepなどtaskの状態を示す。runnable/queuedかどうかは `on_rq` など別の情報とも合わせて読む
- `on_cpu`: CPU上で実行中かを表すscheduler内部状態
- `on_rq`: runqueueへの所属状態を表すscheduler内部状態。値の意味は定義と遷移処理を確認する
- `prio`、`static_prio`、`normal_prio`: scheduler内部の優先度情報。数値が小さいほど高い優先度を表す領域があるが、クラス間のpolicy選択をこれ一つで説明しない
- `rt_priority`: RT policyで使うリアルタイム優先度
- `se`、`rt`、`dl`: fair、RT、deadlineの各classで必要となるentity状態
- `sched_class`: taskが属する内部scheduler classへの参照
- `policy`: `SCHED_NORMAL`、`SCHED_FIFO`、`SCHED_DEADLINE`などのユーザー向けpolicy
- `cpus_ptr` / `cpus_mask`: taskが実行可能なCPUの制約を表すaffinity情報

fair classの `sched_entity` にも、スケジューリングに必要な実行会計・重み・tree node・EEVDF関連値などがあります。現行の定義には `load`、`run_node`、`deadline`、`min_vruntime`、`min_slice`、`sum_exec_runtime`、`vruntime` などが含まれます。EEVDFを追う際は、名前に `deadline` が含まれるfieldだけで判断せず、fair class内部のvirtual deadlineと `SCHED_DEADLINE` policyの実時間deadlineを区別します。

#### taskのaffinityと実行CPUを観測するCコード

次の例は、呼び出し元taskが現在どのCPUで動いているかと、実行可能なCPUのmaskを表示します。これはカーネル内部の `rq` を直接読むものではなく、CPU affinityのユーザー向けinterfaceから配置制約を確認する例です。

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>

int main(void)
{
        cpu_set_t allowed;
        int current_cpu;

        current_cpu = sched_getcpu();
        if (current_cpu == -1) {
                perror("sched_getcpu");
                return 1;
        }

        if (sched_getaffinity(0, sizeof(allowed), &allowed) == -1) {
                perror("sched_getaffinity");
                return 1;
        }

        printf("observed CPU: %d\nallowed CPUs:", current_cpu);
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
                if (CPU_ISSET(cpu, &allowed))
                        printf(" %d", cpu);
        }
        putchar('\n');
        return 0;
}
```

`sched_getcpu()` が返すCPUは呼び出し時点の観測値であり、直後も同じCPUに留まる保証ではありません。affinity maskは実行可能CPUの制約を示しますが、taskが次にどのCPUを選ばれるかやrunqueueの内部状態までは示しません。

#### Linux 2.6のfield説明を現行構造へ読み替える

原文の `run_list`、`array`、`sleep_avg`、`time_slice`、`first_time_slice`、`expired` などは、2.6 O(1) schedulerのデータ構造とアルゴリズムを説明するfieldです。Linux 6.18の `task_struct` に同じ名前・意味で存在するものとして追わないようにします。

また、原文にあるfork時の残り `time_slice` を親子で分割するコードは、現行のfair schedulerの実装を表すコードではありません。現在もtask生成時にスケジューラ初期化は行われますが、fair/RT/deadlineの状態やclassごとのaccountingを整える処理として、`sched_fork()`、`wake_up_new_task()`等の呼び出し経路をLinux 6.18のソースで調べます。

**ソースコードの入口**

- `task_struct` とtaskのスケジューリングfield: `include/linux/sched.h`
- `sched_entity`、classごとのqueue、`struct rq`: `kernel/sched/sched.h`
- fair classのentityと`cfs_rq`: `kernel/sched/fair.c`、`kernel/sched/sched.h`
- RT classのqueue: `kernel/sched/rt.c`、`kernel/sched/sched.h`
- deadline classのentityとqueue: `kernel/sched/deadline.c`、`kernel/sched/sched.h`
- fork後のscheduler初期化: `kernel/sched/core.c`

### 7.3で区別しておきたいこと

- task一覧とCPUごとのrunqueueは別の管理目的を持つ
- `TASK_RUNNING`、`on_rq`、`on_cpu`は関連するが同じ状態を表すfieldではない
- Linux 6.18のrunqueueはclass固有queueの集合であり、2.6のactive/expired priority arrayではない
- `task_struct` は内部実装であり、field配置や利用可能なfieldは構成依存で、ユーザー空間ABIではない
- fair classのvirtual deadlineと `SCHED_DEADLINE` の期限・runtime契約を混同しない

## 7.4 スケジューラが使用する関数

Linux 2.6のスケジューラ関数名は、現在の処理を探す入口として役立ちます。ただし、原文の `scheduler_tick()` 内部で一律にタイムスライスを減らす処理や、`recalc_task_prio()` による休止時間ベースの動的優先度計算を、Linux 6.18にそのまま対応づけることはできません。

現行コードでは、`kernel/sched/core.c` がクラス共通の状態遷移・tick・起床・schedule経路をまとめ、クラス固有の処理は各 `sched_class` callbackを通じて `fair.c`、`rt.c`、`deadline.c` などへ委譲されます。

### 7.4.1 `scheduler_tick()`関数

タイマtickなどを契機に `scheduler_tick()` が実行されると、カーネルはCPUごとのrunqueue時計や実行時間会計を更新し、現在のtaskのscheduler classに応じたtick処理を呼び出します。概念的には次の流れです。

```text
clockevent/tick
      |
      v
scheduler_tick(rq)
      |
      +--> runqueueの時間・負荷情報を更新
      +--> curr->sched_class->task_tick(...)
      |       +--> fair: 実行会計、選択・再スケジュール判定
      |       +--> RT:   RR timeslice等のclass固有処理
      |       +--> deadline: runtime等のclass固有処理
      +--> 必要なら再スケジュール要求
```

tickは「現在taskの固定クォンタムを毎回一つ減らす処理」と同義ではありません。NO_HZなどでtickが抑止される構成もあり、スケジューリングの実行時間会計はtick以外のscheduler eventや時計更新とも連携します。公平性・RT timeslice・deadline runtimeはそれぞれ該当classの規則で扱われます。

Linux 2.6の説明にある `task_timeslice()`、`time_slice` の減算、active/expired arrayへの移動、`rebalance_tick()` 呼び出しを6.18の同一実装として探すのではなく、共通tick処理からclass callbackとSMP balancingの現在の経路を追います。

**ソースコードの入口**

- tick共通処理と `scheduler_tick()`: `kernel/sched/core.c`
- class callback定義: `kernel/sched/sched.h`
- fair task tick: `kernel/sched/fair.c`
- RT task tickとRR量子: `kernel/sched/rt.c`
- deadline taskの実行予算: `kernel/sched/deadline.c`

### 7.4.2 `try_to_wake_up()`関数

`try_to_wake_up()` はsleep状態などにあるtaskを起床させるscheduler共通経路です。単に `__state` を `TASK_RUNNING` に書き換えるだけではなく、起床条件の確認、taskとrunqueueの同期、実行CPUの選択、runqueueへのenqueue、必要に応じた再スケジュール要求を連携させます。

```text
待ち条件が成立 / signalなど
          |
          v
try_to_wake_up(task, state, ...)
          |
          +--> 起床対象のstateを検査
          +--> on_cpu / on_rqと競合しないよう同期
          +--> affinity・wake-up placementからCPUを選ぶ
          +--> taskをrunnableにして選択CPUのrunqueueへenqueue
          +--> 現在taskより優先すべきならrescheduleを要求
```

CPU選択は「常に最後に実行したCPU」または「常に起床を処理したCPU」ではありません。taskのaffinity、CPUのonline/active状態、wake affinity、idle CPU、scheduler domainや負荷などを考慮します。具体的な判断はtaskのclassや構成にも依存します。

起床要求が重なる場合や、taskが別CPU上で切り替え中の場合に備え、schedulerはtask lock・runqueue lockとメモリ順序を用いて状態遷移を同期します。そのため、外部コードからtask状態fieldを直接変更して起床させることはできません。通常のカーネルコードはwait queue等の公開APIを使います。

#### wait queueを使ったカーネルコード例

次は、条件成立までtaskをsleepさせ、条件が成立した側から起床を通知する最小のカーネルコード例です。実際のdriverでは、条件の更新と複数readerの扱いを適切なlockやatomic操作で設計します。

```c
#include <linux/atomic.h>
#include <linux/wait.h>

static DECLARE_WAIT_QUEUE_HEAD(data_waitq);
static atomic_t data_ready = ATOMIC_INIT(0);

int wait_for_data(void)
{
        return wait_event_interruptible(data_waitq,
                                        atomic_read(&data_ready) != 0);
}

void publish_data(void)
{
        atomic_set(&data_ready, 1);
        wake_up_interruptible(&data_waitq);
}
```

`wait_event_interruptible()` は条件を再確認しながら待ち状態へ入り、必要なときにscheduleし、signalで中断された場合は負のerrorを返します。呼び出し側が手作業で状態をsleepへ変えてから `schedule()` を呼ぶより、wait queue APIを使う方が、条件検査とwake-upの競合を正しく扱いやすくなります。例は一度だけreadyになる単純な状態を想定しており、繰り返し消費するデータqueueの同期設計そのものではありません。

**ソースコードの入口**

- `try_to_wake_up()`、CPU選択、wake-up preemption: `kernel/sched/core.c`
- wait queueのAPI: `include/linux/wait.h`
- sleep/wakeupの呼び出し例: `kernel/sched/`、`kernel/`内のwait queue利用箇所
- sched wakeup trace event: `include/trace/events/sched.h`

### 7.4.3 `recalc_task_prio()`関数

`recalc_task_prio()` はLinux 2.6のO(1) schedulerにおいて、平均休止時間から動的優先度やinteractive bonusを更新する関数です。Linux 6.18には、この関数を同じ役割で使って通常taskの優先度を再計算する仕組みはありません。

Linux 6.18でもtaskの起床はfair classの状態に影響しますが、理解の中心は休止時間から「対話型優先度」を作ることではありません。fair classは実行時間・重み・公平性に関するentity状態を管理し、EEVDFのeligibilityとvirtual deadlineを用いて選択します。RTやdeadline classはそれぞれ別の実行規則とaccountingを持ちます。

そのため原文の `sleep_avg`、`CURRENT_BONUS`、`effective_prio()`、`activated` を6.18の `task_struct` fieldや現行関数として検索し続けるのではなく、起床経路の `try_to_wake_up()` から、enqueue時のclass callbackとfair entityの更新を追います。古い関数は、当時の方針が「taskの休止履歴からinteractive性を推定した」ことを示す歴史的な説明として扱います。

**現行実装の入口**

- 起床とenqueueの共通処理: `kernel/sched/core.c`
- fair entityのenqueue・wakeup処理: `kernel/sched/fair.c`
- EEVDFの概念とlagの説明: `Documentation/scheduler/sched-eevdf.rst`

### 7.4.4 `schedule()`関数

`schedule()` は、現在taskが自分からCPUを譲る場合や、sleepからscheduleする場合に使う主要APIです。内部では共通schedulerがrunqueueをロックして状態と候補を整え、各classの選択処理から次のtaskを決め、必要な場合だけcontext switchを行います。

#### sleepによる直接のschedule

資源やeventを待つtaskは、通常wait queueなどのAPIによって待ち条件とtask stateを連携させます。条件が成立するまでtaskはrunnableではなくなり、CPUを別taskへ渡します。起床後に再実行されても条件を再確認する必要があります。これは「scheduleを呼べば必ず別taskに切り替わる」ことを意味しません。runqueue上に選ぶべき別taskがなければ、同じtaskが続けて選ばれることもあります。

#### 再スケジュール要求からの遅延実行

tick、taskのwake-up、policy変更などで再スケジュールが必要になっても、現在の命令の任意の位置から常に `schedule()` を呼べるわけではありません。schedulerは再スケジュール要求を記録し、割り込み復帰やkernel preemptionの許される地点など、適切な経路で実際のschedule処理へ進みます。割り込み禁止区間、spinlock保持中、preemption countなどの制約も関係します。

```text
再スケジュール要因
    ├─ taskがsleepへ入る ───────────────> schedule経路
    ├─ tickで実行機会の再評価が必要 ───> need-reschedを記録
    └─ より優先すべきtaskがwake-up ───> need-reschedを記録
                                              |
                                              v
                            preemption / interrupt return等の適切な地点
                                              |
                                              v
                                      __schedule()
                                              |
                             classから次のtaskを選び、必要ならswitch
```

#### task選択とcontext switch

`__schedule()` は現在taskの状態とrunqueueを整合させた後、次に実行するtaskを選びます。切り替えるtaskが現在taskと異なる場合はcontext switchへ進み、アーキテクチャ依存のCPU context切り替えと必要なメモリ管理処理を行います。同じtaskが再選択された場合は、task間のcontext switchを行わずschedule経路から戻ります。

`schedule()`、`__schedule()`、`context_switch()`、architecture固有の `switch_to` は、同じ関数の別名ではなく、API・共通選択処理・切替準備・architecture依存切替という異なる層に属します。Linux 2.6のBKL、`lock_depth`、旧 `switch_to` 展開を現行のschedule経路として持ち込まず、現行の `kernel/sched/core.c` と各architectureのswitch codeを分けて読みます。

**ソースコードの入口**

- 公開schedule APIと `schedule()`: `include/linux/sched.h`、`kernel/sched/core.c`
- `__schedule()`、runqueue lock、class選択: `kernel/sched/core.c`
- context switchの共通処理: `kernel/sched/core.c`
- CPU固有のregister/stack切替: `arch/*/include/asm/switch_to.h`、architecture固有の実装
- 再スケジュール要求とsched trace event: `include/linux/thread_info.h`、`include/trace/events/sched.h`

### 7.4で区別しておきたいこと

- tickは固定クォンタムの単純な減算器ではなく、classごとのaccounting・tick処理を呼び出す契機の一つ
- `try_to_wake_up()` はstate変更だけでなく、同期・CPU選択・enqueue・preemption判定を含む
- `recalc_task_prio()` はLinux 2.6 O(1) schedulerの関数で、Linux 6.18の現行関数ではない
- sleepによるscheduleと、need-reschedを記録して後でscheduleする経路を区別する
- `schedule()` が呼ばれても、別taskへのcontext switchが必ず起きるとは限らない
- task状態fieldを直接変更せず、wait queueなどの同期APIを使う

## 7.5 マルチプロセッサにおける実行キューの負荷均衡化処理

SMP環境では各CPUにrunqueueがあるため、taskは同時に一つのCPUでしか実行されません。一方、どのCPUに配置するかは単純な「空いているCPUへ移す」だけでは決まりません。CPU topology、taskのaffinity、キャッシュ局所性、CPU capacity、NUMA距離、scheduler classなどを考慮し、配置の利点とtask migrationのコストを比較します。

Linux 6.18の負荷分散は、Linux 2.6当時のtickごとの `rebalance_tick()`、`load_balance()`、`move_tasks()` の固定的な関数分担をそのまま持つわけではありません。現行実装ではscheduler domain hierarchy、各classのbalance iterator、periodic balancing、idle balancing、wake-up時のCPU選択など複数の経路が関与します。

### 7.5.1 スケジューリングドメイン

`struct sched_domain` は、負荷を均衡させるCPUの範囲と、その上位・下位の関係を表します。各CPUには基礎となるdomainがあり、`parent` をたどる階層を形成します。domain内のCPUはscheduler groupに分かれ、group単位の負荷を比較して必要ならtaskを移動します。

```text
NUMA全体のdomain
   ├─ node 0のgroup
   │     └─ node内のCPU/SMT domain
   └─ node 1のgroup
         └─ node内のCPU/SMT domain
```

この図は概念例です。実際のdomain構成はarchitecture、CPU topology、NUMA構成、cpuset partition、kernel configurationなどによって変わります。SMT siblingをどうまとめるか、NUMA node間をいつ比較するかも固定ではありません。Linuxは可能な範囲で近いtopology levelから負荷を比較し、必要な場合に広いdomainへ検討を進めます。

SMPは「全CPUが同じで、どのCPUにも常に同じコストでtaskを移せる」という意味ではありません。big.LITTLE等の非対称capacity、共有cache、SMT sibling、NUMA memory localityなどが存在するため、schedulerはCPU capacityとtopology情報を用います。task affinityやcpusetは、配置可能CPUを制限します。

現行のdomainは2.6時代の「全物理CPUのphys_domains」「論理CPUのcpu_domains」のような固定global配列名をそのまま使うものではありません。Linux 6.18ではCPUごとの `rq->sd` などからdomain階層をたどり、domain・groupの実装を `kernel/sched/sched.h` と `kernel/sched/topology.c` で確認します。

### 7.5.2 周期的な負荷均衡の起動

負荷均衡は、毎tickに全CPUの全taskを走査する処理ではありません。schedulerはtickや他のscheduler eventからbalanceの必要性を判定し、必要な場合にSCHED_SOFTIRQ等の処理へつなぎます。idle CPUが新しいtaskを必要とする場合には、idle balanceの経路が別途動くことがあります。

負荷分散の周期はdomainごとに管理され、偏りの有無やidle状態、過去のbalance結果によって再試行の時期が変化します。NO_HZ環境ではtickの止まったCPUもあるため、2.6のように各CPUでtickごとに同一関数が必ず実行されるとは考えません。

```text
tick / wakeup / CPU idle等
          |
          +--> domainごとにbalanceが必要か判定
                    |
                    +--> periodic balancing (SCHED_SOFTIRQ等)
                    +--> idle balancing
                    +--> wake-up時のplacement/pull
                              |
                              v
                    不均衡・affinity・capacityを評価
```

**ソースコードの入口**

- tickからbalance triggerへの共通経路: `kernel/sched/core.c`
- domainを反復してbalanceする処理: `kernel/sched/fair.c`
- topologyからscheduler domainを構築: `kernel/sched/topology.c`
- domain flagの意味: `include/linux/sched/sd_flags.h`
- scheduler domain設計資料: `Documentation/scheduler/sched-domains.rst`

### 7.5.3 負荷均衡の判定とtask移動

負荷均衡では、単純にrunqueue上のtask数だけを等しくするわけではありません。taskごとの重みやestimated utilization、CPU capacity、group単位の負荷、affinity、taskを動かせるかどうかを考慮し、移動して有効な改善が得られると判断した場合に限ってtaskをpull/pushします。

概念的なpull balancingは次のように整理できます。

1. local CPUのscheduler domainを選び、group間の負荷を評価する
2. 移動元候補となるbusiest group/CPUを探す
3. taskのclass iteratorを通じて移動可能な候補を探す
4. affinity、実行中状態、migration制約などを確認する
5. runqueue lockを取得してsourceからdequeueし、destinationへenqueueする
6. destination CPUでより優先すべきtaskになった場合、必要に応じてrescheduleを要求する

実際の実装にはlock競合やbalanceの再試行、task group、NUMA balancing、asymmetric capacityなどがあり、この手順は概要です。Linux 6.18では、移動候補の列挙やclass固有制約をscheduler classのiterator interfaceと連携させます。2.6の `move_tasks()` がexpired arrayから移すという手順は現行構造には当てはまりません。

task migrationにはcache localityを失うコストがあります。そのため、多少の負荷差があれば常に移動するのではなく、CPUの遊休、負荷差、migration cost、taskの利用可能CPUなどを見て判断します。負荷均衡は「全CPUのrunqueue長を必ず同一にする」機能ではなく、性能や応答性を高めるための調整機構です。

#### CPUごとの実行可能task数を確認する例

ユーザー空間からは `/proc/schedstat` などでCPU単位のscheduler統計を調べられる場合があります。以下はファイルを開いて内容を表示するだけの最小例です。列の意味や版ごとの形式はカーネル文書と照合してください。

```c
#include <stdio.h>

int main(void)
{
        FILE *file = fopen("/proc/schedstat", "r");
        char line[512];

        if (file == NULL) {
                perror("fopen /proc/schedstat");
                return 1;
        }

        while (fgets(line, sizeof(line), file) != NULL)
                fputs(line, stdout);

        if (ferror(file)) {
                perror("fgets /proc/schedstat");
                fclose(file);
                return 1;
        }
        if (fclose(file) == EOF) {
                perror("fclose");
                return 1;
        }
        return 0;
}
```

`/proc/schedstat` の出力は `CONFIG_SCHEDSTATS` 等の構成やカーネルversionに依存します。また、統計値は時間経過とともに変化するため、単発の値から負荷分散が正常・異常と結論づけず、該当カーネルの `Documentation/scheduler/sched-stats.rst`、trace event、CPU topologyと合わせて観測します。

### 7.5.4 taskを別CPUへ移動する条件

taskは、CPU affinity maskの外にあるCPUへ移動できません。加えて、実行中taskの移動にはCPU間の同期が必要で、migration-disabled領域にあるtaskやCPU hotplug処理中など、ただちに移せない場合があります。schedulerはtaskの状態とsource/destination runqueueを同期し、class固有のenqueue/dequeue処理を通して移動させます。

移動できるtaskが見つからないこともあります。affinityによる制約、source CPU上で実行中、migration制約、taskのclassやscheduler policy、負荷を減らす効果が不十分といった理由が考えられます。したがって「CPU Aの負荷が高くCPU Bがidleなら、必ずAのtaskがBへ移る」とは限りません。

taskが移動した後も、destination CPUの次の選択はscheduler class規則に従います。移動の完了と移動先での即時実行は別であり、現在taskの再スケジュールが必要かどうかも別途判断されます。

**ソースコードの入口**

- periodic/idle balancingとtask pulling: `kernel/sched/fair.c`
- task CPU選択、affinity、migration共通処理: `kernel/sched/core.c`
- scheduler classのbalance iterator: `kernel/sched/sched.h`、各class実装
- scheduler domain構築・topology: `kernel/sched/topology.c`
- sched trace event: `include/trace/events/sched.h`
- scheduler統計の形式: `Documentation/scheduler/sched-stats.rst`

### 7.5で区別しておきたいこと

- runqueueはCPUごとだが、task配置はaffinity・topology・capacity・localityを考慮して調整される
- scheduler domainは負荷を比較するCPU範囲を表し、ハードウェア構成ごとに階層が異なり得る
- periodic balancing、idle balancing、wake-up placementは関連するが同一経路ではない
- balancingはtask数を機械的に同数にそろえる処理ではない
- task migrationが成功しても、移動先CPUで即座に実行されるとは限らない
- 2.6の`rebalance_tick()`・`move_tasks()`やexpired arrayの記述を、Linux 6.18の関数構成と混同しない

## 7.6 スケジューリング関連のシステムコール

LinuxのスケジューリングAPIは、ユーザー空間からtaskのnice値、policy、policy固有のパラメータ、CPU affinityなどを設定・取得するための入口です。APIは「プロセス」と呼ばれることがありますが、Linux kernelのschedulerは個々のthread/taskを実行単位として扱います。NPTLのthread groupやlibc wrapperの意味も含め、対象が自分自身か、特定のtaskかをman pageで確かめます。

```text
通常taskの配分を調整      nice() / getpriority() / setpriority()
policy・RT priority       sched_setscheduler() / sched_setparam()
deadline属性              sched_setattr()
実行可能CPUの範囲          sched_setaffinity()
実行機会の譲渡・照会       sched_yield() / sched_rr_get_interval()
```

### 7.6.1 `nice()`システムコール

`nice()` は呼び出し元threadのnice値をincrement分だけ変更するAPIです。Linux 6.18のfair classでは、nice値は同じクラス内のCPU shareに関係する重みとして働きます。nice値が低いほど重みが大きく、相対的にCPU時間を多く受けますが、CPU時間や実行開始時刻を保証するものではありません。

nice値の有効範囲は通常 `-20`（高い相対優先度）から `19`（低い相対優先度）です。自分のtaskのnice値を上げて優先度を下げることは通常許されますが、nice値を下げて優先度を上げるには `CAP_SYS_NICE` または適切な `RLIMIT_NICE` などの条件が関係します。対象task、user namespace、security policyによって許可条件が異なるため、「rootだけ」と単純化しません。

`nice()` は指定したincrementを加算し、範囲外の値は `-20`〜`19` に丸めます。Linuxのraw system callは成功時に `0` を返しますが、POSIX準拠のglibc wrapperは新しいnice値を返します。wrapperの成功値として `-1` が返る可能性もあるため、呼び出し結果を判定するときは `errno` を事前に0にして確認します。設定後の値を明示的に読む場合は `getpriority()` を使います。`getpriority()` も有効なnice値として `-1` を返し得るので、同じく `errno` を併用します。

```c
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>

int main(void)
{
        int nice_value;

        errno = 0;
        nice_value = getpriority(PRIO_PROCESS, 0);
        if (nice_value == -1 && errno != 0) {
                perror("getpriority");
                return 1;
        }

        printf("nice=%d\n", nice_value);
        return 0;
}
```

この例は呼び出し元のnice値を読み取るだけです。優先度を変更する場合は、`nice()` または `setpriority()` を使い、失敗時の `errno` と実行環境の権限を確認します。

### 7.6.2 `getpriority()`、`setpriority()`システムコール

`getpriority()` と `setpriority()` は、`which` と `who` で指定した対象のnice値を取得・設定します。代表的な `which` は `PRIO_PROCESS`、`PRIO_PGRP`、`PRIO_USER` です。`who == 0` は対応する呼び出し元の対象を表します。

Linuxのkernel属性はtask/thread単位で、同じprocess内のthreadが異なるnice値を持つことがあります。`getpriority()` は呼び出しthreadの値を返します。POSIX上の「process」表現との違いがあるため、multi-threaded programではman pageおよびlibcの仕様を確認します。nice値をprocess全体で一つの値として扱うと、Linux上のthread間の差を見落とすことがあります。

`setpriority()` でnice値を下げる（数値を小さくし、CPU優先度を上げる）操作には権限・resource limitが関係します。nice値を上げる方向でも、対象の所有者やsecurity policyなどによって拒否される場合があります。また、nice値の違いはfair class内での配分に作用するもので、RTやdeadline classの優先度を設定するものではありません。

### 7.6.3 `sched_getaffinity()`、`sched_setaffinity()`システムコール

CPU affinityは、taskが実行可能なCPUの集合を制約します。`sched_getaffinity()` は現在の許可maskを取得し、`sched_setaffinity()` はmaskを設定します。新しいmaskに現在のCPUが含まれない場合、kernelは条件を満たすCPUへtaskを移動させます。

指定したmaskが実際に許可される範囲は、online CPUだけでなくcpuset/cgroupなどの制約との共通部分になります。設定要求の全CPUが必ずそのまま反映されるとは限らず、利用可能なCPUがない場合はエラーになります。CPU番号はシステムの構成に依存します。

affinityを単一CPUに絞るとcache localityや測定の再現性に有利な場合がありますが、負荷を他CPUへ分散しにくくし、性能や応答性を悪化させることもあります。affinityは「CPU予約」や「CPU時間の保証」ではありません。CPU isolationやcpusetとの違いも区別します。

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>

int main(void)
{
        cpu_set_t allowed;

        if (sched_getaffinity(0, sizeof(allowed), &allowed) == -1) {
                perror("sched_getaffinity");
                return 1;
        }

        printf("allowed CPUs:");
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
                if (CPU_ISSET(cpu, &allowed))
                        printf(" %d", cpu);
        }
        putchar('\n');
        return 0;
}
```

この例は呼び出し元threadのaffinityを表示するだけです。CPU集合の上限が `CPU_SETSIZE` を超える大規模構成では、動的CPU set APIを検討します。

### 7.6.4 リアルタイムプロセス関連のシステムコール

Linuxでは、次のAPIでpolicyやpolicy固有のパラメータを取得・設定できます。

| API | 役割 | 注意点 |
|---|---|---|
| `sched_getscheduler()` / `sched_setscheduler()` | policyと `sched_param` を取得・設定 | FIFO/RRではpriority 1〜99、通常policyではpriority 0 |
| `sched_getparam()` / `sched_setparam()` | policyを保ったまま `sched_param` を取得・設定 | RT priority設定には権限等の条件がある |
| `sched_get_priority_min()` / `sched_get_priority_max()` | policyごとのpriority範囲を取得 | LinuxのFIFO/RRは通常1〜99、ほかのpolicyは0 |
| `sched_rr_get_interval()` | `SCHED_RR` のquantumを取得 | `SCHED_FIFO`にはRR quantumがない |
| `sched_yield()` | 呼び出しtaskが実行機会を自発的に譲る | 通常policyでの公平性制御手段として安易に使わない |
| `sched_getattr()` / `sched_setattr()` | policyと拡張属性を取得・設定 | Linux固有。`SCHED_DEADLINE` のruntime/deadline/periodなどを扱う |

policyを設定するAPIは、無効なpriorityやpolicyの組合せを拒否します。RT policyへの切り替えや高priorityの設定は、通常 `CAP_SYS_NICE` などの権限が必要です。`SCHED_DEADLINE` は単なるRT priority設定ではなく、runtime/deadline/period、bandwidth admission control、CPU affinityの制約が関係します。

glibcは `sched_setscheduler()` などのwrapperを提供しますが、`sched_setattr()` / `sched_getattr()` はglibc wrapperがない環境があり、`syscall()` と `struct sched_attr` を用いるLinux固有の呼び出しが必要です。可搬性を重視するコードではその差を明示し、kernel/UAPI headerとman pageで構造体sizeやflagsを確認します。

`sched_yield()` は現在の実行可能状態を保ったまま実行権を譲る操作ですが、次にどのtaskがいつ実行されるかを保証しません。特に `SCHED_OTHER` で繰り返し呼び出して同期や公平性を実装するのは適切でない場合があります。RT taskではFIFO/RRの同一priority内の挙動を理解したうえで使います。

**ソースコードの入口**

- syscall共通処理とpolicy変更: `kernel/sched/core.c`
- sched UAPI構造体: `include/uapi/linux/sched/types.h`
- nice値とpriority定義: `include/linux/sched/prio.h`
- affinityとCPU mask: `kernel/sched/core.c`、`include/linux/sched/affinity.h`
- RT classとRR quantum: `kernel/sched/rt.c`
- deadline属性の検証: `kernel/sched/deadline.c`
- user-visible APIの説明: `Documentation/scheduler/`、各system callのman page

### 7.6で区別しておきたいこと

- nice値はfair class内の相対的な重みであり、RT priorityやdeadlineとは別の尺度
- Linux schedulerの実行単位はthread/taskであり、APIやlibcが「process」と呼ぶ対象との範囲を確認する
- CPU affinityは実行可能CPUの制約であって、CPU予約や実行時間保証ではない
- `SCHED_RR` のquantumと通常taskのCPU shareを混同しない
- `sched_yield()` は実行権を譲るAPIであり、待機・同期や公平性を保証しない
- RT/deadline policyはシステム全体へ影響し、権限・bandwidth・admission controlと運用条件を確認する

## 第7章のまとめ

Linux 6.18 LTSのスケジューラは、CPUごとのrunqueueと複数のscheduling classを使い、taskのpolicy・優先度・実行会計・affinity・CPU topologyを考慮して実行対象を選びます。通常taskのfair scheduling、FIFO/RRのRT scheduling、EDF/CBSに基づくdeadline schedulingは別の規則で動きます。

第7章を通して、次のつながりを意識します。

- **誰をどの規則で扱うか**: policyとscheduling class
- **どこに実行候補があるか**: CPUごとのrunqueueとclass固有queue
- **いつ実行機会を再評価するか**: tick、wake-up、sleep、need-resched、schedule
- **CPU間でどう配置するか**: affinity、scheduler domain、load balancing、task migration
- **ユーザー空間から何を指定できるか**: nice、scheduler API、affinity、deadline attributes

原文の2.6 O(1) schedulerにある動的優先度、active/expired array、固定クォンタム、旧関数名は当時の実装理解として参照し、Linux 6.18の実際の処理は `kernel/sched/core.c` と各class実装を起点に追います。

## Linux 6.18 LTSのソースを追う入口

- scheduler共通処理: `kernel/sched/core.c`
- fair classと通常task: `kernel/sched/fair.c`
- RT class: `kernel/sched/rt.c`
- deadline class: `kernel/sched/deadline.c`
- task構造とスケジューリング属性: `include/linux/sched.h`
- EEVDF設計資料: `Documentation/scheduler/sched-eevdf.rst`
- deadline scheduling設計資料: `Documentation/scheduler/sched-deadline.rst`
