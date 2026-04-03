-- Step 2.13 - Tracciamento accessi Web/Desktop
-- Data: 2026-02-20

CREATE TABLE IF NOT EXISTS user_access_logs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    user_id INT NULL,
    email VARCHAR(190) NULL,
    role VARCHAR(50) NULL,
    channel ENUM('web', 'desktop_app') NOT NULL DEFAULT 'web',
    status ENUM('success', 'failed', 'pending_2fa', 'denied_role') NOT NULL DEFAULT 'failed',
    ip_address VARCHAR(64) NULL,
    user_agent VARCHAR(255) NULL,
    details VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user_access_logs_created_at (created_at),
    INDEX idx_user_access_logs_user_id (user_id),
    INDEX idx_user_access_logs_channel (channel),
    INDEX idx_user_access_logs_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
