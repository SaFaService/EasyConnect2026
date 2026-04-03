<?php
session_start();
require 'config.php';
require_once 'auth_common.php';
header('Content-Type: application/json');

function outOk(array $payload = []): void {
    echo json_encode(array_merge(['status' => 'ok'], $payload));
    exit;
}

function outErr(string $message, int $http = 200): void {
    http_response_code($http);
    echo json_encode(['status' => 'error', 'message' => $message]);
    exit;
}

function tableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("\n        SELECT 1\n        FROM information_schema.tables\n        WHERE table_schema = DATABASE()\n          AND table_name = ?\n        LIMIT 1\n    ");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function columnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("\n        SELECT 1\n        FROM information_schema.columns\n        WHERE table_schema = DATABASE()\n          AND table_name = ?\n          AND column_name = ?\n        LIMIT 1\n    ");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

function fetchMasterForUser(PDO $pdo, int $masterId, int $userId, string $userRole, bool $forUpdate = false): ?array {
    if ($masterId <= 0) {
        return null;
    }

    $sql = "SELECT * FROM masters WHERE id = ?";
    $params = [$masterId];

    if ($userRole !== 'admin') {
        $hasBuilder = false;
        try {
            $hasBuilder = columnExists($pdo, 'masters', 'builder_id');
        } catch (Throwable $e) {
            $hasBuilder = false;
        }
        $sql .= " AND (creator_id = ? OR owner_id = ? OR maintainer_id = ?" . ($hasBuilder ? " OR builder_id = ?" : "") . ")";
        $params[] = $userId;
        $params[] = $userId;
        $params[] = $userId;
        if ($hasBuilder) {
            $params[] = $userId;
        }
    }

    if ($forUpdate) {
        $sql .= " FOR UPDATE";
    }

    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $row = $stmt->fetch();
    return $row ?: null;
}

function writeAudit(PDO $pdo, int $masterId, string $action, string $details): void {
    try {
        $stmt = $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, ?, ?)");
        $stmt->execute([$masterId, $action, $details]);
    } catch (Throwable $e) {
        // Audit non bloccante
    }
}

function retireSerialRecord(
    PDO $pdo,
    string $serial,
    string $reasonCode,
    string $reasonDetails,
    int $actorUserId,
    ?string $replacedBySerial = null,
    ?int $masterId = null
): array {
    if ($serial === '') {
        return ['ok' => false, 'reason' => 'empty_serial'];
    }
    if (!tableExists($pdo, 'device_serials')) {
        return ['ok' => false, 'reason' => 'device_serials_missing'];
    }

    $stmt = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
    $stmt->execute([$serial]);
    $row = $stmt->fetch();
    if (!$row) {
        return ['ok' => false, 'reason' => 'not_found'];
    }

    $set = ["status = 'retired'"];
    $params = [];

    if (columnExists($pdo, 'device_serials', 'status_reason_code')) {
        $set[] = 'status_reason_code = ?';
        $params[] = $reasonCode;
    }
    if (columnExists($pdo, 'device_serials', 'status_notes')) {
        $set[] = 'status_notes = ?';
        $params[] = $reasonDetails !== '' ? $reasonDetails : 'Retired from dashboard';
    }
    if (columnExists($pdo, 'device_serials', 'replaced_by_serial')) {
        $set[] = 'replaced_by_serial = ?';
        $params[] = $replacedBySerial;
    }
    if (columnExists($pdo, 'device_serials', 'assigned_master_id')) {
        $set[] = 'assigned_master_id = CASE WHEN assigned_master_id = ? THEN NULL ELSE assigned_master_id END';
        $params[] = $masterId;
    }
    if (columnExists($pdo, 'device_serials', 'status_changed_at')) {
        $set[] = 'status_changed_at = NOW()';
    }
    if (columnExists($pdo, 'device_serials', 'status_changed_by_user_id')) {
        $set[] = 'status_changed_by_user_id = ?';
        $params[] = $actorUserId;
    }
    if (columnExists($pdo, 'device_serials', 'deactivated_at')) {
        $set[] = 'deactivated_at = COALESCE(deactivated_at, NOW())';
    }

    $params[] = $row['id'];
    $upd = $pdo->prepare('UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?');
    $upd->execute($params);

    if (tableExists($pdo, 'serial_lifecycle_events')) {
        try {
            $event = $pdo->prepare("\n                INSERT INTO serial_lifecycle_events (\n                    serial_number,\n                    from_status,\n                    to_status,\n                    reason_code,\n                    reason_details,\n                    replaced_by_serial,\n                    actor_user_id,\n                    master_id\n                ) VALUES (?, ?, 'retired', ?, ?, ?, ?, ?)\n            ");
            $event->execute([
                $serial,
                $row['status'] ?? null,
                $reasonCode,
                $reasonDetails !== '' ? $reasonDetails : null,
                $replacedBySerial,
                $actorUserId,
                $masterId,
            ]);
        } catch (Throwable $e) {
            // Non bloccare il flusso.
        }
    }

    return ['ok' => true, 'previous_status' => $row['status'] ?? null];
}

function activateSerialOnMaster(PDO $pdo, string $serial, int $masterId, int $userId, string $userRole): void {
    if (!tableExists($pdo, 'device_serials') || $serial === '') {
        return;
    }

    $stmt = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
    $stmt->execute([$serial]);
    $row = $stmt->fetch();

    if (!$row) {
        $productType = '02';
        if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial, $m)) {
            $productType = $m[1];
        }
        $serialScheme = preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial) ? 'v2_yyyymmttnnnn' : 'legacy';

        $ins = $pdo->prepare("\n            INSERT INTO device_serials (\n                serial_number, serial_scheme, product_type_code,\n                serial_locked, lock_source, assigned_user_id, assigned_role,\n                assigned_master_id, status\n            ) VALUES (?, ?, ?, 1, 'api_command', ?, ?, ?, 'active')\n        ");
        $ins->execute([$serial, $serialScheme, $productType, $userId, $userRole, $masterId]);
        return;
    }

    $set = [
        "assigned_master_id = ?",
        "assigned_user_id = ?",
        "assigned_role = ?",
        "status = 'active'",
    ];
    $params = [$masterId, $userId, $userRole];

    if (columnExists($pdo, 'device_serials', 'serial_locked')) {
        $set[] = 'serial_locked = 1';
    }
    if (columnExists($pdo, 'device_serials', 'lock_source')) {
        $set[] = "lock_source = 'admin'";
    }
    if (columnExists($pdo, 'device_serials', 'status_reason_code')) {
        $set[] = "status_reason_code = 'master_bind'";
    }
    if (columnExists($pdo, 'device_serials', 'replaced_by_serial')) {
        $set[] = 'replaced_by_serial = NULL';
    }
    if (columnExists($pdo, 'device_serials', 'status_changed_at')) {
        $set[] = 'status_changed_at = NOW()';
    }
    if (columnExists($pdo, 'device_serials', 'status_changed_by_user_id')) {
        $set[] = 'status_changed_by_user_id = ?';
        $params[] = $userId;
    }
    if (columnExists($pdo, 'device_serials', 'activated_at')) {
        $set[] = 'activated_at = COALESCE(activated_at, NOW())';
    }
    if (columnExists($pdo, 'device_serials', 'deactivated_at')) {
        $set[] = 'deactivated_at = NULL';
    }

    $params[] = $row['id'];
    $upd = $pdo->prepare('UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?');
    $upd->execute($params);
}

if (!isset($_SESSION['user_id'], $_SESSION['user_role'])) {
    outErr('Non autorizzato', 401);
}

$userId = (int)$_SESSION['user_id'];
$userRole = (string)$_SESSION['user_role'];
$canFirmwareUpdate = ecAuthCurrentUserCan($pdo, $userId, 'firmware_update');
$canSerialLifecycle = ecAuthCurrentUserCan($pdo, $userId, 'serial_lifecycle');
$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    $input = [];
}
$action = (string)($input['action'] ?? '');

if ($action === '') {
    outErr('Azione non valida');
}

if ($action === 'request_update') {
    if (!$canFirmwareUpdate) {
        outErr('Aggiornamenti firmware non abilitati per questa utenza.', 403);
    }

    $mid = (int)($input['master_id'] ?? 0);
    $master = fetchMasterForUser($pdo, $mid, $userId, $userRole, false);
    if (!$master) {
        outErr('Master non trovato o permessi insufficienti', 403);
    }

    $resetStmt = $pdo->prepare("\n        UPDATE masters SET\n            update_requested = 0,\n            ota_status = NULL,\n            ota_message = NULL,\n            slave_update_request_sn = NULL,\n            slave_ota_status = NULL,\n            slave_ota_message = NULL\n        WHERE id = ?\n    ");
    $resetStmt->execute([$mid]);

    $stmtUpd = $pdo->prepare("UPDATE masters SET update_requested = 1, ota_status = 'Pending' WHERE id = ?");
    if ($stmtUpd->execute([$mid])) {
        writeAudit($pdo, $mid, 'OTA_REQ_MASTER', "Richiesto aggiornamento firmware Master. Utente ID: {$userId}");
        outOk(['message' => 'Richiesta inviata']);
    }
    outErr('Errore DB');
}

if ($action === 'request_slave_update') {
    if (!$canFirmwareUpdate) {
        outErr('Aggiornamenti firmware non abilitati per questa utenza.', 403);
    }

    $mid = (int)($input['master_id'] ?? 0);
    $slaveSn = trim((string)($input['slave_sn'] ?? ''));
    if ($slaveSn === '') {
        outErr('Seriale slave mancante');
    }

    $master = fetchMasterForUser($pdo, $mid, $userId, $userRole, false);
    if (!$master) {
        outErr('Master non trovato o permessi insufficienti', 403);
    }

    $resetStmt = $pdo->prepare("\n        UPDATE masters SET\n            update_requested = 0,\n            ota_status = NULL,\n            ota_message = NULL,\n            slave_update_request_sn = NULL,\n            slave_ota_status = NULL,\n            slave_ota_message = NULL\n        WHERE id = ?\n    ");
    $resetStmt->execute([$mid]);

    $stmtUpd = $pdo->prepare("UPDATE masters SET slave_update_request_sn = ?, slave_ota_status = 'Pending' WHERE id = ?");
    if ($stmtUpd->execute([$slaveSn, $mid])) {
        writeAudit($pdo, $mid, 'OTA_REQ_SLAVE', "Richiesto aggiornamento firmware Slave {$slaveSn}. Utente ID: {$userId}");
        outOk(['message' => 'Richiesta per slave inviata']);
    }
    outErr('Errore DB');
}

if ($action === 'update_serial') {
    if (!$canSerialLifecycle) {
        outErr('Gestione seriali non abilitata per questa utenza.', 403);
    }

    $mid = (int)($input['master_id'] ?? 0);
    $newSerial = trim((string)($input['new_serial'] ?? ''));
    $oldSerialInput = trim((string)($input['old_serial'] ?? ''));
    $reasonCode = trim((string)($input['reason_code'] ?? 'master_replaced'));
    $reasonDetails = trim((string)($input['reason_details'] ?? 'Sostituzione master da dashboard'));

    if ($newSerial === '') {
        outErr('Nuovo seriale mancante');
    }

    try {
        $pdo->beginTransaction();

        $master = fetchMasterForUser($pdo, $mid, $userId, $userRole, true);
        if (!$master) {
            $pdo->rollBack();
            outErr('Master non trovato o permessi insufficienti', 403);
        }

        $oldSerial = $oldSerialInput !== '' ? $oldSerialInput : trim((string)($master['serial_number'] ?? ''));

        $updMaster = $pdo->prepare('UPDATE masters SET serial_number = ? WHERE id = ?');
        $updMaster->execute([$newSerial, $mid]);

        if ($oldSerial !== '' && $oldSerial !== $newSerial) {
            retireSerialRecord($pdo, $oldSerial, $reasonCode, $reasonDetails, $userId, $newSerial, $mid);
        }

        activateSerialOnMaster($pdo, $newSerial, $mid, $userId, $userRole);

        writeAudit(
            $pdo,
            $mid,
            'CHANGE_SERIAL',
            "Sostituzione Master. Vecchio SN: {$oldSerial} -> Nuovo SN: {$newSerial}. Utente ID: {$userId}"
        );

        $pdo->commit();
        outOk(['message' => 'Seriale aggiornato', 'old_serial' => $oldSerial, 'new_serial' => $newSerial]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        outErr('Errore DB: ' . $e->getMessage());
    }
}

if ($action === 'retire_slave_serial') {
    if (!$canSerialLifecycle) {
        outErr('Gestione seriali non abilitata per questa utenza.', 403);
    }

    $mid = (int)($input['master_id'] ?? 0);
    $slaveSn = trim((string)($input['slave_sn'] ?? ''));
    $reasonCode = trim((string)($input['reason_code'] ?? 'field_replaced'));
    $reasonDetails = trim((string)($input['reason_details'] ?? 'Dismissione periferica da dashboard'));
    $replacedBy = trim((string)($input['replaced_by_serial'] ?? ''));

    if ($slaveSn === '') {
        outErr('Seriale slave mancante');
    }

    try {
        $pdo->beginTransaction();
        $master = fetchMasterForUser($pdo, $mid, $userId, $userRole, true);
        if (!$master) {
            $pdo->rollBack();
            outErr('Master non trovato o permessi insufficienti', 403);
        }

        $check = $pdo->prepare("\n            SELECT 1\n            FROM measurements\n            WHERE master_id = ?\n              AND slave_sn = ?\n            LIMIT 1\n        ");
        $check->execute([$mid, $slaveSn]);
        if (!$check->fetchColumn()) {
            $pdo->rollBack();
            outErr('Periferica non associata a questo impianto');
        }

        $ret = retireSerialRecord(
            $pdo,
            $slaveSn,
            $reasonCode,
            $reasonDetails,
            $userId,
            $replacedBy !== '' ? $replacedBy : null,
            $mid
        );
        if (!($ret['ok'] ?? false)) {
            $pdo->rollBack();
            outErr('Impossibile dismettere la periferica: ' . ($ret['reason'] ?? 'errore')); 
        }

        writeAudit($pdo, $mid, 'RETIRE_SLAVE', "Dismessa periferica {$slaveSn} | reason={$reasonCode} | user={$userId}");
        $pdo->commit();
        outOk(['message' => 'Periferica dismessa', 'serial_number' => $slaveSn]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        outErr('Errore DB: ' . $e->getMessage());
    }
}

if ($action === 'retire_master_serial') {
    if (!$canSerialLifecycle) {
        outErr('Gestione seriali non abilitata per questa utenza.', 403);
    }

    $mid = (int)($input['master_id'] ?? 0);
    $serial = trim((string)($input['serial_number'] ?? ''));
    $reasonCode = trim((string)($input['reason_code'] ?? 'damaged'));
    $reasonDetails = trim((string)($input['reason_details'] ?? 'Dismissione master da dashboard'));
    $replacedBy = trim((string)($input['replaced_by_serial'] ?? ''));

    try {
        $pdo->beginTransaction();
        $master = fetchMasterForUser($pdo, $mid, $userId, $userRole, true);
        if (!$master) {
            $pdo->rollBack();
            outErr('Master non trovato o permessi insufficienti', 403);
        }

        if ($serial === '') {
            $serial = trim((string)($master['serial_number'] ?? ''));
        }
        if ($serial === '') {
            $pdo->rollBack();
            outErr('Seriale master non disponibile');
        }

        $ret = retireSerialRecord($pdo, $serial, $reasonCode, $reasonDetails, $userId, $replacedBy !== '' ? $replacedBy : null, $mid);
        if (!($ret['ok'] ?? false)) {
            $pdo->rollBack();
            outErr('Impossibile dismettere la master: ' . ($ret['reason'] ?? 'errore'));
        }

        writeAudit($pdo, $mid, 'RETIRE_MASTER', "Dismessa master {$serial} | reason={$reasonCode} | user={$userId}");
        $pdo->commit();
        outOk(['message' => 'Master dismessa', 'serial_number' => $serial]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        outErr('Errore DB: ' . $e->getMessage());
    }
}

if ($action === 'queue_pressure_config') {
    if (!in_array($userRole, ['admin', 'builder', 'maintainer'], true)) {
        outErr('Permessi insufficienti', 403);
    }

    $mid = (int)($input['master_id'] ?? 0);
    $slaveSn = trim((string)($input['serial_number'] ?? ''));
    $newModeRaw = trim((string)($input['new_mode'] ?? ''));
    $newGroupRaw = trim((string)($input['new_group'] ?? ''));
    $newIpRaw = trim((string)($input['new_ip'] ?? ''));

    $newMode = ($newModeRaw !== '') ? (int)$newModeRaw : null;
    $newGroup = ($newGroupRaw !== '') ? (int)$newGroupRaw : null;
    $newIp = ($newIpRaw !== '') ? (int)$newIpRaw : null;

    if ($slaveSn === '' || $mid <= 0) {
        outErr('Parametri mancanti: master_id o serial_number');
    }
    if ($newMode === null && $newGroup === null && $newIp === null) {
        outErr('Nessuna modifica richiesta');
    }
    if ($newMode !== null && !in_array($newMode, [1, 2, 3], true)) {
        outErr('Modalita non valida (consentite 1,2,3)');
    }
    if ($newGroup !== null && ($newGroup < 1 || $newGroup > 100)) {
        outErr('Gruppo non valido (1..100)');
    }
    if ($newIp !== null && ($newIp < 1 || $newIp > 30)) {
        outErr('IP RS485 non valido (1..30)');
    }
    if (!tableExists($pdo, 'device_commands')) {
        outErr('Tabella device_commands non disponibile: applicare migrazione SQL.');
    }
    if (!tableExists($pdo, 'measurements')) {
        outErr('Tabella measurements non disponibile.');
    }

    try {
        $pdo->beginTransaction();

        $master = fetchMasterForUser($pdo, $mid, $userId, $userRole, true);
        if (!$master) {
            $pdo->rollBack();
            outErr('Master non trovato o permessi insufficienti', 403);
        }

        if (!tableExists($pdo, 'device_serials')) {
            $pdo->rollBack();
            outErr('Tabella device_serials non disponibile.');
        }

        $stmtSerial = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
        $stmtSerial->execute([$slaveSn]);
        $serialRow = $stmtSerial->fetch();
        if (!$serialRow) {
            $pdo->rollBack();
            outErr('Seriale non trovato in archivio.');
        }
        if ((string)($serialRow['product_type_code'] ?? '') !== '04') {
            $pdo->rollBack();
            outErr('Il seriale selezionato non e una scheda pressione.');
        }
        $serialStatus = strtolower(trim((string)($serialRow['status'] ?? '')));
        if (in_array($serialStatus, ['retired', 'voided'], true)) {
            $pdo->rollBack();
            outErr('Seriale non configurabile: stato ' . $serialStatus);
        }

        $belongs = false;
        if (!empty($serialRow['assigned_master_id']) && (int)$serialRow['assigned_master_id'] === $mid) {
            $belongs = true;
        } else {
            $stmtBelongs = $pdo->prepare("SELECT 1 FROM measurements WHERE master_id = ? AND slave_sn = ? LIMIT 1");
            $stmtBelongs->execute([$mid, $slaveSn]);
            $belongs = (bool)$stmtBelongs->fetchColumn();
        }
        if (!$belongs) {
            $pdo->rollBack();
            outErr('La periferica non risulta associata a questa master.');
        }

        $stmtOnline = $pdo->prepare("SELECT MAX(recorded_at) FROM measurements WHERE master_id = ? AND slave_sn = ?");
        $stmtOnline->execute([$mid, $slaveSn]);
        $lastSeen = (string)($stmtOnline->fetchColumn() ?: '');
        if ($lastSeen === '' || strtotime($lastSeen) < (time() - 180)) {
            $pdo->rollBack();
            outErr('Periferica offline: impossibile inviare configurazione remota.');
        }

        $masterType = '';
        $masterSn = trim((string)($master['serial_number'] ?? ''));
        if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $masterSn, $mType)) {
            $masterType = (string)$mType[1];
        }
        if ($newMode !== null && $masterType === '02' && $newMode === 1) {
            $pdo->rollBack();
            outErr('Master in modalita rewamping: la modalita 1 non e consentita (solo 2 o 3).');
        }

        if ($newGroup !== null) {
            $sqlGrpConflict = "
                SELECT m1.slave_sn
                FROM measurements m1
                INNER JOIN (
                    SELECT slave_sn, MAX(id) AS max_id
                    FROM measurements
                    WHERE master_id = ?
                      AND slave_sn IS NOT NULL
                    GROUP BY slave_sn
                ) m2 ON m2.max_id = m1.id
                LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn
                WHERE m1.master_id = ?
                  AND m1.slave_sn <> ?
                  AND m1.slave_grp = ?
                  AND (ds.status IS NULL OR ds.status NOT IN ('retired','voided'))
                LIMIT 1
            ";
            $stmtGrpConflict = $pdo->prepare($sqlGrpConflict);
            $stmtGrpConflict->execute([$mid, $mid, $slaveSn, $newGroup]);
            $conflictSn = (string)($stmtGrpConflict->fetchColumn() ?: '');
            if ($conflictSn !== '') {
                $pdo->rollBack();
                outErr('Cambio gruppo non consentito: gruppo gia usato da seriale ' . $conflictSn);
            }
        }

        if ($newIp !== null) {
            if (!columnExists($pdo, 'measurements', 'slave_id')) {
                $pdo->rollBack();
                outErr('Campo measurements.slave_id non disponibile: applicare migrazione SQL.');
            }

            $sqlIpConflict = "
                SELECT m1.slave_sn
                FROM measurements m1
                INNER JOIN (
                    SELECT slave_sn, MAX(id) AS max_id
                    FROM measurements
                    WHERE master_id = ?
                      AND slave_sn IS NOT NULL
                    GROUP BY slave_sn
                ) m2 ON m2.max_id = m1.id
                LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn
                WHERE m1.master_id = ?
                  AND m1.slave_sn <> ?
                  AND m1.slave_id = ?
                  AND (ds.status IS NULL OR ds.status NOT IN ('retired','voided'))
                LIMIT 1
            ";
            $stmtIpConflict = $pdo->prepare($sqlIpConflict);
            $stmtIpConflict->execute([$mid, $mid, $slaveSn, $newIp]);
            $conflictSn = (string)($stmtIpConflict->fetchColumn() ?: '');
            if ($conflictSn !== '') {
                $pdo->rollBack();
                outErr('Cambio IP non consentito: IP gia usato da seriale ' . $conflictSn);
            }
        }

        $stmtPending = $pdo->prepare("
            SELECT id
            FROM device_commands
            WHERE master_id = ?
              AND target_serial = ?
              AND status IN ('pending','sent')
            ORDER BY id DESC
            LIMIT 1
        ");
        $stmtPending->execute([$mid, $slaveSn]);
        $pendingId = (int)($stmtPending->fetchColumn() ?: 0);
        if ($pendingId > 0) {
            $pdo->rollBack();
            outErr('Esiste gia un comando pendente per questa scheda (ID ' . $pendingId . ').');
        }

        $payload = ['slave_sn' => $slaveSn];
        if ($newMode !== null) $payload['new_mode'] = $newMode;
        if ($newGroup !== null) $payload['new_group'] = $newGroup;
        if ($newIp !== null) $payload['new_ip'] = $newIp;

        $ins = $pdo->prepare("
            INSERT INTO device_commands (
                master_id, target_serial, command_type, payload_json,
                status, created_by_user_id, created_at
            ) VALUES (?, ?, 'pressure_config', ?, 'pending', ?, NOW())
        ");
        $ins->execute([$mid, $slaveSn, json_encode($payload, JSON_UNESCAPED_UNICODE), $userId]);
        $commandId = (int)$pdo->lastInsertId();

        writeAudit(
            $pdo,
            $mid,
            'PRESSURE_CFG_QUEUE',
            "Queue command #{$commandId} su {$slaveSn}: " . json_encode($payload, JSON_UNESCAPED_UNICODE)
        );

        $pdo->commit();
        outOk([
            'message' => 'Configurazione accodata. La master la eseguira al prossimo ciclo di controllo.',
            'command_id' => $commandId
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        outErr('Errore DB: ' . $e->getMessage());
    }
}

outErr('Azione non valida');
?>
