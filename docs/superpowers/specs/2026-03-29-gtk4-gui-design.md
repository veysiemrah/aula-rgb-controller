# GTK4 GUI — Tasarim Dokumani

**Tarih:** 2026-03-29
**Durum:** Onaylandi

## Ozet

F87Control GTK4 GUI uygulamasi. Sidebar + Onizleme layoutu, tum efektler (HW + SW + muzik + sensor), renk paleti + ozel renk, statik onizleme, koyu tema. libf87 API'sini dogrudan kullanir.

## Mimari

```
f87control (GTK4 + libadwaita)
  |
  +-- Sidebar: kategori bazli efekt listesi
  +-- Klavye Onizleme: 88 tus TKL layout, statik snapshot
  +-- Kontrol Paneli: efekte gore degisen kontroller
  +-- Aksiyon: Klavyeye Gonder / Durdur
  |
  v
libf87 API (f87_set_effect, f87_anim_start/stop)
```

- GTK4 + libadwaita — modern Linux masaustu, koyu tema varsayilan
- GUI dogrudan libf87 cagrilari yapar (daemon yok)
- Animasyon thread libf87 icinde, GUI thread ayri (bloklanmaz)
- Klavye onizleme statik — "Klavyeye Gonder" butonu ile gonderilir

## Layout: Sidebar + Onizleme

Sol tarafta efekt listesi, sagda klavye onizleme ve kontroller.

### Sidebar Kategorileri

```
Donanim Efektleri
  Off, Static, Breathing, Wave, Spectrum, Rain, Ripple, Starlight,
  Snake, Aurora, Reactive, Marquee, Circle, Rain Down, Center Ripple, Custom

Yazilimsal Efektler
  Fire, Matrix, Plasma, Radar, Lightning, Explode, Ripple SW,
  Typewriter, Life, KeyHeat

Muzik
  Spectrum, Beat, Energy, VU Meter, FreqMap

Sensor
  Developer, Gamer, System, Ozel Config...
```

- GtkListBox ile kategoriler acilip kapanabilir (expander row)
- Secilen efekt vurgulanir, sag tarafta kontrolleri degisir
- Calisan efekt yaninda yesil indicator gosterilir

## Kontrol Paneli

Her efekt kategorisine gore farkli kontroller:

### Donanim Efektleri
- Parlaklik slider (1-4)
- Hiz slider (0-4)
- Renk paleti (16-20 hazir renk) + "Ozel Renk..." butonu (GtkColorChooser)
- Colorful toggle switch
- Yan isik dropdown (Off/Rainbow/Breath Mix/Static Red/Breath Red)

### Yazilimsal Efektler
- Parlaklik slider (1-4)
- Hiz slider (0-4)
- Renk paleti + ozel renk butonu
- Efekte ozel: yayilma mesafesi (explode), esik hizi (keyheat)

### Muzik
- Ses kaynagi dropdown (Sistem Sesi / Mikrofon)
- Gain slider (0=auto, 1-10 manual) + "Auto" toggle
- Parlaklik slider (1-4)

### Sensor
- Profil dropdown (Developer / Gamer / System)
- "Ozel Config Yukle..." butonu (file chooser)
- Parlaklik slider (1-4)

### Ortak
- [Klavyeye Gonder] butonu
- [Durdur] butonu
- Durum cubugu: "Fire efekti calisiyor — 30fps" veya "Bekleniyor"

## Renk Secimi

- Ust kisim: 16-20 hazir renk kutucugu (hizli secim)
- Alt kisim: "Ozel Renk..." butonu — GtkColorChooser dialog acar (HSV/RGB)

## Klavye Onizleme

- 88 tuslu TKL layout, f87_key_layout[] verisinden pozisyonlar
- Her tus bir kucuk dikdortgen, secilen efektin rengini gosterir
- Statik snapshot — efekt secildiginde veya parametreler degistiginde guncellenir
- Per-key modlarda (custom, keyheat) tuslara tiklanarak renk atanabilir

## Dosya Yapisi

```
gui/
  CMakeLists.txt
  src/
    main.c              — GtkApplication, pencere olusturma
    window.h / window.c — Ana pencere, sidebar + main area duzeni
    sidebar.h / sidebar.c — Efekt listesi, kategori expander
    controls.h / controls.c — Kontrol paneli, efekte gore widget degisimi
    keyboard_view.h / keyboard_view.c — 88 tus klavye onizleme widget
    app_state.h / app_state.c — Uygulama durumu, libf87 baglantisi
  resources/
    f87control.gresource.xml
    style.css           — Koyu tema ozellestirmeleri
```

## Build

```bash
sudo apt install libgtk-4-dev libadwaita-1-dev
mkdir build && cd build && cmake .. -DBUILD_GUI=ON && make
```

```cmake
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ADW REQUIRED libadwaita-1)
```

## Uygulama Akisi

1. main.c -> GtkApplication olustur, window.c ile ana pencere ac
2. sidebar.c -> efekt listesini libf87'den doldur
3. Kullanici efekt secer -> controls.c kontrol panelini gunceller
4. Kullanici parametreleri ayarlar -> keyboard_view.c statik onizleme gunceller
5. "Klavyeye Gonder" -> app_state.c libf87 API cagirir
6. "Durdur" -> app_state.c f87_anim_stop cagirir

## Hata Yonetimi

| Durum | Davranis |
|-------|----------|
| Klavye bagli degil | Uygulama acilir, durum cubugunda "Klavye bulunamadi" uyarisi, "Yeniden Tara" butonu |
| Klavye baglantisi koptu | Efekt durur, durum cubugunda uyari |
| PulseAudio yok | Muzik kategorisi devre disi (gri), tooltip ile aciklama |
| Sensor bulunamadi | Ilgili mapping atlanir, digerleri calisir |
| json-c yok | Sensor kategorisi devre disi |
| USB gonderim hatasi | Durum cubugunda "USB hatasi — tekrar deneniyor..." (3 retry) |
| Direct mode baslatma hatasi | Durum cubugunda "Direct mode baslatilamadi", buton tekrar aktif |
| Efekt zaten calisiyor | Once mevcut durdurulur, sonra yeni baslatilir |
| Timeout (klavye yanit vermiyor) | Durum cubugunda "Klavye yanit vermiyor — baglantiyi kontrol edin" |

Hata mesajlari durum cubugunda 5 saniye gosterilir. Kritik hatalar (klavye koptu) kalici kalir.

## Test

- Build testi: cmake -DBUILD_GUI=ON && make
- Manuel test: her kategoriden efekt sec, parametre degistir, klavyeye gonder, durdur
- Klavyesiz test: uygulama acilmali, uyari gostermeli, cokmemeli
