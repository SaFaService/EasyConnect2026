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

function boolFromInput($value): int {
    if (is_bool($value)) {
        return $value ? 1 : 0;
    }
    if (is_numeric($value)) {
        return ((int)$value) === 1 ? 1 : 0;
    }
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['1', 'true', 'on', 'yes', 'si'], true) ? 1 : 0;
}

function normalizePlantKind($value): string {
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['display', 'standalone', 'rewamping'], true) ? $normalized : '';
}

function serialType(string $serial): string {
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial, $m)) {
        return (string)$m[1];
    }
    return '';
}

function inferPlantKind(array $row, bool $hasPlantKindColumn): string {
    if ($hasPlantKindColumn) {
        $kind = normalizePlantKind($row['plant_kind'] ?? '');
        if ($kind !== '') {
            return $kind;
        }
    }
    if (serialType((string)($row['serial_number'] ?? '')) === '01') {
        return 'display';
    }
    return '';
}

if (!isset($_SESSION['user_id'], $_SESSION['user_role'])) {
    outJson(['status' => 'error', 'message' => 'Non autorizzato'], 401);
}

$userId = (int)$_SESSION['user_id'];
$userRole = (string)$_SESSION['user_role'];
if (!in_array($userRole, ['admin', 'builder', 'maintainer'], true)) {
    outJson(['status' => 'error', 'message' => 'Permessi insufficienti'], 403);
}

if (strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET')) !== 'POST') {
    outJson(['status' => 'error', 'message' => 'Metodo non consentito'], 405);
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    $input = $_POST;
}

$plantId = (int)($input['plant_id'] ?? 0);
$name = trim((string)($input['name'] ?? ''));
$address = trim((string)($input['address'] ?? ''));
$notes = trim((string)($input['notes'] ?? ''));
$deliveryDate = trim((string)($input['delivery_date'] ?? ''));
$permanentlyOffline = boolFromInput($input['permanently_offline'] ?? 0);
$plantKind = normalizePlantKind($input['plant_kind'] ?? '');
if ($plantId <= 0) {
    outJson(['status' => 'error', 'message' => 'plant_id non valido'], 400);
}
if ($name === '') {
    outJson(['status' => 'error', 'message' => 'Nome impianto obbligatorio'], 400);
}

$hasBuilderColumn = columnExists($pdo, 'masters', 'builder_id');
$hasPermanentOfflineColumn = columnExists($pdo, 'masters', 'permanently_offline');
$hasPlantKindColumn = columnExists($pdo, 'masters', 'plant_kind');
$hasNotesColumn = columnExists($pdo, 'masters', 'notes');
$hasDeliveryDateColumn = columnExists($pdo, 'masters', 'delivery_date');

try {
    $sql = "
        SELECT id, nickname, address, serial_number, last_seen, fw_version, rssi
        FROM masters
        WHERE id = :pid
          AND deleted_at IS NULL
    ";
    if ($userRole !== 'admin') {
        $sql .= " AND (
            creator_id = :uid_creator
            OR owner_id = :uid_owner
            OR maintainer_id = :uid_maintainer";
        if ($hasBuilderColumn) {
            $sql .= " OR builder_id = :uid_builder";
        }
        $sql .= ")";
    }
    $stmt = $pdo->prepare($sql);
    $params = ['pid' => $plantId];
    if ($userRole !== 'admin') {
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

    $serialType = serialType((string)($master['serial_number'] ?? ''));
    if ($plantKind !== '' && $serialType === '01' && $plantKind !== 'display') {
        outJson(['status' => 'error', 'message' => 'Per un seriale master di tipo 01 il tipo impianto deve essere Display.'], 400);
    }
    if ($plantKind !== '' && $serialType === '02' && !in_array($plantKind, ['standalone', 'rewamping'], true)) {
        outJson(['status' => 'error', 'message' => 'Per un seriale master di tipo 02 il tipo impianto deve essere Standalone o Rewamping.'], 400);
    }

    $setParts = ['nickname = ?', 'address = ?'];
    $updateParams = [$name, $address];
    if ($hasPermanentOfflineColumn) {
        $setParts[] = 'permanently_offline = ?';
        $updateParams[] = $permanentlyOffline;
    }
    if ($hasPlantKindColumn) {
        $setParts[] = 'plant_kind = ?';
        $updateParams[] = $plantKind !== '' ? $plantKind : null;
    }
    if ($hasNotesColumn) {
        $setParts[] = 'notes = ?';
        $updateParams[] = $notes !== '' ? $notes : null;
    }
    if ($hasDeliveryDateColumn) {
        $setParts[] = 'delivery_date = ?';
        $updateParams[] = $deliveryDate !== '' ? $deliveryDate : null;
    }
    $updateParams[] = $plantId;

    $upd = $pdo->prepare("
        UPDATE masters
        SET " . implode(', ', $setParts) . "
        WHERE id = ?
    ");
    $upd->execute($updateParams);

    $stmtReload = $pdo->prepare("
        SELECT id, nickname, address, serial_number, last_seen, fw_version, rssi,
               " . ($hasPermanentOfflineColumn ? "permanently_offline" : "0 AS permanently_offline") . ",
               " . ($hasPlantKindColumn ? "plant_kind" : "NULL AS plant_kind") . ",
               " . ($hasNotesColumn ? "notes" : "NULL AS notes") . ",
               " . ($hasDeliveryDateColumn ? "delivery_date" : "NULL AS delivery_date") . "
        FROM masters
        WHERE id = ?
        LIMIT 1
    ");
    $stmtReload->execute([$plantId]);
    $row = $stmtReload->fetch(PDO::FETCH_ASSOC);

    $lastSeen = (string)($row['last_seen'] ?? '');
    $lastSeenTs = $lastSeen !== '' ? strtotime($lastSeen) : false;
    $isOnline = ($lastSeenTs !== false && $lastSeenTs >= (time() - 120));

    outJson([
        'status' => 'ok',
        'plant' => [
            'id' => (int)($row['id'] ?? 0),
            'name' => (string)($row['nickname'] ?? ''),
            'address' => (string)($row['address'] ?? ''),
            'serial_number' => (string)($row['serial_number'] ?? ''),
            'online' => $isOnline,
            'permanently_offline' => ((int)($row['permanently_offline'] ?? 0) === 1),
            'plant_kind' => inferPlantKind($row, $hasPlantKindColumn),
            'notes' => (string)($row['notes'] ?? ''),
            'delivery_date' => (string)($row['delivery_date'] ?? ''),
            'firmware_version' => (string)($row['fw_version'] ?? ''),
            'rssi' => (string)($row['rssi'] ?? ''),
            'updated_at' => $lastSeen,
        ],
    ]);
} catch (Throwable $e) {
    outJson(['status' => 'error', 'message' => 'Errore DB: ' . $e->getMessage()], 500);
}
