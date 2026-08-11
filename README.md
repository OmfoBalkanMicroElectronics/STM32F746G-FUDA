# STM32F746G-FUDA MultiPlayer

STM32F746G-FUDA MultiPlayer, **STM32F746G-DISCO** üzerinde çalışan FreeRTOS ve TouchGFX tabanlı bir gömülü multimedya oynatıcısıdır. Proje; yerel WAV/MP3 oynatma, SD ve USB bellek dosya yönetimi, WM8994 ses yönlendirme, gerçek zamanlı DSP, MEMS mikrofon kaydı ve Ethernet internet radyosunu aynı firmware içinde birleştirir.

> Proje gerçek STM32F746G-DISCO donanımında geliştirilmiş ve ana işlevleri kart üzerinde denenmiştir. Bu depo deneysel/geliştirme firmware’idir; ticari ürün kalitesinde güvenlik veya kesintisiz çalışma garantisi vermez.

## Donanım ve yazılım tabanı

- STM32F746NG, Cortex-M7, 216 MHz, tek duyarlıklı FPU ve L1 cache
- STM32F746G-DISCO üzerindeki 480×272 TFT, kapasitif dokunmatik, SDRAM ve QSPI Flash
- WM8994 stereo ses codec’i ve kulaklık çıkışı
- Kart üzerindeki çift ST MEMS mikrofon
- microSD/SDMMC, USB OTG HS Host ve USB OTG FS Device altyapıları
- LAN8742A 10/100 Ethernet PHY
- FreeRTOS/CMSIS-RTOS v2
- TouchGFX 4.26.1 ve Chrom-ART/DMA2D
- STM32CubeF7 1.17.4 HAL/BSP bileşenleri
- CMSIS-DSP, FatFs, LwIP, minimp3 ve Helix AAC

## Özellikler

### Yerel oynatma

- SD kart ve USB Mass Storage üzerinden dosya tarama
- PCM WAV: mono/stereo, 8/16/24/32-bit örnekler
- MP3: minimp3 ile gerçek zamanlı decode
- Parça adı, geçen süre, toplam süre ve seek
- Oynat/duraklat, önceki/sonraki ve ses seviyesi
- Kaydırılabilir dosya listesi, klasör seçimi, kök dizin ve uzun adlar için marquee
- Onay ekranlı dosya silme
- En fazla 128 dosya, 64 oynatılabilir parça ve 64 klasörlük çalışma listeleri

### DSP ve ses kontrolü

- Yerel PCM yolunda CMSIS-DSP tabanlı 10 bant yazılımsal EQ
- 31 Hz–16 kHz bantları, preamp ve Flat/Klasik/Jazz/Rock/Pop/Vocal presetleri
- WM8994 üzerindeki 5 bant donanımsal EQ
- Yerel oynatmaya özel hız ve ton/pitch işleme
- WM8994 ses seviyesi ve kaynak yönlendirme
- AUX giriş modu

### Spektrum analizörü

- 24 bant gerçek zamanlı FFT
- Hann pencereleme
- Yerel oynatmada normalde 1024 nokta
- İnternet radyosu veya hız/pitch işleminde yükü azaltmak için 512 nokta
- TouchGFX tarafında RGB renk geçişi, yumuşatma ve peak fall/decay

### Ses kaydı

- Kart üzerindeki iki MEMS mikrofon ile 16 kHz, 16-bit stereo WAV kayıt
- Sol/sağ seviye göstergeleri
- -30 dB ile +30 dB arasında kayıt kazancı
- Durdurulduktan sonra kaydet/reddet onayı
- Seçili SD veya USB ortamındaki `/Records` klasörüne kayıt
- Kaydedilen dosyaları oynatma ve silme

### Ethernet ve internet radyosu

- DHCP ile IPv4 adresi alma
- Link, IP, 10/100 Mbps ve duplex tanılama bilgileri
- 8.8.8.8 ping ve gecikme ölçümü
- İndirme/yükleme hız testi
- Tamponlu HTTP radyo akışı
- MP3 ve AAC+ radyo decode
- Groove Salad, Radyo 45'lik, Pal Nostalji ve Pal Station Pop
- Radyo modunda oynat/duraklat ve önceki/sonraki istasyon

### USB

- USB OTG HS üzerinden USB Mass Storage Host
- FAT/FatFs üzerinden klasör ve medya erişimi
- USB OTG FS için UAC1 ses aygıtı + HID medya tuşu kaynak kodu

> **Güncel durum:** USB PC UAC1/HID başlangıç çağrısı `Core/Src/main.c` içinde Ethernet kararlılık çalışmaları sırasında bilinçli olarak kapatılmıştır. Kod depodadır fakat varsayılan firmware’de etkin değildir. USB MSC Host aktiftir.

## Hızlı başlangıç

1. STM32CubeIDE, TouchGFX Designer ve STM32CubeProgrammer kurun.
2. Depoyu klonlayın.
3. `TouchGFX/CleanMP3Player.touchgfx` dosyasını TouchGFX Designer 4.26.1 ile açıp **Generate Code** çalıştırın.
4. CubeIDE’de `STM32CubeIDE` klasörünü mevcut proje olarak içe aktarın.
5. `Debug` yapılandırmasını seçip projeyi derleyin.
6. Kartı **USB ST-LINK** konektöründen bağlayın.
7. QSPI external loader etkin olan `STM32F746G_DISCO Debug` launch yapılandırmasıyla çalıştırın veya debug edin.

Ayrıntılı kurulum, komut satırı derleme ve flash adımları için [Kurulum, Derleme ve Flash Rehberi](docs/KURULUM_DERLEME_FLASH.md) belgesine bakın.

## Dokümantasyon

- [Kurulum, derleme ve karta yükleme](docs/KURULUM_DERLEME_FLASH.md)
- [GDB/ST-LINK debug rehberi](docs/DEBUG_REHBERI.md)
- [Mimari ve yeni kod ekleme rehberi](docs/MIMARI_VE_GELISTIRME.md)
- [Üçüncü taraf bileşen ve lisans notları](THIRD_PARTY_NOTICES.md)

## Dizin yapısı

```text
Core/                 Başlangıç, kesmeler, MPU/cache ve çevrebirim kurulumu
Media/                Oynatma, decoder, DSP, FFT, kayıt ve dosya yönetimi
Network/              LwIP, Ethernet, ping, hız testi ve internet radyosu
USBHost/              USB MSC Host entegrasyonu
USBDevice/            Deneysel UAC1 + HID Device entegrasyonu
FatFs/                SD/USB FatFs bağlantı katmanı
Drivers/              STM32 HAL, CMSIS ve kart/BSP sürücüleri
Middlewares/          FreeRTOS, FatFs, CMSIS-DSP ve codec kaynakları
TouchGFX/             UI tasarımı, Presenter/View/Model ve varlıklar
STM32CubeIDE/         CubeIDE proje ve launch yapılandırmaları
STM32F746G_DISCO.ioc  CubeMX donanım yapılandırması
```

## Kod geliştirirken önemli kurallar

- TouchGFX `generated/` altındaki dosyaları elle değiştirmeyin; Designer yeniden üretir.
- Kalıcı UI kodunu `TouchGFX/gui/include` ve `TouchGFX/gui/src` altında tutun.
- UI thread’inden FatFs, decoder, codec veya ağ üzerinde bloklayan işlem çalıştırmayın. UI yalnızca Model/Presenter üzerinden komut göndersin.
- DMA tamponlarını 32-byte hizalayın ve Cortex-M7 D-Cache clean/invalidate kurallarını koruyun.
- STM32CubeMX yeniden üretimi, elle eklenen Media/Network/USB bağlantılarını silebilir. Önce branch/commit alın ve üretim sonrası diff’i satır satır inceleyin.
- QSPI varlıkları değiştiğinde yalnızca MCU Flash’ını değil QSPI içeriğini de yeniden programlayın.

## Bilinen sınırlamalar

- FLAC desteği yoktur.
- Yerel AAC dosya oynatma kullanıcı arayüzüne bağlı değildir; AAC decoder internet radyosu içindir.
- Radyo akışları TLS kullanmayan sabit HTTP adresleridir ve yayıncı tarafında değişebilir.
- Hız/pitch işleme yalnızca yerel dosya kaynağında etkindir.
- USB PC UAC1/HID varsayılan derlemede devre dışıdır.
- TouchGFX tarafından üretilen kaynaklar ve framework kütüphanesi depoya alınmaz; ilk derlemeden önce Generate Code gerekir.

## Lisans durumu

Depoya şu aşamada tek bir üst seviye açık kaynak lisansı uygulanmamıştır; özgün proje kodu aksi belirtilmedikçe **tüm hakları saklı** durumdadır. Bunun nedeni, projede farklı şartlara sahip ST/TouchGFX bileşenlerinin ve RPSL/RCSL ile GPLv3 bildirimleri içeren Helix AAC kodunun birlikte bulunmasıdır.

Depo herkese açık yapılmadan veya binary dağıtılmadan önce lisans uyumluluğu ayrıca tamamlanmalıdır. Ayrıntılar [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) dosyasındadır.

## Proje durumu

Yerel WAV/MP3 oynatma, SD/USB MSC, seek, ses kontrolü, AUX, donanımsal/yazılımsal EQ, hız/pitch, spektrum analizörü, MEMS kayıt, dosya yönetimi, Ethernet tanılama ve internet radyosu gerçek kart üzerinde çalıştırılmıştır. Geliştirme odağı; uzun süreli kararlılık, ağ akışı optimizasyonu, codec/lisans temizliği ve USB Device yolunun tekrar etkinleştirilmesidir.

### Sorumlu geliştirici

**Barbaror4** — OmfoBalkanMicroElectronics
