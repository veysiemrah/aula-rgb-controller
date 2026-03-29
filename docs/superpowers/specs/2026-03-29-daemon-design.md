# F87Control Daemon (f87d) — Tasarim Belgesi

**Tarih:** 2026-03-29
**Durum:** Onaylandi
**Kapsam:** Faz 6.1 — Daemon mode

## Amac

GUI ve CLI'dan bagimsiz bir arka plan servisi (`f87d`) olusturmak. Tum USB erisimi ve efekt calistirma daemon'a ait olacak. Istemciler (GUI, CLI) D-Bus uzerinden daemon'a komut gonderir.

Motivasyon:
- Tek sahip modeli: USB cihaza ayni anda birden fazla process erisimini onler
- Hotplug/reconnect: Klavye cikartilip takildiginda otomatik algilama
- Dayaniklilik: GUI kapansa bile SW efekt devam eder
- Profil restore: Login'de otomatik profil uygulama
- Gelecek fazlar (profiller, wireless) icin altyapi

## Mimari

```
                    D-Bus (sd-bus)
GUI (f87control) ────────────────┐
                                 ▼
CLI (f87ctl)     ────────────────▶  f87d (daemon)
                                     │
                                     ├── libf87 (USB HID)
                                     ├── Animasyon thread'leri
                                     ├── PulseAudio capture (muzik)
                                     └── Sensor polling
                                     │
                                     ▼
                                  USB ──▶ Klavye
```

### Bilesenler

1. **f87d** — daemon process (`daemon/src/main.c`)
   - sd-bus ile D-Bus arayuzu sunar
   - libf87 uzerinden USB erisimi
   - Animasyon/muzik/sensor thread'lerini yonetir
   - Hotplug izleme (libusb hotplug API veya udev monitor)
   - Idle timeout yonetimi

2. **libf87 proxy katmani** — (`lib/src/client.c`, `lib/include/f87/client.h`)
   - Mevcut libf87 API imzalarina uyumlu D-Bus istemci sarmalayici
   - `f87_client_connect()` ile daemon'a baglanma
   - GUI/CLI mevcut kodlarini minimal degisiklikle kullanmaya devam eder

3. **D-Bus yapilandirma dosyalari**
   - `dbus/org.f87.Control.service` — auto-activation
   - `systemd/f87d.service` — user service unit

## D-Bus API

**Bus:** Session bus (kullanici servisi)
**Service name:** `org.f87.Control`
**Object path:** `/org/f87/Control`
**Interface:** `org.f87.Control`

### Metodlar

| Metod | Parametreler | Donus | Aciklama |
|-------|-------------|-------|----------|
| `SetEffect` | `effect_id: i, brightness: y, speed: y, colorful: b, r: y, g: y, b: y` | `b` (basari) | HW efekt uygula |
| `SetSwEffect` | `effect_id: i, brightness: y, speed: y, r: y, g: y, b: y, fps: i` | `b` | SW efekt baslat |
| `SetMusicEffect` | `effect_id: i, brightness: y, r: y, g: y, b: y, gain: d` | `b` | Muzik efekti baslat |
| `SetSensorEffect` | `profile: s, config_path: s` | `b` | Sensor efekti baslat |
| `SetColor` | `r: y, g: y, b: y` | `b` | Aktif efektin rengini degistir |
| `SetBrightness` | `level: y` | `b` | Parlaklik ayarla (1-4) |
| `SetPerKeyColor` | `key_index: y, r: y, g: y, b: y` | `b` | Tek tus rengi |
| `ApplyPerKeyColors` | `colors: a(yyy)` | `b` | Tum tuslari tek seferde gonder (88 RGB) |
| `Stop` | — | `b` | Aktif efekti durdur |
| `Off` | — | `b` | LED'leri kapat |
| `SaveProfile` | `name: s` | `b` | Mevcut durumu profil olarak kaydet |
| `LoadProfile` | `name: s` | `b` | Profili yukle ve uygula |
| `DeleteProfile` | `name: s` | `b` | Profili sil |
| `ListProfiles` | — | `as` | Kayitli profil isimlerini listele |
| `GetDeviceInfo` | — | `a{sv}` | Cihaz bilgileri (urun, firmware, VID/PID) |
| `GetBatteryLevel` | — | `i` | Batarya seviyesi (-1 = kablolu) |
| `Rescan` | — | `b` | Cihaz taramasini tekrarla |
| `GetStatus` | — | `a{sv}` | Daemon durumu (bagli mi, aktif efekt, vb.) |

### Signals

| Signal | Parametreler | Aciklama |
|--------|-------------|----------|
| `DeviceConnected` | `product: s, vid: q, pid: q` | Klavye baglandi |
| `DeviceDisconnected` | — | Klavye ayrildi |
| `EffectChanged` | `effect_id: i, category: s` | Aktif efekt degisti |
| `BatteryLevelChanged` | `level: i` | Batarya seviyesi degisti |
| `ErrorOccurred` | `code: i, message: s` | Hata olustu |

### Properties (read-only)

| Property | Tip | Aciklama |
|----------|-----|----------|
| `Connected` | `b` | Klavye bagli mi |
| `ActiveEffect` | `i` | Aktif efekt ID (-1 = yok) |
| `ActiveCategory` | `s` | "hw", "sw", "music", "sensor", "" |
| `Brightness` | `y` | Mevcut parlaklik (1-4) |
| `BatteryLevel` | `i` | Batarya seviyesi (-1 = kablolu) |

## Daemon Yasam Dongusu

### Baslatma
1. D-Bus auto-activation veya `systemctl --user start f87d`
2. `f87_init()` → `f87_find_devices()` → `f87_open()`
3. D-Bus arayuzunu kaydet, sinyal yayinla (`DeviceConnected` veya hata)
4. Eger profil varsa, varsayilan profili uygula

### Hotplug
- libusb hotplug callback veya udev monitor ile klavye takilma/cikarilmasini izle
- Cikarildiginda: aktif efekti durdur, `DeviceDisconnected` sinyali
- Takildiginda: otomatik baglan, son profili/efekti geri yukle, `DeviceConnected` sinyali

### Idle Timeout
- **Kosul:** GUI/CLI bagli degil VE aktif efekt HW (veya efekt yok)
- **Sure:** 5 dakika (varsayilan)
- **Davranis:** Daemon tamamen kapanir, process sonlanir
- **SW efekt aktifken:** Timeout devre disi — daemon calismaya devam eder
- **Tekrar baslatma:** Sonraki D-Bus cagrisi auto-activation ile daemon'u tekrar baslatir

Idle timeout icin istemci sayaci:
- Her D-Bus metod cagrisi timeout'u sifirlar
- GUI/CLI `org.freedesktop.DBus.NameOwnerChanged` ile izlenebilir veya basit ping/heartbeat

### Kapanma
1. SIGTERM veya `systemctl --user stop f87d`
2. Aktif animasyon thread'lerini durdur (`f87_anim_stop()`)
3. USB baglantiyi kapat (`f87_close()`)
4. D-Bus baglantisini birak
5. Temiz cikis

## Proxy Katmani (libf87-client)

### API

```c
#include <f87/client.h>

/* Daemon'a baglan (D-Bus session bus) */
f87_client *f87_client_connect(void);

/* Baglanti kapat */
void f87_client_disconnect(f87_client *client);

/* Mevcut API'ye uyumlu fonksiyonlar */
int f87_client_set_effect(f87_client *client, int effect_id,
                          uint8_t brightness, uint8_t speed,
                          uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);

int f87_client_set_sw_effect(f87_client *client, int effect_id,
                             const f87_anim_config_t *config);

int f87_client_set_color(f87_client *client, uint8_t r, uint8_t g, uint8_t b);
int f87_client_set_brightness(f87_client *client, uint8_t level);
int f87_client_stop(f87_client *client);
int f87_client_off(f87_client *client);

/* Durum sorgulama */
int f87_client_get_status(f87_client *client, f87_client_status_t *status);
int f87_client_get_device_info(f87_client *client, f87_device_info *info);
int f87_client_get_battery(f87_client *client);

/* Profil yonetimi */
int f87_client_save_profile(f87_client *client, const char *name);
int f87_client_load_profile(f87_client *client, const char *name);
int f87_client_delete_profile(f87_client *client, const char *name);
int f87_client_list_profiles(f87_client *client, char ***names, int *count);
void f87_client_free_profile_list(char **names, int count);

/* Signal callback'leri */
typedef void (*f87_client_device_cb)(int connected, const char *product, void *userdata);
typedef void (*f87_client_effect_cb)(int effect_id, const char *category, void *userdata);
typedef void (*f87_client_battery_cb)(int level, void *userdata);

int f87_client_on_device_change(f87_client *client, f87_client_device_cb cb, void *userdata);
int f87_client_on_effect_change(f87_client *client, f87_client_effect_cb cb, void *userdata);
int f87_client_on_battery_change(f87_client *client, f87_client_battery_cb cb, void *userdata);
```

### GUI Gecisi

Mevcut `app_state.c` degisiklikleri:
- `f87_app_state_init()`: `f87_init()` + `f87_open()` yerine `f87_client_connect()`
- `f87_app_state_start_hw()`: `f87_set_effect()` yerine `f87_client_set_effect()`
- `f87_app_state_start_sw()`: `f87_anim_start()` yerine `f87_client_set_sw_effect()`
- Signal callback'leri ile durum guncellemeleri (baglanti, efekt degisimi)
- `f87_app_state_destroy()`: `f87_client_disconnect()`

### CLI Gecisi

`cli/src/main.c` degisiklikleri:
- `f87_init()` + `f87_open()` yerine `f87_client_connect()`
- Tum komutlar `f87_client_*` fonksiyonlarina yonlendirilir
- `--direct` flag ile daemon bypass (debug/gelistirme icin)

## Dosya Yapisi

```
daemon/
├── CMakeLists.txt
├── src/
│   ├── main.c              # Daemon giris noktasi, signal handling
│   ├── dbus_interface.c    # D-Bus metod/signal/property handler'lari
│   ├── dbus_interface.h
│   ├── device_manager.c    # Hotplug, reconnect, cihaz yasam dongusu
│   ├── device_manager.h
│   ├── effect_manager.c    # Efekt baslat/durdur, thread yonetimi
│   ├── effect_manager.h
│   ├── idle_monitor.c      # Idle timeout mantigi
│   └── idle_monitor.h
lib/
├── include/f87/
│   └── client.h            # Proxy katmani public API
├── src/
│   └── client.c            # D-Bus istemci implementasyonu
dbus/
├── org.f87.Control.service  # D-Bus auto-activation
├── org.f87.Control.conf     # D-Bus policy (opsiyonel)
systemd/
└── f87d.service             # systemd user service unit
```

## Bagimliliklar

- **sd-bus** (systemd'nin parcasi, ek kurulum gerektirmez)
- Mevcut: libusb-1.0, libjson-c, libpulse (muzik efektleri icin)
- CMake: `pkg_check_modules(SYSTEMD libsystemd)` ile sd-bus bulunur

## Build Entegrasyonu

```cmake
option(BUILD_DAEMON "Build f87d daemon" ON)

if(BUILD_DAEMON)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
    add_executable(f87d ...)
    target_link_libraries(f87d PRIVATE f87 ${SYSTEMD_LIBRARIES})
endif()
```

Proxy katmani (`client.c`) her zaman libf87 ile birlikte derlenir (sd-bus bagimliligi).

## Hata Yonetimi

- D-Bus hatalari standart `org.freedesktop.DBus.Error.*` formatinda
- Uygulamaya ozel hatalar: `org.f87.Error.NotConnected`, `org.f87.Error.InvalidEffect`, `org.f87.Error.USBError`
- Proxy katmani D-Bus hatalarini mevcut `F87_ERR_*` kodlarina cevirir

## Test Stratejisi

- **Birim testleri:** `dbus_interface`, `device_manager`, `effect_manager`, `idle_monitor` mock'lanmis D-Bus ile
- **Entegrasyon testi:** Daemon'u baslatip CLI uzerinden komut gondererek dogrulama
- **Donanim testi:** Gercek klavye ile hotplug, efekt gecisi, profil kaydet/yukle

## Kapsam Disi (Bu Fazda)

- Coklu cihaz destegi (tek klavye varsayimi)
- Wireless spesifik ozellikler (Faz 6.3)
- Profil dosya formati detaylari (Faz 6.2)
- GUI profil yonetimi arayuzu (Faz 6.2)
