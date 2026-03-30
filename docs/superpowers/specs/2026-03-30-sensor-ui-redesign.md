# Sensor Monitor UI Redesign

## Overview

Sensor Monitor GUI'sini yeniden tasarlıyoruz. Mevcut durumda 3 hazır profil dropdown ile seçilip Start/Stop yapılıyor, preview simüle değerler gösteriyor. Yeni tasarımda: hazır profiller bilgilendirici gösterilecek, custom profil editörü eklenecek, preview gerçek sensör değerlerini tuşlar üzerinde gösterecek, tuş seçimi klavye üzerinde tıklama ile yapılacak.

## Current State

- Sidebar: "Sensor" kategorisinde 3 profil (developer, gamer, system) ayrı satırlar
- Controls: Profil dropdown + brightness + Start/Stop butonu
- Preview: Simüle salınan bar animasyonu (gerçek sensör verisi yok, profil-agnostik)
- Custom profil oluşturma yok (JSON dosyası gerekli)
- 4 sensör mevcut: cpu_temp, cpu_load, gpu_temp, ram_usage
- Her mapping: sensör + tuş listesi (max 20) + mod (color/bar) + interval

## Design Decisions

| Karar | Seçim |
|-------|-------|
| Sidebar girişi | Tek "Sensor Monitor" girişi (hazır profiller sidebar'da değil) |
| Profil/custom geçişi | Dropdown: Developer / Gamer / System / Custom |
| Custom tuş seçimi | Klavye preview üzerinde tıklama |
| Bar modu davranışı | Tıklanan tuş + sağa doğru N tuş (slider ile 2-8) |
| Preview gösterimi | Tuşlar üzerinde sensör adı + anlık değer yazısı |
| Custom iş akışı | Önce sensör seç → mod seç → tuşa tıkla |
| Sensör dropdown | Anlık değer gösterir: "CPU Temp (72°C)" |

## Architecture

### Sidebar Changes

- "Sensor" kategorisi altındaki 3 ayrı profil girişi kaldırılır
- Yerine tek "Sensor Monitor" girişi eklenir (effect_id = F87_SW_SENSOR, category = "sensor")

### Controls Panel — Hazır Profil Modu

Profil dropdown'dan Developer/Gamer/System seçildiğinde:

1. **Profil dropdown** — üstte, seçili profil adı
2. **Sensör atamaları özet paneli** — read-only kartlar:
   - Her mapping için: sensör adı (renkli), tuş aralığı, mod bilgisi
   - Kartlar yan yana, her sensör farklı renk (sabit renk paleti)
3. **Parlaklık slider** — mevcut gibi
4. **Başlat/Durdur butonu**

### Controls Panel — Custom Editör Modu

Profil dropdown'dan "Custom" seçildiğinde:

1. **Profil dropdown** — "Custom" seçili
2. **Sensör atama alanı:**
   - **Sensör dropdown** — anlık değer gösterir: "CPU Temp (72°C)", "GPU Temp (mevcut değil)"
     - Mevcut olmayan sensörler gri/disabled
   - **Mod seçici** — toggle buton: "Tek Tuş" | "Bar"
   - **Bar uzunluğu slider** — sadece Bar modunda görünür, 2-8 arası, varsayılan 4
   - **Bilgilendirme** — "Klavyede bir tuşa tıklayarak sensörü yerleştir" hint mesajı
3. **Atamalar listesi:**
   - Her atama bir satır: sensör adı (renkli) + tuş aralığı + mod + silme butonu (✕)
   - Atama tıklandığında klavyede ilgili tuşlar vurgulanır
4. **Klavye preview** — seçim modunda:
   - Atanmış tuşlar: ilgili sensörün rengiyle dolu
   - Boş tuşlar: kesikli çerçeve, tıklanabilir
   - Tıklama davranışı:
     - **Tek Tuş modu:** tıklanan tuş atanır
     - **Bar modu:** tıklanan tuş başlangıç, sağa doğru N tuş (slider değeri kadar) otomatik atanır
   - Çakışma kontrolü: zaten atanmış tuşa tıklanırsa uyarı veya mevcut atamayı sil
5. **Parlaklık slider**
6. **Butonlar:** "Başlat" + "Profil Kaydet"

### Preview Enhancement

Hem hazır hem custom profillerde preview aynı mantıkla çalışır:

- **Bar modu tuşları:** sensör değerine göre doluluk gradyanı (yeşil→sarı→kırmızı)
- **Tek tuş:** sensör değerine göre renk gradyanı
- **Etiketler:** her mapping grubunun tuşları altında sensör adı kısaltması + anlık değer
  - Örnek: "CPU 72°C", "Load 45%", "RAM 68%"
- **Güncelleme:** sensör okuma interval'ına göre (tipik 500ms-1s)

Preview'da gösterilen değerler:
- Hazır profil: henüz başlatılmamışsa simüle değerler (mevcut davranış iyileştirilmiş)
- Custom profil (editör): atama yapıldıkça anlık güncellenir
- Çalışan efekt: gerçek sensör değerleri

### Sensör Renk Paleti

Her sensöre sabit bir renk atanır (mapping sırasına göre değil, sensör tipine göre):

| Sensör | Renk | Hex |
|--------|------|-----|
| cpu_temp | Teal | #4ecdc4 |
| cpu_load | Coral | #ff6b6b |
| gpu_temp | Purple | #a855f7 |
| ram_usage | Yellow | #ffe66d |

Bu renkler: atama kartları border'ı, klavye tuş highlight'ı ve etiket rengi olarak kullanılır.

### Custom Profil Kaydetme

"Profil Kaydet" butonu tıklandığında:
- Mevcut atamaları JSON formatında `~/.config/f87control/profiles/sensor_custom.json` olarak kaydeder
- Daemon'a `SetSensorEffect` ile custom profil yolu gönderilir
- Sonraki oturumlarda Custom profil seçildiğinde bu dosyadan yüklenir

### Çakışma Yönetimi

- Bar modu yerleştirirken sağa doğru N tuş alınır — bu tuşlardan biri başka sensöre atanmışsa:
  - Atama yapılmaz, tuşun üzerinde kısa bir kırmızı flash gösterilir
  - Kullanıcı önce mevcut atamayı silmeli
- Klavyenin sonuna yakın tuşta bar yerleştirilirken sığmazsa (ör. F11'den 4 tuş bar):
  - Sığan kadar tuş atanır (F11, F12 = 2 tuş), slider değeri otomatik güncellenmez
  - Veya: atama yapılmaz ve "Yeterli tuş yok" uyarısı gösterilir

**Karar:** Sığmazsa atama yapılmasın, kısa uyarı gösterilsin. Kullanıcı ya bar uzunluğunu kısaltsın ya da daha solda bir tuş seçsin.

## Scope

### In Scope
- Sidebar'ı tek girişe dönüştürme
- Controls panelinde profil dropdown + hazır profil bilgi kartları
- Custom editör: sensör/mod/bar-uzunluk seçimi + klavye tıklama ile atama
- Preview'da sensör etiketleri (ad + değer)
- Atama listesi (silme ile)
- Çakışma kontrolü
- Custom profil JSON kaydetme/yükleme

### Out of Scope
- Yeni sensör ekleme (plugin sistemi zaten mevcut)
- Sensör interval ayarı (default kullanılır: cpu_load 500ms, diğerleri 1000ms)
- Custom renk paleti (sabit sensör renkleri)
- Hazır profilleri düzenleme (read-only, sadece custom düzenlenebilir)
- Profil adı verme (tek "custom" profil)

## File Impact

| Dosya | Değişiklik |
|-------|-----------|
| `gui/src/sidebar.c` | 3 profil girişi → tek "Sensor Monitor" girişi |
| `gui/src/controls.c` | Yeni sensör kontrol paneli (profil dropdown, editör UI, atama listesi) |
| `gui/src/keyboard_view.c` | Sensör seçim modu: tıklama ile atama, renk highlight, etiket overlay |
| `gui/src/preview.c` | Profil-aware sensör preview, etiket gösterimi |
| `gui/src/app_state.c` | Custom profil state yönetimi, sensör değer okuma |
| `gui/src/effect_meta.h` | Yeni parametre flag'leri (F87_PARAM_SENSOR_EDITOR?) |
| `lib/src/sensor.c` | Anlık sensör değer okuma public API (GUI'nin doğrudan kullanması için) |

## Sensör Değer Okuma Stratejisi

GUI, sensör dropdown'unda anlık değer göstermek için doğrudan `lib/src/sensor.h` API'sini kullanır (daemon üzerinden değil — GUI aynı makinede çalışır):

1. Controls paneli oluşturulurken `f87_sensor_count()` + `f87_sensor_get(i)` ile sensör listesi alınır
2. Her sensör için `sensor->init(&ctx)` → `sensor->read(ctx)` → `sensor->destroy(ctx)` ile tek seferlik değer okunur
3. Dropdown açıkken veya custom editör aktifken periyodik güncelleme (2-3s timer) ile değerler tazelenir
4. `init()` başarısız olan sensörler (ör. GPU sensörü yok) dropdown'da "(mevcut değil)" olarak gösterilir, seçilemez

Bu yaklaşım daemon bağımlılığını azaltır ve D-Bus'a yeni method eklemeye gerek kalmaz.
| `lib/src/sensor_config.c` | Custom profil JSON yazma fonksiyonu |
