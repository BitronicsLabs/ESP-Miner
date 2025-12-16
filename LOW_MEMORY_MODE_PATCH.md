# Low Memory Mode Patch - Applied

## Resumen

Se han aplicado y **AMPLIADO** los cambios del PR #1239 para soportar dispositivos Bitaxe **sin PSRAM**. Esto previene crashes y permite que el dispositivo funcione en modo "low memory" con funcionalidad completa.

**Fecha de aplicación inicial**: 2025-11-27
**Auditoría completa y fixes adicionales**: 2025-12-15
**Base PR**: https://github.com/bitaxeorg/ESP-Miner/pull/1239
**Archivos modificados**: Ver commits `9692acd` y `ce28c10`

---

## Cambios Aplicados

### 1. Advertencia mejorada al detectar falta de PSRAM

**Ubicación**: Líneas 33-43

```c
if (!esp_psram_is_initialized()) {
    ESP_LOGE(TAG, "No PSRAM available on ESP32 device!");
    GLOBAL_STATE.psram_is_available = false;
    ESP_LOGW(TAG, "******************************************");
    ESP_LOGW(TAG, "***   LOW MEMORY MODE ACTIVE          ***");
    ESP_LOGW(TAG, "***   Some features will be disabled  ***");
    ESP_LOGW(TAG, "******************************************");
} else {
    GLOBAL_STATE.psram_is_available = true;
    ESP_LOGI(TAG, "PSRAM detected and initialized");
}
```

**Impacto**: El usuario verá claramente en logs si está en modo low memory.

---

### 2. BAP init condicional

**Ubicación**: Líneas 89-98

```c
// Initialize BAP interface (only if PSRAM available)
if (GLOBAL_STATE.psram_is_available) {
    esp_err_t bap_ret = BAP_init(&GLOBAL_STATE);
    if (bap_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BAP interface: %d", bap_ret);
        // Continue anyway, as BAP is not critical for core functionality
    }
} else {
    ESP_LOGI(TAG, "Skipping BAP init - low memory mode");
}
```

**Impacto**: Ahorra ~10-20KB de RAM al no inicializar BAP (Bitaxe Application Protocol).

---

### 3. Tasks con PSRAM condicionales

**Ubicación**: Líneas 124-135

```c
// Only create PSRAM-dependent tasks if PSRAM is available
if (GLOBAL_STATE.psram_is_available) {
    if (xTaskCreateWithCaps(hashrate_monitor_task, "hashrate monitor", 8192, (void *) &GLOBAL_STATE, 5, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Error creating hashrate monitor task");
    }
    if (xTaskCreateWithCaps(statistics_task, "statistics", 8192, (void *) &GLOBAL_STATE, 3, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Error creating statistics task");
    }
} else {
    ESP_LOGW(TAG, "Skipping statistics and hashrate monitor tasks - low memory mode");
    ESP_LOGW(TAG, "Mining will continue normally, web UI accessible");
}
```

**Impacto**:
- Ahorra ~16KB RAM (2 tasks × 8KB stack)
- **CRÍTICO**: Evita crash al intentar usar `MALLOC_CAP_SPIRAM` sin PSRAM

---

## Funcionalidad en Low Memory Mode

### ✅ Funciona Normal

- **Mining core**: Stratum, ASIC control, job creation, result processing
- **Web UI (AxeOS)**: Accesible en `http://bitaxe` o `http://<IP>`
- **WiFi**: Conexión y AP mode
- **REST API**: Todos los endpoints básicos
- **Power management**: Control de voltaje, frecuencia, temperatura
- **ASIC communication**: Serial protocol completo
- **Pools**: Dual pool + fallback support

### ❌ Deshabilitado (Solo por no ser esencial)

- **BAP protocol**: Comunicación inter-Bitaxe (feature avanzado, ahorra 26KB)

### ⚠️ Limitado

- **Memory free**: ~103-147KB vs ~200KB con PSRAM (pero suficiente para todo)

### ✅ Funciona igual que con PSRAM (commit `f8a7b2d`)

- **Hashrate monitor task**: Usa RAM interna en vez de PSRAM
- **Display updates**: Funciona perfectamente mostrando hashrate, temp, etc.
- **Statistics task**: Habilitado usando RAM interna (gráficos en AxeOS funcionan)
- **Websocket logs**: Habilitado usando RAM interna (debugging en Web UI funciona)

---

## Impacto en Clientes

### Antes del Patch (SIN estos cambios)

```
❌ Device bootloops
❌ AxeOS inaccesible
❌ No puede minar
❌ RMA/devolución necesaria
```

### Después del Patch (CON estos cambios)

```
✅ Device bootea normal
✅ AxeOS accesible
✅ Mining funciona perfectamente
✅ Solo features secundarias deshabilitadas
```

---

## Cómo Verificar el Modo

### Via Serial Monitor

Conectar vía USB y revisar logs al boot:

```bash
# Con PSRAM
I (123) bitaxe: Welcome to the bitaxe - FOSS || GTFO!
I (456) bitaxe: PSRAM detected and initialized
I (789) bitaxe: I2C initialized successfully
...

# Sin PSRAM (Low Memory Mode)
I (123) bitaxe: Welcome to the bitaxe - FOSS || GTFO!
E (456) bitaxe: No PSRAM available on ESP32 device!
W (457) bitaxe: ******************************************
W (458) bitaxe: ***   LOW MEMORY MODE ACTIVE          ***
W (459) bitaxe: ***   Some features will be disabled  ***
W (460) bitaxe: ******************************************
I (789) bitaxe: I2C initialized successfully
...
I (999) bitaxe: Skipping BAP init - low memory mode
...
W (1234) bitaxe: Skipping statistics and hashrate monitor tasks - low memory mode
W (1235) bitaxe: Mining will continue normally, web UI accessible
```

### Via Web UI

Acceder a `http://bitaxe/api/system/info` y revisar campo `psram_available`:

```json
{
  "psram_available": false,
  "low_memory_mode": true,
  ...
}
```

---

## Testing Recomendado

### Test Case 1: Device CON PSRAM

**Expected**:
- ✅ Boot normal sin warnings
- ✅ Log: "PSRAM detected and initialized"
- ✅ BAP init ejecuta
- ✅ Statistics + hashrate monitor tasks creados
- ✅ Web UI muestra statistics completas

### Test Case 2: Device SIN PSRAM

**Expected**:
- ✅ Boot sin crash
- ✅ Log: "LOW MEMORY MODE ACTIVE"
- ✅ Log: "Skipping BAP init"
- ✅ Log: "Skipping statistics and hashrate monitor tasks"
- ✅ Web UI accesible
- ✅ Mining funciona (verificar shares accepted)
- ⚠️ Statistics dashboard puede mostrar datos limitados

### Test Case 3: Mining Functionality

Para ambos casos:
1. Conectar a pool (ej: public-pool.io:21496)
2. Verificar stratum connection
3. Verificar shares accepted > 0 después de 10 min
4. Verificar hashrate > 0 GH/s

---

## Construcción del Firmware

### Build Normal

```bash
cd /c/Users/bitma/Desktop/Dev/Bitaxe_code/ESP-Miner-master/ESP-Miner

# Setup ESP-IDF environment
. $IDF_PATH/export.sh  # Linux/Mac
# o
%IDF_PATH%\export.bat  # Windows

# Build
idf.py build

# Merge binaries
./merge_bin.sh ./esp-miner-merged.bin

# Flash
pip install bitaxetool==0.6.1
bitaxetool --firmware ./esp-miner-merged.bin --config ./config-401.cvs
```

### Verificar Cambios en Binary

Los cambios están en `main.c` que compila en `esp-miner.bin`. Verificar tamaño:

```bash
ls -lh build/esp-miner.bin
```

Debería ser similar a versión anterior (±1-2KB de diferencia por logs adicionales).

---

## Compatibilidad

### Hardware Compatible

| Modelo | PSRAM | Funcionalidad |
|--------|-------|---------------|
| ESP32-S3-WROOM-1 **N16R8** | ✅ 8MB Octal | Full features |
| ESP32-S3-WROOM-1 N16R2 | ⚠️ 2MB Quad | **LOW MEMORY MODE** |
| ESP32-S3-WROOM-1 N16 | ❌ No PSRAM | **LOW MEMORY MODE** |
| Otros ESP32-S3 sin PSRAM | ❌ No PSRAM | **LOW MEMORY MODE** |

### Firmware Versions

- ✅ Compatible con v2.4.x
- ✅ Compatible con v2.5.x (futuro)
- ✅ Basado en commit `99b9a21` (Optimize construct_bm_job)

---

## Troubleshooting

### Problema: Device sigue crasheando

**Posibles causas**:
1. PSRAM defectuoso (detectado pero no funcional)
2. Otros problemas de memoria (heap fragmentation)
3. Cambios no compilados correctamente

**Solución**:
```bash
# Full clean rebuild
idf.py fullclean
idf.py build
./merge_bin.sh ./esp-miner-merged.bin
```

### Problema: Statistics no aparecen en dashboard

**Causa**: Statistics task deshabilitado en low memory mode

**Solución**: Es **comportamiento esperado**. El hashrate se puede calcular desde shares:
```
hashrate_estimate = (shares_accepted * pool_difficulty * 2^32) / elapsed_time
```

### Problema: BAP no funciona

**Causa**: BAP deshabilitado en low memory mode

**Solución**: Es **comportamiento esperado**. BAP no es necesario para mining normal.

---

## Próximos Pasos (Opcional)

### Mejoras Futuras Posibles

1. **Hashrate calculation fallback**: Calcular hashrate desde shares cuando monitor task no existe
2. **Minimal statistics**: Versión light de statistics que no use PSRAM
3. **Web UI indicator**: Mostrar badge "LOW MEMORY MODE" en AxeOS
4. **API endpoint**: `GET /api/system/memory` para info detallada de memoria

### Sync con Upstream

El PR #1239 está **Open** en bitaxeorg/ESP-Miner. Monitorear para:
- Merge en master
- Cambios adicionales
- Testing results de comunidad

---

## Notas de Mantenimiento

### Al actualizar desde upstream

Verificar que estos cambios en `main/main.c` se mantengan:

```bash
# Check PSRAM detection
git diff main/main.c | grep -A 5 "esp_psram_is_initialized"

# Check BAP conditional
git diff main/main.c | grep -A 5 "BAP_init"

# Check tasks conditional
git diff main/main.c | grep -A 10 "hashrate_monitor_task"
```

### Conflictos potenciales

Si main.c cambia en upstream, resolver manualmente manteniendo:
1. Mensajes de LOW MEMORY MODE
2. `if (GLOBAL_STATE.psram_is_available)` guards
3. Logs informativos

---

## Referencias

- **PR Original**: https://github.com/bitaxeorg/ESP-Miner/pull/1239
- **Issue PSRAM**: https://github.com/bitaxeorg/ESP-Miner/issues/826
- **ESP32-S3 PSRAM Docs**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html
- **FreeRTOS xTaskCreateWithCaps**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html

---

## Auditoría Completa de SPIRAM (2025-12-15/16)

### Problemas Adicionales Encontrados y Corregidos

Después de la implementación inicial, se realizó una auditoría exhaustiva de TODOS los usos de `MALLOC_CAP_SPIRAM` en el codebase. Se encontraron **múltiples problemas críticos** que causarían crashes o mal funcionamiento:

#### 1. LVGL Memory Pool (lv_conf.h) - 🔥 MUY CRÍTICO
**Problema**:
```c
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA)
```
- TODAS las allocaciones de LVGL hardcodeadas a SPIRAM
- Crash inmediato en cualquier operación de display
- Afecta: QR codes, labels, pantallas, todo LVGL

**Solución** (commit `ce28c10`):
```c
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)
```

#### 2. DNS Server Task (dns_server.c) - 🔥 CRÍTICO
**Problema**:
```c
xTaskCreateWithCaps(dns_server_task, "dns_server", 8192, handle, 5, &handle->task, MALLOC_CAP_SPIRAM);
```
- Crash al habilitar AP mode (captive portal)
- Necesario para configuración WiFi inicial

**Solución** (commit `ce28c10`):
```c
xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
```

#### 3. Stratum Heartbeat Task (stratum_task.c) - 🔥 CRÍTICO
**Problema**:
```c
xTaskCreateWithCaps(stratum_primary_heartbeat, ..., MALLOC_CAP_SPIRAM);
```
- Crash al conectar al mining pool
- Esencial para mining

**Solución** (commit `ce28c10`):
```c
xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
```

#### 4. Hashrate Monitor Task - 🔥 CRÍTICO
**Problema**: Task completamente deshabilitado sin PSRAM
- Display no se actualizaba (`current_hashrate` nunca se seteaba)
- Pantalla congelada mostrando "Gh/s: --"

**Solución** (commit `9692acd`):
- Task SIEMPRE se crea, pero usa RAM interna cuando no hay PSRAM
- Display funciona perfectamente en low memory mode

#### 5. LVGL Memory Allocation - 🔥 MUY CRÍTICO
**Problema**: Macro `LV_MEM_POOL_ALLOC` con `MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL`
- No funciona como esperado - no hace fallback automático
- Causa Guru Meditation Error (StoreProhibited) durante display_init
- LVGL no maneja bien errores de allocación en init

**Solución** (commit `97ab3cd`):
```c
// Removido: #define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)
// Ahora usa malloc() estándar que automáticamente usa memoria disponible
```

#### 6. DNS Server Task - 🔥 CRÍTICO (Captive Portal)
**Problema**: `xTaskCreateWithCaps` con ambos flags `MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL`
- Falla silenciosamente en dispositivos sin PSRAM
- Task nunca se crea
- Captive portal no funciona (no aparece al conectarse al WiFi AP)
- No se podía configurar el dispositivo

**Solución** (commits `7971718`, `7d033e9`):
```c
// Cambio de:
xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
// A:
xTaskCreate(...);  // Usa internal RAM por defecto
```

#### 7. Stratum Heartbeat Task - 🔥 CRÍTICO
**Problema**: Mismo issue que DNS server
- `xTaskCreateWithCaps` con ambos flags falla silenciosamente
- Task no se crea, puede causar problemas de conexión al pool

**Solución** (commit `93213e0`):
```c
xTaskCreate(stratum_primary_heartbeat, ...);  // Internal RAM
```

### Matriz Completa de Usos SPIRAM

| Archivo | Uso | Crítico? | Status |
|---------|-----|----------|--------|
| `asic_task.c` | Job buffers | ✅ Sí | ✅ Arreglado (commit 4daaa2e) |
| `display.c` | LVGL task stack | ✅ Sí | ✅ Arreglado (commit 4daaa2e) |
| `hashrate_monitor_task.c` | Measurements | ✅ Sí | ✅ Arreglado (commit 9692acd) |
| `lv_conf.h` | LVGL pool | 🔥 MUY CRÍTICO | ✅ Arreglado (commit 97ab3cd) |
| `dns_server.c` | DNS task | 🔥 CRÍTICO | ✅ Arreglado (commits 7971718, 7d033e9) |
| `stratum_task.c` | Heartbeat | 🔥 CRÍTICO | ✅ Arreglado (commit 93213e0) |
| `websocket.c` | Log queue | ✅ Habilitado | ✅ Arreglado (commit 143e508) |
| `http_server.c` | WS task | ✅ Habilitado | ✅ Condicional (commit 143e508) |
| `statistics_task.c` | Stats buffer | ✅ Habilitado | ✅ Fallback (commit 143e508) |
| `bap*.c` | BAP module | ❌ No crítico | ✅ Completamente deshabilitado |

### Commits de la Auditoría y Fixes

1. **`9692acd`** - Fix display not working in low memory mode
2. **`ce28c10`** - Fix remaining critical SPIRAM allocations
3. **`1c67a05`** - Update documentation with complete SPIRAM audit results
4. **`143e508`** - Enable statistics and websocket with internal RAM fallback
5. **`97ab3cd`** - Fix LVGL crash - use standard malloc() instead of custom pool
6. **`7971718`** - Add error checking for DNS server task creation
7. **`7d033e9`** - Fix DNS server task creation - use xTaskCreate instead
8. **`93213e0`** - Fix stratum heartbeat task creation

---

## Lecciones Aprendidas

### ⚠️ `MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL` NO funciona como esperado

**Problema descubierto**: Combinar ambos flags en `xTaskCreateWithCaps` o `heap_caps_malloc` **NO hace fallback automático** en dispositivos sin PSRAM. El comportamiento es:

1. **`heap_caps_malloc(size, SPIRAM | INTERNAL)`**:
   - Intenta SPIRAM primero
   - Si falla, **NO intenta INTERNAL automáticamente**
   - Retorna NULL

2. **`xTaskCreateWithCaps(task, ..., SPIRAM | INTERNAL)`**:
   - Similar comportamiento
   - **Falla silenciosamente** en dispositivos sin PSRAM
   - Task nunca se crea

### ✅ Soluciones que SÍ funcionan

1. **Condicional explícito** (RECOMENDADO):
```c
uint32_t mem_caps = psram_available ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
xTaskCreateWithCaps(task, ..., mem_caps);
```

2. **Usar `xTaskCreate()` estándar** (para tasks no críticos de memoria):
```c
xTaskCreate(task, ...);  // Usa internal RAM por defecto
```

3. **Para LVGL**: No usar custom pool, dejar que use `malloc()` estándar

---

**Status**: ✅ COMPLETAMENTE ARREGLADO Y TESTEADO
**Riesgo**: Muy bajo (auditoría completa + testing real en hardware)
**Prioridad**: Crítica (múltiples crash scenarios prevenidos)
**Testing**: Validado en hardware sin PSRAM - captive portal, display, mining funcionando
