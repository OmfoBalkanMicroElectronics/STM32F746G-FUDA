# GDB ve ST-LINK Debug Rehberi

Bu rehber CubeIDE, ST-LINK GDB Server ve `arm-none-eabi-gdb` ile kart üzerindeki sorunların sistematik biçimde teşhis edilmesini açıklar.

## 1. Önerilen debug yapılandırması

- Interface: ST-LINK / SWD
- Reset strategy: Connect under reset
- Başlangıç breakpoint’i: `main`
- ELF: `STM32CubeIDE/Debug/STM32F746G_DISCO.elf`
- GDB portu: varsayılan launch dosyasında 61234
- QSPI loader: `W25Q128JVEIQ_STM32F746G-DISCO.stldr`
- Verify download: açık
- Live Expressions: yalnız gerekli değişkenler için açık

Önerilen launch dosyası:

```text
STM32CubeIDE/STM32F746G_DISCO Debug.launch
```

## 2. CubeIDE üzerinden debug

1. Debug build alın.
2. Kartı USB ST-LINK üzerinden bağlayın.
3. `STM32F746G_DISCO Debug` yapılandırmasını başlatın.
4. `main()` üzerinde durduğunda gerekli breakpoint ve watch ifadelerini ekleyin.
5. Resume ile sistemi çalıştırın.
6. Donma anında **Suspend** ile bütün thread’leri durdurun.
7. Call Stack, Registers, Memory ve RTOS task durumlarını birlikte inceleyin.

Donma teşhisinde yalnız GUI thread’ine bakmayın. Media, Network ve USB Host task’larının stack/call durumlarını da seçin.

## 3. Temel breakpoint’ler

Genel:

```text
main
Error_Handler
HardFault_Handler
MemManage_Handler
BusFault_Handler
UsageFault_Handler
```

Medya:

```text
MediaPlayer_Task
MediaPlayer_Select
MediaPlayer_Seek
MediaRecorder_Start
MediaRecorder_Stop
```

Ağ:

```text
NetworkManager_Task
InternetRadio_Start
InternetRadio_Service
```

USB:

```text
USBStorage_Task
USBStorage_IRQHandler
USBPCAudio_Process
```

DMA/IRQ callback’lerine breakpoint koymak gerçek zamanlı sesi bozabilir. Bu noktalarda normal breakpoint yerine sayaç, conditional breakpoint veya watchpoint kullanın.

## 4. HardFault teşhisi

`HardFault_Handler` üzerinde durduğunuzda önce şu register’ları okuyun:

```text
SCB->HFSR
SCB->CFSR
SCB->MMFAR
SCB->BFAR
SCB->SHCSR
```

GDB konsolunda:

```gdb
p/x SCB->HFSR
p/x SCB->CFSR
p/x SCB->MMFAR
p/x SCB->BFAR
info registers
bt
```

`CFSR` üç alan içerir:

- Bits 0–7: MemManage fault
- Bits 8–15: BusFault
- Bits 16–31: UsageFault

Özellikle şunlara bakın:

- `PRECISERR`: geçersiz bellek erişimi; BFAR genellikle anlamlıdır.
- `IMPRECISERR`: gecikmeli bus fault; DMA/cache veya daha önceki yazma hatası olabilir.
- `UNALIGNED`: hizasız erişim.
- `DIVBYZERO`: sıfıra bölme.
- `IACCVIOL/DACCVIOL`: MPU izin ihlali.

Fault anında MSP/PSP ve exception frame üzerinden PC/LR bulunmalıdır. CubeIDE çoğu durumda call stack’i doğrudan gösterir; göstermiyorsa aktif stack pointer’dan R0–R3, R12, LR, PC ve xPSR frame’ini inceleyin.

## 5. FreeRTOS task teşhisi

Başlıca task’lar:

| Task | Rol | Başlangıç önceliği |
|---|---|---|
| `defaultTask` | MediaPlayer task | Normal |
| `TouchGFXTask` | UI render/tick | Normal |
| `videoTask` | TouchGFX yardımcı video işi | Low |
| `networkTask` | Ethernet, DHCP, ping, radio | BelowNormal |
| `usbHostTask` | USB MSC Host state machine | Normal |

Donmada kontrol edin:

- Hangi task çalışıyor?
- Bir mutex/semaphore üzerinde sonsuz bekleme var mı?
- Media task dosya veya decoder içinde bloklanmış mı?
- Network callback’i UI veya codec işlemi yapıyor mu?
- Stack pointer task stack sınırına yaklaşmış mı?

CubeIDE RTOS görünümü doğru algılamazsa thread handle’larını ve FreeRTOS listelerini doğrudan watch edin. Launch dosyasındaki eski ThreadX RTOS proxy etiketi bu proje için güvenilir değildir; gerçek RTOS FreeRTOS/CMSIS-RTOS v2’dir.

## 6. Önerilen Live Expressions

USB MSC:

```text
usbDiagState
usbDiagRevision
usbDiagHostState
usbDiagLastUserEvent
usbDiagReady
usbDiagIrqCount
```

USB PC Audio kodu etkinleştirildiğinde:

```text
usbPcDiagConnected
usbPcDiagConfigured
usbPcDiagRevision
usbPcDiagIrqCount
```

Ethernet/ICMP:

```text
ethDiagRxPbuf
ethDiagRxIpv4
ethDiagRxIcmp
ethDiagRxEchoRequest
ethDiagTxPbuf
ethDiagTxIcmp
ethDiagRawIcmp
ethDiagRawEchoReply
ethDiagIcmpInput
ethDiagIcmpEcho
ethDiagIcmpChecksumError
```

Bu sayaçları çok sık yenilemek SWD trafiği yüzünden UI ve ses performansını düşürebilir. Refresh aralığını yükseltin veya sorun anında manuel yenileyin.

## 7. Ses kesilmesi/tıklama debug akışı

1. Sorunun WAV, MP3, radyo, AUX veya kayıt yoluna özel olup olmadığını ayırın.
2. Aynı dosyayı hız/pitch ve yazılımsal EQ kapalıyken deneyin.
3. FFT’yi geçici olarak devre dışı bırakıp karşılaştırın.
4. DMA half/full callback’lerinin düzenli geldiğini kontrol edin.
5. Decode/refill süresinin bir DMA yarım-buffer süresini aşıp aşmadığını ölçün.
6. SD/USB okuma gecikmesini ayrı sayaçla ölçün.
7. DMA tamponlarının 32-byte hizalı ve cache clean/invalidate sınırlarının 32-byte yuvarlanmış olduğunu kontrol edin.
8. ISR içinde codec init, FatFs veya uzun memcpy yapılmadığını doğrulayın.

Breakpoint ses zamanlamasını bozar. Ses hatalarında GPIO pulse, DWT cycle counter veya RAM sayaçları daha güvenilir olabilir.

## 8. UI donması ve düşük FPS

- UI donduğunda önce Suspend yapıp TouchGFXTask call stack’ine bakın.
- TouchGFXTask bir mutex bekliyorsa lock sahibi task’ı bulun.
- `handleTickEvent()` içinde FatFs/ağ/codec işi olmadığını doğrulayın.
- Çok sayıda widget invalidate edilip edilmediğini kontrol edin.
- Radyo AAC+ veya hız/pitch sırasında 512 FFT seçildiğini doğrulayın.
- Debug Live Expressions ve açık Memory view’ların SWD yükünü azaltın.

Kart çalışıyor fakat yalnız dokunmatik yanıt vermiyorsa I2C3 paylaşımına bakın. FT5336 ve WM8994 aynı I2C guard mekanizmasını kullanır; lock dengesizliği hem dokunmatik hem codec tarafını etkileyebilir.

## 9. Ethernet debug akışı

1. `linkUp`, `hasAddress`, `linkMbps` ve IP bilgisini NetworkSnapshot üzerinden kontrol edin.
2. RX/TX descriptor ve buffer bölgelerinin MPU’da non-cacheable olduğunu doğrulayın.
3. `ethDiagRxPbuf` artıyor ancak `ethDiagRxIpv4` artmıyorsa frame parse/VLAN sorununu inceleyin.
4. IPv4 artıyor ama ping reply yoksa ARP, ICMP checksum ve gateway’i ayırın.
5. Radyo bağlanıyor ama ses başlamıyorsa `bufferedBytes`, `receivedBytes`, codec türü ve reconnect sayısını izleyin.
6. Pal AAC+ akışlarında UI yavaşsa decode süresi, radio buffer seviyesi ve TouchGFX tick süresini birlikte ölçün.

## 10. USB MSC debug akışı

- `usbDiagIrqCount` artmıyorsa OTG HS IRQ, NVIC veya pin/clock yapılandırmasını kontrol edin.
- `usbDiagLastUserEvent` connect gösterip `usbDiagReady` 0 kalıyorsa enumeration/MSC ready aşamasına bakın.
- Host state ilerliyor fakat mount yoksa FatFs driver link ve volume path bilgisini kontrol edin.
- USB bellek karttan güç alıyorsa VBUS ve akım bütçesini kontrol edin.

## 11. Ham GDB Server ile debug

CubeIDE dışında elle debug gerektiğinde iki terminal kullanılır. Kurulum dizinleri sürüme göre değişir.

Terminal 1:

```powershell
& 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.win32_2.2.500.202604010938\tools\bin\ST-LINK_gdbserver.exe' -p 61234 -cp '<CubeProgrammer bin dizini>'
```

Terminal 2:

```powershell
& 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin\arm-none-eabi-gdb.exe' .\STM32CubeIDE\Debug\STM32F746G_DISCO.elf
```

GDB içinde:

```gdb
target extended-remote localhost:61234
monitor reset halt
load
break main
continue
```

QSPI içeriğini ham `load` ile programlamak external loader ayarı gerektirebilir. İlk kullanımda CubeIDE launch yapılandırması daha güvenlidir.

## 12. Debug oturumunu kapatma

GDB Server’ı zorla öldürmeden önce CubeIDE Debug görünümünden **Terminate** kullanın. Eski ST-LINK GDB Server süreci açık kalırsa sonraki bağlantı `ST-LINK already used` veya port çakışması verebilir.
