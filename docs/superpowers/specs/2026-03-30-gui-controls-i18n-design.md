# GUI Kapsamlı Yeniden Tasarım + i18n

**Tarih:** 2026-03-30
**Kapsam:** `gui/src/` tüm dosyalar, yeni `gui/src/i18n.c`, `po/` dizini

## 1. Amaç

- Efektlerin aldığı parametrelere göre kontrol panelini uyarlamak (gereksiz slider/renk seçici göstermemek)
- Renk seçici kod tekrarını temizlemek ve CSS provider sızıntısını düzeltmek
- gettext tabanlı çoklu dil desteği eklemek (Türkçe + İngilizce, fallback: İngilizce)
- Rescan butonu eklemek (cihaz bağlantısı koptuğunda)
- Efekt seçiminde renk tutarlılığını sağlamak
- SV gradient performansını iyileştirmek (hue cache)
- Klavye view'ı responsive yapmak (aspect ratio koruyarak)
- Paint mode'a fill/clear butonları eklemek
- Keyboard shortcut'lar eklemek (Ctrl+S kaydet, Escape durdur)
- Sidebar'da efekt açıklamaları (tooltip)

## 2. Efekt Parametre Metadata Sistemi

Her efekt için hangi kontrollerin gösterileceğini belirleyen statik flag tablosu.

### 2.1 Flag Tanımları

```c
/* gui/src/controls.h veya ayrı gui/src/effect_meta.h */
typedef enum {
    F87_PARAM_BRIGHTNESS = (1 << 0),  /* Parlaklık slider (1-4) */
    F87_PARAM_SPEED      = (1 << 1),  /* Hız slider (0-4) */
    F87_PARAM_COLOR      = (1 << 2),  /* Renk seçici (sağ panel) */
    F87_PARAM_COLORFUL   = (1 << 3),  /* Renkli mod switch */
    F87_PARAM_AUDIO      = (1 << 4),  /* Kaynak dropdown + gain */
    F87_PARAM_PROFILE    = (1 << 5),  /* Sensor profil dropdown */
    F87_PARAM_PAINT      = (1 << 6),  /* Custom per-key paint mode */
} f87_param_flags;
```

### 2.2 Efekt → Parametre Eşleşme Tablosu

Kaynak koddan doğrulanmış gerçek parametre kullanımı:

#### Donanım Efektleri

| ID | Efekt | Flagler |
|----|-------|---------|
| 0 | Off | (hiçbiri) |
| 1 | Static | BRIGHTNESS \| COLOR |
| 2 | Breathing | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 3 | Wave | BRIGHTNESS \| SPEED |
| 4 | Spectrum | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 5 | Rain | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 7 | Ripple | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 8 | Starlight | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 10 | Snake | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 11 | Aurora | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 12 | Reactive | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 13 | Marquee | BRIGHTNESS \| SPEED \| COLOR \| COLORFUL |
| 15 | Circle | BRIGHTNESS \| SPEED |
| 16 | Rain Down | BRIGHTNESS \| SPEED |
| 17 | Center Ripple | BRIGHTNESS \| SPEED |
| 18 | Custom | BRIGHTNESS \| COLOR \| PAINT |

**Neden renk yok:** Wave, Circle, Rain Down, Center Ripple firmware'de rainbow modunda çalışır — `effects.c:47-51`'de renk gönderilmez.

#### Yazılım Efektleri

| ID | Efekt | Flagler | Neden |
|----|-------|---------|-------|
| 100 | Fire | BRIGHTNESS \| SPEED \| COLOR | `base_color` ile gradient |
| 101 | Matrix | BRIGHTNESS \| SPEED \| COLOR | `base_color` ile ton |
| 102 | Plasma | BRIGHTNESS \| SPEED | Kendi rainbow renkleri |
| 103 | Heatmap | BRIGHTNESS | Sıcaklık renk gradyanı, hız yok |
| 104 | Radar | BRIGHTNESS \| SPEED \| COLOR | `base_color` kullanır |
| 105 | Lightning | BRIGHTNESS \| SPEED \| COLOR | `base_color` kullanır |
| 110 | Explode | BRIGHTNESS \| SPEED | Random hue patlamaları |
| 111 | Ripple SW | BRIGHTNESS \| SPEED \| COLOR | `base_color` kullanır |
| 112 | Typewriter | BRIGHTNESS \| SPEED | Heat gradient (beyaz→kırmızı) |
| 113 | Life | BRIGHTNESS \| SPEED \| COLOR | `base_color` kullanır |
| 114 | KeyHeat | BRIGHTNESS \| SPEED | Heat gradient (mavi→kırmızı) |

#### Müzik Efektleri

| ID | Efekt | Flagler | Neden |
|----|-------|---------|-------|
| 200 | Spectrum | BRIGHTNESS \| SPEED \| AUDIO | Band renkleri sabit |
| 201 | Beat | BRIGHTNESS \| AUDIO | Rotating hue, hız yok |
| 202 | Energy | BRIGHTNESS \| SPEED \| COLOR \| AUDIO | `base_color` kullanır |
| 203 | VU | BRIGHTNESS \| SPEED \| AUDIO | Yeşil→sarı→kırmızı sabit |
| 204 | FreqMap | BRIGHTNESS \| SPEED \| AUDIO | Frekans renkleri sabit |

#### Sensor

| ID | Efekt | Flagler |
|----|-------|---------|
| 106-108 | developer/gamer/system | BRIGHTNESS \| PROFILE |

### 2.3 Veri Yapısı

```c
typedef struct {
    int effect_id;
    uint32_t flags;       /* f87_param_flags bitmask */
    const char *tag;      /* Başlıkta gösterilecek etiket: "rainbow", NULL, vb. */
} effect_meta_t;

/* Statik tablo — GUI'de lookup */
static const effect_meta_t effect_meta[] = {
    { 0,   0, NULL },
    { 1,   F87_PARAM_BRIGHTNESS | F87_PARAM_COLOR, NULL },
    { 2,   F87_PARAM_BRIGHTNESS | F87_PARAM_SPEED | F87_PARAM_COLOR | F87_PARAM_COLORFUL, NULL },
    { 3,   F87_PARAM_BRIGHTNESS | F87_PARAM_SPEED, "rainbow" },
    /* ... tüm efektler ... */
};

const effect_meta_t *effect_meta_lookup(int effect_id);
```

## 3. Kontrol Paneli Layout

### 3.1 Split Layout Kuralı

```
+----------------------------------------------+
| Efekt Adı          [tag: rainbow]            |
+----------------------------------------------+
| Sol Panel          | Ayırıcı | Sağ Panel     |
| (parametreler)     |   |     | (renk seçici) |
|                    |   |     |               |
| [Parlaklık] ----o  |   |     | [SV Gradient] |
| [Hız]       ----o  |   |     | [Hue Bar]     |
| [Renkli]    [OFF]  |   |     | #FF5000 [■]   |
|                    |   |     | [presetler]   |
+----------------------------------------------+
| [Kaydet]                        [Durdur]     |
+----------------------------------------------+
```

- **`HAS_COLOR` flag varsa:** Sol + ayırıcı + sağ panel (flex layout)
- **`HAS_COLOR` flag yoksa:** Sadece sol panel, full genişlik
- **`HAS_PAINT` flag varsa:** Sol panel + renk seçici + paint mode ipucu + keyboard paint etkin

### 3.2 Sol Panel İçeriği (flag'lere göre)

Sıralama (yukarıdan aşağı):
1. `HAS_BRIGHTNESS` → Parlaklık slider (1-4)
2. `HAS_SPEED` → Hız slider (0-4), preview'e bağlı
3. `HAS_COLORFUL` → "Renkli mod" switch
4. `HAS_AUDIO` → Kaynak dropdown + Auto Gain switch + Gain slider
5. `HAS_PROFILE` → Profil dropdown

### 3.3 Sağ Panel (Renk Seçici)

`HAS_COLOR` varsa gösterilir. Bileşenler:
1. SV gradient alanı (mevcut 120x120, `width: 100%` ile responsive)
2. Hue slider (rainbow bar)
3. Hex input + renk preview swatch (tek satır)
4. 12 preset renk swatch

### 3.4 Slider Etiketleri

- Tutarlı 70px genişlik
- Tam kelimeler: Parlaklık, Hız (gettext ile çevrilecek)
- Slider'ın sağında mevcut değer (küçük font)

### 3.5 Efekt Başlık Satırı

```
Efekt Adı  [tag]  [reaktif etiketi]
```

- `tag`: metadata'dan, küçük font, soluk: "rainbow", "müzik", "sistem izleme"
- Reaktif: `f87_preview_is_reactive()` true ise "tuş basmaya duyarlı" etiketi

## 4. Kod Temizliği

### 4.1 Renk Seçici Fonksiyonu

`build_split_layout()` ve `build_custom_controls()` arasındaki tekrar eden renk seçici kodu tek bir fonksiyona çıkarılır:

```c
/* Sağ panel: SV + hue + hex + presetler. GtkBox* döner. */
static GtkWidget *create_color_picker(F87Controls *ctrl);
```

### 4.2 CSS Provider Sızıntısı Düzeltmesi

`sync_color_from_hsv()` her çağrıda yeni provider yaratıp ekliyordu. Düzeltme:

```c
struct _F87Controls {
    /* ... */
    GtkCssProvider *swatch_provider;  /* Tek instance, tekrar kullanılır */
};
```

`sync_color_from_hsv()` içinde:
```c
if (!ctrl->swatch_provider) {
    ctrl->swatch_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(...);
}
gtk_css_provider_load_from_string(ctrl->swatch_provider, css);
```

### 4.3 Preset CSS Tek Seferlik Yükleme

Preset renk CSS'i statik — her `build_*` çağrısında yeniden yaratmaya gerek yok. Uygulama başlangıcında bir kez yüklenir:

```c
static gboolean preset_css_loaded = FALSE;

static void ensure_preset_css(void) {
    if (preset_css_loaded) return;
    /* CSS yarat ve yükle */
    preset_css_loaded = TRUE;
}
```

### 4.4 Birleşik Builder

4 ayrı `build_hw_controls()`, `build_sw_controls()`, `build_music_controls()`, `build_sensor_controls()` yerine tek fonksiyon:

```c
static void build_controls_for_effect(F87Controls *ctrl) {
    const effect_meta_t *meta = effect_meta_lookup(ctrl->effect_id);
    uint32_t flags = meta->flags;

    GtkBox *left = /* sol panel */;
    if (flags & F87_PARAM_BRIGHTNESS) /* slider ekle */;
    if (flags & F87_PARAM_SPEED)      /* slider ekle */;
    if (flags & F87_PARAM_COLORFUL)   /* switch ekle */;
    if (flags & F87_PARAM_AUDIO)      /* dropdown + gain ekle */;
    if (flags & F87_PARAM_PROFILE)    /* dropdown ekle */;

    if (flags & F87_PARAM_COLOR) {
        /* ayırıcı + sağ panel (renk seçici) */
        GtkWidget *picker = create_color_picker(ctrl);
        /* split layout'a ekle */
    }

    if (flags & F87_PARAM_PAINT) {
        /* keyboard paint mode etkinleştir */
    }
}
```

## 5. i18n — gettext Entegrasyonu

### 5.1 Altyapı

- **Çeviri sistemi:** GNU gettext
- **Diller:** İngilizce (fallback), Türkçe
- **Varsayılan:** Sistem locale'i (`LANG`, `LC_MESSAGES`)
- **Domain:** `f87control`
- **Locale dizini:** `${CMAKE_INSTALL_PREFIX}/share/locale`

### 5.2 Dosya Yapısı

```
po/
├── POTFILES.in          # Çevrilecek kaynak dosya listesi
├── f87control.pot       # Template (xgettext çıktısı)
├── tr.po                # Türkçe çeviri
└── en.po                # İngilizce (boş/identity — fallback)
gui/src/
├── i18n.h               # _() makrosu, init fonksiyonu
```

### 5.3 i18n Header

```c
/* gui/src/i18n.h */
#ifndef F87_I18N_H
#define F87_I18N_H

#include <libintl.h>
#include <locale.h>

#define _(str) gettext(str)
#define N_(str) str  /* xgettext marker, çeviri yapmaz */

void f87_i18n_init(void);

#endif
```

### 5.4 Başlatma

```c
/* gui/src/i18n.c */
#include "i18n.h"

#define GETTEXT_PACKAGE "f87control"

void f87_i18n_init(void) {
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
}
```

`LOCALEDIR` CMake'den tanımlanır:
```cmake
add_definitions(-DLOCALEDIR="${CMAKE_INSTALL_PREFIX}/share/locale")
```

### 5.5 Kullanım

Tüm kullanıcı görünür stringler `_()` ile sarılır:

```c
/* Önce */
gtk_label_new("Parlaklik");
gtk_button_new_with_label("Kaydet");

/* Sonra */
gtk_label_new(_("Brightness"));  /* Kaynak İngilizce, Türkçe çeviri po'da */
gtk_button_new_with_label(_("Save"));
```

### 5.6 Çevrilecek Stringler

**Kaynak dili İngilizce** — `.c` dosyalarındaki stringler İngilizce yazılır, `tr.po` Türkçe çeviriyi içerir.

| Kaynak (İngilizce) | Türkçe Çeviri |
|---------------------|---------------|
| Brightness | Parlaklık |
| Speed | Hız |
| Colorful mode | Renkli mod |
| Source | Kaynak |
| System Audio | Sistem Sesi |
| Microphone | Mikrofon |
| Auto Gain | Otomatik Kazanç |
| Gain | Kazanç |
| Profile | Profil |
| Save | Kaydet |
| Stop | Durdur |
| Saving... | Kaydediliyor... |
| Waiting | Bekleniyor |
| Starting... | Başlatılıyor... |
| Connected (daemon) | Bağlı (daemon) |
| Keyboard not found | Klavye bulunamadı |
| Scan failed | Tarama başarısız |
| Connection lost — could not reconnect | Klavye bağlantısı koptu — yeniden bağlanamadı |
| Stop failed | Durdurma hatası |
| Per-key colors failed | Per-key renkler gönderilemedi |
| Custom running | Custom çalışıyor |
| %s running | %s çalışıyor |
| Selected: %s | Seçilen: %s |
| Select effect | Efekt seçiniz |
| Select color, paint by clicking keys | Renk seç, tuşlarını tıklayarak boya |
| key-press reactive | tuş basmaya duyarlı |
| Hardware Effects | Donanım Efektleri |
| Software Effects | Yazılımsal Efektler |
| Music | Müzik |
| Sensor | Sensör |
| mixed colors | karışık renkler |
| rainbow | gökkuşağı |
| music | müzik |
| system monitor | sistem izleme |
| Daemon connection failed | Daemon'a bağlanılamadı |
| Could not get daemon status | Daemon durumu alınamadı |

### 5.7 CMake Entegrasyonu

```cmake
find_package(Intl REQUIRED)

# .po → .mo derleme
set(LANGUAGES tr)
foreach(LANG ${LANGUAGES})
    set(PO_FILE ${CMAKE_SOURCE_DIR}/po/${LANG}.po)
    set(MO_DIR ${CMAKE_BINARY_DIR}/locale/${LANG}/LC_MESSAGES)
    set(MO_FILE ${MO_DIR}/f87control.mo)
    file(MAKE_DIRECTORY ${MO_DIR})
    add_custom_command(
        OUTPUT ${MO_FILE}
        COMMAND msgfmt -o ${MO_FILE} ${PO_FILE}
        DEPENDS ${PO_FILE}
    )
    list(APPEND MO_FILES ${MO_FILE})
    install(FILES ${MO_FILE}
            DESTINATION share/locale/${LANG}/LC_MESSAGES)
endforeach()
add_custom_target(translations ALL DEPENDS ${MO_FILES})
```

### 5.8 pot Dosyası Üretimi

```bash
xgettext --keyword=_ --keyword=N_ \
    --from-code=UTF-8 \
    -o po/f87control.pot \
    gui/src/*.c
```

## 6. Rescan Butonu

### 6.1 Konum

HeaderBar'a bir "Yeniden Tara" butonu eklenir. `AdwHeaderBar`'ın sağ tarafına (end) simge buton:

```c
GtkButton *rescan_btn = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
gtk_widget_set_tooltip_text(GTK_WIDGET(rescan_btn), _("Rescan for keyboard"));
adw_header_bar_pack_end(header, GTK_WIDGET(rescan_btn));
g_signal_connect(rescan_btn, "clicked", G_CALLBACK(on_rescan_clicked), self);
```

### 6.2 Davranış

- Tıklanınca `f87_app_state_rescan()` çağrılır
- Başarılıysa status "Bağlı (daemon)" olur
- Başarısızsa status kırmızı kalır
- Rescan sırasında buton devre dışı (2s cooldown — aynı USB koruma prensibi)

## 7. Efekt Seçiminde Renk Tutarlılığı

### 7.1 Sorun

`on_effect_selected()` (window.c:47) hardcoded renkler set ediyor ama controls tarafında `selected_color` önceki efektten kalıyor — preview ile kontrol arasında uyumsuzluk.

### 7.2 Çözüm

Efekt değiştiğinde kontrol panelindeki mevcut renk keyboard preview'a da yansıtılır:

```c
static void on_effect_selected(...) {
    f87_controls_set_effect(self->controls, category, effect_name, effect_id);

    /* Kontrol panelinin seçili rengini preview'a uygula */
    const uint8_t *c = f87_controls_get_color(self->controls);
    const effect_meta_t *meta = effect_meta_lookup(effect_id);

    if (meta->flags & F87_PARAM_COLOR) {
        f87_keyboard_view_set_color(self->keyboard, c[0], c[1], c[2]);
    } else if (meta->flags & F87_PARAM_PAINT) {
        f87_keyboard_view_clear(self->keyboard);
    } else {
        /* Renk almayan efekt — preview animasyon renklerini kullanacak */
    }
}
```

`f87_controls_get_color()` yeni public fonksiyon — `ctrl->selected_color` döner.

## 8. SV Gradient Performansı

### 8.1 Sorun

`sv_draw()` her çağrıda 120×120 = 14.400 pixel için HSV→RGB hesaplıyor. Hue değişmediği sürece gradient aynı — gereksiz hesaplama.

### 8.2 Çözüm — Hue Cache

```c
struct _F87Controls {
    /* ... */
    cairo_surface_t *sv_cache;    /* Cached SV gradient surface */
    float sv_cache_hue;           /* Hangi hue için cache'lendi */
};
```

```c
static void sv_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
    F87Controls *ctrl = data;

    /* Hue değişmediyse cache'i kullan */
    if (!ctrl->sv_cache || ctrl->sv_cache_hue != ctrl->hue) {
        if (ctrl->sv_cache) cairo_surface_destroy(ctrl->sv_cache);
        ctrl->sv_cache = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
        /* ... pixel hesaplama ... */
        ctrl->sv_cache_hue = ctrl->hue;
    }

    cairo_set_source_surface(cr, ctrl->sv_cache, 0, 0);
    cairo_paint(cr);

    /* Selector dairesi her zaman yeniden çizilir */
    /* ... */
}
```

Cache, hue değiştiğinde veya widget resize olduğunda invalidate edilir. `f87_controls` free'de `cairo_surface_destroy`.

## 9. Responsive Klavye View

### 9.1 Sorun

Klavye 690×255 sabit boyut, pencere büyüdüğünde küçük kalıyor.

### 9.2 Çözüm

`hexpand=TRUE` yapılır, aspect ratio `689:255 ≈ 2.7:1` korunur. `GtkDrawingArea` zaten `draw_func`'a gerçek `width/height` geçiriyor — `ref_to_widget()` doğru ölçekleme yapıyor.

```c
/* keyboard_view.c init'ten sabit boyut kısıtlamasını kaldır */
gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
gtk_widget_set_vexpand(GTK_WIDGET(self), FALSE);
/* Minimum boyut koru, ama büyümeye izin ver */
gtk_widget_set_size_request(GTK_WIDGET(self), 500, -1);  /* min 500px genişlik */
gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self), 690);
gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self), 255);
```

Yükseklik genişliğe göre otomatik hesaplanır (`content_width/content_height` oranı GTK tarafından korunur). `draw_func` zaten `width` ve `height` parametrelerini alıp `ref_to_widget()` ile ölçekleme yapıyor — ek değişiklik gerekmez.

## 10. Paint Mode İyileştirmeleri

### 10.1 Fill ve Clear Butonları

Custom efekt seçildiğinde kontrol panelinin sol tarafına iki buton eklenir:

```c
if (flags & F87_PARAM_PAINT) {
    GtkBox *paint_btns = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));

    GtkButton *fill_btn = GTK_BUTTON(gtk_button_new_with_label(_("Fill All")));
    g_signal_connect(fill_btn, "clicked", G_CALLBACK(on_fill_clicked), ctrl);

    GtkButton *clear_btn = GTK_BUTTON(gtk_button_new_with_label(_("Clear")));
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), ctrl);

    gtk_box_append(paint_btns, GTK_WIDGET(fill_btn));
    gtk_box_append(paint_btns, GTK_WIDGET(clear_btn));
    gtk_box_append(left, GTK_WIDGET(paint_btns));
}
```

- **Fill All:** Tüm 88 tuşu seçili renkle doldurur (`f87_keyboard_view_set_color()`)
- **Clear:** Tüm tuşları siyaha döndürür (`f87_keyboard_view_clear()`)

### 10.2 Drag-to-Paint

Mevcut paint mode sadece tek tıklama destekliyor. Sürükleyerek boyama eklenir:

```c
/* keyboard_view.c — motion handler eklenir */
static void on_paint_motion(GtkEventControllerMotion *motion,
                             double x, double y, gpointer data) {
    F87KeyboardView *self = data;
    if (!self->paint_mode || !self->paint_dragging) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self));
    int h = gtk_widget_get_height(GTK_WIDGET(self));
    int key_id = hit_test((int)x, (int)y, w, h);
    if (key_id >= 0 && key_id != self->last_painted_key) {
        self->last_painted_key = key_id;
        if (self->paint_cb)
            self->paint_cb(key_id, self->paint_data);
    }
}
```

`paint_dragging` flag'i `GtkGestureClick`'in `pressed`/`released` event'leriyle kontrol edilir. `last_painted_key` aynı tuşun tekrar boyanmasını önler.

Struct'a eklenen alanlar:
```c
struct _F87KeyboardView {
    /* ... mevcut ... */
    gboolean paint_dragging;
    int last_painted_key;
};
```

## 11. Keyboard Shortcut'lar

### 11.1 Tanımlanan Kısayollar

| Kısayol | Aksiyon |
|---------|---------|
| `Ctrl+S` | Kaydet (on_send_clicked tetikler) |
| `Escape` | Durdur (on_stop_clicked tetikler) |

### 11.2 Uygulama

`window.c` init'te `GtkShortcutController` eklenir:

```c
/* Ctrl+S → Kaydet */
GtkShortcut *save_shortcut = gtk_shortcut_new(
    gtk_shortcut_trigger_parse_string("<Control>s"),
    gtk_callback_action_new(on_save_shortcut, self, NULL));

/* Escape → Durdur */
GtkShortcut *stop_shortcut = gtk_shortcut_new(
    gtk_shortcut_trigger_parse_string("Escape"),
    gtk_callback_action_new(on_stop_shortcut, self, NULL));

GtkShortcutController *sc = gtk_shortcut_controller_new();
gtk_shortcut_controller_add_shortcut(sc, save_shortcut);
gtk_shortcut_controller_add_shortcut(sc, stop_shortcut);
gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(sc));
```

Callback fonksiyonları `F87Controls`'deki send/stop butonlarını tetikler. `f87_controls_send()` ve `f87_controls_stop()` yeni public API olarak expose edilir.

## 12. Sidebar Efekt Açıklamaları (Tooltip)

### 12.1 Yaklaşım

Her efekt satırına `gtk_widget_set_tooltip_text()` ile kısa açıklama eklenir. Açıklamalar gettext ile çevrilir.

### 12.2 Veri

`sidebar.c`'deki `EffectEntry` struct'ına `tooltip` alanı eklenir:

```c
typedef struct {
    const char *name;
    int id;
    const char *tooltip;  /* N_() ile işaretlenir, _() ile çevrilir */
} EffectEntry;

static const EffectEntry hw[] = {
    {"Off",       0, N_("Turn off all LEDs")},
    {"Static",    1, N_("Single solid color")},
    {"Breathing", 2, N_("Pulsing color fade")},
    {"Wave",      3, N_("Rainbow wave across keyboard")},
    {"Spectrum",  4, N_("Color spread from key presses")},
    {"Rain",      5, N_("Falling rain drops")},
    {"Ripple",    7, N_("Ripple waves from key presses")},
    {"Starlight", 8, N_("Random twinkling stars")},
    {"Snake",    10, N_("Snake trail moving across keys")},
    {"Aurora",   11, N_("Northern lights shimmer")},
    {"Reactive", 12, N_("Single key lights up on press")},
    {"Marquee",  13, N_("Scrolling light marquee")},
    {"Circle",   15, N_("Circular rainbow pattern")},
    {"Rain Down",16, N_("Top to bottom wave")},
    {"Center Ripple",17, N_("Ripple from center outward")},
    {"Custom",   18, N_("Paint each key individually")},
    {NULL, 0, NULL}
};
```

`make_effect_row()` içinde tooltip uygulanır:
```c
if (tooltip)
    gtk_widget_set_tooltip_text(GTK_WIDGET(row), _(tooltip));
```

## 13. Kapsam Dışı

- Sidebar efekt isimleri firmware'den gelir — çevrilmez (Off, Static, Wave, vb. uluslararası). Kategori başlıkları (Donanım Efektleri, Yazılımsal Efektler, vb.) çevrilir.
- CLI (`f87ctl`) i18n kapsamı dışında — yalnızca GUI
- Profil yönetimi GUI'si (ayrı faz)
- Side light / battery light GUI kontrolleri (ayrı faz)
- Battery / wireless bilgisi gösterimi (ayrı faz)
- Sensor canlı veri gösterimi (ayrı faz)
- Loading feedback değişikliği — 2s cooldown bilinçli tasarım kararı (USB reset koruması)

## 14. Etkilenen Dosyalar

| Dosya | Değişiklik |
|-------|-----------|
| `gui/src/i18n.h` | **Yeni** — gettext makroları |
| `gui/src/i18n.c` | **Yeni** — locale init |
| `gui/src/controls.c` | **Büyük refactor** — metadata tablosu, birleşik builder, renk seçici fonksiyonu, CSS fix, SV cache, paint butonları, `_()` |
| `gui/src/controls.h` | `effect_meta_t`, `f87_param_flags`, `f87_controls_get_color()`, `f87_controls_send()`, `f87_controls_stop()` |
| `gui/src/sidebar.c` | Tooltip'ler, kategori başlıkları `_()` ile |
| `gui/src/window.c` | Rescan butonu, shortcut'lar, renk tutarlılığı, status mesajları `_()` ile |
| `gui/src/window.h` | Rescan buton pointer |
| `gui/src/keyboard_view.c` | Responsive layout, drag-to-paint, `paint_dragging`/`last_painted_key` |
| `gui/src/keyboard_view.h` | Değişiklik yok (iç struct) |
| `gui/src/app_state.c` | Tüm `status_text` stringleri `_()` ile |
| `gui/src/main.c` | `f87_i18n_init()` çağrısı |
| `gui/CMakeLists.txt` | Intl bağımlılığı, mo derleme, LOCALEDIR, i18n.c ekleme |
| `gui/resources/style.css` | Değişiklik yok |
| `po/POTFILES.in` | **Yeni** |
| `po/f87control.pot` | **Yeni** — xgettext çıktısı |
| `po/tr.po` | **Yeni** — Türkçe çeviri |
