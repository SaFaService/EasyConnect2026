-- Step 2.14 - Sessioni campionamento DeltaP su portale
-- Data: 2026-02-27

CREATE TABLE IF NOT EXISTS deltap_test_sessions (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    master_id INT NOT NULL,
    master_sn VARCHAR(64) NULL,
    dirt_level TINYINT UNSIGNED NOT NULL,
    total_speeds TINYINT UNSIGNED NOT NULL,
    speed_index TINYINT UNSIGNED NOT NULL,
    status ENUM('running','completed','empty','aborted','error') NOT NULL DEFAULT 'running',
    started_measurement_id BIGINT UNSIGNED NULL,
    ended_measurement_id BIGINT UNSIGNED NULL,
    master_record_count INT UNSIGNED NULL,
    started_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ended_at DATETIME NULL,
    notes VARCHAR(255) NULL,
    created_by_user_id INT NULL,
    created_by_role VARCHAR(50) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_dpts_master_status (master_id, status),
    INDEX idx_dpts_master_started (master_id, started_at),
    INDEX idx_dpts_status (status),
    INDEX idx_dpts_measure_range (started_measurement_id, ended_measurement_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

