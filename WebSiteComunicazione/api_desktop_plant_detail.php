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

function tableExists(PDO $pdo, string $tableName): bool {
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

function partyFromPrefix(array $row, string $prefix): array {
    return [
        'name' => (string)($row[$prefix . '_name'] ?? ''),
        'email' => (string)($row[$prefix . '_email'] ?? ''),
        'phone' => (string)($row[$prefix . '_phone'] ?? ''),
        'company' => (string)($row[$prefix . '_company'] ?? ''),
    ];
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
$canEditPlant = in_array($userRole, ['admin', 'builder', 'maintainer'], true);
$hasBuilderColumn = columnExists($pdo, 'masters', 'builder_id');
$hasUsersCompanyColumn = columnExists($pdo, 'users', 'company');
$hasUsersNameColumn = columnExists($pdo, 'users', 'name');
$hasHumidity = columnExists($pdo, 'measurements', 'humidity');
$hasDeviceSerials = tableExists($pdo, 'device_serials');
$hasDeviceSerialStatus = $hasDeviceSerials ? columnExists($pdo, 'device_serials', 'status') : false;
$hasProductTypes = tableExists($pdo, 'product_types');
$hasPermanentOfflineColumn = columnExists($pdo, 'masters', 'permanently_offline');
$hasPlantKindColumn = columnExists($pdo, 'masters', 'plant_kind');
$hasNotesColumn = columnExists($pdo, 'masters', 'notes');
$hasDeliveryDateColumn = columnExists($pdo, 'masters', 'delivery_date');

$plantId = (int)($_GET['plant_id'] ?? 0);
if ($plantId <= 0) {
    outJson(['status' => 'error', 'message' => 'plant_id non valido'], 400);
}

try {
    $companyOwner = $hasUsersCompanyColumn ? "o.company AS owner_company" : "NULL AS owner_company";
    $companyMaint = $hasUsersCompanyColumn ? "mn.company AS maintainer_company" : "NULL AS maintainer_company";
    $companyBuilder = $hasUsersCompanyColumn ? "b.company AS builder_company" : "NULL AS builder_company";
    $companyCreator = $hasUsersCompanyColumn ? "c.company AS creator_company" : "NULL AS creator_company";
    $ownerNameSelect = $hasUsersNameColumn ? "o.name AS owner_name" : "NULL AS owner_name";
    $maintNameSelect = $hasUsersNameColumn ? "mn.name AS maintainer_name" : "NULL AS maintainer_name";
    $creatorNameSelect = $hasUsersNameColumn ? "c.name AS creator_name" : "NULL AS creator_name";
    $builderNameSelect = $hasUsersNameColumn ? "b.name AS builder_name" : "NULL AS builder_name";
    $builderJoin = $hasBuilderColumn ? "LEFT JOIN users b ON m.builder_id = b.id" : "";
    $builderSelect = $hasBuilderColumn
        ? ", {$builderNameSelect}, b.email AS builder_email, b.phone AS builder_phone, {$companyBuilder}"
        : ", NULL AS builder_name, NULL AS builder_email, NULL AS builder_phone, NULL AS builder_company";

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
            {$ownerNameSelect},
            o.email AS owner_email,
            o.phone AS owner_phone,
            {$companyOwner},
            {$maintNameSelect},
            mn.email AS maintainer_email,
            mn.phone AS maintainer_phone,
            {$companyMaint},
            {$creatorNameSelect},
            c.email AS creator_email,
            c.phone AS creator_phone,
            {$companyCreator}
            {$builderSelect}
        FROM masters m
        LEFT JOIN users o ON m.owner_id = o.id
        LEFT JOIN users mn ON m.maintainer_id = mn.id
        LEFT JOIN users c ON m.creator_id = c.id
        {$builderJoin}
        WHERE m.id = :plant_id
          AND m.deleted_at IS NULL
    ";

    if (!$isAdmin) {
        $sql .= " AND (
            m.creator_id = :uid_creator
            OR m.owner_id = :uid_owner
            OR m.maintainer_id = :uid_maintainer";
        if ($hasBuilderColumn) {
            $sql .= " OR m.builder_id = :uid_builder";
        }
        $sql .= ")";
    }

    $stmt = $pdo->prepare($sql);
    $params = ['plant_id' => $plantId];
    if (!$isAdmin) {
        $params['uid_creator'] = $userId;
        $params['uid_owner'] = $userId;
        $params['uid_maintainer'] = $userId;
        if ($hasBuilderColumn) {
            $params['uid_builder'] = $userId;
        }
    }
    $stmt->execute($params);
    $master = $stmt->fetch(PDO::FETCH_ASSOC);

    if (!$master) {
        outJson(['status' => 'error', 'message' => 'Impianto non trovato o non autorizzato'], 404);
    }

    $hSelect = $hasHumidity ? "m1.humidity" : "NULL AS humidity";
    $serialJoin = $hasDeviceSerials ? "LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn" : "";
    $productJoin = ($hasDeviceSerials && $hasProductTypes) ? "LEFT JOIN product_types pt ON pt.code = ds.product_type_code" : "";
    $ptypeSelect = $hasDeviceSerials ? "ds.product_type_code" : "NULL AS product_type_code";
    $plabelSelect = ($hasDeviceSerials && $hasProductTypes) ? "pt.label AS product_label" : "NULL AS product_label";
    $statusFilter = $hasDeviceSerialStatus ? "(ds.serial_number IS NULL OR ds.status NOT IN ('retired','voided'))" : "1=1";

    $sqlPeripherals = "
        SELECT
            m1.slave_sn,
            m1.slave_grp,
            m1.pressure,
            m1.temperature,
            {$hSelect},
            m1.fw_version,
            m1.recorded_at,
            {$ptypeSelect},
            {$plabelSelect}
        FROM measurements m1
        INNER JOIN (
            SELECT slave_sn, MAX(recorded_at) AS max_date
            FROM measurements
            WHERE master_id = :mid_a
              AND slave_sn IS NOT NULL
              AND slave_sn <> ''
              AND slave_sn <> '0'
              AND recorded_at >= (NOW() - INTERVAL 30 DAY)
            GROUP BY slave_sn
        ) m2 ON m1.slave_sn = m2.slave_sn AND m1.recorded_at = m2.max_date
        {$serialJoin}
        {$productJoin}
        WHERE m1.master_id = :mid_b
          AND ({$statusFilter})
        ORDER BY m1.slave_grp ASC, m1.slave_sn ASC
    ";
    $stmtPeriph = $pdo->prepare($sqlPeripherals);
    $stmtPeriph->execute([
        'mid_a' => (int)$master['id'],
        'mid_b' => (int)$master['id'],
    ]);
    $rows = $stmtPeriph->fetchAll(PDO::FETCH_ASSOC);

    $now = time();
    $peripherals = [];
    foreach ($rows as $r) {
        $lastSeen = (string)($r['recorded_at'] ?? '');
        $lastSeenTs = $lastSeen !== '' ? strtotime($lastSeen) : false;
        $online = (
            $lastSeenTs !== false
            && $lastSeenTs >= ($now - 75)
            && $r['pressure'] !== null
            && $r['temperature'] !== null
        );

        $ptype = strtolower(trim((string)($r['product_type_code'] ?? '')));
        $mode = '-';
        if ($ptype === '04' || strpos($ptype, 'pressure') !== false) {
            $mode = 'Aspirazione/Immissione';
        } elseif ($ptype === '03' || strpos($ptype, 'relay') !== false) {
            $mode = 'Relay';
        }

        $peripherals[] = [
            'serial_number' => (string)($r['slave_sn'] ?? ''),
            'board_type' => $ptype !== '' ? $ptype : 'unknown',
            'board_label' => (string)($r['product_label'] ?? 'Periferica'),
            'group' => (string)($r['slave_grp'] ?? ''),
            'mode' => $mode,
            'firmware_version' => (string)($r['fw_version'] ?? ''),
            'pressure' => $r['pressure'] === null ? '-' : (string)$r['pressure'],
            'temperature' => $r['temperature'] === null ? '-' : (string)$r['temperature'],
            'humidity' => $r['humidity'] === null ? '-' : (string)$r['humidity'],
            'last_seen' => $lastSeen,
            'online' => $online,
        ];
    }

    $lastSeen = (string)($master['last_seen'] ?? '');
    $masterTs = $lastSeen !== '' ? strtotime($lastSeen) : false;
    $masterOnline = ($masterTs !== false && $masterTs >= ($now - 120));

    outJson([
        'status' => 'ok',
        'plant' => [
            'id' => (int)$master['id'],
            'name' => (string)($master['nickname'] ?? ''),
            'address' => (string)($master['address'] ?? ''),
            'serial_number' => (string)($master['serial_number'] ?? ''),
            'created_at' => (string)($master['created_at'] ?? ''),
            'delivery_date' => (string)($master['delivery_date'] ?? ''),
            'notes' => (string)($master['notes'] ?? ''),
            'online' => $masterOnline,
            'permanently_offline' => ((int)($master['permanently_offline'] ?? 0) === 1),
            'plant_kind' => inferPlantKind($master, $hasPlantKindColumn),
            'firmware_version' => (string)($master['fw_version'] ?? ''),
            'rssi' => (string)($master['rssi'] ?? ''),
            'updated_at' => $lastSeen,
        ],
        'owner' => partyFromPrefix($master, 'owner'),
        'maintainer' => partyFromPrefix($master, 'maintainer'),
        'builder' => partyFromPrefix($master, 'builder'),
        'creator' => partyFromPrefix($master, 'creator'),
        'peripherals' => $peripherals,
        'can_edit' => $canEditPlant,
    ]);
} catch (Throwable $e) {
    outJson(['status' => 'error', 'message' => 'Errore DB: ' . $e->getMessage()], 500);
}
