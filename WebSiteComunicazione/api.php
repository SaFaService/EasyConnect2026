<?php
// api.php - Endpoint per ESP32
header('Content-Type: application/json');
require 'config.php';

function tableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = ? LIMIT 1");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function columnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ? AND column_name = ? LIMIT 1");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

function normalizeTrafficCounter($value): ?int {
    if ($value === null || $value === '') {
        return null;
    }
    if (!is_numeric($value)) {
        return null;
    }
    $n = (int)$value;
    if ($n < 0) {
        $n = 0;
    }
    return $n;
}

function normalizeBinaryFlag($value): ?int {
    if ($value === null || $value === '') {
        return null;
    }
    if (is_bool($value)) {
        return $value ? 1 : 0;
    }
    if (is_numeric($value)) {
        return ((int)$value) !== 0 ? 1 : 0;
    }
    $v = strtolower(trim((string)$value));
    if (in_array($v, ['1', 'true', 'on', 'yes', 'si'], true)) {
        return 1;
    }
    if (in_array($v, ['0', 'false', 'off', 'no'], true)) {
        return 0;
    }
    return null;
}

function normalizeFloatValue($value): ?float {
    if ($value === null || $value === '') {
        return null;
    }
    if (!is_numeric($value)) {
        return null;
    }
    return (float)$value;
}

function compareFwVersion(string $a, string $b): int {
    $cleanA = preg_replace('/[^0-9.]/', '', $a);
    $cleanB = preg_replace('/[^0-9.]/', '', $b);
    if ($cleanA === '' && $cleanB === '') return 0;
    if ($cleanA === '') return -1;
    if ($cleanB === '') return 1;

    $pa = array_map('intval', explode('.', $cleanA));
    $pb = array_map('intval', explode('.', $cleanB));
    $len = max(count($pa), count($pb));
    for ($i = 0; $i < $len; $i++) {
        $va = $pa[$i] ?? 0;
        $vb = $pb[$i] ?? 0;
        if ($va < $vb) return -1;
        if ($va > $vb) return 1;
    }
    return 0;
}

function serialSchemeFor(string $serial): string {
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial)) {
        return 'v2_yyyymmttnnnn';
    }
    return 'legacy';
}

function serialTypeFor(string $serial, string $fallback = '04'): string {
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial, $m)) {
        return (string)$m[1];
    }
    return $fallback;
}

function resolveAssignedUser(PDO $pdo, array $master): array {
    $uid = null;
    if (!empty($master['owner_id'])) {
        $uid = (int)$master['owner_id'];
    } elseif (isset($master['builder_id']) && !empty($master['builder_id'])) {
        $uid = (int)$master['builder_id'];
    } elseif (!empty($master['maintainer_id'])) {
        $uid = (int)$master['maintainer_id'];
    } elseif (!empty($master['creator_id'])) {
        $uid = (int)$master['creator_id'];
    }

    if (!$uid) {
        return [null, 'system'];
    }

    try {
        $stmt = $pdo->prepare("SELECT role FROM users WHERE id = ? LIMIT 1");
        $stmt->execute([$uid]);
        $role = (string)($stmt->fetchColumn() ?: 'system');
        return [$uid, $role];
    } catch (Throwable $e) {
        return [$uid, 'system'];
    }
}

function activateSerialFromTelemetry(PDO $pdo, array $master, string $serial, string $fallbackType): void {
    $serial = trim($serial);
    if ($serial === '') {
        return;
    }
    static $deviceSerialsExists = null;
    static $colsCache = null;

    if ($deviceSerialsExists === null) {
        $deviceSerialsExists = tableExists($pdo, 'device_serials');
    }
    if (!$deviceSerialsExists) {
        return;
    }

    [$assignedUserId, $assignedRole] = resolveAssignedUser($pdo, $master);
    if ($colsCache === null) {
        $colsCache = [
            'serial_scheme' => columnExists($pdo, 'device_serials', 'serial_scheme'),
            'product_type_code' => columnExists($pdo, 'device_serials', 'product_type_code'),
            'serial_locked' => columnExists($pdo, 'device_serials', 'serial_locked'),
            'lock_source' => columnExists($pdo, 'device_serials', 'lock_source'),
            'assigned_user_id' => columnExists($pdo, 'device_serials', 'assigned_user_id'),
            'assigned_role' => columnExists($pdo, 'device_serials', 'assigned_role'),
            'assigned_master_id' => columnExists($pdo, 'device_serials', 'assigned_master_id'),
            'status' => columnExists($pdo, 'device_serials', 'status'),
            'status_reason_code' => columnExists($pdo, 'device_serials', 'status_reason_code'),
            'status_changed_at' => columnExists($pdo, 'device_serials', 'status_changed_at'),
            'status_changed_by_user_id' => columnExists($pdo, 'device_serials', 'status_changed_by_user_id'),
            'activated_at' => columnExists($pdo, 'device_serials', 'activated_at'),
            'deactivated_at' => columnExists($pdo, 'device_serials', 'deactivated_at'),
            'replaced_by_serial' => columnExists($pdo, 'device_serials', 'replaced_by_serial'),
            'owner_user_id' => columnExists($pdo, 'device_serials', 'owner_user_id'),
        ];
    }
    $cols = $colsCache;

    $productType = serialTypeFor($serial, $fallbackType);

    $pdo->beginTransaction();
    try {
        $stmt = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
        $stmt->execute([$serial]);
        $row = $stmt->fetch();

        if (!$row) {
            $insertCols = ['serial_number'];
            $insertVals = ['?'];
            $paramsIns = [$serial];

            if ($cols['serial_scheme']) {
                $insertCols[] = 'serial_scheme';
                $insertVals[] = '?';
                $paramsIns[] = serialSchemeFor($serial);
            }
            if ($cols['product_type_code']) {
                $insertCols[] = 'product_type_code';
                $insertVals[] = '?';
                $paramsIns[] = $productType;
            }
            if ($cols['serial_locked']) {
                $insertCols[] = 'serial_locked';
                $insertVals[] = '?';
                $paramsIns[] = 1;
            }
            if ($cols['lock_source']) {
                $insertCols[] = 'lock_source';
                $insertVals[] = '?';
                $paramsIns[] = 'api_data';
            }
            if ($cols['assigned_user_id']) {
                $insertCols[] = 'assigned_user_id';
                $insertVals[] = '?';
                $paramsIns[] = $assignedUserId;
            }
            if ($cols['owner_user_id']) {
                $insertCols[] = 'owner_user_id';
                $insertVals[] = '?';
                $paramsIns[] = $assignedUserId;
            }
            if ($cols['assigned_role']) {
                $insertCols[] = 'assigned_role';
                $insertVals[] = '?';
                $paramsIns[] = $assignedRole;
            }
            if ($cols['assigned_master_id']) {
                $insertCols[] = 'assigned_master_id';
                $insertVals[] = '?';
                $paramsIns[] = (int)$master['id'];
            }
            if ($cols['status']) {
                $insertCols[] = 'status';
                $insertVals[] = '?';
                $paramsIns[] = 'active';
            }
            if ($cols['status_reason_code']) {
                $insertCols[] = 'status_reason_code';
                $insertVals[] = '?';
                $paramsIns[] = 'master_bind';
            }
            if ($cols['activated_at']) {
                $insertCols[] = 'activated_at';
                $insertVals[] = 'NOW()';
            }

            $sqlIns = 'INSERT INTO device_serials (' . implode(', ', $insertCols) . ') VALUES (' . implode(', ', $insertVals) . ')';
            $ins = $pdo->prepare($sqlIns);
            $ins->execute($paramsIns);
        } else {
            $currentStatus = strtolower(trim((string)($row['status'] ?? '')));
            if (($currentStatus === 'retired' || $currentStatus === 'voided') && $fallbackType !== '02') {
                // Per periferiche: non riattivare automaticamente seriali dismessi/annullati.
                $pdo->commit();
                return;
            }

            $set = [];
            $paramsUpd = [];

            if ($cols['product_type_code']) {
                $set[] = 'product_type_code = ?';
                $paramsUpd[] = $productType;
            }
            if ($cols['serial_locked']) {
                $set[] = 'serial_locked = 1';
            }
            if ($cols['lock_source']) {
                $set[] = "lock_source = 'api_data'";
            }
            if ($cols['assigned_user_id']) {
                $set[] = 'assigned_user_id = ?';
                $paramsUpd[] = $assignedUserId;
            }
            if ($cols['owner_user_id']) {
                $set[] = 'owner_user_id = ?';
                $paramsUpd[] = $assignedUserId;
            }
            if ($cols['assigned_role']) {
                $set[] = 'assigned_role = ?';
                $paramsUpd[] = $assignedRole;
            }
            if ($cols['assigned_master_id']) {
                $set[] = 'assigned_master_id = ?';
                $paramsUpd[] = (int)$master['id'];
            }
            if ($cols['status']) {
                $set[] = "status = 'active'";
            }
            if ($cols['status_reason_code']) {
                $set[] = "status_reason_code = 'master_bind'";
            }
            if ($cols['status_changed_at']) {
                $set[] = 'status_changed_at = NOW()';
            }
            if ($cols['status_changed_by_user_id']) {
                $set[] = 'status_changed_by_user_id = ?';
                $paramsUpd[] = $assignedUserId;
            }
            if ($cols['activated_at']) {
                $set[] = 'activated_at = COALESCE(activated_at, NOW())';
            }
            if ($cols['deactivated_at']) {
                $set[] = 'deactivated_at = NULL';
            }
            if ($cols['replaced_by_serial']) {
                $set[] = 'replaced_by_serial = NULL';
            }

            if (!empty($set)) {
                $paramsUpd[] = $row['id'];
                $sqlUpd = 'UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?';
                $upd = $pdo->prepare($sqlUpd);
                $upd->execute($paramsUpd);
            }
        }

        $pdo->commit();
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        // Non blocchiamo la ricezione dati metrologici per errore serial-centric.
    }
}

// 1. Verifica API Key
$headers = getallheaders();
$client_key = $headers['X-API-KEY'] ?? $_SERVER['HTTP_X_API_KEY'] ?? '';
$client_key = trim((string)$client_key);

if (empty($client_key)) {
    http_response_code(403);
    echo json_encode(['error' => 'Missing API Key']);
    exit;
}
if (!preg_match('/^[a-f0-9]{64}$/i', trim((string)$client_key))) {
    http_response_code(403);
    echo json_encode(['error' => 'Invalid API Key']);
    exit;
}

// Cerca la master con questa chiave
$stmt = $pdo->prepare("SELECT * FROM masters WHERE api_key = ?");
$stmt->execute([$client_key]);
$master = $stmt->fetch();

if (!$master) {
    http_response_code(403);
    echo json_encode(['error' => 'Invalid API Key']);
    exit;
}

// 2. Leggi i dati JSON
$json = file_get_contents('php://input');
$data = json_decode($json, true);

if (!$data) {
    echo json_encode(['status' => 'no_data']);
    exit;
}

$measureHasMasterSn = false;
$measureHasApiTxSession = false;
$measureHasApiRxSession = false;
$measureHasApiPostsSession = false;
$measureHasApiTxCycle = false;
$measureHasApiRxCycle = false;
$measureHasResUptime = false;
$measureHasResCpuMhz = false;
$measureHasResHeapFree = false;
$measureHasResHeapMin = false;
$measureHasResHeapTotal = false;
$measureHasResSketchUsed = false;
$measureHasResSketchFree = false;
$measureHasSlaveId = false;
$measureHasDeviceType = false;
$measureHasRelayMode = false;
$measureHasRelayOnline = false;
$measureHasRelayOn = false;
$measureHasRelaySafetyClosed = false;
$measureHasRelayFeedbackOk = false;
$measureHasRelayFeedbackFault = false;
$measureHasRelaySafetyAlarm = false;
$measureHasRelayLifetimeAlarm = false;
$measureHasRelayLampFault = false;
$measureHasRelayLifeLimitHours = false;
$measureHasRelayHoursOn = false;
$measureHasRelayHoursRemaining = false;
$measureHasRelayStarts = false;
$measureHasRelayState = false;
$mastersHasPlantKind = false;
try {
    $measureHasMasterSn = columnExists($pdo, 'measurements', 'master_sn');
    $measureHasApiTxSession = columnExists($pdo, 'measurements', 'api_tx_session_bytes');
    $measureHasApiRxSession = columnExists($pdo, 'measurements', 'api_rx_session_bytes');
    $measureHasApiPostsSession = columnExists($pdo, 'measurements', 'api_posts_session_count');
    $measureHasApiTxCycle = columnExists($pdo, 'measurements', 'api_tx_cycle_bytes');
    $measureHasApiRxCycle = columnExists($pdo, 'measurements', 'api_rx_cycle_bytes');
    $measureHasResUptime = columnExists($pdo, 'measurements', 'uptime_seconds');
    $measureHasResCpuMhz = columnExists($pdo, 'measurements', 'cpu_mhz');
    $measureHasResHeapFree = columnExists($pdo, 'measurements', 'heap_free_bytes');
    $measureHasResHeapMin = columnExists($pdo, 'measurements', 'heap_min_bytes');
    $measureHasResHeapTotal = columnExists($pdo, 'measurements', 'heap_total_bytes');
    $measureHasResSketchUsed = columnExists($pdo, 'measurements', 'sketch_used_bytes');
    $measureHasResSketchFree = columnExists($pdo, 'measurements', 'sketch_free_bytes');
    $measureHasSlaveId = columnExists($pdo, 'measurements', 'slave_id');
    $measureHasDeviceType = columnExists($pdo, 'measurements', 'device_type');
    $measureHasRelayMode = columnExists($pdo, 'measurements', 'relay_mode');
    $measureHasRelayOnline = columnExists($pdo, 'measurements', 'relay_online');
    $measureHasRelayOn = columnExists($pdo, 'measurements', 'relay_on');
    $measureHasRelaySafetyClosed = columnExists($pdo, 'measurements', 'relay_safety_closed');
    $measureHasRelayFeedbackOk = columnExists($pdo, 'measurements', 'relay_feedback_ok');
    $measureHasRelayFeedbackFault = columnExists($pdo, 'measurements', 'relay_feedback_fault');
    $measureHasRelaySafetyAlarm = columnExists($pdo, 'measurements', 'relay_safety_alarm');
    $measureHasRelayLifetimeAlarm = columnExists($pdo, 'measurements', 'relay_lifetime_alarm');
    $measureHasRelayLampFault = columnExists($pdo, 'measurements', 'relay_lamp_fault');
    $measureHasRelayLifeLimitHours = columnExists($pdo, 'measurements', 'relay_life_limit_hours');
    $measureHasRelayHoursOn = columnExists($pdo, 'measurements', 'relay_hours_on');
    $measureHasRelayHoursRemaining = columnExists($pdo, 'measurements', 'relay_hours_remaining');
    $measureHasRelayStarts = columnExists($pdo, 'measurements', 'relay_starts');
    $measureHasRelayState = columnExists($pdo, 'measurements', 'relay_state');
    $mastersHasPlantKind = columnExists($pdo, 'masters', 'plant_kind');
} catch (Throwable $e) {
    $measureHasMasterSn = false;
    $measureHasApiTxSession = false;
    $measureHasApiRxSession = false;
    $measureHasApiPostsSession = false;
    $measureHasApiTxCycle = false;
    $measureHasApiRxCycle = false;
    $measureHasResUptime = false;
    $measureHasResCpuMhz = false;
    $measureHasResHeapFree = false;
    $measureHasResHeapMin = false;
    $measureHasResHeapTotal = false;
    $measureHasResSketchUsed = false;
    $measureHasResSketchFree = false;
    $measureHasSlaveId = false;
    $measureHasDeviceType = false;
    $measureHasRelayMode = false;
    $measureHasRelayOnline = false;
    $measureHasRelayOn = false;
    $measureHasRelaySafetyClosed = false;
    $measureHasRelayFeedbackOk = false;
    $measureHasRelayFeedbackFault = false;
    $measureHasRelaySafetyAlarm = false;
    $measureHasRelayLifetimeAlarm = false;
    $measureHasRelayLampFault = false;
    $measureHasRelayLifeLimitHours = false;
    $measureHasRelayHoursOn = false;
    $measureHasRelayHoursRemaining = false;
    $measureHasRelayStarts = false;
    $measureHasRelayState = false;
    $mastersHasPlantKind = false;
}

// 3. Aggiorna stato Master
$incomingFw = trim((string)($data['fw_ver'] ?? 'unknown'));
$updateStmt = $pdo->prepare("UPDATE masters SET last_seen = NOW(), fw_version = ?, rssi = ? WHERE id = ?");
$updateStmt->execute([$incomingFw, $data['rssi'] ?? 0, $master['id']]);

$incomingMasterMode = isset($data['master_mode']) ? (int)$data['master_mode'] : null;
if ($mastersHasPlantKind && $incomingMasterMode !== null) {
    $kindFromMode = '';
    if ($incomingMasterMode === 1) {
        $kindFromMode = 'standalone';
    } elseif ($incomingMasterMode === 2) {
        $kindFromMode = 'rewamping';
    }
    if ($kindFromMode !== '') {
        try {
            $stmtKindMode = $pdo->prepare("
                UPDATE masters
                SET plant_kind = ?
                WHERE id = ?
                  AND (plant_kind IS NULL OR plant_kind = '')
            ");
            $stmtKindMode->execute([$kindFromMode, (int)$master['id']]);
        } catch (Throwable $e) {
            // Non bloccare la telemetria per errore update plant_kind da master_mode.
        }
    }
}

// 3c. Chiusura robusta OTA:
// se la scheda dopo reboot invia una versione allineata al firmware attivo,
// chiudiamo lo stato a Success anche se il report finale OTA non e' arrivato.
try {
    $masterOtaStatus = trim((string)($master['ota_status'] ?? ''));
    if ($masterOtaStatus === 'InProgress') {
        $shouldCloseSuccess = false;

        $stmtActiveFw = $pdo->prepare("
            SELECT version
            FROM firmware_releases
            WHERE device_type = 'master' AND is_active = 1
            ORDER BY id DESC
            LIMIT 1
        ");
        $stmtActiveFw->execute();
        $activeMasterFw = trim((string)($stmtActiveFw->fetchColumn() ?: ''));

        if ($activeMasterFw !== '') {
            $shouldCloseSuccess = (compareFwVersion($incomingFw, $activeMasterFw) >= 0);
        } else {
            $prevFw = trim((string)($master['fw_version'] ?? ''));
            if ($prevFw !== '') {
                $shouldCloseSuccess = (compareFwVersion($incomingFw, $prevFw) !== 0);
            }
        }

        if ($shouldCloseSuccess) {
            $stmtCloseOta = $pdo->prepare("
                UPDATE masters
                SET update_requested = 0,
                    ota_status = 'Success',
                    ota_message = 'Aggiornamento completato (conferma da telemetria).'
                WHERE id = ?
            ");
            $stmtCloseOta->execute([(int)$master['id']]);
        }
    }
} catch (Throwable $e) {
    // Non bloccare il flusso dati in caso di errore riconciliazione OTA.
}

// 3b. Auto-attivazione seriali rilevati (master + slave)
$masterSn = trim((string)($data['master_sn'] ?? ($master['serial_number'] ?? '')));
if ($masterSn !== '') {
    // Master: fallback type 02 (Standalone/Rewamping)
    activateSerialFromTelemetry($pdo, $master, $masterSn, '02');
}

// 4. Salva i dati (Log)
// DeltaP Master + eventuali contatori traffico API.
$traffic = (isset($data['traffic']) && is_array($data['traffic'])) ? $data['traffic'] : [];
$apiTxSession = normalizeTrafficCounter($traffic['api_tx_session'] ?? null);
$apiRxSession = normalizeTrafficCounter($traffic['api_rx_session'] ?? null);
$apiPostsSession = normalizeTrafficCounter($traffic['api_posts_session'] ?? null);
$hasTrafficPayload = ($apiTxSession !== null || $apiRxSession !== null || $apiPostsSession !== null);

$resources = (isset($data['resources']) && is_array($data['resources'])) ? $data['resources'] : [];
$resUptime = normalizeTrafficCounter($resources['uptime_s'] ?? null);
$resCpuMhz = normalizeTrafficCounter($resources['cpu_mhz'] ?? null);
$resHeapFree = normalizeTrafficCounter($resources['heap_free'] ?? null);
$resHeapMin = normalizeTrafficCounter($resources['heap_min'] ?? null);
$resHeapTotal = normalizeTrafficCounter($resources['heap_total'] ?? null);
$resSketchUsed = normalizeTrafficCounter($resources['sketch_used'] ?? null);
$resSketchFree = normalizeTrafficCounter($resources['sketch_free'] ?? null);
$hasResourcesPayload = (
    $resUptime !== null ||
    $resCpuMhz !== null ||
    $resHeapFree !== null ||
    $resHeapMin !== null ||
    $resHeapTotal !== null ||
    $resSketchUsed !== null ||
    $resSketchFree !== null
);

if (isset($data['delta_p']) || $hasTrafficPayload || $hasResourcesPayload) {
    $insCols = ['master_id'];
    $insVals = ['?'];
    $insParams = [(int)$master['id']];

    if ($measureHasMasterSn) {
        $insCols[] = 'master_sn';
        $insVals[] = '?';
        $insParams[] = $masterSn !== '' ? $masterSn : null;
    }
    if (isset($data['delta_p'])) {
        $insCols[] = 'delta_p';
        $insVals[] = '?';
        $insParams[] = $data['delta_p'];
    }

    if ($measureHasApiTxSession) {
        $insCols[] = 'api_tx_session_bytes';
        $insVals[] = '?';
        $insParams[] = $apiTxSession;
    }
    if ($measureHasApiRxSession) {
        $insCols[] = 'api_rx_session_bytes';
        $insVals[] = '?';
        $insParams[] = $apiRxSession;
    }
    if ($measureHasApiPostsSession) {
        $insCols[] = 'api_posts_session_count';
        $insVals[] = '?';
        $insParams[] = $apiPostsSession;
    }
    if ($measureHasResUptime) {
        $insCols[] = 'uptime_seconds';
        $insVals[] = '?';
        $insParams[] = $resUptime;
    }
    if ($measureHasResCpuMhz) {
        $insCols[] = 'cpu_mhz';
        $insVals[] = '?';
        $insParams[] = $resCpuMhz;
    }
    if ($measureHasResHeapFree) {
        $insCols[] = 'heap_free_bytes';
        $insVals[] = '?';
        $insParams[] = $resHeapFree;
    }
    if ($measureHasResHeapMin) {
        $insCols[] = 'heap_min_bytes';
        $insVals[] = '?';
        $insParams[] = $resHeapMin;
    }
    if ($measureHasResHeapTotal) {
        $insCols[] = 'heap_total_bytes';
        $insVals[] = '?';
        $insParams[] = $resHeapTotal;
    }
    if ($measureHasResSketchUsed) {
        $insCols[] = 'sketch_used_bytes';
        $insVals[] = '?';
        $insParams[] = $resSketchUsed;
    }
    if ($measureHasResSketchFree) {
        $insCols[] = 'sketch_free_bytes';
        $insVals[] = '?';
        $insParams[] = $resSketchFree;
    }

    // Calcolo ciclo locale dal precedente campione sessione (solo se colonne presenti).
    if (($measureHasApiTxCycle || $measureHasApiRxCycle) && ($apiTxSession !== null || $apiRxSession !== null)) {
        $prevTx = null;
        $prevRx = null;
        try {
            $selCols = [];
            if ($measureHasApiTxSession) $selCols[] = 'api_tx_session_bytes';
            if ($measureHasApiRxSession) $selCols[] = 'api_rx_session_bytes';
            if (!empty($selCols)) {
                $nonNullConds = [];
                if ($measureHasApiTxSession) $nonNullConds[] = 'api_tx_session_bytes IS NOT NULL';
                if ($measureHasApiRxSession) $nonNullConds[] = 'api_rx_session_bytes IS NOT NULL';
                $nonNullSql = !empty($nonNullConds) ? (' AND (' . implode(' OR ', $nonNullConds) . ')') : '';
                $stmtPrev = $pdo->prepare("
                    SELECT " . implode(', ', $selCols) . "
                    FROM measurements
                    WHERE master_id = ?
                      AND slave_sn IS NULL
                      {$nonNullSql}
                    ORDER BY id DESC
                    LIMIT 1
                ");
                $stmtPrev->execute([(int)$master['id']]);
                $prev = $stmtPrev->fetch();
                if ($prev) {
                    $prevTx = isset($prev['api_tx_session_bytes']) ? normalizeTrafficCounter($prev['api_tx_session_bytes']) : null;
                    $prevRx = isset($prev['api_rx_session_bytes']) ? normalizeTrafficCounter($prev['api_rx_session_bytes']) : null;
                }
            }
        } catch (Throwable $e) {
            $prevTx = null;
            $prevRx = null;
        }

        if ($measureHasApiTxCycle) {
            $cycleTx = null;
            if ($apiTxSession !== null) {
                if ($prevTx === null || $apiTxSession < $prevTx) {
                    $cycleTx = $apiTxSession; // reboot/reset contatore
                } else {
                    $cycleTx = $apiTxSession - $prevTx;
                }
            }
            $insCols[] = 'api_tx_cycle_bytes';
            $insVals[] = '?';
            $insParams[] = $cycleTx;
        }
        if ($measureHasApiRxCycle) {
            $cycleRx = null;
            if ($apiRxSession !== null) {
                if ($prevRx === null || $apiRxSession < $prevRx) {
                    $cycleRx = $apiRxSession; // reboot/reset contatore
                } else {
                    $cycleRx = $apiRxSession - $prevRx;
                }
            }
            $insCols[] = 'api_rx_cycle_bytes';
            $insVals[] = '?';
            $insParams[] = $cycleRx;
        }
    }

    $sqlMasterLog = "INSERT INTO measurements (" . implode(', ', $insCols) . ") VALUES (" . implode(', ', $insVals) . ")";
    $logStmt = $pdo->prepare($sqlMasterLog);
    $logStmt->execute($insParams);
}

// Dati Slaves
if (isset($data['slaves']) && is_array($data['slaves'])) {
    $slaveInsCols = ['master_id'];
    if ($measureHasMasterSn) $slaveInsCols[] = 'master_sn';
    $slaveInsCols[] = 'slave_sn';
    if ($measureHasSlaveId) $slaveInsCols[] = 'slave_id';
    $slaveInsCols[] = 'slave_grp';
    $slaveInsCols[] = 'pressure';
    $slaveInsCols[] = 'temperature';
    $slaveInsCols[] = 'fw_version';
    if ($measureHasDeviceType) $slaveInsCols[] = 'device_type';
    if ($measureHasRelayMode) $slaveInsCols[] = 'relay_mode';
    if ($measureHasRelayOnline) $slaveInsCols[] = 'relay_online';
    if ($measureHasRelayOn) $slaveInsCols[] = 'relay_on';
    if ($measureHasRelaySafetyClosed) $slaveInsCols[] = 'relay_safety_closed';
    if ($measureHasRelayFeedbackOk) $slaveInsCols[] = 'relay_feedback_ok';
    if ($measureHasRelayFeedbackFault) $slaveInsCols[] = 'relay_feedback_fault';
    if ($measureHasRelaySafetyAlarm) $slaveInsCols[] = 'relay_safety_alarm';
    if ($measureHasRelayLifetimeAlarm) $slaveInsCols[] = 'relay_lifetime_alarm';
    if ($measureHasRelayLampFault) $slaveInsCols[] = 'relay_lamp_fault';
    if ($measureHasRelayLifeLimitHours) $slaveInsCols[] = 'relay_life_limit_hours';
    if ($measureHasRelayHoursOn) $slaveInsCols[] = 'relay_hours_on';
    if ($measureHasRelayHoursRemaining) $slaveInsCols[] = 'relay_hours_remaining';
    if ($measureHasRelayStarts) $slaveInsCols[] = 'relay_starts';
    if ($measureHasRelayState) $slaveInsCols[] = 'relay_state';
    $slaveInsSql = "INSERT INTO measurements (" . implode(', ', $slaveInsCols) . ") VALUES (" . implode(', ', array_fill(0, count($slaveInsCols), '?')) . ")";
    $slaveStmt = $pdo->prepare($slaveInsSql);

    $telemetrySlaveSerials = [];
    $seenRelayTelemetry = false;
    foreach ($data['slaves'] as $slave) {
        $slaveSn = trim((string)($slave['sn'] ?? ''));
        $online485 = !isset($slave['online485']) || (int)$slave['online485'] === 1;
        $deviceType = strtolower(trim((string)($slave['device_type'] ?? '')));
        $isRelayTelemetry = (
            $deviceType === 'relay' ||
            array_key_exists('relay_mode', $slave) ||
            array_key_exists('relay_state', $slave) ||
            array_key_exists('relay_starts', $slave)
        );
        if ($isRelayTelemetry) {
            $seenRelayTelemetry = true;
        }
        $relayOnline = normalizeBinaryFlag($slave['relay_online'] ?? null);
        if ($relayOnline === null) {
            $relayOnline = $online485 ? 1 : 0;
        }
        $relayMode = normalizeTrafficCounter($slave['relay_mode'] ?? null);
        $relayOn = normalizeBinaryFlag($slave['relay_on'] ?? null);
        $relaySafetyClosed = normalizeBinaryFlag($slave['relay_safety_closed'] ?? null);
        $relayFeedbackOk = normalizeBinaryFlag($slave['relay_feedback_ok'] ?? null);
        $relayFeedbackFault = normalizeBinaryFlag($slave['relay_feedback_fault'] ?? null);
        $relaySafetyAlarm = normalizeBinaryFlag($slave['relay_safety_alarm'] ?? null);
        $relayLifetimeAlarm = normalizeBinaryFlag($slave['relay_lifetime_alarm'] ?? null);
        $relayLampFault = normalizeBinaryFlag($slave['relay_lamp_fault'] ?? null);
        $relayLifeLimitHours = normalizeTrafficCounter($slave['relay_life_limit_hours'] ?? null);
        $relayHoursOn = normalizeFloatValue($slave['relay_hours_on'] ?? null);
        $relayHoursRemaining = normalizeFloatValue($slave['relay_hours_remaining'] ?? null);
        $relayStarts = normalizeTrafficCounter($slave['relay_starts'] ?? null);
        $relayState = trim((string)($slave['relay_state'] ?? ''));
        if ($relayState === '') {
            $relayState = null;
        }

        $pressureVal = $online485 ? ($slave['p'] ?? null) : null;
        $tempVal = $online485 ? ($slave['t'] ?? null) : null;
        $slaveParams = [(int)$master['id']];
        if ($measureHasMasterSn) $slaveParams[] = ($masterSn !== '' ? $masterSn : null);
        $slaveParams[] = $slaveSn;
        if ($measureHasSlaveId) $slaveParams[] = (isset($slave['id']) ? (int)$slave['id'] : null);
        $slaveParams[] = $slave['grp'];
        $slaveParams[] = $pressureVal;
        $slaveParams[] = $tempVal;
        $slaveParams[] = $slave['ver'] ?? null;
        if ($measureHasDeviceType) $slaveParams[] = ($isRelayTelemetry ? 'relay' : null);
        if ($measureHasRelayMode) $slaveParams[] = ($isRelayTelemetry ? $relayMode : null);
        if ($measureHasRelayOnline) $slaveParams[] = ($isRelayTelemetry ? $relayOnline : null);
        if ($measureHasRelayOn) $slaveParams[] = ($isRelayTelemetry ? $relayOn : null);
        if ($measureHasRelaySafetyClosed) $slaveParams[] = ($isRelayTelemetry ? $relaySafetyClosed : null);
        if ($measureHasRelayFeedbackOk) $slaveParams[] = ($isRelayTelemetry ? $relayFeedbackOk : null);
        if ($measureHasRelayFeedbackFault) $slaveParams[] = ($isRelayTelemetry ? $relayFeedbackFault : null);
        if ($measureHasRelaySafetyAlarm) $slaveParams[] = ($isRelayTelemetry ? $relaySafetyAlarm : null);
        if ($measureHasRelayLifetimeAlarm) $slaveParams[] = ($isRelayTelemetry ? $relayLifetimeAlarm : null);
        if ($measureHasRelayLampFault) $slaveParams[] = ($isRelayTelemetry ? $relayLampFault : null);
        if ($measureHasRelayLifeLimitHours) $slaveParams[] = ($isRelayTelemetry ? $relayLifeLimitHours : null);
        if ($measureHasRelayHoursOn) $slaveParams[] = ($isRelayTelemetry ? $relayHoursOn : null);
        if ($measureHasRelayHoursRemaining) $slaveParams[] = ($isRelayTelemetry ? $relayHoursRemaining : null);
        if ($measureHasRelayStarts) $slaveParams[] = ($isRelayTelemetry ? $relayStarts : null);
        if ($measureHasRelayState) $slaveParams[] = ($isRelayTelemetry ? $relayState : null);
        $slaveStmt->execute($slaveParams);
        if ($slaveSn !== '' && $slaveSn !== '0') {
            $telemetrySlaveSerials[$slaveSn] = true;
        }
        if ($slaveSn !== '' && $slaveSn !== '0') {
            $fallbackType = $isRelayTelemetry ? '03' : '04';
            activateSerialFromTelemetry($pdo, $master, $slaveSn, $fallbackType);
        }
    }

    if ($seenRelayTelemetry && $mastersHasPlantKind) {
        try {
            $currentPlantKind = strtolower(trim((string)($master['plant_kind'] ?? '')));
            if ($currentPlantKind === '') {
                $stmtSetKind = $pdo->prepare("
                    UPDATE masters
                    SET plant_kind = 'standalone'
                    WHERE id = ?
                      AND (plant_kind IS NULL OR plant_kind = '')
                ");
                $stmtSetKind->execute([(int)$master['id']]);
            }
        } catch (Throwable $e) {
            // Non bloccare la telemetria per errore update plant_kind.
        }
    }
} else {
    $telemetrySlaveSerials = [];
}

// 5. Pulizia Log Vecchi (Opzionale qui, meglio via CronJob, ma per ora va bene)
$cleanStmt = $pdo->prepare("DELETE FROM measurements WHERE master_id = ? AND recorded_at < NOW() - INTERVAL ? DAY");
$cleanStmt->execute([$master['id'], $master['log_retention_days'] ?? 30]);

// 6. Comunica al master eventuali seriali da ignorare (retired/voided).
$ignoreSerials = [];
try {
    if (tableExists($pdo, 'device_serials') && columnExists($pdo, 'device_serials', 'status')) {
        $candidate = [];
        if ($masterSn !== '') {
            $candidate[$masterSn] = true;
        }
        foreach (array_keys($telemetrySlaveSerials) as $snKey) {
            $candidate[$snKey] = true;
        }

        if (!empty($candidate)) {
            $serialList = array_keys($candidate);
            $placeholders = implode(',', array_fill(0, count($serialList), '?'));
            $sqlIgnore = "
                SELECT serial_number
                FROM device_serials
                WHERE serial_number IN ($placeholders)
                  AND status IN ('retired','voided')
            ";
            $stmtIgnore = $pdo->prepare($sqlIgnore);
            $stmtIgnore->execute($serialList);
            foreach ($stmtIgnore->fetchAll(PDO::FETCH_COLUMN) as $sn) {
                $sn = trim((string)$sn);
                if ($sn !== '') {
                    $ignoreSerials[] = $sn;
                }
            }
        }
    }
} catch (Throwable $e) {
    // Non bloccare il flusso dati per errore nella lista ignore.
}

echo json_encode(['status' => 'ok', 'ignore_serials' => $ignoreSerials]);
?>
