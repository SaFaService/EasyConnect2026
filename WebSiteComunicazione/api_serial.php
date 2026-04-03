<?php
session_start();
require 'config.php';
require_once 'auth_common.php';
header('Content-Type: application/json');

/*
 * API serial-centric (Step 2.4)
 * ============================================================
 * Questo endpoint centralizza le operazioni sui seriali:
 * - reserve_next_serial     : riserva automaticamente YYYYMMTTNNNN
 * - check_serial            : verifica stato e metadati seriale
 * - register_manual_serial  : registra seriale manuale (casi eccezionali)
 * - assign_serial_to_master : assegna seriale a una master esistente
 *
 * NOTE OPERATIVE:
 * - Verifica seriale/lista motivazioni: ADMIN/BUILDER/MAINTAINER.
 * - Lifecycle + assegnazione master: richiedono abilitazione serial_lifecycle.
 * - Riserva seriali: richiede abilitazione serial_reserve.
 * - Registrazione manuale seriale: ADMIN.
 * - In fase successiva l'app Python usera' la stessa logica DB.
 * - Le operazioni critiche scrivono audit in serial_audit_logs.
 */

/* ------------------------------------------------------------
 * Helpers di output JSON
 * ------------------------------------------------------------ */
function jsonOk(array $data = []): void {
    echo json_encode(array_merge(['status' => 'ok'], $data));
    exit;
}

function jsonErr(string $message, int $httpCode = 200): void {
    http_response_code($httpCode);
    echo json_encode(['status' => 'error', 'message' => $message]);
    exit;
}

/* ------------------------------------------------------------
 * Autenticazione e autorizzazione base
 * ------------------------------------------------------------ */
if (!isset($_SESSION['user_id'])) {
    jsonErr('Non autorizzato', 401);
}

if (!isset($_SESSION['user_role'])) {
    jsonErr('Permessi insufficienti.', 403);
}

$userId = (int)$_SESSION['user_id'];
$userRole = (string)$_SESSION['user_role'];
$userPermissions = ecAuthCurrentUserPermissions($pdo, $userId);
$canSerialLifecycle = !empty($userPermissions['serial_lifecycle']);
$canSerialReserve = !empty($userPermissions['serial_reserve']);

/* ------------------------------------------------------------
 * Parse input JSON
 * ------------------------------------------------------------ */
$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    $input = [];
}

$action = $input['action'] ?? '';
if ($action === '') {
    jsonErr('Azione mancante');
}

function requireActionRoles(string $action, string $currentRole): void {
    $acl = [
        // Operazioni operative condivise (abilitazione gestita anche da permessi granulari)
        'reserve_next_serial' => ['admin', 'builder', 'maintainer'],
        'register_manual_serial' => ['admin'],
        'assign_serial_to_master' => ['admin', 'builder', 'maintainer'],
        // Shared operations for field/service roles
        'check_serial' => ['admin', 'builder', 'maintainer'],
        'list_status_reasons' => ['admin', 'builder', 'maintainer'],
        'set_serial_status' => ['admin', 'builder', 'maintainer'],
    ];

    if (!isset($acl[$action])) {
        return;
    }
    if (!in_array($currentRole, $acl[$action], true)) {
        jsonErr('Permessi insufficienti per questa operazione.', 403);
    }
}

function requireActionPermission(string $action, bool $canSerialLifecycle, bool $canSerialReserve): void {
    if (in_array($action, ['set_serial_status', 'assign_serial_to_master'], true) && !$canSerialLifecycle) {
        jsonErr('Gestione seriali non abilitata per questa utenza.', 403);
    }
    if ($action === 'reserve_next_serial' && !$canSerialReserve) {
        jsonErr('Riserva seriali non abilitata per questa utenza.', 403);
    }
}

requireActionRoles($action, $userRole);
requireActionPermission($action, $canSerialLifecycle, $canSerialReserve);

/* ------------------------------------------------------------
 * Helper audit seriali (tabella dedicata, indipendente da masters)
 * ------------------------------------------------------------ */
function writeSerialAudit(PDO $pdo, int $actorUserId, string $action, string $details, ?string $serial = null, ?int $masterId = null): void {
    try {
        $stmt = $pdo->prepare("
            INSERT INTO serial_audit_logs (
                actor_user_id,
                action,
                serial_number,
                master_id,
                details
            ) VALUES (?, ?, ?, ?, ?)
        ");
        $stmt->execute([$actorUserId, $action, $serial, $masterId, $details]);
    } catch (Throwable $e) {
        // Non blocchiamo il flusso operativo per errore di audit.
    }
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

function canUserAccessMaster(PDO $pdo, int $masterId, int $userId, string $userRole): bool {
    if ($masterId <= 0) {
        return false;
    }
    if ($userRole === 'admin') {
        return true;
    }
    $hasBuilderMasterCol = columnExists($pdo, 'masters', 'builder_id');
    $sql = "SELECT 1 FROM masters WHERE id = ? AND (creator_id = ? OR owner_id = ? OR maintainer_id = ?";
    $params = [$masterId, $userId, $userId, $userId];
    if ($hasBuilderMasterCol) {
        $sql .= " OR builder_id = ?";
        $params[] = $userId;
    }
    $sql .= ") LIMIT 1";
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    return (bool)$stmt->fetchColumn();
}

function userHasMasterHistoryForSerial(PDO $pdo, string $serial, int $userId): bool {
    $hasBuilderMasterCol = columnExists($pdo, 'masters', 'builder_id');
    $masterScope = "(m.creator_id = ? OR m.owner_id = ? OR m.maintainer_id = ?" . ($hasBuilderMasterCol ? " OR m.builder_id = ?" : "") . ")";
    $baseParams = [$serial, $userId, $userId, $userId];
    if ($hasBuilderMasterCol) {
        $baseParams[] = $userId;
    }

    if (tableExists($pdo, 'serial_audit_logs')) {
        $sql = "
            SELECT 1
            FROM serial_audit_logs sal
            JOIN masters m ON m.id = sal.master_id
            WHERE sal.serial_number = ?
              AND sal.master_id IS NOT NULL
              AND $masterScope
            LIMIT 1
        ";
        $stmt = $pdo->prepare($sql);
        $stmt->execute($baseParams);
        if ((bool)$stmt->fetchColumn()) {
            return true;
        }
    }

    if (tableExists($pdo, 'serial_lifecycle_events')) {
        $sqlEvt = "
            SELECT 1
            FROM serial_lifecycle_events sle
            JOIN masters m ON m.id = sle.master_id
            WHERE sle.serial_number = ?
              AND sle.master_id IS NOT NULL
              AND $masterScope
            LIMIT 1
        ";
        $stmtEvt = $pdo->prepare($sqlEvt);
        $stmtEvt->execute($baseParams);
        if ((bool)$stmtEvt->fetchColumn()) {
            return true;
        }
    }

    return false;
}

function canUserAccessSerial(PDO $pdo, string $serial, int $userId, string $userRole): bool {
    if ($serial === '') {
        return false;
    }
    if ($userRole === 'admin') {
        return true;
    }
    if (!tableExists($pdo, 'device_serials')) {
        return false;
    }

    $hasOwnerUser = columnExists($pdo, 'device_serials', 'owner_user_id');
    $hasAssignedUser = columnExists($pdo, 'device_serials', 'assigned_user_id');
    $hasAssignedMaster = columnExists($pdo, 'device_serials', 'assigned_master_id');
    $select = [
        'serial_number',
        ($hasAssignedMaster ? 'assigned_master_id' : 'NULL AS assigned_master_id'),
        ($hasOwnerUser ? 'owner_user_id' : 'NULL AS owner_user_id'),
        ($hasAssignedUser ? 'assigned_user_id' : 'NULL AS assigned_user_id'),
    ];
    $stmt = $pdo->prepare("SELECT " . implode(', ', $select) . " FROM device_serials WHERE serial_number = ? LIMIT 1");
    $stmt->execute([$serial]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    if (!$row) {
        return false;
    }

    $masterId = isset($row['assigned_master_id']) ? (int)$row['assigned_master_id'] : 0;
    if ($masterId > 0 && canUserAccessMaster($pdo, $masterId, $userId, $userRole)) {
        return true;
    }
    if ($hasOwnerUser && isset($row['owner_user_id']) && (int)$row['owner_user_id'] === $userId) {
        return true;
    }
    if ($hasAssignedUser && isset($row['assigned_user_id']) && (int)$row['assigned_user_id'] === $userId) {
        return true;
    }
    if (userHasMasterHistoryForSerial($pdo, $serial, $userId)) {
        return true;
    }
    return false;
}

$hasDeviceModeColumn = tableExists($pdo, 'device_serials') && columnExists($pdo, 'device_serials', 'device_mode');

function writeSerialLifecycleEvent(
    PDO $pdo,
    string $serial,
    ?string $fromStatus,
    string $toStatus,
    ?string $reasonCode,
    ?string $reasonDetails,
    ?string $replacedBySerial,
    int $actorUserId,
    ?int $masterId = null
): void {
    try {
        if (!tableExists($pdo, 'serial_lifecycle_events')) {
            return;
        }

        $stmt = $pdo->prepare("
            INSERT INTO serial_lifecycle_events (
                serial_number,
                from_status,
                to_status,
                reason_code,
                reason_details,
                replaced_by_serial,
                actor_user_id,
                master_id
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ");
        $stmt->execute([
            $serial,
            $fromStatus,
            $toStatus,
            $reasonCode,
            $reasonDetails,
            $replacedBySerial,
            $actorUserId,
            $masterId
        ]);
    } catch (Throwable $e) {
        // Non blocchiamo l'operazione principale per errore di event log.
    }
}

function reasonCodeAllowed(PDO $pdo, string $targetStatus, string $reasonCode): bool {
    if (!tableExists($pdo, 'serial_status_reasons')) {
        return false;
    }
    $stmt = $pdo->prepare("
        SELECT 1
        FROM serial_status_reasons
        WHERE reason_code = ?
          AND is_active = 1
          AND (applies_to_status = ? OR applies_to_status = 'any')
        LIMIT 1
    ");
    $stmt->execute([$reasonCode, $targetStatus]);
    return (bool)$stmt->fetchColumn();
}

/* ------------------------------------------------------------
 * Validazioni comuni
 * ------------------------------------------------------------ */
function validProductType(string $code): bool {
    return (bool)preg_match('/^(01|02|03|04|05)$/', $code);
}

function normalizeDeviceMode($value): ?int {
    if ($value === null) {
        return null;
    }
    $raw = trim((string)$value);
    if ($raw === '') {
        return null;
    }
    if (!preg_match('/^\d+$/', $raw)) {
        return null;
    }
    $mode = (int)$raw;
    return $mode > 0 ? $mode : null;
}

function allowedDeviceModesForProductType(string $productTypeCode): array {
    switch ($productTypeCode) {
        case '02':
            return [1, 2];
        case '03':
            return [1, 2, 3, 4, 5];
        case '04':
            return [1, 2, 3];
        default:
            return [];
    }
}

function deviceModeLabel(string $productTypeCode, ?int $deviceMode): string {
    if ($deviceMode === null) {
        return '';
    }
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
    }
    return $map[$deviceMode] ?? '';
}

function validateDeviceModeOrErr(?int $deviceMode, string $productTypeCode): void {
    if ($deviceMode === null) {
        return;
    }
    $allowedModes = allowedDeviceModesForProductType($productTypeCode);
    if (empty($allowedModes)) {
        jsonErr('device_mode non previsto per il tipo prodotto selezionato.');
    }
    if (!in_array($deviceMode, $allowedModes, true)) {
        jsonErr('device_mode non valido per il tipo prodotto selezionato.');
    }
}

function detectSerialScheme(string $serial): string {
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial)) {
        return 'v2_yyyymmttnnnn';
    }
    return 'legacy';
}

function parseV2Serial(string $serial): ?array {
    if (!preg_match('/^([0-9]{6})(01|02|03|04|05)([0-9]{4})$/', $serial, $m)) {
        return null;
    }
    return [
        'yyyymm' => $m[1],
        'type' => $m[2],
        'seq' => (int)$m[3],
        'seq_str' => $m[3],
    ];
}

function acquireMonthLock(PDO $pdo, string $yearMonth): bool {
    $lockName = 'ec_serial_month_' . $yearMonth;
    $stmt = $pdo->prepare("SELECT GET_LOCK(?, 10) AS lock_ok");
    $stmt->execute([$lockName]);
    $row = $stmt->fetch();
    return (int)($row['lock_ok'] ?? 0) === 1;
}

function releaseMonthLock(PDO $pdo, string $yearMonth): void {
    try {
        $lockName = 'ec_serial_month_' . $yearMonth;
        $stmt = $pdo->prepare("SELECT RELEASE_LOCK(?)");
        $stmt->execute([$lockName]);
    } catch (Throwable $e) {
        // Non bloccare il flusso per errore di unlock.
    }
}

/* ------------------------------------------------------------
 * ACTION: reserve_next_serial
 * ------------------------------------------------------------ */
if ($action === 'reserve_next_serial') {
    $productTypeCode = (string)($input['product_type_code'] ?? '');
    $notes = $input['notes'] ?? null;
    $deviceModeRaw = $input['device_mode'] ?? null;
    $deviceMode = normalizeDeviceMode($deviceModeRaw);

    if (!validProductType($productTypeCode)) {
        jsonErr('product_type_code non valido.');
    }
    if (trim((string)$deviceModeRaw) !== '' && $deviceMode === null) {
        jsonErr('device_mode non valido.');
    }
    validateDeviceModeOrErr($deviceMode, $productTypeCode);

    $yearMonth = date('Ym');
    $lockAcquired = false;

    try {
        // Lock mensile globale: garantisce progressivo unico nel mese, indipendente dal tipo.
        $lockAcquired = acquireMonthLock($pdo, $yearMonth);
        if (!$lockAcquired) {
            jsonErr('Sistema occupato: riprovare tra qualche secondo.');
        }

        $pdo->beginTransaction();

        $stmtMax = $pdo->prepare("
            SELECT MAX(CAST(RIGHT(serial_number, 4) AS UNSIGNED)) AS max_seq
            FROM device_serials
            WHERE serial_scheme = 'v2_yyyymmttnnnn'
              AND serial_number REGEXP '^[0-9]{6}(01|02|03|04|05)[0-9]{4}$'
              AND LEFT(serial_number, 6) = ?
            FOR UPDATE
        ");
        $stmtMax->execute([$yearMonth]);
        $row = $stmtMax->fetch();
        $maxSeq = (int)($row['max_seq'] ?? 0);

        $nextSeq = $maxSeq + 1;
        if ($nextSeq > 9999) {
            $pdo->rollBack();
            throw new RuntimeException('Progressivo mensile esaurito (max 9999).');
        }

        $serial = $yearMonth . $productTypeCode . str_pad((string)$nextSeq, 4, '0', STR_PAD_LEFT);

        $insertCols = [
            'serial_number',
            'serial_scheme',
            'product_type_code',
            'serial_locked',
            'lock_source',
            'assigned_user_id',
            'assigned_role',
            'status',
            'notes',
        ];
        $insertVals = ['?', "'v2_yyyymmttnnnn'", '?', '1', "'factory'", '?', '?', "'reserved'", '?'];
        $insertParams = [$serial, $productTypeCode, $userId, $userRole, $notes];
        if ($hasDeviceModeColumn) {
            $insertCols[] = 'device_mode';
            $insertVals[] = '?';
            $insertParams[] = $deviceMode;
        }
        $stmtIns = $pdo->prepare("
            INSERT INTO device_serials (" . implode(', ', $insertCols) . ")
            VALUES (" . implode(', ', $insertVals) . ")
        ");
        $stmtIns->execute($insertParams);

        writeSerialAudit(
            $pdo,
            $userId,
            'SERIAL_RESERVE',
            'Riservato seriale automatico tipo ' . $productTypeCode . ' con progressivo mensile globale',
            $serial,
            null
        );

        $pdo->commit();

        if ($lockAcquired) {
            releaseMonthLock($pdo, $yearMonth);
            $lockAcquired = false;
        }

        jsonOk([
            'serial' => $serial,
            'message' => 'Seriale riservato con successo.',
            'year_month' => $yearMonth,
            'monthly_seq' => $nextSeq,
            'device_mode' => $deviceMode,
            'device_mode_label' => deviceModeLabel($productTypeCode, $deviceMode)
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        if ($lockAcquired) {
            releaseMonthLock($pdo, $yearMonth);
            $lockAcquired = false;
        }

        if (stripos($e->getMessage(), 'Duplicate entry') !== false) {
            jsonErr('Conflitto seriale rilevato: riprovare.');
        }
        jsonErr('DB Error: ' . $e->getMessage());
    }
}

/* ------------------------------------------------------------
 * ACTION: check_serial
 * ------------------------------------------------------------ */
if ($action === 'check_serial') {
    $serial = trim((string)($input['serial_number'] ?? ''));
    if ($serial === '') {
        jsonErr('serial_number mancante');
    }

    try {
        $parsed = parseV2Serial($serial);

        $stmt = $pdo->prepare("
            SELECT
                ds.*,
                pt.key_name AS product_type_key,
                pt.label AS product_type_label,
                m.serial_number AS linked_master_serial
            FROM device_serials ds
            LEFT JOIN product_types pt ON pt.code = ds.product_type_code
            LEFT JOIN masters m ON m.id = ds.assigned_master_id
            WHERE ds.serial_number = ?
            LIMIT 1
        ");
        $stmt->execute([$serial]);
        $row = $stmt->fetch();

        // Caso 1: seriale esatto gia' presente in archivio.
        if ($row) {
            if ($userRole !== 'admin' && !canUserAccessSerial($pdo, $serial, $userId, $userRole)) {
                jsonErr('Seriale non accessibile per questa utenza.', 403);
            }
            if (array_key_exists('device_mode', $row)) {
                $row['device_mode_label'] = deviceModeLabel((string)($row['product_type_code'] ?? ''), isset($row['device_mode']) ? (int)$row['device_mode'] : null);
            }
            jsonOk([
                'exists' => true,
                'assignable' => false,
                'reason' => 'exact_exists',
                'message' => 'Seriale gia\' presente nel database.',
                'serial_number' => $serial,
                'record' => $row
            ]);
        }

        // Caso 2: se non e' nel formato ufficiale, non e' assegnabile.
        if ($parsed === null) {
            jsonOk([
                'exists' => false,
                'assignable' => false,
                'reason' => 'invalid_format',
                'message' => 'Formato seriale non valido. Usa YYYYMMTTNNNN.',
                'serial_number' => $serial
            ]);
        }

        // Caso 3: seriale non presente ma, per essere assegnabile, deve essere il prossimo progressivo del mese.
        $stmtMax = $pdo->prepare("
            SELECT MAX(CAST(RIGHT(serial_number, 4) AS UNSIGNED)) AS max_seq
            FROM device_serials
            WHERE serial_scheme = 'v2_yyyymmttnnnn'
              AND serial_number REGEXP '^[0-9]{6}(01|02|03|04|05)[0-9]{4}$'
              AND LEFT(serial_number, 6) = ?
        ");
        $stmtMax->execute([$parsed['yyyymm']]);
        $rowMax = $stmtMax->fetch();
        $maxSeq = (int)($rowMax['max_seq'] ?? 0);
        $expectedSeq = $maxSeq + 1;
        $expectedSeqStr = str_pad((string)$expectedSeq, 4, '0', STR_PAD_LEFT);
        $expectedSerial = $parsed['yyyymm'] . $parsed['type'] . $expectedSeqStr;

        if ((int)$parsed['seq'] !== $expectedSeq) {
            jsonOk([
                'exists' => false,
                'assignable' => false,
                'reason' => 'non_sequential_next',
                'message' => "Non assegnabile: il prossimo progressivo valido per {$parsed['yyyymm']} e' {$expectedSeqStr}.",
                'serial_number' => $serial,
                'expected_next_seq' => $expectedSeqStr,
                'expected_next_serial_for_type' => $expectedSerial
            ]);
        }

        // Caso 4: seriale non presente, ma progressivo mensile NNNN gia' usato da altro tipo.
        if ($parsed !== null) {
            $stmtSeq = $pdo->prepare("
                SELECT serial_number
                FROM device_serials
                WHERE serial_scheme = 'v2_yyyymmttnnnn'
                  AND serial_number REGEXP '^[0-9]{6}(01|02|03|04|05)[0-9]{4}$'
                  AND LEFT(serial_number, 6) = ?
                  AND RIGHT(serial_number, 4) = ?
                  AND serial_number <> ?
                LIMIT 1
            ");
            $stmtSeq->execute([$parsed['yyyymm'], $parsed['seq_str'], $serial]);
            $conflict = $stmtSeq->fetch();

            if ($conflict) {
                jsonOk([
                    'exists' => false,
                    'assignable' => false,
                    'reason' => 'monthly_seq_used',
                    'message' => "Non assegnabile: nel mese {$parsed['yyyymm']} il progressivo {$parsed['seq_str']} e' gia' usato da {$conflict['serial_number']}.",
                    'serial_number' => $serial,
                    'conflict_serial' => $conflict['serial_number'],
                    'conflict_year_month' => $parsed['yyyymm'],
                    'conflict_seq' => $parsed['seq_str']
                ]);
            }
        }

        // Caso 5: seriale disponibile e assegnabile.
        jsonOk([
            'exists' => false,
            'assignable' => true,
            'reason' => 'available',
            'message' => 'Seriale disponibile e assegnabile.',
            'serial_number' => $serial
        ]);
    } catch (Throwable $e) {
        jsonErr('DB Error: ' . $e->getMessage());
    }
}

/* ------------------------------------------------------------
 * ACTION: register_manual_serial
 * Uso: casi eccezionali, seriale inserito manualmente (senza app Python)
 * ------------------------------------------------------------ */
if ($action === 'register_manual_serial') {
    $serial = trim((string)($input['serial_number'] ?? ''));
    $productTypeCode = (string)($input['product_type_code'] ?? '');
    $notes = trim((string)($input['notes'] ?? ''));
    $deviceModeRaw = $input['device_mode'] ?? null;
    $deviceMode = normalizeDeviceMode($deviceModeRaw);

    if ($serial === '') {
        jsonErr('serial_number mancante');
    }
    if (!validProductType($productTypeCode)) {
        jsonErr('product_type_code non valido');
    }
    if (trim((string)$deviceModeRaw) !== '' && $deviceMode === null) {
        jsonErr('device_mode non valido.');
    }
    validateDeviceModeOrErr($deviceMode, $productTypeCode);

    $serialScheme = detectSerialScheme($serial);
    $parsed = parseV2Serial($serial);
    $lockAcquired = false;
    $lockYearMonth = null;

    try {
        // Coerenza tipo prodotto: se seriale e' v2, il TT nel seriale deve combaciare.
        if ($parsed !== null && $parsed['type'] !== $productTypeCode) {
            jsonErr('Seriale non coerente col tipo prodotto selezionato.');
        }

        // Regola richiesta: nel mese il progressivo NNNN deve essere unico a prescindere dal tipo.
        if ($parsed !== null) {
            $lockYearMonth = $parsed['yyyymm'];
            $lockAcquired = acquireMonthLock($pdo, $lockYearMonth);
            if (!$lockAcquired) {
                jsonErr('Sistema occupato: riprovare tra qualche secondo.');
            }

            // Regola sequenziale stretta: in un mese si puo' inserire solo il prossimo NNNN.
            $stmtMax = $pdo->prepare("
                SELECT MAX(CAST(RIGHT(serial_number, 4) AS UNSIGNED)) AS max_seq
                FROM device_serials
                WHERE serial_scheme = 'v2_yyyymmttnnnn'
                  AND serial_number REGEXP '^[0-9]{6}(01|02|03|04|05)[0-9]{4}$'
                  AND LEFT(serial_number, 6) = ?
            ");
            $stmtMax->execute([$parsed['yyyymm']]);
            $rowMax = $stmtMax->fetch();
            $maxSeq = (int)($rowMax['max_seq'] ?? 0);
            $expectedSeq = $maxSeq + 1;

            if ((int)$parsed['seq'] !== $expectedSeq) {
                throw new RuntimeException(
                    "Progressivo non sequenziale per il mese {$parsed['yyyymm']}: atteso " .
                    str_pad((string)$expectedSeq, 4, '0', STR_PAD_LEFT) .
                    ", ricevuto {$parsed['seq_str']}."
                );
            }

            $stmtSeq = $pdo->prepare("
                SELECT serial_number
                FROM device_serials
                WHERE serial_scheme = 'v2_yyyymmttnnnn'
                  AND serial_number REGEXP '^[0-9]{6}(01|02|03|04|05)[0-9]{4}$'
                  AND LEFT(serial_number, 6) = ?
                  AND RIGHT(serial_number, 4) = ?
                  AND serial_number <> ?
                LIMIT 1
            ");
            $stmtSeq->execute([$parsed['yyyymm'], $parsed['seq_str'], $serial]);
            $existingSeq = $stmtSeq->fetch();
            if ($existingSeq) {
                throw new RuntimeException('Progressivo mensile gia\' usato da un altro seriale: ' . $existingSeq['serial_number']);
            }
        }

        $insertCols = [
            'serial_number',
            'serial_scheme',
            'product_type_code',
            'serial_locked',
            'lock_source',
            'assigned_user_id',
            'assigned_role',
            'status',
            'notes',
        ];
        $insertVals = ['?', '?', '?', '1', "'manual'", '?', '?', "'provisioned'", '?'];
        $insertParams = [
            $serial,
            $serialScheme,
            $productTypeCode,
            $userId,
            $userRole,
            $notes !== '' ? $notes : 'Inserimento manuale da portale'
        ];
        if ($hasDeviceModeColumn) {
            $insertCols[] = 'device_mode';
            $insertVals[] = '?';
            $insertParams[] = $deviceMode;
        }
        $stmt = $pdo->prepare("
            INSERT INTO device_serials (" . implode(', ', $insertCols) . ")
            VALUES (" . implode(', ', $insertVals) . ")
        ");
        $stmt->execute($insertParams);

        writeSerialAudit(
            $pdo,
            $userId,
            'SERIAL_REGISTER_MANUAL',
            'Registrazione manuale seriale (tipo ' . $productTypeCode . ')',
            $serial,
            null
        );

        if ($lockAcquired && $lockYearMonth !== null) {
            releaseMonthLock($pdo, $lockYearMonth);
            $lockAcquired = false;
            $lockYearMonth = null;
        }

        jsonOk([
            'serial_number' => $serial,
            'serial_scheme' => $serialScheme,
            'message' => 'Seriale registrato manualmente.',
            'device_mode' => $deviceMode,
            'device_mode_label' => deviceModeLabel($productTypeCode, $deviceMode)
        ]);
    } catch (Throwable $e) {
        if ($lockAcquired && $lockYearMonth !== null) {
            releaseMonthLock($pdo, $lockYearMonth);
            $lockAcquired = false;
            $lockYearMonth = null;
        }

        if (stripos($e->getMessage(), 'Duplicate entry') !== false) {
            jsonErr('Seriale gia\' presente nel database.');
        }
        jsonErr('DB Error: ' . $e->getMessage());
    }
}

/* ------------------------------------------------------------
 * ACTION: list_status_reasons
 * Restituisce elenco motivazioni predefinite per combobox UI.
 * ------------------------------------------------------------ */
if ($action === 'list_status_reasons') {
    $fallback = [
        ['reason_code' => 'wrong_product_type', 'label_it' => 'Tipo prodotto errato', 'label_en' => 'Wrong product type', 'applies_to_status' => 'voided'],
        ['reason_code' => 'wrong_flashing', 'label_it' => 'Programmazione errata', 'label_en' => 'Wrong flashing', 'applies_to_status' => 'voided'],
        ['reason_code' => 'factory_test_discard', 'label_it' => 'Scarto collaudo fabbrica', 'label_en' => 'Factory test discard', 'applies_to_status' => 'voided'],
        ['reason_code' => 'field_replaced', 'label_it' => 'Sostituzione in campo', 'label_en' => 'Field replacement', 'applies_to_status' => 'retired'],
        ['reason_code' => 'damaged', 'label_it' => 'Dismesso per guasto', 'label_en' => 'Dismissed due to fault', 'applies_to_status' => 'retired'],
        ['reason_code' => 'plant_dismission', 'label_it' => 'Impianto dismesso', 'label_en' => 'Plant decommissioned', 'applies_to_status' => 'retired'],
        ['reason_code' => 'master_replaced', 'label_it' => 'Sostituito da altro seriale', 'label_en' => 'Replaced by another serial', 'applies_to_status' => 'retired'],
    ];

    try {
        if (!tableExists($pdo, 'serial_status_reasons')) {
            jsonOk(['reasons' => $fallback, 'source' => 'fallback']);
        }

        $stmt = $pdo->query("
            SELECT reason_code, label_it, label_en, applies_to_status
            FROM serial_status_reasons
            WHERE is_active = 1
            ORDER BY sort_order ASC, reason_code ASC
        ");
        $rows = $stmt->fetchAll();
        if (empty($rows)) {
            jsonOk(['reasons' => $fallback, 'source' => 'fallback_empty']);
        }
        jsonOk(['reasons' => $rows, 'source' => 'db']);
    } catch (Throwable $e) {
        jsonOk(['reasons' => $fallback, 'source' => 'fallback_error']);
    }
}

/* ------------------------------------------------------------
 * ACTION: set_serial_status
 * Gestione stato lifecycle:
 * - retired : seriale dismesso/sostituito
 * - voided  : seriale annullato per errore operativo
 * ------------------------------------------------------------ */
if ($action === 'set_serial_status') {
    $serial = trim((string)($input['serial_number'] ?? ''));
    $targetStatus = trim((string)($input['target_status'] ?? ''));
    $reasonCode = trim((string)($input['reason_code'] ?? ''));
    $reasonDetails = trim((string)($input['reason_details'] ?? ''));
    $replacedBy = trim((string)($input['replaced_by_serial'] ?? ''));

    if ($serial === '') {
        jsonErr('serial_number mancante');
    }
    if (!in_array($targetStatus, ['retired', 'voided', 'active'], true)) {
        jsonErr('target_status non valido (consentiti: active, retired, voided).');
    }
    if ($reasonCode === '') {
        jsonErr('reason_code mancante');
    }

    try {
        // Colonne lifecycle richieste: se mancano, chiediamo esplicitamente la migration.
        $requiredColumns = [
            'status_reason_code',
            'status_notes',
            'replaced_by_serial',
            'status_changed_at',
            'status_changed_by_user_id',
            'activated_at',
            'deactivated_at',
        ];
        foreach ($requiredColumns as $col) {
            if (!columnExists($pdo, 'device_serials', $col)) {
                jsonErr("Schema DB incompleto: manca colonna device_serials.$col. Eseguire migration lifecycle.");
            }
        }

        if (tableExists($pdo, 'serial_status_reasons') && !reasonCodeAllowed($pdo, $targetStatus, $reasonCode)) {
            jsonErr('Motivazione non valida per lo stato richiesto.');
        }

        $pdo->beginTransaction();

        $stmt = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
        $stmt->execute([$serial]);
        $row = $stmt->fetch();
        if (!$row) {
            $pdo->rollBack();
            jsonErr('Seriale non trovato.');
        }
        if ($userRole !== 'admin' && !canUserAccessSerial($pdo, $serial, $userId, $userRole)) {
            $pdo->rollBack();
            jsonErr('Seriale non accessibile per questa utenza.', 403);
        }

        if ($replacedBy !== '') {
            if ($replacedBy === $serial) {
                $pdo->rollBack();
                jsonErr('replaced_by_serial non puo\' coincidere con serial_number.');
            }
            $stmtR = $pdo->prepare("SELECT serial_number FROM device_serials WHERE serial_number = ? LIMIT 1");
            $stmtR->execute([$replacedBy]);
            if (!$stmtR->fetch()) {
                $pdo->rollBack();
                jsonErr('replaced_by_serial non presente in device_serials.');
            }
        } else {
            $replacedBy = null;
        }

        $prevStatus = (string)($row['status'] ?? '');
        $now = date('Y-m-d H:i:s');

        $newActivatedAt = $row['activated_at'] ?? null;
        $newDeactivatedAt = $row['deactivated_at'] ?? null;
        if ($targetStatus === 'active') {
            if (empty($newActivatedAt)) {
                $newActivatedAt = $now;
            }
            $newDeactivatedAt = null;
        } else {
            if (empty($newDeactivatedAt)) {
                $newDeactivatedAt = $now;
            }
        }

        $upd = $pdo->prepare("
            UPDATE device_serials
            SET status = ?,
                status_reason_code = ?,
                status_notes = CASE WHEN ? <> '' THEN ? ELSE status_notes END,
                replaced_by_serial = ?,
                status_changed_at = NOW(),
                status_changed_by_user_id = ?,
                activated_at = ?,
                deactivated_at = ?
            WHERE id = ?
        ");
        $upd->execute([
            $targetStatus,
            $reasonCode,
            $reasonDetails,
            $reasonDetails,
            $replacedBy,
            $userId,
            $newActivatedAt,
            $newDeactivatedAt,
            $row['id']
        ]);

        writeSerialAudit(
            $pdo,
            $userId,
            'SERIAL_STATUS_CHANGE',
            "Cambio stato seriale: {$prevStatus} -> {$targetStatus} | reason={$reasonCode}" .
            ($replacedBy ? " | replaced_by={$replacedBy}" : '') .
            ($reasonDetails !== '' ? " | note={$reasonDetails}" : ''),
            $serial,
            null
        );

        writeSerialLifecycleEvent(
            $pdo,
            $serial,
            $prevStatus !== '' ? $prevStatus : null,
            $targetStatus,
            $reasonCode,
            $reasonDetails !== '' ? $reasonDetails : null,
            $replacedBy,
            $userId,
            null
        );

        $pdo->commit();

        jsonOk([
            'message' => 'Stato seriale aggiornato con successo.',
            'serial_number' => $serial,
            'previous_status' => $prevStatus,
            'new_status' => $targetStatus,
            'reason_code' => $reasonCode,
            'replaced_by_serial' => $replacedBy
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        jsonErr('DB Error: ' . $e->getMessage());
    }
}

/* ------------------------------------------------------------
 * ACTION: assign_serial_to_master
 * Regole:
 * - seriale deve esistere in device_serials
 * - tipo prodotto deve essere 01 o 02 (master display o standalone/rewamping)
 * - seriale non assegnabile se agganciato ad altra master
 * - aggiorna anche masters.serial_number (compatibilita' portale attuale)
 * ------------------------------------------------------------ */
if ($action === 'assign_serial_to_master') {
    $serial = trim((string)($input['serial_number'] ?? ''));
    $masterId = (int)($input['master_id'] ?? 0);
    $notes = trim((string)($input['notes'] ?? ''));

    if ($serial === '') {
        jsonErr('serial_number mancante');
    }
    if ($masterId <= 0) {
        jsonErr('master_id non valido');
    }

    try {
        $pdo->beginTransaction();

        // 1) Lock riga master target
        $stmtMaster = $pdo->prepare("SELECT * FROM masters WHERE id = ? FOR UPDATE");
        $stmtMaster->execute([$masterId]);
        $master = $stmtMaster->fetch();
        if (!$master) {
            $pdo->rollBack();
            jsonErr('Master non trovato.');
        }
        if ($userRole !== 'admin' && !canUserAccessMaster($pdo, $masterId, $userId, $userRole)) {
            $pdo->rollBack();
            jsonErr('Master non accessibile per questa utenza.', 403);
        }

        // 2) Lock riga seriale
        $stmtSerial = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
        $stmtSerial->execute([$serial]);
        $serialRow = $stmtSerial->fetch();
        if (!$serialRow) {
            $pdo->rollBack();
            jsonErr('Seriale non trovato in device_serials.');
        }

        // 3) Verifica tipo prodotto coerente con master:
        // accettiamo sia 01 (Centralina Display) che 02 (Standalone/Rewamping).
        $allowedMasterTypes = ['01', '02'];
        if (!in_array((string)$serialRow['product_type_code'], $allowedMasterTypes, true)) {
            $pdo->rollBack();
            jsonErr('Il seriale selezionato non e\' di tipo master (consentiti: 01 o 02).');
        }

        // 4) Se seriale gia' assegnato ad altra master, blocca
        if (!empty($serialRow['assigned_master_id']) && (int)$serialRow['assigned_master_id'] !== $masterId) {
            $pdo->rollBack();
            jsonErr('Seriale gia\' assegnato a un\'altra master.');
        }

        // 5) Trova eventuale seriale precedente della master e scollegalo
        $oldSerial = (string)($master['serial_number'] ?? '');
        if ($oldSerial !== '' && $oldSerial !== $serial) {
            $stmtOld = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
            $stmtOld->execute([$oldSerial]);
            $oldRow = $stmtOld->fetch();
            if ($oldRow && (int)$oldRow['assigned_master_id'] === $masterId) {
                $updOld = $pdo->prepare("
                    UPDATE device_serials
                    SET assigned_master_id = NULL,
                        status = 'retired',
                        notes = CONCAT(COALESCE(notes,''), ' | Scollegato da master ', ?, ' il ', NOW())
                    WHERE id = ?
                ");
                $updOld->execute([$masterId, $oldRow['id']]);

                // Lifecycle columns opzionali (compatibilita': se non esistono, non blocchiamo).
                try {
                    if (
                        columnExists($pdo, 'device_serials', 'status_reason_code') &&
                        columnExists($pdo, 'device_serials', 'replaced_by_serial') &&
                        columnExists($pdo, 'device_serials', 'status_changed_at') &&
                        columnExists($pdo, 'device_serials', 'status_changed_by_user_id') &&
                        columnExists($pdo, 'device_serials', 'deactivated_at')
                    ) {
                        $updOldLifecycle = $pdo->prepare("
                            UPDATE device_serials
                            SET status_reason_code = 'master_replaced',
                                replaced_by_serial = ?,
                                status_changed_at = NOW(),
                                status_changed_by_user_id = ?,
                                deactivated_at = COALESCE(deactivated_at, NOW())
                            WHERE id = ?
                        ");
                        $updOldLifecycle->execute([$serial, $userId, $oldRow['id']]);
                    }
                } catch (Throwable $e) {
                    // no-op
                }

                writeSerialLifecycleEvent(
                    $pdo,
                    $oldSerial,
                    (string)($oldRow['status'] ?? 'active'),
                    'retired',
                    'master_replaced',
                    'Seriale sostituito durante assegnazione master',
                    $serial,
                    $userId,
                    $masterId
                );
            }
        }

        // 6) Assegna seriale nuovo al master in device_serials
        $updSerial = $pdo->prepare("
            UPDATE device_serials
            SET assigned_master_id = ?,
                assigned_user_id = ?,
                assigned_role = ?,
                status = 'active',
                serial_locked = 1,
                lock_source = 'admin',
                notes = CASE
                    WHEN ? <> '' THEN ?
                    ELSE notes
                END
            WHERE id = ?
        ");
        $updSerial->execute([
            $masterId,
            $userId,
            $userRole,
            $notes,
            $notes,
            $serialRow['id']
        ]);

        // Lifecycle columns opzionali (compatibilita': se non esistono, non blocchiamo).
        try {
            if (
                columnExists($pdo, 'device_serials', 'status_reason_code') &&
                columnExists($pdo, 'device_serials', 'replaced_by_serial') &&
                columnExists($pdo, 'device_serials', 'status_changed_at') &&
                columnExists($pdo, 'device_serials', 'status_changed_by_user_id') &&
                columnExists($pdo, 'device_serials', 'activated_at') &&
                columnExists($pdo, 'device_serials', 'deactivated_at')
            ) {
                $updSerialLifecycle = $pdo->prepare("
                    UPDATE device_serials
                    SET status_reason_code = 'master_bind',
                        replaced_by_serial = NULL,
                        status_changed_at = NOW(),
                        status_changed_by_user_id = ?,
                        activated_at = COALESCE(activated_at, NOW()),
                        deactivated_at = NULL
                    WHERE id = ?
                ");
                $updSerialLifecycle->execute([$userId, $serialRow['id']]);
            }
        } catch (Throwable $e) {
            // no-op
        }

        // 7) Aggiorna serial_number in masters (compatibilita' portale corrente)
        $updMaster = $pdo->prepare("UPDATE masters SET serial_number = ? WHERE id = ?");
        $updMaster->execute([$serial, $masterId]);

        // 8) Audit
        writeSerialAudit(
            $pdo,
            $userId,
            'SERIAL_ASSIGN_MASTER',
            'Assegnato seriale al master. old=' . $oldSerial . ' new=' . $serial . ($notes !== '' ? (' | ' . $notes) : ''),
            $serial,
            $masterId
        );

        writeSerialLifecycleEvent(
            $pdo,
            $serial,
            (string)($serialRow['status'] ?? 'reserved'),
            'active',
            'master_bind',
            $notes !== '' ? $notes : 'Attivato su master',
            null,
            $userId,
            $masterId
        );

        $pdo->commit();

        jsonOk([
            'message' => 'Seriale assegnato correttamente al master.',
            'master_id' => $masterId,
            'serial_number' => $serial
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        if (stripos($e->getMessage(), 'Duplicate entry') !== false) {
            jsonErr('Conflitto seriale: valore gia\' presente.');
        }
        jsonErr('DB Error: ' . $e->getMessage());
    }
}

/* ------------------------------------------------------------
 * Azione sconosciuta
 * ------------------------------------------------------------ */
jsonErr('Azione non valida');
