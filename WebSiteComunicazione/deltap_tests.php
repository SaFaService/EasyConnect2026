<?php
session_start();
require 'config.php';
require 'lang.php';
require_once 'portal_meta.php';

if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$currentUserId = (int)($_SESSION['user_id'] ?? 0);
$currentRole = (string)($_SESSION['user_role'] ?? '');
$isAdmin = ($currentRole === 'admin');

if (!$isAdmin) {
    header("Location: index.php");
    exit;
}

function dtLang(string $key, string $fallback = ''): string {
    global $lang;
    return isset($lang[$key]) ? (string)$lang[$key] : $fallback;
}

function dtTableExists(PDO $pdo, string $tableName): bool {
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

function dtColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
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

function dtMeasurementMasterFilter(bool $hasSlaveSn, bool $hasDeltaP): string {
    $clauses = [];
    if ($hasSlaveSn) $clauses[] = "slave_sn IS NULL";
    if ($hasDeltaP) $clauses[] = "delta_p IS NOT NULL";
    if (empty($clauses)) return "";
    return " AND " . implode(" AND ", $clauses);
}

function dtSecondsBetween(?string $from, ?string $to = null): int {
    $fromTs = ($from !== null && $from !== '') ? strtotime($from) : false;
    if ($fromTs === false) return 0;
    $toTs = ($to !== null && $to !== '') ? strtotime($to) : time();
    if ($toTs === false) $toTs = time();
    $delta = (int)($toTs - $fromTs);
    return ($delta > 0) ? $delta : 0;
}

function dtQueueTestwizCommand(
    PDO $pdo,
    int $masterId,
    string $targetSerial,
    string $commandType,
    array $payload,
    int $createdByUserId
): int {
    $stmtPending = $pdo->prepare("
        SELECT id
        FROM device_commands
        WHERE master_id = ?
          AND command_type IN ('deltap_testwiz_start', 'deltap_testwiz_stop')
          AND status IN ('pending', 'sent')
        ORDER BY id DESC
        LIMIT 1
    ");
    $stmtPending->execute([$masterId]);
    $pendingId = (int)($stmtPending->fetchColumn() ?: 0);
    if ($pendingId > 0) {
        throw new RuntimeException("Esiste gia un comando TESTWIZ pendente (#{$pendingId}).");
    }

    $ins = $pdo->prepare("
        INSERT INTO device_commands (
            master_id, target_serial, command_type, payload_json,
            status, created_by_user_id, created_at
        ) VALUES (?, ?, ?, ?, 'pending', ?, NOW())
    ");
    $ins->execute([
        $masterId,
        $targetSerial,
        $commandType,
        json_encode($payload, JSON_UNESCAPED_UNICODE),
        $createdByUserId
    ]);
    return (int)$pdo->lastInsertId();
}

$hasSessionsTable = dtTableExists($pdo, 'deltap_test_sessions');
$hasDeviceCommandsTable = dtTableExists($pdo, 'device_commands');
$hasMastersTable = dtTableExists($pdo, 'masters');
$hasMeasurementsTable = dtTableExists($pdo, 'measurements');
$hasMasterBuilderColumn = $hasMastersTable && dtColumnExists($pdo, 'masters', 'builder_id');
$hasMasterDeletedAtColumn = $hasMastersTable && dtColumnExists($pdo, 'masters', 'deleted_at');

$measureHasId = $hasMeasurementsTable && dtColumnExists($pdo, 'measurements', 'id');
$measureHasSlaveSn = $hasMeasurementsTable && dtColumnExists($pdo, 'measurements', 'slave_sn');
$measureHasDeltaP = $hasMeasurementsTable && dtColumnExists($pdo, 'measurements', 'delta_p');

$masters = [];
if ($hasMastersTable) {
    if ($isAdmin) {
        $sql = "
            SELECT id, nickname, serial_number, address
            FROM masters
        ";
        if ($hasMasterDeletedAtColumn) {
            $sql .= " WHERE deleted_at IS NULL ";
        }
        $sql .= " ORDER BY nickname ASC, id ASC";
        $masters = $pdo->query($sql)->fetchAll();
    } else {
        $sql = "
            SELECT id, nickname, serial_number, address
            FROM masters
            WHERE (
                creator_id = :uidCreator
                OR owner_id = :uidOwner
                OR maintainer_id = :uidMaintainer
                " . ($hasMasterBuilderColumn ? " OR builder_id = :uidBuilder " : "") . "
            )
        ";
        if ($hasMasterDeletedAtColumn) {
            $sql .= " AND deleted_at IS NULL ";
        }
        $sql .= " ORDER BY nickname ASC, id ASC";
        $stmt = $pdo->prepare($sql);
        $params = [
            'uidCreator' => $currentUserId,
            'uidOwner' => $currentUserId,
            'uidMaintainer' => $currentUserId,
        ];
        if ($hasMasterBuilderColumn) {
            $params['uidBuilder'] = $currentUserId;
        }
        $stmt->execute($params);
        $masters = $stmt->fetchAll();
    }
}

$mastersById = [];
foreach ($masters as $m) {
    $mastersById[(int)$m['id']] = $m;
}

$selectedMasterId = isset($_REQUEST['master_id']) ? (int)$_REQUEST['master_id'] : 0;
if ($selectedMasterId <= 0 && !empty($masters)) {
    $selectedMasterId = (int)$masters[0]['id'];
}
if ($selectedMasterId > 0 && !isset($mastersById[$selectedMasterId])) {
    $selectedMasterId = !empty($masters) ? (int)$masters[0]['id'] : 0;
}

$message = '';
$messageType = '';
$getAction = (string)($_GET['action'] ?? '');

if (
    $_SERVER['REQUEST_METHOD'] === 'GET' &&
    $getAction === 'session_status'
) {
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    header('Pragma: no-cache');
    header('Expires: 0');

    if (!$hasSessionsTable) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'sessions_table_missing']);
        exit;
    }

    $sessionId = (int)($_GET['session_id'] ?? 0);
    $targetValid = (int)($_GET['target_valid'] ?? 400);
    if ($targetValid < 1) $targetValid = 1;
    if ($targetValid > 5000) $targetValid = 5000;

    if ($sessionId <= 0) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'invalid_session_id']);
        exit;
    }
    if (!$hasMeasurementsTable || !$measureHasId) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'measurements_not_compatible']);
        exit;
    }

    $stmtSession = $pdo->prepare("
        SELECT *
        FROM deltap_test_sessions
        WHERE id = ?
        LIMIT 1
    ");
    $stmtSession->execute([$sessionId]);
    $session = $stmtSession->fetch();
    if (!$session) {
        http_response_code(404);
        echo json_encode(['ok' => false, 'error' => 'session_not_found']);
        exit;
    }

    $masterId = (int)$session['master_id'];
    if (!isset($mastersById[$masterId])) {
        http_response_code(403);
        echo json_encode(['ok' => false, 'error' => 'session_not_accessible']);
        exit;
    }

    $status = (string)($session['status'] ?? '');
    $running = ($status === 'running');
    $startId = (int)($session['started_measurement_id'] ?? 1);
    if ($startId <= 0) $startId = 1;

    if ($running) {
        $stmtEnd = $pdo->prepare("SELECT COALESCE(MAX(id), 0) FROM measurements WHERE master_id = ?");
        $stmtEnd->execute([$masterId]);
        $endId = (int)$stmtEnd->fetchColumn();
    } else {
        $endId = (int)($session['ended_measurement_id'] ?? 0);
        if ($endId <= 0) {
            $stmtEnd = $pdo->prepare("SELECT COALESCE(MAX(id), 0) FROM measurements WHERE master_id = ?");
            $stmtEnd->execute([$masterId]);
            $endId = (int)$stmtEnd->fetchColumn();
        }
    }
    if ($endId < $startId) $endId = $startId - 1;

    $validRecords = 0;
    if ($endId >= $startId) {
        $sqlCount = "
            SELECT COUNT(*)
            FROM measurements
            WHERE master_id = :masterId
              AND id BETWEEN :startId AND :endId
              " . dtMeasurementMasterFilter($measureHasSlaveSn, $measureHasDeltaP) . "
        ";
        $stmtCount = $pdo->prepare($sqlCount);
        $stmtCount->execute([
            'masterId' => $masterId,
            'startId' => $startId,
            'endId' => $endId,
        ]);
        $validRecords = (int)$stmtCount->fetchColumn();
    }

    $latestId = 0;
    $latestRecordedAt = null;
    if ($endId >= $startId) {
        $sqlLatest = "
            SELECT id, recorded_at
            FROM measurements
            WHERE master_id = :masterId
              AND id BETWEEN :startId AND :endId
              " . dtMeasurementMasterFilter($measureHasSlaveSn, $measureHasDeltaP) . "
            ORDER BY id DESC
            LIMIT 1
        ";
        $stmtLatest = $pdo->prepare($sqlLatest);
        $stmtLatest->execute([
            'masterId' => $masterId,
            'startId' => $startId,
            'endId' => $endId,
        ]);
        $latestRow = $stmtLatest->fetch(PDO::FETCH_ASSOC);
        if ($latestRow) {
            $latestId = (int)($latestRow['id'] ?? 0);
            $latestRecordedAt = (string)($latestRow['recorded_at'] ?? '');
            if ($latestRecordedAt === '') $latestRecordedAt = null;
        }
    }

    $startedAt = (string)($session['started_at'] ?? '');
    $endedAt = (string)($session['ended_at'] ?? '');
    $elapsedSeconds = $running ? dtSecondsBetween($startedAt, null) : dtSecondsBetween($startedAt, $endedAt);
    $remainingToTarget = max(0, $targetValid - $validRecords);
    $completionPercent = ($targetValid > 0) ? min(100.0, round(($validRecords * 100.0) / $targetValid, 1)) : 0.0;
    $ratePerSec = ($elapsedSeconds > 0) ? ($validRecords / $elapsedSeconds) : 0.0;
    $ratePerMin = round($ratePerSec * 60.0, 2);
    $etaSeconds = null;
    if ($remainingToTarget > 0 && $ratePerSec > 0.0) {
        $etaSeconds = (int)ceil($remainingToTarget / $ratePerSec);
    }

    $latestAgeSeconds = null;
    if ($latestRecordedAt !== null) {
        $latestAgeSeconds = dtSecondsBetween($latestRecordedAt, null);
    }
    $savingRecords = ($running && $latestAgeSeconds !== null && $latestAgeSeconds <= 15);

    echo json_encode([
        'ok' => true,
        'session_id' => $sessionId,
        'master_id' => $masterId,
        'session_status' => $status,
        'running' => $running,
        'dirt_level' => (int)($session['dirt_level'] ?? 0),
        'speed_index' => (int)($session['speed_index'] ?? 0),
        'total_speeds' => (int)($session['total_speeds'] ?? 0),
        'started_at' => ($startedAt !== '' ? $startedAt : null),
        'ended_at' => ($endedAt !== '' ? $endedAt : null),
        'elapsed_seconds' => $elapsedSeconds,
        'target_valid_records' => $targetValid,
        'valid_records' => $validRecords,
        'remaining_to_target' => $remainingToTarget,
        'completion_percent' => $completionPercent,
        'record_rate_per_min' => $ratePerMin,
        'estimated_seconds_to_target' => $etaSeconds,
        'range_start_id' => $startId,
        'range_end_id' => $endId,
        'latest_record_id' => $latestId,
        'latest_recorded_at' => $latestRecordedAt,
        'latest_age_seconds' => $latestAgeSeconds,
        'saving_records' => $savingRecords,
    ]);
    exit;
}

if (
    $_SERVER['REQUEST_METHOD'] === 'GET' &&
    $getAction === 'download' &&
    $hasSessionsTable
) {
    $sessionId = (int)($_GET['session_id'] ?? 0);
    if ($sessionId <= 0) {
        http_response_code(400);
        echo "Sessione non valida.";
        exit;
    }

    $stmtSession = $pdo->prepare("
        SELECT *
        FROM deltap_test_sessions
        WHERE id = ?
        LIMIT 1
    ");
    $stmtSession->execute([$sessionId]);
    $session = $stmtSession->fetch();
    if (!$session) {
        http_response_code(404);
        echo "Sessione non trovata.";
        exit;
    }

    $masterId = (int)$session['master_id'];
    if (!isset($mastersById[$masterId])) {
        http_response_code(403);
        echo "Sessione non accessibile.";
        exit;
    }
    if (!$hasMeasurementsTable || !$measureHasId) {
        http_response_code(500);
        echo "Tabella measurements non compatibile per export.";
        exit;
    }

    $rangeStart = (int)($session['started_measurement_id'] ?? 0);
    if ($rangeStart <= 0) $rangeStart = 1;
    $rangeEnd = (int)($session['ended_measurement_id'] ?? 0);
    if ($rangeEnd <= 0) {
        $stmtMax = $pdo->prepare("SELECT COALESCE(MAX(id), 0) FROM measurements WHERE master_id = ?");
        $stmtMax->execute([$masterId]);
        $rangeEnd = (int)$stmtMax->fetchColumn();
    }

    $optionalCols = [];
    $candidateCols = [
        'master_sn',
        'delta_p',
        'fw_version',
        'uptime_seconds',
        'api_tx_session_bytes',
        'api_rx_session_bytes',
        'api_posts_session_count',
        'api_tx_cycle_bytes',
        'api_rx_cycle_bytes',
        'heap_free_bytes',
        'heap_min_bytes',
        'heap_total_bytes',
    ];
    foreach ($candidateCols as $col) {
        if (dtColumnExists($pdo, 'measurements', $col)) {
            $optionalCols[] = $col;
        }
    }

    $selectCols = array_merge(['id', 'recorded_at'], $optionalCols);
    $sql = "
        SELECT " . implode(', ', $selectCols) . "
        FROM measurements
        WHERE master_id = :masterId
          AND id BETWEEN :startId AND :endId
          " . dtMeasurementMasterFilter($measureHasSlaveSn, $measureHasDeltaP) . "
        ORDER BY id ASC
    ";
    $stmtRows = $pdo->prepare($sql);
    $stmtRows->execute([
        'masterId' => $masterId,
        'startId' => $rangeStart,
        'endId' => $rangeEnd,
    ]);

    $masterLabel = trim((string)($mastersById[$masterId]['nickname'] ?? 'master_' . $masterId));
    $safeMaster = preg_replace('/[^a-zA-Z0-9_-]/', '_', $masterLabel);
    $fileName = "deltap_session_" . $sessionId . "_" . $safeMaster . ".csv";

    header('Content-Type: text/csv; charset=utf-8');
    header('Content-Disposition: attachment; filename="' . $fileName . '"');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    header('Pragma: no-cache');
    header('Expires: 0');

    $out = fopen('php://output', 'w');
    fputcsv($out, ['session_id', $sessionId, 'master_id', $masterId, 'dirt_level', (int)$session['dirt_level'], 'speed', (int)$session['speed_index'] . '/' . (int)$session['total_speeds']]);
    fputcsv($out, ['status', (string)$session['status'], 'started_at', (string)$session['started_at'], 'ended_at', (string)($session['ended_at'] ?? '')]);
    fputcsv($out, []);
    fputcsv($out, $selectCols);
    while ($row = $stmtRows->fetch(PDO::FETCH_ASSOC)) {
        $line = [];
        foreach ($selectCols as $c) {
            $line[] = $row[$c] ?? null;
        }
        fputcsv($out, $line);
    }
    fclose($out);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = (string)($_POST['action'] ?? '');

    if (!$hasSessionsTable) {
        $message = "Tabella deltap_test_sessions non presente. Eseguire prima la migrazione SQL.";
        $messageType = 'danger';
    } else {
        try {
            if ($action === 'start_session') {
                $masterId = (int)($_POST['master_id'] ?? 0);
                $dirtLevel = (int)($_POST['dirt_level'] ?? 0);
                $totalSpeeds = (int)($_POST['total_speeds'] ?? 0);
                $speedIndex = (int)($_POST['speed_index'] ?? 0);
                $notes = trim((string)($_POST['notes'] ?? ''));
                $remoteStart = isset($_POST['remote_start']) && (int)$_POST['remote_start'] === 1;

                if (!isset($mastersById[$masterId])) {
                    throw new RuntimeException("Master non accessibile.");
                }
                if (!$hasMeasurementsTable || !$measureHasId) {
                    throw new RuntimeException("Tabella measurements non compatibile.");
                }
                if ($dirtLevel < 1 || $dirtLevel > 3) {
                    throw new RuntimeException("Livello sporco non valido (1..3).");
                }
                if ($totalSpeeds < 1 || $totalSpeeds > 10) {
                    throw new RuntimeException("Numero velocita non valido (1..10).");
                }
                if ($speedIndex < 1 || $speedIndex > $totalSpeeds) {
                    throw new RuntimeException("Velocita corrente non valida.");
                }

                $pdo->beginTransaction();

                $stmtRunning = $pdo->prepare("
                    SELECT id
                    FROM deltap_test_sessions
                    WHERE master_id = ?
                      AND status = 'running'
                    LIMIT 1
                    FOR UPDATE
                ");
                $stmtRunning->execute([$masterId]);
                if ($stmtRunning->fetchColumn()) {
                    $pdo->rollBack();
                    throw new RuntimeException("Esiste gia una sessione in corso per questo master.");
                }

                $stmtStartId = $pdo->prepare("SELECT COALESCE(MAX(id), 0) + 1 FROM measurements WHERE master_id = ?");
                $stmtStartId->execute([$masterId]);
                $startMeasurementId = (int)$stmtStartId->fetchColumn();

                $masterSn = trim((string)($mastersById[$masterId]['serial_number'] ?? ''));
                $stmtInsert = $pdo->prepare("
                    INSERT INTO deltap_test_sessions (
                        master_id,
                        master_sn,
                        dirt_level,
                        total_speeds,
                        speed_index,
                        status,
                        started_measurement_id,
                        started_at,
                        notes,
                        created_by_user_id,
                        created_by_role
                    ) VALUES (?, ?, ?, ?, ?, 'running', ?, NOW(), ?, ?, ?)
                ");
                $stmtInsert->execute([
                    $masterId,
                    ($masterSn !== '' ? $masterSn : null),
                    $dirtLevel,
                    $totalSpeeds,
                    $speedIndex,
                    $startMeasurementId,
                    ($notes !== '' ? $notes : null),
                    $currentUserId,
                    $currentRole,
                ]);
                $sessionId = (int)$pdo->lastInsertId();

                $queuedCommandId = 0;
                if ($remoteStart) {
                    if (!$hasDeviceCommandsTable) {
                        throw new RuntimeException("Tabella device_commands non disponibile: impossibile avviare remoto.");
                    }
                    $targetSerial = ($masterSn !== '') ? $masterSn : ('MASTER-' . $masterId);
                    $queuedCommandId = dtQueueTestwizCommand(
                        $pdo,
                        $masterId,
                        $targetSerial,
                        'deltap_testwiz_start',
                        [
                            'total_speeds' => $totalSpeeds,
                            'dirt_level' => $dirtLevel,
                            'speed_index' => $speedIndex
                        ],
                        $currentUserId
                    );
                }

                $pdo->commit();
                if ($remoteStart) {
                    $message = "Sessione #{$sessionId} avviata. Comando remoto TESTWIZ_START accodato (#{$queuedCommandId}).";
                } else {
                    $message = "Sessione #{$sessionId} avviata. Avvia ora il test sulla centralina (wizard seriale/web).";
                }
                $messageType = 'success';
            } elseif ($action === 'stop_session' || $action === 'abort_session') {
                $sessionId = (int)($_POST['session_id'] ?? 0);
                if ($sessionId <= 0) {
                    throw new RuntimeException("Sessione non valida.");
                }
                if (!$hasMeasurementsTable || !$measureHasId) {
                    throw new RuntimeException("Tabella measurements non compatibile.");
                }

                $pdo->beginTransaction();
                $stmtSession = $pdo->prepare("
                    SELECT *
                    FROM deltap_test_sessions
                    WHERE id = ?
                    LIMIT 1
                    FOR UPDATE
                ");
                $stmtSession->execute([$sessionId]);
                $session = $stmtSession->fetch();
                if (!$session) {
                    $pdo->rollBack();
                    throw new RuntimeException("Sessione non trovata.");
                }

                $masterId = (int)$session['master_id'];
                if (!isset($mastersById[$masterId])) {
                    $pdo->rollBack();
                    throw new RuntimeException("Sessione non accessibile.");
                }
                if ((string)$session['status'] !== 'running') {
                    $pdo->rollBack();
                    throw new RuntimeException("La sessione non e in stato running.");
                }

                $stmtEndId = $pdo->prepare("SELECT COALESCE(MAX(id), 0) FROM measurements WHERE master_id = ?");
                $stmtEndId->execute([$masterId]);
                $endMeasurementId = (int)$stmtEndId->fetchColumn();

                $startMeasurementId = (int)($session['started_measurement_id'] ?? 1);
                if ($startMeasurementId <= 0) $startMeasurementId = 1;
                $safeEndMeasurementId = $endMeasurementId;
                if ($safeEndMeasurementId < $startMeasurementId) {
                    $safeEndMeasurementId = $startMeasurementId - 1;
                }

                $masterRecordCount = 0;
                if ($safeEndMeasurementId >= $startMeasurementId) {
                    $sqlCount = "
                        SELECT COUNT(*)
                        FROM measurements
                        WHERE master_id = :masterId
                          AND id BETWEEN :startId AND :endId
                          " . dtMeasurementMasterFilter($measureHasSlaveSn, $measureHasDeltaP) . "
                    ";
                    $stmtCount = $pdo->prepare($sqlCount);
                    $stmtCount->execute([
                        'masterId' => $masterId,
                        'startId' => $startMeasurementId,
                        'endId' => $safeEndMeasurementId,
                    ]);
                    $masterRecordCount = (int)$stmtCount->fetchColumn();
                }

                $finalStatus = 'aborted';
                if ($action === 'stop_session') {
                    $finalStatus = ($masterRecordCount > 0) ? 'completed' : 'empty';
                }

                $stmtUpdate = $pdo->prepare("
                    UPDATE deltap_test_sessions
                    SET status = ?,
                        ended_measurement_id = ?,
                        ended_at = NOW(),
                        master_record_count = ?,
                        updated_at = NOW()
                    WHERE id = ?
                ");
                $stmtUpdate->execute([
                    $finalStatus,
                    $safeEndMeasurementId,
                    $masterRecordCount,
                    $sessionId,
                ]);
                $pdo->commit();

                if ($action === 'abort_session') {
                    $message = "Sessione annullata.";
                } else {
                    $message = "Sessione chiusa. Record DeltaP master nel range: " . $masterRecordCount . ".";
                }
                $messageType = 'success';
            } elseif ($action === 'queue_remote_stop') {
                $sessionId = (int)($_POST['session_id'] ?? 0);
                $stopAction = trim((string)($_POST['stop_action'] ?? 'save'));
                if ($sessionId <= 0) {
                    throw new RuntimeException("Sessione non valida.");
                }
                if (!$hasDeviceCommandsTable) {
                    throw new RuntimeException("Tabella device_commands non disponibile.");
                }

                $pdo->beginTransaction();
                $stmtSession = $pdo->prepare("
                    SELECT *
                    FROM deltap_test_sessions
                    WHERE id = ?
                    LIMIT 1
                    FOR UPDATE
                ");
                $stmtSession->execute([$sessionId]);
                $session = $stmtSession->fetch();
                if (!$session) {
                    $pdo->rollBack();
                    throw new RuntimeException("Sessione non trovata.");
                }
                $masterId = (int)$session['master_id'];
                if (!isset($mastersById[$masterId])) {
                    $pdo->rollBack();
                    throw new RuntimeException("Sessione non accessibile.");
                }
                if ((string)$session['status'] !== 'running') {
                    $pdo->rollBack();
                    throw new RuntimeException("La sessione non e in stato running.");
                }

                $masterSn = trim((string)($mastersById[$masterId]['serial_number'] ?? ''));
                $targetSerial = ($masterSn !== '') ? $masterSn : ('MASTER-' . $masterId);
                $queuePayload = ['stop_action' => ($stopAction === 'abort' ? 'abort' : 'save')];
                $commandId = dtQueueTestwizCommand(
                    $pdo,
                    $masterId,
                    $targetSerial,
                    'deltap_testwiz_stop',
                    $queuePayload,
                    $currentUserId
                );

                $pdo->commit();
                $message = "Comando remoto TESTWIZ_STOP accodato (#{$commandId}). Quando il test e realmente fermo, premi 'Stop + Chiudi'.";
                $messageType = 'success';
            }
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            $message = "Errore sessione DeltaP: " . $e->getMessage();
            $messageType = 'danger';
        }
    }
}

$sessions = [];
$runningSession = null;
if ($hasSessionsTable && $selectedMasterId > 0 && isset($mastersById[$selectedMasterId])) {
    $stmtSessions = $pdo->prepare("
        SELECT s.*, m.nickname AS master_nickname, m.serial_number AS master_serial
        FROM deltap_test_sessions s
        LEFT JOIN masters m ON m.id = s.master_id
        WHERE s.master_id = ?
        ORDER BY s.id DESC
        LIMIT 200
    ");
    $stmtSessions->execute([$selectedMasterId]);
    $sessions = $stmtSessions->fetchAll();
    foreach ($sessions as $sx) {
        if ((string)$sx['status'] === 'running') {
            $runningSession = $sx;
            break;
        }
    }
}
?>
<!DOCTYPE html>
<html lang="<?php echo htmlspecialchars((string)($_SESSION['lang'] ?? 'it')); ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo htmlspecialchars(dtLang('deltap_tests_title', 'Campionamento DeltaP')); ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">
<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="d-flex justify-content-between align-items-center mb-3">
        <h4 class="mb-0"><i class="fas fa-flask-vial"></i> <?php echo htmlspecialchars(dtLang('deltap_tests_title', 'Campionamento DeltaP')); ?></h4>
        <span class="badge bg-secondary">Portal v<?php echo htmlspecialchars(ANTRALUX_PORTAL_VERSION); ?></span>
    </div>

    <?php if (!$hasSessionsTable): ?>
        <div class="alert alert-warning">
            <strong>DB non pronto.</strong> Eseguire prima lo script SQL <code>sql_step_2_14_deltap_test_sessions.sql</code>.
        </div>
    <?php endif; ?>
    <?php if (!$hasDeviceCommandsTable): ?>
        <div class="alert alert-warning">
            <strong>Comandi remoti non disponibili.</strong> Tabella <code>device_commands</code> assente. Applica anche <code>sql_step_2_11_device_commands_and_slave_id.sql</code>.
        </div>
    <?php endif; ?>

    <?php if (!empty($message)): ?>
        <div class="alert alert-<?php echo htmlspecialchars($messageType ?: 'info'); ?>">
            <?php echo htmlspecialchars($message); ?>
        </div>
    <?php endif; ?>

    <div class="card shadow-sm mb-3">
        <div class="card-body">
            <form method="get" class="row g-2 align-items-end">
                <div class="col-md-6">
                    <label class="form-label">Master</label>
                    <select name="master_id" class="form-select" required>
                        <?php if (empty($masters)): ?>
                            <option value="">Nessun master disponibile</option>
                        <?php else: ?>
                            <?php foreach ($masters as $m): ?>
                                <?php $mid = (int)$m['id']; ?>
                                <option value="<?php echo $mid; ?>" <?php echo ($selectedMasterId === $mid) ? 'selected' : ''; ?>>
                                    <?php echo htmlspecialchars(($m['nickname'] ?? ('Master ' . $mid)) . ' | SN: ' . ($m['serial_number'] ?? '-')); ?>
                                </option>
                            <?php endforeach; ?>
                        <?php endif; ?>
                    </select>
                </div>
                <div class="col-md-3">
                    <button type="submit" class="btn btn-outline-primary w-100"><i class="fas fa-filter"></i> Carica sessioni</button>
                </div>
                <div class="col-md-3">
                    <a href="deltap_tests.php" class="btn btn-outline-secondary w-100"><i class="fas fa-rotate-right"></i> Reset</a>
                </div>
            </form>
        </div>
    </div>

    <?php if ($selectedMasterId > 0 && isset($mastersById[$selectedMasterId])): ?>
        <div class="card shadow-sm mb-3">
            <div class="card-header">Nuova sessione test</div>
            <div class="card-body">
                <form method="post" class="row g-2 align-items-end">
                    <input type="hidden" name="action" value="start_session">
                    <input type="hidden" name="master_id" value="<?php echo (int)$selectedMasterId; ?>">
                    <div class="col-md-2">
                        <label class="form-label">Livello sporco</label>
                        <input type="number" class="form-control" name="dirt_level" min="1" max="3" value="1" required>
                    </div>
                    <div class="col-md-2">
                        <label class="form-label">N. velocita</label>
                        <input type="number" class="form-control" name="total_speeds" min="1" max="10" value="5" required>
                    </div>
                    <div class="col-md-2">
                        <label class="form-label">Velocita corrente</label>
                        <input type="number" class="form-control" name="speed_index" min="1" max="10" value="1" required>
                    </div>
                    <div class="col-md-4">
                        <label class="form-label">Note (opzionale)</label>
                        <input type="text" class="form-control" name="notes" maxlength="255" placeholder="Es. filtro pulito, prova laboratorio">
                    </div>
                    <div class="col-md-1">
                        <label class="form-label">Remoto</label>
                        <div class="form-check mt-2">
                            <input class="form-check-input" type="checkbox" name="remote_start" value="1" id="remoteStartChk" checked <?php echo !$hasDeviceCommandsTable ? 'disabled' : ''; ?>>
                            <label class="form-check-label" for="remoteStartChk">ON</label>
                        </div>
                    </div>
                    <div class="col-md-1">
                        <button type="submit" class="btn btn-success w-100" <?php echo !$hasSessionsTable ? 'disabled' : ''; ?>>
                            <i class="fas fa-play"></i> Avvia
                        </button>
                    </div>
                </form>
                <div class="mt-2 text-muted small">
                    Nota: con "Remoto ON" il portale accoda il comando TESTWIZ_START alla master. Esecuzione al prossimo ciclo comandi (tipicamente entro ~30s). Con firmware >= 1.1.14 l'invio al portale passa a 2s durante TESTWIZ.
                </div>
            </div>
        </div>

        <?php if ($runningSession): ?>
            <div class="card border-warning shadow-sm mb-3">
                <div class="card-header bg-warning-subtle">Sessione in corso</div>
                <div class="card-body">
                    <div><strong>ID sessione:</strong> <?php echo (int)$runningSession['id']; ?></div>
                    <div><strong>Range start:</strong> <?php echo (int)($runningSession['started_measurement_id'] ?? 0); ?></div>
                    <div><strong>Velocita:</strong> <?php echo (int)$runningSession['speed_index']; ?>/<?php echo (int)$runningSession['total_speeds']; ?> | <strong>Sporco:</strong> <?php echo (int)$runningSession['dirt_level']; ?></div>
                    <div class="mt-3 border rounded p-2 bg-light">
                        <div class="fw-semibold mb-1">Monitor live test (target 400 record validi)</div>
                        <div class="row g-2 small">
                            <div class="col-md-4"><strong>Stato:</strong> <span id="dtLiveStatus">--</span></div>
                            <div class="col-md-4"><strong>Salvataggio:</strong> <span id="dtLiveSaving">--</span></div>
                            <div class="col-md-4"><strong>Tempo trascorso:</strong> <span id="dtLiveElapsed">--</span></div>
                            <div class="col-md-4"><strong>Record validi:</strong> <span id="dtLiveValid">--</span></div>
                            <div class="col-md-4"><strong>Restanti a 400:</strong> <span id="dtLiveRemaining">--</span></div>
                            <div class="col-md-4"><strong>ETA:</strong> <span id="dtLiveEta">--</span></div>
                            <div class="col-md-6"><strong>Range ID:</strong> <span id="dtLiveRange">--</span></div>
                            <div class="col-md-6"><strong>Ultimo record:</strong> <span id="dtLiveLastRecord">--</span></div>
                        </div>
                        <div class="progress mt-2" role="progressbar" aria-label="Progresso record validi">
                            <div id="dtLiveProgressBar" class="progress-bar bg-success" style="width: 0%">0%</div>
                        </div>
                        <div class="small text-muted mt-1" id="dtLiveUpdatedAt">Aggiornamento in corso...</div>
                    </div>
                    <div class="mt-2 d-flex gap-2">
                        <form method="post">
                            <input type="hidden" name="master_id" value="<?php echo (int)$selectedMasterId; ?>">
                            <input type="hidden" name="action" value="queue_remote_stop">
                            <input type="hidden" name="session_id" value="<?php echo (int)$runningSession['id']; ?>">
                            <input type="hidden" name="stop_action" value="save">
                            <button type="submit" class="btn btn-sm btn-outline-primary"><i class="fas fa-tower-broadcast"></i> Stop remoto</button>
                        </form>
                        <form method="post">
                            <input type="hidden" name="master_id" value="<?php echo (int)$selectedMasterId; ?>">
                            <input type="hidden" name="action" value="stop_session">
                            <input type="hidden" name="session_id" value="<?php echo (int)$runningSession['id']; ?>">
                            <button type="submit" class="btn btn-sm btn-warning"><i class="fas fa-stop"></i> Stop + Chiudi</button>
                        </form>
                        <form method="post">
                            <input type="hidden" name="master_id" value="<?php echo (int)$selectedMasterId; ?>">
                            <input type="hidden" name="action" value="abort_session">
                            <input type="hidden" name="session_id" value="<?php echo (int)$runningSession['id']; ?>">
                            <button type="submit" class="btn btn-sm btn-outline-danger"><i class="fas fa-ban"></i> Annulla</button>
                        </form>
                    </div>
                </div>
            </div>
        <?php endif; ?>

        <div class="card shadow-sm mb-4">
            <div class="card-header">Storico sessioni (master selezionato)</div>
            <div class="card-body p-0">
                <div class="table-responsive">
                    <table class="table table-sm table-striped mb-0 align-middle">
                        <thead>
                            <tr>
                                <th>ID</th>
                                <th>Stato</th>
                                <th>Sporco</th>
                                <th>Velocita</th>
                                <th>Start ID</th>
                                <th>End ID</th>
                                <th>Record</th>
                                <th>Avvio</th>
                                <th>Fine</th>
                                <th>Azioni</th>
                            </tr>
                        </thead>
                        <tbody>
                            <?php if (empty($sessions)): ?>
                                <tr><td colspan="10" class="text-muted text-center py-3">Nessuna sessione registrata.</td></tr>
                            <?php else: ?>
                                <?php foreach ($sessions as $s): ?>
                                    <tr>
                                        <td><?php echo (int)$s['id']; ?></td>
                                        <td><?php echo htmlspecialchars((string)$s['status']); ?></td>
                                        <td><?php echo (int)$s['dirt_level']; ?></td>
                                        <td><?php echo (int)$s['speed_index']; ?>/<?php echo (int)$s['total_speeds']; ?></td>
                                        <td><?php echo (int)($s['started_measurement_id'] ?? 0); ?></td>
                                        <td><?php echo (int)($s['ended_measurement_id'] ?? 0); ?></td>
                                        <td><?php echo (int)($s['master_record_count'] ?? 0); ?></td>
                                        <td><?php echo htmlspecialchars((string)($s['started_at'] ?? '')); ?></td>
                                        <td><?php echo htmlspecialchars((string)($s['ended_at'] ?? '')); ?></td>
                                        <td>
                                            <a class="btn btn-sm btn-outline-primary" href="deltap_tests.php?action=download&session_id=<?php echo (int)$s['id']; ?>&master_id=<?php echo (int)$selectedMasterId; ?>">
                                                <i class="fas fa-file-csv"></i> CSV
                                            </a>
                                            <?php if ((string)$s['status'] === 'running'): ?>
                                                <form method="post" class="d-inline">
                                                    <input type="hidden" name="master_id" value="<?php echo (int)$selectedMasterId; ?>">
                                                    <input type="hidden" name="action" value="stop_session">
                                                    <input type="hidden" name="session_id" value="<?php echo (int)$s['id']; ?>">
                                                    <button type="submit" class="btn btn-sm btn-warning"><i class="fas fa-stop"></i></button>
                                                </form>
                                            <?php endif; ?>
                                        </td>
                                    </tr>
                                <?php endforeach; ?>
                            <?php endif; ?>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    <?php endif; ?>
</div>

<script>
document.addEventListener('DOMContentLoaded', function () {
    const runningSessionId = <?php echo $runningSession ? (int)$runningSession['id'] : 0; ?>;
    const selectedMasterId = <?php echo (int)$selectedMasterId; ?>;
    const targetValid = 400;
    if (!runningSessionId || !selectedMasterId) return;

    const elStatus = document.getElementById('dtLiveStatus');
    const elSaving = document.getElementById('dtLiveSaving');
    const elElapsed = document.getElementById('dtLiveElapsed');
    const elValid = document.getElementById('dtLiveValid');
    const elRemaining = document.getElementById('dtLiveRemaining');
    const elEta = document.getElementById('dtLiveEta');
    const elRange = document.getElementById('dtLiveRange');
    const elLastRecord = document.getElementById('dtLiveLastRecord');
    const elProgressBar = document.getElementById('dtLiveProgressBar');
    const elUpdatedAt = document.getElementById('dtLiveUpdatedAt');

    function fmtDuration(totalSeconds) {
        const sec = Math.max(0, Number(totalSeconds) || 0);
        const h = Math.floor(sec / 3600);
        const m = Math.floor((sec % 3600) / 60);
        const s = Math.floor(sec % 60);
        if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
        return m + 'm ' + s + 's';
    }

    function fmtDate(dtString) {
        if (!dtString) return 'N/D';
        const dt = new Date(String(dtString).replace(' ', 'T'));
        if (Number.isNaN(dt.getTime())) return String(dtString);
        return dt.toLocaleString();
    }

    function setText(el, text) {
        if (el) el.textContent = text;
    }

    function renderStatus(data) {
        const running = !!data.running;
        const statusText = running ? 'RUNNING' : String(data.session_status || 'N/D').toUpperCase();
        setText(elStatus, statusText);

        const saving = !!data.saving_records;
        setText(elSaving, saving ? 'SI (record recenti)' : (running ? 'NO (nessun record recente)' : 'N/D'));

        setText(elElapsed, fmtDuration(data.elapsed_seconds || 0));

        const valid = Number(data.valid_records || 0);
        setText(elValid, valid + ' / ' + targetValid);

        const remaining = Number(data.remaining_to_target || 0);
        setText(elRemaining, String(remaining));

        const eta = data.estimated_seconds_to_target;
        setText(elEta, (eta === null || eta === undefined) ? 'N/D' : fmtDuration(eta));

        const startId = Number(data.range_start_id || 0);
        const endId = Number(data.range_end_id || 0);
        setText(elRange, startId + ' -> ' + endId);

        const lastId = Number(data.latest_record_id || 0);
        const lastAge = data.latest_age_seconds;
        if (lastId > 0) {
            const ageText = (lastAge === null || lastAge === undefined) ? 'eta N/D' : ('eta ' + fmtDuration(lastAge) + ' fa');
            setText(elLastRecord, '#' + lastId + ' @ ' + fmtDate(data.latest_recorded_at) + ' (' + ageText + ')');
        } else {
            setText(elLastRecord, 'Nessun record nel range');
        }

        const pct = Math.max(0, Math.min(100, Number(data.completion_percent || 0)));
        if (elProgressBar) {
            elProgressBar.style.width = pct + '%';
            elProgressBar.textContent = pct.toFixed(1) + '%';
        }

        const ratePerMin = Number(data.record_rate_per_min || 0);
        const updatedStr = new Date().toLocaleTimeString();
        setText(elUpdatedAt, 'Ultimo refresh: ' + updatedStr + ' | ritmo: ' + ratePerMin.toFixed(2) + ' rec/min');
    }

    function renderError(message) {
        setText(elUpdatedAt, 'Errore refresh live: ' + message);
    }

    async function pollStatus() {
        const qs = new URLSearchParams({
            action: 'session_status',
            session_id: String(runningSessionId),
            master_id: String(selectedMasterId),
            target_valid: String(targetValid),
            _ts: String(Date.now())
        });
        try {
            const resp = await fetch('deltap_tests.php?' + qs.toString(), { cache: 'no-store' });
            if (!resp.ok) {
                renderError('HTTP ' + resp.status);
                return;
            }
            const data = await resp.json();
            if (!data || !data.ok) {
                renderError((data && data.error) ? data.error : 'risposta non valida');
                return;
            }
            renderStatus(data);
        } catch (e) {
            renderError('rete non disponibile');
        }
    }

    pollStatus();
    setInterval(pollStatus, 5000);
});
</script>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
