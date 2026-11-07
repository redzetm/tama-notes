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

- 原文対応: 6.1「クロック回路とタイマ回路」および6.1.1〜6.1.6、6.2「時間管理の仕組み」および6.2.1〜6.2.3
- 今回の主題: 時刻を測るclocksource、期限にイベントを起こすclockevent、timekeeping、jiffies、CPUごとのtickとNO_HZ
- 6.18 LTSの視点: 旧式の全体割り込み・CPU別割り込みという固定図ではなく、共通時間管理層と各CPUのclockevent、設定に応じたtick運用を追う
- 現在の進捗: 6.1、6.1.1〜6.1.6、6.2、6.2.1〜6.2.3を作成。6.3以降は未着手

## 6章の見取り図

| 原文の節 | 研究する主題 | 進捗 |
|---|---|---|
| 6.1、6.1.1〜6.1.6 | クロック回路とタイマ回路 | 作成済み |
| 6.2、6.2.1〜6.2.3 | 時間管理の仕組みとデータ構造 | 作成済み |
| 6.3 | 時刻と日付の更新 | 未着手 |
| 6.4、6.4.1〜6.4.4 | 統計情報、負荷、プロファイリング、watchdog | 未着手 |
| 6.5、6.5.1〜6.5.3 | カーネルタイマ、待機、短時間遅延 | 未着手 |
| 6.6、6.6.1〜6.6.4 | 時間管理関連のシステムコール | 未着手 |

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

## UmuOSでの観測上の注意

- `uname -r` でゲストが実行しているカーネル版を確認する
- `/sys/devices/system/clocksource/clocksource0/` が存在する場合は、利用中および選択可能なclocksourceを確認する
- `/proc/interrupts` はCPUごとの割り込み状況を観測する手掛かりだが、特定デバイスがclocksourceとして使われていることまでは示さない
- `/proc/timer_list` の内容はカーネル構成や権限に依存する。存在しない場合もある
- 仮想マシンでは、ゲストから見えるタイマデバイスが物理ホストの実デバイスと一致するとは限らない
