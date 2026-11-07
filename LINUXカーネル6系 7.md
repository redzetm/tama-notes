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

- 原文対応: 7.1「スケジューリング方針」、7.1.1「プロセスプリエンプション」、7.1.2「最適なクォンタムの長さ」
- 今回の主題: Linux 6.18のスケジューリングクラスとpolicy、通常taskの公平性、nice値、リアルタイム・deadline方針、再スケジュール要求、固定クォンタムという見方の限界
- 現在の進捗: 7.1、7.1.1、7.1.2を作成。次は7.2「スケジューリングアルゴリズム」を節単位で扱う

## 7章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 7.1、7.1.1〜7.1.2 | 方針、プリエンプション、実行時間配分 | 作成済み |
| 7.2、7.2.1〜7.2.2 | 通常taskとリアルタイムtaskのスケジューリング | 未着手 |
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

## Linux 6.18 LTSのソースを追う入口

- scheduler共通処理: `kernel/sched/core.c`
- fair classと通常task: `kernel/sched/fair.c`
- RT class: `kernel/sched/rt.c`
- deadline class: `kernel/sched/deadline.c`
- task構造とスケジューリング属性: `include/linux/sched.h`
- EEVDF設計資料: `Documentation/scheduler/sched-eevdf.rst`

