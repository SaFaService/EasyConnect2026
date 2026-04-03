-- Step 2.9 - Statistiche traffico API master (sessione/giorno/settimana)
-- ======================================================================
-- Scopo:
-- 1) Salvare nei log misure i byte TX/RX cumulativi sessione (dal boot master)
-- 2) Salvare i byte TX/RX per ciclo (delta tra campioni successivi)
-- 3) Abilitare dashboard/pagina seriale con statistiche traffico

START TRANSACTION;

ALTER TABLE `measurements`
  ADD COLUMN IF NOT EXISTS `api_tx_session_bytes` BIGINT UNSIGNED NULL AFTER `delta_p`,
  ADD COLUMN IF NOT EXISTS `api_rx_session_bytes` BIGINT UNSIGNED NULL AFTER `api_tx_session_bytes`,
  ADD COLUMN IF NOT EXISTS `api_posts_session_count` BIGINT UNSIGNED NULL AFTER `api_rx_session_bytes`,
  ADD COLUMN IF NOT EXISTS `api_tx_cycle_bytes` BIGINT UNSIGNED NULL AFTER `api_posts_session_count`,
  ADD COLUMN IF NOT EXISTS `api_rx_cycle_bytes` BIGINT UNSIGNED NULL AFTER `api_tx_cycle_bytes`;

SET @idx_traffic_exists := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name = 'measurements'
    AND index_name = 'idx_measurements_master_traffic_time'
);
SET @sql_idx_traffic := IF(
  @idx_traffic_exists = 0,
  'ALTER TABLE measurements ADD INDEX idx_measurements_master_traffic_time (master_id, recorded_at)',
  'SELECT 1'
);
PREPARE stmt_idx_traffic FROM @sql_idx_traffic;
EXECUTE stmt_idx_traffic;
DEALLOCATE PREPARE stmt_idx_traffic;

COMMIT;
