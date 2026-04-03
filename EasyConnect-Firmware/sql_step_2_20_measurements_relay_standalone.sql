-- Step 2.20 - Telemetria Relay Standalone (UVC)
-- Eseguire su phpMyAdmin. Se alcune ALTER falliscono per "duplicate column/index", ignorare.

ALTER TABLE `measurements`
  ADD COLUMN `device_type` varchar(24) DEFAULT NULL AFTER `fw_version`,
  ADD COLUMN `relay_mode` tinyint(3) unsigned DEFAULT NULL AFTER `device_type`,
  ADD COLUMN `relay_online` tinyint(1) DEFAULT NULL AFTER `relay_mode`,
  ADD COLUMN `relay_on` tinyint(1) DEFAULT NULL AFTER `relay_online`,
  ADD COLUMN `relay_safety_closed` tinyint(1) DEFAULT NULL AFTER `relay_on`,
  ADD COLUMN `relay_feedback_ok` tinyint(1) DEFAULT NULL AFTER `relay_safety_closed`,
  ADD COLUMN `relay_feedback_fault` tinyint(1) DEFAULT NULL AFTER `relay_feedback_ok`,
  ADD COLUMN `relay_safety_alarm` tinyint(1) DEFAULT NULL AFTER `relay_feedback_fault`,
  ADD COLUMN `relay_lifetime_alarm` tinyint(1) DEFAULT NULL AFTER `relay_safety_alarm`,
  ADD COLUMN `relay_lamp_fault` tinyint(1) DEFAULT NULL AFTER `relay_lifetime_alarm`,
  ADD COLUMN `relay_life_limit_hours` int(10) unsigned DEFAULT NULL AFTER `relay_lamp_fault`,
  ADD COLUMN `relay_hours_on` decimal(10,2) DEFAULT NULL AFTER `relay_life_limit_hours`,
  ADD COLUMN `relay_hours_remaining` decimal(10,2) DEFAULT NULL AFTER `relay_hours_on`,
  ADD COLUMN `relay_starts` int(10) unsigned DEFAULT NULL AFTER `relay_hours_remaining`,
  ADD COLUMN `relay_state` varchar(20) DEFAULT NULL AFTER `relay_starts`;

ALTER TABLE `measurements`
  ADD INDEX `idx_measurements_master_relay_time` (`master_id`, `slave_sn`, `recorded_at`);
