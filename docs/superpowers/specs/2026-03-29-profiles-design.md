# F87Control Profiller — Tasarim Belgesi

**Tarih:** 2026-03-29
**Durum:** Onaylandi
**Kapsam:** Faz 6.2 — Profil kaydetme/yukleme

## Amac

Efekt ayarlarini profil olarak kaydetmek ve yuklemek. Daemon baslangicindan son durumu hatirlama. Kullanicinin farkli senaryolar icin hazir profiller olusturabilmesi.

## Profil Icerigi

Her profil asagidaki bilgiyi saklar:

- **Efekt durumu:** kategori (hw/sw/music/sensor), effect_id, brightness, speed, renk (RGB), colorful flag
- **Yan isik modu:** side_light (0=off, 1=rainbow, 2=breath mix, 3=static red, 4=breath red)
- **Batarya isik modu:** battery_light (ayni degerler)
- **Per-key renk haritasi:** 88 tus RGB (custom mode icin), veya null

## JSON Formati

```json
{
  "name": "gaming",
  "category": "hw",
  "effect_id": 3,
  "brightness": 4,
  "speed": 2,
  "colorful": false,
  "color": [255, 0, 0],
  "side_light": 1,
  "battery_light": 0,
  "per_key_colors": null
}
```

- `per_key_colors`: `null` ise per-key mevcut degil. Custom mode icin 88 elemanli dizi: `[[r,g,b], [r,g,b], ...]`
- `category`: `"hw"`, `"sw"`, `"music"`, `"sensor"`, `""`
- Sensor profili durumunda ek alanlar:
  - `"sensor_profile": "gamer"` (opsiyonel)
  - `"sensor_config_path": "/path/to/config.json"` (opsiyonel)
- Music efekti durumunda:
  - `"gain": 0.0` (opsiyonel, 0=auto)

## Depolama

- **Profil dizini:** `~/.config/f87control/profiles/`
- **Profil dosyalari:** `<name>.json` (orn. `gaming.json`, `work.json`)
- **Son durum:** `~/.config/f87control/last.json`
- Dizin yoksa daemon otomatik olusturur (`mkdir -p`)
- Profil isimlerinde sadece alfanumerik, tire ve alt cizgi gecerli (`[a-zA-Z0-9_-]`)

## Daemon Davranisi

### Baslangic
1. `~/.config/f87control/last.json` var mi kontrol et
2. Varsa oku ve efekti uygula (cihaz bagliysa)
3. Cihaz bagli degilse, profili bellekte tut, baglanti gelince uygula

### Her Efekt Degisikliginde
- `SetEffect`, `SetSwEffect`, `SetMusicEffect`, `SetSensorEffect`, `SetSideLight`, `SetBatteryLight` cagrildiginda `last.json`'a aktif durumu yaz
- `Stop` ve `Off` durumlarinda da `last.json` guncellenir (effect_id=-1 veya 0)
- Yazim hatasi sessiz — profil yazma basarisiz olursa daemon calismaya devam eder

### SaveProfile(name)
1. Profil ismi validasyonu (`[a-zA-Z0-9_-]+`, bos degil, max 64 karakter)
2. Aktif efekt durumunu JSON'a serialize et
3. `~/.config/f87control/profiles/<name>.json` dosyasina yaz
4. Basarili: `true`, hata: D-Bus error

### LoadProfile(name)
1. `~/.config/f87control/profiles/<name>.json` dosyasini oku
2. JSON'dan efekt durumunu parse et
3. Efekti uygula (kategoriye gore `set_hw`, `set_sw`, `set_music`, `set_sensor`)
4. Yan isik ve batarya isik modunu uygula
5. `last.json` guncelle
6. `EffectChanged` sinyali yayinla

### DeleteProfile(name)
1. `~/.config/f87control/profiles/<name>.json` dosyasini sil
2. Dosya yoksa hata donme, sessiz basari

### ListProfiles()
1. `~/.config/f87control/profiles/*.json` glob
2. Dosya isimlerinden `.json` uzantisini cikar
3. String dizisi olarak don (`as`)

## D-Bus API Eklemeleri

### Yeni Metodlar

| Metod | Parametreler | Donus | Aciklama |
|-------|-------------|-------|----------|
| `SaveProfile` | `name: s` | `b` | Aktif durumu profil olarak kaydet |
| `LoadProfile` | `name: s` | `b` | Profili yukle ve uygula |
| `DeleteProfile` | `name: s` | `b` | Profili sil |
| `ListProfiles` | — | `as` | Profil isimlerini listele |
| `SetSideLight` | `mode: y` | `b` | Yan isik modunu ayarla (0-4) |
| `SetBatteryLight` | `mode: y` | `b` | Batarya isik modunu ayarla (0-4) |

### Property Eklemeleri

| Property | Tip | Aciklama |
|----------|-----|----------|
| `SideLight` | `y` | Mevcut yan isik modu (0-4) |
| `BatteryLight` | `y` | Mevcut batarya isik modu (0-4) |

## Effect Manager Degisiklikleri

`f87d_effect_manager_t` struct'ina eklenmesi gereken alanlar:

```c
uint8_t side_light;     /* 0-4 */
uint8_t battery_light;  /* 0-4 */
```

Yan/batarya isik degisiklikleri `effect_manager` uzerinden takip edilir, boylece profil kaydederken tum durum tek yerden okunur.

## Dosya Yapisi

```
daemon/src/
├── profile_manager.h    # Profil okuma/yazma API
├── profile_manager.c    # JSON serialize/deserialize, dizin yonetimi
```

Degistirilecek dosyalar:
- `daemon/src/effect_manager.h` — side/battery light alanlari
- `daemon/src/effect_manager.c` — set_side_light, set_battery_light fonksiyonlari
- `daemon/src/dbus_interface.c` — profil + side/battery D-Bus metodlari
- `daemon/src/dbus_interface.h` — (gerekirse)
- `daemon/src/main.c` — baslangicta last.json yukleme
- `daemon/CMakeLists.txt` — profile_manager.c, json-c bagimliligi
- `lib/include/f87/client.h` — profil + side/battery client fonksiyonlari
- `lib/src/client.c` — D-Bus proxy implementasyonu
- `cli/src/main.c` — profil CLI komutlari

## Bagimliliklar

- `json-c` — zaten lib'de sensor config icin kullaniliyor
- Daemon'da da `json-c` linklenecek

## CLI Komutlari

```
f87ctl profile save <name>        Aktif durumu profil olarak kaydet
f87ctl profile load <name>        Profili yukle
f87ctl profile delete <name>      Profili sil
f87ctl profile list               Profilleri listele
f87ctl sidelight <0-4>            Yan isik modu
f87ctl batterylight <0-4>         Batarya isik modu
```

## Hata Durumlari

- Gecersiz profil ismi: `org.f87.Error.InvalidName`
- Profil bulunamadi: `org.f87.Error.NotFound`
- JSON parse hatasi: `org.f87.Error.InvalidProfile`
- Dizin olusturulamadi: `org.f87.Error.IOError`

## Test

- Birim testi: `profile_manager` JSON serialize/deserialize, dosya okuma/yazma (gecici dizinde)
- Entegrasyon: daemon baslat, profil kaydet/yukle/listele/sil CLI ile
- Baslangic testi: daemon kapat, tekrar baslat, son efektin geri yuklenmesini dogrula

## Kapsam Disi

- GUI profil yonetim arayuzu (ayri task olarak ele alinabilir)
- Profil import/export
- Profil paylaşim
