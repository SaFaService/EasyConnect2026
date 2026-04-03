-- Step 2.19 - Firmware opzionale su anagrafica seriali
-- ============================================================

START TRANSACTION;

SET @col_ds_firmware_version_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND column_name = 'firmware_version'
);
SET @sql_ds_firmware_version := IF(
  @col_ds_firmware_version_exists = 0,
  'ALTER TABLE device_serials ADD COLUMN firmware_version VARCHAR(32) DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_ds_firmware_version FROM @sql_ds_firmware_version;
EXECUTE stmt_ds_firmware_version;
DEALLOCATE PREPARE stmt_ds_firmware_version;

COMMIT;
