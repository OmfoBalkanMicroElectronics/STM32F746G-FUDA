# Mimari ve Yeni Kod Ekleme Rehberi

Bu belge projenin ana katmanlarını, veri akışını ve güvenli genişletme yöntemini açıklar.

## 1. Yüksek seviye mimari

```text
TouchGFX View
    ↓ kullanıcı olayı
Presenter → Model
    ↓ thread-safe komut/snapshot API
MediaPlayer / NetworkManager / USBStorage task'ları
    ↓
FatFs + decoder + DSP + SAI DMA + WM8994
    ↓
Kulaklık / AUX / MEMS / Ethernet / SD / USB
```

UI tarafı donanımı doğrudan sürmez. Kullanıcı olayları Presenter/Model üzerinden C API’lerine aktarılır; durum değişiklikleri snapshot/revision alanlarıyla UI’ya geri taşınır.

## 2. Başlangıç ve task oluşturma

Ana başlangıç noktası `Core/Src/main.c` dosyasındadır:

1. MPU ayarlanır.
2. I-Cache ve D-Cache açılır.
3. HAL ve 216 MHz clock ağacı kurulur.
4. GPIO, CRC, DMA2D, FMC/SDRAM, I2C3, LTDC ve QSPI hazırlanır.
5. TouchGFX pre-OS init çalışır.
6. FreeRTOS kernel initialize edilir.
7. Media, USB Host ve Network modülleri initialize edilir.
8. Media, TouchGFX, video, network ve USB Host task’ları oluşturulur.
9. Scheduler başlatılır.

`defaultTask`, doğrudan `MediaPlayer_Task()` fonksiyonuna girer. UI ve media aynı öncelik sınıfında çalıştığı için uzun bloklayan işler ciddi UX kaybına yol açabilir.

## 3. Media katmanı

Ana dosyalar:

```text
Media/Inc/media_player.h
Media/Src/media_player.c
```

Bu katmanın sorumlulukları:

- SD ve USB FatFs volume yönetimi
- klasör/dosya tarama ve seçme
- WAV parse ve PCM dönüşümü
- MP3 decode
- internet radyosu MP3/AAC decode
- seek, pause, next/previous
- WM8994/SAI çıkış kontrolü
- AUX ve kaynak değiştirme
- 5 bant donanımsal ve 10 bant yazılımsal EQ
- hız/pitch işleme
- FFT spektrum üretimi
- MEMS mikrofon kayıt kuyruğu
- dosya silme

UI veya başka task’lar media state değişkenlerini doğrudan yazmamalıdır. `MediaPlayer_*` API’lerini kullanın.

### Yeni media komutu ekleme

1. `media_player.c` içindeki komut enum’una yeni bir `CMD_*` değeri ekleyin.
2. Public API fonksiyonunu `media_player.h` içine ekleyin.
3. API içinde parametreyi doğrulayarak media komut kuyruğuna gönderin.
4. Komutu yalnız `MediaPlayer_Task` context’inde işleyin.
5. `MediaSnapshot` değişiyorsa revision güncellemesini atomik/guard’lı biçimde yapın.
6. Model ve Presenter’a yalnız gerekli yüzeyi ekleyin.

Bu model UI thread’inde FatFs veya codec çağrısı yapılmasını engeller.

## 4. Yerel ses veri akışı

```text
FatFs read
  → WAV PCM dönüştürme veya minimp3 decode
  → hız/pitch (etkinse)
  → 10 bant yazılımsal EQ (yerel kaynak)
  → FFT örnek toplama
  → stereo 16-bit PCM DMA buffer
  → SAI TX DMA
  → WM8994
  → kulaklık
```

WAV kaynak 8/16/24/32-bit olabilir; çıktı WM8994’e gitmeden önce 16-bit stereo PCM’e dönüştürülür. Mono giriş iki kanala genişletilir.

## 5. Radyo veri akışı

```text
LAN8742A
  → Ethernet DMA
  → LwIP TCP
  → HTTP header/stream parser
  → radio ring buffer
  → MP3 veya AAC+ decoder
  → FFT (512 nokta)
  → SAI/WM8994
```

Radyo station tablosu `Network/Src/internet_radio.c` içindedir. Yeni istasyon eklerken:

1. Doğrudan, TLS gerektirmeyen gerçek stream URL’sini bulun.
2. Host, path, port ve codec türünü station tablosuna ekleyin.
3. HTTP redirect, chunked transfer ve metadata davranışını test edin.
4. En az 15 dakika kesintisiz oynatma, reconnect ve kaynak değiştirip geri dönme testi yapın.
5. AAC+ istasyonlarında UI FPS ve audio buffer seviyesini ölçün.

Web sayfası URL’si ile gerçek radyo stream URL’si aynı değildir.

## 6. DSP katmanı

### Yazılımsal EQ

10 bant yazılımsal EQ, CMSIS-DSP biquad filtre zinciri kullanır ve yalnız yerel PCM yolunda uygulanır. Bant, preamp veya preset değişikliklerinde katsayılar media task context’inde yeniden hesaplanmalıdır.

### Donanımsal EQ

WM8994 donanımsal EQ beş banttır. Codec register erişimi FT5336 dokunmatik ile paylaşılan I2C3 guard’ını kullanmalıdır.

### FFT

- Normal yerel oynatma: 1024 nokta
- Radyo veya hız/pitch etkin: 512 nokta
- Çıkış: 24 band seviye dizisi
- UI: hedef seviyelere ayrı interpolasyon ve peak decay uygular

FFT veya EQ tamponlarını büyütürken internal SRAM, D-Cache line alignment ve task deadline birlikte hesaplanmalıdır.

## 7. Kayıt katmanı

Kayıt akışı:

```text
Dual MEMS → SAI RX DMA → aligned recorder buffer
          → bounded write queue → FatFs WAV file
          → stop → WAV header finalize → save/discard
```

Kayıt formatı 16 kHz, 16-bit stereo PCM’dir. DMA callback yalnız buffer parçasını kuyruğa almalıdır; FatFs yazması ISR içinde yapılmamalıdır.

Yeni kayıt formatı eklerken header, byte rate, block align, süre hesabı ve playback parser birlikte güncellenmelidir.

## 8. USB mimarisi

### USB MSC Host

`USBHost/` modülü OTG HS Host, MSC class ve FatFs driver bağlantısını yönetir. State machine ayrı task’ta çalışır. UI yalnız `USBStorage_*` snapshot/state fonksiyonlarını kullanır.

### USB PC Audio Device

`USBDevice/` altında UAC1 OUT + Custom HID consumer control kodu vardır. Codec ve SAI işlemleri USB ISR’den çıkarılıp `USBPCAudio_Process()` üzerinden normal task context’ine ertelenmiştir.

Varsayılan firmware’de `USBPCAudio_Init()` yorum satırındadır. Yeniden etkinleştirirken Ethernet/USB interrupt öncelikleri, pin sahipliği, codec source geçişi ve uzun süreli stream kararlılığı yeniden test edilmelidir.

## 9. TouchGFX katmanı

Elle düzenlenen alanlar:

```text
TouchGFX/gui/include/gui/
TouchGFX/gui/src/
TouchGFX/assets/
TouchGFX/CleanMP3Player.touchgfx
```

Üretilen ve elle değiştirilmemesi gereken alanlar:

```text
TouchGFX/generated/
TouchGFX/config/
```

### Yeni ekran ekleme

1. TouchGFX Designer’da ekranı ve widget’ları oluşturun.
2. Anlamlı ve sabit widget adları verin.
3. Generate Code çalıştırın.
4. `gui/include/gui/<screen>_screen` ve `gui/src/<screen>_screen` altındaki View/Presenter dosyalarına davranışı ekleyin.
5. Donanım erişimini View içine koymayın; Presenter → Model → backend API yolunu kullanın.
6. Her tick tüm ekranı invalidate etmek yerine değişen widget alanlarını yenileyin.

### Yeni backend verisini UI’ya taşıma

1. Backend snapshot yapısına alan ekleyin.
2. Snapshot revision mantığını güncelleyin.
3. `Model` içinde veriyi periyodik okuyun.
4. `ModelListener` callback’i ekleyin.
5. İlgili Presenter callback’i View’a taşısın.
6. View yalnız değer değiştiğinde text/widget invalidate etsin.

## 10. Yeni C/C++ kaynak dosyası ekleme

1. Dosyayı doğru modülün `Inc` veya `Src` klasörüne ekleyin.
2. CubeIDE Project Explorer’da ilgili virtual folder altında **New → Source File** kullanın veya linked resource ekleyin.
3. Project Properties → C/C++ Build → Settings altında include yolunu ekleyin.
4. Kaynağın Debug build’de `subdir.mk`/objects listesine girdiğini doğrulayın.
5. C API C++ tarafından kullanılacaksa header’da `extern "C"` guard kullanın.
6. Clean build alın.

CubeIDE’nin `Debug/subdir.mk` dosyaları otomatik üretilir ve commitlenmez.

## 11. CubeMX ile çevrebirim ekleme

`STM32F746G_DISCO.ioc` dosyasını değiştirmek yüksek riskli bir işlemdir; projede Media, Network ve USB entegrasyonlarının bir kısmı sonradan elle eklenmiştir.

Önerilen akış:

1. Temiz branch ve commit oluşturun.
2. `.ioc` yedeği alın.
3. CubeMX’te yalnız gerekli çevrebirimi değiştirin.
4. Keep User Code seçeneğini açık tutun.
5. Generate Code sonrası `main.c`, MSP, IRQ, HAL config, `.project`, `.cproject` ve linker script diff’ini inceleyin.
6. Kaybolan Media/Network/USB init ve IRQ bağlantılarını geri kurun.
7. Clean build, flash ve gerçek kart regresyon testi yapın.

## 12. Cortex-M7 cache ve DMA kuralları

- DMA tampon başlangıcı en az 32-byte hizalı olmalıdır.
- Cache clean/invalidate adresi ve uzunluğu cache line sınırlarına yuvarlanmalıdır.
- CPU’nun DMA’ya vereceği veri önce clean edilmelidir.
- DMA’nın yazdığı veri CPU tarafından okunmadan önce invalidate edilmelidir.
- Ethernet descriptor/RX buffer bölgesi MPU ile non-cacheable tutulur.
- SDRAM veya QSPI MPU ayarını değiştirmek TouchGFX ve DMA davranışını etkileyebilir.

Cache işlemlerini kaldırarak sorunu “düzeltmek” veri tutarlılığı hatasını gizleyebilir.

## 13. Kod inceleme kontrol listesi

- ISR içinde bloklayan işlem var mı?
- UI thread’inde dosya/ağ/codec çağrısı var mı?
- Yeni buffer hizalı mı ve taşma sınırları kontrol ediliyor mu?
- Snapshot/string yazımları kapasiteyi koruyor mu?
- FatFs dönüş kodları ve kısa write/read sonuçları kontrol ediliyor mu?
- Kaynak değişiminde eski DMA/codec/state tamamen durduruluyor mu?
- QSPI/UI asset değişikliği flash akışına dahil mi?
- SD, USB, radyo, AUX, kayıt ve kaynaklar arası geçiş regresyonu yapıldı mı?

## 14. Önerilen regresyon testi

1. SD’den kısa/uzun isimli WAV ve MP3 listeleme.
2. Scroll hareketinin item click üretmediğini doğrulama.
3. Her dosyada play/pause, seek, next/previous.
4. EQ presetleri ve tüm yazılımsal bantlar.
5. Hız/pitch açık ve kapalı uzun süreli oynatma.
6. USB MSC takma, çıkarma ve SD’ye geri dönme.
7. AUX’a geçiş ve yüksek ses kontrolü.
8. Tüm radyo istasyonlarında buffer/reconnect/UI FPS.
9. Kısa ve uzun kayıt, save/discard ve geri oynatma.
10. Ethernet kablosunu çalışma sırasında çıkarıp yeniden takma.
11. Menü geçişleri sırasında ses kesintisi ve TouchGFX akıcılığı.
