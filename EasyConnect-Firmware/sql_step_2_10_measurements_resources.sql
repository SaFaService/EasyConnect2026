-- Step 2.10 - Monitor risorse master (CPU/RAM/Flash/Uptime)
-- =========================================================
-- Aggiunge colonne su measurements per salvare metriche runtime
-- ricevute dal firmware master.

START TRANSACTION;

ALTER TABLE `measurements`
  ADD COLUMN IF NOT EXISTS `uptime_seconds` BIGINT UNSIGNED NULL AFTER `api_rx_cycle_bytes`,
  ADD COLUMN IF NOT EXISTS `cpu_mhz` SMALLINT UNSIGNED NULL AFTER `uptime_seconds`,
  ADD COLUMN IF NOT EXISTS `heap_free_bytes` BIGINT UNSIGNED NULL AFTER `cpu_mhz`,
  ADD COLUMN IF NOT EXISTS `heap_min_bytes` BIGINT UNSIGNED NULL AFTER `heap_free_bytes`,
  ADD COLUMN IF NOT EXISTS `heap_total_bytes` BIGINT UNSIGNED NULL AFTER `heap_min_bytes`,
  ADD COLUMN IF NOT EXISTS `sketch_used_bytes` BIGINT UNSIGNED NULL AFTER `heap_total_bytes`,
  ADD COLUMN IF NOT EXISTS `sketch_free_bytes` BIGINT UNSIGNED NULL AFTER `sketch_used_bytes`;

SET @idx_res_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'measurements'
    AND index_name = 'idx_measurements_master_runtime'
);
SET @sql_idx_res := IF(
  @idx_res_exists = 0,
  'ALTER TABLE measurements ADD INDEX idx_measurements_master_runtime (master_id, recorded_at)',
  'SELECT 1'
);
PREPARE stmt_idx_res FROM @sql_idx_res;
EXECUTE stmt_idx_res;
DEALLOCATE PREPARE stmt_idx_res;

COMMIT;
