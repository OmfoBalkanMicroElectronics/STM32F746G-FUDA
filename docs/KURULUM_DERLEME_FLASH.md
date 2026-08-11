# Kurulum, Derleme ve Karta Yükleme

Bu belge STM32F746G-FUDA MultiPlayer kaynaklarını temiz bir Windows bilgisayarında hazırlamak, derlemek ve STM32F746G-DISCO kartına yüklemek için izlenecek akışı açıklar.

## 1. Gerekli donanım

- STM32F746G-DISCO
- Veri aktarabilen Micro-USB kablosu
- USB ST-LINK konektörü üzerinden PC bağlantısı
- FAT32 biçimlendirilmiş SD kart veya USB bellek
- 3,5 mm kulaklık
- Ethernet özellikleri için DHCP sunuculu bir ağ ve Ethernet kablosu

USB bellek Host modunda kullanılacaksa kartın güç bütçesine dikkat edin. Kararsız veya yüksek akım isteyen belleklerde harici beslemeli USB hub gerekebilir.

## 2. Gerekli yazılımlar

Bu snapshot aşağıdaki sürümlerle hazırlanmıştır:

- STM32CubeIDE 2.2.0
- GNU Tools for STM32 14.3.rel1
- TouchGFX Designer 4.26.1
- STM32Cube FW F7 1.17.4
- STM32CubeProgrammer ve ST-LINK GDB Server (CubeIDE ile gelen sürüm)

Daha yeni sürümler çalışabilir; ancak otomatik migration veya CubeMX code generation büyük bir diff üretebilir. İlk açılışta yedek/branch oluşturmadan migration onaylamayın.

## 3. Depoyu klonlama

```powershell
git clone https://github.com/OmfoBalkanMicroElectronics/STM32F746G-FUDA.git
cd STM32F746G-FUDA
```

Depo özel olduğu sürece GitHub hesabınızın organizasyon erişimi gerekir.

## 4. TouchGFX kaynaklarını üretme

Framework ve `TouchGFX/generated` çıktıları depoya alınmaz. İlk derlemeden önce:

1. `TouchGFX/CleanMP3Player.touchgfx` dosyasını TouchGFX Designer ile açın.
2. Projenin TouchGFX 4.26.1 ile açıldığını kontrol edin.
3. Sağ üstten **Generate Code** seçin.
4. Üretimin hatasız bittiğini doğrulayın.

Bu işlem şunları oluşturur veya günceller:

- `TouchGFX/generated/`
- `TouchGFX/config/`
- gerekli TouchGFX framework/kütüphane dosyaları
- CubeIDE linked-resource girdileri

### UI değişikliği yaptıktan sonra

1. Designer’da ekran/widget değişikliğini yapın.
2. Widget adlarını C++ View kodunun kullandığı adlarla uyumlu tutun.
3. Generate Code çalıştırın.
4. `TouchGFX/gui/src` ve `TouchGFX/gui/include` altındaki özel kodun hâlâ derlendiğini doğrulayın.
5. Git diff’te yalnızca beklenen `.touchgfx`, asset, text ve özel View/Presenter dosyalarını commit edin.

## 5. STM32CubeIDE’ye aktarma

1. CubeIDE’yi açın.
2. **File → Import → General → Existing Projects into Workspace** yolunu izleyin.
3. Kök dizin olarak deponun `STM32CubeIDE` klasörünü seçin.
4. `STM32F746G_DISCO` projesini içe aktarın.
5. Project Explorer’da bağlı kaynakların kırmızı çarpı göstermediğini kontrol edin.
6. Yapılandırmayı **Debug** seçin.

Proje `PARENT-1-PROJECT_LOC` kullanan linked resource yapısındadır. Bu nedenle `STM32CubeIDE` klasörünü proje kökünden ayırmayın ve yalnız başına taşımayın.

## 6. CubeIDE ile derleme

1. **Project → Clean** çalıştırın.
2. **Project → Build Project** seçin.
3. Başarılı derlemede aşağıdaki çıktılar oluşur:

```text
STM32CubeIDE/Debug/STM32F746G_DISCO.elf
STM32CubeIDE/Debug/STM32F746G_DISCO.hex
STM32CubeIDE/Debug/STM32F746G_DISCO.map
STM32CubeIDE/Debug/STM32F746G_DISCO.list
```

Referans snapshot, GNU Tools 14.3.rel1 ile gerçek projede `make all` kullanılarak derlenmiştir.

## 7. Komut satırından derleme

CubeIDE bir kez Debug makefile’larını ürettikten sonra PowerShell’den derleme yapılabilir. CubeIDE kurulumunuzdaki gerçek dizinleri kullanın:

```powershell
$cubeIde = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE'
$gccBin = Join-Path $cubeIde 'plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin'
$makeBin = Join-Path $cubeIde 'plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.200.202604021615\tools\bin'
$env:Path = "$gccBin;$makeBin;$env:Path"

Set-Location .\STM32CubeIDE\Debug
make all -j8
arm-none-eabi-size .\STM32F746G_DISCO.elf
```

Plugin klasörlerindeki tarih/sürüm eki CubeIDE sürümüne göre değişebilir. Sabit yolu kopyalamak yerine kurulumunuzun `plugins` klasöründen doğru dizini bulun.

### Temiz derleme

```powershell
Set-Location .\STM32CubeIDE\Debug
make clean
make all -j8
```

Makefile CubeIDE tarafından üretildiği için elle düzenlenmemelidir. Kalıcı source/include/library değişikliklerini CubeIDE Project Properties üzerinden yapın.

## 8. CubeIDE ile karta yükleme

Bu projede bitmap/font varlıkları QSPI Flash’ta, program kodu ise dahili MCU Flash’ında bulunabilir. İkisini birlikte yüklemek için external loader gerekir.

1. Kartı **USB ST-LINK** konektöründen bağlayın.
2. Windows Aygıt Yöneticisi veya CubeProgrammer üzerinden ST-LINK’in göründüğünü kontrol edin.
3. CubeIDE’de **Run → Debug Configurations** açın.
4. `STM32F746G_DISCO Debug` yapılandırmasını seçin.
5. **Debugger** sekmesinde SWD ve **Connect under reset** kullanın.
6. **Startup/External Loaders** bölümünde şu loader etkin olmalıdır:

```text
W25Q128JVEIQ_STM32F746G-DISCO.stldr
```

7. **Apply → Debug** seçin.
8. İlk breakpoint `main()` üzerinde durduğunda Resume ile çalıştırın.

`STM32CubeIDE/STM32F746G_DISCO Debug.launch` dosyası QSPI loader ayarlı referans launch yapılandırmasıdır.

### Sadece çalıştırma

Debug oturumu istemiyorsanız aynı launch ayarını **Run As → STM32 C/C++ Application** ile kullanabilirsiniz. QSPI varlıkları değiştiyse external loader’ın hâlâ etkin olduğunu kontrol edin.

## 9. STM32CubeProgrammer ile flash

CubeProgrammer GUI yöntemi:

1. ST-LINK/SWD bağlantısını seçin.
2. External Loader listesinden `W25Q128JVEIQ_STM32F746G-DISCO` loader’ını işaretleyin.
3. `STM32CubeIDE/Debug/STM32F746G_DISCO.hex` dosyasını açın.
4. **Download** ve ardından verify çalıştırın.
5. Kartı resetleyin.

CLI yolu kurulumdan kuruluma değişir. Örnek:

```powershell
$programmer = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304\tools\bin\STM32_Programmer_CLI.exe'
& $programmer -c port=SWD mode=UR reset=HWrst -w .\STM32CubeIDE\Debug\STM32F746G_DISCO.hex -v -rst
```

Hex dosyasında 0x90000000 QSPI bölümü varsa CLI çağrısında external loader gerekebilir. CubeProgrammer sürümünüzün `-el` sözdizimini `STM32_Programmer_CLI --help` ile doğrulayın; GUI yöntemi loader seçimini daha görünür yaptığı için ilk kurulumda önerilir.

## 10. Tam silme ve kurtarma

Kart firmware’den sonra başlamıyor veya ST-LINK bağlanamıyorsa:

1. CubeProgrammer’da **Connect under reset** seçin.
2. Kartın RESET düğmesine basılı tutun, Connect’e basın ve sonra RESET’i bırakın.
3. Dahili Flash için full chip erase uygulayın.
4. QSPI loader’ı seçip gerekiyorsa external Flash’ı ayrıca silin/yazın.
5. Doğrulanmış `.hex` dosyasını yeniden yükleyin.

Full erase kayıtlı ayarları ve QSPI varlıklarını silebilir. Yalnızca normal flash işlemi sonuç vermediğinde kullanın.

## 11. İlk çalışma testi

- UI akıcı şekilde açılmalı ve dokunmatik yanıt vermelidir.
- SD kart takıldığında dosya sayısı görünmelidir.
- Bir WAV ve MP3 seçilerek ses, süre ve seek test edilmelidir.
- USB MSC için kaynak USB’ye alınmalı ve bellek takılıp çıkarılmalıdır.
- Ethernet bağlandığında IP adresi alınmalı; ping ve radyo ayrı ayrı denenmelidir.
- Recorder ile kısa kayıt alınıp `/Records` altında geri oynatılmalıdır.

## 12. Sık karşılaşılan sorunlar

### UI açılıyor ama görseller bozuk/boş

- QSPI external loader kullanılmamış olabilir.
- TouchGFX Generate Code sonrası QSPI bölümü yeniden flash edilmemiş olabilir.
- `STM32F746G_DISCO Debug.launch` yerine loader’sız launch seçilmiş olabilir.

### Linked resource bulunamadı

- CubeIDE’ye depo kökü yerine `STM32CubeIDE` klasörünü import edin.
- Proje dizin yapısını bozmayın.
- TouchGFX Generate Code işlemini tekrar çalıştırın.

### `arm-none-eabi-gcc` veya `make` bulunamadı

- CubeIDE’nin GNU Tools ve Make `tools/bin` klasörlerini PATH’e ekleyin.
- PowerShell script policy sorununda doğrudan `.exe` veya `.cmd` dosyasını çağırın.

### ST-LINK bağlanmıyor

- Veri kablosu kullanın.
- Doğru USB ST-LINK konektörüne bağlandığınızı doğrulayın.
- Başka CubeIDE/CubeProgrammer/GDB Server oturumlarını kapatın.
- Connect under reset deneyin.

### SD mount hatası

- FAT32 kullanın.
- Kartı güvenli biçimde çıkarıp yeniden takın.
- Başka bir düşük kapasiteli SD kartla karşılaştırın.
- GDB’de FatFs dönüş kodu ve SD DMA callback’lerini inceleyin.

### Ses var ancak UI yavaş

- Radyo/AAC ve hız-pitch modunda FFT’nin 512 nokta modunda olduğunu doğrulayın.
- UI thread’ine bloklayan dosya/ağ/codec işi eklenmediğini kontrol edin.
- Debug build ve açık Live Expressions’ın performansı etkileyebileceğini unutmayın.
