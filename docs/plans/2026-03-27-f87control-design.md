# F87Control — Aula F87 Pro Linux RGB Kontrol Tasarım Belgesi

**Tarih:** 2026-03-27
**Durum:** Onaylandı

## Amaç

Aula F87 Pro klavyenin firmware'ini tersine mühendislik ile analiz edip, Linux ortamında klavye aydınlatmasını tam kontrol edebilecek bir uygulama geliştirmek. Windows-only olan resmi yazılımın tüm aydınlatma özelliklerini Linux'a taşımak.

## Kapsam

- Önceden tanımlı efektler (rainbow, breathing, wave vb.)
- Per-key RGB aydınlatma
- Reaktif aydınlatma (tuş basımına tepki)
- Klavye sensörleri (varsa)
- Sistem bilgilerine göre aydınlatma (CPU sıcaklığı, RAM vb.)
- Profil kaydetme/yükleme

## Mimari: Katmanlı Yaklaşım

```
GTK GUI (f87control) ──┐
                       ├──▶ libf87 (C API) ──▶ libusb ──▶ USB HID ──▶ Klavye
CLI Tool (f87ctl)   ──┘
```

Üç katman, her biri bağımsız kullanılabilir:
1. **libf87** — paylaşımlı C kütüphanesi, USB protokolünü soyutlar
2. **f87ctl** — komut satırı aracı, libf87 üzerine
3. **f87control** — GTK4 GUI uygulaması, libf87 üzerine

## Proje Yapısı

```
F87Control/
├── lib/                    # libf87
│   ├── include/f87/
│   │   ├── f87.h           # Ana public API
│   │   ├── device.h        # Cihaz bulma/bağlanma
│   │   ├── lighting.h      # Aydınlatma kontrol API
│   │   ├── effects.h       # Efekt tanımları
│   │   └── protocol.h      # Internal — USB protokol detayları
│   └── src/
│       ├── device.c         # USB cihaz yönetimi (libusb)
│       ├── protocol.c       # HID paket oluşturma/parse etme
│       ├── lighting.c       # Aydınlatma komutları
│       └── effects.c        # Efekt motor mantığı
├── cli/                    # f87ctl
│   └── src/
│       └── main.c
├── gui/                    # f87control
│   ├── src/
│   │   ├── main.c
│   │   ├── app.c           # GtkApplication
│   │   ├── keyboard_widget.c # Klavye layout çizimi (Cairo)
│   │   └── effects_panel.c  # Efekt ayar paneli
│   └── resources/
│       ├── layout.ui        # GTK UI tanımları
│       └── f87control.css   # Stil
├── tools/                  # Tersine mühendislik araçları
│   ├── pcap_parser.py      # Wireshark yakalamalarını analiz
│   └── protocol_notes.md   # Protokol dokümantasyonu
├── udev/
│   └── 99-f87.rules        # udev kuralları (root'suz erişim)
├── docs/plans/
├── CMakeLists.txt
└── README.md
```

## libf87 API

### Cihaz Yönetimi (device.h)

```c
f87_ctx *f87_init(void);
void f87_exit(f87_ctx *ctx);
int f87_find_devices(f87_ctx *ctx, f87_device_info **list, int *count);
f87_device *f87_open(f87_ctx *ctx, f87_device_info *info);
void f87_close(f87_device *dev);
const char *f87_get_firmware_version(f87_device *dev);
int f87_get_battery_level(f87_device *dev);
```

### Aydınlatma (lighting.h)

```c
int f87_set_brightness(f87_device *dev, uint8_t level);
int f87_get_brightness(f87_device *dev, uint8_t *level);
int f87_lights_off(f87_device *dev);
int f87_set_key_color(f87_device *dev, uint8_t key_id, f87_color color);
int f87_set_all_keys(f87_device *dev, f87_color color);
int f87_set_key_map(f87_device *dev, const f87_color *colors, int count);
int f87_apply(f87_device *dev);

typedef struct { uint8_t r, g, b; } f87_color;
typedef struct {
    uint8_t key_id;
    const char *name;
    uint8_t row, col;
} f87_key_info;

int f87_get_key_count(f87_device *dev);
const f87_key_info *f87_get_key_map(f87_device *dev);
```

### Efektler (effects.h)

```c
typedef enum {
    F87_EFFECT_STATIC, F87_EFFECT_BREATHING, F87_EFFECT_WAVE,
    F87_EFFECT_RAINBOW, F87_EFFECT_RIPPLE, F87_EFFECT_REACTIVE,
} f87_effect_type;

typedef struct {
    f87_effect_type type;
    uint8_t speed;
    uint8_t brightness;
    f87_color color1, color2;
    uint8_t direction;
} f87_effect;

int f87_set_effect(f87_device *dev, const f87_effect *effect);
int f87_get_current_effect(f87_device *dev, f87_effect *effect);
int f87_get_supported_effects(f87_device *dev, f87_effect_type **list, int *count);
```

### Internal Protokol (protocol.h)

```c
#define F87_VENDOR_ID   0x????
#define F87_PRODUCT_ID  0x????
#define F87_PKT_SIZE    64

typedef struct {
    uint8_t report_id;
    uint8_t command;
    uint8_t sub_command;
    uint8_t payload[F87_PKT_SIZE - 3];
} f87_packet;

int f87_pkt_send(f87_device *dev, const f87_packet *pkt);
int f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms);
```

## CLI Aracı (f87ctl)

```
f87ctl info                          # Cihaz bilgisi
f87ctl list                          # Bağlı cihazlar
f87ctl brightness 80                 # Parlaklık %80
f87ctl off                           # Işıkları kapat
f87ctl effect rainbow --speed 5      # Efekt ayarla
f87ctl key set ESC ff0000            # Per-key renk
f87ctl key set-all 0000ff            # Tüm tuşlar
f87ctl key load profile.json         # Profil yükle
f87ctl raw send "04 01 ff 00 00"     # Ham paket (RE/debug)
f87ctl raw listen                    # Paket dinle
f87ctl dump                          # Durum JSON çıktısı
```

## GTK GUI (f87control)

Ana pencere:
- **Üst bar:** Cihaz seçici, firmware versiyonu, pil durumu
- **Orta:** Klavye layout widget'ı (Cairo ile çizim, tıklanabilir tuşlar)
- **Sol panel:** Sekme navigasyonu (Efektler, Per-Key, Profiller, Sensörler, Ayarlar)
- **Sağ panel:** Seçili sekmenin içeriği
- **Alt bar:** Durum çubuğu

Klavye widget: GtkDrawingArea + Cairo, tuş seçimi (tıkla, shift+tıkla, sürükle), renk seçici entegrasyonu.

## Tersine Mühendislik Stratejisi

1. **Keşif:** VID/PID tespiti, HID interface/endpoint belirleme, init paketleri yakalama
2. **Sistematik Yakalama:** Her efekt/parametre için ayrı pcap dosyası
3. **Binary Analiz:** Driver DLL/EXE Ghidra ile analiz, USB gönderim fonksiyonları, checksum tespiti
4. **Doğrulama:** Linux'ta libusb ile ilk test paketi, bilinen komutu çalıştır

Araç: `tools/pcap_parser.py` — pcap dosyalarını parse edip paket yapısını analiz eder.

## Bağımlılıklar

- libusb-1.0 — USB HID iletişimi
- GTK4 — GUI
- json-c — Profil dosyaları
- lm-sensors — CPU sıcaklığı (opsiyonel)
- CMake >= 3.16 — Build sistemi

## Geliştirme Fazları

- **Faz 0:** Tersine mühendislik araçları (pcap_parser.py)
- **Faz 1:** libf87 çekirdek (device.c, protocol.c) + ilk USB iletişim
- **Faz 2:** f87ctl — CLI ile parlaklık, efekt, per-key test
- **Faz 3:** libf87 tam API (tüm efektler, per-key, sensörler)
- **Faz 4:** f87control — GTK GUI
- **Faz 5:** Sensör entegrasyonu, profiller, ince ayar

## Teknik Kararlar

- **Toplu güncelleme:** f87_apply() ile batch gönderim
- **Hata yönetimi:** Negatif dönüş değeri (errno tarzı)
- **Root'suz erişim:** udev kuralı ile
- **Build:** CMake, her katman ayrı hedef
