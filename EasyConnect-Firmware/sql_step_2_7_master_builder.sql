-- Step 2.7 - Assegnazione costruttore su impianti (masters.builder_id)
-- ====================================================================
-- DB target: antralux_iot (MariaDB 10.11+)
-- Eseguire da phpMyAdmin prima di usare l'assegnazione Builder in settings.php

START TRANSACTION;

ALTER TABLE `masters`
  ADD COLUMN IF NOT EXISTS `builder_id` int(11) DEFAULT NULL AFTER `owner_id`;

SET @idx_builder_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND index_name = 'idx_masters_builder'
);
SET @sql_idx_builder := IF(
  @idx_builder_exists = 0,
  'ALTER TABLE masters ADD INDEX idx_masters_builder (builder_id)',
  'SELECT 1'
);
PREPARE stmt_idx_builder FROM @sql_idx_builder;
EXECUTE stmt_idx_builder;
DEALLOCATE PREPARE stmt_idx_builder;

SET @fk_builder_exists := (
  SELECT COUNT(*)
  FROM information_schema.table_constraints
  WHERE table_schema = DATABASE()
    AND table_name = 'masters'
    AND constraint_name = 'fk_masters_builder'
);
SET @sql_fk_builder := IF(
  @fk_builder_exists = 0,
  'ALTER TABLE masters ADD CONSTRAINT fk_masters_builder FOREIGN KEY (builder_id) REFERENCES users(id) ON UPDATE CASCADE ON DELETE SET NULL',
  'SELECT 1'
);
PREPARE stmt_fk_builder FROM @sql_fk_builder;
EXECUTE stmt_fk_builder;
DEALLOCATE PREPARE stmt_fk_builder;

COMMIT;
