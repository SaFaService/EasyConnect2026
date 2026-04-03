<?php
session_start();
require 'config.php';
require_once 'auth_common.php';

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

function resolveUserAddress(PDO $pdo, ?int $ownerId, ?int $maintainerId, ?int $builderId, bool $hasUsersAddressColumn): string {
    if (!$hasUsersAddressColumn) {
        return '';
    }
    foreach ([$ownerId, $maintainerId, $builderId] as $candidateId) {
        if ($candidateId === null || $candidateId <= 0) {
            continue;
        }
        $stmt = $pdo->prepare("SELECT address FROM users WHERE id = ? LIMIT 1");
        $stmt->execute([$candidateId]);
        $address = trim((string)($stmt->fetchColumn() ?: ''));
        if ($address !== '') {
            return $address;
        }
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
$userPermissions = ecAuthCurrentUserPermissions($pdo, $userId);
$canCreatePlants = !empty($userPermissions['plant_create']);
if (!$canCreatePlants) {
    outJson(['status' => 'error', 'message' => 'Utenza non abilitata alla creazione impianti.'], 403);
}
if ($userRole !== 'admin' && !ecAuthCanReceiveNewAssignments(ecAuthCurrentUserAccessLevel($pdo, $userId))) {
    outJson(['status' => 'error', 'message' => 'La tua utenza non puo creare nuovi impianti.'], 403);
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    outJson(['status' => 'error', 'message' => 'Metodo non consentito'], 405);
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    $input = $_POST;
}

$nickname = trim((string)($input['name'] ?? $input['nickname'] ?? ''));
$address = trim((string)($input['address'] ?? ''));
$serial = trim((string)($input['serial_number'] ?? ''));
$ownerIdRaw = $input['owner_id'] ?? null;
$ownerId = is_numeric($ownerIdRaw) ? (int)$ownerIdRaw : null;
$maintainerIdRaw = $input['maintainer_id'] ?? null;
$maintainerId = is_numeric($maintainerIdRaw) ? (int)$maintainerIdRaw : null;
$builderIdRaw = $input['builder_id'] ?? null;
$builderIdInput = is_numeric($builderIdRaw) ? (int)$builderIdRaw : null;
$notes = trim((string)($input['notes'] ?? ''));
$deliveryDate = trim((string)($input['delivery_date'] ?? ''));
$permanentlyOffline = boolFromInput($input['permanently_offline'] ?? 0);
$plantKind = normalizePlantKind($input['plant_kind'] ?? '');
$serialTypeCode = serialType($serial);

if ($nickname === '' || $serial === '') {
    outJson(['status' => 'error', 'message' => 'Nome impianto e seriale master sono obbligatori.'], 400);
}
if ($plantKind !== '' && $serialTypeCode === '01' && $plantKind !== 'display') {
    outJson(['status' => 'error', 'message' => 'Per un seriale master di tipo 01 il tipo impianto deve essere Display.'], 400);
}
if ($plantKind !== '' && $serialTypeCode === '02' && !in_array($plantKind, ['standalone', 'rewamping'], true)) {
    outJson(['status' => 'error', 'message' => 'Per un seriale master di tipo 02 il tipo impianto deve essere Standalone o Rewamping.'], 400);
}

$hasBuilderColumn = columnExists($pdo, 'masters', 'builder_id');
$hasPermanentOfflineColumn = columnExists($pdo, 'masters', 'permanently_offline');
$hasPlantKindColumn = columnExists($pdo, 'masters', 'plant_kind');
$hasNotesColumn = columnExists($pdo, 'masters', 'notes');
$hasDeliveryDateColumn = columnExists($pdo, 'masters', 'delivery_date');
$hasUsersAddressColumn = columnExists($pdo, 'users', 'address');
$apiKey = bin2hex(random_bytes(32));

try {
    $stmtCheck = $pdo->prepare("SELECT id FROM masters WHERE serial_number = ? AND deleted_at IS NULL LIMIT 1");
    $stmtCheck->execute([$serial]);
    if ($stmtCheck->fetch()) {
        outJson(['status' => 'error', 'message' => 'Seriale master gia associato ad altro impianto.'], 409);
    }

    if ($address === '') {
        $address = resolveUserAddress($pdo, $ownerId, $maintainerId, $builderIdInput !== null ? $builderIdInput : (($userRole === 'builder') ? $userId : null), $hasUsersAddressColumn);
    }

    $insertCols = ['creator_id', 'owner_id', 'serial_number', 'api_key', 'nickname', 'address'];
    $insertVals = ['?', '?', '?', '?', '?', '?'];
    $insertParams = [$userId, $ownerId, $serial, $apiKey, $nickname, $address];

    $insertCols[] = 'maintainer_id';
    $insertVals[] = '?';
    $insertParams[] = $maintainerId;
    if ($hasBuilderColumn) {
        $builderId = $builderIdInput !== null ? $builderIdInput : (($userRole === 'builder') ? $userId : null);
        $insertCols[] = 'builder_id';
        $insertVals[] = '?';
        $insertParams[] = $builderId;
    }
    if ($hasPermanentOfflineColumn) {
        $insertCols[] = 'permanently_offline';
        $insertVals[] = '?';
        $insertParams[] = $permanentlyOffline;
    }
    if ($hasPlantKindColumn) {
        $insertCols[] = 'plant_kind';
        $insertVals[] = '?';
        $insertParams[] = $plantKind !== '' ? $plantKind : null;
    }
    if ($hasNotesColumn) {
        $insertCols[] = 'notes';
        $insertVals[] = '?';
        $insertParams[] = $notes !== '' ? $notes : null;
    }
    if ($hasDeliveryDateColumn) {
        $insertCols[] = 'delivery_date';
        $insertVals[] = '?';
        $insertParams[] = $deliveryDate !== '' ? $deliveryDate : null;
    }

    $stmt = $pdo->prepare("
        INSERT INTO masters (" . implode(', ', $insertCols) . ")
        VALUES (" . implode(', ', $insertVals) . ")
    ");
    $stmt->execute($insertParams);

    outJson([
        'status' => 'ok',
        'plant_id' => (int)$pdo->lastInsertId(),
        'name' => $nickname,
        'address' => $address,
        'serial_number' => $serial,
        'permanently_offline' => $permanentlyOffline === 1,
        'plant_kind' => $plantKind,
        'notes' => $notes,
        'delivery_date' => $deliveryDate,
    ]);
} catch (Throwable $e) {
    outJson(['status' => 'error', 'message' => 'Errore DB: ' . $e->getMessage()], 500);
}
