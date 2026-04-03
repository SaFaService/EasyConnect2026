-- Step 2.17 - Assegnazione manuale periferiche su impianti offline
-- ============================================================
-- Compatibile con MariaDB/MySQL senza IF NOT EXISTS multipli nello stesso ALTER.

START TRANSACTION;

SET @col_ds_assigned_user_id_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND column_name = 'assigned_user_id'
);
SET @sql_ds_assigned_user_id := IF(
  @col_ds_assigned_user_id_exists = 0,
  'ALTER TABLE device_serials ADD COLUMN assigned_user_id INT DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_ds_assigned_user_id FROM @sql_ds_assigned_user_id;
EXECUTE stmt_ds_assigned_user_id;
DEALLOCATE PREPARE stmt_ds_assigned_user_id;

SET @col_ds_assigned_role_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND column_name = 'assigned_role'
);
SET @sql_ds_assigned_role := IF(
  @col_ds_assigned_role_exists = 0,
  'ALTER TABLE device_serials ADD COLUMN assigned_role VARCHAR(32) DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_ds_assigned_role FROM @sql_ds_assigned_role;
EXECUTE stmt_ds_assigned_role;
DEALLOCATE PREPARE stmt_ds_assigned_role;

SET @col_ds_assigned_master_id_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND column_name = 'assigned_master_id'
);
SET @sql_ds_assigned_master_id := IF(
  @col_ds_assigned_master_id_exists = 0,
  'ALTER TABLE device_serials ADD COLUMN assigned_master_id INT DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_ds_assigned_master_id FROM @sql_ds_assigned_master_id;
EXECUTE stmt_ds_assigned_master_id;
DEALLOCATE PREPARE stmt_ds_assigned_master_id;

SET @col_ds_serial_locked_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND column_name = 'serial_locked'
);
SET @sql_ds_serial_locked := IF(
  @col_ds_serial_locked_exists = 0,
  'ALTER TABLE device_serials ADD COLUMN serial_locked TINYINT(1) NOT NULL DEFAULT 0',
  'SELECT 1'
);
PREPARE stmt_ds_serial_locked FROM @sql_ds_serial_locked;
EXECUTE stmt_ds_serial_locked;
DEALLOCATE PREPARE stmt_ds_serial_locked;

SET @col_ds_lock_source_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND column_name = 'lock_source'
);
SET @sql_ds_lock_source := IF(
  @col_ds_lock_source_exists = 0,
  'ALTER TABLE device_serials ADD COLUMN lock_source VARCHAR(32) DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_ds_lock_source FROM @sql_ds_lock_source;
EXECUTE stmt_ds_lock_source;
DEALLOCATE PREPARE stmt_ds_lock_source;

SET @idx_ds_assigned_master_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_assigned_master'
);
SET @sql_idx_ds_assigned_master := IF(
  @idx_ds_assigned_master_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_assigned_master (assigned_master_id)',
  'SELECT 1'
);
PREPARE stmt_idx_ds_assigned_master FROM @sql_idx_ds_assigned_master;
EXECUTE stmt_idx_ds_assigned_master;
DEALLOCATE PREPARE stmt_idx_ds_assigned_master;

SET @idx_ds_lock_source_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_lock_source'
);
SET @sql_idx_ds_lock_source := IF(
  @idx_ds_lock_source_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_lock_source (lock_source)',
  'SELECT 1'
);
PREPARE stmt_idx_ds_lock_source FROM @sql_idx_ds_lock_source;
EXECUTE stmt_idx_ds_lock_source;
DEALLOCATE PREPARE stmt_idx_ds_lock_source;

COMMIT;
