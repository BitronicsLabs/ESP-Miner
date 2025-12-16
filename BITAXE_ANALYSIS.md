# Análisis del Proyecto ESP-Miner (Bitaxe Firmware)

## Resumen Ejecutivo

**ESP-Miner** es el firmware open source oficial para los dispositivos [Bitaxe](https://github.com/bitaxeorg/bitaxe), un hardware de minería Bitcoin basado en chips ASIC (BM1397, BM1366, BM1368, BM1370) controlados por ESP32-S3.

- **Repositorio**: https://github.com/bitaxeorg/ESP-Miner
- **Hardware**: ESP32-S3-WROOM-1 N16R8 (16MB Flash + 8MB PSRAM)
- **Build system**: ESP-IDF (no Arduino, no PlatformIO)
- **Lenguaje**: C puro
- **Web UI**: AxeOS con REST API

---

## Arquitectura del Proyecto

### Estructura de Directorios

```
ESP-Miner/
├── main/                      # Aplicación principal
│   ├── main.c                 # Entry point
│   ├── global_state.h         # Estado global compartido
│   ├── tasks/                 # FreeRTOS tasks
│   │   ├── asic_task.c        # Control de ASIC chips
│   │   ├── asic_result_task.c # Procesa resultados de mining
│   │   ├── stratum_task.c     # Cliente Stratum
│   │   ├── create_jobs_task.c # Generación de jobs
│   │   ├── hashrate_monitor_task.c
│   │   ├── power_management_task.c
│   │   └── statistics_task.c
│   ├── http_server/           # AxeOS REST API + Web UI
│   ├── self_test/             # Auto-test del hardware
│   ├── thermal/               # Gestión térmica
│   ├── power/                 # Gestión de alimentación
│   └── bap/                   # BAP protocol (Bitaxe Application Protocol)
│
├── components/                # Componentes ESP-IDF
│   ├── asic/                  # Drivers para chips ASIC
│   │   ├── bm1397.c           # BM1397 (Bitaxe 100, 200)
│   │   ├── bm1366.c           # BM1366 (Bitaxe Hex)
│   │   ├── bm1368.c           # BM1368 (Bitaxe Supra)
│   │   ├── bm1370.c           # BM1370 (Bitaxe Gamma)
│   │   ├── serial.c           # UART communication con ASIC
│   │   ├── pll.c              # PLL frequency control
│   │   └── crc.c              # CRC5 para comandos ASIC
│   ├── stratum/               # Implementación protocolo Stratum
│   ├── connect/               # WiFi connection management
│   └── dns_server/            # DNS server para AP mode
│
├── tools/                     # Herramientas de desarrollo
├── test/                      # Unit tests
├── doc/                       # Documentación
├── config-*.cvs               # NVS configs por modelo hardware
├── partitions.csv             # Tabla de particiones flash
├── sdkconfig.defaults         # Configuración ESP-IDF
└── CMakeLists.txt             # Build configuration
```

---

## Componentes Core

### 1. Main Application Flow (`main/main.c`)

```c
void app_main(void)
{
    // 1. Check PSRAM
    // 2. Init I2C bus
    // 3. Hold ASIC reset LOW (reduce power)
    // 4. Init ADC (voltage/current monitoring)
    // 5. Init NVS config
    // 6. Init device config
    // 7. Self-test (optional)
    // 8. Init system + peripherals
    // 9. WiFi init + connect
    // 10. Start power management task
    // 11. Start AxeOS REST server
    // 12. Init BAP interface
    // 13. Init work queues
    // 14. Initialize ASIC (cold boot)
    // 15. Create FreeRTOS tasks:
    //     - stratum_task (priority 5)
    //     - create_jobs_task (priority 10)
    //     - ASIC_task (priority 10)
    //     - ASIC_result_task (priority 15) <- highest
    //     - hashrate_monitor_task (priority 5)
    //     - statistics_task (priority 3)
}
```

**Task Priorities**:
- ASIC_result_task: 15 (crítico - procesa nonces encontrados)
- ASIC_task, create_jobs_task, power_management: 10
- stratum_task, hashrate_monitor: 5
- statistics_task: 3

---

### 2. ASIC Control (`components/asic/`)

#### Chips Soportados

| Chip   | Modelo Bitaxe | Características |
|--------|---------------|-----------------|
| BM1397 | Bitaxe 100, 200 | 1 chip, ~400 GH/s |
| BM1366 | Bitaxe Hex | 6 chips, ~3 TH/s |
| BM1368 | Bitaxe Supra | Multiple chips |
| BM1370 | Bitaxe Gamma | Más eficiente |

#### Comunicación ASIC

**Protocolo**: UART serial full-duplex
**Comandos principales**:
- `WRITE_REG` - Configurar registros del chip
- `READ_REG` - Leer registros
- `SET_CHIP_ADDRESS` - Chain addressing
- `SEND_WORK` - Enviar trabajo de minería
- `SET_BAUDRATE` - Cambiar velocidad UART
- `SET_PLL` - Ajustar frecuencia de reloj

**CRC5**: Todos los comandos incluyen CRC5 para integridad

#### Frequency Control (`pll.c`)

```c
// Ajusta frecuencia del ASIC chip
// Rango típico: 400 MHz - 600 MHz
// Mayor frecuencia = mayor hashrate pero más calor/consumo
void set_asic_frequency(uint16_t freq_mhz);
```

**Voltage Control**:
- Rango: ~1.0V - 1.3V (dependiendo del chip)
- Control vía I2C PMBus (componente `power/`)
- Overclocking requiere mayor voltaje

---

### 3. Stratum Client (`components/stratum/`)

#### Implementación

**Stratum v1** completo con extensiones:
- `mining.subscribe` - Conexión inicial
- `mining.authorize` - Autenticación
- `mining.notify` - Nuevos jobs
- `mining.submit` - Submit shares
- `mining.set_difficulty` - Ajuste dificultad
- `mining.set_version_mask` - Version rolling (overt ASICBoost)
- `mining.set_extranonce` - Extranonce subscription

**Fallback Pool Support**:
```c
SystemModule {
    char * pool_url;
    char * fallback_pool_url;
    uint16_t pool_port;
    uint16_t fallback_pool_port;
    bool use_fallback_stratum;
    bool is_using_fallback;
}
```

#### Job Processing (`create_jobs_task.c`)

1. Recibe job de stratum
2. Construye coinbase transaction (coinb1 + extranonce + coinb2)
3. Calcula merkle root
4. Prepara block header
5. **Precalcula midstate** (primeros 64 bytes SHA256)
6. Envía trabajo al ASIC via UART

**Optimización**: El ASIC solo procesa los últimos 16 bytes (nonce + timestamp)

---

### 4. Work Queue System (`work_queue.c`)

```c
typedef struct {
    work_queue stratum_queue;      // Jobs desde pool
    work_queue ASIC_jobs_queue;    // Jobs enviados a ASIC
} GlobalState;
```

**Thread-safe**: Usa `pthread_mutex_t` para sincronización
**Capacidad**: Configurable, típicamente 4-8 jobs en cola

---

### 5. Power Management (`main/tasks/power_management_task.c`)

#### Thermal Management

```c
PowerManagementModule {
    float chip_temp;           // Temperatura del ASIC
    float vreg_temp;          // Temperatura voltage regulator
    float fan_speed;          // RPM del ventilador
    float fan_perc;           // % velocidad
    bool overheat_mode;       // Throttling térmico
}
```

**Control térmico**:
- Monitoreo continuo vía I2C sensors
- Fan control automático (PWM)
- Throttling si T > threshold
- Emergency shutdown si T > critical

#### Power Monitoring

- **Voltage**: Monitoreo via ADC
- **Current**: Sensor de corriente I2C
- **Power**: Cálculo V × I
- **Efficiency**: W/TH tracking

---

### 6. AxeOS Web UI + REST API (`main/http_server/`)

#### API Endpoints

**GET**:
- `/api/system/info` - System info (firmware version, model, etc)
- `/api/system/asic` - ASIC settings (freq, voltage, cores)
- `/api/system/statistics` - Mining stats
- `/api/system/statistics/dashboard` - Dashboard data
- `/api/system/wifi/scan` - WiFi scan

**POST**:
- `/api/system/restart` - Reboot device
- `/api/system/identify` - Blink LED
- `/api/system/OTA` - Firmware update
- `/api/system/OTAWWW` - Web UI update

**PATCH**:
- `/api/system` - Update settings (JSON)

#### Configuración Overclocking

**URL**: `http://<IP>/settings?oc`
Desbloquea campos de frecuencia y voltaje

**Warning**: Requiere cooling adicional, puede dañar hardware

---

### 7. BAP Protocol (`main/bap/`)

**Bitaxe Application Protocol** - Comunicación inter-Bitaxe
- **Purpose**: Permite comunicación entre múltiples Bitaxes
- **Transport**: Probablemente UART/I2C (a investigar)
- **Status**: No crítico (app continúa si falla BAP_init)

---

### 8. Device Config System (`main/device_config.c`)

#### Config Files (`config-*.cvs`)

Formato NVS (Name-Value Storage):
```csv
nvs,namespace,key,type,encoding,value
nvs,devconfig,deviceModel,data,string,401
nvs,devconfig,asicModel,data,string,BM1397
nvs,devconfig,asicCount,data,u16,1
nvs,devconfig,coreVoltage,data,u16,1200
nvs,devconfig,frequency,data,u16,485
```

**Device Models**:
- 102: Bitaxe 100
- 201-207: Bitaxe 200 variants
- 303: Bitaxe Hex
- 401-403: Bitaxe Gamma
- 601-602: Bitaxe Supra
- 800x: Custom boards

#### Custom Config

`config-custom.cvs` permite configuraciones personalizadas basadas en `devicemodel` y `asicmodel` existentes.

---

### 9. Self-Test System (`main/self_test/`)

**Verificación hardware** al boot:
1. PSRAM check
2. I2C bus check
3. ASIC chip detection
4. Voltage rails check
5. Temperature sensors check
6. Fan check

**Result**:
```c
SelfTestModule {
    bool is_active;
    bool is_finished;
    char *message;
    char *result;
}
```

---

## Build System (ESP-IDF)

### Requisitos

- **ESP-IDF**: v5.x (verificar versión específica en docs)
- **CMake**: 3.5+
- **Node.js/npm**: Para build de AxeOS web UI
- **Python**: 3.4+ con pip
- **bitaxetool**: CLI tool para flashing

### Build Commands

```bash
# 1. Setup ESP-IDF environment
. $IDF_PATH/export.sh

# 2. Configure (si necesario)
idf.py menuconfig

# 3. Build
idf.py build

# 4. Merge binaries
./merge_bin.sh ./esp-miner-merged.bin

# 5. Flash con bitaxetool
pip install bitaxetool==0.6.1
bitaxetool --config ./config-401.cvs --firmware ./esp-miner-merged.bin
```

### Partition Table (`partitions.csv`)

```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x1E0000,
www,      app,  www,     0x1F0000,0x100000,
spiffs,   data, spiffs,  0x2F0000,0x100000,
coredump, data, coredump,0x3F0000,0x10000,
```

- **factory**: Firmware principal (1.875 MB)
- **www**: AxeOS web UI (1 MB)
- **spiffs**: Storage (1 MB)
- **nvs**: Configuración persistente (24 KB)
- **coredump**: Debug crashes (64 KB)

### Scripts

- `merge_bin.sh` - Merge bootloader + partitions + app
- `merge_bin_all.sh` - Merge para todos los configs
- `merge_bin_with_config.sh` - Include NVS config

---

## Diferencias Clave vs NerdMiner

| Aspecto | Bitaxe (ESP-Miner) | NerdMiner |
|---------|-------------------|-----------|
| **Build System** | ESP-IDF (CMake) | PlatformIO (Arduino) |
| **Lenguaje** | C puro | C++ (Arduino) |
| **Mining** | ASIC chips externos (BM1397, etc) | ESP32 SHA256 interno |
| **Hashrate** | 400 GH/s - 3 TH/s | 40-50 KH/s |
| **UART** | Control de ASIC chips | No usado para mining |
| **I2C** | Power management, sensors | Display (opcional) |
| **Complejidad** | Mucho mayor | Relativamente simple |
| **Purpose** | Mining productivo | Educativo/novelty |
| **Tasks** | 6 FreeRTOS tasks | 3-4 tasks |
| **Web UI** | AxeOS (profesional) | WiFiManager básico |
| **OTA** | Dual (firmware + www) | Single firmware |
| **Thermal Mgmt** | Critical (fan, throttling) | Minimal |
| **Power Mgmt** | PMBus voltage control | None |

---

## Características Destacables

### 1. ASIC Communication

**Serial protocol** custom para cada chip:
- BM1397: Comandos de 8 bytes + CRC5
- Baudrate dinámico (115200 -> 3M baud)
- Chain addressing para múltiples chips
- Work distribution entre chips

### 2. Frequency Scaling

**PLL Control** dinámico:
- Auto-tuning basado en temperatura
- Per-chip frequency adjustment (en multi-chip)
- Thermal throttling inteligente

### 3. Version Rolling (overt ASICBoost)

```c
uint32_t version_mask;
bool new_stratum_version_rolling_msg;
```

Permite optimización del 20-30% en eficiencia energética

### 4. Hashrate Monitoring

```c
HashrateMonitorModule {
    float current_hashrate;
    float error_percentage;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t best_nonce_diff;
    char best_diff_string[10];
}
```

**Tracking**:
- Hashrate instantáneo y promedio
- Error rate (HW errors del ASIC)
- Best share ever found
- Session statistics

### 5. Dual Pool + Fallback

Automatic failover si pool principal falla:
- Timeout detection
- Auto-switch a fallback pool
- Configurable retry logic

### 6. mDNS Support

Acceso via `http://bitaxe` sin necesidad de IP

### 7. Recovery Mode

`http://<IP>/recovery` - Permite recuperar de fallos de OTA update

---

## Hardware Específico

### ESP32-S3 Requirements

**Mandatory**:
- Module: ESP32-S3-WROOM-1 **N16R8**
  - 16MB Flash (Quad SPI)
  - 8MB PSRAM (**Octal SPI** - crítico)

**No compatible**:
- Modules sin PSRAM
- Modules con Quad SPI PSRAM
- ESP32-S3-WROOM-1U (different pinout)

### Peripherals

**I2C Bus**:
- Power management IC (PMBus)
- Temperature sensors (TMP1075, etc)
- Fan controller (EMC2101, etc)

**UART**:
- ASIC chip communication (primary purpose)
- USB-to-Serial (debug/programming)

**ADC**:
- Voltage monitoring
- Current sensing (via shunt resistor)

**GPIO**:
- Fan PWM control
- ASIC reset line
- Status LEDs
- Chip select (multi-chip boards)

---

## Roadmap & Development

### Características en Desarrollo

(Basado en issues de GitHub - verificar repo actual)

- [ ] Multi-pool load balancing
- [ ] Advanced temp curves
- [ ] Stratum v2 support
- [ ] Mesh networking (BAP expansion)
- [ ] Better power efficiency algorithms
- [ ] Remote management protocol

### Testing

**Unit tests** en `test/`:
- CRC calculations
- Stratum parsing
- ASIC command generation
- Job creation

**Test-CI** pipeline para continuous integration

---

## Comandos Útiles

### Bitaxetool

```bash
# Install
pip install bitaxetool==0.6.1

# Flash factory image (reset completo)
bitaxetool --firmware ./esp-miner-factory-401-v2.4.2.bin

# Flash solo config
bitaxetool --config ./config-401.cvs

# Flash firmware + config
bitaxetool --config ./config-401.cvs --firmware ./esp-miner-factory-401-v2.4.2.bin
```

### ESP-IDF

```bash
# Build
idf.py build

# Flash directo (USB)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial
idf.py monitor

# Clean
idf.py fullclean

# Menuconfig
idf.py menuconfig
```

### API Testing

```bash
# System info
curl http://bitaxe/api/system/info | jq

# ASIC settings
curl http://bitaxe/api/system/asic | jq

# Update frequency (PATCH)
curl -X PATCH http://bitaxe/api/system \
  -H "Content-Type: application/json" \
  -d '{"frequency": 500}'

# Restart
curl -X POST http://bitaxe/api/system/restart

# Identify (blink LED)
curl -X POST http://bitaxe/api/system/identify
```

---

## Debugging

### Serial Monitor

```bash
idf.py monitor

# Filtering
idf.py monitor | grep "bitaxe"

# Con exception decoder
idf.py monitor --decode
```

### Coredump Analysis

Si device crashea, coredump se guarda en partition.

```bash
# Extract coredump
esptool.py -p /dev/ttyUSB0 read_flash 0x3F0000 0x10000 coredump.bin

# Analyze
idf.py coredump-info coredump.bin
```

### Common Issues

**1. ASIC not detected**
- Check UART connections
- Verify ASIC power rails (I2C PMBus)
- Check reset line (should pulse at boot)

**2. No PSRAM**
- **CRITICAL**: Must use N16R8 module with Octal PSRAM
- Quad PSRAM modules won't work

**3. WiFi blocked mining**
- Disable AiProtection (ASUS routers)
- Disable IoT blocking (some TP-Link)
- Check firewall rules for stratum ports

**4. Overheating**
- Reduce frequency
- Lower voltage
- Check fan (PWM signal, RPM sensor)
- Improve cooling

---

## Recursos

- **GitHub**: https://github.com/bitaxeorg/ESP-Miner
- **Discord**: https://discord.gg/osmu
- **Bitaxe Hardware**: https://github.com/bitaxeorg/bitaxe
- **Docs**: `doc/` folder en repo
- **Flashing Guide**: `flashing.md`
- **OpenAPI Spec**: `main/http_server/openapi.yaml`

---

## Conclusiones

### Fortalezas

1. **Profesional**: Código bien estructurado, modular
2. **ESP-IDF nativo**: Máximo performance, control total
3. **Multi-chip support**: 4 generaciones de ASIC
4. **Robust error handling**: Self-test, recovery mode, fallback pools
5. **Active development**: Community activa, updates frecuentes
6. **REST API**: Fácil integración con otros sistemas
7. **Thermal/Power management**: Critical para operación estable

### Complejidad

- **Alta curva de aprendizaje**: ESP-IDF vs Arduino
- **Hardware específico**: No funciona en ESP32 genéricos
- **ASIC knowledge**: Requiere entender datasheets de BM13XX
- **Electrical engineering**: Voltage control, power management

### Para Aprender

- **UART protocol design**: Excelente ejemplo de custom serial protocol
- **FreeRTOS tasks**: Arquitectura multi-threaded bien diseñada
- **ESP-IDF components**: Modularidad y reusabilidad
- **REST API implementation**: HTTP server en ESP32
- **ASIC interfacing**: Control de chips mining externos

---

## Siguiente Paso

Para análisis más profundo:
1. Estudiar `components/asic/bm1397.c` - Protocolo ASIC detallado
2. Revisar `main/tasks/asic_task.c` - Work distribution logic
3. Analizar `components/stratum/` - Implementación Stratum
4. Investigar `main/http_server/` - AxeOS implementation
5. Leer datasheets de BM1397/BM1366/BM1368/BM1370

---

**Generado**: 2025-27-11
**Versión analizada**: ESP-Miner master branch
**Hardware de referencia**: Bitaxe Gamma (401)
