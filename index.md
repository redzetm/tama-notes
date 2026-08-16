---
title: "奥玉直宏 技術研究室"
---

# 奥玉直宏 技術研究室

Linux、C言語、Bash、システムプログラミング、低レイヤ技術を中心とした個人技術メモ。

この研究室では、日々の試行錯誤や考察、設定記録などをMarkdownで残していきます。  

## メンバ紹介
- 私（要件定義・設計・レビュー・実装・テスト・運用担当）
- ぴこたん（MS365 Copilot）相談役
- ぴこぴん（GitHub Copilot）設計・レビュー・実装・テストなどパートナー

## コーディング
- 各コーディングは、Vibe Codingを行い、レビューは、私が担当
- もしくは、補完機能なしでコーディングして、レビューは、AI担当。
- VScodeを利用
- 記載内容は基本動作確認およびテストを実施済み

## 環境
- サーバー　富士通プライマジー RockyLinux9.7　Intel(R) Xeon(R) CPU E3-1230 V2 @ 3.30GHz　MEM32GB　HDD2TB 仮想マシンマネージャのサーバとして利用
- 開発環境　Ubuntu24.04LTS（プライマジーの仮想マシンマネージャのゲスト）
- 自作OS　UmuOS0.1.4-stable（観測・研究用OS）（プライマジーのターミナルから起動常駐）
- 各サーバー、仮想マシンには、SSH接続とtelnet接続（観測のため）
- ローカルPC　MINISFORUM　Windows11 Pro 25H2 12th Gen Intel(R) Core(TM) i9-12900HK (2.50 GHz) MEM32GB SSD1TB

## 研究メモ
- [運用コマンド man風ノート インデックス（Rocky Linux 9.7）]({{ '/man_index.html' | relative_url }})
- [C言語ライブラリ／システムコール man風ノート インデックス]({{ '/lib-syscall_index.html' | relative_url }})
- [開発現場でのSELinuxの位置づけとdisable]({{ '/selinux.html' | relative_url }})
- [Let's EncryptでSSLで暗号化する（Certbot + Apache）]({{ '/setup-lets-encrypt.html' | relative_url }})
- [今どきは必須のSSL/TLSの実装とカーネルSSL（KTLS）通信の基本]({{ '/ssl_tls.html' | relative_url }})
- [再現可能な自作OS UmuOS-0.1.4-base-stable（telnetd対応ベース安定版）]({{ '/umuos-0.1.4-base-stable.html' | relative_url }})
- [再現可能な自作OS UmuOS-0.1.7-base-stable（ネットワーク/ログ/認証の観測用・固定版ベース）]({{ '/umuos-0.1.7-base-stable.html' | relative_url }})
- [再現可能な自作OS UmuOS-0.1.xの起動メカニズム徹底解説]({{ '/umuos-0.1.7-base-stable起動メカニズム.html' | relative_url }})
- [UmuOSの為のC言語１（初級）　基礎～ループ編]({{ '/umuOSの為のC言語1.html' | relative_url }})
- [UmuOSの為のC言語２（初級）　配列・関数・基本型 編]({{ '/umuOSの為のC言語2.html' | relative_url }})
- [UmuOSの為のC言語３（初級）　マクロ・入出力・文字列 編]({{ '/umuOSの為のC言語3.html' | relative_url }})
- [UmuOSの為のC言語４（初級）　ポインタ・文字列とポインタ 編]({{ '/umuOSの為のC言語4.html' | relative_url }})
- [UmuOSの為のC言語５（初級）　構造体・ファイル処理編]({{ '/umuOSの為のC言語5.html' | relative_url }})
- [UmuOSの為のC言語１（中級）　1章　概要および主要概念]({{ '/umuOSの為のC言語（中級）1.html' | relative_url }})
- [UmuOSの為のC言語２（中級）　2章　ファイルI/O]({{ '/umuOSの為のC言語（中級）2.html' | relative_url }})
- [UmuOSの為のC言語３（中級）　3章　I/Oのバッファリング]({{ '/umuOSの為のC言語（中級）3.html' | relative_url }})
- [UmuOSの為のC言語４（中級）　4章　高度なファイルI/O]({{ '/umuOSの為のC言語（中級）4.html' | relative_url }})
- [UmuOSの為のC言語５（中級）　5章　プロセス管理]({{ '/umuOSの為のC言語（中級）5.html' | relative_url }})
- [UmuOSの為のC言語６（中級）　6章　高度なプロセス管理]({{ '/umuOSの為のC言語（中級）6.html' | relative_url }})
- [UmuOSの為のC言語７（中級）　7章　ファイル、ディレクトリの管理]({{ '/umuOSの為のC言語（中級）7.html' | relative_url }})
- [UmuOSの為のC言語８（中級）　8章　メモリ管理]({{ '/umuOSの為のC言語（中級）8.html' | relative_url }})
- [UmuOSの為のC言語９（中級）　9章　シグナル]({{ '/umuOSの為のC言語（中級）9.html' | relative_url }})
- [UmuOSの為のC言語１０（中級）　10章　時間]({{ '/umuOSの為のC言語（中級）10.html' | relative_url }})
- [UmuOSの為のC言語１１（中級）　付録　現代の Linux/C 設計で知っておく GCC/Clang 系拡張]({{ '/umuOSの為のC言語（中級）11.html' | relative_url }})
- [TCP/IP通信解析の為のC言語]({{ '/tcpip_c_analysis.html' | relative_url }})
- [UmuOSの為のC言語　Linuxシステムコール、低レベルライブラリ関数１（ファイルI/O）]({{ '/system_call_lib1.html' | relative_url }})
- [徹底解説！自作シェル ush（うーしゅ）0.0.6解説書]({{ '/ush-0.0.6徹底解説.html' | relative_url }})
- [徹底解説！自作シェル ush（うーしゅ）0.0.6開発の考え方]({{ '/ush-0.0.6開発の考え方.html' | relative_url }})
- [徹底解説！自作エディタuim（ゆーあいえむ）0.0.2徹底解説書]({{ '/uim-0.0.2徹底解説.html' | relative_url }})
- [ソフトウェア開発者またはシステム基盤担当でも最低限必要なTCP/IP知識]({{ '/tcpip.html' | relative_url }})

※ 随時追加予定
