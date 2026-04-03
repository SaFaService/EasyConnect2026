-- Step 2.22 - Permesso utente dedicato alla riserva seriali
-- Eseguire su phpMyAdmin.

SET @col_exists := (
    SELECT COUNT(*)
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'users'
      AND COLUMN_NAME = 'can_reserve_serials'
);

SET @sql := IF(
    @col_exists = 0,
    'ALTER TABLE `users` ADD COLUMN `can_reserve_serials` tinyint(1) NOT NULL DEFAULT 0 AFTER `can_manage_serial_lifecycle`',
    'SELECT ''can_reserve_serials already exists'' AS info'
);

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
