-- Step 2.21 - Permessi funzionali per utente
-- Eseguire su phpMyAdmin. Se alcune ALTER falliscono per "duplicate column", ignorare.

ALTER TABLE `users`
  ADD COLUMN `can_firmware_update` tinyint(1) NOT NULL DEFAULT 0 AFTER `portal_access_level`,
  ADD COLUMN `can_create_plants` tinyint(1) NOT NULL DEFAULT 0 AFTER `can_firmware_update`,
  ADD COLUMN `can_manage_serial_lifecycle` tinyint(1) NOT NULL DEFAULT 0 AFTER `can_create_plants`,
  ADD COLUMN `can_reserve_serials` tinyint(1) NOT NULL DEFAULT 0 AFTER `can_manage_serial_lifecycle`,
  ADD COLUMN `can_assign_manual_peripherals` tinyint(1) NOT NULL DEFAULT 0 AFTER `can_reserve_serials`;
