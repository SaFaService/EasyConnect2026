-- Step 2.4 - Audit dedicato alle operazioni serial-centric
-- ========================================================
-- Questa tabella e' usata da WebSiteComunicazione/api_serial.php
-- per tracciare riserva, registrazione manuale e assegnazioni seriali.
--
-- Esegui questo script su phpMyAdmin nel DB antralux_iot.

CREATE TABLE IF NOT EXISTS `serial_audit_logs` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `actor_user_id` int(11) NOT NULL COMMENT 'Utente che ha effettuato l''operazione',
  `action` varchar(64) NOT NULL COMMENT 'Tipo azione: SERIAL_RESERVE, SERIAL_ASSIGN_MASTER, ...',
  `serial_number` varchar(32) DEFAULT NULL,
  `master_id` int(11) DEFAULT NULL,
  `details` text DEFAULT NULL,
  `created_at` timestamp NULL DEFAULT current_timestamp(),
  PRIMARY KEY (`id`),
  KEY `idx_serial_audit_actor` (`actor_user_id`),
  KEY `idx_serial_audit_serial` (`serial_number`),
  KEY `idx_serial_audit_master` (`master_id`),
  CONSTRAINT `fk_serial_audit_actor`
    FOREIGN KEY (`actor_user_id`) REFERENCES `users` (`id`)
    ON UPDATE CASCADE
    ON DELETE RESTRICT,
  CONSTRAINT `fk_serial_audit_master`
    FOREIGN KEY (`master_id`) REFERENCES `masters` (`id`)
    ON UPDATE CASCADE
    ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

