---
title: "LINUXカーネル6系の研究５ カーネルの同期処理"
---

# LINUXカーネル6系の研究５ カーネルの同期処理

このノートでは、Linux 6.18 LTS のカーネル内で複数の実行経路が共有データへアクセスするとき、どのような競合が起こり、何を同期すべきかを研究します。
古い本文は問題を見つける入口として使い、説明の中心は Linux 6.18 LTS の実行文脈、スケジューリング、同期プリミティブに置きます。

同期を考える際は、単に「同時に動くか」だけでなく、同じ CPU 上で割り込まれる可能性、別 CPU で並行実行される可能性、処理を待たせてよいかを見分けます。
そのため、ロックの種類を先に暗記するのではなく、共有データへ到達する実行経路と、その経路が動く文脈を特定するところから始めます。

## UmuOS との接続

UmuOS-0.1.7-base-stable は Linux 6.18.1 を QEMU/KVM 上で動かす観測用環境です。
同期プリミティブやスケジューラを実装する主体はゲストの Linux カーネルであり、UmuOS のユーザー空間スクリプトがカーネル内部のロックを直接管理するわけではありません。

`uname -r` で実行中カーネルの版を確認し、`/boot/config-$(uname -r)` が利用できれば `CONFIG_PREEMPT` などの構成も確認できます。
ただし、カーネル構成の表示だけでは、特定のデータがどのロックで保護されるかや、実際に競合が起きたことまでは分かりません。
ソースコード上のアクセス経路と、必要に応じて scheduler の trace event などの観測結果を照合します。

## 今回扱う範囲

- 原文対応: 5.1「カーネルは要求をどのように処理するか」、5.1.1「カーネル内プリエンプション」、5.1.2「同期の必要がある場合」、5.1.3「同期の必要がない場合」、5.2「同期プリミティブ」、5.2.1「CPUごとの変数」、5.2.2「アトミック操作」、5.2.3「最適化処理とメモリバリア」、5.2.4「スピンロック」、5.2.4.1「カーネル内プリエンプションを伴う spin_lock マクロ」、5.2.4.2「カーネル内プリエンプションを伴わない spin_lock マクロ」、5.2.4.3「spin_unlock マクロ」、5.2.5「読み書き用スピンロック」、5.2.5.1「読み込み用ロックの取得と解除」、5.2.5.2「書き出し用ロックの取得と解除」、5.2.6「順次ロック」、5.2.7「Read-Copy Update（RCU）」、5.2.8「セマフォ」、5.2.8.1「セマフォの取得と解放」、5.2.9「読み書き用セマフォ」、5.2.10「完了通知」、5.2.11「ローカル割り込み禁止」、5.2.12「遅延処理の禁止と許可」、5.3「カーネルデータアクセスの同期」導入、5.3.1「スピンロック、セマフォおよび割り込み禁止の選択」、5.3.1.1「例外がアクセスするデータの保護」
- 今回の主題: 実行文脈の入れ子、per-CPU データ、atomic / memory ordering、spinlock / rwlock / seqlock / RCU / semaphore / rw_semaphore / completion / local IRQ / bottom-half control の役割と実行文脈上の制約
- 6.18 LTS の視点: 各同期 API の保証を実行文脈とカーネル構成に結び付ける。`spinlock_t`、`raw_spinlock_t`、`rwlock_t`、`seqcount_t`、`seqlock_t`、RCU、`rw_semaphore` の役割と PREEMPT_RT による意味の違いを確認する
- 現在の進捗: 5.1、5.1.1〜5.1.3、5.2、5.2.1〜5.2.12、5.3導入、5.3.1、5.3.1.1を作成。続きは 5.3.1.2 以降を小節単位で追加する

## 5.1 カーネルは要求をどのように処理するか

カーネルの処理は一つの順番待ち行列を一つずつ消化する形ではありません。
ユーザータスクはシステムコールという明示的な入口からカーネルへ入り、x86-64 では通常 `SYSCALL` 命令が使われます。これはデバイス割り込みや CPU 例外とは区別して考えます。
そのほか、CPU の例外、デバイス割り込み、別 CPU 上のタスク実行が共有状態へ影響することもあります。
したがって、ある関数の実行中に別の処理が同じデータへ到達できるかを考えることが、同期設計の出発点です。

### 5.1.1 カーネル内プリエンプション

#### 「カーネルを実行中」と「割り込み文脈」は同じではない

Linux がカーネルコードを実行している場面には、少なくとも次のような異なる実行文脈があります。

- **プロセス文脈**: 現在のタスクがシステムコールやページフォルト処理などでカーネルコードを実行している。処理の性質と状態が許せば、待ち状態に入ってスケジュールされることがある
- **ハード IRQ 文脈**: デバイス割り込みへの応答として実行される。通常のタスクのようにその場で休止してロック待ちすることはできない
- **softirq などの遅延処理文脈**: 割り込みの即時処理から後続作業を引き継ぐ。実行方式は種類や設定によって異なるため、一律にプロセス文脈とみなせない

この区別は重要です。カーネル内プリエンプションは、タスクがプロセス文脈でカーネルコードを実行している間に、スケジューラが別タスクへ切り替えられるかという問題です。
ハード IRQ が実行中のタスクを中断することや、別 CPU が並行して動くこととは、別の軸です。

#### カーネル内プリエンプションが起こる条件

カーネル内プリエンプションが有効な構成では、プリエンプト可能な状態にあるタスクがカーネルコードの途中にいても、再スケジュール要求に応じて別タスクへ切り替わることがあります。
たとえば、割り込み処理が実行可能なタスクを起こし、そのタスクを先に実行すべきだとスケジューラが判断する場合です。
中断されたタスクのカーネルスタックと実行状態は保持され、後で再び選ばれたときに、カーネル内の続きから実行を再開します。

ただし、カーネル関数のどの命令の間でも無条件に切り替わるわけではありません。
ロック保持中など、プリエンプションを許してはならない区間ではプリエンプションが抑止されます。また、割り込み禁止区間など、実行状態に応じた制約もあります。
Linux 6.18 の実際の可否は、カーネルの preemption model、実行中の文脈、現在の preemption state を合わせて読み取る必要があります。

#### 計画的なスケジュールと強制的な切り替え

タスクが資源待ちなどで自らブロックし、スケジューラへ実行権を渡すのは、カーネル内プリエンプションの有無にかかわらず起こる計画的なスケジュールです。
一方、実行可能な別タスクを先に動かすため、現在のタスクがカーネルコードの途中でスケジューラに切り替えられるのが、ここでいうカーネル内プリエンプションです。

この切り替えはタスクの実行を中断・再開するものであり、二つのタスクが同じ CPU 上で同時に命令を実行するわけではありません。
同じ CPU 上での競合は、実行の切り替わりや割り込みによって共有データへのアクセスが交互に起きることで発生します。
別 CPU では、本当に並行してアクセスされる可能性があります。

```text
CPU 0: タスク A がカーネル関数を実行
                  |
                  +-- 再スケジュール可能な点で task B へ切り替え
                  |      task B が実行
                  |
                  +-- 後に task A が選ばれ、関数の続きから再開

CPU 1: 別タスクが同じ共有データへ並行してアクセスする可能性
```

#### 応答性と同期の関係

カーネル内プリエンプションは、カーネル処理が長く続いている間も、より優先度の高いタスクなどへ CPU を渡せるようにし、応答性を高めます。
しかし、切り替え可能な箇所が増えるほど、プロセス文脈のカーネルコードが共有データを更新している最中に、別タスクが同じデータへアクセスする可能性も検討しなければなりません。
応答性の改善とデータ保護は別々の設計課題ではなく、実行経路を正しく同期させる必要があります。

#### Linux 6.18 LTS のソースを追う入口

- preemption state と関連 API: `include/linux/preempt.h`
- スケジューラとタスク切り替えの流れ: `kernel/sched/core.c` の `schedule()`、`preempt_schedule()` 周辺
- x86 の低レベルなコンテキスト切り替え: `arch/x86/` 以下の scheduler / switch 実装
- カーネル構成で選択される preemption model: `kernel/Kconfig.preempt`

旧本文の `switch_to` や `thread_info` の説明は、当時の実装をそのまま Linux 6.18 の固定配置・全体像として読むのではなく、現在のスケジューラ経路とアーキテクチャ依存部分を分けて確認します。

### 5.1.2 同期の必要がある場合

共有データを複数の実行経路が読み書きし、その間に処理が切り替わったり、別 CPU で並行したりするなら、競合が起きないことを保証する必要があります。
たとえば、二つの経路が同じカウンタを単純に読み、増やし、書き戻すと、双方が同じ古い値を読み、片方の更新が失われる可能性があります。
個々の C 文が一見単純でも、複数のアクセスから成る更新全体が不可分とは限りません。

同期が必要かを考える際は、少なくとも次を確認します。

1. **誰がデータへアクセスするか**: タスク、ハード IRQ、softirq、workqueue、別 CPU など
2. **どの文脈で実行するか**: 待てるプロセス文脈か、待てない割り込み文脈か
3. **どのように重なり得るか**: 同一 CPU 上でのプリエンプション・割り込みか、SMP 上の並行実行か
4. **何を一貫させるか**: 単一の値か、複数フィールド間の不変条件か、オブジェクトの寿命か

```text
タスク A ─┐
          ├──> 共有状態 ──> 相互排他・寿命・順序の保証が必要か検討
IRQ handler ┘
      ↑
      └── 別 CPU 上のタスクや handler からもアクセスされる可能性
```

同じ CPU しか使わないように見えても、タスクが共有データを更新中に IRQ が入って同じデータを更新すれば競合します。
逆に、ローカル割り込みを禁止するだけでは別 CPU からの同時アクセスは止まりません。
競合の発生源を特定してから、後続の 5.2 で扱うアトミック操作、ロック、割り込み制御、RCU などから適切な仕組みを選びます。

### 5.1.3 同期の必要がない場合

同期を増やせば常に安全になるわけではありません。ロックの取得・解放にはコストがあり、待ち合わせによる遅延やデッドロックの可能性も生じます。
したがって、アクセス経路を制限して競合そのものを作らない設計が可能なら、それも有力な選択です。

たとえば、初期化が完了し、他の実行経路から安全に参照できる状態で公開された後に変更されないデータを複数の経路が読み取るだけなら、通常は書き込み競合を防ぐロックは必要ありません。
また、データの所有者を一つの実行経路に限定し、他の経路とはメッセージや適切な引き渡しで連携する設計なら、共有更新を減らせます。

CPU ごとのデータも、CPU 間の不要な共有を減らす手段です。ただし、それだけであらゆる同期が不要になるわけではありません。
同じ CPU の要素をタスクと IRQ の両方が触る場合、タスクの移動やプリエンプションにより要素の選択と利用がずれる場合、別 CPU から参照される場合は、追加の保護やアクセス規約が必要です。
CPU ごとのデータを使うときも、アクセスする文脈と CPU 固定の条件を確かめます。

「同期不要」と判断する根拠は、単にテストで競合が見えなかったことではなく、書き手・読み手・実行文脈・データ寿命を調べても同時更新や不整合が起こらないことです。
この判断を先に行うことで、必要な箇所だけに同期を置き、コードの動作と性能を両立させやすくなります。

#### この節で固定したいこと

- カーネル内プリエンプションは、タスク文脈でカーネルコードを実行中のタスクが別タスクへ切り替わる問題
- システムコール、CPU 例外、ハード IRQ は同じ種類の入口ではなく、実行文脈も一様ではない
- 同一 CPU 上の切り替わりと、SMP による CPU 間の並行実行は別々に検討する
- IRQ 文脈など、休止できない実行経路を通常のブロッキングロックで待たせることはできない
- 同期不要と判断するには、共有データの全アクセス経路と不変条件を確認する

## 5.2 同期プリミティブ

Linux 6.18 LTS には、共有状態の性質や実行文脈に応じた複数の同期手段があります。
CPU ごとに状態を分けて共有を減らす方法、単一の値の更新を不可分にする方法、ロックでクリティカルセクションを保護する方法、読み手と書き手の要件に合わせた RCU などは、同じ問題を同じ方法で解決するものではありません。

どの手段を選ぶかは、保護対象のデータと全アクセス経路を明らかにした後で判断します。
この節ではまず CPU ごとのデータを扱い、続く小節でアトミック操作、メモリ順序、ロックや待ち合わせを個別に見ていきます。
旧本文の一覧表にある名称や分類を、そのまま Linux 6.18 の API 一覧として使わず、実際の用途と制約を対応付けます。

### 5.2.1 CPU ごとの変数

#### CPU ごとに状態を分け、共有を減らす

CPU ごとの変数（per-CPU variable）は、各 CPU に対応する別々のデータ領域を用意する仕組みです。
全 CPU が一つのカウンタを更新する代わりに各 CPU が自分のカウンタを更新し、必要なときに集計すれば、更新時の CPU 間競合や共有キャッシュラインの移動を減らせます。

```c
DEFINE_PER_CPU(unsigned long, event_count);

static void record_event(void)
{
	this_cpu_inc(event_count);
}
```

この例では、`this_cpu_inc()` が現在実行中の CPU に対応する `event_count` を増やします。
全 CPU の合計が必要なら各 CPU の値を読む集計処理が必要ですが、その読み取りと更新が同時に起きるときの整合性は、用途に応じて別途考えます。
CPU ごとの値を置いただけで、全 CPU を通じた一つの厳密な合計値になるわけではありません。

#### 現在 CPU 用のアクセスと、一般のポインタ参照

Linux の per-CPU API には、静的な領域を宣言する `DEFINE_PER_CPU()`、CPU を指定して対応要素を選ぶ `per_cpu()`、動的に領域を確保する `alloc_percpu()` / `free_percpu()`、指定 CPU の要素へのポインタを得る `per_cpu_ptr()` などがあります。
現在 CPU の要素には `this_cpu_read()`、`this_cpu_write()`、`this_cpu_add()` などの `this_cpu_*()` 操作が用意されています。

```c
DEFINE_PER_CPU(unsigned long, bytes_seen);

static unsigned long current_cpu_bytes(void)
{
	return this_cpu_read(bytes_seen);
}
```

`this_cpu_*()` は現在 CPU の要素を操作する API です。対応アーキテクチャでは CPU 固有の基準アドレスを使ってアクセスし、操作ごとに CPU を移動しないよう明示的にプリエンプションを止めなくても、現在 CPU の値を扱えるよう設計されています。
これは、この API の呼び出しをまたぐコード全体で CPU が固定されるという意味でも、排他ロックを取るという意味でもありません。
また、個々の `this_cpu_*()` 操作が便利だからといって、複数のフィールドをまとめて一貫した状態として更新する処理まで自動的に保護されるわけではありません。
`__this_cpu_*()` 系は通常の `this_cpu_*()` と異なり、プリエンプションや割り込みとの競合を防ぐ保証がありません。呼び出し側で実行文脈と競合条件を満たす必要があります。

一方、`this_cpu_ptr()` などで通常のポインタを得て複数の操作に使う場合は、ポインタを得た CPU と利用中の CPU が変わらない条件が必要かを確認します。
CPU 間の移動を防ぐ API を使うケースでも、それはデータの排他ロックとは異なります。ローカル IRQ からの同時アクセスや、別 CPU からのリモートアクセスを自動的に排除するものではありません。

#### CPU ごとの変数でも同期が必要になる場合

CPU ごとの領域が分離するのは、各 CPU が自分の要素を使うという設計が守られている場合です。個々の `this_cpu_*()` 操作はその API の保証する範囲で使えますが、次のようなケースでは競合や不整合が残ります。

- `__this_cpu_*()` や通常のポインタ経由で、同じ CPU のタスク文脈と IRQ / softirq が同じ要素を更新する
- 複数の `this_cpu_*()` 呼び出しの途中で別の実行経路が入り、複数フィールド間の不変条件が崩れる
- 別 CPU が対象 CPU の要素へ書き込む
- CPU をまたいだ集計中に各 CPU の値が変化し、読み手が必要とする一貫性を満たさない

特に、CPU ごとの要素をリモートから書き換えると、対象 CPU が行うローカル更新と衝突する可能性があります。
他 CPU の状態を変更する必要があるなら、対象 CPU 上で更新を実行させる方法などを検討し、単に `per_cpu_ptr()` でポインタを得たことを排他の保証と取り違えないようにします。

また、per-CPU 領域が常に各 CPU 専用のキャッシュラインに完全分離されると考えるのも正確ではありません。
CPU ごとのデータは競合やキャッシュラインの移動を減らす設計に有効ですが、変数の配置、同一 CPU 上の非同期アクセス、集計の整合性といった問題は別に残ります。

#### Linux 6.18 LTS のソースを追う入口

- per-CPU 宣言、アクセス API: `include/linux/percpu.h`、`include/linux/percpu-defs.h`
- `this_cpu_*()` の説明とアーキテクチャ依存の実装: `Documentation/core-api/this_cpu_ops.rst`、`arch/*/include/asm/percpu.h`
- 動的 per-CPU 領域の管理: `mm/percpu.c`

読むときは、変数の宣言だけでなく、どの CPU の値を選ぶか、タスクが移動し得るか、IRQ など同一 CPU 上の別文脈も触るか、リモート書き込みがないかを確認します。

#### UmuOS での観察

UmuOS のゲスト上では、CPU 数を `nproc` や `/proc/cpuinfo` で確認できます。
ただし、ユーザー空間から見える CPU 数だけでは、カーネル内の特定の per-CPU 変数の値や、その更新時の同期条件は分かりません。
調査対象のコードとカーネル構成を確認し、必要な場合は trace やカーネル計測を使って、観測できる事実と内部実装の推定を分けます。

#### この小節で固定したいこと

- CPU ごとの変数は、CPU 間で一つの値を共有する頻度を減らす設計手段
- `this_cpu_*()` は現在 CPU の要素を扱う API だが、複数フィールドの不変条件や全 CPU の整合した集計までは保証しない
- CPU 固定やプリエンプション抑止は、排他ロックや IRQ 同期と同じ意味ではない
- リモート書き込み、割り込み文脈からのアクセス、集計時の一貫性は別途検討する
- per-CPU 配置はキャッシュライン分離や競合解消をあらゆる場合に保証するものではない

### 5.2.2 アトミック操作（不可分操作）

#### 複合更新で起きる lost update

共有カウンタを通常の C 式で `count++` と更新すると、概念上は「現在値を読む」「1 を加える」「書き戻す」という複数段階になります。
二つの CPU が同じ値を読み取ってからそれぞれ書き戻すと、増加が一回分失われることがあります。単一のロードやストアが機械語で一命令になるかどうかだけでは、この read-modify-write（RMW）全体の不可分性は保証されません。

```text
初期値 count = 10

CPU 0: count を読む (10) ---- 11 を書く
CPU 1:       count を読む (10) ---- 11 を書く

結果: 11   （期待した 12 ではない）
```

#### Linux atomic API が保証するもの

Linux は `atomic_t`、`atomic64_t`、`atomic_long_t` と、それらに対する `atomic_inc()`、`atomic_add()`、`atomic_fetch_add()`、`atomic_cmpxchg()` などの API を提供します。
この API は、単一の atomic 変数に対する RMW を CPU 間で不可分に行うためのものです。内部実装はアーキテクチャ依存で、x86 の `LOCK` 付き命令だけが実装手段ではありません。

```c
static atomic_t completed = ATOMIC_INIT(0);

static int record_completion(void)
{
	return atomic_inc_return(&completed);
}
```

ここでは `atomic_inc_return()` の一回の呼び出しでカウンタを増やし、新しい値を得ます。
複数 CPU がこの API を同じ変数に対して実行しても、それぞれの更新が失われないように扱われます。

ただし、atomic 変数を使えば任意のデータ構造全体が保護されるわけではありません。たとえば「カウンタが 0 なら別のポインタを設定する」といった複数オブジェクトにまたがる不変条件は、一つの atomic 操作では保証できません。
条件判定と更新を一体化する必要がある場合は `atomic_cmpxchg()` / `atomic_try_cmpxchg()` などの条件付き操作が候補になりますが、複雑な状態遷移にはロックなど適切な同期手段が必要です。

#### atomic_t は必ずしも必要ではない

`atomic_read()` と `atomic_set()` は、それぞれ値を読み書きする操作であり、複数の操作をまとめた RMW ではありません。
値の読み書きだけに `atomic_t` を使っても、読み取りと後続更新を一体化する効果はありません。単純な共有読み書きでは、目的に応じて `READ_ONCE()` / `WRITE_ONCE()` や acquire / release アクセスを検討します。
`READ_ONCE()` / `WRITE_ONCE()` はコンパイラによるアクセスの変形を制約するためのもので、それだけで複数 CPU 間の排他やメモリ順序全般を保証するものではありません。必要な同期の契約に合わせて使い分けます。

逆に、atomic カウンタを `atomic_read()` してから通常の C 演算で更新するコードは、RMW の不可分性を得られません。更新そのものに対応する atomic API を使う必要があります。
ハードウェアレジスタなどの MMIO 領域を `atomic_t` として操作してはならず、デバイス I/O 用の API を使用します。

#### 操作の不可分性とメモリ順序は別の保証

atomic RMW が不可分であることと、別のメモリ位置への読み書きがその前後にどの順序で観測されるかは別問題です。
Linux の atomic API には順序付けの異なる形式があり、`_relaxed`、`_acquire`、`_release` などの接尾辞を持つ API は、その指定に応じたメモリ順序を提供します。
接尾辞のない RMW の順序も操作の形に応じて規則が異なるため、「atomic なら全メモリに対する完全なバリア」と一般化してはいけません。

```text
atomicity: その atomic 変数に対する RMW が途中で分割されないこと
ordering:  他のメモリ位置へのアクセスがいつ見えるか
```

Linux 6.18 の atomic API 文書にある基本的な順序規則は次のとおりです。

- `atomic_read()` / `atomic_set()` など RMW でない操作は、他のメモリ位置に対する順序を基本的に与えない
- 戻り値のない RMW（例: `atomic_inc()`）は、他のメモリ位置に対して基本的に unordered
- 戻り値を返す RMW（例: `atomic_inc_return()`）は、接尾辞なしでは fully ordered
- 条件付き RMW は、失敗時には順序付けを与えない。成功時は操作の種類と `_relaxed` / `_acquire` / `_release` の指定に従う

メモリ順序を必要とする理由は、5.2.3「最適化処理とメモリバリア」で別に扱います。
データ公開や状態通知を atomic カウンタで表現する場合も、更新 API の名前と suffix が何を保証するかを文書・呼び出し側コードで確認します。

#### カウンタと参照カウントを区別する

単純な統計カウンタなど、値の増減自体が必要な場合は `atomic_t` が適することがあります。
しかし、オブジェクトの寿命管理に使う参照カウントでは、オーバーフローや不正な増減に対する追加の保護が重要です。Linux では参照カウント用に `refcount_t` API が用意されているため、寿命管理を `atomic_t` で独自実装せず、`refcount_t` の利用箇所と規約を確認します。

#### ビット操作は目的に合う API を選ぶ

Linux にはビットマップ用のビット操作もあります。ビットを検査する操作と、設定・消去を同時に不可分に行う操作では性質が異なり、通常版と atomic 版の区別もあります。
ビット操作を使うときは、共有ビットマップの同じビットに競合する更新があるかを調べ、`test_and_set_bit()` などの戻り値が操作前と操作後のどちらを表すか、対象アーキテクチャの API 契約を確認します。
原文にある古い `atomic_set_mask()` などの名前を、現行の標準 API とみなして転記しないでください。

#### Linux 6.18 LTS のソースを追う入口

- atomic 型・API の意味と順序付け: `Documentation/core-api/wrappers/atomic_t.rst`
- 共通 atomic API: `include/linux/atomic/` および `include/linux/atomic.h`
- アーキテクチャ別実装: `arch/*/include/asm/atomic*.h`
- 参照カウントの用途と契約: `include/linux/refcount.h`
- ビット操作 API: `include/linux/bitops.h`、`arch/*/include/asm/bitops.h`

#### この小節で固定したいこと

- 通常の `count++` は read-modify-write 全体の不可分性を保証しない
- atomic API は単一 atomic 変数に対する対応操作の不可分性を提供し、複数フィールドの不変条件までは保護しない
- atomicity とメモリ順序は別の性質であり、操作の戻り値や suffix ごとの契約を確認する
- 読み書きだけなら `atomic_t` が不要な場合がある。read と後続更新を分けても不可分な更新にはならない
- 参照カウントには原則として `refcount_t` を使い、MMIO には atomic API を使わない

### 5.2.3 最適化処理とメモリバリア

#### ソースコードの順序だけでは、観測順序は決まらない

カーネルの共有データを扱うときは、ソース上で先に書いた処理が、別 CPU からも必ず先に見えるとは限りません。
コンパイラは言語上許される範囲でメモリアクセスを移動・統合・省略することがあり、CPU もアーキテクチャのメモリモデルに従ってロードやストアを他 CPU から異なる順序で観測させることがあります。

```text
CPU 0: payload = 42; ready = 1;
CPU 1: if (ready) use(payload);
```

このコードだけでは、CPU 1 が `ready == 1` を確認したときに、必ず `payload == 42` を観測するという公開順序の契約が明確ではありません。
コンパイラによる変形を抑える仕組みと、CPU 間のメモリアクセス順序を定める仕組みを分けて考えます。

#### コンパイラバリアと CPU メモリバリア

`barrier()` はコンパイラに対するバリアです。カーネルコードのメモリアクセスをその前後へ移動させないよう制約しますが、それ自体は CPU 間のメモリアクセス順序を強制する命令ではありません。
そのため、CPU 間でデータを公開・通知する目的で `barrier()` だけを置いても、必要なハードウェア順序は得られません。

`READ_ONCE()` / `WRITE_ONCE()` は、特定の共有変数へのアクセスをコンパイラが意図せず複製・統合・再読み込みすることを抑えるための API です。
これらも単独ではロックや一般的な CPU メモリバリアの代わりにならず、アクセスの不可分性や他の変数との順序まで一律に保証するものではありません。

一方、`smp_mb()` はロードとストアの順序を CPU 間で制約する一般的な SMP メモリバリアです。`smp_rmb()` はロード同士、`smp_wmb()` はストア同士の順序を制約します。
必要な順序が片方向・片種類のアクセスに限られるなら、その契約に対応する acquire / release API も候補になります。

`smp_*()` 系の正確な実装はアーキテクチャと構成に依存します。SMP でない構成では CPU 間のバリア命令が不要になる場合がありますが、必要なコンパイラ制約まで無意味になるわけではありません。
逆に x86 で特定のバリアが命令を生成しない場合があっても、それは Linux の API 契約が不要という意味ではなく、アーキテクチャの保証を利用しているためです。

#### release / acquire でデータを公開する

フラグを使ってデータの準備完了を知らせる場合、Linux の `smp_store_release()` と `smp_load_acquire()` を組み合わせると、公開側と参照側の順序を明示できます。

```c
struct message {
	int payload;
	int ready;
};

static struct message msg;

static void publish(void)
{
	WRITE_ONCE(msg.payload, 42);
	smp_store_release(&msg.ready, 1);
}

static void consume(void)
{
	if (smp_load_acquire(&msg.ready))
		use(READ_ONCE(msg.payload));
}
```

ここでは一度だけデータを公開する想定です。公開側では payload の書き込みが release store より後に観測されることを防ぎ、参照側では acquire load の後に payload を読む順序を作ります。
この順序付けは同じ `ready` 変数を介した対応関係として設計します。データを繰り返し更新する場合は、再公開や読み手の寿命を含む別のプロトコルが必要です。

acquire と release は一方向のバリアであり、一般的な完全バリア `smp_mb()` と同じではありません。
適切な対応関係がある場合に必要な順序を与えますが、すべてのアクセスを前後両方向に順序付ける用途には使えません。必要以上に強いバリアを置くより、公開プロトコルに合った API を選びます。

#### バリアは「処理の完了」や「キャッシュのフラッシュ」ではない

メモリバリアは、指定されたアクセスの順序を制約するもので、バリアより前のすべてのメモリアクセスが物理メモリまで完了したことや、別 CPU が必ず直ちに値を読むことを一般に保証しません。
また、片側の CPU にバリアを置くだけで、相手側の読み取り順序まで自動的に整うわけではありません。共有プロトコル全体で、書き手と読み手の対応する順序付けを確認します。

デバイス DMA や MMIO との順序付けは、CPU 間同期と同じ API で考えてはいけません。
デバイスとの共有メモリには `dma_rmb()` / `dma_wmb()` や DMA API、MMIO には適切な I/O accessor とその順序規則を使います。通常の `smp_*()` バリアだけでデバイス可視性や DMA 完了を代用できるとは限りません。

#### x86 の命令表を一般則として使わない

原文にある `lfence` / `sfence` / `mfence`、`LOCK` 付き命令、`cli` / `sti` などの説明は、特定世代の x86 の実装や CPU 動作を理解する入口です。
Linux 6.18 のコードを読むときは、特定命令がすべての構成で出力されると仮定せず、まず `smp_mb()`、`smp_store_release()` など Linux API の契約を確認し、必要な場合にアーキテクチャ実装を追います。
割り込み禁止は割り込み実行文脈を制御する仕組みであり、メモリバリアの一般的な代わりとして扱いません。

#### Linux 6.18 LTS のソースを追う入口

- メモリバリアの抽象モデル、API、使用上の注意: `Documentation/core-api/wrappers/memory-barriers.rst`
- `barrier()`、`READ_ONCE()` / `WRITE_ONCE()`、SMP barrier の定義: `include/linux/compiler.h`、`include/asm-generic/barrier.h`
- アーキテクチャ依存のバリア実装: `arch/*/include/asm/barrier.h`
- atomic API の順序付け: `Documentation/core-api/wrappers/atomic_t.rst`
- DMA とデバイス向けの順序付け: `Documentation/core-api/dma-api.rst`、`Documentation/driver-api/device-io.rst`

#### この小節で固定したいこと

- コンパイラバリアと CPU メモリバリアは別の保証であり、`barrier()` 単独では CPU 間順序を作らない
- `READ_ONCE()` / `WRITE_ONCE()` はアクセス形態を制約するが、ロックや一般的なメモリ順序を代替しない
- `smp_mb()`、`smp_rmb()`、`smp_wmb()` は、必要なアクセス順序に応じて使い分ける
- release / acquire は対応する公開・取得プロトコルで使う一方向の順序付けで、完全バリアとは異なる
- バリアは処理完了・キャッシュフラッシュ・デバイス DMA 完了を意味しない
- 命令列ではなく Linux API の契約から読み始め、x86 固有実装は必要に応じて確認する

### 5.2.4 スピンロック

#### ロックは共有データへのアクセス規約

ロックは、共有データを扱う複数の実行経路が同じ規約に従い、同時にクリティカルセクションへ入らないようにする仕組みです。
ロックを一つ取得したコードだけがそのデータを保護できるのではなく、対象データにアクセスするすべての経路が同じロック、または明確に両立する保護規則を使う必要があります。

```text
task A ---- lock ----> 共有状態を更新 ---- unlock
task B ---- lock ----> A が解放するまで待機
IRQ     ---- lock ----> 同じ保護規則に従う
```

ロックが保護する範囲は、ロック取得から解放までのクリティカルセクションです。
互いに独立したデータなら別のロックで同時実行できますが、ロックを細分化すると取得順序の循環によるデッドロックなど、新たな問題も生じます。まず単一のロックで不変条件を明確に保護し、並列性の必要が明らかな場合にだけ分割を検討します。

#### spinlock_t の基本的な使い方

スピンロックはロックを取得できるまで待つ方式のロックです。通常の非 RT カーネルでは、短いクリティカルセクションで使い、待ち時間中に CPU を明け渡さないため、保護区間内でスリープや長時間処理をしてはいけません。

```c
static DEFINE_SPINLOCK(state_lock);
static unsigned long shared_state;

static void update_state(unsigned long value)
{
	spin_lock(&state_lock);
	shared_state = value;
	spin_unlock(&state_lock);
}
```

ロックの取得・解放には、保護対象データに対する順序付けも伴います。
ただし、保護したいすべての読み書きが同じロック規約に参加することが前提です。ロック外のアクセスを一つでも許せば、ロックだけではそのアクセスとの競合を防げません。

#### 割り込み文脈とロック suffix

同じロックをプロセス文脈とハード IRQ handler の両方が取得する設計では、同一 CPU 上での自己デッドロックを防ぐ必要があります。
たとえばタスクがロックを保持したまま IRQ に中断され、IRQ handler が同じロックを取得しようとすると、IRQ は解放を待ち続け、タスクは IRQ の終了を待つことになります。

非 RT カーネルでこの共有を保護する典型形は `spin_lock_irqsave()` / `spin_unlock_irqrestore()` です。
ロック取得前のローカル割り込み状態を保存して無効化し、解放時に元の状態へ戻すため、呼び出し側の割り込み状態を壊しません。
softirq と共有する設計では `spin_lock_bh()` / `spin_unlock_bh()` を使うケースがあります。どの suffix が適切かは、そのロックを取る全実行文脈と、割り込み側がどの種類かを調べて選びます。

```c
unsigned long flags;

spin_lock_irqsave(&state_lock, flags);
shared_state++;
spin_unlock_irqrestore(&state_lock, flags);
```

`spin_lock_irqsave()` は他 CPU の割り込みを止めるものではありません。CPU 間の相互排他はロック自体が担い、IRQ 制御は同一 CPU 上の割り込み再入を避ける役割です。
割り込み禁止だけでは SMP 上の競合を防げず、逆に通常の `spin_lock()` だけでは、ロック保持中に同じ CPU の割り込み handler が同じロックを取る構成でデッドロックし得ます。

#### PREEMPT_RT では spinlock_t の意味を確認する

Linux 6.18 では `spinlock_t` の意味は PREEMPT_RT の有無によって異なります。
非 PREEMPT_RT では `spinlock_t` は strict なスピンロックとして動作し、ロック保持中のプリエンプションを抑止します。
PREEMPT_RT では `spinlock_t` は RT-mutex ベースの実装に置き換わり、取得時にタスクがブロックする可能性があり、通常の意味でプリエンプションを無効にするわけではありません。ハード IRQ suffix の割り込み状態への効果も非 RT と異なります。

一方、`raw_spinlock_t` は PREEMPT_RT を含む構成でも strict なスピンロックです。低レベル割り込み処理や、実際にハード IRQ を無効にしてハードウェア状態を保護する必要がある限定的なコードで使い、通常のドライバや共有データ保護に安易に選びません。
PREEMPT_RT でハード IRQ の無効化が本当に必要な箇所では `raw_spin_lock_irqsave()` 系が候補になりますが、どの文脈から取得できるか、IRQ handler をどのようにスレッド化しているかをカーネル構成と API 契約に照らして確認します。

#### Linux 6.18 LTS のソースを追う入口

- スピンロック API と型: `include/linux/spinlock.h`、`include/linux/spinlock_types.h`
- ロックの種類と PREEMPT_RT での意味: `Documentation/locking/locktypes.rst`
- スピンロックの基本例と IRQ suffix の考え方: `Documentation/locking/spinlocks.rst`
- 実装と構成依存の分岐: `kernel/locking/spinlock.c`、`include/linux/spinlock_rt.h`、`include/linux/spinlock_api_smp.h`

#### この小節で固定したいこと

- スピンロックはロックを共有規約として使う全アクセス経路の合意があって初めてデータを保護する
- 一般のスピンロックのクリティカルセクションは短く保ち、スリープ可能な処理を含めない
- IRQ と共有するなら同一 CPU 上の再入によるデッドロックを考え、適切な irq / bh suffix を選ぶ
- ローカル IRQ 禁止は CPU 間の排他を提供せず、ロックと役割が異なる
- PREEMPT_RT では `spinlock_t` と `raw_spinlock_t` の実行・プリエンプション・IRQ の意味が異なる

### 5.2.4.1 カーネル内プリエンプションを伴う spin_lock マクロ

#### 旧マクロ展開ではなく、API の契約を読む

原文は、プリエンプション可能な SMP カーネルでの `spin_lock()` を、`preempt_disable()`、test-and-set、待ちループなどに分けて説明しています。
これはロックを取得するための概念を理解する助けになりますが、Linux 6.18 で `spinlock_t` が常にその命令列へ展開されるという意味ではありません。ロックの内部表現、取得方法、待ち方はアーキテクチャと PREEMPT_RT の構成によって変わります。

非 PREEMPT_RT での `spin_lock()` は通常、ロック取得中に同じタスクが別 CPU へ移動しないようプリエンプションを抑止します。
ロックを取得できない場合、通常はロック所有者が解放するまで待ちます。PREEMPT_RT の `spinlock_t` はブロックし得る実装であるため、旧本文の「待機中もプリエンプションを一時的に許す」という具体的手順を一般化してはいけません。

スピンロックを待つ文脈では、ロック所有者が実行を続けて解放できることが重要です。
ロック所有者を同じ CPU 上で無期限に待たせる構成や、割り込み側からロック保持側を中断して同じロックを要求する構成はデッドロックにつながります。どの API variant が必要かを、所有者と待ち手の実行文脈から判断します。

#### Linux 6.18 LTS のソースを追う入口

- 公開 API と `spin_lock()` の定義: `include/linux/spinlock.h`
- 非 RT のロック取得・解放経路: `include/linux/spinlock_api_smp.h`、`kernel/locking/spinlock.c`
- RT 向け spinlock 実装: `include/linux/spinlock_rt.h`
- ロック待ちのデバッグと依存関係検査: `kernel/locking/lockdep.c`、`kernel/locking/`

関数の実装だけで結論を出さず、型定義、PREEMPT_RT の条件分岐、lockdep の制約も併せて確認します。

#### この小節で固定したいこと

- `spin_lock()` の内部命令列を、Linux 6.18 全構成に共通する固定実装とみなさない
- 非 RT でのプリエンプション抑止と、PREEMPT_RT の `spinlock_t` のブロック可能性を区別する
- ロック待ち中もロック所有者が実行して解放できる構造であることを確認する
- `spin_lock()`、`spin_lock_irqsave()`、`raw_spin_lock()` は文脈と保護要件に応じて選択する

### 5.2.4.2 カーネル内プリエンプションを伴わない spin_lock マクロ

#### プリエンプション設定と相互排他は別の軸

原文のこの小節は、カーネル内プリエンプションを無効にした構成での `spin_lock()` を扱い、x86 の `lock; dec` や `pause` を使う旧実装例を示します。
Linux 6.18 を読むときは、プリエンプションモデルの選択と、spinlock が共有状態に与える相互排他を分けます。

SMP では、カーネル内プリエンプションが無効なモデルでも、複数 CPU が同じ共有データへ同時にアクセスできます。そのため、別 CPU との排他を提供するスピンロックは依然必要です。
反対に、UP では同時に動く CPU が一つでも、ロックが常に何もしないとは限りません。プリエンプションや割り込み文脈など、構成と呼び出し文脈に応じた保護が必要になる場合があります。

したがって「プリエンプションを使わない構成なら spin_lock は不要」「spin_lock はプリエンプションを止めるだけ」とは言えません。
ロックの役割は、同じ保護規約に参加する実行経路間の排他と順序付けです。プリエンプション抑止は実装・構成に結び付く効果の一つで、SMP 上の排他の代替ではありません。

#### 旧 x86 命令列を現在の展開とみなさない

原文の `lock; decb`、符号フラグによる取得判定、`pause` を使った待ちループは、当時の x86 のスピンロック実装を説明する例です。
Linux 6.18 のロック取得は、アーキテクチャ、ロック実装、カーネル構成によって異なります。spinlock の内部フィールドや命令列を前提にせず、まず `spin_lock()` が保証する契約と、対象データに対するロック規約を読みます。

ロックを待つコードが同じ CPU 上で走り続けると、保持側が実行できず解放されない構造になり得ます。また、ロック保持中に同じロックを取る IRQ が入る場合も停止します。
このため、ロック待ちが発生し得る構造か、ローカル IRQ / bottom half を抑止すべきか、PREEMPT_RT でブロック可能な `spinlock_t` なのかを別々に確認します。

#### Linux 6.18 LTS のソースを追う入口

- プリエンプションモデルの選択肢: `kernel/Kconfig.preempt`
- spinlock の公開契約と構成依存の定義: `include/linux/spinlock.h`
- spinlock / raw_spinlock の PREEMPT_RT 差: `Documentation/locking/locktypes.rst`
- x86 のロック実装: `arch/x86/include/asm/spinlock.h`、`arch/x86/include/asm/qspinlock.h`

#### この小節で固定したいこと

- カーネル内プリエンプションの有無と、SMP で共有データを排他する必要性は別問題
- プリエンプション無効の構成でも、複数 CPU 間の競合には適切なロックが必要
- UP であることだけを理由に、spinlock がすべての構成で不要・無動作だと決めつけない
- 旧 x86 の `lock; dec` / `pause` 命令列やロック内部状態を Linux 6.18 の固定実装とみなさない
- 待ち手がロック所有者を妨げないこと、IRQ 再入による自己デッドロックがないことを確認する

### 5.2.4.3 spin_unlock マクロ

#### ロック解放は保護区間の終わりを示す

`spin_unlock()` は取得済みロックを解放し、同じロックを待つ実行経路が保護区間へ進めるようにします。
対応する取得・解放が正しく組になっていることが前提であり、取得していないロックの解放、二重解放、別のロックを解放する操作はロック規約を破ります。

```c
spin_lock(&state_lock);
shared_state = new_value;
spin_unlock(&state_lock);
```

ロックを保持していた実行経路は、保護対象の操作を終えた後に解放します。
ロックの解放後に同じ共有データへ触れる処理は保護区間の外にあるため、別の実行経路との競合がないか改めて確認します。

#### 解放とメモリ順序

ロック解放には release の順序付けがあり、クリティカルセクション内で先行した共有データへの更新が、同じロックを後で取得する実行経路から正しく観測されるための契約を形成します。
ロック取得側の acquire と解放側の release を対にして使うことで、データ保護と順序付けを一つのロック規約として扱えます。

これは「解放時点で全データが物理メモリへ書き出される」「ロック待ちの CPU が直ちに実行される」という意味ではありません。
また、ロック外の読み書き、別ロックを使うアクセス、デバイス DMA との同期まで自動的に保証するものでもありません。

#### suffix と状態復元を対にする

`spin_lock_irqsave()` で取得した場合は、対応する `spin_unlock_irqrestore()` で保存した割り込み状態を復元します。
同様に `spin_lock_bh()` には `spin_unlock_bh()` を対応させます。suffix の異なる操作を混ぜると、呼び出し側が入る前の割り込み状態を壊したり、必要な保護を早く解除したりする可能性があります。

```c
unsigned long flags;

spin_lock_irqsave(&state_lock, flags);
shared_state = new_value;
spin_unlock_irqrestore(&state_lock, flags);
```

PREEMPT_RT では `spinlock_t` の解放が単なるロック変数へのストアとプリエンプション再許可に見えるとは限りません。
ロック所有権の解放、待ちタスクの扱い、IRQ suffix の意味はロック型と構成に依存します。旧本文の `movb` 命令列を、現行の解放処理として当てはめないでください。

#### Linux 6.18 LTS のソースを追う入口

- spinlock の公開 API と unlock 系定義: `include/linux/spinlock.h`
- generic raw lock 解放とアーキテクチャ依存経路: `kernel/locking/spinlock.c`、`include/linux/spinlock_api_smp.h`
- PREEMPT_RT 向け spinlock 実装: `include/linux/spinlock_rt.h`
- ロックの release / acquire と PREEMPT_RT 上の規則: `Documentation/locking/locktypes.rst`、`Documentation/core-api/wrappers/memory-barriers.rst`

#### この小節で固定したいこと

- ロック解放は保護区間の終端であり、取得したロックと正しく対応させる
- unlock の release 順序は、同じロックの後続 acquire と共有データの可視性を結び付ける
- unlock は物理メモリへの書き出し完了や DMA 完了を意味しない
- `irqsave` / `irqrestore`、`bh` などの suffix は対応する組で使用する
- 解放時の内部命令列や wake-up 処理は `spinlock_t` / `raw_spinlock_t` と PREEMPT_RT の条件を見て追う

### 5.2.5 読み書き用スピンロック

#### 読み手を並行させ、書き手を排他的にする

読み書き用スピンロック（`rwlock_t`）は、同じデータを読む複数の実行経路を同時に許し、書き手は読み手・他の書き手のいずれとも重ならないようにするロックです。

```text
read_lock A ---- 読み取り ---- read_unlock
read_lock B ---- 読み取り ---- read_unlock
write_lock C     (A/B が抜けるまで取得できない)
```

読み取りが多く、読み手同士を並行させることで有効な利点が得られる場合に使えます。
ただし読み取り側もロック状態を共有し、取得・解放には atomic 操作やキャッシュライン競合が伴います。短い読み取り区間では単純な `spinlock_t` の方が低コストなことがあります。

Linux の locking 文書は、読み書き用スピンロックを新たに導入する前に慎重に検討するよう勧めています。読み取り中心のデータ構造では、要件に応じて RCU など他の設計が適切なこともあります。
また `rwlock_t` の意味は PREEMPT_RT で変わります。非 RT ではスピンロックですが、PREEMPT_RT では別の実装に置き換わり得るため、割り込み文脈から使う前提を含めて構成ごとの契約を確認します。

#### Linux 6.18 LTS のソースを追う入口

- `rwlock_t` の API と型: `include/linux/spinlock.h`、`include/linux/rwlock.h`
- ロック種別と PREEMPT_RT での違い: `Documentation/locking/locktypes.rst`
- 読み書きロックの設計上の注意: `Documentation/locking/spinlocks.rst`
- アーキテクチャ依存の実装: `arch/*/include/asm/spinlock.h`

### 5.2.5.1 読み込み用ロックの取得と解除

#### read_lock はロックされたデータを変更しない

`read_lock()` は読み取り側としてロックを取得し、ほかの読み手との並行アクセスを許可しながら、書き手が排他ロックを得るのを防ぎます。
読み取り区間では、保護対象データを変更してはいけません。条件によって一部だけ更新するかもしれない場合でも、変更が必要になってから read lock を write lock へ変換するのではなく、最初から書き手用ロックで入る必要があります。

```c
static DEFINE_RWLOCK(table_lock);

static int find_entry(int key)
{
	int result;

	read_lock(&table_lock);
	result = lookup_table(key);
	read_unlock(&table_lock);

	return result;
}
```

`read_lock()` はデータを読むコードの前に取得し、対応する `read_unlock()` で解放します。
読み手がロックを保持している間、書き手は排他取得できません。逆に書き手がロックを保持中なら、新しい読み手は保護区間へ入れず待ちます。
このロックはデータの寿命管理や、ロック外で保存したポインタの有効性までは保証しません。

#### 読み取り側も実行文脈を考慮する

通常の `read_lock()` はローカル IRQ を無効にする API ではありません。
同じ `rwlock_t` をハード IRQ handler とタスク文脈の両方から取得する場合、タスクが read lock を保持中に同じ CPU の IRQ が割り込み、同じロックを求めることでデッドロックする可能性があります。
必要な場合は `read_lock_irqsave()` / `read_unlock_irqrestore()` などの variant を検討します。ローカル IRQ 制御だけでは CPU 間の排他を行わず、ロック自体も引き続き必要です。

PREEMPT_RT では `rwlock_t` の実装・ブロック特性が非 RT と異なります。割り込み文脈や非プリエンプト可能な文脈から使えると決めつけず、該当するロック型・variant と構成の制約を確認します。
いずれの構成でも、読み取り処理を長時間ロック内に置くと書き手を遅らせます。待ち時間や読み書き比率も、rwlock を選ぶ根拠に含めます。

#### 読み取りロックの昇格を前提にしない

`read_lock()` で入った後に `write_lock()` を取得しても、安全な昇格にはなりません。複数の読み手が同時に保持したままそれぞれ昇格しようとすると、互いの read lock が残って書き手が進めなくなる可能性があります。
読み取り後に更新が必要と判明する設計なら、ロックを解放してから書き手ロックを取り直し、状態が変化していないかを再確認するか、最初から書き手ロックを取得します。

#### Linux 6.18 LTS のソースを追う入口

- `read_lock()`、`read_unlock()` と IRQ variant: `include/linux/rwlock.h`
- 共通 spinlock / rwlock API: `include/linux/spinlock.h`
- PREEMPT_RT における `rwlock_t` の意味: `Documentation/locking/locktypes.rst`
- 非 RT の x86 rwlock 実装: `arch/x86/include/asm/spinlock.h`

#### この小節で固定したいこと

- 複数読み手は並行できるが、書き手はすべての読み手・他の書き手と排他的に動作する
- read lock 中は保護対象を書き換えず、更新があり得るなら write lock を最初から使う
- read lock の後から write lock への安全な昇格を前提にしない
- IRQ と共有する場合は同一 CPU 上の再入を考え、必要な IRQ variant を選ぶ
- rwlock は読み手も共有状態を更新するため常に高速とは限らず、PREEMPT_RT での意味も確認する

### 5.2.5.2 書き出し用ロックの取得と解除

#### write_lock は読み手・書き手を排除する

`write_lock()` は書き手としてロックを取得し、取得中は他の書き手だけでなく読み手も保護区間へ入れないようにします。
書き手は `rwlock_t` で保護される共有状態を読み書きできます。複数フィールドの関係を一体として保つ必要がある場合も、同じ write lock の範囲にまとめます。

```c
static DEFINE_RWLOCK(table_lock);

static void replace_entry(int key, struct entry *new_entry)
{
	write_lock(&table_lock);
	update_table(key, new_entry);
	write_unlock(&table_lock);
}
```

ロックを保持している間、既存の読み手が残っていれば writer は取得を待ちます。writer が取得した後は、新しい読者・書き手とも同じ保護区間へ入れません。
`write_unlock()` で解放すると、待っていた読み手または書き手が API とロック実装の規則に従って進めるようになります。

#### 書き手用 variant と割り込み文脈

`write_lock()` はローカル IRQ を無効にしません。同じ `rwlock_t` をハード IRQ handler と共有する場合は、タスク側が write lock を保持したまま IRQ に中断され、IRQ 側が同じロックを取得しようとするデッドロックを防ぐ必要があります。
対象となる実行文脈に応じて `write_lock_irqsave()` / `write_unlock_irqrestore()` や bottom-half 用 variant を検討します。IRQ 制御はローカル CPU に対するもので、別 CPU との排他は引き続き rwlock が担います。

PREEMPT_RT では `rwlock_t` の実装や待ち方が通常の非 RT スピンロックとは異なります。ハード IRQ から取得可能か、取得時にブロックし得るかを古いマクロ展開から推測せず、カーネル構成に対応する API 契約を確認します。

#### 読み手を止める時間とロック順序

write lock は読み手も止めるため、保護区間には共有状態の更新に必要な処理だけを置きます。長時間処理、スリープを伴う処理、外部処理の完了待ちを含める必要があるなら、別の同期設計へ分割できないか検討します。

複数のロックを同時に取得するコードでは、取得順序を一貫させないとデッドロックが起きます。ある経路が `table_lock` の後に別ロックを取り、別の経路が逆順で取る構造がないか、呼び出し元を含めて確認します。
read/write のロック状態や待ち行列の具体的なビット表現は、Linux 6.18 でも API 契約ではありません。アーキテクチャと構成に依存する実装詳細として扱います。

#### Linux 6.18 LTS のソースを追う入口

- `write_lock()`、`write_unlock()` と IRQ / BH variant: `include/linux/rwlock.h`
- 共通 spinlock / rwlock API と型: `include/linux/spinlock.h`
- PREEMPT_RT での rwlock の意味: `Documentation/locking/locktypes.rst`
- x86 rwlock の実装詳細: `arch/x86/include/asm/spinlock.h`

#### この小節で固定したいこと

- write lock は複数の読み手と書き手すべてに対して排他的である
- write lock 中に共有状態の関連フィールドを一体として更新し、unlock 後に保護が続くと思い込まない
- IRQ / BH と共有する場合は同一 CPU 上の再入を考慮し、対応する suffix の取得・解放を組にする
- write lock は読み手を待たせるため、保護区間を短くし、ロック順序を一貫させる
- カウンタ表現や命令列を API 契約と混同せず、PREEMPT_RT とアーキテクチャごとの実装を分けて読む

### 5.2.6 順次ロック

#### 読み手は待たずに読み取りをやり直す

順次ロック（seqlock）は、読み取り側をロックで待たせず、データを読み取った前後で sequence counter を検査する方式です。
書き手が読み取り中に更新を始めた、または読み取り中に更新を完了した場合、読み手は得たコピーを破棄して読み直します。書き手は読み手の終了を待たないため、読み取りが多く書き込みが少ないデータで有効です。

```c
static DEFINE_SEQLOCK(clock_lock);
static struct clock_snapshot clock_data;

static struct clock_snapshot read_clock_snapshot(void)
{
	struct clock_snapshot copy;
	unsigned int seq;

	do {
		seq = read_seqbegin(&clock_lock);
		copy = clock_data;
	} while (read_seqretry(&clock_lock, seq));

	return copy;
}

static void update_clock_snapshot(struct clock_snapshot next)
{
	write_seqlock(&clock_lock);
	clock_data = next;
	write_sequnlock(&clock_lock);
}
```

概念上、書き手は更新開始時に sequence を奇数にし、更新終了時に偶数へ戻します。読み手は開始時に偶数の値を得て、データをローカルへコピーした後に同じ値が維持されたかを調べます。
API の細部は型や variant ごとに異なるため、現行の Linux では `read_seqbegin()` / `read_seqretry()` など対応する API の契約を確認します。

#### seqcount_t と seqlock_t は同じものではない

`seqcount_t` は sequence counter による整合性検査の仕組みであり、複数 writer を直列化するロックを内部に持ちません。
writer 同士を外部ロックで直列化し、writer が読者に中断されて sequence が奇数のままにならないよう、プリエンプションや割り込みの条件も呼び出し側で満たす必要があります。

`seqlock_t` は sequence counter に writer 用の spinlock を組み合わせた型です。`write_seqlock()` / `write_sequnlock()` で writer を直列化し、読み手はロックレスに読み取りを試みて必要なら再試行します。
どの read/write variant を使うかは、hard IRQ や softirq の読者があるかを含む実行文脈によります。sequence counter が自動的にあらゆる writer / IRQ 問題を解決すると考えず、Linux 6.18 の該当 variant と locking 文書を確認します。

#### 保護対象はコピー可能で、副作用のない読み取りに限る

読み取り区間は再実行される可能性があるため、読み取り中に副作用を起こしてはいけません。
たとえば同じ読み取りをもう一度行うことでカウンタを二重加算したり、デバイス操作を繰り返したりする処理は不適切です。読み手は必要な値をローカル変数へコピーし、再試行が終わってから結果を使います。

ポインタを含む構造体をコピーすること自体は可能ですが、sequence retry は指し先の寿命を保護しません。読み手がポインタを追っている間に writer が対象を解放・置換すれば、読み手は無効なメモリへアクセスする危険があります。
ポインタの寿命や差し替えも保護する必要がある場合は、RCU など寿命管理を含む別の仕組みを検討します。

また、writer が読者に割り込まれ、その reader が奇数の sequence を見て再試行を続けると、writer が実行を再開して sequence を偶数へ戻せません。高優先度 reader がこの状態を作ると livelock につながることがあります。
writer が reader に中断されない条件と、reader が実行される文脈を組み合わせて確認します。通常の seqlock は NMI reader に対する万能な保護ではなく、そうした要件には latch sequence counter など別の設計が必要になる場合があります。

#### 書き込み頻度と再試行コスト

順次ロックでは writer は reader を待たせませんが、更新と重なった reader は読み直す必要があります。書き込み頻度が高い、読み取り区間が長い、再試行を許容できない場合は、効率や応答性が悪化します。
単に「読み手が多い」だけで選ぶのではなく、writer の頻度、読み取り区間の長さ、再試行可能性、データの寿命、割り込み優先度を合わせて検討します。

#### Linux 6.18 LTS のソースを追う入口

- seqcount / seqlock の型と API: `include/linux/seqlock.h`
- sequence counter と sequential lock の契約・使い方: `Documentation/locking/seqlock.rst`
- 時刻情報での利用例: `kernel/time/timekeeping.c`
- RCU とポインタ寿命の考え方: `Documentation/RCU/` 以下の設計文書

#### この小節で固定したいこと

- sequence counter は読み取りの一貫性を検査し、更新と重なった読み手に再試行を要求する
- `seqcount_t` は writer の直列化を提供しない。`seqlock_t` は writer 用ロックを内包する
- 再試行する読み取り区間に副作用を置かず、必要なデータをローカルへコピーする
- sequence retry はポインタの寿命を保護しないため、指し先の解放・置換を別の仕組みで守る
- reader が writer を中断して奇数 sequence を待ち続けないよう、IRQ / NMI / 優先度と writer の実行条件を確認する
- 読み手の再試行コストと書き込み頻度を考慮し、用途に合わない場合は通常のロックや RCU 等を選ぶ

### 5.2.7 Read-Copy Update（RCU）

#### 読み手の負担を抑え、更新と回収を分離する

RCU は、読み取りが多く、読み手に短い読み取り区間を求められるデータ構造を扱う同期機構です。
読み手は通常の mutex や spinlock を取得せずに参照でき、書き手は新しい状態を用意して公開します。古い状態は、公開先から外した直後には解放せず、それを参照していた可能性のある読み手が通過し終えた後に回収します。

```text
reader: RCU read-side section で現在のポインタを参照 ─────┐
                                                          │
writer: 新しいオブジェクトを用意 -> ポインタを差し替え   │
        -> grace period の完了を待つ -> 古い物を解放      │
                                                          └ 古い参照がなくなってから回収
```

この「公開先から外す」段階と「実際に破棄する」段階を分離することが RCU の中心です。
読み手は更新前か更新後のオブジェクトを参照し得ます。RCU が保証するのは read-side critical section 中の参照とオブジェクト回収の関係であり、複数フィールドの任意の同時更新を自動的に一貫させることではありません。

#### 読み手の API とポインタの公開

一般的な RCU 読み手は `rcu_read_lock()` と `rcu_read_unlock()` で読み取り区間を示し、共有ポインタを読むときは `rcu_dereference()` を使います。
書き手は新しいオブジェクトを初期化した後、`rcu_assign_pointer()` で公開します。これらの API は単なるポインタ代入・読み込みではなく、RCU の依存関係と必要な順序を表します。

```c
rcu_read_lock();
obj = rcu_dereference(current_obj);
if (obj)
	value = obj->value;  /* 読み取り区間内で値をコピー */
rcu_read_unlock();
```

RCU read-side critical section の実行条件は RCU の flavor と API によって確認します。
Linux 6.18 の通常の preemptible RCU では読み手がプリエンプトされることはありますが、一般の RCU read-side critical section で任意にスリープしてよいわけではありません。sleep 可能な読み取りには SRCU など要件に合う仕組みを検討します。
旧本文の「`rcu_read_lock()` は `preempt_disable()` と同じ」という説明を現行 Linux 全体へ当てはめないでください。

#### 書き手の直列化と更新後の再確認

RCU は複数の書き手を自動的に直列化するロックではありません。
同じポインタや関連データを更新する書き手が複数いる場合は、mutex や spinlock など別の手段で更新を直列化し、更新時の不変条件を保ちます。

更新は通常、新しいオブジェクトを初期化してから共有ポインタを差し替える形です。読み手はポインタを得た後、read-side critical section の中で必要な値を読み取ります。
対象オブジェクトをその区間の外まで使う必要があるなら、参照カウントなど別の寿命管理が必要です。RCU ポインタをコピーしただけで、区間終了後も有効だとは限りません。

#### grace period と古いオブジェクトの回収

ポインタを新しいオブジェクトへ差し替えた後でも、古いポインタを既に取得している読み手が残っている可能性があります。
そのため、書き手は `synchronize_rcu()` で grace period の完了を待ってから古いオブジェクトを解放するか、`call_rcu()` / `kfree_rcu()` で grace period 後に行う回収を登録します。

```text
初期ポインタ: current_obj -> old
更新:         current_obj -> new
              old は既存 reader が使う可能性があるため保持
grace period: old を参照していた reader がすべて区間を抜ける
回収:         old を解放
```

`synchronize_rcu()` は呼び出し側を待たせるため、呼び出し可能な実行文脈に制約があります。
待たせられない経路では `call_rcu()` 等の遅延回収を検討しますが、コールバックの実行文脈・作業内容・オブジェクトの確保方法も API の契約に合わせます。
grace period は「その時点で存在する全 reader が終了した」ことを保証する仕組みであり、すべての参照者が永久にいなくなったことを示すものではありません。以後の reader が古いポインタを得られないよう、先に公開構造から取り除く必要があります。

#### RCU が守るもの、別に同期するもの

RCU は主にポインタの公開・読み取りと、読み手が参照中のオブジェクトをいつ回収できるかを扱います。オブジェクトの中身を複数の writer が同時に書き換える場合、そのデータ競合は別のロックや atomic API で保護します。
更新頻度が高く、読み手が再試行を避けたい構造や、読み手が自由にスリープする必要がある構造には、RCU が最適とは限りません。

ネットワークや VFS などの実例を読むときは、`rcu_read_lock()` だけを検索して終わらず、対応する `rcu_dereference()`、writer の `rcu_assign_pointer()`、古いオブジェクトの `call_rcu()` / `kfree_rcu()`、writer 間の直列化まで一続きで追います。

#### Linux 6.18 LTS のソースを追う入口

- RCU の目的・基本概念: `Documentation/RCU/whatisRCU.rst`、`Documentation/RCU/rcu.rst`
- RCU list の更新・参照パターン: `Documentation/RCU/listRCU.rst`
- RCU API 宣言と read-side primitives: `include/linux/rcupdate.h`
- RCU の実装と grace period 処理: `kernel/rcu/`
- SRCU の API と利用条件: `include/linux/srcu.h`、`Documentation/RCU/`

#### この小節で固定したいこと

- RCU は読み取りとオブジェクト回収を結び付ける仕組みで、一般的な相互排他ロックではない
- 読み手は `rcu_read_lock()` / `rcu_read_unlock()`、ポインタ参照は `rcu_dereference()` を使う
- 書き手は更新を直列化し、初期化済みポインタを `rcu_assign_pointer()` で公開する
- 古いオブジェクトは公開先から外した後、grace period を経てから回収する
- grace period 後の回収でも、read-side critical section を越えてポインタを使うなら別の寿命管理が必要
- RCU の read-side 区間を任意に sleep できるとは限らず、必要に応じて SRCU などを選ぶ

### 5.2.8 セマフォ

#### カウント資源の取得と待ち合わせ

Linux のカーネル semaphore は、一定数の利用枠を表す counting semaphore です。
初期値を `n` にすると、取得可能な枠がある間は最大 `n` 回まで取得でき、枠がなくなった後の取得要求はタスクを待たせます。単一の所有者による相互排他だけでなく、同時に許可する利用数を制限したい場合に意味があります。

```c
static DEFINE_SEMAPHORE(io_slots, 3);

static int perform_io(void)
{
	if (down_interruptible(&io_slots))
		return -EINTR;

	do_io();
	up(&io_slots);
	return 0;
}
```

取得できなかったタスクは wait queue で待機し、`up()` で枠が返ると再び実行可能になります。
semaphore は mutex と異なり owner を記録せず、取得した task と別の task や割り込み文脈から `up()` して枠を返すことができます。この性質は設計上の用途を広げる一方、誰が取得・解放の対応を管理するかをコード側で明確にする必要があります。

#### mutex・completion と使い分ける

一つの task が取得し、同じ task が解放する相互排他には、owner semantics とデバッグ支援を持つ mutex が通常適しています。
Linux の locking 文書は、semaphore を新しい用途で相互排他と待ち合わせの両方に使うより、mutex と completion などの別機構に分けることを勧めています。
セマフォは「同時にいくつまで入れるか」という数の制限が本質であり、単なる binary mutex の代替として選ぶものではありません。

ユーザー空間の System V semaphore とカーネル内の `struct semaphore` も別の API です。この節で扱うのはカーネル内部の counting semaphore で、ユーザー空間 IPC の操作とは混同しません。

#### 利用できる実行文脈

`down()`、`down_interruptible()`、`down_killable()`、`down_timeout()` は枠がなければ task を sleep させるため、基本的に sleep 可能な task context から呼び出します。ハード IRQ や softirq のように待ち時間中に sleep できない文脈では、取得できない可能性がある `down*()` を使ってはいけません。

`down_trylock()` は待たずに取得を試みる API で、成功時は 0、取得できない場合は 1 を返します。この戻り値は `spin_trylock()` や `mutex_trylock()` と逆向きなので注意します。
Linux 6.18 の実装では `down_trylock()` と `up()` は interrupt context から呼べる旨が `include/linux/semaphore.h` に明記されています。実装上、成功が確実な場合に限り `down()` を interrupt context から呼ぶ既存箇所も想定されていますが、通常の設計では待ち得る `down*()` を IRQ / softirq で使わず、非ブロッキング API と明確な条件を選びます。「待たない」操作でも、保護対象や呼び出し側の実行文脈の制約がすべて消えるわけではありません。

#### Linux 6.18 LTS のソースを追う入口

- semaphore 型と公開 API: `include/linux/semaphore.h`
- acquire / release、wait queue、`down_trylock()` の実装: `kernel/locking/semaphore.c`
- mutex・semaphore の使い分けと lock type: `Documentation/locking/locktypes.rst`
- completion によるイベント待ち合わせ: `include/linux/completion.h`、`kernel/sched/completion.c`

### 5.2.8.1 セマフォの取得と解放

#### `down*()` の選択と戻り値

Linux 6.18 では待機時の中断条件に応じて複数の acquire API が用意されています。

- `down_interruptible()`: signal による中断を許し、中断時は負の errno（通常 `-EINTR`）を返す
- `down_killable()`: fatal signal による中断を許す
- `down_timeout()`: 指定時間内に取得できない場合、`-ETIME` を返す
- `down_trylock()`: sleep せずに試み、成功なら 0、失敗なら 1 を返す
- `down()`: 無期限に待つが、現行の実装文書では deprecated とされ、通常は中断可能または killable な variant を選ぶ

戻り値を無視すると、取得できていないのに保護対象を使う経路が生じます。`down_interruptible()` などの結果を確認し、取得に成功した場合にだけ対応する `up()` を実行する構造にします。

```c
static int use_slot(void)
{
	int ret;

	ret = down_interruptible(&io_slots);
	if (ret)
		return ret;

	do_io();
	up(&io_slots);
	return 0;
}
```

実際の関数では、取得後のすべての exit path が枠を返すことを確認します。エラー分岐や早期 return がある場合、解放漏れは後続 task を永久に待たせる可能性があります。スコープ cleanup を使う場合も、対象の kernel API と既存コードの書き方に合わせます。

#### `up()` は owner に縛られない

`up()` は利用可能な枠を戻し、待ち task があれば起床対象にします。mutex の unlock と異なり、semaphore は owner を持たないため、必ずしも `down*()` を実行した task 自身が `up()` する必要はありません。
このため、取得 task と解放 task が異なる設計では、枠を返す責任と一回だけ返される保証を、状態遷移やエラー処理を含めて明確にします。二重の `up()` は意図しない並行数を許す原因になります。

#### 排他用途とカウント用途の違い

初期値 1 の semaphore は一度に一つの取得だけを許せますが、owner semantics がなく、mutex のような誤用検出・優先度継承の性質も同じではありません。
排他目的で使う既存コードを変更するときは、`up()` を別 task が呼ぶ必要が本当にあるか、sleep・signal・優先度 inversion の振る舞いが変わらないかを確認せず、機械的に mutex へ置換しないでください。
新しい設計では、数に基づく枠管理なら semaphore、task 所有の排他なら mutex、イベント完了の通知なら completion を候補として比較します。

#### Linux 6.18 LTS のソースを追う入口

- `down()` の deprecated 注記、各 acquire variant と戻り値: `kernel/locking/semaphore.c`
- semaphore の構造と interrupt-context に関する注記: `include/linux/semaphore.h`
- wait queue と起床処理: `kernel/locking/semaphore.c` の `__down_common()` / `__up()` 周辺
- mutex の owner semantics・RT の扱い: `Documentation/locking/locktypes.rst`

#### この小節で固定したいこと

- semaphore はカウント資源を管理し、所有者を持たない。mutex の単純な別名ではない
- sleep し得る `down*()` は基本的に sleep 可能な task context で使い、戻り値を処理する。IRQ / softirq では待たない API と実装上の条件を確認する
- `down_trylock()` は失敗時 1、成功時 0 という戻り値に注意する
- `up()` は取得 task 以外からも呼べるため、解放責任と二重解放防止を設計する
- 新しい用途では排他に mutex、イベント待ちに completion、並行数制限に semaphore を使い分ける

### 5.2.9 読み書き用セマフォ

#### 複数の読み手と排他的な書き手

Linux 6.18 の読み書き用セマフォは `struct rw_semaphore` を使い、API 名では `rwsem` と呼ばれます。複数の読み手が同時に保護区間へ入れますが、書き手は他の読み手・書き手が保護区間にいない状態でのみ取得できます。読み取りが多く、書き込みが相対的に少ない共有状態に適します。

```c
static DECLARE_RWSEM(config_sem);
static int config_value;

static int read_config(int *result)
{
	int ret;

	ret = down_read_interruptible(&config_sem);
	if (ret)
		return ret;

	*result = config_value;
	up_read(&config_sem);
	return 0;
}

static void update_config(int value)
{
	down_write(&config_sem);
	config_value = value;
	up_write(&config_sem);
}
```

ロックは保護対象へのアクセス規約として、読み書き双方のすべての経路で一貫して使います。読者が保護区間内でデータを書き換えたり、書き手がロックを取らずに更新したりすれば、rwsem を導入しても競合は防げません。

#### sleep するロックとして使う

`rw_semaphore` は sleeping lock です。競合時に task を待たせるため、取得 API はプリエンプト可能な task context から呼びます。ハード IRQ や softirq のように sleep できない文脈から取得してはいけません。また、読み取り側もロック保持中に任意の実行文脈へ移れるわけではなく、呼び出し経路の制約を確認します。

`down_read_interruptible()` と `down_read_killable()`、`down_write_killable()` は中断可能な取得を提供し、失敗時には負の errno を返します。結果を確認し、取得成功後だけ対応する `up_read()` / `up_write()` を実行します。`down_read_trylock()` と `down_write_trylock()` は待たずに試み、成功時 1、競合時 0 を返します。通常の semaphore の `down_trylock()` とは成功・失敗の戻り値が逆です。

#### 公平性と PREEMPT_RT

古い本文にある待ち行列の具体的な FIFO 処理や `rw_semaphore` の内部フィールドは、Linux 6.18 の固定的な実装説明として引き継ぎません。実装表現や待ち合わせの詳細はカーネル版・構成に依存します。

Linux 6.18 の locking 文書では、非 PREEMPT_RT の rwsem 実装は writer starvation を防ぐ公平な実装と説明されています。一方、PREEMPT_RT では別の rt_mutex ベースの実装となり、公平性の性質が変わります。とくに読み手がプリエンプトされたままロックを保持する場合、書き手の優先度継承では読み手を進行させられず、優先度の高い書き手も待つことがあります。性能や待ち時間を評価するときは、`CONFIG_PREEMPT_RT` の有無と実際の競合パターンを分けて確認します。

#### 書き込みロックの downgrade

`downgrade_write()` は、保持中の書き込みロックを読み取りロックへ移します。更新後も読み取り保護を継続したいとき、いったん `up_write()` してから `down_read()` を取り直す間に別の書き手が入る隙間を作らずに済みます。これは write から read への移行であり、read ロックを保持したまま write ロックへ昇格する手段ではありません。読み取り中に更新が必要になった場合は、いったん読み取りロックを解放し、書き込みロックを取得して状態を再確認する設計にします。

#### Linux 6.18 LTS のソースを追う入口

- 型、初期化、公開 API と戻り値: `include/linux/rwsem.h`
- 待ち合わせとロック取得・解放の実装: `kernel/locking/rwsem.c`
- sleep 可能な文脈、公平性、PREEMPT_RT の違い: `Documentation/locking/locktypes.rst` の `rw_semaphore`
- メモリ管理での使用例: `include/linux/mm_types.h` と `mm/` 配下の利用箇所。個々のデータ・経路の保護契約を確認する

#### この小節で固定したいこと

- `rw_semaphore` は複数読み手・単一書き手の sleeping lock で、競合時に待つ task context から使う
- 保護対象の全読み書き経路で同じ規約を守る。読み手の並行実行は読み取り専用であることが前提
- `down_*_trylock()` は成功 1、失敗 0。通常 semaphore の trylock と戻り値が逆
- 非 PREEMPT_RT と PREEMPT_RT では内部実装と公平性の性質が同じではない
- `downgrade_write()` は write から read への移行であり、read から write への昇格ではない

### 5.2.10 完了通知

#### 「完了した」というイベントを待つ

completion（完了通知）は、ある処理が特定の地点まで到達したことを別の実行経路へ知らせる同期プリミティブです。典型的には、task が作業を開始し、別の task やデバイス割り込みハンドラが処理完了を通知し、待っていた task が再開します。セマフォのように資源の利用枠を管理するのではなく、「このイベントが起きたか」を待ち合わせるために使います。

```c
struct request_state {
	struct completion finished;
	int result;
};

static void finish_request(struct request_state *req, int result)
{
	req->result = result;
	complete(&req->finished);
}

static int wait_request(struct request_state *req)
{
	int ret;

	ret = wait_for_completion_interruptible(&req->finished);
	if (ret)
		return ret;

	return req->result;
}
```

要求を発行する側は、非同期処理へ渡す前に `init_completion(&req->finished)` を呼びます。`req` とその completion は、通知側がアクセスを終えるまで有効でなければなりません。

completion は待つ側と通知側のどちらが先に動いても成立します。通知が先なら完了状態が記録され、後から `wait_for_completion*()` を呼んだ側は待たずに進みます。まだ通知されていなければ、wait API が task を待たせます。これにより、通知側が先に動いた場合を見落とすための独自フラグと待ちキューの競合処理を、利用側で組み立てずに済みます。

#### 待つ側と通知側の API

- `wait_for_completion()` は完了まで無期限に待ちます。通常は uninterruptible wait です
- `wait_for_completion_interruptible()` / `wait_for_completion_killable()` は中断可能な待ち合わせで、戻り値を確認します
- timeout 付き variant は、完了・タイムアウト・signal 中断の戻り値を区別して扱います。jiffies 単位の timeout は `msecs_to_jiffies()` などで変換します
- `complete()` は完了を記録し、待ち task があれば一つを起床させます。待ち手がまだいない場合でも通知は保持されます
- `complete_all()` は待ち手すべてを起床させ、以後の待ち手も完了扱いにします。再利用する場合は、前の利用サイクルが終了してから `reinit_completion()` します

`complete()` は IRQ / atomic context から呼べますが、待ち API は task を sleep させるため、IRQ、softirq、その他の atomic context では呼べません。`complete_all()` も通常の文脈では通知側で使えますが、Linux 6.18 の実装は PREEMPT_RT で threaded context を要求する assertion を持つため、hard IRQ から呼ぶ設計にはせず、対象構成で許される文脈を確認します。完了通知を送る処理と完了を待つ処理で、許される実行文脈が異なる点が重要です。

#### 生存期間と再利用の同期

completion オブジェクトは、通知側と待ち側の双方が最後にアクセスし終えるまで有効でなければなりません。スタック上の completion を使う場合、wait API が timeout や signal で先に戻っても、非同期の通知側が後からそのアドレスへアクセスする可能性があります。オブジェクトを解放・関数から復帰する前に、通知側の処理終了まで別途同期する必要があります。長寿命の要求・デバイス構造体に埋め込む設計は、この寿命管理をしやすくします。

同じ completion を複数回使うとき、`init_completion()` を再度呼ぶと待ちキューを再初期化して待ち手を失うおそれがあります。再利用には `reinit_completion()` を使いますが、並行する wait / complete / reinit の競合を自動解決するものではありません。前の利用サイクルの待ち手と通知側がいなくなったことを確かめてから再初期化します。`complete_all()` 後の再初期化にも同じサイクル管理が必要です。

completion は共有データを排他的に保護するロックではありません。通知前に生成した結果を待ち手が使うなら、適切な同期関係とオブジェクト寿命を保ちます。複数 task が共有状態を継続的に更新する場合は、別途 mutex や spinlock などで保護します。

#### Linux 6.18 LTS のソースを追う入口

- completion 型、初期化、wait / signal API: `include/linux/completion.h`
- 完了通知の利用上の注意、寿命、戻り値、実行文脈: `Documentation/scheduler/completion.rst`
- 待ち合わせと `complete()` / `complete_all()` の実装: `kernel/sched/completion.c`
- 実デバイスでの利用例: `drivers/` 配下で `init_completion()`、`wait_for_completion*()`、`complete()` の組を追い、エラー・timeout 時の寿命管理も読む

#### この小節で固定したいこと

- completion は資源の排他ではなく、特定イベントの完了を待つための仕組み
- 通知が待機より先でも状態が保持される。`complete()` は通常一つ、`complete_all()` はすべての待ち手を通す
- `complete()` は IRQ / atomic context から使えるが、sleep する wait API は task context に限る。PREEMPT_RT では `complete_all()` の実行文脈も確認する
- timeout / signal で待ちが先に戻る経路を含め、非同期の通知側が使い終わるまでオブジェクトを生存させる
- 再利用時は `reinit_completion()` を使い、前サイクルの wait / complete が残らないよう別途同期する

### 5.2.11 ローカル割り込み禁止

#### ローカル CPU のハード IRQ を一時的に抑止する

`local_irq_disable()` は呼び出した CPU のハードウェア割り込みを無効化し、`local_irq_enable()` は有効化します。`irqs_disabled()` は現在の CPU で割り込みが無効かどうかを調べる API です。これはローカルな実行文脈の制御であり、他 CPU の割り込みや実行を止める機能ではありません。

タスク文脈と同じ CPU のハード IRQ handler が共有データを触る場合、タスク側でローカル IRQ を無効にすれば、その CPU 上でのマスク可能なハード IRQ による割り込みを避けられます。しかし別 CPU は引き続き並行実行できます。共有データを複数 CPU から更新するなら、ローカル IRQ 制御だけでは足りず、CPU 間排他を行うロックなども必要です。NMI のように通常のローカル IRQ 無効化で抑止されない実行経路も、別途考慮します。

```c
static DEFINE_SPINLOCK(state_lock);
static unsigned long shared_state;

static void update_from_task(unsigned long value)
{
	unsigned long flags;

	spin_lock_irqsave(&state_lock, flags);
	shared_state = value;
	spin_unlock_irqrestore(&state_lock, flags);
}
```

この形は、非 PREEMPT_RT でタスクとハード IRQ handler が同じロックを使う典型例です。ロックが CPU 間の相互排他を担い、IRQ 制御は同じ CPU 上の handler がそのロックを再取得してデッドロックすることを避けます。IRQ handler 側も同じロックを正しい variant で使う必要があります。PREEMPT_RT では `spin_lock_irqsave()` の IRQ suffix が CPU の割り込み状態を無効化しないため、この例の実行文脈上の意味をそのまま当てはめません。ロックを併用しない単なる割り込み禁止は SMP のロックの代替ではありません。

#### 割り込み状態を保存して復元する

クリティカル区間を抜けるときに無条件で `local_irq_enable()` を呼ぶと、入る前から割り込み禁止だった呼び出し元の状態を壊す可能性があります。入れ子や既存の割り込み状態を保つ必要がある通常の形は `local_irq_save()` と `local_irq_restore()` の組です。

```c
static void update_local_state(void)
{
	unsigned long flags;

	local_irq_save(flags);
	/* ローカル IRQ と競合する短い処理 */
	local_irq_restore(flags);
}
```

`flags` は同じ呼び出しの保存・復元に対応させ、別の保存値で上書きしません。復元は保存時の状態に戻すため、元が有効なら有効に、無効なら無効のままになります。単純な `local_irq_disable()` / `local_irq_enable()` は、呼び出し前に割り込みが有効だと分かっていて、その前提を呼び出し階層全体で保証できる場合に限ります。

割り込みを無効にした区間では、sleep する処理や長い処理を行いません。IRQ が遅れて処理され、システムの応答性やデバイス処理の遅延に影響するため、対象を必要最小限に絞ります。また、IRQ 制御はメモリ共有の一般的なロックやメモリバリアと同じ保証ではありません。

#### PREEMPT_RT とアーキテクチャ依存性

Linux 6.18 の `local_irq_*()` はアーキテクチャが提供する IRQ 制御に接続され、lockdep 用の追跡なども API に含まれます。x86 の `cli` / `sti` や `EFLAGS.IF` を全アーキテクチャ共通の実装とみなさず、まず Linux API の契約を読みます。

PREEMPT_RT では `spinlock_t` が RT-mutex ベースとなるため、`local_irq_disable()` の後に通常の `spin_lock()` を取得する、非 RT 前提の組み合わせは成立しません。割り込み状態の制御とロックを別々に扱う必要がある場合は、`Documentation/locking/locktypes.rst` の該当ルールを確認し、per-CPU データに対する `local_lock` など適切な仕組みを検討します。ハード IRQ を実際に抑止する必要があるからといって、機械的に `raw_spinlock_t` へ置き換えるのではなく、共有相手と実行文脈を特定します。

#### Linux 6.18 LTS のソースを追う入口

- IRQ 制御 API と状態追跡: `include/linux/irqflags.h`
- アーキテクチャ固有の割り込み状態操作: `arch/*/include/asm/irqflags.h`
- 割り込み無効化と PREEMPT_RT・`local_lock` の規則: `Documentation/locking/locktypes.rst`
- IRQ がロックを共有する場合の使い分け: `Documentation/locking/spinlocks.rst`

#### この小節で固定したいこと

- `local_irq_disable()` は呼び出し元 CPU のマスク可能なハード IRQ を抑止する。SMP 全体の排他ではない
- 元の IRQ 状態を保つ必要がある区間は `local_irq_save(flags)` / `local_irq_restore(flags)` を対で使う
- IRQ 禁止区間は短くし、sleep する処理を置かない。NMI や他 CPU のアクセスも別に考える
- CPU 間排他はロックが担い、ローカル IRQ 制御は同一 CPU 上の IRQ 再入を防ぐなど別の役割を担う
- PREEMPT_RT では IRQ 制御と `spinlock_t` の組み合わせを非 RT の感覚で流用せず、lock type の契約を確認する

### 5.2.12 遅延処理の禁止と許可

#### bottom half 制御の対象

`local_bh_disable()` / `local_bh_enable()` は、呼び出した CPU における bottom half（主に softirq）の処理を一時的に抑止・再許可する API です。softirq として実行される tasklet もこの制御の対象になります。一方、ハード IRQ を禁止する API ではなく、workqueue のような独立した task context の処理すべてを止めるものでもありません。

```c
static LIST_HEAD(packet_queue);

static void inspect_queue_from_task(void)
{
	local_bh_disable();
	/* ローカル softirq と同時に実行させたくない短い参照 */
	inspect_packet_queue(&packet_queue);
	local_bh_enable();
}
```

bottom half の無効化はローカル CPU に対する制御です。他 CPU では同じ種類の softirq が並行して動く場合があり、同じデータを共有するなら CPU 間排他も必要です。また、この API だけで通常のデータロックやメモリ順序保証が得られるわけではありません。共有相手が task と softirq であるロック保護では、用途に応じて `spin_lock_bh()` / `spin_unlock_bh()` の組を使います。

#### 入れ子と再許可時の処理

`local_bh_disable()` は入れ子で呼べます。呼び出し回数と対応する `local_bh_enable()` を対にし、外側の無効化が残っている間に内側の enable が処理を再許可したと誤解しないようにします。状態管理の内部表現は Linux 6.18 では旧本文にある単純な `thread_info` のカウンタ説明に固定せず、`include/linux/bottom_half.h` と `kernel/softirq.c` の実装を参照します。

最後の `local_bh_enable()` で抑止を解除すると、その CPU に保留 softirq があれば、その場で処理される場合があります。呼び出し元がプリエンプト不可の文脈なら、実装は softirq daemon を起こすなど別経路へ回すこともあります。したがって enable の直後に「softirq が必ず実行済み」と仮定せず、長い処理やsleepを許可する API ともみなしません。`local_bh_enable()` はハード IRQ handler 内から使う API ではなく、Linux 6.18 実装も hardirq 文脈での呼び出しを警告します。

#### PREEMPT_RT で再確認すること

非 PREEMPT_RT では bottom-half disable はプリエンプション制御と結び付いた実装ですが、PREEMPT_RT では softirq 実行がプリエンプト可能になり、`local_bh_disable()` の内部管理も変わります。したがって、「softirq 文脈だからプリエンプトされない」「ローカル BH を無効にすればあらゆる実行経路に対して per-CPU データが直列化される」という前提は置きません。

とくに softirq context だけでアクセスする per-CPU データは、非 RT の非プリエンプト性に暗黙に依存しないようにします。Linux 6.18 の locking 文書は、必要に応じて `local_lock_t` と `local_lock_nested_bh()` / `local_unlock_nested_bh()` で保護範囲を明示し、lockdep の検証対象にする方法を説明しています。データが task、softirq、別 CPU のどこから参照・更新されるかを洗い出し、構成に適した保護を選びます。

#### Linux 6.18 LTS のソースを追う入口

- bottom-half API: `include/linux/bottom_half.h`
- softirq の pending 状態、実行、BH disable/enable の実装: `kernel/softirq.c`
- `local_bh_disable()` と PREEMPT_RT、per-CPU softirq データの保護: `Documentation/locking/locktypes.rst` の CPU local scope / bottom-half の項
- tasklet と softirq の関係: `include/linux/interrupt.h`、`kernel/softirq.c`

#### この小節で固定したいこと

- `local_bh_disable()` はローカル CPU の softirq 等の bottom-half 実行を抑止する。hard IRQ や workqueue 全般を止めるものではない
- 入れ子の disable / enable を対にし、最後の enable で保留 softirq が実行される可能性を見込む
- bottom-half 制御はSMP全体のロックではない。別 CPU との共有や一般のデータ競合は別途保護する
- PREEMPT_RT では BH 制御と softirq の実行・プリエンプション特性が変わる。暗黙の非プリエンプト性に依存しない
- softirq 専用の per-CPU データなどでは、必要に応じて `local_lock_nested_bh()` 系と lockdep の契約を確認する

## 5.3 カーネルデータアクセスの同期

### 並列性と保護の範囲を同時に設計する

共有データの同期は、競合を止めるだけでなく、必要な並列実行を残す設計です。単一のカウンタ、複数の読み手を許す状態、イベント完了、リンク構造の更新では、守るべき不変条件も適切な同期方法も異なります。割り込み禁止やスピンロックを短くすれば常に性能が向上するわけでもなく、待ち時間、キャッシュ競合、実行文脈による制約を合わせて見ます。

候補を選ぶ前に、少なくとも次を特定します。

| 確認点 | 読み解く内容 |
|---|---|
| 共有状態 | どのフィールドやオブジェクト間の不変条件を一緒に守るか |
| 実行経路 | task、hard IRQ、softirq、workqueue、別 CPU のどこからアクセスするか |
| 操作の性質 | 単一値の更新か、複数フィールドの一体更新か、読み取りと書き込みの比率はどうか |
| 待機可能性 | 競合時に sleep できるか、即時に戻る必要があるか |
| 寿命と公開 | ポインタの公開、参照中のオブジェクト寿命、削除後の回収をどう保証するか |
| 構成依存性 | PREEMPT_RT、SMP、アーキテクチャごとの API 契約に差がないか |

たとえば整数一つの加算なら atomic API が候補になりますが、複数フィールドの整合性まで自動的に保つわけではありません。読み取り中心なら seqlock や RCU が候補になることがありますが、再試行やオブジェクト寿命を別に設計します。短い共有更新には spinlock 系、待ちを許容する task 間の保護には mutex などが候補です。どの方式も、保護対象へアクセスする全経路が同じ契約に従って初めて成立します。

局所的な割り込み禁止や bottom-half 禁止は、同じ CPU 上の特定の再入経路を制御する手段です。それだけで SMP 全体の相互排他やデータ構造の一貫性を得られるわけではありません。同様に、ロックがプリエンプションや割り込み状態へ与える効果もロック型・suffix・PREEMPT_RT によって異なります。「ロックを取ればすべての実行文脈が止まる」という一般化を避け、API ごとの契約を確認します。

本節ではまず、例外・割り込み・遅延処理のどの組み合わせが共有データへ触れるかを整理し、その組み合わせに応じた保護を見ます。原文の表や古い実装説明は候補を考える入口として使い、Linux 6.18 の実行文脈、ロック API、構成条件に照らして読み替えます。

#### Linux 6.18 LTS のソースを追う入口

- ロック種別と実行文脈ごとの規則: `Documentation/locking/locktypes.rst`
- スピンロックと IRQ / bottom-half suffix: `Documentation/locking/spinlocks.rst`
- 同期 API の宣言と利用契約: `include/linux/` の各ロックヘッダー、および `Documentation/locking/`
- 個別データの保護規則: 対象構造体の定義から全 read / write access とロック取得箇所を追う

### 5.3.1 スピンロック、セマフォおよび割り込み禁止の選択

#### 「どの文脈が触るか」から保護を決める

共有データを守る方法は、アクセス元が system call か IRQ かという名前だけでは決まりません。同じ CPU で割り込まれて再入するか、複数 CPU から並行してアクセスされるか、競合時に task を sleep させられるか、PREEMPT_RT で意味が変わるかを確認します。古い本文の「単一 CPU」と「マルチ CPU」の表は候補を考える入口として有用ですが、Linux 6.18 のロックが取得時に一律で IRQ・softirq・プリエンプションを禁止するという意味には読み替えません。

| 主なアクセス文脈 | 保護を考えるときの軸 |
|---|---|
| task と task | sleep 可能な区間なら mutex などの sleeping lock。atomic context なら sleep しないロックを検討する |
| task と hard IRQ | SMP 排他に共有ロックが必要。同じ CPU 上の再入も防ぐ必要があり、非 RT では task 側の `spin_lock_irqsave()` が典型。PREEMPT_RT の IRQ 実行方式とロック型を別途確認する |
| task と softirq | softirq と共有する task 側区間では、用途に応じて `spin_lock_bh()` を使う。別 CPU の softirq に対する排他はロックが担う |
| hard IRQ 同士 | IRQ handler はsleepできない。共有ロックを使う場合、同一 CPU のネストと別 CPU からの並行実行の双方を調べる |
| softirq 同士 | 同じ種類の softirq が CPU をまたいで並行実行し得る。ローカル BH 制御だけで全 CPU の共有状態を直列化しない |
| 読み取り主体の共有状態 | 共有の仕方と待てる文脈に応じて rwlock、rwsem、seqlock、RCU などを比較し、寿命管理や再試行も含めて設計する |

これは候補の一覧であり、行だけ見てロックを機械的に決める表ではありません。たとえば IRQ handler がロックを取る経路では、そのロック待ちの間に sleep できないため、task 側だけを mutex で保護する設計は成立しません。逆に、task 間だけの保護に割り込み禁止や raw spinlock を加える必要は通常ありません。アクセス経路ごとに同じ保護規約を守ることが前提です。

#### ローカル制御と CPU 間ロックの役割を分ける

`local_irq_disable()` は呼び出し CPU のマスク可能な hard IRQ を抑止し、`local_bh_disable()` はローカルの bottom half の実行を抑止します。どちらも他 CPU での並行アクセスを止めません。共有する CPU 間の相互排他はロックや用途に合った同期機構が担います。

非 PREEMPT_RT では `spin_lock_irqsave()` の組み合わせにより、ロックで CPU 間を排他しながら、保存したローカル IRQ 状態を復元できます。`spin_lock_bh()` は task と bottom half の同一 CPU 上の競合を扱う選択肢です。一方、PREEMPT_RT では `spinlock_t` の IRQ suffix が CPU の IRQ 状態を無効化せず、`spinlock_t` 自体のブロック特性も変わります。hard IRQ と共有するデータの実際の保護方法は、threaded IRQ を含む実行形態と `Documentation/locking/locktypes.rst` の契約を確認して決めます。

#### atomic・RCU をロックの代用と決めつけない

atomic API は単一の値に対する個別操作を不可分にできますが、複数フィールドからなる不変条件をまとめて保護するものではありません。ポインタの公開や解放後の回収を扱う場合も、単なる atomic ポインタ操作では寿命が保証されません。RCU などの公開・参照・回収プロトコルを含めて選びます。

読み取りが多い場合も、読み手を増やせば速いと先に決めないようにします。`rwlock_t` は読み手も原子的なロック操作を行い、`rw_semaphore` はsleep可能な task context 向けです。seqlock の読者は再試行を受け入れる必要があり、RCU では更新後の回収まで設計します。これらの適否は、保護データの更新頻度・読み取りの長さ・実行文脈で判断します。

#### Linux 6.18 LTS のソースを追う入口

- 実行文脈とロック種別の規則: `Documentation/locking/locktypes.rst`
- IRQ と共有するスピンロックの考え方: `Documentation/locking/spinlocks.rst`
- IRQ / bottom-half 制御 API: `include/linux/irqflags.h`、`include/linux/bottom_half.h`
- PREEMPT_RT のロックと割り込み suffix: `Documentation/locking/locktypes.rst` の `spinlock_t and PREEMPT_RT`

#### この小節で固定したいこと

- 同期方式を選ぶ前に、共有状態・全アクセス文脈・再入可能性・SMP 並行性・sleep 可否を洗い出す
- ローカル IRQ / bottom-half 禁止はCPU局所の制御であり、CPU間ロックの代わりではない
- atomic は複数フィールドの不変条件やポインタ寿命を自動では守らない
- PREEMPT_RT では `spinlock_t` と IRQ suffix の意味が変わるため、非 RT の表や慣習をそのまま適用しない

#### 5.3.1.1 例外がアクセスするデータの保護

##### システムコールは task context で実行される

古い本文でいう「例外処理」の代表例は、ユーザー空間からシステムコールで入るカーネル処理です。ただし、Linux の用語ではシステムコール入口を CPU 例外・割り込みと同じ実行文脈として扱わず、呼び出し元 task の task context として考えます。システムコール処理は必要に応じて sleep できるため、共有資源を複数 task が操作する場合は mutex などの sleeping lock が適することがあります。

```c
static DEFINE_MUTEX(config_mutex);
static int config_value;

static int update_config(int value)
{
	int ret = 0;

	if (value < 0)
		return -EINVAL;

	mutex_lock(&config_mutex);
	if (value > CONFIG_LIMIT)
		ret = -ERANGE;
	else
		config_value = value;
	mutex_unlock(&config_mutex);

	return ret;
}
```

ロックを取る前に検証できる入力は先に検証し、共有状態の読み書きに必要な範囲だけを保護します。全ての task 側アクセスが同じ mutex 規約に従えば、同一 CPU 上でロックを持つ task がプリエンプトされ、その後別 task が同じ mutex を要求しても、後者は待機し、前者が再実行されてロックを解放できます。これを理由に CPU 間の同期やロック自体が不要になるわけではありません。

##### 「例外」と名の付く処理を一括りにしない

システムコールのように task context で動く経路と、hard IRQ handler のように sleep できない経路は同期の選択が異なります。また、ページフォルトなどの同期例外も、処理内容・ロック保持状況・メモリ管理の制約によって sleep の可否が異なるため、「例外ハンドラなら mutex を取れる」と一般化できません。実際の入口から関数呼び出しを追い、atomic context か、既にどのロックを保持しているかを確認します。

カウント資源の上限を管理する場合は semaphore が候補ですが、task 間の単純な相互排他には mutex を優先します。Linux 6.18 では mutex は owner semantics を持ち、セマフォは owner を持ちません。データの排他と「同時に何個まで許すか」は別の要件です。

##### Linux 6.18 LTS のソースを追う入口

- システムコールの入口と task context: `arch/*/entry/` 配下の syscall entry、および `kernel/entry/`
- mutex の API と利用条件: `include/linux/mutex.h`、`Documentation/locking/`
- mutex の wait / wake 実装: `kernel/locking/mutex.c`
- 対象共有データの保護規約: 構造体定義から syscall、workqueue、IRQ など全アクセス経路を追う

##### この小節で固定したいこと

- システムコールは通常、呼び出し元 task の task context で動き、sleep 可能な区間では mutex を使える
- task 間の同期は mutex 自体が担う。プリエンプション禁止を追加して代用する必要はない
- 「例外ハンドラ」という呼び方だけでは sleep 可否を判断できない。システムコール、ページフォルト、IRQ などの実際の文脈を特定する
- mutex は相互排他、semaphore はカウント資源の管理という要件の違いで選ぶ
