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
- VScodeを利用
- 記載内容は基本動作確認およびテストを実施済み

## 環境
- サーバー　富士通プライマジー RockyLinux9.7　Intel(R) Xeon(R) CPU E3-1230 V2 @ 3.30GHz　MEM32GB　HDD2TB 仮想マシンマネージャのサーバとして利用
- 開発環境　Ubuntu24.04LTS（プライマジーの仮想マシンマネージャのゲスト）
- 自作OS　UmuOS0.1.4-stable（観測・研究用OS）（プライマジーのターミナルから起動常駐）
- 各サーバー、仮想マシンには、SSH接続とtelnet接続（観測のため）
- ローカルPC　MINISFORUM　Windows11 Pro 25H2 12th Gen Intel(R) Core(TM) i9-12900HK (2.50 GHz) MEM32GB SSD1TB

## 研究メモ
- [開発現場でのSELinuxの位置づけとdisable]({{ '/selinux.html' | relative_url }})
- [Let's EncryptでSSLで暗号化する（Certbot + Apache）]({{ '/setup-lets-encrypt.html' | relative_url }})
- [今どきは必須のSSL/TLSの実装とカーネルSSL（KTLS）通信の基本]({{ '/ssl_tls.html' | relative_url }})
- [再現可能な自作OS UmuOS-0.1.4-base-stable（telnetd対応ベース安定版）]({{ '/umuos-0.1.4-base-stable.html' | relative_url }})
- [ソフトウェア開発者またはシステム基盤担当でも最低限必要なTCP/IP知識]({{ '/tcpip.html' | relative_url }})

※ 随時追加予定
