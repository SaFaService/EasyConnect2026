<?php
session_start();
require 'config.php';
require 'lang.php';
require_once 'auth_common.php';

if (!isset($_SESSION['user_id'])) {
    header('Location: login.php');
    exit;
}

$currentUserId = (int)$_SESSION['user_id'];
$currentUserRole = (string)($_SESSION['user_role'] ?? '');
$isAdmin = ($currentUserRole === 'admin');
$canManagePeripherals = ecAuthCurrentUserCan($pdo, $currentUserId, 'manual_peripheral');
$isIt = (string)($_SESSION['lang'] ?? 'it') === 'it';

function pdTxt(string $it, string $en): string {
    global $isIt;
    return $isIt ? $it : $en;
}

function pdTableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = ? LIMIT 1");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function pdColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ? AND column_name = ? LIMIT 1");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

function pdColumnType(PDO $pdo, string $tableName, string $columnName): string {
    $stmt = $pdo->prepare("SELECT column_type FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ? AND column_name = ? LIMIT 1");
    $stmt->execute([$tableName, $columnName]);
    return (string)($stmt->fetchColumn() ?: '');
}

function pdEnumHasValue(PDO $pdo, string $tableName, string $columnName, string $value): bool {
    $columnType = strtolower(pdColumnType($pdo, $tableName, $columnName));
    if ($columnType === '' || strpos($columnType, 'enum(') !== 0) {
        return false;
    }
    return strpos($columnType, "'" . strtolower($value) . "'") !== false;
}

function pdIsManualLockSource(string $lockSource, array $manualSources): bool {
    return $lockSource !== '' && in_array($lockSource, $manualSources, true);
}

function pdBoolFromInput($value): int {
    if (is_bool($value)) {
        return $value ? 1 : 0;
    }
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['1', 'true', 'on', 'yes', 'si'], true) ? 1 : 0;
}

function pdFetchUserAddress(PDO $pdo, ?int $userId, bool $hasUsersAddressColumn): string {
    if (!$hasUsersAddressColumn || $userId === null || $userId <= 0) {
        return '';
    }
    $stmt = $pdo->prepare("SELECT address FROM users WHERE id = ? LIMIT 1");
    $stmt->execute([$userId]);
    return trim((string)($stmt->fetchColumn() ?: ''));
}

function pdResolvedPlantAddress(PDO $pdo, ?int $ownerUserId, ?int $maintainerUserId, ?int $builderUserId, bool $hasUsersAddressColumn): string {
    foreach ([$ownerUserId, $maintainerUserId, $builderUserId] as $candidateId) {
        $address = pdFetchUserAddress($pdo, $candidateId, $hasUsersAddressColumn);
        if ($address !== '') {
            return $address;
        }
    }
    return '';
}

function pdUserDisplayLabel(array $row, bool $hasUsersNameColumn, bool $hasUsersCompanyColumn, bool $hasUsersPortalAccessLevelColumn): string {
    $parts = [];
    if ($hasUsersNameColumn) {
        $name = trim((string)($row['name'] ?? ''));
        if ($name !== '') {
            $parts[] = $name;
        }
    }
    if ($hasUsersCompanyColumn) {
        $company = trim((string)($row['company'] ?? ''));
        if ($company !== '') {
            $parts[] = $company;
        }
    }
    $email = trim((string)($row['email'] ?? ''));
    if ($email !== '') {
        $parts[] = $email;
    }
    if (empty($parts)) {
        $parts[] = 'Utente #' . (int)($row['id'] ?? 0);
    }
    $label = implode(' | ', array_unique($parts));
    if ($hasUsersPortalAccessLevelColumn) {
        $label .= ' [' . ecAuthPortalAccessLabel((string)($row['portal_access_level'] ?? 'active')) . ']';
    }
    return $label;
}

function pdFetchAccessiblePlant(PDO $pdo, int $plantId, int $userId, bool $isAdmin, bool $forUpdate = false): ?array {
    if ($plantId <= 0) {
        return null;
    }
    if ($isAdmin) {
        $sql = 'SELECT * FROM masters WHERE id = ? AND deleted_at IS NULL' . ($forUpdate ? ' FOR UPDATE' : '');
        $stmt = $pdo->prepare($sql);
        $stmt->execute([$plantId]);
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        return $row ?: null;
    }
    $hasBuilderColumn = pdColumnExists($pdo, 'masters', 'builder_id');
    $sql = 'SELECT * FROM masters WHERE id = ? AND deleted_at IS NULL AND (creator_id = ? OR owner_id = ? OR maintainer_id = ?';
    if ($hasBuilderColumn) {
        $sql .= ' OR builder_id = ?';
    }
    $sql .= ')' . ($forUpdate ? ' FOR UPDATE' : '');
    $params = [$plantId, $userId, $userId, $userId];
    if ($hasBuilderColumn) {
        $params[] = $userId;
    }
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row ?: null;
}

function pdNormalizePlantKind($value): string {
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['display', 'standalone', 'rewamping'], true) ? $normalized : '';
}

function pdInferPlantKind(array $master, bool $hasPlantKindColumn): string {
    if ($hasPlantKindColumn) {
        $kind = pdNormalizePlantKind($master['plant_kind'] ?? '');
        if ($kind !== '') {
            return $kind;
        }
    }
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', (string)($master['serial_number'] ?? ''), $m)) {
        if ((string)$m[1] === '01') {
            return 'display';
        }
        if ((string)$m[1] === '02') {
            return 'standalone';
        }
    }
    return '';
}

function pdPlantKindLabel(string $kind): string {
    switch ($kind) {
        case 'display':
            return 'Display';
        case 'standalone':
            return 'Standalone';
        case 'rewamping':
            return 'Rewamping';
        default:
            return '-';
    }
}

function pdDeviceModeLabel(string $productTypeCode, $deviceMode): string {
    if ($deviceMode === null || $deviceMode === '') {
        return '-';
    }
    $mode = (int)$deviceMode;
    $map = [];
    switch ($productTypeCode) {
        case '02':
            $map = [1 => 'Standalone', 2 => 'Rewamping'];
            break;
        case '03':
            $map = [1 => 'LUCE', 2 => 'UVC', 3 => 'ELETTROSTATICO', 4 => 'GAS', 5 => 'COMANDO'];
            break;
        case '04':
            $map = [1 => 'Temp/Humidity', 2 => 'Pressure', 3 => 'All'];
            break;
        case '05':
            $map = [1 => 'Immissione', 2 => 'Aspirazione'];
            break;
    }
    return isset($map[$mode]) ? ($mode . ' - ' . $map[$mode]) : (string)$mode;
}

function pdFlashRedirect(int $plantId, string $type, string $message): void {
    $_SESSION['plant_detail_flash'] = ['type' => $type, 'message' => $message];
    header('Location: plant_detail.php?plant_id=' . $plantId);
    exit;
}

function pdInsertAudit(PDO $pdo, int $plantId, string $action, string $details): void {
    try {
        $stmt = $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, ?, ?)");
        $stmt->execute([$plantId, $action, $details]);
    } catch (Throwable $e) {
    }
}

$hasBuilderColumn = pdColumnExists($pdo, 'masters', 'builder_id');
$hasUsersCompanyColumn = pdColumnExists($pdo, 'users', 'company');
$hasUsersNameColumn = pdColumnExists($pdo, 'users', 'name');
$hasUsersPhoneColumn = pdColumnExists($pdo, 'users', 'phone');
$hasUsersAddressColumn = pdColumnExists($pdo, 'users', 'address');
$hasUsersPortalAccessLevelColumn = pdColumnExists($pdo, 'users', 'portal_access_level');
$hasPermanentOfflineColumn = pdColumnExists($pdo, 'masters', 'permanently_offline');
$hasPlantKindColumn = pdColumnExists($pdo, 'masters', 'plant_kind');
$hasNotesColumn = pdColumnExists($pdo, 'masters', 'notes');
$hasDeliveryDateColumn = pdColumnExists($pdo, 'masters', 'delivery_date');
$hasDeviceSerials = pdTableExists($pdo, 'device_serials');
$hasProductTypes = pdTableExists($pdo, 'product_types');
$hasProductTypeCode = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'product_type_code');
$hasDeviceSerialStatus = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'status');
$hasAssignedMasterId = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'assigned_master_id');
$hasAssignedUserId = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'assigned_user_id');
$hasAssignedRole = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'assigned_role');
$hasSerialLocked = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'serial_locked');
$hasLockSource = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'lock_source');
$hasStatusReasonCode = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'status_reason_code');
$hasStatusChangedAt = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'status_changed_at');
$hasStatusChangedBy = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'status_changed_by_user_id');
$hasActivatedAt = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'activated_at');
$hasDeactivatedAt = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'deactivated_at');
$hasReplacedBySerial = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'replaced_by_serial');
$hasDeviceMode = $hasDeviceSerials && pdColumnExists($pdo, 'device_serials', 'device_mode');
$manualFeatureReady = $hasDeviceSerials && $hasAssignedMasterId && $hasLockSource && $hasProductTypeCode;
$runtimeWarnings = [];
$manualLockSources = [];
if ($hasLockSource) {
    if (pdEnumHasValue($pdo, 'device_serials', 'lock_source', 'manual_plant')) {
        $manualLockSources[] = 'manual_plant';
    }
    if (pdEnumHasValue($pdo, 'device_serials', 'lock_source', 'manual')) {
        $manualLockSources[] = 'manual';
    }
}
if (empty($manualLockSources)) {
    $manualLockSources[] = 'manual';
}
$manualAssignLockValue = in_array('manual_plant', $manualLockSources, true) ? 'manual_plant' : $manualLockSources[0];
$manualUnassignLockValue = pdEnumHasValue($pdo, 'device_serials', 'lock_source', 'manual_unassign') ? 'manual_unassign' : $manualAssignLockValue;

$plantId = (int)($_REQUEST['plant_id'] ?? 0);
if ($plantId <= 0) {
    http_response_code(400);
    echo pdTxt('ID impianto non valido.', 'Invalid plant ID.');
    exit;
}

$flash = $_SESSION['plant_detail_flash'] ?? null;
unset($_SESSION['plant_detail_flash']);

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = trim((string)($_POST['action'] ?? ''));
    $serialNumber = trim((string)($_POST['serial_number'] ?? ''));

    if ($action === 'update_plant_admin') {
        if (!$isAdmin) {
            pdFlashRedirect($plantId, 'danger', pdTxt('Solo un amministratore puo modificare l impianto da questa pagina.', 'Only an administrator can edit the plant from this page.'));
        }

        $nickname = trim((string)($_POST['nickname'] ?? ''));
        $address = trim((string)($_POST['address'] ?? ''));
        $notes = trim((string)($_POST['notes'] ?? ''));
        $deliveryDate = trim((string)($_POST['delivery_date'] ?? ''));
        $logRetentionDays = max(1, min(365, (int)($_POST['log_retention_days'] ?? 30)));
        $ownerId = isset($_POST['owner_id']) && $_POST['owner_id'] !== '' ? (int)$_POST['owner_id'] : null;
        $maintainerId = isset($_POST['maintainer_id']) && $_POST['maintainer_id'] !== '' ? (int)$_POST['maintainer_id'] : null;
        $builderId = ($hasBuilderColumn && isset($_POST['builder_id']) && $_POST['builder_id'] !== '') ? (int)$_POST['builder_id'] : null;
        $permanentlyOffline = pdBoolFromInput($_POST['permanently_offline'] ?? 0);
        $plantKind = pdNormalizePlantKind($_POST['plant_kind'] ?? '');

        if ($nickname === '') {
            pdFlashRedirect($plantId, 'danger', pdTxt('Il nickname impianto e obbligatorio.', 'Plant nickname is required.'));
        }

        try {
            $pdo->beginTransaction();
            $masterLock = pdFetchAccessiblePlant($pdo, $plantId, $currentUserId, true, true);
            if (!$masterLock) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Impianto non trovato.', 'Plant not found.'));
            }

            $serialType = '';
            if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', (string)($masterLock['serial_number'] ?? ''), $m)) {
                $serialType = (string)$m[1];
            }
            if ($plantKind !== '' && $serialType === '01' && $plantKind !== 'display') {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Per un seriale master di tipo 01 il tipo impianto deve essere Display.', 'For a type 01 master serial the plant type must be Display.'));
            }
            if ($plantKind !== '' && $serialType === '02' && !in_array($plantKind, ['standalone', 'rewamping'], true)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Per un seriale master di tipo 02 il tipo impianto deve essere Standalone o Rewamping.', 'For a type 02 master serial the plant type must be Standalone or Rewamping.'));
            }

            if ($address === '') {
                $address = pdResolvedPlantAddress($pdo, $ownerId, $maintainerId, $builderId, $hasUsersAddressColumn);
            }

            $setParts = [
                'nickname = ?',
                'address = ?',
                'log_retention_days = ?',
                'owner_id = ?',
                'maintainer_id = ?',
            ];
            $params = [$nickname, $address, $logRetentionDays, $ownerId, $maintainerId];
            if ($hasBuilderColumn) {
                $setParts[] = 'builder_id = ?';
                $params[] = $builderId;
            }
            if ($hasPermanentOfflineColumn) {
                $setParts[] = 'permanently_offline = ?';
                $params[] = $permanentlyOffline;
            }
            if ($hasPlantKindColumn) {
                $setParts[] = 'plant_kind = ?';
                $params[] = $plantKind !== '' ? $plantKind : null;
            }
            if ($hasNotesColumn) {
                $setParts[] = 'notes = ?';
                $params[] = $notes !== '' ? $notes : null;
            }
            if ($hasDeliveryDateColumn) {
                $setParts[] = 'delivery_date = ?';
                $params[] = $deliveryDate !== '' ? $deliveryDate : null;
            }
            $params[] = $plantId;

            $pdo->prepare('UPDATE masters SET ' . implode(', ', $setParts) . ' WHERE id = ?')->execute($params);
            pdInsertAudit($pdo, $plantId, 'PLANT_DETAIL_UPDATE', 'Impianto aggiornato da admin ' . $currentUserId);
            $pdo->commit();
            pdFlashRedirect($plantId, 'success', pdTxt('Dati impianto aggiornati.', 'Plant data updated.'));
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            pdFlashRedirect($plantId, 'danger', pdTxt('Errore aggiornamento impianto: ', 'Plant update error: ') . $e->getMessage());
        }
    }

    if (in_array($action, ['assign_manual_peripheral', 'remove_manual_peripheral', 'confirm_detected_peripherals'], true)) {
        if (!$canManagePeripherals) {
            pdFlashRedirect($plantId, 'danger', pdTxt('Non hai i permessi per gestire le periferiche manuali.', 'You do not have permission to manage manual peripherals.'));
        }
        if (!$manualFeatureReady) {
            pdFlashRedirect($plantId, 'danger', pdTxt('Funzione non disponibile: eseguire la migration SQL 2.17.', 'Feature unavailable: run SQL migration 2.17.'));
        }
    }

    if ($action === 'assign_manual_peripheral') {
        if ($serialNumber === '') {
            pdFlashRedirect($plantId, 'danger', pdTxt('Seleziona un seriale periferica da assegnare.', 'Select a peripheral serial to assign.'));
        }

        try {
            $pdo->beginTransaction();
            $masterLock = pdFetchAccessiblePlant($pdo, $plantId, $currentUserId, $isAdmin, true);
            if (!$masterLock) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Impianto non trovato o non autorizzato.', 'Plant not found or not authorized.'));
            }

            $stmtSerial = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
            $stmtSerial->execute([$serialNumber]);
            $serialRow = $stmtSerial->fetch(PDO::FETCH_ASSOC);
            if (!$serialRow) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Seriale non trovato in archivio.', 'Serial not found.'));
            }

            $productType = trim((string)($serialRow['product_type_code'] ?? ''));
            if (in_array($productType, ['01', '02'], true)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Puoi assegnare manualmente solo periferiche, non una master.', 'You can manually assign peripherals only, not a master.'));
            }

            $status = strtolower(trim((string)($serialRow['status'] ?? '')));
            if ($hasDeviceSerialStatus && in_array($status, ['retired', 'voided'], true)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Il seriale selezionato non e assegnabile perche risulta dismesso o annullato.', 'The selected serial cannot be assigned because it is retired or voided.'));
            }

            $assignedMasterId = (int)($serialRow['assigned_master_id'] ?? 0);
            $lockSource = trim((string)($serialRow['lock_source'] ?? ''));
            if ($assignedMasterId > 0 && $assignedMasterId !== $plantId) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Il seriale risulta gia assegnato a un altro impianto.', 'The serial is already assigned to another plant.'));
            }
            if ($assignedMasterId === $plantId && pdIsManualLockSource($lockSource, $manualLockSources)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'warning', pdTxt('Il seriale era gia assegnato manualmente a questo impianto.', 'The serial was already manually assigned to this plant.'));
            }
            if ($assignedMasterId === $plantId && $lockSource !== '' && !pdIsManualLockSource($lockSource, $manualLockSources)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'warning', pdTxt('Il seriale e gia associato a questo impianto tramite rilevazione reale.', 'The serial is already linked to this plant via live detection.'));
            }

            $setParts = ['assigned_master_id = ?'];
            $params = [$plantId];
            if ($hasAssignedUserId) {
                $setParts[] = 'assigned_user_id = ?';
                $params[] = $currentUserId;
            }
            if ($hasAssignedRole) {
                $setParts[] = 'assigned_role = ?';
                $params[] = $currentUserRole;
            }
            if ($hasDeviceSerialStatus) {
                $setParts[] = "status = 'active'";
            }
            if ($hasSerialLocked) {
                $setParts[] = 'serial_locked = 1';
            }
            if ($hasLockSource) {
                $setParts[] = 'lock_source = ?';
                $params[] = $manualAssignLockValue;
            }
            if ($hasStatusReasonCode) {
                $setParts[] = "status_reason_code = 'master_bind'";
            }
            if ($hasStatusChangedAt) {
                $setParts[] = 'status_changed_at = NOW()';
            }
            if ($hasStatusChangedBy) {
                $setParts[] = 'status_changed_by_user_id = ?';
                $params[] = $currentUserId;
            }
            if ($hasActivatedAt) {
                $setParts[] = 'activated_at = COALESCE(activated_at, NOW())';
            }
            if ($hasDeactivatedAt) {
                $setParts[] = 'deactivated_at = NULL';
            }
            if ($hasReplacedBySerial) {
                $setParts[] = 'replaced_by_serial = NULL';
            }

            $params[] = (int)$serialRow['id'];
            $pdo->prepare('UPDATE device_serials SET ' . implode(', ', $setParts) . ' WHERE id = ?')->execute($params);
            pdInsertAudit($pdo, $plantId, 'MANUAL_PERIPHERAL_ASSIGN', 'Assegnata manualmente periferica ' . $serialNumber . ' da utente ' . $currentUserId);
            $pdo->commit();
            pdFlashRedirect($plantId, 'success', pdTxt('Periferica assegnata manualmente all impianto.', 'Peripheral manually assigned to plant.'));
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            pdFlashRedirect($plantId, 'danger', pdTxt('Errore assegnazione periferica: ', 'Peripheral assignment error: ') . $e->getMessage());
        }
    }

    if ($action === 'remove_manual_peripheral') {
        if ($serialNumber === '') {
            pdFlashRedirect($plantId, 'danger', pdTxt('Seriale periferica mancante.', 'Missing peripheral serial.'));
        }

        try {
            $pdo->beginTransaction();
            $masterLock = pdFetchAccessiblePlant($pdo, $plantId, $currentUserId, $isAdmin, true);
            if (!$masterLock) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Impianto non trovato o non autorizzato.', 'Plant not found or not authorized.'));
            }

            $stmtSerial = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
            $stmtSerial->execute([$serialNumber]);
            $serialRow = $stmtSerial->fetch(PDO::FETCH_ASSOC);
            if (!$serialRow) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Seriale non trovato in archivio.', 'Serial not found.'));
            }

            if ((int)($serialRow['assigned_master_id'] ?? 0) !== $plantId) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'warning', pdTxt('Il seriale non risulta assegnato a questo impianto.', 'The serial is not assigned to this plant.'));
            }
            if (!pdIsManualLockSource(trim((string)($serialRow['lock_source'] ?? '')), $manualLockSources)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'warning', pdTxt('Da questa pagina puoi rimuovere solo assegnazioni manuali. Le schede rilevate online restano gestite dai dati reali.', 'Only manual assignments can be removed from this page. Online-detected boards remain managed by live data.'));
            }

            $setParts = ['assigned_master_id = NULL'];
            $params = [];
            if ($hasSerialLocked) {
                $setParts[] = 'serial_locked = 0';
            }
            if ($hasLockSource) {
                $setParts[] = 'lock_source = ?';
                $params[] = $manualUnassignLockValue;
            }
            if ($hasStatusReasonCode) {
                $setParts[] = 'status_reason_code = NULL';
            }
            if ($hasStatusChangedAt) {
                $setParts[] = 'status_changed_at = NOW()';
            }
            if ($hasStatusChangedBy) {
                $setParts[] = 'status_changed_by_user_id = ?';
                $params[] = $currentUserId;
            }
            $params[] = (int)$serialRow['id'];
            $pdo->prepare('UPDATE device_serials SET ' . implode(', ', $setParts) . ' WHERE id = ?')->execute($params);
            pdInsertAudit($pdo, $plantId, 'MANUAL_PERIPHERAL_UNASSIGN', 'Rimossa assegnazione manuale periferica ' . $serialNumber . ' da utente ' . $currentUserId);
            $pdo->commit();
            pdFlashRedirect($plantId, 'success', pdTxt('Assegnazione manuale rimossa.', 'Manual assignment removed.'));
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            pdFlashRedirect($plantId, 'danger', pdTxt('Errore rimozione assegnazione: ', 'Assignment removal error: ') . $e->getMessage());
        }
    }

    if ($action === 'confirm_detected_peripherals') {
        try {
            $pdo->beginTransaction();
            $masterLock = pdFetchAccessiblePlant($pdo, $plantId, $currentUserId, $isAdmin, true);
            if (!$masterLock) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'danger', pdTxt('Impianto non trovato o non autorizzato.', 'Plant not found or not authorized.'));
            }

            $statusFilter = $hasDeviceSerialStatus ? " AND (ds.serial_number IS NULL OR ds.status NOT IN ('retired','voided'))" : '';
            $stmtDetected = $pdo->prepare("
                SELECT DISTINCT m.slave_sn
                FROM measurements m
                LEFT JOIN device_serials ds ON ds.serial_number = m.slave_sn
                WHERE m.master_id = ?
                  AND m.slave_sn IS NOT NULL
                  AND m.slave_sn <> ''
                  AND m.slave_sn <> '0'
                  AND m.recorded_at >= (NOW() - INTERVAL 30 DAY)
                  {$statusFilter}
                ORDER BY m.slave_sn ASC
            ");
            $stmtDetected->execute([$plantId]);
            $detectedMap = [];
            foreach ($stmtDetected->fetchAll(PDO::FETCH_ASSOC) as $rowDetected) {
                $sn = trim((string)($rowDetected['slave_sn'] ?? ''));
                if ($sn !== '') {
                    $detectedMap[$sn] = true;
                }
            }
            if (empty($detectedMap)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'warning', pdTxt('Nessuna periferica rilevata online da confermare.', 'No online-detected peripherals to confirm.'));
            }

            $manualLockFilter = '';
            $manualParams = [$plantId];
            if ($hasLockSource && !empty($manualLockSources)) {
                $manualLockFilter = ' AND ds.lock_source IN (' . implode(', ', array_fill(0, count($manualLockSources), '?')) . ')';
                $manualParams = array_merge($manualParams, $manualLockSources);
            }
            $manualStatusFilter = $hasDeviceSerialStatus ? " AND (ds.status IS NULL OR ds.status NOT IN ('retired','voided'))" : '';
            $stmtManual = $pdo->prepare("
                SELECT ds.serial_number
                FROM device_serials ds
                WHERE ds.assigned_master_id = ?
                  AND ds.product_type_code NOT IN ('01','02')
                  {$manualLockFilter}
                  {$manualStatusFilter}
            ");
            $stmtManual->execute($manualParams);
            $manualMap = [];
            foreach ($stmtManual->fetchAll(PDO::FETCH_ASSOC) as $rowManual) {
                $sn = trim((string)($rowManual['serial_number'] ?? ''));
                if ($sn !== '') {
                    $manualMap[$sn] = true;
                }
            }

            $detectedOnly = array_values(array_diff(array_keys($detectedMap), array_keys($manualMap)));
            $manualOnly = array_values(array_diff(array_keys($manualMap), array_keys($detectedMap)));

            if (empty($detectedOnly)) {
                $pdo->rollBack();
                pdFlashRedirect($plantId, 'info', pdTxt('Periferiche gia allineate: nessuna conferma necessaria.', 'Peripherals already aligned: no confirmation needed.'));
            }
            if (!empty($manualOnly)) {
                $pdo->rollBack();
                $preview = implode(', ', array_slice($manualOnly, 0, 3));
                $suffix = count($manualOnly) > 3 ? ', ...' : '';
                pdFlashRedirect(
                    $plantId,
                    'warning',
                    pdTxt(
                        'Conferma automatica bloccata: sono presenti seriali manuali non rilevati online. Rimuovili o verifica le schede prima di confermare.',
                        'Automatic confirmation blocked: there are manually assigned serials not detected online. Remove them or verify boards before confirming.'
                    ) . ($preview !== '' ? (' [' . $preview . $suffix . ']') : '')
                );
            }

            $placeholders = implode(', ', array_fill(0, count($detectedOnly), '?'));
            $stmtRows = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number IN ({$placeholders}) FOR UPDATE");
            $stmtRows->execute($detectedOnly);
            $rowsBySerial = [];
            foreach ($stmtRows->fetchAll(PDO::FETCH_ASSOC) as $dsRow) {
                $sn = trim((string)($dsRow['serial_number'] ?? ''));
                if ($sn !== '') {
                    $rowsBySerial[$sn] = $dsRow;
                }
            }

            $missingSerials = [];
            $conflictSerials = [];
            foreach ($detectedOnly as $sn) {
                if (!isset($rowsBySerial[$sn])) {
                    $missingSerials[] = $sn;
                    continue;
                }
                $row = $rowsBySerial[$sn];
                $status = strtolower(trim((string)($row['status'] ?? '')));
                if ($hasDeviceSerialStatus && in_array($status, ['retired', 'voided'], true)) {
                    $conflictSerials[] = $sn;
                    continue;
                }
                $assignedMasterId = (int)($row['assigned_master_id'] ?? 0);
                if ($assignedMasterId > 0 && $assignedMasterId !== $plantId) {
                    $conflictSerials[] = $sn;
                }
            }
            if (!empty($missingSerials) || !empty($conflictSerials)) {
                $pdo->rollBack();
                $pieces = [];
                if (!empty($missingSerials)) {
                    $pieces[] = pdTxt('non censiti', 'not registered') . ': ' . implode(', ', array_slice($missingSerials, 0, 3)) . (count($missingSerials) > 3 ? ', ...' : '');
                }
                if (!empty($conflictSerials)) {
                    $pieces[] = pdTxt('in conflitto', 'in conflict') . ': ' . implode(', ', array_slice($conflictSerials, 0, 3)) . (count($conflictSerials) > 3 ? ', ...' : '');
                }
                pdFlashRedirect(
                    $plantId,
                    'danger',
                    pdTxt(
                        'Conferma non eseguita: alcune periferiche richiedono intervento manuale',
                        'Confirmation not executed: some peripherals require manual intervention'
                    ) . ' (' . implode(' | ', $pieces) . ').'
                );
            }

            foreach ($detectedOnly as $sn) {
                $row = $rowsBySerial[$sn];
                $setParts = ['assigned_master_id = ?'];
                $params = [$plantId];
                if ($hasAssignedUserId) {
                    $setParts[] = 'assigned_user_id = ?';
                    $params[] = $currentUserId;
                }
                if ($hasAssignedRole) {
                    $setParts[] = 'assigned_role = ?';
                    $params[] = $currentUserRole;
                }
                if ($hasDeviceSerialStatus) {
                    $setParts[] = "status = 'active'";
                }
                if ($hasSerialLocked) {
                    $setParts[] = 'serial_locked = 1';
                }
                if ($hasLockSource) {
                    $setParts[] = 'lock_source = ?';
                    $params[] = $manualAssignLockValue;
                }
                if ($hasStatusReasonCode) {
                    $setParts[] = "status_reason_code = 'master_bind'";
                }
                if ($hasStatusChangedAt) {
                    $setParts[] = 'status_changed_at = NOW()';
                }
                if ($hasStatusChangedBy) {
                    $setParts[] = 'status_changed_by_user_id = ?';
                    $params[] = $currentUserId;
                }
                if ($hasActivatedAt) {
                    $setParts[] = 'activated_at = COALESCE(activated_at, NOW())';
                }
                if ($hasDeactivatedAt) {
                    $setParts[] = 'deactivated_at = NULL';
                }
                if ($hasReplacedBySerial) {
                    $setParts[] = 'replaced_by_serial = NULL';
                }
                $params[] = (int)$row['id'];
                $pdo->prepare('UPDATE device_serials SET ' . implode(', ', $setParts) . ' WHERE id = ?')->execute($params);
            }

            pdInsertAudit(
                $pdo,
                $plantId,
                'MANUAL_PERIPHERAL_CONFIRM',
                'Confermate periferiche rilevate online: ' . implode(', ', $detectedOnly) . ' da utente ' . $currentUserId
            );
            $pdo->commit();
            pdFlashRedirect(
                $plantId,
                'success',
                pdTxt(
                    'Periferiche confermate e allineate allo stato reale: ',
                    'Peripherals confirmed and aligned to real status: '
                ) . (string)count($detectedOnly)
            );
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            pdFlashRedirect($plantId, 'danger', pdTxt('Errore conferma periferiche: ', 'Peripheral confirmation error: ') . $e->getMessage());
        }
    }
}

$companyOwner = $hasUsersCompanyColumn ? 'o.company AS owner_company' : 'NULL AS owner_company';
$companyMaint = $hasUsersCompanyColumn ? 'mn.company AS maintainer_company' : 'NULL AS maintainer_company';
$companyBuilder = $hasUsersCompanyColumn ? 'b.company AS builder_company' : 'NULL AS builder_company';
$companyCreator = $hasUsersCompanyColumn ? 'c.company AS creator_company' : 'NULL AS creator_company';
$ownerNameSelect = $hasUsersNameColumn ? 'o.name AS owner_name' : 'NULL AS owner_name';
$maintNameSelect = $hasUsersNameColumn ? 'mn.name AS maintainer_name' : 'NULL AS maintainer_name';
$builderNameSelect = $hasUsersNameColumn ? 'b.name AS builder_name' : 'NULL AS builder_name';
$creatorNameSelect = $hasUsersNameColumn ? 'c.name AS creator_name' : 'NULL AS creator_name';
$ownerPhoneSelect = $hasUsersPhoneColumn ? 'o.phone AS owner_phone' : 'NULL AS owner_phone';
$maintPhoneSelect = $hasUsersPhoneColumn ? 'mn.phone AS maintainer_phone' : 'NULL AS maintainer_phone';
$builderPhoneSelect = $hasUsersPhoneColumn ? 'b.phone AS builder_phone' : 'NULL AS builder_phone';
$creatorPhoneSelect = $hasUsersPhoneColumn ? 'c.phone AS creator_phone' : 'NULL AS creator_phone';
$builderJoin = $hasBuilderColumn ? 'LEFT JOIN users b ON m.builder_id = b.id' : '';
$builderSelect = $hasBuilderColumn
    ? "{$builderNameSelect}, b.email AS builder_email, {$builderPhoneSelect}, {$companyBuilder}"
    : 'NULL AS builder_name, NULL AS builder_email, NULL AS builder_phone, NULL AS builder_company';

$sqlPlant = "
    SELECT m.*, {$ownerNameSelect}, o.email AS owner_email, {$ownerPhoneSelect}, {$companyOwner},
           {$maintNameSelect}, mn.email AS maintainer_email, {$maintPhoneSelect}, {$companyMaint},
           {$builderSelect}, {$creatorNameSelect}, c.email AS creator_email, {$creatorPhoneSelect}, {$companyCreator}
    FROM masters m
    LEFT JOIN users o ON m.owner_id = o.id
    LEFT JOIN users mn ON m.maintainer_id = mn.id
    LEFT JOIN users c ON m.creator_id = c.id
    {$builderJoin}
    WHERE m.id = :plant_id
      AND m.deleted_at IS NULL
";
if (!$isAdmin) {
    $sqlPlant .= " AND (m.creator_id = :uid_creator OR m.owner_id = :uid_owner OR m.maintainer_id = :uid_maintainer";
    if ($hasBuilderColumn) {
        $sqlPlant .= " OR m.builder_id = :uid_builder";
    }
    $sqlPlant .= ')';
}
$paramsPlant = ['plant_id' => $plantId];
if (!$isAdmin) {
    $paramsPlant['uid_creator'] = $currentUserId;
    $paramsPlant['uid_owner'] = $currentUserId;
    $paramsPlant['uid_maintainer'] = $currentUserId;
    if ($hasBuilderColumn) {
        $paramsPlant['uid_builder'] = $currentUserId;
    }
}
$master = null;
try {
    $stmtPlant = $pdo->prepare($sqlPlant);
    $stmtPlant->execute($paramsPlant);
    $master = $stmtPlant->fetch(PDO::FETCH_ASSOC);
} catch (Throwable $e) {
    $runtimeWarnings[] = pdTxt("Alcuni dati collegati all'impianto non sono leggibili con lo schema attuale. La scheda viene aperta in modalita compatibile.", 'Some linked plant data cannot be read with the current schema. The detail page has been opened in compatibility mode.');
    $sqlPlantFallback = "SELECT m.* FROM masters m WHERE m.id = :plant_id AND m.deleted_at IS NULL";
    if (!$isAdmin) {
        $sqlPlantFallback .= " AND (m.creator_id = :uid_creator OR m.owner_id = :uid_owner OR m.maintainer_id = :uid_maintainer";
        if ($hasBuilderColumn) {
            $sqlPlantFallback .= " OR m.builder_id = :uid_builder";
        }
        $sqlPlantFallback .= ')';
    }
    $stmtPlantFallback = $pdo->prepare($sqlPlantFallback);
    $stmtPlantFallback->execute($paramsPlant);
    $master = $stmtPlantFallback->fetch(PDO::FETCH_ASSOC);
    if ($master) {
        foreach ([
            'owner_name', 'owner_email', 'owner_phone', 'owner_company',
            'maintainer_name', 'maintainer_email', 'maintainer_phone', 'maintainer_company',
            'builder_name', 'builder_email', 'builder_phone', 'builder_company',
            'creator_name', 'creator_email', 'creator_phone', 'creator_company',
        ] as $missingField) {
            if (!array_key_exists($missingField, $master)) {
                $master[$missingField] = null;
            }
        }
    }
}
if (!$master) {
    http_response_code(404);
    echo pdTxt('Impianto non trovato o non autorizzato.', 'Plant not found or not authorized.');
    exit;
}

$adminOwnerOptions = [];
$adminMaintainerOptions = [];
$adminBuilderOptions = [];
if ($isAdmin) {
    try {
        $ownerSql = "SELECT id, email";
        if ($hasUsersNameColumn) {
            $ownerSql .= ", name";
        }
        if ($hasUsersCompanyColumn) {
            $ownerSql .= ", company";
        }
        if ($hasUsersPortalAccessLevelColumn) {
            $ownerSql .= ", portal_access_level";
        }
        $ownerSql .= " FROM users WHERE role IN ('client','builder','maintainer') ORDER BY email ASC";
        $adminOwnerOptions = $pdo->query($ownerSql)->fetchAll(PDO::FETCH_ASSOC);
    } catch (Throwable $e) {
        $adminOwnerOptions = [];
    }
    try {
        $maintSql = "SELECT id, email";
        if ($hasUsersNameColumn) {
            $maintSql .= ", name";
        }
        if ($hasUsersCompanyColumn) {
            $maintSql .= ", company";
        }
        if ($hasUsersPortalAccessLevelColumn) {
            $maintSql .= ", portal_access_level";
        }
        $maintSql .= " FROM users WHERE role = 'maintainer' ORDER BY email ASC";
        $adminMaintainerOptions = $pdo->query($maintSql)->fetchAll(PDO::FETCH_ASSOC);
    } catch (Throwable $e) {
        $adminMaintainerOptions = [];
    }
    if ($hasBuilderColumn) {
        try {
            $builderSql = "SELECT id, email";
            if ($hasUsersNameColumn) {
                $builderSql .= ", name";
            }
            if ($hasUsersCompanyColumn) {
                $builderSql .= ", company";
            }
            if ($hasUsersPortalAccessLevelColumn) {
                $builderSql .= ", portal_access_level";
            }
            $builderSql .= " FROM users WHERE role = 'builder' ORDER BY email ASC";
            $adminBuilderOptions = $pdo->query($builderSql)->fetchAll(PDO::FETCH_ASSOC);
        } catch (Throwable $e) {
            $adminBuilderOptions = [];
        }
    }
}

$plantKind = pdInferPlantKind($master, $hasPlantKindColumn);
$plantKindLabel = pdPlantKindLabel($plantKind);
$isPermanentOffline = $hasPermanentOfflineColumn && ((int)($master['permanently_offline'] ?? 0) === 1);
$lastSeenValue = trim((string)($master['last_seen'] ?? ''));
$lastSeenTs = $lastSeenValue !== '' ? strtotime($lastSeenValue) : false;
$plantOnline = ($lastSeenTs !== false && $lastSeenTs >= (time() - 120));
$plantAddressMapUrl = trim((string)($master['address'] ?? '')) !== ''
    ? ('https://www.google.com/maps/search/?api=1&query=' . urlencode((string)$master['address']))
    : '';

$actualPeripherals = [];
if ($hasDeviceSerials) {
    try {
        $productJoin = ($hasProductTypes && $hasProductTypeCode) ? 'LEFT JOIN product_types pt ON pt.code = ds.product_type_code' : '';
        $productLabelSelect = ($hasProductTypes && $hasProductTypeCode) ? 'pt.label AS product_label' : 'NULL AS product_label';
        $statusFilter = $hasDeviceSerialStatus ? "(ds.serial_number IS NULL OR ds.status NOT IN ('retired','voided'))" : '1=1';
        $deviceModeSelect = $hasDeviceMode ? 'ds.device_mode' : 'NULL AS device_mode';
        $productTypeSelect = $hasProductTypeCode ? 'ds.product_type_code' : 'NULL AS product_type_code';
        $stmtActual = $pdo->prepare("
            SELECT m1.slave_sn, m1.slave_grp, m1.fw_version, m1.recorded_at, {$productTypeSelect}, {$deviceModeSelect}, {$productLabelSelect}
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
            LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn
            {$productJoin}
            WHERE m1.master_id = :mid_b
              AND ({$statusFilter})
            ORDER BY m1.slave_grp ASC, m1.slave_sn ASC
        ");
        $stmtActual->execute(['mid_a' => $plantId, 'mid_b' => $plantId]);
        $actualPeripherals = $stmtActual->fetchAll(PDO::FETCH_ASSOC);
    } catch (Throwable $e) {
        $actualPeripherals = [];
        $runtimeWarnings[] = pdTxt('Non e stato possibile leggere le periferiche rilevate online con lo schema attuale.', 'Unable to read peripherals detected online with the current schema.');
    }
}

$manualPeripherals = [];
$availablePeripheralOptions = [];
if ($manualFeatureReady) {
    try {
        $productJoin = ($hasProductTypes && $hasProductTypeCode) ? 'LEFT JOIN product_types pt ON pt.code = ds.product_type_code' : '';
        $productLabelSelect = ($hasProductTypes && $hasProductTypeCode) ? 'pt.label AS product_label' : 'NULL AS product_label';
        $deviceModeSelect = $hasDeviceMode ? 'ds.device_mode' : 'NULL AS device_mode';
        $statusFilter = $hasDeviceSerialStatus ? "AND (ds.status IS NULL OR ds.status NOT IN ('retired','voided'))" : '';
        $manualLockFilter = '';
        if ($hasLockSource && !empty($manualLockSources)) {
            $manualLockFilter = ' AND ds.lock_source IN (' . implode(', ', array_fill(0, count($manualLockSources), '?')) . ')';
        }

        $stmtManual = $pdo->prepare("
            SELECT ds.serial_number, ds.product_type_code, {$deviceModeSelect}, ds.lock_source,
                   " . ($hasDeviceSerialStatus ? 'ds.status' : "NULL AS status") . ",
                   ds.created_at, {$productLabelSelect}
            FROM device_serials ds
            {$productJoin}
            WHERE ds.assigned_master_id = ?
              AND ds.product_type_code NOT IN ('01','02')
              {$manualLockFilter}
              {$statusFilter}
            ORDER BY ds.serial_number ASC
        ");
        $stmtManual->execute(array_merge([$plantId], $manualLockSources));
        $manualPeripherals = $stmtManual->fetchAll(PDO::FETCH_ASSOC);

        $stmtAvailable = $pdo->prepare("
            SELECT ds.serial_number, ds.product_type_code, {$deviceModeSelect}, ds.assigned_master_id, ds.lock_source, {$productLabelSelect}
            FROM device_serials ds
            {$productJoin}
            WHERE ds.product_type_code NOT IN ('01','02')
              {$statusFilter}
              AND (ds.assigned_master_id IS NULL OR ds.assigned_master_id = ?)
            ORDER BY ds.serial_number DESC
            LIMIT 500
        ");
        $stmtAvailable->execute([$plantId]);
        $availablePeripheralOptions = $stmtAvailable->fetchAll(PDO::FETCH_ASSOC);
    } catch (Throwable $e) {
        $manualPeripherals = [];
        $availablePeripheralOptions = [];
        $runtimeWarnings[] = pdTxt('La sezione periferiche manuali non e disponibile con lo schema attuale.', 'The manual peripherals section is not available with the current schema.');
    }
}

$actualMap = [];
foreach ($actualPeripherals as $row) {
    $serial = trim((string)($row['slave_sn'] ?? ''));
    if ($serial !== '') {
        $actualMap[$serial] = $row;
    }
}
$manualMap = [];
foreach ($manualPeripherals as $row) {
    $serial = trim((string)($row['serial_number'] ?? ''));
    if ($serial !== '') {
        $manualMap[$serial] = $row;
    }
}

$comparisonRows = [];
$mismatchCount = 0;
foreach ($manualPeripherals as $row) {
    $serial = trim((string)($row['serial_number'] ?? ''));
    $statusLabel = pdTxt('In attesa verifica', 'Pending verification');
    $statusClass = 'bg-secondary';
    $lastSeen = '-';
    if (isset($actualMap[$serial])) {
        $statusLabel = pdTxt('Verificata', 'Verified');
        $statusClass = 'bg-success';
        $lastSeen = trim((string)($actualMap[$serial]['recorded_at'] ?? '')) ?: '-';
    } elseif (!empty($actualMap)) {
        $statusLabel = pdTxt('Assegnazione errata', 'Wrong assignment');
        $statusClass = 'bg-danger';
        $mismatchCount++;
    } elseif ($isPermanentOffline) {
        $statusLabel = pdTxt('Offline permanente', 'Permanent offline');
        $statusClass = 'bg-dark';
    }
    $comparisonRows[] = [
        'serial_number' => $serial,
        'product_type_code' => (string)($row['product_type_code'] ?? ''),
        'product_label' => (string)($row['product_label'] ?? 'Periferica'),
        'device_mode' => $row['device_mode'] ?? null,
        'source' => pdTxt('Manuale', 'Manual'),
        'status_label' => $statusLabel,
        'status_class' => $statusClass,
        'last_seen' => $lastSeen,
    ];
}
foreach ($actualPeripherals as $row) {
    $serial = trim((string)($row['slave_sn'] ?? ''));
    if ($serial === '' || isset($manualMap[$serial])) {
        continue;
    }
    $comparisonRows[] = [
        'serial_number' => $serial,
        'product_type_code' => (string)($row['product_type_code'] ?? ''),
        'product_label' => (string)($row['product_label'] ?? 'Periferica'),
        'device_mode' => $row['device_mode'] ?? null,
        'source' => pdTxt('Rilevata online', 'Detected online'),
        'status_label' => pdTxt('Periferica reale non prevista', 'Unexpected real peripheral'),
        'status_class' => 'bg-warning text-dark',
        'last_seen' => trim((string)($row['recorded_at'] ?? '')) ?: '-',
    ];
    $mismatchCount++;
}
$manualOnlySerials = array_values(array_diff(array_keys($manualMap), array_keys($actualMap)));
$detectedOnlySerials = array_values(array_diff(array_keys($actualMap), array_keys($manualMap)));
$canConfirmDetectedPeripherals = $canManagePeripherals
    && $manualFeatureReady
    && !empty($detectedOnlySerials)
    && empty($manualOnlySerials);

$availableOptionsMap = [];
foreach ($availablePeripheralOptions as $row) {
    $serial = trim((string)($row['serial_number'] ?? ''));
    if ($serial === '' || isset($manualMap[$serial])) {
        continue;
    }
    $availableOptionsMap[$serial] = $row;
}
$availablePeripheralOptions = array_values($availableOptionsMap);
?>
<!DOCTYPE html>
<html lang="<?php echo htmlspecialchars((string)($_SESSION['lang'] ?? 'it')); ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo htmlspecialchars(pdTxt('Scheda impianto', 'Plant detail')); ?> #<?php echo (int)$plantId; ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <style>
        .plant-meta-card { border-left: 4px solid #0d6efd; }
        .status-chip { min-width: 120px; }
        .code-link { font-weight: 600; }
        .table td, .table th { vertical-align: middle; }
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">
<?php require 'navbar.php'; ?>

<div class="container my-4 flex-grow-1">
    <div class="d-flex justify-content-between align-items-center mb-3">
        <div>
            <h4 class="mb-1"><i class="fas fa-industry"></i> <?php echo htmlspecialchars((string)($master['nickname'] ?? 'Impianto')); ?></h4>
            <div class="text-muted">ID impianto: <strong><?php echo (int)$plantId; ?></strong></div>
        </div>
        <div class="d-flex gap-2">
            <a href="settings.php" class="btn btn-outline-primary btn-sm"><i class="fas fa-cogs"></i> <?php echo htmlspecialchars(pdTxt('Gestione impianti', 'Plant management')); ?></a>
            <a href="index.php" class="btn btn-outline-secondary btn-sm"><i class="fas fa-arrow-left"></i> <?php echo htmlspecialchars(pdTxt('Dashboard', 'Dashboard')); ?></a>
        </div>
    </div>
    <?php if (is_array($flash) && !empty($flash['message'])): ?>
        <div class="alert alert-<?php echo htmlspecialchars((string)($flash['type'] ?? 'info')); ?> py-2"><?php echo htmlspecialchars((string)$flash['message']); ?></div>
    <?php endif; ?>
    <?php foreach ($runtimeWarnings as $runtimeWarning): ?>
        <div class="alert alert-warning py-2"><?php echo htmlspecialchars((string)$runtimeWarning); ?></div>
    <?php endforeach; ?>
    <?php if (!$manualFeatureReady): ?>
        <div class="alert alert-warning"><?php echo htmlspecialchars(pdTxt('La gestione periferiche manuali richiede la migration SQL Step 2.17.', 'Manual peripheral management requires SQL migration Step 2.17.')); ?></div>
    <?php endif; ?>
    <?php if ($mismatchCount > 0): ?>
        <div class="alert alert-danger d-flex flex-column gap-2">
            <div><?php echo htmlspecialchars(pdTxt('Sono presenti discrepanze tra periferiche assegnate manualmente e periferiche rilevate dal sistema.', 'There are mismatches between manually assigned peripherals and peripherals detected by the system.')); ?></div>
            <div class="small">
                <?php echo htmlspecialchars(pdTxt('Rilevate non confermate', 'Detected not confirmed')); ?>: <strong><?php echo count($detectedOnlySerials); ?></strong> |
                <?php echo htmlspecialchars(pdTxt('Manuali non rilevate', 'Manual not detected')); ?>: <strong><?php echo count($manualOnlySerials); ?></strong>
            </div>
            <?php if ($canManagePeripherals && $manualFeatureReady): ?>
                <?php if ($canConfirmDetectedPeripherals): ?>
                    <form method="POST" class="d-inline-block" onsubmit="return confirm('<?php echo htmlspecialchars(pdTxt('Confermi l allineamento automatico delle periferiche rilevate online?', 'Confirm automatic alignment of online-detected peripherals?')); ?>');">
                        <input type="hidden" name="plant_id" value="<?php echo (int)$plantId; ?>">
                        <input type="hidden" name="action" value="confirm_detected_peripherals">
                        <button type="submit" class="btn btn-sm btn-light border-danger text-danger">
                            <i class="fas fa-check"></i> <?php echo htmlspecialchars(pdTxt('Conferma periferiche rilevate', 'Confirm detected peripherals')); ?>
                        </button>
                    </form>
                <?php elseif (!empty($manualOnlySerials)): ?>
                    <div class="small">
                        <?php echo htmlspecialchars(pdTxt('Conferma automatica non disponibile: prima rimuovi o verifica i seriali manuali non rilevati online.', 'Automatic confirmation unavailable: first remove or verify manually assigned serials not detected online.')); ?>
                    </div>
                <?php endif; ?>
            <?php endif; ?>
        </div>
    <?php endif; ?>

    <div class="row g-3">
        <div class="col-lg-7">
            <div class="card shadow-sm plant-meta-card">
                <div class="card-header"><strong><?php echo htmlspecialchars(pdTxt('Dati impianto', 'Plant data')); ?></strong></div>
                <div class="card-body">
                    <div class="row g-3">
                        <div class="col-md-6">
                            <div><strong><?php echo htmlspecialchars(pdTxt('Seriale master', 'Master serial')); ?>:</strong> <a class="text-decoration-none code-link" href="serial_detail.php?serial=<?php echo urlencode((string)($master['serial_number'] ?? '')); ?>"><?php echo htmlspecialchars((string)($master['serial_number'] ?? '-')); ?></a></div>
                            <div><strong><?php echo htmlspecialchars(pdTxt('Tipo impianto', 'Plant type')); ?>:</strong> <?php echo htmlspecialchars($plantKindLabel); ?></div>
                            <div><strong><?php echo htmlspecialchars(pdTxt('Stato', 'Status')); ?>:</strong> <?php if ($plantOnline): ?><span class="badge bg-success"><?php echo htmlspecialchars(pdTxt('Online', 'Online')); ?></span><?php else: ?><span class="badge bg-secondary"><?php echo htmlspecialchars(pdTxt('Offline', 'Offline')); ?></span><?php endif; ?><?php if ($isPermanentOffline): ?><span class="badge bg-dark ms-1"><?php echo htmlspecialchars(pdTxt('Offline permanente', 'Permanent offline')); ?></span><?php endif; ?></div>
                            <div><strong><?php echo htmlspecialchars(pdTxt('Ultima connessione', 'Last connection')); ?>:</strong> <?php echo htmlspecialchars($lastSeenValue !== '' ? $lastSeenValue : '-'); ?></div>
                        </div>
                        <div class="col-md-6">
                            <div><strong><?php echo htmlspecialchars(pdTxt('Indirizzo', 'Address')); ?>:</strong>
                                <?php if ($plantAddressMapUrl !== ''): ?>
                                    <a href="<?php echo htmlspecialchars($plantAddressMapUrl); ?>" target="_blank" class="text-decoration-none">
                                        <?php echo htmlspecialchars((string)($master['address'] ?? '-')); ?>
                                    </a>
                                <?php else: ?>
                                    <?php echo htmlspecialchars((string)($master['address'] ?? '-')); ?>
                                <?php endif; ?>
                            </div>
                            <div><strong><?php echo htmlspecialchars(pdTxt('Data creazione', 'Creation date')); ?>:</strong> <?php echo htmlspecialchars((string)($master['created_at'] ?? '-')); ?></div>
                            <?php if ($hasDeliveryDateColumn): ?><div><strong><?php echo htmlspecialchars(pdTxt('Data consegna', 'Delivery date')); ?>:</strong> <?php echo htmlspecialchars((string)($master['delivery_date'] ?? '-')); ?></div><?php endif; ?>
                            <div><strong>Firmware:</strong> <?php echo htmlspecialchars((string)($master['fw_version'] ?? '-')); ?></div>
                        </div>
                    </div>
                    <?php if ($hasNotesColumn): ?>
                        <hr>
                        <div><strong><?php echo htmlspecialchars(pdTxt('Note impianto', 'Plant notes')); ?>:</strong></div>
                        <div class="text-muted"><?php echo nl2br(htmlspecialchars((string)($master['notes'] ?? '-'))); ?></div>
                    <?php endif; ?>
                </div>
            </div>
        </div>
        <div class="col-lg-5">
            <div class="card shadow-sm">
                <div class="card-header"><strong><?php echo htmlspecialchars(pdTxt('Soggetti associati', 'Assigned parties')); ?></strong></div>
                <div class="card-body small">
                    <div class="mb-2"><strong><?php echo htmlspecialchars(pdTxt('Proprietario', 'Owner')); ?>:</strong><br><?php echo htmlspecialchars((string)($master['owner_name'] ?: $master['owner_company'] ?: $master['owner_email'] ?: 'N/D')); ?></div>
                    <div class="mb-2"><strong><?php echo htmlspecialchars(pdTxt('Manutentore', 'Maintainer')); ?>:</strong><br><?php echo htmlspecialchars((string)($master['maintainer_name'] ?: $master['maintainer_company'] ?: $master['maintainer_email'] ?: 'N/D')); ?></div>
                    <?php if ($hasBuilderColumn): ?><div class="mb-2"><strong><?php echo htmlspecialchars(pdTxt('Costruttore', 'Builder')); ?>:</strong><br><?php echo htmlspecialchars((string)($master['builder_name'] ?: $master['builder_company'] ?: $master['builder_email'] ?: 'N/D')); ?></div><?php endif; ?>
                    <div><strong><?php echo htmlspecialchars(pdTxt('Creatore record', 'Record creator')); ?>:</strong><br><?php echo htmlspecialchars((string)($master['creator_name'] ?: $master['creator_company'] ?: $master['creator_email'] ?: 'N/D')); ?></div>
                </div>
            </div>
        </div>
    </div>

    <?php if ($isAdmin): ?>
    <div class="card shadow-sm mt-3">
        <div class="card-header"><strong><?php echo htmlspecialchars(pdTxt('Modifica impianto', 'Edit plant')); ?></strong></div>
        <div class="card-body">
            <form method="POST">
                <input type="hidden" name="plant_id" value="<?php echo (int)$plantId; ?>">
                <input type="hidden" name="action" value="update_plant_admin">
                <div class="row g-3">
                    <div class="col-lg-4">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Nickname', 'Nickname')); ?></label>
                        <input type="text" name="nickname" class="form-control" value="<?php echo htmlspecialchars((string)($master['nickname'] ?? '')); ?>" required>
                    </div>
                    <div class="col-lg-5">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Indirizzo', 'Address')); ?></label>
                        <input type="text" name="address" class="form-control" value="<?php echo htmlspecialchars((string)($master['address'] ?? '')); ?>">
                    </div>
                    <div class="col-lg-3">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Giorni log', 'Log days')); ?></label>
                        <input type="number" name="log_retention_days" class="form-control" min="1" max="365" value="<?php echo (int)($master['log_retention_days'] ?? 30); ?>">
                    </div>
                    <div class="col-lg-3">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Tipo impianto', 'Plant type')); ?></label>
                        <select name="plant_kind" class="form-select">
                            <option value="" <?php echo $plantKind === '' ? 'selected' : ''; ?>><?php echo htmlspecialchars(pdTxt('Non specificato', 'Not specified')); ?></option>
                            <option value="display" <?php echo $plantKind === 'display' ? 'selected' : ''; ?>>Display</option>
                            <option value="standalone" <?php echo $plantKind === 'standalone' ? 'selected' : ''; ?>>Standalone</option>
                            <option value="rewamping" <?php echo $plantKind === 'rewamping' ? 'selected' : ''; ?>>Rewamping</option>
                        </select>
                    </div>
                    <?php if ($hasDeliveryDateColumn): ?>
                    <div class="col-lg-3">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Data consegna', 'Delivery date')); ?></label>
                        <input type="date" name="delivery_date" class="form-control" value="<?php echo htmlspecialchars((string)($master['delivery_date'] ?? '')); ?>">
                    </div>
                    <?php endif; ?>
                    <div class="col-lg-3 d-flex align-items-end">
                        <div class="form-check form-switch mb-2">
                            <input class="form-check-input" type="checkbox" role="switch" id="plantDetailPermanentOffline" name="permanently_offline" value="1" <?php echo $isPermanentOffline ? 'checked' : ''; ?>>
                            <label class="form-check-label" for="plantDetailPermanentOffline"><?php echo htmlspecialchars(pdTxt('Impianto permanentemente offline', 'Permanent offline plant')); ?></label>
                        </div>
                    </div>
                    <div class="col-lg-4">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Proprietario', 'Owner')); ?></label>
                        <select name="owner_id" class="form-select">
                            <option value=""><?php echo htmlspecialchars(pdTxt('Nessuno', 'None')); ?></option>
                            <?php foreach ($adminOwnerOptions as $ownerOption): ?>
                                <option value="<?php echo (int)($ownerOption['id'] ?? 0); ?>" <?php echo ((int)($ownerOption['id'] ?? 0) === (int)($master['owner_id'] ?? 0)) ? 'selected' : ''; ?>>
                                    <?php echo htmlspecialchars(pdUserDisplayLabel($ownerOption, $hasUsersNameColumn, $hasUsersCompanyColumn, $hasUsersPortalAccessLevelColumn)); ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <div class="col-lg-4">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Manutentore', 'Maintainer')); ?></label>
                        <select name="maintainer_id" class="form-select">
                            <option value=""><?php echo htmlspecialchars(pdTxt('Nessuno', 'None')); ?></option>
                            <?php foreach ($adminMaintainerOptions as $maintainerOption): ?>
                                <option value="<?php echo (int)($maintainerOption['id'] ?? 0); ?>" <?php echo ((int)($maintainerOption['id'] ?? 0) === (int)($master['maintainer_id'] ?? 0)) ? 'selected' : ''; ?>>
                                    <?php echo htmlspecialchars(pdUserDisplayLabel($maintainerOption, $hasUsersNameColumn, $hasUsersCompanyColumn, $hasUsersPortalAccessLevelColumn)); ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <?php if ($hasBuilderColumn): ?>
                    <div class="col-lg-4">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Costruttore', 'Builder')); ?></label>
                        <select name="builder_id" class="form-select">
                            <option value=""><?php echo htmlspecialchars(pdTxt('Nessuno', 'None')); ?></option>
                            <?php foreach ($adminBuilderOptions as $builderOption): ?>
                                <option value="<?php echo (int)($builderOption['id'] ?? 0); ?>" <?php echo ((int)($builderOption['id'] ?? 0) === (int)($master['builder_id'] ?? 0)) ? 'selected' : ''; ?>>
                                    <?php echo htmlspecialchars(pdUserDisplayLabel($builderOption, $hasUsersNameColumn, $hasUsersCompanyColumn, $hasUsersPortalAccessLevelColumn)); ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <?php endif; ?>
                    <?php if ($hasNotesColumn): ?>
                    <div class="col-12">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Note impianto', 'Plant notes')); ?></label>
                        <textarea name="notes" class="form-control" rows="3"><?php echo htmlspecialchars((string)($master['notes'] ?? '')); ?></textarea>
                    </div>
                    <?php endif; ?>
                    <div class="col-12 d-flex justify-content-end">
                        <button type="submit" class="btn btn-primary"><i class="fas fa-save"></i> <?php echo htmlspecialchars(pdTxt('Salva dati impianto', 'Save plant data')); ?></button>
                    </div>
                </div>
            </form>
        </div>
    </div>
    <?php endif; ?>

    <div class="card shadow-sm mt-3">
        <div class="card-header d-flex justify-content-between align-items-center">
            <strong><?php echo htmlspecialchars(pdTxt('Periferiche assegnate manualmente', 'Manually assigned peripherals')); ?></strong>
            <span class="badge bg-primary"><?php echo count($manualPeripherals); ?></span>
        </div>
        <div class="card-body">
            <?php if ($canManagePeripherals && $manualFeatureReady): ?>
                <form method="POST" class="row g-2 align-items-end mb-3">
                    <input type="hidden" name="plant_id" value="<?php echo (int)$plantId; ?>">
                    <input type="hidden" name="action" value="assign_manual_peripheral">
                    <div class="col-md-8">
                        <label class="form-label"><?php echo htmlspecialchars(pdTxt('Aggiungi periferica manuale', 'Add manual peripheral')); ?></label>
                        <input class="form-control" type="text" name="serial_number" list="manualPeripheralOptions" placeholder="Es. 202603039989" required>
                        <datalist id="manualPeripheralOptions">
                            <?php foreach ($availablePeripheralOptions as $opt): ?>
                                <option value="<?php echo htmlspecialchars((string)($opt['serial_number'] ?? '')); ?>"><?php echo htmlspecialchars((string)($opt['product_label'] ?? 'Periferica') . ' | ' . pdDeviceModeLabel((string)($opt['product_type_code'] ?? ''), $opt['device_mode'] ?? null)); ?></option>
                            <?php endforeach; ?>
                        </datalist>
                    </div>
                    <div class="col-md-4 d-grid">
                        <button type="submit" class="btn btn-primary"><i class="fas fa-link"></i> <?php echo htmlspecialchars(pdTxt('Assegna periferica', 'Assign peripheral')); ?></button>
                    </div>
                </form>
                <div class="alert alert-light border small"><?php echo htmlspecialchars(pdTxt("Qui puoi gestire le periferiche attese sugli impianti offline. Se l'impianto poi va online, il portale confronterà le schede attese con quelle realmente rilevate.", 'Here you can manage expected peripherals on offline plants. If the plant later goes online, the portal will compare expected boards with those actually detected.')); ?></div>
            <?php endif; ?>
            <div class="table-responsive">
                <table class="table table-sm table-hover">
                    <thead class="table-light"><tr><th><?php echo htmlspecialchars(pdTxt('Seriale', 'Serial')); ?></th><th><?php echo htmlspecialchars(pdTxt('Tipo', 'Type')); ?></th><th><?php echo htmlspecialchars(pdTxt('Modalita', 'Mode')); ?></th><th><?php echo htmlspecialchars(pdTxt('Fonte', 'Source')); ?></th><th><?php echo htmlspecialchars(pdTxt('Azioni', 'Actions')); ?></th></tr></thead>
                    <tbody>
                        <?php if (empty($manualPeripherals)): ?>
                            <tr><td colspan="5" class="text-center text-muted"><?php echo htmlspecialchars(pdTxt('Nessuna periferica assegnata manualmente.', 'No manually assigned peripherals.')); ?></td></tr>
                        <?php else: ?>
                            <?php foreach ($manualPeripherals as $row): $rowSerial = trim((string)($row['serial_number'] ?? '')); ?>
                                <tr>
                                    <td><a class="text-decoration-none code-link" href="serial_detail.php?serial=<?php echo urlencode($rowSerial); ?>"><?php echo htmlspecialchars($rowSerial); ?></a></td>
                                    <td><?php echo htmlspecialchars((string)($row['product_label'] ?? ($row['product_type_code'] ?? 'Periferica'))); ?></td>
                                    <td><?php echo htmlspecialchars(pdDeviceModeLabel((string)($row['product_type_code'] ?? ''), $row['device_mode'] ?? null)); ?></td>
                                    <td><span class="badge bg-primary"><?php echo htmlspecialchars(pdTxt('Manuale', 'Manual')); ?></span></td>
                                    <td><?php if ($canManagePeripherals): ?><form method="POST" class="d-inline" onsubmit="return confirm('Rimuovere questa assegnazione manuale?');"><input type="hidden" name="plant_id" value="<?php echo (int)$plantId; ?>"><input type="hidden" name="action" value="remove_manual_peripheral"><input type="hidden" name="serial_number" value="<?php echo htmlspecialchars($rowSerial); ?>"><button type="submit" class="btn btn-sm btn-outline-danger"><i class="fas fa-unlink"></i></button></form><?php endif; ?></td>
                                </tr>
                            <?php endforeach; ?>
                        <?php endif; ?>
                    </tbody>
                </table>
            </div>
        </div>
    </div>

    <div class="card shadow-sm mt-3">
        <div class="card-header d-flex justify-content-between align-items-center">
            <strong><?php echo htmlspecialchars(pdTxt('Confronto atteso / reale', 'Expected / actual comparison')); ?></strong>
            <span class="badge <?php echo $mismatchCount > 0 ? 'bg-danger' : 'bg-success'; ?>"><?php echo $mismatchCount > 0 ? htmlspecialchars(pdTxt('Da verificare', 'Needs review')) : htmlspecialchars(pdTxt('Allineato', 'Aligned')); ?></span>
        </div>
        <div class="card-body">
            <div class="table-responsive">
                <table class="table table-sm table-hover">
                    <thead class="table-light"><tr><th><?php echo htmlspecialchars(pdTxt('Seriale', 'Serial')); ?></th><th><?php echo htmlspecialchars(pdTxt('Tipo', 'Type')); ?></th><th><?php echo htmlspecialchars(pdTxt('Modalita', 'Mode')); ?></th><th><?php echo htmlspecialchars(pdTxt('Origine', 'Source')); ?></th><th><?php echo htmlspecialchars(pdTxt('Esito', 'Result')); ?></th><th><?php echo htmlspecialchars(pdTxt('Ultimo dato', 'Last data')); ?></th></tr></thead>
                    <tbody>
                        <?php if (empty($comparisonRows)): ?>
                            <tr><td colspan="6" class="text-center text-muted"><?php echo htmlspecialchars(pdTxt('Nessun confronto disponibile.', 'No comparison available.')); ?></td></tr>
                        <?php else: ?>
                            <?php foreach ($comparisonRows as $row): ?>
                                <tr>
                                    <td><a class="text-decoration-none code-link" href="serial_detail.php?serial=<?php echo urlencode((string)$row['serial_number']); ?>"><?php echo htmlspecialchars((string)$row['serial_number']); ?></a></td>
                                    <td><?php echo htmlspecialchars((string)$row['product_label']); ?></td>
                                    <td><?php echo htmlspecialchars(pdDeviceModeLabel((string)$row['product_type_code'], $row['device_mode'] ?? null)); ?></td>
                                    <td><?php echo htmlspecialchars((string)$row['source']); ?></td>
                                    <td><span class="badge status-chip <?php echo htmlspecialchars((string)$row['status_class']); ?>"><?php echo htmlspecialchars((string)$row['status_label']); ?></span></td>
                                    <td><?php echo htmlspecialchars((string)$row['last_seen']); ?></td>
                                </tr>
                            <?php endforeach; ?>
                        <?php endif; ?>
                    </tbody>
                </table>
            </div>
        </div>
    </div>

    <div class="card shadow-sm mt-3">
        <div class="card-header d-flex justify-content-between align-items-center">
            <strong><?php echo htmlspecialchars(pdTxt('Periferiche rilevate online', 'Peripherals detected online')); ?></strong>
            <span class="badge bg-info text-dark"><?php echo count($actualPeripherals); ?></span>
        </div>
        <div class="card-body">
            <div class="table-responsive">
                <table class="table table-sm table-hover">
                    <thead class="table-light"><tr><th><?php echo htmlspecialchars(pdTxt('Seriale', 'Serial')); ?></th><th><?php echo htmlspecialchars(pdTxt('Tipo', 'Type')); ?></th><th><?php echo htmlspecialchars(pdTxt('Modalita', 'Mode')); ?></th><th>Grp</th><th>FW</th><th><?php echo htmlspecialchars(pdTxt('Ultimo dato', 'Last data')); ?></th></tr></thead>
                    <tbody>
                        <?php if (empty($actualPeripherals)): ?>
                            <tr><td colspan="6" class="text-center text-muted"><?php echo htmlspecialchars(pdTxt('Nessuna periferica rilevata.', 'No peripherals detected.')); ?></td></tr>
                        <?php else: ?>
                            <?php foreach ($actualPeripherals as $row): $actualSerial = trim((string)($row['slave_sn'] ?? '')); ?>
                                <tr>
                                    <td><a class="text-decoration-none code-link" href="serial_detail.php?serial=<?php echo urlencode($actualSerial); ?>"><?php echo htmlspecialchars($actualSerial); ?></a></td>
                                    <td><?php echo htmlspecialchars((string)($row['product_label'] ?? ($row['product_type_code'] ?? 'Periferica'))); ?></td>
                                    <td><?php echo htmlspecialchars(pdDeviceModeLabel((string)($row['product_type_code'] ?? ''), $row['device_mode'] ?? null)); ?></td>
                                    <td><?php echo htmlspecialchars((string)($row['slave_grp'] ?? '-')); ?></td>
                                    <td><?php echo htmlspecialchars((string)($row['fw_version'] ?? '-')); ?></td>
                                    <td><?php echo htmlspecialchars((string)($row['recorded_at'] ?? '-')); ?></td>
                                </tr>
                            <?php endforeach; ?>
                        <?php endif; ?>
                    </tbody>
                </table>
            </div>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
