---
title: "Let's EncryptでSSLで暗号化する"
---

# Let's EncryptでSSLで暗号化する

お疲れ様です。
レンタルサーバーなどですぐ利用できるWordPressサービスとかだとサービス提供会社が無料のSSL証明書「Let's Encrypt」を管理機能から申し込みできるけど、シェルログインが許されているVPSサーバーやベアメタルサーバー、もしくは、自身の会社などに立てたオンプレサーバーなどでは、直接「Let's Encrypt」に手続きしないといけません。

しかし、手続きは意外と簡単でLet’s EncryptサーバーとCertbotクライアント間で通信して**10分もあればSSL証明書が入手**できます。

証明書を設定しようとしているサーバー機でSSHでシェルにログインしrootになり、Certbotクライアントをインストールします。
インストールしたCertbotクライアントが、Let’s Encrypt のサーバーとやり取りすることで証明書の発行と設定を自動的に行います。
※設定ファイルの手動編集は、あります。

---

## １　まずは、現在稼働中のhttpd(apache)のバージョンを確認する。

```bash
httpd -v
```
出力
```text
Server version: Apache/2.4.62 (CentOS Stream)
Server built: Aug 3 2024 00:00:00
```

うんうん現在の最新になっている。

## ２　Certbotクライアントをインストール

Certbotクライアントは、EPELリポジトリからインストールことが出来る。
EPELリポジトリ（Extra Packages for Enterprise Linux）は、Red Hat Enterprise Linux (RHEL)およびその派生ディストリビューション（CentOS、Rocky Linux など）向けの追加パッケージを提供するリポジトリ。
EPELは、オープンソースのパッケージを集めており、多くの開発者が利用する。
EPELリポジトリには、以下のようなパッケージが含まれる
◆さまざまなツールやライブラリを提供する。
◆セキュリティ関連のパッケージを含む。
◆ネットワーク関連のパッケージを含む。
◆デバッグやテストツールを提供する。
EPELリポジトリをインストールした上で、certbotとpython-certbot-apacheをインストールする。
※python-certbot-apacheは、Let's EncryptのCertbotプロジェクトの一部で、Apache HTTPサーバーと統合されたツールです。
このツールを使用すると、以下のようなことができます。
〇SSL証明書の自動取得とインストール
　Let's Encryptから無料のSSL証明書を取得し、Apacheサーバーに　自動的にインストールします。
〇HTTPS設定の自動化
　Apacheの設定を自動的に調整し、HTTPSトラフィックを有効にします。
〇証明書の自動更新：証明書の有効期限が切れる前に自動的に更新します。
　systemdタイマーを自動で設定してくれる。
　全部終わったら systemdctl list-timers で確認する。

今回はすでにepel-release　をインストールしているので念のためアップデートを行う。

```bash
dnf -y update epel-release
```

出力
```text
メタデータの期限切れの最終確認: 0:41:59 前の 2025年01月13日 14時40分40秒 に実施しました。
依存関係が解決しました。
行うべきことはありません。
完了しました!
```

ということで更新はない模様なので進める。

## ３　certbotとpython-certbot-apacheをインストールする

```bash
dnf install certbot python-certbot-apache
```

※普通なら問題なくインストール終了する。

## ４　Let's Encryptクライアントを実行して証明書を作成する

ここでは、ApachehttpdのDocumentRootが私の環境の/var/www/htmlに設定されているとして説明する。
実際のDocumentRootの設定に変換して設定する。（DocumentRootはhttpd.confを参照）
ではcertbotコマンドを実行する。
-d オプションには、証明書を発行するサーバーのドメイン、-wにはDocumentRootのパスを指定する。
※certbotコマンドは、対話式です。

```bash
certbot certonly --webroot -w /var/www/html/ -d www.redzetm.net -d redzetm.net
```

コマンドの詳細：
certonly：証明書のみを取得し、インストール作業は手動で行うことを意味します。
--webroot：ウェブルート（Webroot）方式を指定します。この方式では、ウェブサーバーのルートディレクトリに特定のファイルを配置してドメインの所有権を確認します。
-w /var/www/html/：ウェブサーバーのルートディレクトリを指定します。この場合、/var/www/html/ がウェブルートです。
-d www.redzetm.net：証明書を取得したいドメイン名を指定します。※-dオプションで、2つを指定してもOK

このコマンドを実行すると、Let's Encryptが指定されたウェブルートディレクトリに所有権確認用のファイルを配置し、そのファイルをアクセスして確認します。
確認が成功すると、証明書が発行されます。
Certbotの実行時に次のようなプロンプトが表示される場合があります。指示に従って設定を完了します。
証明書の更新も自動で行われるように設定すると便利です。
証明書の取得後、Apacheの設定ファイル（/etc/httpd/conf.d/ssl.conf など）を編集して、新しい証明書を使用するように設定する必要があります。
設定が完了したら、Apacheサーバーを再起動して変更を適用します。

対話式で進めていきます。
```text
certbot certonly --webroot -w /var/www/html/ -d www.redzetm.net -d redzetm.net

Saving debug log to /var/log/letsencrypt/letsencrypt.log
Enter email address (used for urgent renewal and security notices)
(Enter 'c' to cancel):
和訳
デバッグログを /var/log/letsencrypt/letsencrypt.log に保存しています
メールアドレスを入力してください（緊急の更新やセキュリティ通知に使用されます）
（キャンセルするには 'c' を入力）：

red.ze.tm@gmail.com

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Please read the Terms of Service at
https://letsencrypt.org/documents/LE-SA-v1.4-April-3-2024.pdf. You must agree in
order to register with the ACME server. Do you agree?
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
(Y)es/(N)o:
和訳
Let's Encryptの利用規約を https://letsencrypt.org/documents/LE-SA-v1.4-April-3-2024.pdfで確認してください。
ACMEサーバーに登録するには、同意する必要があります。同意しますか？
(Y)es/(N)o:

Y

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Would you be willing, once your first certificate is successfully issued, to
share your email address with the Electronic Frontier Foundation, a founding
partner of the Let's Encrypt project and the non-profit organization that
develops Certbot? We'd like to send you email about our work encrypting the web,
EFF news, campaigns, and ways to support digital freedom.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
(Y)es/(N)o:
和訳
最初の証明書が正常に発行された後、Let's Encryptプロジェクトの創設パートナーであり、Certbotを開発している非営利団体である
Electronic Frontier Foundation（EFF）とあなたのメールアドレスを共有することに同意いただけますか？
私たちは、ウェブの暗号化に関する作業、EFFのニュース、キャンペーン、およびデジタル自由を支援する方法についての
メールを送信したいと考えています。
(Y)es/(N)o: N
※Nとしたが、メールを受け取りたい場合はYとする。Nだからと言って証明書が取得できないことはないです。

Y

Account registered.
Requesting a certificate for www.redzetm.net and redzetm.net

Successfully received certificate.
Certificate is saved at: /etc/letsencrypt/live/www.redzetm.net/fullchain.pem
Key is saved at: /etc/letsencrypt/live/www.redzetm.net/privkey.pem
This certificate expires on 2025-04-13.
These files will be updated when the certificate renews.
Certbot has set up a scheduled task to automatically renew this certificate in the background.

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
If you like Certbot, please consider supporting our work by:
* Donating to ISRG / Let's Encrypt: https://letsencrypt.org/donate
* Donating to EFF: https://eff.org/donate-le
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
和訳
証明書の取得に成功しました。
証明書は /etc/letsencrypt/live/www.redzetm.net/fullchain.pem に保存されています。
キーは /etc/letsencrypt/live/www.redzetm.net/privkey.pem に保存されています。
この証明書の有効期限は2025年4月13日です。 これらのファイルは証明書が更新されるときに自動的に更新されます。
Certbotは、この証明書をバックグラウンドで
自動的に更新するためのスケジュールタスクを設定しました。
Certbotを気に入っていただけたら、以下の方法で私たちの活動を支援することをご検討ください：
ISRG / Let's Encryptへの寄付: https://letsencrypt.org/donate
EFFへの寄付: https://eff.org/donate-le
```

とりあえず上手くインストール出来ました。

/etc/letsencrypt/live/www.redzetm.net　に以下のファイルが出来ました。うんうん成功です。
```text
-rw-r--r-- 1 root root 692 1月 13 16:10 README
lrwxrwxrwx 1 root root 39 1月 13 16:10 cert.pem -> ../../archive/www.redzetm.net/cert1.pem
lrwxrwxrwx 1 root root 40 1月 13 16:10 chain.pem -> ../../archive/www.redzetm.net/chain1.pem
lrwxrwxrwx 1 root root 44 1月 13 16:10 fullchain.pem -> ../../archive/www.redzetm.net/fullchain1.pem
lrwxrwxrwx 1 root root 42 1月 13 16:10 privkey.pem -> ../../archive/www.redzetm.net/privkey1.pem
```

## ５　Apacheで使えるようにする設定

/etc/httpd/conf.d/ssl.confをvimで編集する。
```text
SSLCertificateFile /etc/letsencrypt/live/www.redzetm.net/cert.pem
SSLCertificateKeyFile /etc/letsencrypt/live/www.redzetm.net/privkey.pem
SSLCertificateChainFile /etc/letsencrypt/live/www.redzetm.net/chain.pem
```

※それぞれ実情に沿ってを編集する。

## ６　httpdの再起動

```bash
systemctl restart httpd
```

この時、原因不明のエラーで再起動しなかった。
ログにAH00526: Syntax error on line 5 of /etc/httpd/conf.d/ssl.confとあり、/etc/httpd/conf.d/ssl.confの5行目にシンタックスエラーだとのこと。
調べたら Listen 443 の設定で確か、httpd.conf にあとでSSLもインストールするからListen 80 と Listen 443を設定していたのを思い出した。
httpd.confの方のListen 443を削除し再度systemctl restart httpdしたら問題なく起動した。

```bash
systemctl status httpd
```

```text
● httpd.service - The Apache HTTP Server
Loaded: loaded (/usr/lib/systemd/system/httpd.service; enabled; preset: disabled)
Active: active (running) since Mon 2025-01-13 16:44:54 JST; 24s ago
Docs: man:httpd.service(8)
Main PID: 48956 (httpd)
Status: "Total requests: 0; Idle/Busy workers 100/0;Requests/sec: 0; Bytes served/sec: 0 B/sec"
Tasks: 177 (limit: 204624)
Memory: 42.9M
CPU: 80ms
CGroup: /system.slice/httpd.service
├─48956 /usr/sbin/httpd -DFOREGROUND
├─48957 /usr/sbin/httpd -DFOREGROUND
├─48958 /usr/sbin/httpd -DFOREGROUND
├─48959 /usr/sbin/httpd -DFOREGROUND
└─48960 /usr/sbin/httpd -DFOREGROUND

1月 13 16:44:54 hsv.redzetm.net systemd[1]: Starting The Apache HTTP Server...
1月 13 16:44:54 hsv.redzetm.net httpd[48956]: Server configured, listening on: port 443, port 80
1月 13 16:44:54 hsv.redzetm.net systemd[1]: Started The Apache HTTP Server.
```

## ７　firewall-cmdでSSLのport443を解放する

```bash
firewall-cmd --add-service=https --zone=public --permanent
firewall-cmd --reload
```

```text
PORT STATE SERVICE
22/tcp open ssh
80/tcp open http
443/tcp open https　　#うんうん問題なし！
```

## ８　テストする

ドメインにアクセスしてhttps://で表示出来ればOK

