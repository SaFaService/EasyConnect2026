-- Step 2.18 - Referenti aggiuntivi utente
-- ============================================================

START TRANSACTION;

SET @col_users_extra_contacts_json_exists := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name = 'users'
    AND column_name = 'extra_contacts_json'
);
SET @sql_users_extra_contacts_json := IF(
  @col_users_extra_contacts_json_exists = 0,
  'ALTER TABLE users ADD COLUMN extra_contacts_json TEXT DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt_users_extra_contacts_json FROM @sql_users_extra_contacts_json;
EXECUTE stmt_users_extra_contacts_json;
DEALLOCATE PREPARE stmt_users_extra_contacts_json;

COMMIT;
