-- Step 2.11 - Comandi remoti configurazione periferiche + tracciamento slave_id
-- Eseguire su phpMyAdmin. Se alcune ALTER falliscono per "duplicate column/index", ignorare.

ALTER TABLE `measurements`
  ADD COLUMN `slave_id` int(11) DEFAULT NULL AFTER `slave_grp`;

ALTER TABLE `measurements`
  ADD INDEX `idx_measurements_master_slaveid_time` (`master_id`, `slave_id`, `recorded_at`);

CREATE TABLE IF NOT EXISTS `device_commands` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `master_id` int(11) NOT NULL,
  `target_serial` varchar(32) NOT NULL,
  `command_type` varchar(40) NOT NULL,
  `payload_json` text NOT NULL,
  `status` varchar(20) NOT NULL DEFAULT 'pending',
  `attempt_count` int(11) NOT NULL DEFAULT 0,
  `created_by_user_id` int(11) DEFAULT NULL,
  `created_at` timestamp NULL DEFAULT current_timestamp(),
  `sent_at` timestamp NULL DEFAULT NULL,
  `completed_at` timestamp NULL DEFAULT NULL,
  `result_message` text DEFAULT NULL,
  `result_json` text DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_device_commands_master_status` (`master_id`, `status`),
  KEY `idx_device_commands_target` (`target_serial`),
  KEY `idx_device_commands_created` (`created_at`),
  CONSTRAINT `fk_device_commands_master` FOREIGN KEY (`master_id`) REFERENCES `masters` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_device_commands_user` FOREIGN KEY (`created_by_user_id`) REFERENCES `users` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

