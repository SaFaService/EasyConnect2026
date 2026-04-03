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
$currentRole = (string)($_SESSION['user_role'] ?? '');
$isAdmin = ($currentRole === 'admin');
$canLifecycle = ecAuthCurrentUserCan($pdo, $currentUserId, 'serial_lifecycle');
$isIt = (string)($_SESSION['lang'] ?? 'it') === 'it';

function sdTxt(string $it, string $en): string {
    global $isIt;
    return $isIt ? $it : $en;
}

function sdTableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = ? LIMIT 1");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function sdColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ? AND column_name = ? LIMIT 1");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

function sdCanAccessMaster(PDO $pdo, int $masterId, int $userId, bool $isAdmin): bool {
    if ($isAdmin) return true;

    $hasBuilder = sdColumnExists($pdo, 'masters', 'builder_id');
    $sql = "SELECT 1 FROM masters WHERE id = ? AND (creator_id = ? OR owner_id = ? OR maintainer_id = ?" . ($hasBuilder ? " OR builder_id = ?" : "") . ") LIMIT 1";
    $params = [$masterId, $userId, $userId, $userId];
    if ($hasBuilder) {
        $params[] = $userId;
    }
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    return (bool)$stmt->fetchColumn();
}

function sdFormatBytes(?int $bytes): string {
    if ($bytes === null) {
        return '-';
    }
    if ($bytes < 1024) {
        return $bytes . ' B';
    }
    $kb = $bytes / 1024;
    if ($kb < 1024) {
        return number_format($kb, 2, '.', '') . ' KB';
    }
    $mb = $kb / 1024;
    if ($mb < 1024) {
        return number_format($mb, 2, '.', '') . ' MB';
    }
    $gb = $mb / 1024;
    return number_format($gb, 2, '.', '') . ' GB';
}

function sdFormatDuration(?int $seconds): string {
    if ($seconds === null) {
        return '-';
    }
    $d = intdiv($seconds, 86400);
    $h = intdiv($seconds % 86400, 3600);
    $m = intdiv($seconds % 3600, 60);
    $s = $seconds % 60;
    if ($d > 0) return sprintf('%dd %02dh %02dm', $d, $h, $m);
    return sprintf('%02dh %02dm %02ds', $h, $m, $s);
}

function sdFriendlyScheme(?string $scheme, bool $isIt): string {
    $s = strtolower(trim((string)$scheme));
    if ($s === 'v2_yyyymmttnnnn') {
        return $isIt ? 'Nuovo formato (YYYYMMTTNNNN)' : 'New format (YYYYMMTTNNNN)';
    }
    if ($s === 'legacy') {
        return $isIt ? 'Legacy (pre-standard)' : 'Legacy (pre-standard)';
    }
    if ($s === '') {
        return '-';
    }
    return $scheme;
}

function sdFlagPretty($value): string {
    global $isIt;
    if ($value === null || $value === '') {
        return '-';
    }
    return ((int)$value === 1) ? ($isIt ? 'SI' : 'YES') : ($isIt ? 'NO' : 'NO');
}

function sdDeviceModeLabel(string $productTypeCode, $deviceMode): string {
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

function sdDeviceModeOptions(string $productTypeCode): array {
    switch ($productTypeCode) {
        case '02':
            return [1 => 'Standalone', 2 => 'Rewamping'];
        case '03':
            return [1 => 'LUCE', 2 => 'UVC', 3 => 'ELETTROSTATICO', 4 => 'GAS', 5 => 'COMANDO'];
        case '04':
            return [1 => 'Temp/Humidity', 2 => 'Pressure', 3 => 'All'];
        case '05':
            return [1 => 'Immissione', 2 => 'Aspirazione'];
        default:
            return [];
    }
}

function sdNormalizePlantKind($value): string {
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['display', 'standalone', 'rewamping'], true) ? $normalized : '';
}

$statusReasons = [
    ['reason_code' => 'field_replaced', 'label_it' => 'Sostituzione in campo', 'label_en' => 'Field replacement', 'applies_to_status' => 'retired'],
    ['reason_code' => 'damaged', 'label_it' => 'Dismesso per guasto', 'label_en' => 'Dismissed due to fault', 'applies_to_status' => 'retired'],
    ['reason_code' => 'plant_dismission', 'label_it' => 'Impianto dismesso', 'label_en' => 'Plant decommissioned', 'applies_to_status' => 'retired'],
    ['reason_code' => 'master_replaced', 'label_it' => 'Sostituito da altro seriale', 'label_en' => 'Replaced by another serial', 'applies_to_status' => 'retired'],
    ['reason_code' => 'wrong_product_type', 'label_it' => 'Tipo prodotto errato', 'label_en' => 'Wrong product type', 'applies_to_status' => 'voided'],
    ['reason_code' => 'wrong_flashing', 'label_it' => 'Programmazione errata', 'label_en' => 'Wrong flashing', 'applies_to_status' => 'voided'],
];
if (sdTableExists($pdo, 'serial_status_reasons')) {
    try {
        $rows = $pdo->query("
            SELECT reason_code, label_it, label_en, applies_to_status
            FROM serial_status_reasons
            WHERE is_active = 1
            ORDER BY sort_order ASC, reason_code ASC
        ")->fetchAll();
        if (!empty($rows)) {
            $statusReasons = $rows;
        }
    } catch (Throwable $e) {
        // Fallback statico.
    }
}

$serial = trim((string)($_GET['serial'] ?? ''));
if ($serial === '') {
    http_response_code(400);
    echo sdTxt('Seriale mancante.', 'Missing serial.');
    exit;
}

if (!sdTableExists($pdo, 'device_serials')) {
    http_response_code(500);
    echo sdTxt('Tabella device_serials non disponibile.', 'device_serials table not available.');
    exit;
}

$message = '';
$messageType = 'info';
$backSteps = 1;

if ($_SERVER['REQUEST_METHOD'] === 'POST' && $canLifecycle) {
    $action = (string)($_POST['action'] ?? '');
    if ($action === 'activate_serial') {
        try {
            $pdo->beginTransaction();
            $stmtLock = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
            $stmtLock->execute([$serial]);
            $rowLock = $stmtLock->fetch();
            if (!$rowLock) {
                $pdo->rollBack();
                $message = sdTxt('Seriale non trovato.', 'Serial not found.');
                $messageType = 'danger';
            } else {
                $set = ["status = 'active'"];
                $params = [];
                if (sdColumnExists($pdo, 'device_serials', 'status_reason_code')) {
                    $set[] = "status_reason_code = 'master_bind'";
                }
                if (sdColumnExists($pdo, 'device_serials', 'status_notes')) {
                    $set[] = "status_notes = 'Attivazione manuale da pagina seriale'";
                }
                if (sdColumnExists($pdo, 'device_serials', 'status_changed_at')) {
                    $set[] = 'status_changed_at = NOW()';
                }
                if (sdColumnExists($pdo, 'device_serials', 'status_changed_by_user_id')) {
                    $set[] = 'status_changed_by_user_id = ?';
                    $params[] = $currentUserId;
                }
                if (sdColumnExists($pdo, 'device_serials', 'activated_at')) {
                    $set[] = 'activated_at = COALESCE(activated_at, NOW())';
                }
                if (sdColumnExists($pdo, 'device_serials', 'deactivated_at')) {
                    $set[] = 'deactivated_at = NULL';
                }
                if (sdColumnExists($pdo, 'device_serials', 'replaced_by_serial')) {
                    $set[] = 'replaced_by_serial = NULL';
                }
                $params[] = (int)$rowLock['id'];
                $sqlUpd = 'UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?';
                $pdo->prepare($sqlUpd)->execute($params);
                $pdo->commit();
                $message = 'Seriale impostato su ACTIVE.';
                $messageType = 'success';
                $backSteps = 2;
            }
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            $message = sdTxt('Errore attivazione: ', 'Activation error: ') . $e->getMessage();
            $messageType = 'danger';
        }
    } elseif ($action === 'update_device_mode' && $isAdmin) {
        if (!sdColumnExists($pdo, 'device_serials', 'device_mode')) {
            $message = sdTxt('Colonna device_mode non disponibile. Applica la migration SQL.', 'device_mode column not available. Apply the SQL migration.');
            $messageType = 'danger';
        } else {
            try {
                $pdo->beginTransaction();
                $stmtLock = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
                $stmtLock->execute([$serial]);
                $rowLock = $stmtLock->fetch();
                if (!$rowLock) {
                    $pdo->rollBack();
                    $message = sdTxt('Seriale non trovato.', 'Serial not found.');
                    $messageType = 'danger';
                } else {
                    $rawMode = trim((string)($_POST['device_mode'] ?? ''));
                    $allowedModes = sdDeviceModeOptions((string)($rowLock['product_type_code'] ?? ''));
                    $modeValue = null;
                    if ($rawMode !== '') {
                        $modeValue = (int)$rawMode;
                        if (!array_key_exists($modeValue, $allowedModes)) {
                            $pdo->rollBack();
                            $message = sdTxt('Modalita non valida per questo tipo di scheda.', 'Invalid mode for this board type.');
                            $messageType = 'danger';
                        }
                    }

                    if ($pdo->inTransaction()) {
                        $params = [$modeValue, (int)$rowLock['id']];
                        $set = ['device_mode = ?'];
                        if (sdColumnExists($pdo, 'device_serials', 'status_changed_at')) {
                            $set[] = 'status_changed_at = NOW()';
                        }
                        if (sdColumnExists($pdo, 'device_serials', 'status_changed_by_user_id')) {
                            $set[] = 'status_changed_by_user_id = ?';
                            $params = [$modeValue, $currentUserId, (int)$rowLock['id']];
                        }
                        $sqlUpd = 'UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?';
                        $pdo->prepare($sqlUpd)->execute($params);
                        $pdo->commit();
                        $message = sdTxt('Modalita seriale aggiornata.', 'Serial mode updated.');
                        $messageType = 'success';
                        $backSteps = 2;
                    }
                }
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $message = sdTxt('Errore aggiornamento modalita: ', 'Mode update error: ') . $e->getMessage();
                $messageType = 'danger';
            }
        }
    } elseif ($action === 'update_serial_firmware' && $isAdmin) {
        if (!sdColumnExists($pdo, 'device_serials', 'firmware_version')) {
            $message = sdTxt('Colonna firmware_version non disponibile. Applica la migration SQL.', 'firmware_version column not available. Apply the SQL migration.');
            $messageType = 'danger';
        } else {
            try {
                $pdo->beginTransaction();
                $stmtLock = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
                $stmtLock->execute([$serial]);
                $rowLock = $stmtLock->fetch();
                if (!$rowLock) {
                    $pdo->rollBack();
                    $message = sdTxt('Seriale non trovato.', 'Serial not found.');
                    $messageType = 'danger';
                } else {
                    $firmwareVersion = trim((string)($_POST['firmware_version'] ?? ''));
                    $params = [$firmwareVersion !== '' ? $firmwareVersion : null];
                    $set = ['firmware_version = ?'];
                    if (sdColumnExists($pdo, 'device_serials', 'status_changed_at')) {
                        $set[] = 'status_changed_at = NOW()';
                    }
                    if (sdColumnExists($pdo, 'device_serials', 'status_changed_by_user_id')) {
                        $set[] = 'status_changed_by_user_id = ?';
                        $params[] = $currentUserId;
                    }
                    $params[] = (int)$rowLock['id'];
                    $sqlUpd = 'UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?';
                    $pdo->prepare($sqlUpd)->execute($params);
                    $pdo->commit();
                    $message = sdTxt('Firmware seriale aggiornato.', 'Serial firmware updated.');
                    $messageType = 'success';
                    $backSteps = 2;
                }
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $message = sdTxt('Errore aggiornamento firmware: ', 'Firmware update error: ') . $e->getMessage();
                $messageType = 'danger';
            }
        }
    } elseif ($action === 'set_serial_status') {
        $targetStatus = strtolower(trim((string)($_POST['target_status'] ?? '')));
        $reasonCode = trim((string)($_POST['reason_code'] ?? ''));
        $reasonDetails = trim((string)($_POST['reason_details'] ?? ''));
        $replacedBy = trim((string)($_POST['replaced_by_serial'] ?? ''));

        if (!in_array($targetStatus, ['retired', 'voided'], true)) {
            $message = sdTxt('Stato non valido.', 'Invalid status.');
            $messageType = 'danger';
        } elseif ($reasonCode === '') {
            $message = sdTxt('Motivazione obbligatoria.', 'Reason is required.');
            $messageType = 'danger';
        } else {
            try {
                $pdo->beginTransaction();
                $stmtLock = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
                $stmtLock->execute([$serial]);
                $rowLock = $stmtLock->fetch();
                if (!$rowLock) {
                    $pdo->rollBack();
                    $message = sdTxt('Seriale non trovato.', 'Serial not found.');
                    $messageType = 'danger';
                } else {
                    $set = ["status = ?"];
                    $params = [$targetStatus];

                    if (sdColumnExists($pdo, 'device_serials', 'status_reason_code')) {
                        $set[] = 'status_reason_code = ?';
                        $params[] = $reasonCode;
                    }
                    if (sdColumnExists($pdo, 'device_serials', 'status_notes')) {
                        $set[] = 'status_notes = ?';
                        $params[] = $reasonDetails !== '' ? $reasonDetails : null;
                    }
                    if (sdColumnExists($pdo, 'device_serials', 'replaced_by_serial')) {
                        $set[] = 'replaced_by_serial = ?';
                        $params[] = $replacedBy !== '' ? $replacedBy : null;
                    }
                    if (sdColumnExists($pdo, 'device_serials', 'status_changed_at')) {
                        $set[] = 'status_changed_at = NOW()';
                    }
                    if (sdColumnExists($pdo, 'device_serials', 'status_changed_by_user_id')) {
                        $set[] = 'status_changed_by_user_id = ?';
                        $params[] = $currentUserId;
                    }
                    if (sdColumnExists($pdo, 'device_serials', 'deactivated_at')) {
                        $set[] = 'deactivated_at = COALESCE(deactivated_at, NOW())';
                    }

                    $params[] = (int)$rowLock['id'];
                    $sqlUpd = 'UPDATE device_serials SET ' . implode(', ', $set) . ' WHERE id = ?';
                    $pdo->prepare($sqlUpd)->execute($params);

                    if (sdTableExists($pdo, 'serial_lifecycle_events')) {
                        try {
                            $evt = $pdo->prepare("
                                INSERT INTO serial_lifecycle_events (
                                    serial_number, from_status, to_status, reason_code, reason_details,
                                    replaced_by_serial, actor_user_id, master_id
                                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                            ");
                            $evt->execute([
                                $serial,
                                $rowLock['status'] ?? null,
                                $targetStatus,
                                $reasonCode,
                                $reasonDetails !== '' ? $reasonDetails : null,
                                $replacedBy !== '' ? $replacedBy : null,
                                $currentUserId,
                                !empty($rowLock['assigned_master_id']) ? (int)$rowLock['assigned_master_id'] : null
                            ]);
                        } catch (Throwable $e) {
                            // Non bloccare aggiornamento principale.
                        }
                    }

                    $pdo->commit();
                    $message = sdTxt('Stato seriale aggiornato.', 'Serial status updated.');
                    $messageType = 'success';
                }
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $message = sdTxt('Errore aggiornamento stato: ', 'Status update error: ') . $e->getMessage();
                $messageType = 'danger';
            }
        }
    } elseif ($action === 'reset_master_stats') {
        try {
            if (!sdTableExists($pdo, 'master_stats_resets')) {
                $message = sdTxt('Tabella master_stats_resets non disponibile. Applica la migrazione SQL.', 'master_stats_resets table not available. Apply SQL migration.');
                $messageType = 'danger';
            } else {
                $stmtMasterBySn = $pdo->prepare("SELECT id FROM masters WHERE serial_number = ? LIMIT 1");
                $stmtMasterBySn->execute([$serial]);
                $masterIdForReset = (int)($stmtMasterBySn->fetchColumn() ?: 0);
                if ($masterIdForReset <= 0) {
                    $message = sdTxt('Reset disponibile solo su seriale Master.', 'Reset is available only for Master serials.');
                    $messageType = 'danger';
                } else {
                    if (!$isAdmin && !sdCanAccessMaster($pdo, $masterIdForReset, $currentUserId, $isAdmin)) {
                        $message = sdTxt('Permessi insufficienti per reset statistiche.', 'Insufficient permissions for stats reset.');
                        $messageType = 'danger';
                    } else {
                        $hasTxSession = sdColumnExists($pdo, 'measurements', 'api_tx_session_bytes');
                        $hasRxSession = sdColumnExists($pdo, 'measurements', 'api_rx_session_bytes');
                        $hasPostsSession = sdColumnExists($pdo, 'measurements', 'api_posts_session_count');

                        $baseTx = null;
                        $baseRx = null;
                        $basePosts = null;
                        $selCols = [];
                        if ($hasTxSession) $selCols[] = 'api_tx_session_bytes';
                        if ($hasRxSession) $selCols[] = 'api_rx_session_bytes';
                        if ($hasPostsSession) $selCols[] = 'api_posts_session_count';
                        if (!empty($selCols)) {
                            $stmtLatest = $pdo->prepare("
                                SELECT " . implode(', ', $selCols) . "
                                FROM measurements
                                WHERE master_id = ?
                                  AND slave_sn IS NULL
                                ORDER BY recorded_at DESC
                                LIMIT 1
                            ");
                            $stmtLatest->execute([$masterIdForReset]);
                            $latest = $stmtLatest->fetch();
                            if ($latest) {
                                if ($hasTxSession && isset($latest['api_tx_session_bytes'])) $baseTx = (int)$latest['api_tx_session_bytes'];
                                if ($hasRxSession && isset($latest['api_rx_session_bytes'])) $baseRx = (int)$latest['api_rx_session_bytes'];
                                if ($hasPostsSession && isset($latest['api_posts_session_count'])) $basePosts = (int)$latest['api_posts_session_count'];
                            }
                        }

                        $ins = $pdo->prepare("
                            INSERT INTO master_stats_resets (
                                master_id, reset_by_user_id, reset_at,
                                base_tx_session_bytes, base_rx_session_bytes, base_posts_session_count, notes
                            ) VALUES (?, ?, NOW(), ?, ?, ?, ?)
                        ");
                        $ins->execute([
                            $masterIdForReset,
                            $currentUserId,
                            $baseTx,
                            $baseRx,
                            $basePosts,
                            'Manual reset from serial_detail'
                        ]);

                        $message = sdTxt('Statistiche azzerate. I grafici ora partono dal nuovo riferimento.', 'Statistics reset. Counters now start from the new baseline.');
                        $messageType = 'success';
                    }
                }
            }
        } catch (Throwable $e) {
            $message = sdTxt('Errore reset statistiche: ', 'Stats reset error: ') . $e->getMessage();
            $messageType = 'danger';
        }
    }
}

$stmtSerial = $pdo->prepare("SELECT ds.*, pt.label AS product_label FROM device_serials ds LEFT JOIN product_types pt ON pt.code = ds.product_type_code WHERE ds.serial_number = ? LIMIT 1");
$stmtSerial->execute([$serial]);
$serialRow = $stmtSerial->fetch();
if (!$serialRow) {
    http_response_code(404);
    echo sdTxt('Seriale non trovato.', 'Serial not found.');
    exit;
}

$hasOwnerUser = sdColumnExists($pdo, 'device_serials', 'owner_user_id');
$hasAssignedUser = sdColumnExists($pdo, 'device_serials', 'assigned_user_id');

$accessAllowed = $isAdmin;
$assignedMasterId = (int)($serialRow['assigned_master_id'] ?? 0);
if (!$accessAllowed && $assignedMasterId > 0) {
    $accessAllowed = sdCanAccessMaster($pdo, $assignedMasterId, $currentUserId, $isAdmin);
}
if (!$accessAllowed && $hasOwnerUser && (int)($serialRow['owner_user_id'] ?? 0) === $currentUserId) {
    $accessAllowed = true;
}
if (!$accessAllowed && $hasAssignedUser && (int)($serialRow['assigned_user_id'] ?? 0) === $currentUserId) {
    $accessAllowed = true;
}
if (!$accessAllowed) {
    http_response_code(403);
    $subject = rawurlencode(sdTxt('Richiesta accesso seriale ', 'Serial access request ') . $serial);
    $body = rawurlencode(sdTxt(
        "Buongiorno,\nrichiedo l'accesso al seriale {$serial}.\nMotivazione: ",
        "Hello,\nI request access to serial {$serial}.\nReason: "
    ));
    ?>
    <!DOCTYPE html>
    <html lang="<?php echo $_SESSION['lang']; ?>">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title><?php echo htmlspecialchars(sdTxt('Accesso non consentito', 'Access denied')); ?> - Antralux</title>
        <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
        <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
        <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    </head>
    <body class="d-flex flex-column min-vh-100 bg-light">
    <?php require 'navbar.php'; ?>
    <div class="container flex-grow-1 d-flex align-items-center justify-content-center py-5">
        <div class="card shadow-sm border-0 text-center" style="max-width:760px;">
            <div class="card-body p-4 p-lg-5">
                <img src="assets/img/AntraluxCloud.png" alt="AntraluxCloud" style="max-width:220px;" class="mb-3">
                <h4 class="mb-2"><?php echo htmlspecialchars(sdTxt('Accesso non autorizzato', 'Access not authorized')); ?></h4>
                <p class="text-muted mb-3">
                    <?php echo htmlspecialchars(sdTxt(
                        'Il seriale richiesto non rientra tra quelli assegnati alla tua utenza.',
                        'The requested serial is not assigned to your account.'
                    )); ?>
                </p>
                <div class="alert alert-warning text-start small mb-4">
                    <div><strong><?php echo htmlspecialchars(sdTxt('Seriale richiesto:', 'Requested serial:')); ?></strong> <code><?php echo htmlspecialchars($serial); ?></code></div>
                    <div><?php echo htmlspecialchars(sdTxt(
                        'Se ritieni che l\'accesso sia necessario, contatta un amministratore.',
                        'If you believe access is needed, contact an administrator.'
                    )); ?></div>
                </div>
                <div class="d-flex flex-wrap gap-2 justify-content-center">
                    <a href="index.php" class="btn btn-outline-secondary">
                        <i class="fas fa-arrow-left"></i> <?php echo htmlspecialchars(sdTxt('Torna alla dashboard', 'Back to dashboard')); ?>
                    </a>
                    <a href="contact_form.php?subject=<?php echo $subject; ?>&body=<?php echo $body; ?>" class="btn btn-primary">
                        <i class="fas fa-envelope"></i> <?php echo htmlspecialchars(sdTxt('Richiedi accesso', 'Request access')); ?>
                    </a>
                </div>
            </div>
        </div>
    </div>
    <?php require 'footer.php'; ?>
    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
    </body>
    </html>
    <?php
    exit;
}

// Master associata, se disponibile.
$masterRow = null;
if ($assignedMasterId > 0) {
    $stmtMaster = $pdo->prepare("SELECT * FROM masters WHERE id = ? LIMIT 1");
    $stmtMaster->execute([$assignedMasterId]);
    $masterRow = $stmtMaster->fetch();
}

// Se seriale coincide con una master, privilegia il record master.
if (!$masterRow) {
    $stmtMasterBySn = $pdo->prepare("SELECT * FROM masters WHERE serial_number = ? LIMIT 1");
    $stmtMasterBySn->execute([$serial]);
    $masterBySn = $stmtMasterBySn->fetch();
    if ($masterBySn) {
        $masterRow = $masterBySn;
        $assignedMasterId = (int)$masterBySn['id'];
    }
}

$isMasterSerial = ($masterRow && ((string)$masterRow['serial_number'] === $serial));
$serialProductTypeCode = (string)($serialRow['product_type_code'] ?? '');
$masterDeviceMode = null;
if ($serialProductTypeCode === '02' && isset($serialRow['device_mode']) && $serialRow['device_mode'] !== null && $serialRow['device_mode'] !== '') {
    $masterDeviceMode = (int)$serialRow['device_mode'];
}
if ($serialProductTypeCode === '02' && $masterDeviceMode === null) {
    $masterPlantKind = sdNormalizePlantKind(is_array($masterRow) ? ($masterRow['plant_kind'] ?? '') : '');
    if ($masterPlantKind === 'standalone') {
        $masterDeviceMode = 1;
    } elseif ($masterPlantKind === 'rewamping') {
        $masterDeviceMode = 2;
    }
}
$showMasterDeltaPLog = false;
if ($isMasterSerial) {
    if ($serialProductTypeCode === '02') {
        // Compatibilita' storica: se la modalita non e' valorizzata, manteniamo visibile DeltaP.
        // In standalone (mode=1) viene sempre nascosto.
        $showMasterDeltaPLog = ($masterDeviceMode !== 1);
    } elseif ($serialProductTypeCode === '01') {
        // Futuro supporto centralina display.
        $showMasterDeltaPLog = true;
    }
}

$trafficStats = null;
$resourceStats = null;
$statsResetInfo = null;
if ($isMasterSerial && $assignedMasterId > 0) {
    $hasTxSession = sdColumnExists($pdo, 'measurements', 'api_tx_session_bytes');
    $hasRxSession = sdColumnExists($pdo, 'measurements', 'api_rx_session_bytes');
    $hasPostsSession = sdColumnExists($pdo, 'measurements', 'api_posts_session_count');
    $hasTxCycle = sdColumnExists($pdo, 'measurements', 'api_tx_cycle_bytes');
    $hasRxCycle = sdColumnExists($pdo, 'measurements', 'api_rx_cycle_bytes');

    if (sdTableExists($pdo, 'master_stats_resets')) {
        try {
            $stmtReset = $pdo->prepare("
                SELECT reset_at, base_tx_session_bytes, base_rx_session_bytes, base_posts_session_count
                FROM master_stats_resets
                WHERE master_id = ?
                ORDER BY id DESC
                LIMIT 1
            ");
            $stmtReset->execute([$assignedMasterId]);
            $statsResetInfo = $stmtReset->fetch() ?: null;
        } catch (Throwable $e) {
            $statsResetInfo = null;
        }
    }

    if ($hasTxSession || $hasRxSession || $hasPostsSession || $hasTxCycle || $hasRxCycle) {
        $trafficStats = [
            'session_total' => null,
            'session_tx' => null,
            'session_rx' => null,
            'session_posts' => null,
            'day_total' => null,
            'week_total' => null,
        ];

        $selLatest = [];
        if ($hasTxSession) $selLatest[] = 'api_tx_session_bytes';
        if ($hasRxSession) $selLatest[] = 'api_rx_session_bytes';
        if ($hasPostsSession) $selLatest[] = 'api_posts_session_count';
        if (!empty($selLatest)) {
            $stmtTrafficLatest = $pdo->prepare("
                SELECT " . implode(', ', $selLatest) . "
                FROM measurements
                WHERE master_id = ?
                  AND slave_sn IS NULL
                ORDER BY recorded_at DESC
                LIMIT 1
            ");
            $stmtTrafficLatest->execute([$assignedMasterId]);
            $latestTraffic = $stmtTrafficLatest->fetch();
            if ($latestTraffic) {
                $tx = isset($latestTraffic['api_tx_session_bytes']) ? (int)$latestTraffic['api_tx_session_bytes'] : null;
                $rx = isset($latestTraffic['api_rx_session_bytes']) ? (int)$latestTraffic['api_rx_session_bytes'] : null;

                if ($statsResetInfo) {
                    $baseTx = isset($statsResetInfo['base_tx_session_bytes']) ? (int)$statsResetInfo['base_tx_session_bytes'] : null;
                    $baseRx = isset($statsResetInfo['base_rx_session_bytes']) ? (int)$statsResetInfo['base_rx_session_bytes'] : null;
                    if ($tx !== null && $baseTx !== null) $tx = max(0, $tx - $baseTx);
                    if ($rx !== null && $baseRx !== null) $rx = max(0, $rx - $baseRx);
                }

                $trafficStats['session_tx'] = $tx;
                $trafficStats['session_rx'] = $rx;
                if ($tx !== null || $rx !== null) {
                    $trafficStats['session_total'] = (int)($tx ?? 0) + (int)($rx ?? 0);
                }
                if (isset($latestTraffic['api_posts_session_count'])) {
                    $posts = (int)$latestTraffic['api_posts_session_count'];
                    if ($statsResetInfo) {
                        $basePosts = isset($statsResetInfo['base_posts_session_count']) ? (int)$statsResetInfo['base_posts_session_count'] : null;
                        if ($basePosts !== null) $posts = max(0, $posts - $basePosts);
                    }
                    $trafficStats['session_posts'] = $posts;
                }
            }
        }

        if ($hasTxCycle || $hasRxCycle) {
            $partsDay = [];
            $partsWeek = [];
            if ($hasTxCycle) {
                $partsDay[] = 'COALESCE(SUM(api_tx_cycle_bytes),0)';
                $partsWeek[] = 'COALESCE(SUM(api_tx_cycle_bytes),0)';
            }
            if ($hasRxCycle) {
                $partsDay[] = 'COALESCE(SUM(api_rx_cycle_bytes),0)';
                $partsWeek[] = 'COALESCE(SUM(api_rx_cycle_bytes),0)';
            }

            $sqlDay = "SELECT (" . implode(' + ', $partsDay) . ") AS total_bytes
                       FROM measurements
                       WHERE master_id = ?
                         AND slave_sn IS NULL
                         AND recorded_at >= " . ($statsResetInfo ? "GREATEST((NOW() - INTERVAL 1 DAY), ?)" : "(NOW() - INTERVAL 1 DAY)");
            $stmtTrafficDay = $pdo->prepare($sqlDay);
            $paramsDay = [$assignedMasterId];
            if ($statsResetInfo) $paramsDay[] = (string)$statsResetInfo['reset_at'];
            $stmtTrafficDay->execute($paramsDay);
            $trafficStats['day_total'] = (int)($stmtTrafficDay->fetchColumn() ?? 0);

            $sqlWeek = "SELECT (" . implode(' + ', $partsWeek) . ") AS total_bytes
                        FROM measurements
                        WHERE master_id = ?
                          AND slave_sn IS NULL
                          AND recorded_at >= " . ($statsResetInfo ? "GREATEST((NOW() - INTERVAL 7 DAY), ?)" : "(NOW() - INTERVAL 7 DAY)");
            $stmtTrafficWeek = $pdo->prepare($sqlWeek);
            $paramsWeek = [$assignedMasterId];
            if ($statsResetInfo) $paramsWeek[] = (string)$statsResetInfo['reset_at'];
            $stmtTrafficWeek->execute($paramsWeek);
            $trafficStats['week_total'] = (int)($stmtTrafficWeek->fetchColumn() ?? 0);
        }
    }

    $hasResUptime = sdColumnExists($pdo, 'measurements', 'uptime_seconds');
    $hasResCpuMhz = sdColumnExists($pdo, 'measurements', 'cpu_mhz');
    $hasResHeapFree = sdColumnExists($pdo, 'measurements', 'heap_free_bytes');
    $hasResHeapMin = sdColumnExists($pdo, 'measurements', 'heap_min_bytes');
    $hasResHeapTotal = sdColumnExists($pdo, 'measurements', 'heap_total_bytes');
    $hasResSketchUsed = sdColumnExists($pdo, 'measurements', 'sketch_used_bytes');
    $hasResSketchFree = sdColumnExists($pdo, 'measurements', 'sketch_free_bytes');

    if ($hasResUptime || $hasResCpuMhz || $hasResHeapFree || $hasResHeapMin || $hasResHeapTotal || $hasResSketchUsed || $hasResSketchFree) {
        $resourceStats = [
            'latest' => null,
            'day_min_heap_free' => null,
            'week_min_heap_free' => null,
        ];

        $resCols = [];
        if ($hasResUptime) $resCols[] = 'uptime_seconds';
        if ($hasResCpuMhz) $resCols[] = 'cpu_mhz';
        if ($hasResHeapFree) $resCols[] = 'heap_free_bytes';
        if ($hasResHeapMin) $resCols[] = 'heap_min_bytes';
        if ($hasResHeapTotal) $resCols[] = 'heap_total_bytes';
        if ($hasResSketchUsed) $resCols[] = 'sketch_used_bytes';
        if ($hasResSketchFree) $resCols[] = 'sketch_free_bytes';

        $stmtResLatest = $pdo->prepare("
            SELECT " . implode(', ', $resCols) . "
            FROM measurements
            WHERE master_id = ?
              AND slave_sn IS NULL
            ORDER BY recorded_at DESC
            LIMIT 1
        ");
        $stmtResLatest->execute([$assignedMasterId]);
        $latestRes = $stmtResLatest->fetch();
        if ($latestRes) {
            $resourceStats['latest'] = $latestRes;
        }

        if ($hasResHeapFree) {
            $stmtMinDay = $pdo->prepare("
                SELECT MIN(heap_free_bytes)
                FROM measurements
                WHERE master_id = ?
                  AND slave_sn IS NULL
                  AND heap_free_bytes IS NOT NULL
                  AND recorded_at >= (NOW() - INTERVAL 1 DAY)
            ");
            $stmtMinDay->execute([$assignedMasterId]);
            $resourceStats['day_min_heap_free'] = $stmtMinDay->fetchColumn();

            $stmtMinWeek = $pdo->prepare("
                SELECT MIN(heap_free_bytes)
                FROM measurements
                WHERE master_id = ?
                  AND slave_sn IS NULL
                  AND heap_free_bytes IS NOT NULL
                  AND recorded_at >= (NOW() - INTERVAL 7 DAY)
            ");
            $stmtMinWeek->execute([$assignedMasterId]);
            $resourceStats['week_min_heap_free'] = $stmtMinWeek->fetchColumn();
        }
    }
}

$latestSlaveData = null;
$recentData = [];
if ($isMasterSerial) {
    $recentMasterCols = [
        'recorded_at',
        'slave_sn',
        'slave_grp',
        'pressure',
        'temperature',
        'fw_version',
    ];
    $recentMasterCols[] = ($showMasterDeltaPLog && sdColumnExists($pdo, 'measurements', 'delta_p'))
        ? 'delta_p'
        : 'NULL AS delta_p';
    foreach (['relay_mode', 'relay_on', 'relay_state', 'relay_safety_closed', 'relay_feedback_ok', 'relay_hours_remaining', 'relay_starts'] as $relayCol) {
        $recentMasterCols[] = sdColumnExists($pdo, 'measurements', $relayCol)
            ? $relayCol
            : ('NULL AS ' . $relayCol);
    }
    $stmtRecent = $pdo->prepare("SELECT " . implode(', ', $recentMasterCols) . " FROM measurements WHERE master_id = ? ORDER BY recorded_at DESC LIMIT 20");
    $stmtRecent->execute([$assignedMasterId]);
    $recentData = $stmtRecent->fetchAll();
} else {
    $stmtLatestSlave = $pdo->prepare("SELECT * FROM measurements WHERE slave_sn = ? ORDER BY recorded_at DESC LIMIT 1");
    $stmtLatestSlave->execute([$serial]);
    $latestSlaveData = $stmtLatestSlave->fetch();

    $recentSlaveCols = ['recorded_at', 'slave_grp', 'pressure', 'temperature', 'fw_version', 'master_id'];
    foreach (['relay_mode', 'relay_on', 'relay_state', 'relay_safety_closed', 'relay_feedback_ok', 'relay_hours_remaining', 'relay_starts'] as $relayCol) {
        if (sdColumnExists($pdo, 'measurements', $relayCol)) {
            $recentSlaveCols[] = $relayCol;
        }
    }
    $stmtRecent = $pdo->prepare("SELECT " . implode(', ', $recentSlaveCols) . " FROM measurements WHERE slave_sn = ? ORDER BY recorded_at DESC LIMIT 20");
    $stmtRecent->execute([$serial]);
    $recentData = $stmtRecent->fetchAll();

    if (!$masterRow && $latestSlaveData && !empty($latestSlaveData['master_id'])) {
        $stmtMasterFromMeas = $pdo->prepare("SELECT * FROM masters WHERE id = ? LIMIT 1");
        $stmtMasterFromMeas->execute([(int)$latestSlaveData['master_id']]);
        $masterRow = $stmtMasterFromMeas->fetch();
        $assignedMasterId = (int)($masterRow['id'] ?? 0);
    }
}

$connected = false;
$lastSeenLabel = '-';
if ($isMasterSerial && $masterRow) {
    if (!empty($masterRow['last_seen'])) {
        $lastSeenTs = strtotime((string)$masterRow['last_seen']);
        $connected = ($lastSeenTs !== false && $lastSeenTs > (time() - 120));
        $lastSeenLabel = (string)$masterRow['last_seen'];
    }
} elseif ($latestSlaveData && !empty($latestSlaveData['recorded_at'])) {
    $lastSeenTs = strtotime((string)$latestSlaveData['recorded_at']);
    $connected = ($lastSeenTs !== false && $lastSeenTs > (time() - 180));
    $lastSeenLabel = (string)$latestSlaveData['recorded_at'];
}

$lifecycleEvents = [];
if (sdTableExists($pdo, 'serial_lifecycle_events')) {
    $stmtEvt = $pdo->prepare("SELECT created_at, from_status, to_status, reason_code, reason_details, replaced_by_serial, actor_user_id, master_id FROM serial_lifecycle_events WHERE serial_number = ? ORDER BY id DESC LIMIT 20");
    $stmtEvt->execute([$serial]);
    $lifecycleEvents = $stmtEvt->fetchAll();
}

$serialAuditRows = [];
if (sdTableExists($pdo, 'serial_audit_logs')) {
    try {
        $stmtAudit = $pdo->prepare("
            SELECT sal.created_at, sal.action, sal.master_id, sal.details, u.email AS actor_email
            FROM serial_audit_logs sal
            LEFT JOIN users u ON u.id = sal.actor_user_id
            WHERE sal.serial_number = ?
            ORDER BY sal.id DESC
            LIMIT 20
        ");
        $stmtAudit->execute([$serial]);
        $serialAuditRows = $stmtAudit->fetchAll();
    } catch (Throwable $e) {
        $serialAuditRows = [];
    }
}

$linkedPlantLabel = '-';
$linkedPlantUrl = '';
if ($masterRow) {
    $linkedPlantLabel = '#' . (int)($masterRow['id'] ?? 0) . ' - ' . trim((string)($masterRow['nickname'] ?? 'Impianto'));
    if (!empty($masterRow['id'])) {
        $linkedPlantUrl = 'plant_detail.php?plant_id=' . (int)$masterRow['id'];
    }
}

$reasonLabelMap = [];
foreach ($statusReasons as $sr) {
    $reasonLabelMap[(string)($sr['reason_code'] ?? '')] = $isIt ? (string)($sr['label_it'] ?? '') : (string)($sr['label_en'] ?? '');
}
$serialReasonCode = trim((string)($serialRow['status_reason_code'] ?? ''));
$serialReasonLabel = $serialReasonCode !== '' ? ($reasonLabelMap[$serialReasonCode] ?? '') : '';
$serialReasonTitle = $serialReasonLabel !== ''
    ? $serialReasonLabel
    : ($serialReasonCode !== '' ? str_replace('_', ' ', $serialReasonCode) : sdTxt('Nessuna motivazione registrata', 'No reason recorded'));
$serialReasonHintMapIt = [
    'master_bind' => 'Il seriale risulta correttamente associato all impianto.',
    'field_replaced' => 'Scheda sostituita in campo: non e piu prevista in esercizio.',
    'damaged' => 'Scheda guasta o non affidabile: da sostituire.',
    'plant_dismission' => 'Seriale dismesso per chiusura o dismissione impianto.',
    'master_replaced' => 'Seriale sostituito da una nuova scheda.',
    'wrong_product_type' => 'Annullato per errore di tipo prodotto in anagrafica.',
    'wrong_flashing' => 'Annullato per programmazione firmware non corretta.',
    'factory_test_discard' => 'Annullato dopo collaudo di fabbrica.',
];
$serialReasonHintMapEn = [
    'master_bind' => 'The serial is correctly linked to the plant.',
    'field_replaced' => 'Board replaced in the field and no longer in service.',
    'damaged' => 'Board marked faulty or unreliable and to be replaced.',
    'plant_dismission' => 'Serial retired because the plant is decommissioned.',
    'master_replaced' => 'Serial replaced by a newer board.',
    'wrong_product_type' => 'Voided due to wrong product type registry.',
    'wrong_flashing' => 'Voided due to wrong firmware programming.',
    'factory_test_discard' => 'Voided after factory testing.',
];
$serialReasonHint = '-';
if ($serialReasonCode !== '') {
    $serialReasonHint = $isIt
        ? ($serialReasonHintMapIt[$serialReasonCode] ?? 'Motivazione registrata in fase di gestione seriale.')
        : ($serialReasonHintMapEn[$serialReasonCode] ?? 'Reason stored during serial lifecycle management.');
} else {
    $serialReasonHint = sdTxt('Nessuna motivazione specifica salvata per questo seriale.', 'No specific reason was saved for this serial.');
}
$serialStatusNotes = trim((string)($serialRow['status_notes'] ?? ''));
$serialSchemePretty = sdFriendlyScheme((string)($serialRow['serial_scheme'] ?? ''), $isIt);
$serialModePretty = sdDeviceModeLabel((string)($serialRow['product_type_code'] ?? ''), $serialRow['device_mode'] ?? null);
$serialModeOptions = sdDeviceModeOptions((string)($serialRow['product_type_code'] ?? ''));
$serialStoredFirmware = sdColumnExists($pdo, 'device_serials', 'firmware_version')
    ? trim((string)($serialRow['firmware_version'] ?? ''))
    : '';

$isRelayBoard = ($serialProductTypeCode === '03');
$isPressureBoard = ($serialProductTypeCode === '04');
$isSerialActive = (strtolower(trim((string)($serialRow['status'] ?? ''))) === 'active');
$relayModeCurrent = null;
if (isset($serialRow['device_mode']) && $serialRow['device_mode'] !== null && $serialRow['device_mode'] !== '') {
    $relayModeCurrent = (int)$serialRow['device_mode'];
} elseif ($latestSlaveData && array_key_exists('relay_mode', $latestSlaveData) && $latestSlaveData['relay_mode'] !== null && $latestSlaveData['relay_mode'] !== '') {
    $relayModeCurrent = (int)$latestSlaveData['relay_mode'];
}
$relayModePretty = $isRelayBoard ? sdDeviceModeLabel('03', $relayModeCurrent) : '-';
$recentDataColspan = $showMasterDeltaPLog ? 7 : 10;
if (!$isMasterSerial) {
    if ($isRelayBoard) {
        $recentDataColspan = 10;
    } elseif ($isPressureBoard) {
        $recentDataColspan = 6;
    } else {
        $recentDataColspan = 4;
    }
}
$canRemoteCfg = $canLifecycle && $isPressureBoard;
$hasSlaveIdCol = sdColumnExists($pdo, 'measurements', 'slave_id');
$currentSlaveIp = null;
if ($hasSlaveIdCol && $latestSlaveData && array_key_exists('slave_id', $latestSlaveData) && $latestSlaveData['slave_id'] !== null) {
    $currentSlaveIp = (int)$latestSlaveData['slave_id'];
}
$currentSlaveGrp = $latestSlaveData ? (int)($latestSlaveData['slave_grp'] ?? 0) : null;
$masterTypeCode = '';
if ($masterRow && preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', (string)($masterRow['serial_number'] ?? ''), $mType)) {
    $masterTypeCode = (string)$mType[1];
}
$modeRestrictionText = '-';
if ($masterTypeCode === '02') {
    $modeRestrictionText = sdTxt('Master Rewamping: consentite solo modalità 2 o 3', 'Rewamping master: only mode 2 or 3 allowed');
} elseif ($masterTypeCode === '01') {
    $modeRestrictionText = sdTxt('Master Display: modalità 1/2/3 consentite', 'Display master: mode 1/2/3 allowed');
}
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo htmlspecialchars(sdTxt('Seriale ', 'Serial ')); ?><?php echo htmlspecialchars($serial); ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body class="d-flex flex-column min-vh-100 bg-light">
<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="d-flex justify-content-between align-items-center mb-3">
        <h5 class="mb-0">
            <i class="fas fa-microchip"></i>
            <?php echo htmlspecialchars(sdTxt('Dettaglio Seriale:', 'Serial Detail:')); ?>
            <code id="serialCodeValue"><?php echo htmlspecialchars($serial); ?></code>
            <button type="button" class="btn btn-outline-secondary btn-sm ms-1" onclick="copySerialCode()">
                <i class="fas fa-copy"></i> <?php echo htmlspecialchars(sdTxt('Copia', 'Copy')); ?>
            </button>
        </h5>
        <button type="button" class="btn btn-outline-secondary btn-sm" onclick="goBackWithFallback(<?php echo (int)$backSteps; ?>)">
            <i class="fas fa-arrow-left"></i> <?php echo htmlspecialchars(sdTxt('Indietro', 'Back')); ?>
        </button>
    </div>

    <?php if ($message !== ''): ?>
        <div class="alert alert-<?php echo htmlspecialchars($messageType); ?> py-2"><?php echo htmlspecialchars($message); ?></div>
    <?php endif; ?>

    <div class="row g-3">
        <div class="col-lg-6">
            <div class="card shadow-sm">
                <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Stato attuale', 'Current status')); ?></strong></div>
                <div class="card-body">
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Tipo:', 'Type:')); ?></strong> <?php echo htmlspecialchars((string)($serialRow['product_type_code'] ?? '')); ?><?php if (!empty($serialRow['product_label'])): ?> - <?php echo htmlspecialchars((string)$serialRow['product_label']); ?><?php endif; ?></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Stato seriale:', 'Serial status:')); ?></strong> <span class="badge <?php echo ((string)($serialRow['status'] ?? '') === 'active') ? 'bg-success' : 'bg-secondary'; ?>"><?php echo htmlspecialchars((string)($serialRow['status'] ?? 'n/d')); ?></span></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Connesso:', 'Connected:')); ?></strong> <?php echo $connected ? '<span class="text-success">' . htmlspecialchars(sdTxt('SI', 'YES')) . '</span>' : '<span class="text-danger">' . htmlspecialchars(sdTxt('NO', 'NO')) . '</span>'; ?></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Ultimo dato:', 'Last data:')); ?></strong> <?php echo htmlspecialchars($lastSeenLabel); ?></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Master associata:', 'Linked master:')); ?></strong>
                        <?php if ($masterRow): ?>
                            #<?php echo (int)$masterRow['id']; ?> - <?php echo htmlspecialchars((string)$masterRow['nickname']); ?>
                            (<a href="serial_detail.php?serial=<?php echo urlencode((string)$masterRow['serial_number']); ?>" class="text-decoration-none"><code><?php echo htmlspecialchars((string)$masterRow['serial_number']); ?></code></a>)
                        <?php else: ?>
                            <span class="text-muted"><?php echo htmlspecialchars(sdTxt('Non associata', 'Not linked')); ?></span>
                        <?php endif; ?>
                    </div>
                    <?php if (!$isMasterSerial && $latestSlaveData): ?>
                        <hr>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Gruppo:', 'Group:')); ?></strong> <?php echo htmlspecialchars((string)($latestSlaveData['slave_grp'] ?? '-')); ?></div>
                        <?php if ($isPressureBoard): ?>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Pressione:', 'Pressure:')); ?></strong> <?php echo htmlspecialchars((string)($latestSlaveData['pressure'] ?? '-')); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Temperatura:', 'Temperature:')); ?></strong> <?php echo htmlspecialchars((string)($latestSlaveData['temperature'] ?? '-')); ?></div>
                        <?php elseif ($isRelayBoard): ?>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Modalita relay:', 'Relay mode:')); ?></strong> <?php echo htmlspecialchars($relayModePretty); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Stato relay:', 'Relay state:')); ?></strong> <?php echo htmlspecialchars((string)($latestSlaveData['relay_state'] ?? '-')); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Relay ON:', 'Relay ON:')); ?></strong> <?php echo htmlspecialchars(sdFlagPretty($latestSlaveData['relay_on'] ?? null)); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Sicurezza chiusa:', 'Safety closed:')); ?></strong> <?php echo htmlspecialchars(sdFlagPretty($latestSlaveData['relay_safety_closed'] ?? null)); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Feedback OK:', 'Feedback OK:')); ?></strong> <?php echo htmlspecialchars(sdFlagPretty($latestSlaveData['relay_feedback_ok'] ?? null)); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Ore residue:', 'Hours remaining:')); ?></strong> <?php echo htmlspecialchars((string)($latestSlaveData['relay_hours_remaining'] ?? '-')); ?></div>
                        <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Avvii:', 'Starts:')); ?></strong> <?php echo htmlspecialchars((string)($latestSlaveData['relay_starts'] ?? '-')); ?></div>
                        <?php endif; ?>
                        <div class="mb-1"><strong>FW:</strong> <?php echo htmlspecialchars((string)($latestSlaveData['fw_version'] ?? '-')); ?></div>
                    <?php endif; ?>
                </div>
            </div>
        </div>

        <div class="col-lg-6">
            <div class="card shadow-sm">
                <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Metadati seriale', 'Serial metadata')); ?></strong></div>
                <div class="card-body">
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Creato:', 'Created:')); ?></strong> <?php echo htmlspecialchars((string)($serialRow['created_at'] ?? '-')); ?></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Formato seriale:', 'Serial format:')); ?></strong> <?php echo htmlspecialchars($serialSchemePretty); ?></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Impianto associato:', 'Linked plant:')); ?></strong>
                        <?php if ($linkedPlantUrl !== ''): ?>
                            <a href="<?php echo htmlspecialchars($linkedPlantUrl); ?>" class="text-decoration-none"><?php echo htmlspecialchars($linkedPlantLabel); ?></a>
                        <?php else: ?>
                            <?php echo htmlspecialchars($linkedPlantLabel); ?>
                        <?php endif; ?>
                    </div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Modalita:', 'Mode:')); ?></strong> <?php echo htmlspecialchars($serialModePretty); ?></div>
                    <?php if ($isAdmin && sdColumnExists($pdo, 'device_serials', 'device_mode') && !empty($serialModeOptions)): ?>
                    <form method="POST" class="row g-2 align-items-end mt-2">
                        <input type="hidden" name="action" value="update_device_mode">
                        <div class="col-sm-8">
                            <label class="form-label form-label-sm mb-1"><?php echo htmlspecialchars(sdTxt('Modifica modalita', 'Edit mode')); ?></label>
                            <select name="device_mode" class="form-select form-select-sm">
                                <option value=""><?php echo htmlspecialchars(sdTxt('Non specificata', 'Not set')); ?></option>
                                <?php foreach ($serialModeOptions as $modeCode => $modeLabel): ?>
                                    <option value="<?php echo (int)$modeCode; ?>" <?php echo ((int)($serialRow['device_mode'] ?? 0) === (int)$modeCode) ? 'selected' : ''; ?>>
                                        <?php echo htmlspecialchars((string)$modeCode . ' - ' . (string)$modeLabel); ?>
                                    </option>
                                <?php endforeach; ?>
                            </select>
                        </div>
                        <div class="col-sm-4 d-grid">
                            <button type="submit" class="btn btn-outline-primary btn-sm">
                                <i class="fas fa-pen"></i> <?php echo htmlspecialchars(sdTxt('Salva', 'Save')); ?>
                            </button>
                        </div>
                    </form>
                    <?php endif; ?>
                    <?php if ($isAdmin && sdColumnExists($pdo, 'device_serials', 'firmware_version')): ?>
                    <form method="POST" class="row g-2 align-items-end mt-2">
                        <input type="hidden" name="action" value="update_serial_firmware">
                        <div class="col-sm-8">
                            <label class="form-label form-label-sm mb-1"><?php echo htmlspecialchars(sdTxt('Firmware anagrafica (opzionale)', 'Registry firmware (optional)')); ?></label>
                            <input type="text" name="firmware_version" class="form-control form-control-sm" value="<?php echo htmlspecialchars($serialStoredFirmware); ?>" placeholder="Es. 1.2.3">
                        </div>
                        <div class="col-sm-4 d-grid">
                            <button type="submit" class="btn btn-outline-primary btn-sm">
                                <i class="fas fa-pen"></i> <?php echo htmlspecialchars(sdTxt('Salva', 'Save')); ?>
                            </button>
                        </div>
                    </form>
                    <?php endif; ?>
                    <div class="mb-1"><strong>Lock:</strong> <?php echo ((int)($serialRow['serial_locked'] ?? 0) === 1) ? htmlspecialchars(sdTxt('SI', 'YES')) : htmlspecialchars(sdTxt('NO', 'NO')); ?></div>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Motivo stato:', 'Status reason:')); ?></strong> <?php echo htmlspecialchars($serialReasonTitle); ?></div>
                    <div class="small text-muted mb-1"><?php echo htmlspecialchars($serialReasonHint); ?></div>
                    <?php if ($serialStatusNotes !== ''): ?>
                    <div class="small mb-1"><strong><?php echo htmlspecialchars(sdTxt('Dettaglio registrato:', 'Recorded detail:')); ?></strong> <?php echo htmlspecialchars($serialStatusNotes); ?></div>
                    <?php endif; ?>
                    <div class="mb-1"><strong><?php echo htmlspecialchars(sdTxt('Sostituito da:', 'Replaced by:')); ?></strong>
                        <?php
                            $repl = trim((string)($serialRow['replaced_by_serial'] ?? ''));
                            if ($repl !== ''):
                        ?>
                            <a href="serial_detail.php?serial=<?php echo urlencode($repl); ?>" class="text-decoration-none"><code><?php echo htmlspecialchars($repl); ?></code></a>
                        <?php else: ?>
                            -
                        <?php endif; ?>
                    </div>

                    <?php if ($canLifecycle): ?>
                    <hr>
                    <div class="d-flex flex-wrap gap-2 align-items-center">
                        <form method="POST" class="m-0" onsubmit="return confirm('<?php echo htmlspecialchars(sdTxt('Confermi attivazione manuale del seriale?', 'Confirm manual serial activation?')); ?>');">
                            <input type="hidden" name="action" value="activate_serial">
                            <button type="submit" class="btn <?php echo $isSerialActive ? 'btn-secondary' : 'btn-success'; ?> btn-sm" <?php echo $isSerialActive ? 'disabled' : ''; ?>>
                                <i class="fas fa-bolt"></i> <?php echo htmlspecialchars(sdTxt('Imposta ACTIVE manualmente', 'Set ACTIVE manually')); ?>
                            </button>
                        </form>
                        <button type="button" class="btn btn-danger btn-sm" data-bs-toggle="modal" data-bs-target="#serialLifecycleModal">
                            <i class="fas fa-power-off"></i> <?php echo htmlspecialchars(sdTxt('Aggiorna stato seriale', 'Update serial status')); ?>
                        </button>
                    </div>
                    <?php endif; ?>
                </div>
            </div>
        </div>
    </div>

    <?php if ($canRemoteCfg): ?>
    <div class="card shadow-sm mt-3">
        <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Impostazioni scheda pressione', 'Pressure board settings')); ?></strong></div>
        <div class="card-body">
            <div class="row g-2 mb-3">
                <div class="col-md-4">
                    <div><strong><?php echo htmlspecialchars(sdTxt('IP RS485 corrente:', 'Current RS485 IP:')); ?></strong> <?php echo htmlspecialchars($currentSlaveIp !== null ? (string)$currentSlaveIp : '-'); ?></div>
                </div>
                <div class="col-md-4">
                    <div><strong><?php echo htmlspecialchars(sdTxt('Gruppo corrente:', 'Current group:')); ?></strong> <?php echo htmlspecialchars($currentSlaveGrp !== null ? (string)$currentSlaveGrp : '-'); ?></div>
                </div>
                <div class="col-md-4">
                    <div><strong><?php echo htmlspecialchars(sdTxt('Vincoli modalità:', 'Mode rules:')); ?></strong> <?php echo htmlspecialchars($modeRestrictionText); ?></div>
                </div>
            </div>
            <div class="alert alert-warning py-2 small">
                <?php echo htmlspecialchars(sdTxt(
                    'Operazione sensibile: un cambio errato di modalità/gruppo/IP può compromettere l\'impianto.',
                    'Sensitive operation: wrong mode/group/IP changes can affect plant operation.'
                )); ?>
            </div>
            <?php if (!$connected || !$masterRow): ?>
                <div class="text-muted"><?php echo htmlspecialchars(sdTxt('Configurazione remota disponibile solo con scheda online e master associata.', 'Remote configuration is available only when the board is online and linked to a master.')); ?></div>
            <?php else: ?>
                <form id="pressureConfigForm">
                    <input type="hidden" name="serial_number" value="<?php echo htmlspecialchars($serial); ?>">
                    <input type="hidden" name="master_id" value="<?php echo (int)$assignedMasterId; ?>">
                    <div class="row g-2">
                        <div class="col-md-4">
                            <label class="form-label"><?php echo htmlspecialchars(sdTxt('Nuova modalità (opzionale)', 'New mode (optional)')); ?></label>
                            <select class="form-select form-select-sm" name="new_mode">
                                <option value=""><?php echo htmlspecialchars(sdTxt('Nessuna modifica', 'No change')); ?></option>
                                <option value="1">1 - <?php echo htmlspecialchars(sdTxt('Solo Temp/Umidità', 'Temp/Humidity only')); ?></option>
                                <option value="2">2 - <?php echo htmlspecialchars(sdTxt('Solo Pressione', 'Pressure only')); ?></option>
                                <option value="3">3 - <?php echo htmlspecialchars(sdTxt('Pressione + Temp/Umidità', 'Pressure + Temp/Humidity')); ?></option>
                            </select>
                        </div>
                        <div class="col-md-4">
                            <label class="form-label"><?php echo htmlspecialchars(sdTxt('Nuovo gruppo (opzionale)', 'New group (optional)')); ?></label>
                            <input type="number" class="form-control form-control-sm" name="new_group" min="1" max="100" placeholder="<?php echo htmlspecialchars(sdTxt('es. 2', 'e.g. 2')); ?>">
                        </div>
                        <div class="col-md-4">
                            <label class="form-label"><?php echo htmlspecialchars(sdTxt('Nuovo IP RS485 (opzionale)', 'New RS485 IP (optional)')); ?></label>
                            <input type="number" class="form-control form-control-sm" name="new_ip" min="1" max="30" placeholder="<?php echo htmlspecialchars(sdTxt('es. 7', 'e.g. 7')); ?>">
                        </div>
                    </div>
                    <button type="submit" class="btn btn-primary btn-sm mt-3">
                        <i class="fas fa-sliders"></i> <?php echo htmlspecialchars(sdTxt('Invia configurazione remota', 'Send remote configuration')); ?>
                    </button>
                    <div id="pressureConfigResult" class="small mt-2 text-muted"></div>
                </form>
            <?php endif; ?>
        </div>
    </div>
    <?php endif; ?>

    <?php if ($isMasterSerial): ?>
    <div class="card shadow-sm mt-3">
        <div class="card-header d-flex justify-content-between align-items-center">
            <strong><?php echo htmlspecialchars(sdTxt('Statistiche traffico', 'Traffic statistics')); ?></strong>
            <?php if ($canLifecycle): ?>
            <form method="POST" class="m-0" onsubmit="return confirm('<?php echo htmlspecialchars(sdTxt('Confermi azzeramento statistiche traffico?', 'Confirm traffic stats reset?')); ?>');">
                <input type="hidden" name="action" value="reset_master_stats">
                <button type="submit" class="btn btn-outline-warning btn-sm">
                    <i class="fas fa-rotate-left"></i> <?php echo htmlspecialchars(sdTxt('Azzera statistiche', 'Reset statistics')); ?>
                </button>
            </form>
            <?php endif; ?>
        </div>
        <div class="card-body">
            <?php if ($trafficStats === null): ?>
                <div class="text-muted"><?php echo htmlspecialchars(sdTxt('Statistiche non disponibili: applicare migrazione DB traffico.', 'Statistics unavailable: apply traffic DB migration.')); ?></div>
            <?php else: ?>
                <?php if ($statsResetInfo): ?>
                    <div class="alert alert-info py-2 small mb-2">
                        <?php echo htmlspecialchars(sdTxt('Ultimo reset statistiche:', 'Last stats reset:')); ?> <?php echo htmlspecialchars((string)($statsResetInfo['reset_at'] ?? '-')); ?>
                    </div>
                <?php endif; ?>
                <div class="row g-2">
                    <div class="col-md-4">
                        <div><strong><?php echo htmlspecialchars(sdTxt('Da connessione corrente', 'Current connection')); ?></strong></div>
                        <div><?php echo htmlspecialchars(sdTxt('Totale TX+RX:', 'Total TX+RX:')); ?> <span class="fw-bold"><?php echo htmlspecialchars(sdFormatBytes($trafficStats['session_total'])); ?></span></div>
                        <div><?php echo htmlspecialchars(sdTxt('TX:', 'TX:')); ?> <?php echo htmlspecialchars(sdFormatBytes($trafficStats['session_tx'])); ?></div>
                        <div><?php echo htmlspecialchars(sdTxt('RX:', 'RX:')); ?> <?php echo htmlspecialchars(sdFormatBytes($trafficStats['session_rx'])); ?></div>
                        <div><?php echo htmlspecialchars(sdTxt('POST inviati:', 'POST sent:')); ?> <?php echo htmlspecialchars((string)($trafficStats['session_posts'] ?? '-')); ?></div>
                    </div>
                    <div class="col-md-4">
                        <div><strong><?php echo htmlspecialchars(sdTxt('Ultimo giorno', 'Last day')); ?></strong></div>
                        <div class="fw-bold"><?php echo htmlspecialchars(sdFormatBytes($trafficStats['day_total'])); ?></div>
                    </div>
                    <div class="col-md-4">
                        <div><strong><?php echo htmlspecialchars(sdTxt('Ultima settimana', 'Last week')); ?></strong></div>
                        <div class="fw-bold"><?php echo htmlspecialchars(sdFormatBytes($trafficStats['week_total'])); ?></div>
                    </div>
                </div>
            <?php endif; ?>
        </div>
    </div>

    <div class="card shadow-sm mt-3">
        <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Monitor risorse', 'Resource monitor')); ?></strong></div>
        <div class="card-body">
            <?php if ($resourceStats === null || empty($resourceStats['latest'])): ?>
                <div class="text-muted"><?php echo htmlspecialchars(sdTxt('Monitor non disponibile: aggiorna firmware master e migrazione DB risorse.', 'Monitor unavailable: update master firmware and DB resources migration.')); ?></div>
            <?php else: ?>
                <?php
                    $lr = $resourceStats['latest'];
                    $heapFree = isset($lr['heap_free_bytes']) ? (int)$lr['heap_free_bytes'] : null;
                    $heapMin = isset($lr['heap_min_bytes']) ? (int)$lr['heap_min_bytes'] : null;
                    $heapTotal = isset($lr['heap_total_bytes']) ? (int)$lr['heap_total_bytes'] : null;
                    $sketchUsed = isset($lr['sketch_used_bytes']) ? (int)$lr['sketch_used_bytes'] : null;
                    $sketchFree = isset($lr['sketch_free_bytes']) ? (int)$lr['sketch_free_bytes'] : null;
                    $uptime = isset($lr['uptime_seconds']) ? (int)$lr['uptime_seconds'] : null;
                    $cpuMhz = isset($lr['cpu_mhz']) ? (int)$lr['cpu_mhz'] : null;
                    $heapUsedPct = null;
                    if ($heapTotal && $heapTotal > 0 && $heapFree !== null) {
                        $heapUsedPct = (100.0 * ($heapTotal - $heapFree)) / $heapTotal;
                    }
                ?>
                <div class="row g-2">
                    <div class="col-md-4">
                        <div><strong><?php echo htmlspecialchars(sdTxt('CPU / Uptime', 'CPU / Uptime')); ?></strong></div>
                        <div><?php echo htmlspecialchars(sdTxt('Frequenza CPU:', 'CPU frequency:')); ?> <span class="fw-bold"><?php echo htmlspecialchars($cpuMhz !== null ? ($cpuMhz . ' MHz') : '-'); ?></span></div>
                        <div><?php echo htmlspecialchars(sdTxt('Uptime:', 'Uptime:')); ?> <span class="fw-bold"><?php echo htmlspecialchars(sdFormatDuration($uptime)); ?></span></div>
                    </div>
                    <div class="col-md-4">
                        <div><strong><?php echo htmlspecialchars(sdTxt('RAM Heap', 'Heap RAM')); ?></strong></div>
                        <div><?php echo htmlspecialchars(sdTxt('Libera ora:', 'Free now:')); ?> <span class="fw-bold"><?php echo htmlspecialchars(sdFormatBytes($heapFree)); ?></span></div>
                        <div><?php echo htmlspecialchars(sdTxt('Min sessione:', 'Session min:')); ?> <?php echo htmlspecialchars(sdFormatBytes($heapMin)); ?></div>
                        <div><?php echo htmlspecialchars(sdTxt('Totale heap:', 'Heap total:')); ?> <?php echo htmlspecialchars(sdFormatBytes($heapTotal)); ?></div>
                        <div><?php echo htmlspecialchars(sdTxt('Uso heap:', 'Heap usage:')); ?> <?php echo htmlspecialchars($heapUsedPct !== null ? (number_format($heapUsedPct, 1, '.', '') . '%') : '-'); ?></div>
                    </div>
                    <div class="col-md-4">
                        <div><strong><?php echo htmlspecialchars(sdTxt('Flash sketch', 'Sketch flash')); ?></strong></div>
                        <div><?php echo htmlspecialchars(sdTxt('Usata:', 'Used:')); ?> <span class="fw-bold"><?php echo htmlspecialchars(sdFormatBytes($sketchUsed)); ?></span></div>
                        <div><?php echo htmlspecialchars(sdTxt('Libera:', 'Free:')); ?> <?php echo htmlspecialchars(sdFormatBytes($sketchFree)); ?></div>
                        <?php
                            $dayMinHeap = ($resourceStats['day_min_heap_free'] === null || $resourceStats['day_min_heap_free'] === false) ? null : (int)$resourceStats['day_min_heap_free'];
                            $weekMinHeap = ($resourceStats['week_min_heap_free'] === null || $resourceStats['week_min_heap_free'] === false) ? null : (int)$resourceStats['week_min_heap_free'];
                        ?>
                        <div class="mt-2"><strong><?php echo htmlspecialchars(sdTxt('Min heap 24h:', 'Min heap 24h:')); ?></strong> <?php echo htmlspecialchars(sdFormatBytes($dayMinHeap)); ?></div>
                        <div><strong><?php echo htmlspecialchars(sdTxt('Min heap 7gg:', 'Min heap 7d:')); ?></strong> <?php echo htmlspecialchars(sdFormatBytes($weekMinHeap)); ?></div>
                    </div>
                </div>
            <?php endif; ?>
        </div>
    </div>
    <?php endif; ?>

    <div class="card shadow-sm mt-3">
        <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Ultimi dati (max 20)', 'Latest data (max 20)')); ?></strong></div>
        <div class="card-body table-responsive">
            <table class="table table-sm table-striped align-middle mb-0">
                <thead>
                    <tr>
                        <th><?php echo htmlspecialchars(sdTxt('Quando', 'When')); ?></th>
                        <?php if ($isMasterSerial): ?>
                        <?php if ($showMasterDeltaPLog): ?>
                        <th><?php echo htmlspecialchars(sdTxt('Delta P', 'Delta P')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Slave', 'Slave')); ?></th>
                        <th>Grp</th>
                        <th>P</th>
                        <th>T</th>
                        <th>FW</th>
                        <?php else: ?>
                        <th><?php echo htmlspecialchars(sdTxt('Slave', 'Slave')); ?></th>
                        <th>Grp</th>
                        <th><?php echo htmlspecialchars(sdTxt('Stato relay', 'Relay state')); ?></th>
                        <th>ON</th>
                        <th><?php echo htmlspecialchars(sdTxt('Sicurezza', 'Safety')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Feedback', 'Feedback')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Ore residue', 'Hours remaining')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Avvii', 'Starts')); ?></th>
                        <th>FW</th>
                        <?php endif; ?>
                        <?php else: ?>
                        <?php if ($isRelayBoard): ?>
                        <th>Grp</th>
                        <th><?php echo htmlspecialchars(sdTxt('Stato relay', 'Relay state')); ?></th>
                        <th>ON</th>
                        <th><?php echo htmlspecialchars(sdTxt('Sicurezza', 'Safety')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Feedback', 'Feedback')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Ore residue', 'Hours remaining')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Avvii', 'Starts')); ?></th>
                        <th>FW</th>
                        <th><?php echo htmlspecialchars(sdTxt('Master ID', 'Master ID')); ?></th>
                        <?php elseif ($isPressureBoard): ?>
                        <th>Grp</th>
                        <th>P</th>
                        <th>T</th>
                        <th>FW</th>
                        <th><?php echo htmlspecialchars(sdTxt('Master ID', 'Master ID')); ?></th>
                        <?php else: ?>
                        <th>Grp</th>
                        <th>FW</th>
                        <th><?php echo htmlspecialchars(sdTxt('Master ID', 'Master ID')); ?></th>
                        <?php endif; ?>
                        <?php endif; ?>
                    </tr>
                </thead>
                <tbody>
                    <?php if (empty($recentData)): ?>
                        <tr><td colspan="<?php echo (int)$recentDataColspan; ?>" class="text-muted"><?php echo htmlspecialchars(sdTxt('Nessun dato disponibile.', 'No data available.')); ?></td></tr>
                    <?php else: ?>
                        <?php foreach ($recentData as $r): ?>
                            <tr>
                                <td><?php echo htmlspecialchars((string)($r['recorded_at'] ?? '')); ?></td>
                                <?php if ($isMasterSerial): ?>
                                <?php if ($showMasterDeltaPLog): ?>
                                <td><?php echo htmlspecialchars((string)($r['delta_p'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['slave_sn'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['slave_grp'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['pressure'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['temperature'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['fw_version'] ?? '')); ?></td>
                                <?php else: ?>
                                <td><?php echo htmlspecialchars((string)($r['slave_sn'] ?? '-')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['slave_grp'] ?? '-')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['relay_state'] ?? '-')); ?></td>
                                <td><?php echo htmlspecialchars(sdFlagPretty($r['relay_on'] ?? null)); ?></td>
                                <td><?php echo htmlspecialchars(sdFlagPretty($r['relay_safety_closed'] ?? null)); ?></td>
                                <td><?php echo htmlspecialchars(sdFlagPretty($r['relay_feedback_ok'] ?? null)); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['relay_hours_remaining'] ?? '-')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['relay_starts'] ?? '-')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['fw_version'] ?? '-')); ?></td>
                                <?php endif; ?>
                                <?php else: ?>
                                <?php if ($isRelayBoard): ?>
                                <td><?php echo htmlspecialchars((string)($r['slave_grp'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['relay_state'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars(sdFlagPretty($r['relay_on'] ?? null)); ?></td>
                                <td><?php echo htmlspecialchars(sdFlagPretty($r['relay_safety_closed'] ?? null)); ?></td>
                                <td><?php echo htmlspecialchars(sdFlagPretty($r['relay_feedback_ok'] ?? null)); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['relay_hours_remaining'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['relay_starts'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['fw_version'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['master_id'] ?? '')); ?></td>
                                <?php elseif ($isPressureBoard): ?>
                                <td><?php echo htmlspecialchars((string)($r['slave_grp'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['pressure'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['temperature'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['fw_version'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['master_id'] ?? '')); ?></td>
                                <?php else: ?>
                                <td><?php echo htmlspecialchars((string)($r['slave_grp'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['fw_version'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($r['master_id'] ?? '')); ?></td>
                                <?php endif; ?>
                                <?php endif; ?>
                            </tr>
                        <?php endforeach; ?>
                    <?php endif; ?>
                </tbody>
            </table>
        </div>
    </div>

    <div class="card shadow-sm mt-3">
        <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Storico seriale / impianto (max 20)', 'Serial / plant history (max 20)')); ?></strong></div>
        <div class="card-body table-responsive">
            <table class="table table-sm table-striped align-middle mb-0">
                <thead>
                    <tr>
                        <th><?php echo htmlspecialchars(sdTxt('Quando', 'When')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Azione', 'Action')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Impianto', 'Plant')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Attore', 'Actor')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Dettagli', 'Details')); ?></th>
                    </tr>
                </thead>
                <tbody>
                    <?php if (empty($serialAuditRows)): ?>
                        <tr><td colspan="5" class="text-muted"><?php echo htmlspecialchars(sdTxt('Nessuno storico audit disponibile per questo seriale.', 'No audit history available for this serial.')); ?></td></tr>
                    <?php else: ?>
                        <?php foreach ($serialAuditRows as $auditRow): ?>
                            <tr>
                                <td><?php echo htmlspecialchars((string)($auditRow['created_at'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($auditRow['action'] ?? '')); ?></td>
                                <td>
                                    <?php if (!empty($auditRow['master_id'])): ?>
                                        <a href="plant_detail.php?plant_id=<?php echo (int)$auditRow['master_id']; ?>" class="text-decoration-none">#<?php echo (int)$auditRow['master_id']; ?></a>
                                    <?php else: ?>
                                        -
                                    <?php endif; ?>
                                </td>
                                <td><?php echo htmlspecialchars((string)($auditRow['actor_email'] ?? '-')); ?></td>
                                <td><?php echo htmlspecialchars((string)($auditRow['details'] ?? '')); ?></td>
                            </tr>
                        <?php endforeach; ?>
                    <?php endif; ?>
                </tbody>
            </table>
        </div>
    </div>

    <div class="card shadow-sm mt-3 mb-4">
        <div class="card-header"><strong><?php echo htmlspecialchars(sdTxt('Timeline lifecycle (max 20)', 'Lifecycle timeline (max 20)')); ?></strong></div>
        <div class="card-body table-responsive">
            <table class="table table-sm table-striped align-middle mb-0">
                <thead>
                    <tr>
                        <th><?php echo htmlspecialchars(sdTxt('Quando', 'When')); ?></th>
                        <th>From</th>
                        <th>To</th>
                        <th><?php echo htmlspecialchars(sdTxt('Motivo', 'Reason')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Dettagli', 'Details')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Replaced by', 'Replaced by')); ?></th>
                        <th><?php echo htmlspecialchars(sdTxt('Master', 'Master')); ?></th>
                    </tr>
                </thead>
                <tbody>
                    <?php if (empty($lifecycleEvents)): ?>
                        <tr><td colspan="7" class="text-muted"><?php echo htmlspecialchars(sdTxt('Nessun evento lifecycle.', 'No lifecycle events.')); ?></td></tr>
                    <?php else: ?>
                        <?php foreach ($lifecycleEvents as $e): ?>
                            <tr>
                                <td><?php echo htmlspecialchars((string)($e['created_at'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($e['from_status'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($e['to_status'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($e['reason_code'] ?? '')); ?></td>
                                <td><?php echo htmlspecialchars((string)($e['reason_details'] ?? '')); ?></td>
                                <td>
                                    <?php if (!empty($e['replaced_by_serial'])): ?>
                                        <a href="serial_detail.php?serial=<?php echo urlencode((string)$e['replaced_by_serial']); ?>" class="text-decoration-none"><code><?php echo htmlspecialchars((string)$e['replaced_by_serial']); ?></code></a>
                                    <?php else: ?>
                                        -
                                    <?php endif; ?>
                                </td>
                                <td>
                                    <?php if (!empty($e['master_id'])): ?>
                                        <a href="plant_detail.php?plant_id=<?php echo (int)$e['master_id']; ?>" class="text-decoration-none">#<?php echo (int)$e['master_id']; ?></a>
                                    <?php else: ?>
                                        -
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

<?php if ($canLifecycle): ?>
<div class="modal fade" id="serialLifecycleModal" tabindex="-1" aria-hidden="true">
    <div class="modal-dialog modal-dialog-centered">
        <div class="modal-content">
            <div class="modal-header bg-danger text-white">
                <h5 class="modal-title"><i class="fas fa-triangle-exclamation"></i> <?php echo htmlspecialchars(sdTxt('Operazione sensibile', 'Sensitive operation')); ?></h5>
                <button type="button" class="btn-close btn-close-white" data-bs-dismiss="modal" aria-label="Close"></button>
            </div>
            <form method="POST" onsubmit="return confirm('<?php echo htmlspecialchars(sdTxt('Confermi aggiornamento stato seriale?', 'Confirm serial status update?')); ?>');">
                <div class="modal-body">
                    <div class="alert alert-warning py-2">
                        <?php echo htmlspecialchars(sdTxt(
                            'Stai dismettendo/annullando un seriale. Verifica attentamente i dati prima di confermare.',
                            'You are retiring/voiding a serial. Verify data carefully before confirming.'
                        )); ?>
                    </div>
                    <input type="hidden" name="action" value="set_serial_status">
                    <div class="mb-2">
                        <label class="form-label"><?php echo htmlspecialchars(sdTxt('Nuovo stato', 'Target status')); ?></label>
                        <select name="target_status" class="form-select form-select-sm" required>
                            <option value="retired"><?php echo htmlspecialchars(sdTxt('retired - dismesso', 'retired - dismissed')); ?></option>
                            <option value="voided"><?php echo htmlspecialchars(sdTxt('voided - annullato', 'voided - cancelled')); ?></option>
                        </select>
                    </div>
                    <div class="mb-2">
                        <label class="form-label"><?php echo htmlspecialchars(sdTxt('Motivazione', 'Reason')); ?></label>
                        <select name="reason_code" class="form-select form-select-sm" required>
                            <?php foreach ($statusReasons as $sr): ?>
                                <option value="<?php echo htmlspecialchars((string)$sr['reason_code']); ?>">
                                    <?php
                                        $lbl = $isIt ? ($sr['label_it'] ?? $sr['reason_code']) : ($sr['label_en'] ?? $sr['reason_code']);
                                        echo htmlspecialchars((string)$sr['reason_code'] . ' - ' . (string)$lbl);
                                    ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <div class="mb-2">
                        <label class="form-label"><?php echo htmlspecialchars(sdTxt('Replaced by (opzionale)', 'Replaced by (optional)')); ?></label>
                        <input type="text" name="replaced_by_serial" class="form-control form-control-sm" placeholder="Es. 202602050004">
                    </div>
                    <div class="mb-2">
                        <label class="form-label"><?php echo htmlspecialchars(sdTxt('Dettagli (opzionale)', 'Details (optional)')); ?></label>
                        <input type="text" name="reason_details" class="form-control form-control-sm" placeholder="<?php echo htmlspecialchars(sdTxt('Nota tecnica / ticket', 'Technical note / ticket')); ?>">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" data-bs-dismiss="modal"><?php echo htmlspecialchars(sdTxt('Annulla', 'Cancel')); ?></button>
                    <button type="submit" class="btn btn-danger"><?php echo htmlspecialchars(sdTxt('Conferma', 'Confirm')); ?></button>
                </div>
            </form>
        </div>
    </div>
</div>
<?php endif; ?>

<?php require 'footer.php'; ?>
<script>
function goBackWithFallback(steps) {
    const safeSteps = Number.isFinite(Number(steps)) && Number(steps) > 0 ? Number(steps) : 1;
    if (window.history.length > safeSteps) {
        window.history.go(-safeSteps);
        return;
    }
    window.location.href = 'serials.php';
}

function copySerialCode() {
    const text = document.getElementById('serialCodeValue')?.innerText || '';
    if (!text) return;
    if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(text);
        return;
    }
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    document.execCommand('copy');
    document.body.removeChild(ta);
}

const pressureConfigForm = document.getElementById('pressureConfigForm');
if (pressureConfigForm) {
    pressureConfigForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const fd = new FormData(pressureConfigForm);
        const payload = {
            action: 'queue_pressure_config',
            serial_number: String(fd.get('serial_number') || '').trim(),
            master_id: Number(fd.get('master_id') || 0),
            new_mode: String(fd.get('new_mode') || '').trim(),
            new_group: String(fd.get('new_group') || '').trim(),
            new_ip: String(fd.get('new_ip') || '').trim()
        };
        const resultEl = document.getElementById('pressureConfigResult');
        if (!payload.new_mode && !payload.new_group && !payload.new_ip) {
            resultEl.textContent = <?php echo json_encode(sdTxt('Seleziona almeno un parametro da modificare.', 'Select at least one parameter to change.')); ?>;
            resultEl.className = 'small mt-2 text-danger';
            return;
        }
        if (!confirm(<?php echo json_encode(sdTxt(
            "Confermi l'invio del comando? Questa operazione può alterare il funzionamento dell'impianto.",
            'Confirm command send? This may alter plant behavior.'
        )); ?>)) {
            return;
        }
        resultEl.textContent = <?php echo json_encode(sdTxt('Invio richiesta...', 'Sending request...')); ?>;
        resultEl.className = 'small mt-2 text-muted';
        try {
            const res = await fetch('api_command.php', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            const raw = await res.text();
            let data;
            try {
                data = JSON.parse(raw);
            } catch (err) {
                throw new Error(raw || 'Invalid JSON response');
            }
            if (!res.ok || data.status === 'error') {
                throw new Error(data.message || ('HTTP ' + res.status));
            }
            resultEl.textContent = data.message || <?php echo json_encode(sdTxt('Comando accodato con successo.', 'Command queued successfully.')); ?>;
            resultEl.className = 'small mt-2 text-success';
        } catch (err) {
            resultEl.textContent = err.message || <?php echo json_encode(sdTxt('Errore invio comando.', 'Command send error.')); ?>;
            resultEl.className = 'small mt-2 text-danger';
        }
    });
}
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
