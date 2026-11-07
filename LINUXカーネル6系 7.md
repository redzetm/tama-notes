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

- 原文対応: 7.1「スケジューリング方針」、7.1.1「プロセスプリエンプション」、7.1.2「最適なクォンタムの長さ」、7.2「スケジューリングアルゴリズム」、7.2.1「従来型プロセスのスケジュール」、7.2.2「リアルタイムプロセスのスケジュール」
- 今回の主題: Linux 6.18のスケジューリングクラスとpolicy、通常taskの公平性、nice値、リアルタイム・deadline方針、再スケジュール要求、固定クォンタムという見方の限界、EEVDF・FIFO/RR・EDF/CBSの選択規則
- 現在の進捗: 7.1〜7.2.2を作成。次は7.3「スケジューラが使用するデータ構造」を節単位で扱う

## 7章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 7.1、7.1.1〜7.1.2 | 方針、プリエンプション、実行時間配分 | 作成済み |
| 7.2、7.2.1〜7.2.2 | 通常taskとリアルタイムtaskのスケジューリング | 作成済み |
| 7.3、7.3.1〜7.3.2 | 実行キューとtaskのスケジューリング情報 | 未着手 |
| 7.4、7.4.1〜7.4.4 | tick、起床、選択、scheduleの経路 | 未着手 |
| 7.5、7.5.1〜7.5.4 | SMPの負荷均衡とtask移動 | 未着手 |
| 7.6、7.6.1〜7.6.4 | nice、affinity、リアルタイム関連API | 未着手 |

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

## Linux 6.18 LTSのソースを追う入口

- scheduler共通処理: `kernel/sched/core.c`
- fair classと通常task: `kernel/sched/fair.c`
- RT class: `kernel/sched/rt.c`
- deadline class: `kernel/sched/deadline.c`
- task構造とスケジューリング属性: `include/linux/sched.h`
- EEVDF設計資料: `Documentation/scheduler/sched-eevdf.rst`
- deadline scheduling設計資料: `Documentation/scheduler/sched-deadline.rst`
