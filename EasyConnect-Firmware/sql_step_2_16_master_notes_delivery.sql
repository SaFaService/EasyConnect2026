-- Step 2.16 - Note impianto e data consegna
-- ============================================================

START TRANSACTION;

SET @col_masters_notes_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND column_name = 'notes'
);
SET @sql_masters_notes := IF(
  @col_masters_notes_exists = 0,
  'ALTER TABLE masters ADD COLUMN notes TEXT DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_masters_notes FROM @sql_masters_notes;
EXECUTE stmt_masters_notes;
DEALLOCATE PREPARE stmt_masters_notes;

SET @col_masters_delivery_date_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND column_name = 'delivery_date'
);
SET @sql_masters_delivery_date := IF(
  @col_masters_delivery_date_exists = 0,
  'ALTER TABLE masters ADD COLUMN delivery_date DATE DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_masters_delivery_date FROM @sql_masters_delivery_date;
EXECUTE stmt_masters_delivery_date;
DEALLOCATE PREPARE stmt_masters_delivery_date;

SET @idx_masters_delivery_date_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND index_name = 'idx_masters_delivery_date'
);
SET @sql_idx_masters_delivery_date := IF(
  @idx_masters_delivery_date_exists = 0,
  'ALTER TABLE masters ADD INDEX idx_masters_delivery_date (delivery_date)',
  'SELECT 1'
);
PREPARE stmt_idx_masters_delivery_date FROM @sql_idx_masters_delivery_date;
EXECUTE stmt_idx_masters_delivery_date;
DEALLOCATE PREPARE stmt_idx_masters_delivery_date;

COMMIT;
