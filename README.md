# Zephyr Async UART Altyapısı

Bu depo, **Zephyr RTOS** üzerinde çalışan, yüksek verimli ve modüler bir **asenkron UART haberleşme** altyapısı sağlar. 
Katmanlı mimari sayesinde Zephyr’in **UART Async API**’sini destekleyen **UART’ı olan herhangi bir cihaz/kart** ile kullanılabilir.

> Örnek kullanım `app/main/src/main.c` içinde verilmiştir.

---

## İçindekiler

- [Özellikler](#özellikler)
- [Klasör Yapısı](#klasör-yapısı)
- [Kconfig Seçenekleri](#kconfig-seçenekleri)
- [UART Yapılandırması (`uart_cfg.h`)](#uart-yapılandırması-uart_cfgh)
- [Çerçeve (Frame) ve Paketleme](#çerçeve-frame-ve-paketleme)
- [Kullanım Örneği (RX Callback + Gönderim)](#kullanım-örneği-rx-callback--gönderim)
- [Derleme ve Yükleme](#derleme-ve-yükleme)
  - [Yöntem 1: west](#yöntem-1-west)
  - [Yöntem 2: PowerShell betiği (`scripts/bulid.ps1`)](#yöntem-2-powershell-betiği-scriptsbulidps1)
- [Donanım Uyumluluğu ve Port Etme](#donanım-uyumluluğu-ve-port-etme)
- [ASYNC UART TEST](#test)
- [Nucleo F070RB Notları](#nucleo-f070rb-notları)
- [Lisans](#lisans)

---

## Özellikler

- **Asenkron RX/TX**: Zephyr’in `CONFIG_UART_ASYNC_API` sürücüsüyle çalışır; ISR hafif, ağır işler thread tarafında.
- **Çift buffer ve ring buffer**: ISR’de gelen baytlar `ring_buffer`’a alınır, işleme `k_work` ile yapılır.
- **Framer + CRC16-CCITT**: SYNC/LEN/DATA/CRC formatında çerçeveleme. Veri bütünlüğü için CRC-16 (init `0xFFFF`).
- **Büyük veri aktarımı**: 7 baytlık **segment header** ile parçalı gönderim (`SEG_HDR_SIZE=7`).
- **Kolay API**: 
  - `uart_io_init()`
  - `uart_io_register_rx_cb()`
  - `uart_io_send_frame()`
  - `uart_io_send_buffer()`
  - `uart_io_send_larg()` *(büyük aktarım için; fonksiyon adı dosyada bu şekilde tanımlı)*
- **Logger entegrasyonu**: Geliştirici modu ve `file:line` ekleme seçenekleri.

---

## Klasör Yapısı

```
app/
 ├─ main/                     # Örnek uygulama (main.c, sys_init.c)
 ├─ peripherals/
 │   └─ uart/                 # UART katmanı
 │       ├─ data/             # Framer, segment header ve yardımcılar
 │       ├─ include/          # Public header'lar (API ve konfig)
 │       └─ src/              # Implementasyon (UART Async handler vb.)
 └─ utils/
     └─ log/                  # logger.h (APP_LOG_* makroları)
boards/
 └─ nucleo_f070rb.overlay     # UART pin/dma eşlemesi ve alias
scripts/
 └─ bulid.ps1                 # PowerShell build betiği (adı "bulid.ps1")
```

---

## Kconfig Seçenekleri

Projeye özgü semboller `Kconfig` içinde tanımlıdır:

| Sembol                     | Tip   | Varsayılan | Açıklama                                      |
|---------------------------|-------|------------|-----------------------------------------------|
| `CONFIG_APP_DEV_BUILD`    | bool  | –          | Geliştirici günlüğü (log seviyesi **DBG**).  |
| `CONFIG_APP_LOG_WITH_FILELINE` | bool | –     | Log çıktısına `dosya:Satır` bilgisini ekler. |
| `CONFIG_CUSTOM_UART_ENABLE`| bool | `y`        | UART özelleştirmelerini etkinleştirir.        |
| `CONFIG_CUSTOM_UART_RX_STACK_SIZE` | int | `64` | UART RX iş parçacığı/yığın boyutu ayarı (*not: `depends on BUZZER_ENABLE` ifadesi Kconfig’te mevcut — gerekmiyorsa kaldırılabilir/uyarlanabilir*). |

> `prj.conf` örneği zaten depo içinde mevcut ve aşağıdaki gibi temel ayarları açar:
>
> ```ini
> # Günlükleme
> CONFIG_LOG=y
> CONFIG_LOG_DEFAULT_LEVEL=3
> CONFIG_APP_DEV_BUILD=y
> CONFIG_APP_LOG_WITH_FILELINE=y
>
> #Custom UART CONFIG
> CONFIG_CUSTOM_UART_ENABLE=y
> CONFIG_CUSTOM_UART_RX_STACK_SIZE=64
> CONFIG_CUSTOM_UART_RX_CHUNK_SIZE=64
> CONFIG_CUSTOM_UART_SYNC_BYTE=0xAA
> 
> # DMA ve UART Async
> CONFIG_DMA=y
> CONFIG_UART_ASYNC_API=y
> 
> # Stack boyutları (örnek)
> CONFIG_MAIN_STACK_SIZE=1024
> CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=768
> CONFIG_ISR_STACK_SIZE=512
> CONFIG_IDLE_STACK_SIZE=256
> ```

---

## UART Yapılandırması (`uart_cfg.h`)

Aşağıdaki makrolar **varsayılanlarıyla** gelir. İhtiyacınıza göre `uart_cfg.h` içinde değiştirebilir veya derleme bayrağıyla (`-D...`) geçersiz kılabilirsiniz.

| Makro                      | Varsayılan                              | Anlamı |
|---------------------------|------------------------------------------|--------|
| `UART_MAX_PACKET_SIZE`    | `64`                                     | Bir **frame** içindeki **payload** üst sınırı (LEN alanının değeri). Segment header kullanılıyorsa `LEN = header + parça` olarak hesaplanır. |
| `UART_RX_CHUNK_LEN`       | `64`                                     | DMA/Async RX **çift buffer** boyutu (ping–pong). |
| `UART_RB_SZ`              | `(UART_RX_CHUNK_LEN * 4)`                | ISR sonrası veri için `ring_buffer` kapasitesi. |
| `UART_MSGQ_DEPTH`         | `4`                                      | RX’te çözümlenen karelerin kuyruk derinliği. |
| `UART_SYNC_BYTE`          | `0xAA`                                   | Çerçeve başlangıç baytı (**SYNC**). |
| `UART_CRC_INT`            | `0xFFFF`                                 | CRC-16/CCITT başlangıç değeri. |
| `SEG_TYP_DATA`            | `0x01`                                   | Segment türü (**DATA**). |
| `SEG_HDR_SIZE`            | `7`                                      | Segment başlığı boyutu (typ,xid,total,offset,clen). |
| `PAYLOAD_MAX`             | `UART_MAX_PACKET_SIZE - SEG_HDR_SIZE`    | Segmentli aktarımda tek karede taşınabilecek azami veri. |
| `FRAME_OVERHEAD_BYTES`    | `1(SYNC) + 1(LEN) + 2(CRC) = 4`          | Çerçeve üstverisi. |
| `FRAME_MAX_TOTAL`         | `FRAME_OVERHEAD_BYTES + UART_MAX_PACKET_SIZE` | Bir çerçevenin toplam üst sınırı. |

> **Baudrate / pin / DMA** yapılandırması **device tree overlay** üzerinden yapılır (bkz. `boards/nucleo_f070rb.overlay`). Başka karta port ederken kendi UART düğümünüzü ve DMA kanallarınızı tanımlayın.

---

## Çerçeve (Frame) ve Paketleme

**Temel çerçeve formatı:**

> CRC ayrıntıları için `app/peripherals/uart/include/crc16_ccitt.h` dosyasına bakınız


```
+--------+-------+-------------+---------+
| SYNC   | LEN   |   DATA(...) | CRC16   |
+--------+-------+-------------+---------+
 1 byte   1 byte   LEN bytes     2 bytes (big-endian)
```

- `SYNC` = `0xAA`
- `LEN`   = izleyen **DATA** uzunluğu (byte) — **segment header dahil**.
- `CRC16` = **CRC-16/CCITT** (init `0xFFFF`), **LEN** ve **DATA** üzerine hesaplanır (big‑endian ile gönderilir).

**Büyük veri aktarımı (segment header):** DATA kısmı aşağıdaki **7 baytlık** başlıkla başlar:

```
+-----+-----+--------+--------+------+
| typ | xid | total  | offset | clen |
+-----+-----+--------+--------+------+
 1B    1B     2B BE    2B BE    1B
```

- `typ`  = `SEG_TYP_DATA (0x01)`  
- `xid`  = aktarım kimliği (uygulama seviyesinde takip için)  
- `total`= toplam veri uzunluğu (bayt)  
- `offset`= bu çerçevedeki parçanın **orijinal verideki** başlangıç ofseti  
- `clen` = bu çerçevede taşınan parça uzunluğu  

`uart_io_send_larg()` (dosyada bu isimle tanımlı) fonksiyonu, büyük bir buffer’ı otomatik olarak **birden çok frame**’e böler ve her frame’in DATA’sına bu header’ı ekler.

---

## Kullanım Örneği (RX Callback + Gönderim)

**RX Callback** uygulama tarafında kaydedilir; callback **thread** bağlamında çağrılır (ISR değil), bu yüzden burada ağır iş yapılabilir. Callback, `void *ctx` içinde bir frame’i alır. `framer.h` içindeki tipi dahil edebilirsiniz veya kendi veri yapınıza göre özelleştirebilirsiniz. 

```c
#include "uart_io.h"
#include "framer.h"  // frame_rx_packet_t için

static void uart_rx_cb(void *ctx)
{
    if (!ctx) return;

    frame_rx_packet_t *pkt = (frame_rx_packet_t *)ctx;
    LOG_INF("RX len=%u", pkt->len);
    // pkt->data[0..len-1] işlenebilir
}

int main(void)
{
    int rc = uart_io_init();
    if (rc) {
        LOG_ERR("UART init failed (%d)", rc);
        return rc;
    }

    uart_io_register_rx_cb(uart_rx_cb);

    const char *msg = "Hello UART!";
    uart_io_send_frame((const uint8_t *)msg, (uint8_t)strlen(msg), K_MSEC(100));
    return 0;
}
```

> Projedeki örnek `main.c` içinde benzer bir kullanım gösterilmektedir. Orada tipler yerel olarak tanımlanmıştır; üretimde `framer.h` kullanmanız önerilir.

---

## Derleme ve Yükleme

### Yöntem 1: west

```bash
# Proje kökünden
west build -b nucleo_f070rb app/main \
  -DEXTRA_DTC_OVERLAY_FILE=boards/nucleo_f070rb.overlay

west flash
```

> Farklı bir karta derlerken `-b <board>` ve uygun overlay yolunu geçiniz.

### Yöntem 2: PowerShell betiği (`scripts/bulid.ps1`)

Bu depo içinde isim **bilerek/yanlışlıkla** `bulid.ps1` olarak geçiyor. Betik aşağıdaki **kullanımı** destekler:

```
Usage:
  build.ps1 [-Board <name>] [-DT_OVERLAY <path>] [-BuildDir <path>] [-Clean]
  build.ps1 --board <name> --DT_OVERLAY <path> [--build-dir <path>] [--clean]

Examples:
  build.ps1 -Board nucleo_f070rb -DT_OVERLAY boards\nucleo_f070rb.overlay
  build.ps1 --board nucleo_f070rb --DT_OVERLAY boards/nucleo_f070rb.overlay --clean
```

Önemli noktalar:
- Betik, sisteminizde `west`’in erişilebilir olmasını bekler.
- Varsayılan kart: `nucleo_f070rb`
- `DT_OVERLAY` verilmezse `boards/<board>.overlay` kullanılır.
- Betik içindeki `ZEPHYR_BASE` ve `ZEPHYR_SDK_INSTALL_DIR` Windows örnek yolları ile **hard‑coded**’dır. **Kendi ortamınıza göre düzenleyin.**

---

## Donanım Uyumluluğu ve Port Etme

Bu altyapı, Zephyr’in **UART Async** sürücüsünü sağlayan her platformda çalışır. Yeni bir karta taşımak için:

1. **DTS/Overlay**: UART pin mux, DMA kanalları ve opsiyonel `current-speed` ayarlarını kartınıza göre tanımlayın (`boards/<board>.overlay` oluşturup `-DEXTRA_DTC_OVERLAY_FILE` ile verin).
2. **Kconfig**: `CONFIG_UART_ASYNC_API=y` ve gerekirse `CONFIG_DMA=y` ayarlarını açın.
3. **sys_init.c**: Platformunuzda özel DMA remap vb. gerekiyorsa, `SYS_INIT(...)` ile erken aşamada düzeltmeler yapın (Nucleo F070RB için örnek eklidir).
4. **Buffer Boyutları**: Gerekirse `uart_cfg.h` içindeki `UART_RX_CHUNK_LEN`, `UART_RB_SZ` ve `UART_MAX_PACKET_SIZE` değerlerini uygulamanıza göre ayarlayın.

---
## Test 

> **Test Ortamı**
>
> | Bileşen                | Sürüm / Model           |
> |------------------------|------------------------|
> | USB-UART Dönüştürücü   | CP2102                 |
> | Geliştirme Kartı       | Nucleo-F070RB          |
> | Python                 | 3.13.5                 |
> | Zephyr                 | 4.1.99                  |
> | Zephyr-SDK             | 0.17.2                 |

> `test/zephyr_uart_testbench.py` dosyasını aşağıdaki komutla çalıştırarak bilgisayarınıza bağlı UART kartı üzerinden MCU'ya mesaj gönderip alabilirsiniz. Ekrandaki loglar aşağıdaki gibi görünecektir:

```powershell
PS: zephyr-async-uart\test> python zephyr_uart_testbench.py --port COM7 --send "Hello ASYNC UART"

[TX] 16B -> Frame 20B: AA 10 48 65 6C 6C 6F 20 41 53 59 4E 43 20 55 41 52 54 69 A5
[INFO] RX running. Press Ctrl+C to stop.
[RX] Frame: LEN=16  CRC OK  RAW=AA 10 48 65 6C 6C 6F 20 41 53 59 4E 43 20 55 41 52 54 69 A5
[RX] DATA (text): 'Hello ASYNC UART'
```
---
## Nucleo F070RB Notları

- `boards/nucleo_f070rb.overlay`: USART1 pinleri **PA9/PA10** ve **DMA1** kanalları (`tx=4`, `rx=5`) etkinleştirilmiştir. Ayrıca `aliases { uart-com = &usart1; };` tanımı bulunur.
- `app/main/src/sys_init.c`: **USART1 DMA remap** düzeltmesi yapılır (TX: Ch2→Ch4, RX: Ch3→Ch5). Bu, bazı STM32F0 varyantlarında gerekli olabilir.

---

## Lisans

Bu proje, kökteki **LICENSE** dosyasında belirtilen lisansla dağıtılmaktadır.
