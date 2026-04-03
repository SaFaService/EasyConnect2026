<?php

/**
 * Helpers condivisi di autenticazione e audit accessi.
 * Usati sia dal login web sia dalle API desktop.
 */

function ecAuthTableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("
        SELECT 1
        FROM information_schema.tables
        WHERE table_schema = DATABASE()
          AND table_name = ?
        LIMIT 1
    ");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function ecAuthColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = DATABASE()
          AND table_name = ?
          AND column_name = ?
        LIMIT 1
    ");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

function ecAuthEnsureAccessLogTable(PDO $pdo): void {
    static $checked = false;
    if ($checked) {
        return;
    }
    $checked = true;

    try {
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS user_access_logs (
                id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
                user_id INT NULL,
                email VARCHAR(190) NULL,
                role VARCHAR(50) NULL,
                channel ENUM('web','desktop_app') NOT NULL DEFAULT 'web',
                status ENUM('success','failed','pending_2fa','denied_role') NOT NULL DEFAULT 'failed',
                ip_address VARCHAR(64) NULL,
                user_agent VARCHAR(255) NULL,
                details VARCHAR(255) NULL,
                created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_user_access_logs_created_at (created_at),
                INDEX idx_user_access_logs_user_id (user_id),
                INDEX idx_user_access_logs_channel (channel),
                INDEX idx_user_access_logs_status (status)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        ");
    } catch (Throwable $e) {
        // Se l'utente DB non ha permessi DDL, non blocchiamo il login.
    }
}

function ecAuthClientIp(): string {
    $candidates = [
        $_SERVER['HTTP_CF_CONNECTING_IP'] ?? null,
        $_SERVER['HTTP_X_FORWARDED_FOR'] ?? null,
        $_SERVER['REMOTE_ADDR'] ?? null,
    ];
    foreach ($candidates as $raw) {
        if (!$raw) {
            continue;
        }
        $ip = trim(explode(',', (string)$raw)[0]);
        if ($ip !== '') {
            return $ip;
        }
    }
    return '';
}

function ecAuthWriteAccessLog(
    PDO $pdo,
    ?int $userId,
    string $email,
    string $role,
    string $channel,
    string $status,
    string $details = ''
): void {
    ecAuthEnsureAccessLogTable($pdo);
    if (!ecAuthTableExists($pdo, 'user_access_logs')) {
        return;
    }

    $allowedChannels = ['web', 'desktop_app'];
    $allowedStatus = ['success', 'failed', 'pending_2fa', 'denied_role'];
    if (!in_array($channel, $allowedChannels, true)) {
        $channel = 'web';
    }
    if (!in_array($status, $allowedStatus, true)) {
        $status = 'failed';
    }

    $ip = ecAuthClientIp();
    $ua = substr((string)($_SERVER['HTTP_USER_AGENT'] ?? ''), 0, 255);
    $email = substr(trim($email), 0, 190);
    $role = substr(trim($role), 0, 50);
    $details = substr(trim($details), 0, 255);

    try {
        $stmt = $pdo->prepare("
            INSERT INTO user_access_logs (
                user_id, email, role, channel, status, ip_address, user_agent, details
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ");
        $stmt->execute([
            $userId,
            $email !== '' ? $email : null,
            $role !== '' ? $role : null,
            $channel,
            $status,
            $ip !== '' ? $ip : null,
            $ua !== '' ? $ua : null,
            $details !== '' ? $details : null,
        ]);
    } catch (Throwable $e) {
        // Audit non bloccante.
    }
}

function ecAuthFindUserByEmail(PDO $pdo, string $email): ?array {
    $hasPortalAccessLevel = ecAuthColumnExists($pdo, 'users', 'portal_access_level');
    $stmt = $pdo->prepare("
        SELECT id, email, password_hash, force_password_change, role, google_auth_secret
             " . ($hasPortalAccessLevel ? ", portal_access_level" : ", 'active' AS portal_access_level") . "
        FROM users
        WHERE email = ?
        LIMIT 1
    ");
    $stmt->execute([$email]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return is_array($row) ? $row : null;
}

function ecAuthPortalAccessLevel($rawLevel): string {
    $level = strtolower(trim((string)$rawLevel));
    if (!in_array($level, ['active', 'existing_only', 'blocked'], true)) {
        return 'active';
    }
    return $level;
}

function ecAuthPortalLoginAllowed(string $accessLevel): bool {
    $level = ecAuthPortalAccessLevel($accessLevel);
    return in_array($level, ['active', 'existing_only'], true);
}

function ecAuthCanReceiveNewAssignments(string $accessLevel): bool {
    return ecAuthPortalAccessLevel($accessLevel) === 'active';
}

function ecAuthPortalAccessLabel(string $accessLevel): string {
    switch (ecAuthPortalAccessLevel($accessLevel)) {
        case 'active':
            return 'Attivo';
        case 'existing_only':
            return 'Solo impianti esistenti';
        case 'blocked':
            return 'Bloccato';
        default:
            return 'Attivo';
    }
}

function ecAuthPortalAccessBadgeClass(string $accessLevel): string {
    switch (ecAuthPortalAccessLevel($accessLevel)) {
        case 'active':
            return 'bg-success';
        case 'existing_only':
            return 'bg-warning text-dark';
        case 'blocked':
            return 'bg-secondary';
        default:
            return 'bg-success';
    }
}

function ecAuthMakeUnusablePasswordHash(): string {
    try {
        return password_hash(bin2hex(random_bytes(32)), PASSWORD_DEFAULT);
    } catch (Throwable $e) {
        return password_hash(uniqid('antralux_unusable_', true), PASSWORD_DEFAULT);
    }
}

function ecAuthCurrentUserAccessLevel(PDO $pdo, int $userId): string {
    static $hasPortalAccessLevel = null;
    if ($hasPortalAccessLevel === null) {
        $hasPortalAccessLevel = ecAuthColumnExists($pdo, 'users', 'portal_access_level');
    }
    if (!$hasPortalAccessLevel) {
        return 'active';
    }
    $stmt = $pdo->prepare("SELECT portal_access_level FROM users WHERE id = ? LIMIT 1");
    $stmt->execute([$userId]);
    return ecAuthPortalAccessLevel($stmt->fetchColumn() ?: 'active');
}

function ecAuthPermissionColumns(): array {
    return [
        'firmware_update' => 'can_firmware_update',
        'plant_create' => 'can_create_plants',
        'serial_lifecycle' => 'can_manage_serial_lifecycle',
        'serial_reserve' => 'can_reserve_serials',
        'manual_peripheral' => 'can_assign_manual_peripherals',
    ];
}

function ecAuthPermissionFlag($rawValue): bool {
    if (is_bool($rawValue)) {
        return $rawValue;
    }
    if (is_numeric($rawValue)) {
        return ((int)$rawValue) === 1;
    }
    $normalized = strtolower(trim((string)$rawValue));
    return in_array($normalized, ['1', 'true', 'yes', 'on'], true);
}

function ecAuthPermissionRoleAllowed(string $permissionKey, string $role): bool {
    $roleNorm = ecAuthNormalizeRole($role);
    if ($roleNorm === 'admin') {
        return true;
    }
    switch ($permissionKey) {
        case 'firmware_update':
            return in_array($roleNorm, ['builder', 'maintainer'], true);
        case 'plant_create':
            return in_array($roleNorm, ['builder', 'maintainer'], true);
        case 'serial_lifecycle':
            return in_array($roleNorm, ['builder', 'maintainer'], true);
        case 'serial_reserve':
            return in_array($roleNorm, ['builder', 'maintainer'], true);
        case 'manual_peripheral':
            return in_array($roleNorm, ['builder', 'maintainer'], true);
        default:
            return false;
    }
}

function ecAuthLegacyPermissionDefault(string $permissionKey, string $role): bool {
    $roleNorm = ecAuthNormalizeRole($role);
    if ($roleNorm === 'admin') {
        return true;
    }
    return false;
}

function ecAuthCurrentUserPermissions(PDO $pdo, int $userId): array {
    static $cache = [];
    if (isset($cache[$userId])) {
        return $cache[$userId];
    }

    $defaults = [
        'firmware_update' => false,
        'plant_create' => false,
        'serial_lifecycle' => false,
        'serial_reserve' => false,
        'manual_peripheral' => false,
    ];

    try {
        $permColumns = ecAuthPermissionColumns();
        $availableCols = [];
        foreach ($permColumns as $permKey => $columnName) {
            if (ecAuthColumnExists($pdo, 'users', $columnName)) {
                $availableCols[$permKey] = $columnName;
            }
        }

        $selectParts = ['role'];
        foreach ($permColumns as $permKey => $columnName) {
            if (isset($availableCols[$permKey])) {
                $selectParts[] = $columnName;
            } else {
                $selectParts[] = '0 AS ' . $columnName;
            }
        }

        $stmt = $pdo->prepare("SELECT " . implode(', ', $selectParts) . " FROM users WHERE id = ? LIMIT 1");
        $stmt->execute([$userId]);
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        if (!$row) {
            $cache[$userId] = $defaults;
            return $cache[$userId];
        }

        $roleNorm = ecAuthNormalizeRole((string)($row['role'] ?? ''));
        if ($roleNorm === 'admin') {
            $cache[$userId] = [
                'firmware_update' => true,
                'plant_create' => true,
                'serial_lifecycle' => true,
                'serial_reserve' => true,
                'manual_peripheral' => true,
            ];
            return $cache[$userId];
        }

        $permissions = [];
        foreach ($permColumns as $permKey => $columnName) {
            if (!ecAuthPermissionRoleAllowed($permKey, $roleNorm)) {
                $permissions[$permKey] = false;
                continue;
            }
            if (isset($availableCols[$permKey])) {
                $permissions[$permKey] = ecAuthPermissionFlag($row[$columnName] ?? 0);
            } else {
                $permissions[$permKey] = ecAuthLegacyPermissionDefault($permKey, $roleNorm);
            }
        }

        $cache[$userId] = array_merge($defaults, $permissions);
        return $cache[$userId];
    } catch (Throwable $e) {
        $cache[$userId] = $defaults;
        return $cache[$userId];
    }
}

function ecAuthCurrentUserCan(PDO $pdo, int $userId, string $permissionKey): bool {
    $permissions = ecAuthCurrentUserPermissions($pdo, $userId);
    return !empty($permissions[$permissionKey]);
}

function ecAuthEnsureActivationTokensTable(PDO $pdo): void {
    static $checked = false;
    if ($checked) {
        return;
    }
    $checked = true;

    try {
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS user_activation_tokens (
                id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
                user_id INT NOT NULL,
                token_hash CHAR(64) NOT NULL,
                expires_at DATETIME NOT NULL,
                used_at DATETIME NULL,
                created_by_user_id INT NULL,
                created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uniq_user_activation_token_hash (token_hash),
                KEY idx_user_activation_tokens_user_id (user_id),
                KEY idx_user_activation_tokens_expires_at (expires_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        ");
    } catch (Throwable $e) {
        // Non blocchiamo il flusso se mancano permessi DDL.
    }
}

function ecAuthIssueActivationToken(PDO $pdo, int $userId, ?int $createdByUserId = null, int $ttlHours = 72): ?string {
    ecAuthEnsureActivationTokensTable($pdo);
    if (!ecAuthTableExists($pdo, 'user_activation_tokens')) {
        return null;
    }

    try {
        $rawToken = bin2hex(random_bytes(32));
    } catch (Throwable $e) {
        $rawToken = hash('sha256', uniqid('antralux_activation_', true));
    }
    $tokenHash = hash('sha256', $rawToken);

    $pdo->prepare("
        UPDATE user_activation_tokens
        SET used_at = COALESCE(used_at, NOW())
        WHERE user_id = ?
          AND used_at IS NULL
    ")->execute([$userId]);

    $stmt = $pdo->prepare("
        INSERT INTO user_activation_tokens (user_id, token_hash, expires_at, created_by_user_id)
        VALUES (?, ?, DATE_ADD(NOW(), INTERVAL ? HOUR), ?)
    ");
    $stmt->execute([$userId, $tokenHash, $ttlHours, $createdByUserId]);

    return $rawToken;
}

function ecAuthActivationUrl(string $token): string {
    $scriptName = str_replace('\\', '/', (string)($_SERVER['SCRIPT_NAME'] ?? ''));
    $basePath = trim((string)dirname($scriptName), '/.');
    $basePath = $basePath !== '' ? '/' . $basePath : '';
    $host = trim((string)($_SERVER['HTTP_HOST'] ?? ''));
    $scheme = (!empty($_SERVER['HTTPS']) && strtolower((string)$_SERVER['HTTPS']) !== 'off') ? 'https' : 'http';
    $relative = $basePath . '/activate_account.php?token=' . urlencode($token);
    if ($host === '') {
        return $relative;
    }
    return $scheme . '://' . $host . $relative;
}

function ecAuthSendActivationEmail(string $email, string $activationUrl): bool {
    $subject = 'Attivazione accesso portale Antralux';
    $body = "Ciao,\n\n"
        . "e stato autorizzato il tuo accesso al portale Antralux.\n"
        . "Per completare l'attivazione imposta la tua password dal seguente link:\n\n"
        . $activationUrl . "\n\n"
        . "Il link ha validita limitata. Se non riesci ad accedere, contatta l'amministratore.\n\n"
        . "Saluti,\n"
        . "Team Antralux";
    $headers = 'From: no-reply@antralux.com' . "\r\n"
        . 'Reply-To: no-reply@antralux.com' . "\r\n"
        . 'X-Mailer: PHP/' . phpversion();
    return @mail($email, $subject, $body, $headers);
}

function ecAuthNormalizeRole(string $role): string {
    $raw = strtolower(trim($role));
    if ($raw === '') {
        return '';
    }
    $map = [
        'admin' => 'admin',
        'administrator' => 'admin',
        'amministratore' => 'admin',
        'builder' => 'builder',
        'costruttore' => 'builder',
        'maintainer' => 'maintainer',
        'manutentore' => 'maintainer',
        'client' => 'client',
        'cliente' => 'client',
        'owner' => 'client',
    ];
    return $map[$raw] ?? $raw;
}

function ecAuthDesktopRoleAllowed(string $role): bool {
    $roleNorm = ecAuthNormalizeRole($role);
    return in_array($roleNorm, ['admin', 'builder', 'maintainer'], true);
}
