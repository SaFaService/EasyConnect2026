-- Step 2.12 - Reset statistiche traffico master (baseline)
-- Eseguire su phpMyAdmin. Se alcune chiavi esistono già, ignorare l'errore duplicato.

CREATE TABLE IF NOT EXISTS `master_stats_resets` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `master_id` int(11) NOT NULL,
  `reset_by_user_id` int(11) DEFAULT NULL,
  `reset_at` timestamp NULL DEFAULT current_timestamp(),
  `base_tx_session_bytes` bigint(20) DEFAULT NULL,
  `base_rx_session_bytes` bigint(20) DEFAULT NULL,
  `base_posts_session_count` bigint(20) DEFAULT NULL,
  `notes` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_master_stats_resets_master` (`master_id`, `reset_at`),
  KEY `idx_master_stats_resets_user` (`reset_by_user_id`),
  CONSTRAINT `fk_master_stats_resets_master` FOREIGN KEY (`master_id`) REFERENCES `masters` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_master_stats_resets_user` FOREIGN KEY (`reset_by_user_id`) REFERENCES `users` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

