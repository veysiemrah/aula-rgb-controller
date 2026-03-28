# Yazilimsal Efektler ve Muzige Uyumlu Aydinlatma — Tasarim Dokumani

**Tarih:** 2026-03-28
**Durum:** Onaylandi

## Ozet

libf87 kutuphanesine yazilimsal animasyon motoru ve muzik reaktif LED efektleri eklenmesi. Producer-consumer (iki thread) mimarisi ile ses analizi ve render ayristirilir. CLI sadece test/gelistirme icin; asil kullanici arayuzu GUI (Faz 5) olacak.

## Mimari: Producer-Consumer (Iki Thread)

```
+---------------------------------------------------------+
|                    CLI / GUI                             |
|         f87_anim_start() / f87_anim_stop()              |
+----------------------------+----------------------------+
                             |
+----------------------------v----------------------------+
|                      libf87                             |
|                                                         |
|  +----------------+   ring buffer   +-----------------+ |
|  | Audio Thread   | -------------> | Anim Thread     | |
|  |                | audio_data_t   |                 | |
|  | - PulseAudio   |                | - efekt sec     | |
|  | - KissFFT      |                | - frame hesapla | |
|  | - beat detect  |                | - USB gonder    | |
|  +----------------+                +-----------------+ |
|                                          |              |
|                              f87_direct_send_frame()    |
+----------------------------+----------------------------+
                             |
                      +------v------+
                      |  USB / HID  |
                      |  Klavye     |
                      +-------------+
```

### Veri Akisi

1. Audio thread ses yakalar -> FFT -> beat tespiti -> `f87_audio_data_t` ring buffer'a yazar
2. Anim thread ring buffer'dan en guncel veriyi okur -> aktif efekte verir -> frame hesaplar -> USB'ye gonderir
3. Muzik modu degilse audio thread hic baslamaz

### Public API (animate.h)

```c
typedef struct {
    uint8_t color[3];                // ana renk (varsayilan efekte gore)
    uint8_t brightness;              // 1-4 (varsayilan 3)
    uint8_t speed;                   // 0-4 (varsayilan 2)
    f87_audio_source_t audio_source; // F87_AUDIO_MONITOR veya F87_AUDIO_MIC
} f87_anim_config_t;

f87_anim_ctx_t *f87_anim_start(f87_device *dev, int effect_id, f87_anim_config_t *config);
int f87_anim_stop(f87_anim_ctx_t *ctx);
int f87_anim_set_effect(f87_anim_ctx_t *ctx, int effect_id);
int f87_anim_set_color(f87_anim_ctx_t *ctx, uint8_t r, uint8_t g, uint8_t b);
```

## Audio Thread — Ses Yakalama ve Analiz

### Ses Yakalama

- **Kaynaklar:** PulseAudio monitor (sistem sesi) veya mikrofon — kullanici secer
- **API:** PulseAudio Simple API (`pa_simple`) — thread zaten ayri, asenkron gereksiz
- **PipeWire uyumlulugu:** PipeWire'in PulseAudio uyumluluk katmani uzerinden otomatik calisir
- **Format:** 44100Hz, mono, float32
- **Buffer:** 1024 sample per read (~23ms)
- **Kaynak enum:** `F87_AUDIO_MONITOR` veya `F87_AUDIO_MIC`

### Frekans Analizi (KissFFT)

```
PCM samples -> Hanning window -> KissFFT -> magnitude spectrum -> band grouping
```

- **FFT boyutu:** 2048 sample (44100Hz'de ~21Hz frekans cozunurlugu)
- **Windowing:** Hanning
- **6 frekans bandi** (normalize 0.0 - 1.0):
  - sub_bass: 20-60 Hz
  - bass: 60-250 Hz
  - low_mid: 250-500 Hz
  - mid: 500-2000 Hz
  - high_mid: 2000-4000 Hz
  - treble: 4000-16000 Hz

### Beat Tespiti

- **Yontem:** Bass bandinin (20-250Hz) anlik enerjisi > son 500ms ortalama x esik degeri
- **Cooldown:** Beat'ler arasi minimum 100ms (600 BPM ust limit)
- **Cikti:** `beat` flag + `beat_intensity` (0.0-1.0)

### Ring Buffer'a Yazilan Struct

```c
typedef struct {
    float bands[6];         // normalize frekans bantlari (0.0-1.0)
    float energy;           // toplam ses enerjisi (0.0-1.0)
    float beat_intensity;   // beat siddeti (0.0-1.0, 0=beat yok)
    bool  beat;             // bu frame'de beat var mi
    uint64_t timestamp_us;  // mikrosaniye zaman damgasi
} f87_audio_data_t;
```

### Thread Dongusu

Audio ~43Hz'de calisir (1024/44100). 30fps render'dan hizli — anim thread her zaman guncel veri bulur.

```
while (running):
    pa_simple_read(1024 samples)
    hanning_window(samples)
    kiss_fft(samples -> spectrum)
    group_bands(spectrum -> bands[6])
    detect_beat(bands -> beat)
    ring_buffer_write(audio_data)
```

## Animasyon Thread — Efekt Motoru

### Frame Rate

- **Yazilimsal efektler:** 30fps sabit (direct mode limiti)
- **Muzik reaktif:** Ses analizi daha yuksek rate'de calisabilir. Varsayilan 1024 sample buffer'da dogal rate ~43Hz. Daha yuksek rate icin buffer boyutu kucultulebilir (512 sample = ~86Hz) veya overlapping window kullanilabilir. Gercek donanim uzerinde CPU ve latency testi ile en uygun deger belirlenecek. USB render her durumda 30fps'e throttle edilir.

### Efekt Arayuzu

```c
typedef struct {
    const char *name;
    bool needs_audio;

    void (*init)(f87_effect_ctx_t *ctx);
    void (*render)(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                   const f87_audio_data_t *audio);
    void (*destroy)(f87_effect_ctx_t *ctx);
} f87_sw_effect_t;
```

### Efekt Context

```c
typedef struct {
    f87_device *dev;
    uint8_t base_color[3];
    uint8_t brightness;       // 1-4
    uint8_t speed;            // 0-4
    uint64_t frame_count;
    uint64_t start_time_us;
    void *effect_data;        // efekte ozel durum verisi
} f87_effect_ctx_t;
```

### Frame Buffer

```c
typedef struct {
    uint8_t keys[88][3];  // RGB per key
} f87_frame_t;
```

### Animasyon Dongusu

```
f87_direct_mode_enable()

while (running):
    t0 = now()
    audio = ring_buffer_read_latest()
    active_effect->render(ctx, &frame, audio)
    frame_to_direct_mode(frame) -> led_buffer
    f87_direct_send_frame(dev, led_buffer)
    sleep(33ms - elapsed)

f87_direct_mode_disable()
```

## Yazilimsal Efektler (10 Adet)

Donanim efektlerinden ayrisan, yazilim avantajini kullanan efektler:

| ID | Efekt | Aciklama |
|----|-------|----------|
| SW_FIRE | Ates | Alttan yukari titreyen alev simulasyonu (Doom fire algoritmasi) |
| SW_MATRIX | Matrix | Yukaridan asagi akan yesil karakter yagmuru, rastgele hizlarda |
| SW_PLASMA | Plazma | Sinus dalgalarinin kesisimi ile akan renkli plazma deseni |
| SW_HEATMAP | Sicaklik haritasi | CPU/GPU sicaklik -> renk (mavi=soguk, kirmizi=sicak) |
| SW_REACTIVE_EXPLODE | Patlama | Tus basiminda disa yayilan cok renkli patlama, fizik simulasyonu |
| SW_REACTIVE_RIPPLE | Su dalgasi | Tus basiminda gercekci su dalgasi yayilimi, dalgalar kesisir |
| SW_TYPEWRITER | Daktilo | Basilan tus yanar, zaman gectikce kizarip soner |
| SW_RADAR | Radar | Merkezden donen isin, gectigi tuslari aydinlatir ve iz birakir |
| SW_LIGHTNING | Simsek | Rastgele tuslar arasinda cakan kisa elektrik arklari |
| SW_LIFE | Game of Life | Conway's Game of Life, tus basimiyla hucre ekleme |

## Muzik Reaktif Efektler (5 Adet)

| ID | Efekt | Aciklama |
|----|-------|----------|
| MU_SPECTRUM | Spektrum | 6 bant -> tus siralarina esle, alt=bass ust=treble |
| MU_BEAT | Beat pulse | Beat'te tum klavye flas, yogunluga gore renk |
| MU_ENERGY | Enerji dalga | Merkezden disa yayilan dalga, enerji ile orantili |
| MU_VU | VU meter | Sol->sag ses seviye cubugu |
| MU_FREQ_MAP | Frekans harita | Her tus sirasini bir frekans bandina boya |

## Ring Buffer ve Thread Senkronizasyonu

### Lock-Free SPSC Ring Buffer

```c
#define F87_AUDIO_RING_SIZE 8

typedef struct {
    f87_audio_data_t slots[F87_AUDIO_RING_SIZE];
    _Atomic uint32_t write_idx;
    _Atomic uint32_t read_idx;
} f87_audio_ring_t;
```

- 8 slot, power of 2 — atomic index ile senkronizasyon
- Audio thread write_idx arttirir, anim thread read_idx'i write_idx'e yakalar
- En guncel veri okunur, eski frame'ler atlanir

### Thread Yasam Dongusu

```
f87_anim_start():
  +-> audio thread baslat (eger needs_audio)
  |     -> pa_simple_new() -> dongu
  +-> anim thread baslat
  |     -> direct_mode_enable() -> dongu
  +-> return ctx (non-blocking)

f87_anim_stop():
  +-> running = false (atomic flag)
  +-> anim thread join -> direct_mode_disable()
  +-> audio thread join -> pa_simple_free()
```

### Graceful Shutdown

- `_Atomic bool running` flag'i ile temiz cikis
- Anim thread cikmadan once direct mode disable — klavye onceki efekte doner
- `f87_anim_stop()` her iki thread'i `pthread_join()` ile bekler

## Tus Basimi Yakalama (Reaktif Efektler)

- `/dev/input/eventX` uzerinden non-blocking `poll()` ile
- Anim thread dongusunde kontrol edilir, ayri thread degil
- Sadece reaktif efektler (SW_REACTIVE_EXPLODE, SW_REACTIVE_RIPPLE, SW_TYPEWRITER, SW_LIFE) aktifken okunur

## CLI (Test/Gelistirme)

```bash
f87ctl animate <efekt_adi> [renk] [--speed 0-4] [--brightness 1-4]
f87ctl music <mod_adi> [--source monitor|mic]
f87ctl animate stop
```

Foreground'da calisir, Ctrl+C ile durur. Asil kullanici arayuzu GUI (Faz 5).

## Build Sistemi

```cmake
# Yeni bagimliliklar
pkg_check_modules(PULSE libpulse-simple)

# KissFFT kaynak olarak dahil
add_subdirectory(vendor/kissfft)

# Opsiyonel audio
option(BUILD_AUDIO "Build with audio support" ON)

if(BUILD_AUDIO AND PULSE_FOUND)
    target_sources(f87 PRIVATE src/audio.c src/spectrum.c src/visualizer.c)
    target_link_libraries(f87 ${PULSE_LIBRARIES} kissfft)
    target_compile_definitions(f87 PRIVATE F87_HAS_AUDIO=1)
endif()

# Animasyon motoru her zaman dahil
target_sources(f87 PRIVATE src/animate.c src/effects_sw.c)
target_link_libraries(f87 pthread)
```

PulseAudio yoksa yazilimsal efektler calisir, muzik modlari devre disi kalir.

## Dosya Yapisi

```
lib/
  include/f87/
    animate.h          # public API
    audio_types.h      # f87_audio_data_t, f87_audio_source_t
  src/
    animate.c          # thread yonetimi, frame dongusu, ring buffer
    effects_sw.c       # 10 yazilimsal efekt
    audio.c            # PulseAudio yakalama thread
    spectrum.c         # KissFFT, band grouping, beat tespiti
    visualizer.c       # 5 muzik reaktif efekt
vendor/
  kissfft/             # KissFFT kaynak
```

## Test Stratejisi

### Unit Testler (Donanimsiz)

| Test | Kapsam |
|------|--------|
| test_spectrum | KissFFT: bilinen sinus dalgasi -> dogru frekans bandi |
| test_beat | Beat tespiti: yapay bass spike -> algilansin, sessizlik -> algilanmasin |
| test_ring_buffer | Lock-free SPSC: eszamanli yaz/oku, veri kaybi yok |
| test_effects_sw | Her efektin render fonksiyonu: NULL audio ile cokmemeli |
| test_frame_convert | key_id -> led_index donusumu dogrulama |

### Donanim Testleri (Manuel, CLI ile)

Her efekt calistirilir, gozle dogrulanir. Muzik modlari gercek ses kaynagi ile test edilir.

## Hata Yonetimi

| Durum | Davranis |
|-------|----------|
| USB kopmasi | Anim thread durur, ctx->error = F87_ERR_USB |
| PulseAudio baglanti hatasi | Audio thread baslamaz, muzik modu F87_ERR_AUDIO doner |
| Ses kaynagi kaybolmasi | Audio thread durur, anim thread ses verisi olmadan devam eder |
| Ctrl+C / SIGTERM | Signal handler f87_anim_stop() cagirir, temiz cikis |
| Efekt degisimi | Atomic pointer swap, destroy() + init() anim thread icerisinde |

### Yeni Hata Kodlari

```c
#define F87_ERR_AUDIO    -7
#define F87_ERR_ANIMATE  -8
```
