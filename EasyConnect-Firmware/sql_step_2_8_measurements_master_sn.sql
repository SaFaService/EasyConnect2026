-- Step 2.8 - Tracciamento seriale master nei log misure
-- Necessario per distinguere online/offline tra seriale DB e seriale LIVE in dashboard.

ALTER TABLE `measurements`
  ADD COLUMN IF NOT EXISTS `master_sn` VARCHAR(32) NULL AFTER `master_id`;

CREATE INDEX IF NOT EXISTS `idx_measurements_master_sn_time`
  ON `measurements` (`master_id`, `master_sn`, `recorded_at`);
