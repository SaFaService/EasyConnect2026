-- Step 2.6 - Serial Lifecycle (retired / voided) + motivazioni + eventi
-- ======================================================================
-- DB target: antralux_iot (MariaDB 10.11+)
-- Eseguire da phpMyAdmin prima di usare l'azione API set_serial_status.

START TRANSACTION;

-- 1) Motivazioni predefinite (combobox guidata)
CREATE TABLE IF NOT EXISTS `serial_status_reasons` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `reason_code` varchar(64) NOT NULL,
  `label_it` varchar(128) NOT NULL,
  `label_en` varchar(128) NOT NULL,
  `applies_to_status` enum('any','active','retired','voided') NOT NULL DEFAULT 'any',
  `sort_order` int(11) NOT NULL DEFAULT 100,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_at` timestamp NULL DEFAULT current_timestamp(),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_serial_status_reasons_code` (`reason_code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `serial_status_reasons` (`reason_code`, `label_it`, `label_en`, `applies_to_status`, `sort_order`, `is_active`)
VALUES
('wrong_product_type', 'Tipo prodotto errato', 'Wrong product type', 'voided', 10, 1),
('wrong_flashing', 'Programmazione errata', 'Wrong flashing', 'voided', 20, 1),
('factory_test_discard', 'Scarto collaudo fabbrica', 'Factory test discard', 'voided', 30, 1),
('field_replaced', 'Sostituzione in campo', 'Field replacement', 'retired', 40, 1),
('damaged', 'Dismesso per guasto', 'Dismissed due to fault', 'retired', 50, 1),
('plant_dismission', 'Impianto dismesso', 'Plant decommissioned', 'retired', 60, 1),
('master_replaced', 'Sostituito da altro seriale', 'Replaced by another serial', 'retired', 70, 1),
('master_bind', 'Assegnato a master', 'Bound to master', 'active', 80, 1)
ON DUPLICATE KEY UPDATE
  `label_it` = VALUES(`label_it`),
  `label_en` = VALUES(`label_en`),
  `applies_to_status` = VALUES(`applies_to_status`),
  `sort_order` = VALUES(`sort_order`),
  `is_active` = VALUES(`is_active`);

-- 2) Estensioni lifecycle su device_serials
ALTER TABLE `device_serials`
  ADD COLUMN IF NOT EXISTS `status_reason_code` varchar(64) DEFAULT NULL AFTER `status`,
  ADD COLUMN IF NOT EXISTS `status_notes` text DEFAULT NULL AFTER `status_reason_code`,
  ADD COLUMN IF NOT EXISTS `replaced_by_serial` varchar(32) DEFAULT NULL AFTER `status_notes`,
  ADD COLUMN IF NOT EXISTS `status_changed_at` timestamp NULL DEFAULT NULL AFTER `replaced_by_serial`,
  ADD COLUMN IF NOT EXISTS `status_changed_by_user_id` int(11) DEFAULT NULL AFTER `status_changed_at`,
  ADD COLUMN IF NOT EXISTS `activated_at` timestamp NULL DEFAULT NULL AFTER `status_changed_by_user_id`,
  ADD COLUMN IF NOT EXISTS `deactivated_at` timestamp NULL DEFAULT NULL AFTER `activated_at`,
  ADD COLUMN IF NOT EXISTS `owner_user_id` int(11) DEFAULT NULL AFTER `deactivated_at`;

-- Indici opzionali (creati solo se non presenti)
SET @idx1_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_status'
);
SET @sql_idx1 := IF(
  @idx1_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_status (status)',
  'SELECT 1'
);
PREPARE stmt_idx1 FROM @sql_idx1;
EXECUTE stmt_idx1;
DEALLOCATE PREPARE stmt_idx1;

SET @idx2_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_reason'
);
SET @sql_idx2 := IF(
  @idx2_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_reason (status_reason_code)',
  'SELECT 1'
);
PREPARE stmt_idx2 FROM @sql_idx2;
EXECUTE stmt_idx2;
DEALLOCATE PREPARE stmt_idx2;

SET @idx3_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_replaced'
);
SET @sql_idx3 := IF(
  @idx3_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_replaced (replaced_by_serial)',
  'SELECT 1'
);
PREPARE stmt_idx3 FROM @sql_idx3;
EXECUTE stmt_idx3;
DEALLOCATE PREPARE stmt_idx3;

SET @idx4_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND index_name = 'idx_device_serials_owner'
);
SET @sql_idx4 := IF(
  @idx4_exists = 0,
  'ALTER TABLE device_serials ADD INDEX idx_device_serials_owner (owner_user_id)',
  'SELECT 1'
);
PREPARE stmt_idx4 FROM @sql_idx4;
EXECUTE stmt_idx4;
DEALLOCATE PREPARE stmt_idx4;

-- FK opzionali (creati solo se non presenti)
SET @fk1_exists := (
  SELECT COUNT(*)
  FROM information_schema.table_constraints
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND constraint_name = 'fk_device_serials_reason_code'
);
SET @sql_fk1 := IF(
  @fk1_exists = 0,
  'ALTER TABLE device_serials ADD CONSTRAINT fk_device_serials_reason_code FOREIGN KEY (status_reason_code) REFERENCES serial_status_reasons(reason_code) ON UPDATE CASCADE ON DELETE SET NULL',
  'SELECT 1'
);
PREPARE stmt_fk1 FROM @sql_fk1;
EXECUTE stmt_fk1;
DEALLOCATE PREPARE stmt_fk1;

SET @fk2_exists := (
  SELECT COUNT(*)
  FROM information_schema.table_constraints
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND constraint_name = 'fk_device_serials_status_changed_by'
);
SET @sql_fk2 := IF(
  @fk2_exists = 0,
  'ALTER TABLE device_serials ADD CONSTRAINT fk_device_serials_status_changed_by FOREIGN KEY (status_changed_by_user_id) REFERENCES users(id) ON UPDATE CASCADE ON DELETE SET NULL',
  'SELECT 1'
);
PREPARE stmt_fk2 FROM @sql_fk2;
EXECUTE stmt_fk2;
DEALLOCATE PREPARE stmt_fk2;

SET @fk3_exists := (
  SELECT COUNT(*)
  FROM information_schema.table_constraints
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND constraint_name = 'fk_device_serials_owner'
);
SET @sql_fk3 := IF(
  @fk3_exists = 0,
  'ALTER TABLE device_serials ADD CONSTRAINT fk_device_serials_owner FOREIGN KEY (owner_user_id) REFERENCES users(id) ON UPDATE CASCADE ON DELETE SET NULL',
  'SELECT 1'
);
PREPARE stmt_fk3 FROM @sql_fk3;
EXECUTE stmt_fk3;
DEALLOCATE PREPARE stmt_fk3;

SET @fk4_exists := (
  SELECT COUNT(*)
  FROM information_schema.table_constraints
  WHERE table_schema = DATABASE()
    AND table_name = 'device_serials'
    AND constraint_name = 'fk_device_serials_replaced'
);
SET @sql_fk4 := IF(
  @fk4_exists = 0,
  'ALTER TABLE device_serials ADD CONSTRAINT fk_device_serials_replaced FOREIGN KEY (replaced_by_serial) REFERENCES device_serials(serial_number) ON UPDATE CASCADE ON DELETE SET NULL',
  'SELECT 1'
);
PREPARE stmt_fk4 FROM @sql_fk4;
EXECUTE stmt_fk4;
DEALLOCATE PREPARE stmt_fk4;

-- 3) Log eventi lifecycle (timeline seriale)
CREATE TABLE IF NOT EXISTS `serial_lifecycle_events` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `serial_number` varchar(32) NOT NULL,
  `from_status` varchar(32) DEFAULT NULL,
  `to_status` varchar(32) NOT NULL,
  `reason_code` varchar(64) DEFAULT NULL,
  `reason_details` text DEFAULT NULL,
  `replaced_by_serial` varchar(32) DEFAULT NULL,
  `actor_user_id` int(11) NOT NULL,
  `master_id` int(11) DEFAULT NULL,
  `created_at` timestamp NULL DEFAULT current_timestamp(),
  PRIMARY KEY (`id`),
  KEY `idx_sle_serial` (`serial_number`),
  KEY `idx_sle_to_status` (`to_status`),
  KEY `idx_sle_actor` (`actor_user_id`),
  KEY `idx_sle_master` (`master_id`),
  CONSTRAINT `fk_sle_serial` FOREIGN KEY (`serial_number`) REFERENCES `device_serials` (`serial_number`) ON UPDATE CASCADE ON DELETE CASCADE,
  CONSTRAINT `fk_sle_reason` FOREIGN KEY (`reason_code`) REFERENCES `serial_status_reasons` (`reason_code`) ON UPDATE CASCADE ON DELETE SET NULL,
  CONSTRAINT `fk_sle_actor` FOREIGN KEY (`actor_user_id`) REFERENCES `users` (`id`) ON UPDATE CASCADE ON DELETE RESTRICT,
  CONSTRAINT `fk_sle_master` FOREIGN KEY (`master_id`) REFERENCES `masters` (`id`) ON UPDATE CASCADE ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

COMMIT;
