<?php
session_start();
require 'config.php';

header('Content-Type: application/json');

function outJson(array $payload, int $httpCode = 200): void {
    http_response_code($httpCode);
    echo json_encode($payload);
    exit;
}

function columnExists(PDO $pdo, string $tableName, string $columnName): bool {
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

function normalizePlantKind($value): string {
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['display', 'standalone', 'rewamping'], true) ? $normalized : '';
}

function inferPlantKind(array $row, bool $hasPlantKindColumn): string {
    if ($hasPlantKindColumn) {
        $kind = normalizePlantKind($row['plant_kind'] ?? '');
        if ($kind !== '') {
            return $kind;
        }
    }
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', (string)($row['serial_number'] ?? ''), $m)) {
        if ((string)$m[1] === '01') {
            return 'display';
        }
    }
    return '';
}

if (!isset($_SESSION['user_id'], $_SESSION['user_role'])) {
    outJson(['status' => 'error', 'message' => 'Non autorizzato'], 401);
}

$userId = (int)$_SESSION['user_id'];
$userRole = (string)$_SESSION['user_role'];
$isAdmin = ($userRole === 'admin');
$hasBuilderColumn = columnExists($pdo, 'masters', 'builder_id');
$hasUsersCompanyColumn = columnExists($pdo, 'users', 'company');
$hasPermanentOfflineColumn = columnExists($pdo, 'masters', 'permanently_offline');
$hasPlantKindColumn = columnExists($pdo, 'masters', 'plant_kind');
$hasNotesColumn = columnExists($pdo, 'masters', 'notes');
$hasDeliveryDateColumn = columnExists($pdo, 'masters', 'delivery_date');

try {
    $ownerCompanySelect = $hasUsersCompanyColumn ? "o.company AS owner_company" : "NULL AS owner_company";
    $maintainerCompanySelect = $hasUsersCompanyColumn ? "mn.company AS maintainer_company" : "NULL AS maintainer_company";
    $builderCompanySelect = $hasUsersCompanyColumn ? "b.company AS builder_company" : "NULL AS builder_company";
    $builderJoin = $hasBuilderColumn ? "LEFT JOIN users b ON m.builder_id = b.id" : "";
    $builderSelect = $hasBuilderColumn ? ", b.email AS builder_email, {$builderCompanySelect}" : ", NULL AS builder_email, NULL AS builder_company";

    if ($isAdmin) {
        $sql = "
            SELECT
                m.id,
                m.nickname,
                m.address,
                m.serial_number,
                m.created_at,
                m.last_seen,
                m.fw_version,
                m.rssi,
                " . ($hasPermanentOfflineColumn ? "m.permanently_offline" : "0 AS permanently_offline") . ",
                " . ($hasPlantKindColumn ? "m.plant_kind" : "NULL AS plant_kind") . ",
                " . ($hasNotesColumn ? "m.notes" : "NULL AS notes") . ",
                " . ($hasDeliveryDateColumn ? "m.delivery_date" : "NULL AS delivery_date") . ",
                o.email AS owner_email,
                {$ownerCompanySelect},
                mn.email AS maintainer_email,
                {$maintainerCompanySelect}
                {$builderSelect}
            FROM masters m
            LEFT JOIN users o ON m.owner_id = o.id
            LEFT JOIN users mn ON m.maintainer_id = mn.id
            {$builderJoin}
            WHERE m.deleted_at IS NULL
            ORDER BY m.nickname ASC
        ";
        $stmt = $pdo->query($sql);
    } else {
        $where = "(m.creator_id = :uid_creator OR m.owner_id = :uid_owner OR m.maintainer_id = :uid_maintainer";
        if ($hasBuilderColumn) {
            $where .= " OR m.builder_id = :uid_builder";
        }
        $where .= ")";

        $sql = "
            SELECT
                m.id,
                m.nickname,
                m.address,
                m.serial_number,
                m.created_at,
                m.last_seen,
                m.fw_version,
                m.rssi,
                " . ($hasPermanentOfflineColumn ? "m.permanently_offline" : "0 AS permanently_offline") . ",
                " . ($hasPlantKindColumn ? "m.plant_kind" : "NULL AS plant_kind") . ",
                " . ($hasNotesColumn ? "m.notes" : "NULL AS notes") . ",
                " . ($hasDeliveryDateColumn ? "m.delivery_date" : "NULL AS delivery_date") . ",
                o.email AS owner_email,
                {$ownerCompanySelect},
                mn.email AS maintainer_email,
                {$maintainerCompanySelect}
                {$builderSelect}
            FROM masters m
            LEFT JOIN users o ON m.owner_id = o.id
            LEFT JOIN users mn ON m.maintainer_id = mn.id
            {$builderJoin}
            WHERE m.deleted_at IS NULL
              AND {$where}
            ORDER BY m.nickname ASC
        ";
        $stmt = $pdo->prepare($sql);
        $params = [
            'uid_creator' => $userId,
            'uid_owner' => $userId,
            'uid_maintainer' => $userId,
        ];
        if ($hasBuilderColumn) {
            $params['uid_builder'] = $userId;
        }
        $stmt->execute($params);
    }

    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);
    $now = time();
    $plants = [];

    foreach ($rows as $r) {
        $lastSeen = (string)($r['last_seen'] ?? '');
        $lastSeenTs = $lastSeen !== '' ? strtotime($lastSeen) : false;
        $isOnline = ($lastSeenTs !== false && $lastSeenTs >= ($now - 120));

        $plants[] = [
            'id' => (int)($r['id'] ?? 0),
            'name' => (string)($r['nickname'] ?? ''),
            'address' => (string)($r['address'] ?? ''),
            'serial_number' => (string)($r['serial_number'] ?? ''),
            'created_at' => (string)($r['created_at'] ?? ''),
            'delivery_date' => (string)($r['delivery_date'] ?? ''),
            'notes' => (string)($r['notes'] ?? ''),
            'online' => $isOnline,
            'permanently_offline' => ((int)($r['permanently_offline'] ?? 0) === 1),
            'plant_kind' => inferPlantKind($r, $hasPlantKindColumn),
            'firmware_version' => (string)($r['fw_version'] ?? ''),
            'rssi' => (string)($r['rssi'] ?? ''),
            'updated_at' => $lastSeen,
            'owner_email' => (string)($r['owner_email'] ?? ''),
            'owner_company' => (string)($r['owner_company'] ?? ''),
            'maintainer_email' => (string)($r['maintainer_email'] ?? ''),
            'maintainer_company' => (string)($r['maintainer_company'] ?? ''),
            'builder_email' => (string)($r['builder_email'] ?? ''),
            'builder_company' => (string)($r['builder_company'] ?? ''),
        ];
    }

    outJson(['status' => 'ok', 'plants' => $plants]);
} catch (Throwable $e) {
    outJson(['status' => 'error', 'message' => 'Errore DB: ' . $e->getMessage()], 500);
}
