# Sensor Heatmap — Tasarim Dokumani

**Tarih:** 2026-03-29
**Durum:** Onaylandi

## Ozet

Klavye tuslarini sistem sensorlerine atayarak canli donanim izleme. Eklenti bazli sensor arayuzu, JSON config ile tus-sensor eslemesi, hazir profiller. Mevcut animasyon altyapisini kullanir.

## Sensor Plugin Arayuzu

```c
typedef struct {
    const char *name;           /* "cpu_temp", "cpu_load", "gpu_temp", ... */
    const char *description;    /* "CPU Temperature (C)" */
    float min_value;            /* Beklenen min (orn. 30.0) */
    float max_value;            /* Beklenen max (orn. 100.0) */
    int default_interval_ms;    /* Varsayilan okuma araligi */

    int  (*init)(void **ctx);   /* Sensor baslat, context ayir */
    float (*read)(void *ctx);   /* Deger oku (normalize edilmemis) */
    void (*destroy)(void *ctx); /* Temizle */
} f87_sensor_t;
```

Normalizasyon: `(value - min) / (max - min)` ile 0.0-1.0 araligina cevrilir.

## Yerlesik Sensorler

| Sensor | Kaynak | Varsayilan Interval |
|--------|--------|---------------------|
| cpu_temp | `/sys/class/thermal/thermal_zone*/temp` (tip taramasi ile) | 1000ms |
| cpu_load | `/proc/stat` (delta hesabi) | 500ms |
| gpu_temp | `/sys/class/drm/card*/device/hwmon/*/temp1_input` veya nvidia-smi | 1000ms |
| ram_usage | `/proc/meminfo` | 1000ms |

Her sensorun `init()` fonksiyonu dogru dosya yolunu dinamik olarak tarar — sabit yol kullanmaz.
Bulunamazsa sensor devre disi kalir, hata vermez. Tum Linux dagitimlarinda calisir.

## Config Sistemi (JSON)

```json
{
    "profile": "developer",
    "mappings": [
        {
            "sensor": "cpu_temp",
            "keys": ["F1", "F2", "F3", "F4"],
            "mode": "bar",
            "interval_ms": 1000
        },
        {
            "sensor": "cpu_load",
            "keys": ["F5", "F6", "F7", "F8"],
            "mode": "bar",
            "interval_ms": 500
        },
        {
            "sensor": "gpu_temp",
            "keys": ["ESC"],
            "mode": "color",
            "interval_ms": 1000
        },
        {
            "sensor": "ram_usage",
            "keys": ["F9", "F10", "F11", "F12"],
            "mode": "bar",
            "interval_ms": 1000
        }
    ]
}
```

### Modlar

- **`color`** — tek tus, sensor degerine gore mavi(0.0) -> yesil -> sari -> turuncu -> kirmizi(1.0) gradyan
- **`bar`** — tus dizisi, deger arttikca soldan saga tuslar yaniyor, her tus pozisyonuna gore renk alir, son tus kismi parlaklik ile orantili

### Hazir Profiller

Config dosyalari `configs/sensor/` altinda:
- `developer.json` — CPU temp/load + RAM
- `gamer.json` — GPU temp/load + CPU temp
- `system.json` — tum sensorler yayilmis

## Mimari

```
CLI/GUI
  |
  v
libf87
  +-- Sensor Thread: her sensor kendi interval_ms suresinde okunur
  |     sensor_data[MAX_SENSORS] (shared array + mutex)
  |
  +-- Anim Thread: config'deki mapping'lere gore
        sensor degeri -> renk hesapla -> frame buffer -> USB
```

### Sensor Thread

- Audio thread'e benzer, shared array + mutex
- En kisa interval'e gore uyur, her uyanmada zamani gelen sensorleri okur
- Cikti: `float normalized_values[MAX_SENSORS]` (0.0-1.0)
- Okuma hatasi: `error_flag[MAX_SENSORS]` set edilir

### Anim Thread (Render)

- Config parse edip mapping listesi olusturur
- Her frame: her mapping icin sensor degerini al -> moda gore (color/bar) tus renklerini hesapla
- Atanmamis tuslar karanlik kalir
- 30fps (sensor verisi yavas degisir)
- Sensor okuma hatasi durumunda: o mapping'in tuslari 500ms aralikla yanip soner

## Renk Paleti

### Color Modu

```
0.0      0.2       0.5       0.75      1.0
 |        |         |         |         |
mavi -> yesil -> sari -> turuncu -> kirmizi
```

### Bar Modu

Soldan saga tuslar yaniyor. Her yanan tus pozisyonuna gore renk alir:
- Ilk tuslar: yesil
- Orta tuslar: sari/turuncu
- Son tuslar: kirmizi
- Esik tusun parlakligi: sensor degerinin o tusa dusen kesri ile orantili

## CLI Kullanimi

```bash
f87ctl animate sensor                          # varsayilan profil (developer)
f87ctl animate sensor --profile gamer          # hazir profil
f87ctl animate sensor --config custom.json     # kullanici config
```

## Dosya Yapisi

```
lib/
  src/
    sensor.h          — f87_sensor_t arayuzu, registry, thread
    sensor.c          — sensor thread, dinamik tarama, yerlesik sensorler
    sensor_config.h   — config parse (JSON)
    sensor_config.c   — JSON okuma, mapping olusturma
    effects_sw.c      — sensor efekti eklenir (mevcut dosya)
configs/sensor/
    developer.json
    gamer.json
    system.json
```

## Hata Yonetimi

| Durum | Davranis |
|-------|----------|
| Sensor bulunamadi (init basarisiz) | O mapping atlanir, digerleri calisir |
| Config dosyasi bulunamadi | Varsayilan profil (developer) kullanilir |
| Gecersiz tus adi config'de | O mapping atlanir, stderr'e uyari |
| Sensor okuma hatasi | O mapping'in tuslari 500ms aralikla yanip soner |
| Sensor tekrar okuma basarili | Normal renge doner |

## Test

- Unit test: sensor init/read/destroy dongusu cokmemeli
- Unit test: read() min-max araliginda deger donmeli
- Unit test: config parse — gecerli JSON -> dogru mapping, gecersiz JSON -> hata kodu
- Donanim test: `f87ctl animate sensor --profile developer` ile canli test
