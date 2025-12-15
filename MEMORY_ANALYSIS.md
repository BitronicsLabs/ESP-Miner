# ESP32-S3 RAM Analysis - Low Memory Mode

## Hardware Specs

- **ESP32-S3**: ~320KB internal RAM available for applications
- **Without PSRAM**: All allocations must use internal RAM

---

## Complete Task Inventory

### Core Application Tasks (ALWAYS RUNNING)

| Task Name | Stack Size | Memory Type | Notes |
|-----------|------------|-------------|-------|
| **power_management** | 8192 (8KB) | Internal | ASIC power/thermal control |
| **stratum_task** | 8192 (8KB) | Internal | Pool communication |
| **stratum_heartbeat** | 8192 (8KB) | Internal/PSRAM* | Keep-alive packets |
| **create_jobs_task** | 8192 (8KB) | Internal | Mining job generation |
| **ASIC_task** | 8192 (8KB) | Internal | ASIC communication |
| **ASIC_result_task** | 8192 (8KB) | Internal | Process mining results |
| **hashrate_monitor** | 8192 (8KB) | Internal/PSRAM* | **CRITICAL for display** |
| **nvs_task** | 8192 (8KB) | Internal | NVS config writes |
| **dns_server** | 8192 (8KB) | Internal/PSRAM* | Captive portal (AP mode) |

**Subtotal Core Tasks**: 9 × 8KB = **72KB**

*Now with automatic fallback to internal RAM

---

### System Tasks (ESP-IDF)

These are managed by ESP-IDF and always present:

| Task | Estimated Stack | Notes |
|------|----------------|-------|
| **WiFi/LwIP tasks** | ~24KB | Network stack (3-4 tasks) |
| **Main/Arduino loop** | ~8KB | Main application loop |
| **IDLE tasks (2 cores)** | ~4KB | FreeRTOS idle tasks |
| **Timer service** | ~4KB | Software timers |

**Subtotal System Tasks**: **~40KB**

---

### Display/LVGL Tasks

| Task | Stack/Memory | Memory Type | Notes |
|------|--------------|-------------|-------|
| **LVGL task** | 8192 (8KB) | Internal/PSRAM* | Display refresh |
| **LVGL memory pool** | ~5-10KB | Internal/PSRAM* | Dynamic UI allocations |

**Subtotal Display**: **~13-18KB**

---

### Optional Features (Can Enable/Disable)

#### 1. Statistics Task
| Component | Size | Type | Usage Pattern |
|-----------|------|------|---------------|
| Task stack | 8KB | Internal/PSRAM* | Constant |
| Statistics buffer | 32KB | Internal/PSRAM* | **Grows gradually** |
| Initial allocation | 0KB | - | Empty at boot |
| After 5 minutes | ~3KB | - | 60 entries × 44 bytes |
| After 1 hour (full) | 32KB | - | 720 entries × 44 bytes |

**Peak Total**: **40KB** (but only 8KB at boot)

#### 2. Websocket Logs
| Component | Size | Type | Usage Pattern |
|-----------|------|------|---------------|
| Task stack | 8KB | Internal/PSRAM* | Constant |
| Message queue | 512B | Internal | Dynamic (empty without clients) |

**Total**: **~8.5KB** (mostly unused without connected clients)

#### 3. BAP Protocol (Currently Disabled)
| Component | Size | Type |
|-----------|------|------|
| UART RX task | 8KB | Internal/PSRAM |
| UART TX task | 8KB | Internal/PSRAM |
| Subscription task | 8KB | Internal/PSRAM |
| Queues/buffers | ~2KB | Internal/PSRAM |

**Total**: **~26KB** (not recommended to enable)

---

## Dynamic Memory Allocations

### ASIC Job Management
```c
active_jobs = malloc(sizeof(bm_job *) * 128);  // 128 × 4 = 512 bytes
valid_jobs = malloc(sizeof(uint8_t) * 128);    // 128 bytes
```
**Total**: **~640 bytes** + actual job structs (varies)

### Hashrate Monitor
```c
total_measurement = malloc(sizeof(measurement_t) * asic_count);     // ~48 bytes
domain_measurements = malloc(...);                                   // ~192 bytes (if 4 domains)
error_measurement = malloc(sizeof(measurement_t) * asic_count);     // ~48 bytes
```
**Total**: **~288 bytes**

### HTTP Server
- REST server context: ~2KB
- Request buffers: Dynamic (up to 2KB during requests)
- Response buffers: Dynamic

**Estimated**: **~2-4KB base** + dynamic during requests

---

## Total RAM Usage Summary

### Minimum Configuration (No Optional Features)

| Category | RAM Used | Notes |
|----------|----------|-------|
| Core App Tasks | 72KB | Always running |
| System/ESP-IDF Tasks | 40KB | WiFi, TCP/IP, etc |
| Display/LVGL | 13-18KB | UI and graphics |
| Dynamic allocations | 5-10KB | Jobs, buffers, etc |
| **SUBTOTAL** | **130-140KB** | |
| Safety margin (20%) | 26-28KB | Fragmentation, peaks |
| **TOTAL MINIMUM** | **~156-168KB** | |
| **FREE RAM** | **~152-164KB** | Available for features |

### With Statistics + Websocket Enabled

| Additional Components | RAM Used | Growth Pattern |
|----------------------|----------|----------------|
| Statistics task | 8KB | Immediate |
| Statistics buffer | 0→32KB | Gradual over 1 hour |
| Websocket task | 8KB | Immediate |
| Websocket queue | 512B | Dynamic |
| **TOTAL ADDED** | **16.5KB→48.5KB** | |

**FREE RAM after enabling**:
- At boot: ~135-148KB
- After 1 hour: ~103-116KB

---

## Recommendations

### ✅ SAFE TO ENABLE
1. **Websocket logs** - Only 8.5KB, very useful for debugging
2. **Statistics task** - 40KB peak but grows slowly, provides value

### ⚠️ MONITOR
- Keep an eye on heap fragmentation
- Test worst-case scenario (all features active, multiple HTTP clients, etc)

### ❌ NOT RECOMMENDED
- **BAP protocol** - 26KB for rarely-used feature

---

## Memory Safety Margins

With both Statistics and Websocket enabled:

| Scenario | RAM Used | RAM Free | Safety Margin |
|----------|----------|----------|---------------|
| **Best case (boot)** | ~173KB | ~147KB | ✅ Excellent (46%) |
| **Typical (15 min)** | ~190KB | ~130KB | ✅ Good (41%) |
| **Worst case (1 hr)** | ~217KB | ~103KB | ✅ Acceptable (32%) |

**Minimum recommended free RAM**: ~80KB (25%)
**Current worst case**: ~103KB (32%) ✅

---

## Testing Checklist

Before deploying with optional features enabled:

- [ ] Boot test - check free heap with `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
- [ ] Stress test - all features active simultaneously:
  - [ ] Multiple HTTP clients
  - [ ] WebSocket client connected
  - [ ] Active mining
  - [ ] Display updating
- [ ] Long-term test - run for 2+ hours, monitor heap
- [ ] Check for memory leaks
- [ ] Verify no crashes under load

---

## Conclusion

**ESP32-S3 without PSRAM can comfortably run:**
- ✅ All core mining functions
- ✅ Full display/UI
- ✅ Statistics logging
- ✅ Websocket logs
- ✅ Web UI (AxeOS)

**With ~103KB free RAM in worst case**, we have sufficient headroom for:
- Network buffers
- Dynamic allocations
- Heap fragmentation
- Future features

**Decision: ENABLE both Statistics and Websocket** 👍
