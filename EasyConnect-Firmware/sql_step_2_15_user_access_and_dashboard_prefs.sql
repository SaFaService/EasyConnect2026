-- Step 2.15 - Stato accesso utenti, profilo esteso e attivazione account
-- ============================================================
-- Compatibile con MariaDB/MySQL che non supportano bene
-- ADD COLUMN IF NOT EXISTS multipli nello stesso ALTER TABLE.

START TRANSACTION;

SET @col_users_portal_access_level_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND column_name = 'portal_access_level'
);
SET @sql_users_portal_access_level := IF(
  @col_users_portal_access_level_exists = 0,
  'ALTER TABLE users ADD COLUMN portal_access_level VARCHAR(20) NOT NULL DEFAULT ''active''',
  'SELECT 1'
);
PREPARE stmt_users_portal_access_level FROM @sql_users_portal_access_level;
EXECUTE stmt_users_portal_access_level;
DEALLOCATE PREPARE stmt_users_portal_access_level;

SET @col_users_address_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND column_name = 'address'
);
SET @sql_users_address := IF(
  @col_users_address_exists = 0,
  'ALTER TABLE users ADD COLUMN address VARCHAR(255) DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_users_address FROM @sql_users_address;
EXECUTE stmt_users_address;
DEALLOCATE PREPARE stmt_users_address;

SET @col_users_vat_number_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND column_name = 'vat_number'
);
SET @sql_users_vat_number := IF(
  @col_users_vat_number_exists = 0,
  'ALTER TABLE users ADD COLUMN vat_number VARCHAR(64) DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_users_vat_number FROM @sql_users_vat_number;
EXECUTE stmt_users_vat_number;
DEALLOCATE PREPARE stmt_users_vat_number;

SET @col_users_dashboard_filter_prefs_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND column_name = 'dashboard_filter_prefs'
);
SET @sql_users_dashboard_filter_prefs := IF(
  @col_users_dashboard_filter_prefs_exists = 0,
  'ALTER TABLE users ADD COLUMN dashboard_filter_prefs TEXT DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_users_dashboard_filter_prefs FROM @sql_users_dashboard_filter_prefs;
EXECUTE stmt_users_dashboard_filter_prefs;
DEALLOCATE PREPARE stmt_users_dashboard_filter_prefs;

SET @idx_users_portal_access_level_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND index_name = 'idx_users_portal_access_level'
);
SET @sql_idx_users_portal_access_level := IF(
  @idx_users_portal_access_level_exists = 0,
  'ALTER TABLE users ADD INDEX idx_users_portal_access_level (portal_access_level)',
  'SELECT 1'
);
PREPARE stmt_idx_users_portal_access_level FROM @sql_idx_users_portal_access_level;
EXECUTE stmt_idx_users_portal_access_level;
DEALLOCATE PREPARE stmt_idx_users_portal_access_level;

SET @idx_users_vat_number_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND index_name = 'idx_users_vat_number'
);
SET @sql_idx_users_vat_number := IF(
  @idx_users_vat_number_exists = 0,
  'ALTER TABLE users ADD INDEX idx_users_vat_number (vat_number)',
  'SELECT 1'
);
PREPARE stmt_idx_users_vat_number FROM @sql_idx_users_vat_number;
EXECUTE stmt_idx_users_vat_number;
DEALLOCATE PREPARE stmt_idx_users_vat_number;

CREATE TABLE IF NOT EXISTS user_activation_tokens (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    token_hash CHAR(64) NOT NULL,
    expires_at DATETIME NOT NULL,
    used_at DATETIME DEFAULT NULL,
    created_by_user_id INT DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uniq_user_activation_token_hash (token_hash),
    KEY idx_user_activation_tokens_user_id (user_id),
    KEY idx_user_activation_tokens_expires_at (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

COMMIT;
