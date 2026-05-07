#ifndef ESP32_BIN_LOADER_RETURN_H
#define ESP32_BIN_LOADER_RETURN_H

#include <esp_ota_ops.h>
#include <esp_partition.h>

static inline esp_err_t esp32BinLoaderReturnToFactoryOnNextBoot() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running || running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    return ESP_OK;
  }

  const esp_partition_t *factory = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP,
    ESP_PARTITION_SUBTYPE_APP_FACTORY,
    nullptr
  );
  if (!factory) {
    return ESP_ERR_NOT_FOUND;
  }

  return esp_ota_set_boot_partition(factory);
}

#endif  // ESP32_BIN_LOADER_RETURN_H
