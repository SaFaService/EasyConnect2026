-- Step 2.12 - Plant metadata e modalita seriali
-- ============================================================
-- DB target: antralux_iot (MariaDB 10.11+)
-- Eseguire prima di usare:
-- - flag impianto permanentemente offline
-- - classificazione impianto display/standalone/rewamping
-- - device_mode nella gestione seriali

START TRANSACTION;

ALTER TABLE `masters`
  ADD COLUMN IF NOT EXISTS `permanently_offline` TINYINT(1) NOT NULL DEFAULT 0 AFTER `address`,
  ADD COLUMN IF NOT EXISTS `plant_kind` VARCHAR(32) DEFAULT NULL AFTER `permanently_offline`;

ALTER TABLE `device_serials`
  ADD COLUMN IF NOT EXISTS `device_mode` TINYINT UNSIGNED DEFAULT NULL AFTER `product_type_code`;

SET @idx_masters_permanent_offline_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND index_name = 'idx_masters_permanent_offline'
);
SET @sql_idx_masters_permanent_offline := IF(
  @idx_masters_permanent_offline_exists = 0,
  'ALTER TABLE masters ADD INDEX idx_masters_permanent_offline (permanently_offline)',
  'SELECT 1'
);
PREPARE stmt_idx_masters_permanent_offline FROM @sql_idx_masters_permanent_offline;
EXECUTE stmt_idx_masters_permanent_offline;
DEALLOCATE PREPARE stmt_idx_masters_permanent_offline;

SET @idx_masters_plant_kind_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND index_name = 'idx_masters_plant_kind'
);
SET @sql_idx_masters_plant_kind := IF(
  @idx_masters_plant_kind_exists = 0,
  'ALTER TABLE masters ADD INDEX idx_masters_plant_kind (plant_kind)',
  'SELECT 1'
);
PREPARE stmt_idx_masters_plant_kind FROM @sql_idx_masters_plant_kind;
EXECUTE stmt_idx_masters_plant_kind;
DEALLOCATE PREPARE stmt_idx_masters_plant_kind;

SET @idx_device_serials_device_mode_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_device_mode'
);
SET @sql_idx_device_serials_device_mode := IF(
  @idx_device_serials_device_mode_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_device_mode (device_mode)',
  'SELECT 1'
);
PREPARE stmt_idx_device_serials_device_mode FROM @sql_idx_device_serials_device_mode;
EXECUTE stmt_idx_device_serials_device_mode;
DEALLOCATE PREPARE stmt_idx_device_serials_device_mode;

COMMIT;
