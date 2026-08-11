# Third-Party Components and License Notes

Bu depo farklı kaynaklardan gelen firmware, middleware, decoder ve görsel bileşenleri içerir. Her üçüncü taraf dosya kendi copyright ve lisans bildirimi altında kalır. Bu belge hukuki danışmanlık değildir; public/binary dağıtım öncesi ayrıca lisans incelemesi yapılmalıdır.

## Üst seviye proje lisansı

Bu snapshot için üst seviyede MIT/BSD/GPL gibi tek bir lisans seçilmemiştir. Özgün proje kodu aksi açıkça belirtilmedikçe tüm hakları saklıdır. Deponun private tutulması önerilir.

Tek lisans eklenmemesinin temel nedeni, aşağıdaki bileşenlerin farklı ve bazı durumlarda birbiriyle uyumluluğu ayrıca değerlendirilmesi gereken şartlar taşımasıdır.

## Bileşenler

### STMicroelectronics HAL, CMSIS Device ve BSP

Konumlar:

```text
Drivers/STM32F7xx_HAL_Driver/
Drivers/CMSIS/
Drivers/BSP/
```

Bu dosyalardaki ST copyright ve redistribution başlıkları korunmalıdır. Depodaki lisans dosyaları:

```text
Drivers/STM32F7xx_HAL_Driver/LICENSE.txt
Drivers/CMSIS/LICENSE.txt
Drivers/CMSIS/Device/ST/STM32F7xx/LICENSE.txt
```

### TouchGFX

Konumlar:

```text
TouchGFX/
Middlewares/ST/touchgfx/   (depoya alınmaz, araç tarafından sağlanır/üretilir)
```

TouchGFX framework ve üretim araçları ST’nin kendi lisans şartlarına tabidir. Framework kütüphanesi bu depoya kopyalanmamıştır. Kullanıcı TouchGFX Designer ile Generate Code çalıştırmalıdır.

### FreeRTOS

Konum:

```text
Middlewares/Third_Party/FreeRTOS/
```

Her kaynak dosyasındaki mevcut lisans başlıkları korunmalıdır.

### CMSIS-DSP

Konum:

```text
Middlewares/Third_Party/CMSIS_DSP/
```

ARM CMSIS-DSP bildirim ve lisans şartları geçerlidir. Kaynak ve prebuilt library dosyalarının kendi bildirimleri korunmalıdır.

### FatFs

Konumlar:

```text
Middlewares/Third_Party/FatFs/
FatFs/
```

FatFs dosyalarındaki ChaN copyright/lisans bildirimi korunmalıdır.

### LwIP

Konum:

```text
Network/LwIP/
Network/Src/
```

LwIP BSD-benzeri lisans bildirimleri kaynak dosyalarında yer alır ve korunmalıdır.

### minimp3

Konum:

```text
Middlewares/Third_Party/minimp3/
```

Depodaki `LICENSE` dosyası CC0 1.0 Universal metnini içerir:

```text
Middlewares/Third_Party/minimp3/LICENSE
```

### Helix AAC ve wrapper

Konum:

```text
Middlewares/Third_Party/helix-aac/
```

Helix AAC kaynak başlıkları RealNetworks RPSL/RCSL seçeneklerine atıf yapar. Ayrıca depoda wrapper için GNU GPLv3 metni bulunur:

```text
Middlewares/Third_Party/helix-aac/WRAPPER_GPL3_LICENSE.txt
```

Bu, public kaynak veya binary dağıtımı için en önemli açık lisans konusudur. Projenin tümüne basitçe MIT lisansı eklemek bu şartları ortadan kaldırmaz ve yanıltıcı olabilir.

Public sürüm öncesi önerilen seçeneklerden biri seçilmelidir:

1. Helix AAC/wrapper bileşenini kaldırmak ve yalnız uyumlu/permissive bileşenlerle yayınlamak.
2. Uyumlu lisanslı başka bir AAC decoder kullanmak.
3. Helix/RPSL/RCSL ve wrapper GPLv3 şartlarını uzman incelemesiyle tam olarak karşılayacak dağıtım modeli hazırlamak.
4. AAC bölümünü ayrı, kullanıcı tarafından sonradan eklenen opsiyonel bileşen yapmak.

### Material Icons ve diğer UI varlıkları

Material Icons bildirimi:

```text
TouchGFX/MATERİAL-ICONS-LICENSE
```

TouchGFX theme varlıkları ve projeye eklenen görseller için kaynak/izin bilgileri public dağıtımdan önce ayrıca doğrulanmalıdır. Kişisel veya üçüncü taraf görsel/müzik dosyaları açık lisans kapsamına otomatik olarak girmez.

## Public yayın öncesi kontrol listesi

- Helix AAC dağıtım modeli kararlaştırıldı mı?
- TouchGFX framework dosyaları depodan hariç tutuldu mu?
- ST/CMSIS/BSP lisans dosyaları ve kaynak başlıkları korundu mu?
- Material Icons lisans bildirimi korundu mu?
- UI görsellerinin dağıtım hakkı doğrulandı mı?
- Test müziği, kayıtlar, token, parola ve özel URL bulunmadığı kontrol edildi mi?
- Seçilen üst seviye lisans yalnız organizasyonun yazdığı dosyalara nasıl uygulanacağı açıklandı mı?

Bu kontrol tamamlanana kadar deponun private ve üst seviye lisanssız tutulması en güvenli seçenektir.
