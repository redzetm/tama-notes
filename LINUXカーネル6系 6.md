---
title: "LINUXカーネル6系の研究６ 時間管理"
---

# LINUXカーネル6系の研究６ 時間管理

このノートでは、Linux 6.18 LTS が時間をどのように測り、期限をどのように検出し、カーネルやユーザー空間へ時間情報を提供するかを研究します。
『詳解 Linux カーネル 第三版』の第6章は、PC/AT互換機のハードウェアを入口にしていますが、説明の中心は Linux 6.18 LTS の時間管理の構造に置きます。

時間管理では、「現在時刻を知ること」「一定時間が経過したことを検出すること」「期限に応じて処理を起こすこと」を区別する必要があります。
これらは関連しますが、同じハードウェアや同じカーネル機構がすべてを担うわけではありません。
まずハードウェアの役割を整理し、その後、カーネルの時間基準、タイマ、統計情報、ユーザー向けインターフェースへ進みます。

## UmuOS との接続

UmuOS-0.1.7-base-stable は Linux 6.18.1 を QEMU/KVM 上で動かす観測用環境です。
ゲストの時計源・クロックイベントデバイスの選択や時間管理は、ゲスト Linux カーネルと仮想ハードウェアの組み合わせで決まります。
ホストの時計、QEMU が提示する仮想デバイス、ゲストの Linux が公開する時刻は、関連はしますが同一の層ではありません。

起動後は `clocksource` の sysfs 表示や `/proc/timer_list`、`/proc/interrupts` などが調査の手掛かりになります。
ただし、表示されるデバイスや割り込み数は、カーネル構成、仮想マシン設定、実行中の負荷によって変わります。
観測値だけからハードウェアの実装やカーネル内部の全経路を断定せず、カーネル構成とソースコードも照合します。

## 今回扱う範囲

- 原文対応: 6.1「クロック回路とタイマ回路」および6.1.1〜6.1.6、6.2「時間管理の仕組み」および6.2.1〜6.2.3、6.3「時刻と日付の更新」、6.4「統計情報の更新」および6.4.1〜6.4.4、6.5「ソフトウェアタイマと遅延関数」および6.5.1〜6.5.3、6.6「時間管理関連のシステムコール」および6.6.1〜6.6.4
- 今回の主題: 時刻を測るclocksource、期限にイベントを起こすclockevent、timekeeping、jiffies、CPUごとのtickとNO_HZ、時刻補正、CPU時間会計、load average、perf、lockup watchdog、timer wheel・hrtimer・sleep・busy-waitの使い分け、ユーザー向け時刻・タイマAPI
- 6.18 LTSの視点: 旧式の全体割り込み・CPU別割り込みという固定図ではなく、共通時間管理層と各CPUのclockevent、設定に応じたtick運用を追う
- 現在の進捗: 6.1、6.1.1〜6.1.6、6.2、6.2.1〜6.2.3、6.3、6.4、6.4.1〜6.4.4、6.5、6.5.1〜6.5.3、6.6、6.6.1〜6.6.4を作成。第6章の全節を作成済み

## 6章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 6.1、6.1.1〜6.1.6 | クロック回路とタイマ回路 | 作成済み |
| 6.2、6.2.1〜6.2.3 | 時間管理の仕組みとデータ構造 | 作成済み |
| 6.3 | 時刻と日付の更新 | 作成済み |
| 6.4、6.4.1〜6.4.4 | 統計情報、負荷、プロファイリング、watchdog | 作成済み |
| 6.5、6.5.1〜6.5.3 | カーネルタイマ、待機、短時間遅延 | 作成済み |
| 6.6、6.6.1〜6.6.4 | 時間管理関連のシステムコール | 作成済み |

## 6章を読むための基本モデル

Linuxの時間管理を追うときは、次の二つの役割を分けます。

- **clocksource（時計源）**: カウンタを読み出して、時間の経過を測るための基準
- **clockevent device（クロックイベントデバイス）**: 指定した期限に割り込みなどのイベントを発生させる仕組み

```text
経過時間を測る                                  期限に処理を起こす
clocksource                                    clockevent device
    |                                               |
    +------------------+----------------------------+
                       v
         Linux共通の時間管理・tick・タイマ機構
                       |
              時刻の読み出し / 期限処理
```

ハードウェアによってはカウンタと割り込み機能の両方を備えますが、Linux内部では「時間を測る役割」と「イベントを発生させる役割」を別の抽象として扱います。
また、すべてのシステムで同じデバイスが選ばれるわけではありません。
このほか、スケジューラなどで使う `sched_clock()` や、短い遅延の測定に使うdelay timerもあります。
これらは関連する時間機構ですが、clocksourceと同じ目的・保証を持つものではありません。

## 6.1 クロック回路とタイマ回路

原文の6.1は、PCで利用されてきた時計・タイマ回路を説明します。
Linux 6.18 LTSを読む際には、デバイスの名称をそのまま時間管理の全体像とみなさず、それぞれが時計源なのか、イベント発生源なのか、あるいは起動時の時刻保持を担うのかを整理します。

### 6.1.1 リアルタイムクロック（RTC）

RTC（Real-Time Clock）は、電源断中も電池などで動作し、カレンダー時刻を保持するためのデバイスです。
Linuxは起動時などにRTCから時刻を読み取り、システム時刻の初期化に利用できます。
RTCは通常、システム稼働中の高精度な経過時間測定や、短い期限を頻繁に通知するための中心的な仕組みではありません。

LinuxではRTCを共通のRTCクラスとして扱い、個別デバイスドライバがハードウェア固有の操作を担当します。
PC互換機のCMOS RTCはその一例ですが、Linuxがすべての環境で特定のI/Oポートを直接操作するという理解は避けます。
仮想マシンでは、ゲストが見るRTCも仮想デバイスとして提供される場合があります。

**見る観点**

- RTCが保持するカレンダー時刻と、カーネル内部で経過時間を測る基準は別の役割
- 起動時のシステム時刻設定と、稼働中の時刻補正・タイマ処理を区別する
- RTCデバイスの操作はRTCクラスと個別ドライバを通る

**ソースコードの入口**

- RTC共通層: `drivers/rtc/`
- PC互換RTCのドライバ例: `drivers/rtc/rtc-cmos.c`

### 6.1.2 タイムスタンプカウンタ（TSC）

TSC（Time Stamp Counter）は、x86 CPUが提供するカウンタです。
LinuxはTSCを読み出し、条件が適切なら高分解能のclocksourceとして利用できます。
ただし、TSCの値を時間へ換算するには、その周波数と安定性を把握する必要があります。

現代のCPUでは、TSCの周波数がコアの動作周波数に連動しない invariant TSC が広く利用されていますが、すべてのCPUや仮想化環境で同じ保証があるとは限りません。
CPU間でカウンタが同期しているか、実行中に周波数やオフセットが変化しないか、仮想マシンの移行時にも基準を保てるかなどを確認する必要があります。
したがって「TSCはCPUクロックごとに必ず増える」「常に最も正確」という説明を一般化してはいけません。

TSCは時間を測るclocksourceとしての役割が中心です。
指定期限に割り込みを発生させるclockeventの役割とは区別します。

**ソースコードの入口**

- x86 TSCの検出・較正・利用: `arch/x86/kernel/tsc.c`
- clocksourceの共通管理: `kernel/time/clocksource.c`

### 6.1.3 プログラマブルインターバルタイマ（PIT）

PIT（Programmable Interval Timer）は、PC互換機で長く使われてきた周期割り込み用タイマです。
従来のシステムでは、PITの周期割り込みがカーネルのtickを刻む代表的な方法でした。
しかしLinux 6.18 LTSの時間管理を「PIT割り込みが一定周波数で全処理を駆動する仕組み」と捉えるのは適切ではありません。

Linuxはclockevent deviceの抽象を通じて、ハードウェアに期限を設定し、必要なイベントを発生させます。
利用するデバイスはプラットフォームやカーネル構成によって異なり、PITは主に旧式環境や代替手段として位置付けられます。
また、カーネルの `HZ` は基本的なtick設定に関わりますが、あらゆるタイマの精度や、割り込みが常に一定周波数で発生することを意味しません。

**ソースコードの入口**

- i8253/PIT clockeventの実装: `drivers/clocksource/i8253.c`
- tickとclockeventの共通処理: `kernel/time/tick-common.c`

### 6.1.4 CPUローカルタイマ

CPUローカルタイマは、特定のCPUへイベントを通知するタイマです。
x86ではローカルAPICタイマが代表例で、各CPUに対して個別のclockevent deviceとして働きます。
グローバルなタイマ割り込みがシステム全体に共通する通知を担うのに対し、ローカルタイマはCPUごとの処理を起動できます。

Linux 6.18 LTSでは、ローカルAPICタイマを含むデバイスがclockeventの仕組みに接続され、tick処理や高分解能タイマなどの要求に応じて利用されます。
ただし、ローカルAPICタイマを設定することと、そのCPUで常に固定周期の割り込みを発生させることは同じではありません。
tickless idleなどの動作では、不要な周期tickを抑え、次に必要な期限に合わせる構成があります。

**ソースコードの入口**

- x86 local APICタイマ: `arch/x86/kernel/apic/apic.c`
- clockevent deviceの共通定義: `include/linux/clockchips.h`
- tick/nohzの処理: `kernel/time/tick-*.c`

### 6.1.5 高精度イベントタイマ（HPET）

HPET（High Precision Event Timer）は、カウンタと比較器を備え、時刻測定やイベント発生に利用可能なハードウェアです。
機能上はclocksourceやclockeventとして使える場合がありますが、Linux 6.18 LTSがHPETを常に選択する、あるいは他のタイマより常に優先するとは限りません。

実際の選択では、ファームウェアによる記述、ハードウェアの機能・信頼性、カーネルの設定、ほかのデバイスの利用可能性などが関係します。
TSCやローカルAPICタイマが適切に使えるシステムでは、HPETがすべての時間処理を担うわけではありません。
デバイス名だけで「高精度」と決めつけず、どの役割に選ばれたかを確認します。

**ソースコードの入口**

- HPET関連のx86実装: `arch/x86/kernel/hpet.c`
- ACPI PMタイマ: `drivers/clocksource/acpi_pm.c`
- clocksource / clockeventの共通層: `kernel/time/clocksource.c`、`kernel/time/clockevents.c`

### 6.1.6 ACPI電源管理タイマ（ACPI PMタイマ）

ACPI PMタイマ（ACPI Power Management Timer、PMTMR）は、ACPIプラットフォームから利用できるカウンタです。
CPUの動作周波数変化に依存しない時間基準として利用できる場合があり、TSCの較正や代替clocksourceなど、用途はシステム構成によって異なります。

PMタイマはカウンタ値を読み取るための時計源であり、単独で各CPUへ任意の期限割り込みを提供するものではありません。
また、読み取りコストやカウンタ幅、プラットフォームの実装状況なども利用判断に影響します。
そのため「HPETがなければ必ずPMタイマ、それもなければTSC」という固定順位として理解するのではなく、Linuxが検出・評価した候補の一つとして捉えます。

### 6.1を通じて整理するデバイスの役割

| デバイス | 主な役割 | Linux 6.18 LTSでの見方 |
|---|---|---|
| RTC | 電源断中を含むカレンダー時刻の保持 | 起動時の時刻取得など。稼働中の高分解能時計とは別 |
| TSC | CPUカウンタによる経過時間測定 | 条件を満たせばclocksource。安定性・同期性の確認が必要 |
| PIT | 旧式PCの周期イベント | clockeventの候補・代替手段。全環境の基本tick源ではない |
| ローカルAPICタイマ | CPUごとのイベント通知 | CPU単位のclockeventとして利用 |
| HPET | カウンタと比較器による測定・イベント | 利用可能な機能の一つ。常に選択されるとは限らない |
| ACPI PMタイマ | ACPIプラットフォームのカウンタ | clocksource候補や較正用途など。用途は環境依存 |

この節で固定したい区別は、**時刻を測る時計源**と**期限にイベントを起こすデバイス**、そして**カレンダー時刻を保持するRTC**の三つです。
次の6.2では、Linuxがこれらのハードウェアを共通の時間管理機構へどう接続し、システム時刻やタイマ処理を組み立てるかを追います。

## 6.2 時間管理の仕組み

時間管理は、一つの周期割り込みが「時刻」「プロセス実行時間」「タイマ期限」をすべて直接決める仕組みではありません。
Linux 6.18 LTSでは、時計源のカウンタを基準に時間を管理し、clockevent deviceが必要な時点で割り込みを発生させ、tick・タイマ・スケジューラなどの処理へつなぎます。

```text
clocksourceのカウンタ ──> timekeeping ──> realtime / monotonic等の時刻
                                 ^
                                 | 必要な時点で時間管理を進める
clockevent device ──> tick / hrtimer / scheduler / CPUごとの処理
                                 |
                                 +──> 必要な期限がなければtickを抑止できる（NO_HZ）
```

ここで「tick」はカーネル内部の時間単位・定期処理の基準を指し、必ず一定周期のハードウェア割り込みが全CPUで続くという意味ではありません。
Linuxは周期tickを使う構成に加え、idle CPUのtickを抑止するNO_HZ_IDLEや、条件を満たすCPUでtickをより広く抑止するNO_HZ_FULLを備えます。
そのため、時間の経過を数えるclocksource、イベントを起こすclockevent、スケジューラ向けの `sched_clock()` は、関連していても別の役割として読みます。

### 6.2.1 時間管理のデータ構造

原文の `timer_opts`、`cur_timer`、`xtime` は、Linux 2.6当時の実装を説明するための名前です。
Linux 6.18 LTSでは、時計源・イベントデバイス・timekeepingの共通層に分かれており、これらの旧データ構造を現行の実装名として探すのではなく、担っていた役割を現在の構造へ対応付けます。

#### clocksourceとtimekeeping

`struct clocksource` は、単調に進むハードウェアカウンタなどを、Linuxが時間の基準として利用するための記述です。
timekeeping層は、カウンタの前回値・変換係数などを使って経過時間をナノ秒へ換算し、壁時計時刻や単調時刻などの基準を管理します。
現在時刻の読み出しは、カウンタを読むたびにRTCへ問い合わせるのではなく、この基準とカウンタの差分から得ます。

内部状態は `struct timekeeper` を中心に保持され、読み取りと更新の整合性はシーケンスカウンタなどを使う経路で守られます。
原文の `xtime` や `wall_to_monotonic` は、現在のカーネルに同名の主要な時刻変数がそのまま存在するという意味ではありません。
現行コードでは、`ktime_get_real_ts64()`、`ktime_get_ts64()` などのtimekeeping APIを入口にし、必要な時刻基準を選びます。

時刻基準は用途ごとに異なります。

- **realtime**: UTCの壁時計時刻。管理者や時刻同期機構によって補正・変更され得る
- **monotonic**: 起動後に進み続ける基準。壁時計の変更で過去へ戻らず、通常の `CLOCK_MONOTONIC` はサスペンド中の時間を含めない
- **boottime**: monotonicと同様に単調増加し、サスペンド中の経過時間も含む基準

ユーザー空間の `CLOCK_REALTIME`、`CLOCK_MONOTONIC`、`CLOCK_BOOTTIME` は、こうした異なる用途を表します。
これらの差は6.6の時計関連インターフェースを読む際にも重要です。

#### jiffiesとHZ

`jiffies` は、カーネルのtickに基づく経過時間を表すカウンタです。
カーネル内部では期限の管理などに使われますが、ナノ秒精度の時刻そのものではありません。
`HZ` はカーネル構成における基本tick頻度を示す値であり、実際の割り込みが常にその周波数で全CPUに届くことや、時刻読み出しの分解能を直接保証するものではありません。
高分解能タイマやtick抑止が有効な環境では、固定周期tickだけを前提とした理解は特に不十分です。

`jiffies` は型幅に応じて循環するため、時刻比較には通常の大小比較ではなく `time_after()`、`time_before()` などを使います。
起動後の長時間を扱う場合には、`jiffies_64` と `get_jiffies_64()` も関係します。
32bit環境では64bit値の読み取りが単一操作で完了するとは限らないため、カーネルの提供する読み取り方法を使います。

```c
if (time_after_eq(jiffies, deadline))
        handle_expired_deadline();
```

この比較方法は、`deadline` と `jiffies` の差が比較可能な範囲にあることを前提とします。
期限を手作業で通常の整数として比較すると、カウンタが循環した際に誤判定する可能性があります。

#### clockeventとCPUごとのデバイス

`struct clock_event_device` は、Linuxが期限を設定してイベントを発生させるデバイスを表します。
システム全体で一つのデバイスを共有する構成も、CPUごとのローカルデバイスを使う構成もあり、デバイス能力に応じて周期動作または単発の期限設定を行います。
ハードウェア割り込みを起こすclockeventは、時間を測るclocksourceとは独立した役割です。
同じハードウェアが両方を支えることはありますが、一対一対応ではありません。

スケジューラなどが高速に使う `sched_clock()` も別の時間基準です。
clocksourceと同じカウンタを利用する場合はありますが、clocksource APIやユーザー向け時刻の代替として扱うものではありません。

**ソースコードの入口**

- timekeeping内部状態と読み取りAPI: `kernel/time/timekeeping.c`、`include/linux/timekeeper_internal.h`、`include/linux/timekeeping.h`
- clocksource: `include/linux/clocksource.h`、`kernel/time/clocksource.c`
- clockevent: `include/linux/clockchips.h`、`kernel/time/clockevents.c`
- jiffiesと循環対応マクロ: `include/linux/jiffies.h`
- tickとNO_HZ: `kernel/time/tick-common.c`、`kernel/time/tick-sched.c`

### 6.2.2 単一CPUシステムにおける時間管理

原文の単一CPUモデルでは、PITまたはHPETのIRQ 0から時間関連処理が始まります。
Linux 6.18 LTSでは、単一CPUであっても特定のIRQ番号やPIT/HPETを前提にせず、選択されたclockevent deviceと共通のtick・timekeeping処理を追います。

起動時には、おおむね次の関係が構築されます。

1. 起動時の時刻情報などをもとに、システムのrealtime基準を初期化する
2. 利用可能なclocksourceを登録・評価し、timekeepingが経過時間を換算できるようにする
3. clockevent deviceを登録し、tickや期限処理を起動できるようにする
4. tickと高分解能タイマの設定に応じて、定期イベントまたは次の期限を設定する

この流れは、原文の `time_init()`、`select_timer()`、`timer_interrupt()` といった関数列をLinux 6.18の同名関数へ読み替えるものではありません。
初期化の細部はアーキテクチャとデバイスによって異なり、共通層とx86固有コードを行き来して確認します。

周期tickが発生した場合は、そのイベントを契機として時間管理やtickに関連する処理が進みます。
一方、時刻を読み出すだけなら、clocksourceのカウンタとtimekeepingの保存状態から補間できるため、各読み出しごとに割り込みを待つ必要はありません。
また、CPUがidleに入ると、NO_HZ_IDLEなどの条件により定期tickを抑止し、次の期限に合わせてclockeventを設定する場合があります。
したがって「tick割り込みの回数＝実時間の経過を知る唯一の手段」ではありません。

### 6.2.3 SMPシステムにおける時間管理

SMPでは、システム共通の時刻基準とCPUごとに進む処理を分けて考えます。
Linux 6.18 LTSでは、システム全体のtimekeeping状態を共通層が管理する一方、スケジューラtickやCPU単位の処理には各CPUに関連付けられたclockeventが使われることがあります。
実際の割り込み経路はプラットフォームやデバイス構成に依存します。

そのため、原文の「グローバルタイマ割り込みは全体処理、ローカルAPICタイマ割り込みはCPU別処理」という説明は、歴史的な構成を理解するモデルとして読みます。
現行Linuxで、全機種が固定的にこの二種類の割り込みを持ち、同じ関数分担で動くという意味ではありません。

**CPUローカルイベントが重要な理由**

- CPUごとのスケジューラtickやローカル処理を、それぞれのCPU上で起動できる
- SMPではCPUごとにclockevent deviceを持つ構成が一般的
- CPUがidleになった場合やローカルタイマを止める場合に、別CPUなどからイベントを配送するbroadcast機構が必要になることがある
- NO_HZ_IDLE / NO_HZ_FULLの適用範囲や条件によって、CPUごとのtick動作が異なる

単調増加する時刻基準を複数CPUから一貫して読めるかも重要です。
特にTSCのようなCPUごとのカウンタでは、CPU間の同期・安定性や仮想化時の特性が関係します。
Linuxはclocksourceの評価とtimekeepingの仕組みを通じてこれらを扱いますが、`sched_clock()` の性質やCPU単位の実行時間計測とは区別して調べます。

```text
                     共通のtimekeeping
                   realtime / monotonic
                           ^
                           |
        +------------------+------------------+
        |                                     |
  CPU 0 clockevent                       CPU 1 clockevent
  local tick / deadline                  local tick / deadline
        |                                     |
  scheduler / CPU処理                    scheduler / CPU処理

  共通時刻の読み取りと、CPUごとのイベント発生は別の役割
```

**ソースコードの入口**

- tickの共通処理と全体管理: `kernel/time/tick-common.c`
- CPUごとのtickとNO_HZ: `kernel/time/tick-sched.c`
- clockevent deviceの登録・選択: `kernel/time/clockevents.c`
- x86 local APIC timer: `arch/x86/kernel/apic/apic.c`
- 各CPUの実行時間・scheduler tick処理は6.4.1と7章で追う

### 6.2で区別しておきたいこと

- clocksourceは時間を測り、clockeventは期限にイベントを発生させる
- `jiffies` はtick基準のカウンタであり、高分解能のシステム時刻そのものではない
- `HZ` はtick設定に関係するが、常時一定周期の割り込みや時刻読み出し精度を保証しない
- `xtime`、`wall_to_monotonic`、`timer_opts` はLinux 2.6当時の説明用データ構造・名前であり、6.18の現行構造と混同しない
- SMPの時間管理を「必ずグローバルtickとローカルtickの二系統」と決めつけず、実機のclockevent選択とNO_HZ設定を確認する

## 6.3 時刻と日付の更新

原文では、周期タイマ割り込みで `jiffies` と `xtime` を更新し、割り込みを処理できなかった分は `wall_jiffies` との差から補うモデルを説明しています。
Linux 6.18 LTSでは、カーネルの時刻基準は `struct timekeeper` を中心に保持され、単純に「1 tickにつき一定のナノ秒を加算する」だけではありません。

timekeepingはclocksourceのカウンタ値と前回の基準値との差分から経過時間を求めます。
概念的には、カウンタの差をナノ秒へ変換し、保存している時刻基準を進めます。

```text
clocksourceの現在カウンタ
          |
          +-- 差分 = 現在値 - 前回値（カウンタ幅に応じて循環を考慮）
                    |
                    +-- 差分をnsへ変換（mult / shift）
                              |
                              +-- realtime / monotonic等の基準を進める
```

変換は概念的に次の形です。

```text
経過ns ≈ (カウンタ差分 × mult) >> shift
```

実際のコードでは、丸め誤差やカウンタ幅、整数演算の効率を考慮した内部表現を使います。
重要なのは、tick割り込みの回数だけで時刻を決めるのではなく、clocksourceの実際の経過カウントを利用することです。

### 時刻の読み取りとtimekeepingの更新

clocksourceが進んでいれば、最後にtimekeeping状態を更新した後の経過時間もカウンタから読み取れます。
時刻読み取りAPIは、その差分を補間して現在時刻を返すため、ユーザープロセスが時刻を読むたびにタイマ割り込みやRTCを待つ必要はありません。
timekeeping内部の更新処理は基準値を前進させ、読み取り側は必要に応じて最後の更新以降を補間します。

このため、割り込み処理が遅れたり、tickが抑止されたりしても、それだけで実時間が同じ量だけ失われるわけではありません。
ただし、clocksource自体の停止・不安定さや、長時間割り込み処理が滞ることによる他の副作用まで無くなるという意味ではありません。
「時刻を読む仕組み」と「期限を処理するイベント配送」は別の問題として調べます。

### 壁時計時刻と単調時刻

timekeepingは、同じ経過時間の基準から複数の時計を提供します。

- **`CLOCK_REALTIME`** はUTCベースの壁時計時刻で、管理者による設定や時刻同期によって前後に補正されることがある
- **`CLOCK_MONOTONIC`** は壁時計の変更により後退しない経過時間の基準で、通常の定義ではサスペンド中の時間を含まない
- **`CLOCK_BOOTTIME`** はサスペンド中の時間も含めて起動後の経過を表す

したがって、実行時間やタイムアウトの測定では、用途に応じた時計を選びます。
壁時計を基準に期限を作ると、NTP補正や管理者の時刻変更の影響を受け得ます。
`CLOCK_MONOTONIC` や `CLOCK_BOOTTIME` は、経過時間を扱う場面でその性質を考慮して使います。

### 時刻補正とクロックドリフト

実際のclocksourceの進みは、理想的なUTCと完全には一致しません。
NTPなどの時刻同期機構は観測した誤差をもとに、カーネルの時刻進行を調整します。
補正が小さい場合は、壁時計を一度に大きく飛ばすのではなく、時刻の進む速度を調整して誤差を徐々に縮める方法が使われます。
大きな誤差の初期修正などでは、ユーザー空間の時刻管理サービスがシステム時刻を設定する場合もあります。

これらの補正は、RTCやTSCなどのハードウェアカウンタ自体の周波数を直接変えることとは異なります。
カウンタを基準として、カーネルが公開する壁時計の進み方やオフセットを調整します。
時刻同期の詳細と `adjtimex()` 系APIは6.6.2で扱います。

### RTCとシステム時刻は別に管理される

RTCは起動時にシステム時刻を初期化するための情報源の一つですが、稼働中の `CLOCK_REALTIME` と同一の値を常時保持しているわけではありません。
外部時刻源と同期しているときは、Linuxが対応するRTCへ定期的にシステム時刻を書き戻す構成があります。PC互換RTCでは、時刻同期が有効な場合におよそ11分間隔でカーネルがRTCを更新する動作が知られています。
これはすべてのRTCデバイスや構成に一律に適用される保証ではなく、システム時刻の通常の進行とも区別します。

```text
起動時:
RTC / ファームウェア時刻 ──> システム時刻の初期値

稼働中:
clocksource ──> カーネルtimekeeping ──> CLOCK_REALTIME等
                        ^                    |
                        |                    +-- 時刻同期サービスが補正
                        |
             clockevent / tick等

同期が有効な構成:
システム時刻 ──> 対応するRTCへ定期的に書き戻す場合がある
```

### 6.3のソースコードを追う入口

- 時刻の保存・読み取り・更新: `kernel/time/timekeeping.c`
- 時刻読み取りAPI: `include/linux/timekeeping.h`
- timekeeperの内部データ構造: `include/linux/timekeeper_internal.h`
- NTP調整状態と補正処理: `kernel/time/ntp.c`
- tick側から時間管理を進める経路: `kernel/time/tick-common.c`
- RTCクラスとドライバ: `drivers/rtc/`

読み取り側から `ktime_get_real_ts64()` などのAPIを見て、次に `timekeeping.c` のclocksourceカウンタ読み取りとtimekeeperの基準値へ進むと、ユーザーに見える時刻がどう組み立てられるかを追えます。

### 6.3で区別しておきたいこと

- `jiffies` の増加と、ナノ秒単位で公開されるシステム時刻の進行は同じ概念ではない
- tickや割り込みを読み取りのたびに待つのではなく、clocksourceのカウンタ差分から経過時間を補間できる
- NTPなどによる時刻補正は、壁時計の進み方・オフセットの調整であり、ハードウェアカウンタそのものの書き換えではない
- `CLOCK_REALTIME` は補正・設定で変化し得るため、経過時間の測定には時計の性質を確認する
- RTCはシステム時刻の初期化や定期的な書き戻しに関係するが、稼働中のtimekeepingそのものではない

## 6.4 統計情報の更新

時間管理の仕組みは、時刻や期限だけでなく、タスクやCPUがどの状態でどれだけ動作したかを把握するためにも使われます。
原文では周期tickごとに実行中タスクの時間を加算する構成が中心ですが、Linux 6.18 LTSでは、統計の種類やカーネル構成によってtickベースの会計とより細かな時間会計が使い分けられます。

```text
実行中タスク / 割り込み / idle
              |
              +--> task・CPUのCPU時間会計 ------> /proc/<pid>/stat, /proc/stat
              |
              +--> runnable等の状態 ------------> load average
              |
              +--> PMU / software event sampling -> perf
              |
              +--> hrtimer / perf watchdog --------> soft / hard lockup検出
```

各統計の意味と更新方法は同一ではありません。
`/proc/stat` のCPU時間、個々のtaskのuser/system time、load average、perfのサンプルは、それぞれ別の量を表します。

### 6.4.1 ローカルCPUとタスクの統計情報

カーネルはユーザー空間のtaskやCPU単位の実行時間を会計します。
taskのuser timeとsystem timeは、ユーザーモードとカーネルモードで実行したCPU時間を表し、CPU単位の統計にはさらにidle、iowait、IRQ、softirq、stealなどの分類があります。
これらの値は `/proc/<pid>/stat`、`/proc/stat` や、会計設定に応じたインターフェースから観測できます。

原文の `update_process_times()` のようにtick割り込みで時間を加算するだけが唯一の方法ではありません。
Linuxにはtickベースの会計に加え、構成に応じたvirtual CPU time accountingやIRQ time accountingなどがあり、tickless動作とも両立させる必要があります。
また、IRQやsoftirqの実行時間をtaskのsystem timeとどう分けるかにも設定や会計経路が関係します。
したがって、統計値を読むときは、単位・対象（taskかCPUか）・分類方法を確認します。

`scheduler_tick()` はスケジューラの周期的な処理を起動する経路の一つですが、「タイムスライスをtickごとに単純減算する処理」と現代のスケジューラ全体を同一視しないようにします。
スケジューリングの判断と実行時間の会計は関連しますが、別の責務です。

**ソースコードの入口**

- task / CPU時間の会計: `kernel/sched/cputime.c`
- CPU統計の公開・集計: `kernel/sched/`、`fs/proc/stat.c`
- tickからスケジューラへの通知: `kernel/sched/core.c`
- CPU時間の分類名: `include/linux/kernel_stat.h`

### 6.4.2 システム負荷とload average

`uptime` や `/proc/loadavg` が示すload averageは、CPU使用率そのものではありません。
Linuxでは、実行中または実行可能なtaskと、uninterruptible sleep状態のtaskの数をもとにした負荷の移動平均を算出します。
そのため、CPUを実行中のtaskが少なくても、I/O完了などを待つuninterruptible状態のtaskが多ければload averageは高くなることがあります。

load averageは典型的には1分・5分・15分相当の時間窓で平滑化された値です。
値は活動中task数の平均に基づくため、CPUコア数で正規化した使用率ではありません。
単一CPU環境ではload average 1がCPUを完全に使うtask一つと直感的に近い場合がありますが、SMP環境では値をCPU数や実行可能task数と比較して解釈する必要があります。
I/O待ちが混ざるため、CPU飽和の判断にはCPU使用率や実行キューなども合わせて確認します。

現在の実装は、全CPUのtask数を毎tick単純に走査するのではなく、CPUごとの変化を集約し、NO_HZ環境で失われる周期サンプルも考慮します。
原文の `calc_load()` を読むときは、load averageの概念の入口とし、6.18の実装は `kernel/sched/loadavg.c` から追います。

**観測上の手掛かり**

- `/proc/loadavg`: load averageと実行中 / 総task数など
- `uptime`: load averageの表示
- `/proc/stat`: CPU時間分類
- `top` や `vmstat`: load averageとCPU/I/O状態を合わせて観測

### 6.4.3 カーネルコードのプロファイリング

原文の `readprofile` は、周期tick時の命令位置をサンプリングし、カーネル内のホットスポットを推定する方式を説明しています。
Linux 6.18 LTSで性能解析を行う際は、通常 `perf` とperf eventsを入口にします。
perfはCPUのperformance monitoring unit（PMU）を使うhardware eventや、ソフトウェアイベントをサンプリング・集計し、ユーザー空間とカーネル空間を含む解析を行えます。

サンプリング結果は確率的な観測です。高頻度サンプリングはオーバーヘッドを増やし、得られるサンプル数はCPU・イベント・カーネル構成などに左右されます。
測定対象とイベントを明示し、十分な期間・負荷で比較する必要があります。
perfの利用可否や収集できる情報は `perf_event_paranoid`、capability、カーネル構成などのアクセス制御にも影響されます。

```text
PMU / software event
        |
        +--> perf_event kernel subsystem
                    |
                    +--> sampling buffer / counters
                                  |
                                  +--> perf report等で集計・解析
```

`/proc/profile` や `readprofile` を現在の代表的なプロファイリング手段として前提にせず、目的に応じて `perf record` / `perf report`、tracepoint、ftraceなどを選びます。
このノートでは、ツールの具体的な測定手順ではなく、サンプルがどう生成されるかと、tick割り込みを使う旧式の説明との違いを押さえます。

**ソースコードの入口**

- perf event共通層: `kernel/events/core.c`
- x86 PMU: `arch/x86/events/`
- perf関連の利用者向け文書: `Documentation/admin-guide/perf/`
- `perf_event_paranoid` 等の設定: `/proc/sys/kernel/` と `Documentation/admin-guide/sysctl/kernel.rst`

### 6.4.4 lockup watchdog

lockup watchdogは、CPUがカーネル内で進行できなくなる状態を検出するための診断機構です。
Linux 6.18 LTSでは、soft lockup detectorとhard lockup detectorを区別します。

- **soft lockup**: CPUが長時間スケジューリング可能な状態へ戻らず、他のtaskを実行できない状態を検出する
- **hard lockup**: CPUが通常の割り込み処理すら進められない状態を検出する

現行実装では、soft lockup検出はhrtimerとwatchdog taskを使い、hard lockup検出は利用可能な構成でperfイベントなどを利用します。
x86ではNMIを用いたhard lockup検出が使われる場合がありますが、すべてのアーキテクチャ・仮想環境でNMI watchdogが使えるとは限りません。
原文の「ローカルAPICのNMIが毎tick発生し、APIC timer IRQのカウンタ増加を確認する」という方式を、現行の全実装に当てはめないことが重要です。

検出時に出るstack traceや警告は、停止箇所を調査する材料です。
watchdogがlockupを検出したことだけで、根本原因やデッドロックの原因箇所が確定するわけではありません。
panicさせるか警告を出して継続するか、どのCPUでwatchdogを有効にするかなどは、カーネル構成・起動引数・sysctl設定に依存します。
NO_HZ_FULLのCPUではtickless動作との関係から、標準のwatchdog対象CPUが制限される場合もあります。

**ソースコードと観測の入口**

- 共通watchdog実装: `kernel/watchdog.c`
- x86 NMI / hard lockup処理: `arch/x86/kernel/nmi.c`
- 関連設定: `/proc/sys/kernel/watchdog`、`watchdog_thresh`、`nmi_watchdog`、`watchdog_cpumask`
- 詳細: `Documentation/admin-guide/lockup-watchdogs.rst`

### 6.4で区別しておきたいこと

- CPU時間の会計、load average、perfサンプルは対象と意味が異なる
- load averageはCPU使用率ではなく、実行可能taskとuninterruptible taskを含む移動平均
- taskやCPUの時間会計は必ずしも一定周期のtickだけに依存しない
- `readprofile` / `/proc/profile` は歴史的な入口であり、現在の汎用的な性能解析はperfなどを使う
- NMI watchdogはhard lockup検出の一方式であり、soft lockup検出や全アーキテクチャの共通仕様と混同しない

## 6.5 ソフトウェアタイマと遅延関数

カーネルが指定した期限後に処理を起こす仕組みには、用途の異なるタイマがあります。
Linux 6.18 LTSでは、一般的なtimeoutにはtick基準のtimer wheel、細かな期限が必要な処理にはhrtimer、短いハードウェア待ちにはbusy-wait delay関数を使い分けます。
タイマが「期限に達した」ことと、コールバックが正確な期限の瞬間に実行されることは同じではありません。
スケジューリングや割り込み遅延、コールバック処理の都合により、実行時刻には遅れが生じ得ます。

```text
期限を登録する
    |
    +-- timer_list / timer wheel --> 一般的なtimeout。jiffies基準の粗い粒度
    |
    +-- hrtimer -------------------> 高分解能の期限。ktime_t基準
    |
    +-- delay関数 ----------------> 呼び出し側CPUをbusy-waitさせて短時間待つ
```

### 6.5.1 カーネルタイマ

`struct timer_list` は、比較的粗い期限処理やtimeoutに使うカーネルタイマです。
タイマは `jiffies` を基準に期限を登録し、期限が来るとコールバックを実行します。
Linux 6.18 LTSのtimer wheelはCPUごとに管理され、期限の遠さに応じた複数段の配列で、多数のtimeoutを効率よく扱います。
タイマを時刻順に走査する単一の巨大リストではありません。

現在のAPIでは、タイマを `timer_setup()` で初期化し、`mod_timer()` などで登録・期限変更します。
コールバックは `struct timer_list *` を受け取り、`from_timer()` / `timer_container_of()` などで包含するオブジェクトへ戻れます。
原文にある `function(unsigned long data)` や `init_timer()` は古いAPIです。

```c
struct device_state {
        struct timer_list timeout;
};

static void timeout_fn(struct timer_list *timer)
{
        struct device_state *state =
                container_of(timer, struct device_state, timeout);

        /* 非同期のtimeout処理。ここでsleepして待たない */
}

static void setup_timeout(struct device_state *state)
{
        timer_setup(&state->timeout, timeout_fn, 0);
        mod_timer(&state->timeout, jiffies + msecs_to_jiffies(100));
}
```

この例はAPI形状を示すための断片であり、実際のドライバではオブジェクト寿命・再登録・並行する停止処理を設計する必要があります。
短いtimeout用途のtimer wheelは、期限ちょうどに実行される精密タイマではありません。
期限の近いtimerは概ねtick粒度に基づいて処理され、CPU負荷やtick運用によってcallbackが遅れることがあります。

#### タイマcallbackと実行文脈

通常のtimer callbackはsoftirq系の実行文脈から呼ばれるため、通常のtaskのようにsleepして資源待ちすることはできません。
callbackは必要な状態を記録し、処理がsleepを必要とするならworkqueueなどへ引き継ぎます。
callback実行中に同じtimerを再登録する設計も可能ですが、停止・解放側との競合を明示的に扱わなければなりません。

#### 取消しとオブジェクト寿命

タイマを登録したままcallbackが参照するデータを解放すると、解放後にcallbackが実行される危険があります。
単に `timer_delete()` を呼ぶだけでは、既に別CPUで実行中のcallbackが終わるのを待つ用途には不十分です。
オブジェクトを解放する前に、必要な同期を取ってcallbackの実行と再登録を止めます。

- `timer_delete()` は登録済みtimerを取り消すが、実行中callbackの完了待ちはしない
- `timer_delete_sync()` は実行中callbackの完了も待つ。callbackと同じロックを保持したまま呼ぶとデッドロックし得る
- `timer_shutdown_sync()` は終了処理向けに、同期してcallbackを止め、以後の再armも防ぐ

どのAPIを使うかは、callbackが自分を再登録するか、他の経路が再登録するか、呼び出し側がどのロックを保持するかで決めます。
timer APIを使うコードでは、「タイマを消した」だけでなく「callbackがもう対象データに触れない」ことを確認してから解放します。

**ソースコードの入口**

- `struct timer_list` と登録・削除API: `include/linux/timer.h`
- timer wheelとcallback処理: `kernel/time/timer.c`
- callbackの遅延実行との関係: `kernel/softirq.c`
- 実行中callbackとの競合や終了順序: `Documentation/core-api/timer.rst`

### 6.5.2 動的タイマの適用例：nanosleep()

原文では `nanosleep()` を、`timer_list` と `schedule_timeout()` でtaskを起こす例として説明しています。
Linux 6.18 LTSでは、`nanosleep()` や `clock_nanosleep()` はhrtimerを使う経路を持ちます。実際に高分解能で動作するかは、カーネル構成やclockeventなどの能力にも依存します。
旧例は「期限を登録し、taskを休止し、期限またはsignalで起床する」という設計の理解に使い、現行の実装コードそのものとして読まないようにします。

```text
ユーザーtaskがnanosleepを要求
             |
             +--> 指定clockに対する期限を設定
             |
             +--> taskをsleep状態にし、schedulerへ実行権を渡す
                      |
          +-----------+----------------+
          |                            |
   期限満了で起床                signal等で中断
          |                            |
   sleep終了                      残り時間を返す場合がある
```

taskをsleepさせることで、その待ち時間中にCPUを別の実行可能taskへ割り当てられます。
これは `udelay()` のように呼び出し側CPUを回し続けるbusy-waitとは根本的に異なります。

ユーザー空間の `nanosleep()` は指定時間ちょうどにtaskが再実行されることを保証しません。
期限満了後に起床可能になっても、実際にCPUを得る時刻はスケジューラとシステム負荷にも依存します。
signalで中断された場合は `EINTR` と残り時間を扱う設計が必要です。

**ソースコードの入口**

- 高分解能sleepとhrtimer: `kernel/time/hrtimer.c`
- POSIX時計・timer・sleep syscall: `kernel/time/posix-timers.c`
- `schedule_timeout()` を使うjiffies単位のカーネル待機: `kernel/time/sleep_timeout.c`
- hrtimerの設計: `Documentation/timers/hrtimers.rst`

### 6.5.3 遅延関数

`udelay()`、`ndelay()`、`mdelay()` は、呼び出し側のCPUをbusy-waitさせて短い時間を待つ関数です。
デバイスのレジスタ操作後に数マイクロ秒だけ待つなど、sleepできない文脈で短時間待機が必要な場合に使います。
この間、CPUは他のtaskを実行できないため、長い時間の待機に使うとCPU時間を浪費します。

sleep可能な文脈で数ミリ秒以上待つ場合は、通常 `msleep()` や `usleep_range()` のようなsleep関数を検討します。
Linux 6.18では `fsleep()` もあり、待機時間に応じて短いbusy-wait、`usleep_range()`、`msleep()` の仕組みを選びます。
どのAPIが適切かは、呼び出し文脈、許容する誤差、CPUを占有できる時間で判断します。

| API | 待ち方 | 主な用途と制約 |
|---|---|---|
| `ndelay()` / `udelay()` | busy-wait | 極短時間。呼び出しCPUを占有する |
| `mdelay()` | busy-wait | 原則として避け、sleep可能ならsleep APIを優先する |
| `usleep_range()` | taskをsleep | マイクロ秒〜短い待機。範囲を渡してtimer coalescingの余地を与える |
| `msleep()` | taskをsleep | ミリ秒以上の待機。精密な期限実行ではない |
| `fsleep()` | 時間に応じて選択 | 使える文脈でbusy-waitとsleepを切り替える |

```text
短いデバイス待ち、sleep不可   -> udelay() / ndelay()
sleep可能な短時間待ち         -> usleep_range()
比較的長い待ち                -> msleep()
待ち時間に応じた汎用選択      -> fsleep()
```

busy-wait関数は較正値やアーキテクチャ依存の実装に基づく近似です。
高負荷・割り込み・仮想化などで指定時間を厳密な実時間として保証するものではありません。
また、`udelay()` に数ミリ秒以上を指定すると精度やオーバーフローの問題があり得るため、カーネルのAPI説明と呼び出し条件を確認します。

**ソースコードの入口**

- delay・sleep APIの宣言と `fsleep()`: `include/linux/delay.h`
- x86のdelay実装: `arch/x86/lib/delay.c`
- sleepとdelay APIの説明: `Documentation/timers/`、`Documentation/driver-api/`

### 6.5で区別しておきたいこと

- `timer_list` は一般的なtimeout向けであり、期限ちょうどの実行を保証する高精度タイマではない
- hrtimerは高分解能の期限管理に使われ、timer wheelとはデータ構造・用途が異なる
- callbackは非同期に実行され、通常のtimer callbackからsleepしてはならない
- callbackを含むデータの解放では、timerを取り消すだけでなく実行中callbackの完了と再armの有無を考慮する
- `nanosleep()` はtaskをsleepさせ、`udelay()` はCPUをbusy-waitさせる
- 高分解能の期限であっても、期限満了時にユーザーtaskが即座に実行される保証とは異なる

## 6.6 時間管理関連のシステムコール

ユーザー空間から時間を扱うAPIには、時刻を読む・設定するAPI、時計を調整するAPI、一定時間後に通知するタイマAPI、指定時間だけtaskを休止させるAPIがあります。
Linux 6.18 LTSでは、APIの名前から「必ずシステムコール命令でカーネルへ入る」とは限りません。
たとえば時刻の読み取りは、対応するアーキテクチャと環境ではvDSOがユーザー空間で高速に処理する場合があります。
一方、時刻の設定やタイマの作成など状態を変更する操作は、カーネルの検査・更新処理を必要とします。

```text
アプリケーションの時間要求
       |
       +--> 読み取り: clock_gettime() / time()
       |       +--> vDSOで処理できる場合がある
       |       +--> それ以外はシステムコール経路
       |
       +--> 設定・補正: clock_settime() / clock_adjtime()
       |       +--> 権限検査を経てtimekeepingへ
       |
       +--> 期限通知: setitimer() / timer_create() / timerfd
               +--> カーネルタイマ・hrtimer等からsignalやfd通知
```

### 6.6.1 `time()`、`gettimeofday()`、`clock_gettime()`

`time()` と `gettimeofday()` は、UTCの壁時計時刻を取得する歴史あるAPIです。
ただし、新しいプログラムで時計の性質を明示するには `clock_gettime()` を使います。
`gettimeofday()` のtimezone引数は現在のtimezone設定を提供する仕組みとして使うものではなく、timezone変換はユーザー空間のライブラリで扱います。

主な時計の選び方は次のとおりです。

- `CLOCK_REALTIME`: UTCの壁時計。ログや外部に保存する時刻に使うが、時刻設定・同期により前後へ変化し得る
- `CLOCK_MONOTONIC`: 壁時計の変更により後退しない経過時間。処理時間や相対timeoutの計測に使う
- `CLOCK_BOOTTIME`: サスペンド中も含めた起動後の経過時間
- `CLOCK_MONOTONIC_RAW`: NTP等の周波数補正を受けない生の経過時間基準。一般的なtimeout選択の代替として無条件に使うものではない
- `CLOCK_TAI`: leap secondを含むUTCとは別のTAI時刻基準。利用環境でTAI offsetが適切に管理されていることを確認する

```c
#define _POSIX_C_SOURCE 200809L
#include <time.h>

int main(void)
{
        struct timespec wall;
        struct timespec elapsed;

        if (clock_gettime(CLOCK_REALTIME, &wall) == -1)
                return 1;
        if (clock_gettime(CLOCK_MONOTONIC, &elapsed) == -1)
                return 1;

        /* wall: UTCの壁時計時刻、elapsed: 単調増加する経過時間基準 */
        return 0;
}
```

この例は時計の性質を示すための最小例です。エラー時には実際のプログラムでは `errno` を調べて処理します。
二つの値は異なる時計基準なので、相互に引き算して経過時間を求めてはいけません。
一つの処理の開始時と終了時を同じ `CLOCK_MONOTONIC` で取得し、その差を計算します。

`clock_getres()` はAPIが返す時計分解能を調べるもので、実際の処理がその周期でスケジュールされることや、その精度でcallbackが実行されることを保証しません。
また、`clock_gettime()` の呼び出しがvDSOで処理されるか、カーネルへのtrapを伴うかはアーキテクチャ・clock ID・実行環境に依存します。

**ソースコードの入口**

- ユーザー空間のvDSO実装・データ: `kernel/vdso/`、アーキテクチャ固有の `arch/*/entry/vdso/`
- timekeepingの読み取りAPI: `include/linux/timekeeping.h`、`kernel/time/timekeeping.c`
- システムコール側の時計処理: `kernel/time/posix-stubs.c`、`kernel/time/posix-timers.c`

### 6.6.2 `adjtimex()`、`clock_adjtime()`と時刻設定

時刻同期では、「壁時計を設定して大きく変更する」操作と、「進み方を調整して誤差をなめらかに補正する」操作を区別します。
NTPクライアントなどの時刻管理サービスは、外部時刻源との差を計測し、適切な補正をユーザー空間からカーネルへ依頼します。
カーネルは要求された補正状態を管理し、timekeepingに反映します。

`adjtimex()` はLinuxで利用できる従来APIで、時刻の位相・周波数補正や同期状態の取得・設定に使われます。
`clock_adjtime()` は時計IDを指定して調整するAPIです。
これらは `clock_settime()` のように現在時刻を単純に置き換える操作と目的が異なります。
内部の詳細は6.3で扱ったtimekeepingとNTP stateの更新へつながります。

`clock_settime(CLOCK_REALTIME, ...)` や `settimeofday()` は壁時計を設定するAPIです。
システム時刻を設定・調整する権限は通常 `CAP_SYS_TIME` に関係し、一般ユーザーが任意にシステム時刻を変更できるわけではありません。
壁時計の変更は `CLOCK_REALTIME` ベースの処理に影響しますが、`CLOCK_MONOTONIC` による経過時間の性質とは区別します。

これらのAPIを呼び出してもRTCが必ず同時に書き換わるわけではありません。
システム時刻とRTCの同期方針は、カーネル構成や時刻同期サービスによって別途決まります。
また、Linux固有の時刻調整APIに依存するプログラムは、可搬性を必要とする場合に注意が必要です。

**ソースコードの入口**

- `adjtimex()` / `clock_adjtime()` のシステムコール処理: `kernel/time/ntp.c`、`kernel/time/posix-timers.c`
- NTP調整状態: `kernel/time/ntp.c`
- 壁時計の設定とtimekeeping: `kernel/time/timekeeping.c`
- RTCへの同期機構: `kernel/time/ntp.c`、`drivers/rtc/`

### 6.6.3 `setitimer()`、`alarm()`とinterval timer

`setitimer()` は一つのプロセスに対して設定する従来型interval timerです。
三種類のtimerは、期限を測る時計基準と配送するsignalが異なります。

| 種類 | 期限の基準 | 期限満了時のsignal |
|---|---|---|
| `ITIMER_REAL` | 実経過時間 | `SIGALRM` |
| `ITIMER_VIRTUAL` | プロセスのuser CPU time | `SIGVTALRM` |
| `ITIMER_PROF` | user + system CPU time | `SIGPROF` |

`alarm()` は実経過時間を使った一回限りの `SIGALRM` 通知を設定する簡易APIです。
`alarm()` と `ITIMER_REAL` は同じプロセス内の実時間タイマ資源を共有するため、独立した二つのタイマとして併用できません。

このAPIは、タイマの期限が到来したときに同期的に処理を実行する仕組みではなく、プロセスへsignalを配送します。
signalはブロック・保留されることがあり、周期満了が複数回起きても、通常のsignal配送だけでは各満了を一回ずつ確実に数えられない場合があります。
また、POSIXでは `setitimer()` がobsolete扱いであり、新規設計では用途に応じてPOSIX timerやtimerfdなどを検討します。

Linux 6.18の実装は、原文の `task_struct` にあるtick単位のカウンタや、`ITIMER_REAL` 用 `timer_list` をそのまま使う説明とは異なります。
実時間タイマにはhrtimerが使われ、CPU時間ベースのtimerはCPU時間会計と連携します。

### 6.6.4 POSIX timerとPOSIX clock

POSIX clock APIは時計基準の読み取り・分解能取得などを行い、POSIX timer APIは指定したclockを基準に期限を設定します。
代表的なAPIには `clock_gettime()`、`clock_getres()`、`clock_nanosleep()`、`timer_create()`、`timer_settime()`、`timer_gettime()`、`timer_getoverrun()`、`timer_delete()` があります。

POSIX timerは `timer_create()` の `sigevent` 設定に応じて、signalなどで期限を通知できます。
timerはスレッドまたはプロセスへ通知先を設定でき、`CLOCK_REALTIME`、`CLOCK_MONOTONIC`、`CLOCK_BOOTTIME` など対応するclockを基準にできます。
利用できるclock IDや通知方式は、APIとカーネル構成に依存します。

期限満了時にユーザー空間がすぐ動くわけではありません。
カーネルがtimerを満了扱いにして通知を準備した後、signal配送やスケジューリングを経て、対象taskが実行されます。
周期timerの複数回満了がsignal配送の遅れなどでまとめられた場合には、`timer_getoverrun()` で追加の満了数を確認できる場合があります。
ただし、これは任意の期限ごとにアプリケーションコードが一回ずつ実行されたことを意味しません。

```text
POSIX clockを選ぶ
      |
timer_create(clockid, sigevent)
      |
timer_settime(絶対期限または相対期限、周期)
      |
期限満了 -- hrtimer等 --> signal / 指定された通知
                               |
                         ユーザーtaskが後で処理
```

ファイルディスクリプタを使ったevent loopでは、`timerfd_create()`、`timerfd_settime()` を使う選択肢もあります。
timerfdはPOSIX timerそのものではなく、timer期限をreadableなファイルディスクリプタとして通知し、`poll` / `epoll` と統合するLinux APIです。

**ソースコードの入口**

- POSIX clock・timer syscall: `kernel/time/posix-timers.c`
- interval timer: `kernel/time/itimer.c`
- timerのhrtimer基盤: `kernel/time/hrtimer.c`
- timerfd: `fs/timerfd.c`
- ユーザー空間のclock API: `include/linux/time.h`、`include/uapi/linux/time.h`

### 6.6で区別しておきたいこと

- 時刻取得APIはvDSOで処理される場合があり、API呼び出しとカーネルへのシステムコール進入は同義ではない
- 壁時計の取得・設定と、単調増加する経過時間の計測ではclock IDを使い分ける
- `adjtimex()` 系の補正と `clock_settime()` / `settimeofday()` の時刻設定は異なる操作
- `setitimer()` は従来型のsignal timerであり、Linux 6.18の内部実装を旧 `timer_list` の説明から推測しない
- POSIX timerやtimerfdの期限満了は通知の契機であり、ユーザー処理が期限時刻に必ず実行される保証ではない
- signal通知では満了回数とsignalの配送回数が一致しない場合があり、必要ならoverrunや別の通知方式を検討する

## Linux 6.18 LTSのソースを追う入口

- 時計源の選択・登録: `kernel/time/clocksource.c`
- 時刻管理の中核: `kernel/time/timekeeping.c`
- clockevent deviceの共通層: `kernel/time/clockevents.c`
- tick処理: `kernel/time/tick-common.c`、`kernel/time/tick-sched.c`
- x86 TSC: `arch/x86/kernel/tsc.c`
- x86 HPET: `arch/x86/kernel/hpet.c`
- x86 local APICタイマ: `arch/x86/kernel/apic/apic.c`
- i8253/PIT: `drivers/clocksource/i8253.c`
- RTCクラス: `drivers/rtc/`
- timer wheel: `kernel/time/timer.c`
- hrtimer: `kernel/time/hrtimer.c`
- delay / sleep API: `include/linux/delay.h`
- POSIX clock・timer: `kernel/time/posix-timers.c`
- interval timer: `kernel/time/itimer.c`
- timerfd: `fs/timerfd.c`

## UmuOSでの観測上の注意

- `uname -r` でゲストが実行しているカーネル版を確認する
- `/sys/devices/system/clocksource/clocksource0/` が存在する場合は、利用中および選択可能なclocksourceを確認する
- `/proc/interrupts` はCPUごとの割り込み状況を観測する手掛かりだが、特定デバイスがclocksourceとして使われていることまでは示さない
- `/proc/timer_list` の内容はカーネル構成や権限に依存する。存在しない場合もある
- 仮想マシンでは、ゲストから見えるタイマデバイスが物理ホストの実デバイスと一致するとは限らない
- タイマ期限の測定とcallbackが実行される時刻は別であり、負荷・割り込み遅延・taskスケジューリングの影響を受ける

## 第6章のまとめ

Linux 6.18 LTSの時間管理は、一つの周期割り込みや一つのタイマデバイスで全てを実現する構造ではありません。
clocksourceが経過時間の基準を与え、timekeepingが複数の時計を提供し、clockeventとtimer機構が期限を処理します。
その上に、CPU会計・load average・perf・watchdogといった統計・診断機能、ユーザー空間の時刻取得・タイマAPIが接続されます。

第6章を通して、次の役割を分けて理解します。

- **時刻の基準**: clocksourceとtimekeeping
- **期限の通知**: clockevent、timer wheel、hrtimer
- **時間の種類**: realtime、monotonic、boottimeなど
- **実行時間の会計・診断**: task/CPU統計、load average、perf、lockup watchdog
- **ユーザー空間API**: 時計の取得・設定・補正、interval timer、POSIX timer、timerfd

Linuxソースを調べるときは、デバイス固有の割り込み入口だけを追うのではなく、共通時間管理APIからclocksource・clockevent・タイマ基盤へ進み、必要な場合にアーキテクチャ固有コードへ降りていくと、現行の責務分担をつかみやすくなります。
