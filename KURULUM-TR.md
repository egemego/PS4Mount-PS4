# PS4Mount 0.7.4-tr1: Türkçe kurulum rehberi

PS4 12.02, harici exFAT SSD ve kurulmadan oynanan pkg'ler için hazırlandı.

> [!WARNING]
> Bu sürüm bilgisayardaki tüm testlerden geçti ama **henüz bir konsolda denenmedi**. PS4Mount'un kendisi de 12.02'de değil, 13.52 ve 6.72'de denendi. İlk kullanımdan önce kayıtlarını yedekle (aşağıda 6. bölüm).

## Kısaca: ne olur, ne olmaz

| İstek | Durum |
| --- | --- |
| SSD'deki pkg'leri kurmadan oynamak | Evet. Oyun verisi iç diske kopyalanmaz, oyun doğrudan SSD'deki pkg'den okunur. |
| Güncelleme ve DLC pkg'leri | Evet. Her pkg içindeki Title ID'ye (CUSAxxxxx) göre eşleşir: GTA V güncellemesi yalnızca GTA V ile, God of War güncellemesi yalnızca God of War ile çalışır. Bunu kanıtlayan otomatik bir test de eklendi. |
| SSD'yi çıkarıp PC'de pkg eklemek | Evet. SSD exFAT kalır, PC onu normal bir disk gibi görür. |
| Kayıt yerini seçmek | Kısmen, bilinçli olarak böyle: oyun kayıtlarını her zaman konsolda kullanır, her oturumdan sonra SSD'ye otomatik bir kopya alınır ve panelden geri yüklenir. Kayıtların *yalnızca* SSD'de durması seçeneği yok: dinlenme modu USB'nin gücünü keser, sürücü de çekilebilir, yani kayıt yazılırken yarıda kalabilirdi. |
| İç diske hiç dokunmamak | Neredeyse. DLC'ler kurulmadan kullanılamaz (sistem yalnızca kurulu DLC'yi görür), bu yüzden iç diske kurulurlar. Bunu "Konsolda boş bırakılacak alan" ayarı sınırlar. PS4Mount'un kendi küçük dosyaları da (oyun başına birkaç MB) konsolda durur. |

## 1. Konsolun: 12.02 ve Aeolia A2

- **12.02 jailbreak'lenebilen bir sürüm.** Çekirdek açığı *Lapse*'tir (5.00–12.02 arası, 12.50'de kapatıldı). GoldHEN 12.02'yi destekliyor. Giriş yolları: tarayıcı (WebKit), BD-JB diski, Lua oyun kaydı, Netflix uygulaması, Vue uygulaması. GoldHEN'in Sistem Bilgisi ekranında "Southbridge: Aeolia A2" yazısını gördüysen GoldHEN zaten çalışıyor demektir.
- **Aeolia**, ilk "fat" PS4 kasasının (CUH-10xx) southbridge'idir. PS4Mount kasa modeline ya da southbridge'e bağlı hiçbir kod içermiyor (kaynakta kontrol ettim), bu yüzden bir farkı yok.
- PS4Mount'un derlendiği SDK 12.02'nin çekirdek adreslerini biliyor (12.00 ile aynı).
- 12.02'de jailbreak kalıcı değildir: **her yeniden başlatmadan sonra** jailbreak'i, GoldHEN'i ve PS4Mount'u tekrar yüklemen gerekir.

## 2. SSD'yi PC'de hazırlamak

1. SSD'deki önemli dosyaları yedekle; biçimlendirme her şeyi siler.
2. Windows'ta **Disk Yönetimi**'ni aç: disk **MBR**, tek bölüm, **exFAT** olsun (GPT disklerde PS4 sürücüyü görmeyebiliyor).
3. SSD'yi PS4'te **"Genişletilmiş depolama olarak biçimlendir"** deme: o zaman disk şifrelenir ve PC'de açılmaz.
4. SSD'nin köküne **`PS4Mount`** adında bir klasör aç ve pkg'leri içine koy (iki alt klasör derinliğine kadar). Dosya adları önemli değil:

```
SSD:\PS4Mount\GTA V\gta5-oyun.pkg
SSD:\PS4Mount\GTA V\gta5-guncelleme.pkg
SSD:\PS4Mount\God of War\gow-oyun.pkg
SSD:\PS4Mount\God of War\gow-guncelleme.pkg
SSD:\PS4Mount\God of War\gow-dlc.pkg
```

İlk taramadan sonra PS4Mount bunları aynı disk içinde taşır (kopyalamaz, yalnızca yeniden adlandırır):

```
SSD:\ps4\CUSA00411\app.pkg                      GTA V
SSD:\ps4\CUSA00411\patch.pkg                    GTA V güncellemesi
SSD:\ps4\CUSA07408\app.pkg                      God of War
SSD:\ps4\CUSA07408\patch.pkg                    God of War güncellemesi
SSD:\ps4\CUSA07408\addcont\...\ac.pkg           God of War DLC'si
SSD:\PS4Mount\saves\<kullanıcı>\<CUSA>\...      kayıtların kopyaları
```

Title ID'ler (CUSA...) oyunun bölgesine göre değişir. Bunlar yalnızca örnek.

Önemli:

- `ps4\` içindeki dosyaların adını ya da yerini elle değiştirme. Yeni oyunları her zaman `PS4Mount\` klasörüne koy.
- Güncelleme, oyunla **aynı Title ID'ye** sahip olmalı. Örneğin Avrupa sürümünün güncellemesi Amerika sürümüne uygulanmaz. PS4Mount böyle bir güncellemeyi hiçbir oyuna takmaz, bekletir.
- Birden fazla güncelleme varsa en yüksek sürüm kullanılır, eskisi `ps4\<CUSA>\replaced` klasörüne taşınır. Birleştirilmiş (base+update) pkg'ler de çalışır.
- Yalnızca **fake pkg**'ler kullanılabilir.
- 12.02'den yüksek yazılım sürümü isteyen bir pkg yalnızca backport ile çalışır. PS4Mount bunu panelde ve günlükte söyler.

## 3. PS4Mount 0.7.4-tr1'i derlemek

Bu ortamda PS4 SDK'sını derleme iznim olmadığı için hazır bir `.bin` veremiyorum. Projenin kendi GitHub derleme hattı bunu senin yerine yapar:

1. GitHub'da <https://github.com/Vld439/PS4Mount-PS4> sayfasını aç ve **Fork**'a bas.
2. PC'de [Git for Windows](https://git-scm.com/download/win) kurulu olmalı. `ps4mount-0.7.4-tr1.patch` dosyasını aynı klasöre koy ve şunları çalıştır:
   ```
   git config --global user.name "adın"
   git config --global user.email "e-postan"
   git clone https://github.com/<kullanıcı-adın>/PS4Mount-PS4
   cd PS4Mount-PS4
   git am ../ps4mount-0.7.4-tr1.patch
   git push
   ```
3. Fork'unda **Actions** sekmesine gir, iş akışlarını etkinleştir. "Build and release" çalışması bitince **Artifacts → ps4mount-ps4** zip'ini indir. İçinde `ps4mount-ps4.bin` var.
4. İstersen `git tag v0.7.4-tr1` ve `git push --tags` ile bir Release de oluşturabilirsin.

Bir sorun çıkarsa, karşılaştırmak için resmi **0.7.4-fix** sürümünü dene. Ondaki temel özellikler aynı, yalnızca kayıt kopyası ve Türkçe yok.

## 4. Konsolda ilk kurulum

1. Jailbreak'i ve GoldHEN'i her zamanki gibi yükle. GoldHEN ayarlarında **BinLoader**'ı (port 9090) ve **FTP**'yi aç.
2. `ps4mount-ps4.bin` dosyasını konsolda **`/data/payloads/`** klasörüne koy (FTP ile, port 2121, örneğin FileZilla).
3. GoldHEN'in **Payloader/Launchpad**'i (ya da Payload Guest) ile yükle. "PS4Mount 0.7.4-tr1 çalışıyor. Panel: http://192.168.x.x:8770" bildirimi gelir.
4. Aynı ağdaki telefondan ya da PC'den o adresi aç. İlk açılışta **Ayarlar**'a gir:
   - Pkg aranacak klasörler: `/PS4Mount`
   - **Pkg'leri ps4 klasöründe düzenle: AÇIK.** Kapalı olursa konsolun kendi pkg yükleyicisi bu dosyaları kurulabilir olarak gösterir; yanlışlıkla kurarsan iç disk dolar.
   - **Konsoldaki /data/PS4Mount içindeki pkg'leri de kullan: KAPALI** (iç diske pkg koymayacaksın).
   - **DLC'leri kur: AÇIK.** **Büyük DLC'leri de kur:** iç diskte yer azsa **KAPALI**.
   - **Konsolda boş bırakılacak alan (GB):** 10 GB boş yerin varsa 4 önerilir.
   - **Kayıtlar → Her oturumdan sonra kayıtları USB sürücüye kopyala: AÇIK**, saklanacak kopya: 10.
   - **Dil:** Türkçe (konsol Türkçe ise kendiliğinden seçilir).
   - **Kaydet**'e bas.
5. SSD'yi tak ve bildirimleri bekle: "usb0: 2 PS4 oyunu bulundu..." → "Ekleniyor 1/2..." → "... eklendi. Oynamak için konsolu bir kez yeniden başlat".
6. Konsolu **yeniden başlat**. Jailbreak'i, GoldHEN'i ve PS4Mount'u tekrar yükle. "N oyun oynamaya hazır" bildiriminden sonra oyunu ana ekrandan aç.

## 5. Günlük kullanım

- Her açılışta: jailbreak → GoldHEN → PS4Mount. "Oynamaya hazır" bildirimi gelmeden oyunu açma; çok erken açılan oyun "veriler bozuk" diyebilir.
- **Dinlenme moduna geçmeden önce PS4Mount oyununu kapat.** Dinlenme modu USB'nin gücünü keser.
- **SSD'yi çıkarmak:**
  1. Oyunu kapat.
  2. 20–30 saniye içinde gelen "Kayıtlar usb0 sürücüsüne kopyalandı" bildirimini bekle.
  3. En güvenlisi, konsolu tamamen kapatıp öyle çıkarmak.
  4. PC'de yeni pkg'leri `PS4Mount\` klasörüne koy ve diski **"Donanımı güvenle kaldır"** ile çıkar.
  5. SSD'yi tekrar takınca "usb0: ... bulundu" bildirimini bekle. Yeni bir oyun ya da sonradan eklenen bir güncelleme için konsolu bir kez yeniden başlatman gerekir.
- **Panel → oyun sayfası → Kayıtlar:** konsoldaki kayıtları, SSD'deki kopyaları, "Kayıtları şimdi kopyala" ve "Geri yükle" düğmelerini gösterir.

## 6. Kayıtlar nasıl korunuyor

- Konsol bir oyunun kayıtlarını `/user/home/<kullanıcı>/savedata/<CUSA>/` içinde tutar: her kayıt için şifreli bir görüntü (`sdimg_...`) ve bir anahtar (`....bin`). Genellikle birkaç MB'tır, iç diskte sorun olmaz.
- **Kopyalama:** oyun kapanınca, kayıtlar değiştiyse SSD'ye kopyalanır. Kopya önce `.part` klasörüne yazılır ve yalnızca her dosya eksiksiz gelince adı verilir. Kopyalama sırasında kayıt değişirse o kopya atılır ve daha sonra yeniden denenir. Oyun ve kullanıcı başına en yeni 10 kopya saklanır.
- **Geri yükleme:** oyun kapalıyken yapılır. Yalnızca konsolda zaten olan ve aynı boyuttaki kayıtların yerine konur. Önce konsoldaki mevcut kayıtların bir kopyası alınır ("geri yüklemeden önce" etiketiyle), yani işlem aynı yolla geri alınabilir. Sistemin kayıt veritabanına hiç yazılmaz.
- Konsolda kayıt hiç yoksa (silindiyse) önce oyunu açıp bir kez kaydet, sonra geri yükle.
- Bu kopyalar şifrelidir ve **bu konsola ve bu kullanıcıya** bağlıdır; başka bir konsolda çalışmaz. Taşınabilir ya da şifresiz bir yedek için Apollo Save Tool kullan.
- **İlk denemeden önce** kayıtlarını ayrıca yedekle: Ayarlar → Uygulama Kayıtlı Verileri Yönetimi → USB'ye kopyala, ya da Apollo Save Tool.

## 7. Riskler

- Jailbreak'li konsolla ve fake pkg'lerle **PSN'e bağlanma**: ban riski var. Konsolu 12.02'den yukarı güncelleme; güncelleme sunucularını engelle.
- Exploit bazen başarısız olur ya da konsol kilitlenir (kernel panic). Yeniden başlatıp tekrar dene.
- Dosya taşınırken ya da kayıtlar kopyalanırken SSD'yi çekme.
- PS4Mount beta bir yazılım. 0.7.4-tr1 ise henüz konsolda denenmedi.

## 8. Bu sürümde değişenler

- `src/saves.c`, `src/saves.h`: kayıt kopyası ve geri yükleme (yeni).
- `src/main.c`: ayarlar (kayıt kopyası, saklanacak kopya sayısı, boş bırakılacak alan), oyun kapanınca kopyalama, DLC'ler için alan koruması, panel API'si.
- `src/web.c`: `/api/saves`, `/api/saves/backup`, `/api/saves/restore` (yalnızca POST ve panel başlığıyla).
- `web/index.html`, `web/lang.js`: kayıtlar bölümü, yeni ayarlar, eksiksiz Türkçe tablo. `src/lang.c`: Türkçe (konsol dili 19).
- Testler:
  - `test/saves_test.c` (65 kontrol)
  - catalog_test'e "iki oyun karışmaz" testi
  - panel ve sunucu testleri
  - Tüm testler geçiyor ve kaynak PS4 hedefi için hatasız ve uyarısız derleniyor (yalnızca tür/sözdizimi kontrolü).
