<?php
session_start();
require 'config.php';
require_once 'auth_common.php';

// Includi il gestore della lingua
require 'lang.php';

// Controllo se l'utente è loggato
if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

// Controllo aggiuntivo: se l'utente deve cambiare password, forzalo sulla pagina apposita
$stmt_check = $pdo->prepare("SELECT force_password_change FROM users WHERE id = ?");
$stmt_check->execute([$_SESSION['user_id']]);
$user_status = $stmt_check->fetch();

if ($user_status && $user_status['force_password_change']) {
    header("Location: change_password.php");
    exit;
}

// LOGICA RUOLI
$isAdmin = ($_SESSION['user_role'] === 'admin');
$currentRole = (string)($_SESSION['user_role'] ?? '');
$currentUserId = $_SESSION['user_id'];
$currentUserPermissions = ecAuthCurrentUserPermissions($pdo, (int)$currentUserId);
$canFirmwareUpdate = !empty($currentUserPermissions['firmware_update']);
$canSerialLifecycle = !empty($currentUserPermissions['serial_lifecycle']);
$hasBuilderColumn = false;
$hasUsersCompanyColumn = false;
$hasUsersDashboardFilterPrefsColumn = false;
$hasMasterPermanentOfflineColumn = false;
$hasMasterPlantKindColumn = false;
try {
    $chkBuilderCol = $pdo->query("SHOW COLUMNS FROM masters LIKE 'builder_id'");
    $hasBuilderColumn = (bool)$chkBuilderCol->fetch();
    $chkUsersCompany = $pdo->query("SHOW COLUMNS FROM users LIKE 'company'");
    $hasUsersCompanyColumn = (bool)$chkUsersCompany->fetch();
    $chkDashboardFilterPrefs = $pdo->query("SHOW COLUMNS FROM users LIKE 'dashboard_filter_prefs'");
    $hasUsersDashboardFilterPrefsColumn = (bool)$chkDashboardFilterPrefs->fetch();
    $chkPermanentOffline = $pdo->query("SHOW COLUMNS FROM masters LIKE 'permanently_offline'");
    $hasMasterPermanentOfflineColumn = (bool)$chkPermanentOffline->fetch();
    $chkPlantKind = $pdo->query("SHOW COLUMNS FROM masters LIKE 'plant_kind'");
    $hasMasterPlantKindColumn = (bool)$chkPlantKind->fetch();
} catch (Throwable $e) {
    $hasBuilderColumn = false;
    $hasUsersCompanyColumn = false;
    $hasUsersDashboardFilterPrefsColumn = false;
    $hasMasterPermanentOfflineColumn = false;
    $hasMasterPlantKindColumn = false;
}

$ownerCompanySelect = $hasUsersCompanyColumn ? "o.company as owner_company" : "NULL as owner_company";
$maintainerCompanySelect = $hasUsersCompanyColumn ? "mn.company as maintainer_company" : "NULL as maintainer_company";
$builderCompanySelect = $hasUsersCompanyColumn ? "b.company as builder_company" : "NULL as builder_company";
$builderJoin = $hasBuilderColumn ? " LEFT JOIN users b ON m.builder_id = b.id " : "";
$builderSelect = $hasBuilderColumn ? ", b.email as builder_email, {$builderCompanySelect}" : ", NULL as builder_email, NULL as builder_company";
$builderFilter = $hasBuilderColumn ? " OR m.builder_id = :userIdBuilder " : "";

if ($isAdmin) {
    // Admin: vede tutto, anche i cancellati (ma marcati)
    $sql = "SELECT m.*, o.email as owner_email, {$ownerCompanySelect}, mn.email as maintainer_email, {$maintainerCompanySelect} {$builderSelect} FROM masters m 
            LEFT JOIN users o ON m.owner_id = o.id
            LEFT JOIN users mn ON m.maintainer_id = mn.id
            {$builderJoin}
            ORDER BY m.deleted_at ASC, m.created_at DESC";
    $stmt = $pdo->query($sql);
} else {
    // Gli altri utenti vedono solo gli impianti a cui sono associati (come creatori, proprietari o manutentori)
    // e che non sono stati cancellati.
    $sql = "SELECT m.*, o.email as owner_email, {$ownerCompanySelect}, mn.email as maintainer_email, {$maintainerCompanySelect} {$builderSelect} FROM masters m 
            LEFT JOIN users o ON m.owner_id = o.id
            LEFT JOIN users mn ON m.maintainer_id = mn.id
            {$builderJoin}
            WHERE (m.creator_id = :userIdCreator OR m.owner_id = :userIdOwner OR m.maintainer_id = :userIdMaintainer {$builderFilter}) 
            AND m.deleted_at IS NULL 
            ORDER BY m.created_at DESC";
    $stmt = $pdo->prepare($sql);
    $paramsIdx = [
        'userIdCreator' => $currentUserId,
        'userIdOwner' => $currentUserId,
        'userIdMaintainer' => $currentUserId,
    ];
    if ($hasBuilderColumn) {
        $paramsIdx['userIdBuilder'] = $currentUserId;
    }
    $stmt->execute($paramsIdx);
}
$masters = $stmt->fetchAll();

$dashboardEnabledFilters = ecDashboardFilterPrefs([]);
if ($hasUsersDashboardFilterPrefsColumn) {
    try {
        $stmtFilterPrefs = $pdo->prepare("SELECT dashboard_filter_prefs FROM users WHERE id = ?");
        $stmtFilterPrefs->execute([$currentUserId]);
        $rawFilterPrefs = trim((string)$stmtFilterPrefs->fetchColumn());
        if ($rawFilterPrefs !== '') {
            $decodedFilterPrefs = json_decode($rawFilterPrefs, true);
            if (is_array($decodedFilterPrefs)) {
                $dashboardEnabledFilters = ecDashboardFilterPrefs($decodedFilterPrefs);
            }
        }
    } catch (Throwable $e) {
        $dashboardEnabledFilters = ecDashboardFilterPrefs([]);
    }
}

$dashboardAddressBookUsers = [];
try {
    $stmtContacts = $pdo->prepare("
        SELECT linked_user_id, name
        FROM contacts
        WHERE managed_by_user_id = ?
          AND linked_user_id IS NOT NULL
    ");
    $stmtContacts->execute([$currentUserId]);
    foreach ($stmtContacts->fetchAll() as $row) {
        $linkedUserId = (int)($row['linked_user_id'] ?? 0);
        if ($linkedUserId <= 0) {
            continue;
        }
        $dashboardAddressBookUsers[$linkedUserId] = trim((string)($row['name'] ?? ''));
    }
} catch (Throwable $e) {
    $dashboardAddressBookUsers = [];
}

// Recupera l'ultima versione firmware disponibile per Master (Rewamping)
$stmtFw = $pdo->prepare("SELECT version FROM firmware_releases WHERE device_type = 'master' AND is_active = 1 ORDER BY id DESC LIMIT 1");
$stmtFw->execute();
$latestFw = $stmtFw->fetchColumn();

// Recupera l'ultima versione firmware disponibile per Slave Pressione
$stmtFwS = $pdo->prepare("SELECT version FROM firmware_releases WHERE device_type = 'slave_pressure' AND is_active = 1 ORDER BY id DESC LIMIT 1");
$stmtFwS->execute();
$latestFwSlaveP = $stmtFwS->fetchColumn();

// Recupera l'ultima versione firmware disponibile per Slave Relay
$stmtFwR = $pdo->prepare("SELECT version FROM firmware_releases WHERE device_type = 'slave_relay' AND is_active = 1 ORDER BY id DESC LIMIT 1");
$stmtFwR->execute();
$latestFwSlaveR = $stmtFwR->fetchColumn();

function ecVersionCompare(string $a, string $b): int {
    $pa = array_map('intval', explode('.', preg_replace('/[^0-9.]/', '', $a)));
    $pb = array_map('intval', explode('.', preg_replace('/[^0-9.]/', '', $b)));
    $len = max(count($pa), count($pb));
    for ($i = 0; $i < $len; $i++) {
        $va = $pa[$i] ?? 0;
        $vb = $pb[$i] ?? 0;
        if ($va < $vb) return -1;
        if ($va > $vb) return 1;
    }
    return 0;
}

function ecCompanyLabel(?string $company, ?string $email): string {
    $company = trim((string)$company);
    if ($company !== '') {
        return $company;
    }
    return 'N/D';
}

function ecNormalizePlantKind($value): string {
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['display', 'standalone', 'rewamping'], true) ? $normalized : '';
}

function ecInferPlantKind(array $master, bool $hasPlantKindColumn): string {
    if ($hasPlantKindColumn) {
        $kind = ecNormalizePlantKind($master['plant_kind'] ?? '');
        if ($kind !== '') {
            return $kind;
        }
    }
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', (string)($master['serial_number'] ?? ''), $m)) {
        if ((string)$m[1] === '01') {
            return 'display';
        }
    }
    return '';
}

function ecPlantKindLabel(string $kind): string {
    switch ($kind) {
        case 'display':
            return 'Display';
        case 'standalone':
            return 'Standalone';
        case 'rewamping':
            return 'Rewamping';
        default:
            return '';
    }
}

function ecDeviceTypeLabel(string $productTypeCode): string {
    switch ($productTypeCode) {
        case '03':
            return 'Relay';
        case '04':
            return 'Pressione';
        case '05':
            return 'Motore';
        default:
            return $productTypeCode !== '' ? $productTypeCode : 'Periferica';
    }
}

function ecDeviceModeLabel(string $productTypeCode, $deviceMode): string {
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

function ecDashboardFilterPrefs(array $rawPrefs): array {
    $available = [
        'status',
        'owner',
        'plant_name',
        'address',
        'builder',
        'maintainer',
        'firmware',
        'plant_kind',
        'last_seen_date',
        'created_date',
        'serial',
    ];
    $enabled = [];
    foreach ($rawPrefs as $key) {
        $key = trim((string)$key);
        if ($key !== '' && in_array($key, $available, true)) {
            $enabled[$key] = true;
        }
    }
    if (empty($enabled)) {
        foreach ($available as $key) {
            $enabled[$key] = true;
        }
    }
    return $enabled;
}

// Motivazioni disponibili per dismissione da dashboard.
$retireReasons = [
    ['reason_code' => 'field_replaced', 'label_it' => 'Sostituzione in campo', 'label_en' => 'Field replacement', 'applies_to_status' => 'retired'],
    ['reason_code' => 'damaged', 'label_it' => 'Dismesso per guasto', 'label_en' => 'Dismissed due to fault', 'applies_to_status' => 'retired'],
    ['reason_code' => 'plant_dismission', 'label_it' => 'Impianto dismesso', 'label_en' => 'Plant decommissioned', 'applies_to_status' => 'retired'],
    ['reason_code' => 'master_replaced', 'label_it' => 'Sostituito da altro seriale', 'label_en' => 'Replaced by another serial', 'applies_to_status' => 'retired'],
];
try {
    $stmtReasons = $pdo->query("
        SELECT reason_code, label_it, label_en, applies_to_status
        FROM serial_status_reasons
        WHERE is_active = 1
          AND (applies_to_status = 'retired' OR applies_to_status = 'any')
        ORDER BY sort_order ASC, reason_code ASC
    ");
    $rowsReasons = $stmtReasons->fetchAll();
    if (!empty($rowsReasons)) {
        $retireReasons = $rowsReasons;
    }
} catch (Throwable $e) {
    // fallback statico
}

// Feature flags DB: usati per filtrare dalla dashboard i seriali dismessi/annullati.
$hasDeviceSerials = false;
$hasDeviceSerialsStatus = false;
$hasDeviceSerialsAssignedMaster = false;
$hasDeviceSerialsLockSource = false;
$hasMeasurementsMasterSn = false;
$hasMeasurementsDeviceType = false;
$hasMeasurementsRelayMode = false;
$hasMeasurementsRelayOnline = false;
$hasMeasurementsRelayOn = false;
$hasMeasurementsRelaySafetyClosed = false;
$hasMeasurementsRelayFeedbackOk = false;
$hasMeasurementsRelayFeedbackFault = false;
$hasMeasurementsRelayLifetimeAlarm = false;
$hasMeasurementsRelayLampFault = false;
$hasMeasurementsRelayHoursRemaining = false;
$hasMeasurementsRelayStarts = false;
$hasMeasurementsRelayState = false;
try {
    $chkDs = $pdo->query("SHOW TABLES LIKE 'device_serials'");
    $hasDeviceSerials = (bool)$chkDs->fetch();
    if ($hasDeviceSerials) {
        $chkDsStatus = $pdo->query("SHOW COLUMNS FROM device_serials LIKE 'status'");
        $hasDeviceSerialsStatus = (bool)$chkDsStatus->fetch();
        $chkDsAssignedMaster = $pdo->query("SHOW COLUMNS FROM device_serials LIKE 'assigned_master_id'");
        $hasDeviceSerialsAssignedMaster = (bool)$chkDsAssignedMaster->fetch();
        $chkDsLockSource = $pdo->query("SHOW COLUMNS FROM device_serials LIKE 'lock_source'");
        $hasDeviceSerialsLockSource = (bool)$chkDsLockSource->fetch();
    }
    $chkMasterSn = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'master_sn'");
    $hasMeasurementsMasterSn = (bool)$chkMasterSn->fetch();
    $chkDeviceType = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'device_type'");
    $hasMeasurementsDeviceType = (bool)$chkDeviceType->fetch();
    $chkRelayMode = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_mode'");
    $hasMeasurementsRelayMode = (bool)$chkRelayMode->fetch();
    $chkRelayOnline = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_online'");
    $hasMeasurementsRelayOnline = (bool)$chkRelayOnline->fetch();
    $chkRelayOn = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_on'");
    $hasMeasurementsRelayOn = (bool)$chkRelayOn->fetch();
    $chkRelaySafetyClosed = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_safety_closed'");
    $hasMeasurementsRelaySafetyClosed = (bool)$chkRelaySafetyClosed->fetch();
    $chkRelayFeedbackOk = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_feedback_ok'");
    $hasMeasurementsRelayFeedbackOk = (bool)$chkRelayFeedbackOk->fetch();
    $chkRelayFeedbackFault = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_feedback_fault'");
    $hasMeasurementsRelayFeedbackFault = (bool)$chkRelayFeedbackFault->fetch();
    $chkRelayLifetimeAlarm = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_lifetime_alarm'");
    $hasMeasurementsRelayLifetimeAlarm = (bool)$chkRelayLifetimeAlarm->fetch();
    $chkRelayLampFault = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_lamp_fault'");
    $hasMeasurementsRelayLampFault = (bool)$chkRelayLampFault->fetch();
    $chkRelayHoursRemaining = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_hours_remaining'");
    $hasMeasurementsRelayHoursRemaining = (bool)$chkRelayHoursRemaining->fetch();
    $chkRelayStarts = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_starts'");
    $hasMeasurementsRelayStarts = (bool)$chkRelayStarts->fetch();
    $chkRelayState = $pdo->query("SHOW COLUMNS FROM measurements LIKE 'relay_state'");
    $hasMeasurementsRelayState = (bool)$chkRelayState->fetch();
} catch (Throwable $e) {
    $hasDeviceSerials = false;
    $hasDeviceSerialsStatus = false;
    $hasDeviceSerialsAssignedMaster = false;
    $hasDeviceSerialsLockSource = false;
    $hasMeasurementsMasterSn = false;
    $hasMeasurementsDeviceType = false;
    $hasMeasurementsRelayMode = false;
    $hasMeasurementsRelayOnline = false;
    $hasMeasurementsRelayOn = false;
    $hasMeasurementsRelaySafetyClosed = false;
    $hasMeasurementsRelayFeedbackOk = false;
    $hasMeasurementsRelayFeedbackFault = false;
    $hasMeasurementsRelayLifetimeAlarm = false;
    $hasMeasurementsRelayLampFault = false;
    $hasMeasurementsRelayHoursRemaining = false;
    $hasMeasurementsRelayStarts = false;
    $hasMeasurementsRelayState = false;
}

// Pre-calcolo allarmi impianto (seriale master duplicato / periferiche offline)
// e ordinamento: impianti con allarme in alto.
$nowTs = time();
foreach ($masters as &$mx) {
    $masterId = (int)($mx['id'] ?? 0);
    $dbSerial = trim((string)($mx['serial_number'] ?? ''));
    $detectedSerial = $dbSerial;
    $serialMismatch = false;
    $hasOfflineSlave = false;
    $hasManualPeripheralMismatch = false;
    $plantKind = ecInferPlantKind($mx, $hasMasterPlantKindColumn);
    $plantKindLabel = ecPlantKindLabel($plantKind);
    $isPermanentOffline = $hasMasterPermanentOfflineColumn && ((int)($mx['permanently_offline'] ?? 0) === 1);

    if ($masterId > 0 && $hasMeasurementsMasterSn) {
        try {
            $stmtDet = $pdo->prepare("
                SELECT master_sn
                FROM measurements
                WHERE master_id = ?
                  AND recorded_at >= (NOW() - INTERVAL 10 MINUTE)
                  AND master_sn IS NOT NULL
                  AND master_sn <> ''
                ORDER BY recorded_at DESC
                LIMIT 1
            ");
            $stmtDet->execute([$masterId]);
            $det = trim((string)$stmtDet->fetchColumn());
            if ($det !== '') {
                $detectedSerial = $det;
            }
        } catch (Throwable $e) {
            $detectedSerial = $dbSerial;
        }
    }

    $serialMismatch = ($detectedSerial !== '' && $dbSerial !== '' && $detectedSerial !== $dbSerial);

    if ($masterId > 0) {
        try {
            $sqlOffline = "
                SELECT m1.slave_sn, m1.recorded_at, m1.pressure, m1.temperature
                FROM measurements m1
                INNER JOIN (
                    SELECT slave_sn, MAX(recorded_at) AS max_date
                    FROM measurements
                    WHERE master_id = ?
                      AND slave_sn IS NOT NULL
                      AND slave_sn <> ''
                      AND slave_sn <> '0'
                      AND recorded_at >= (NOW() - INTERVAL 15 MINUTE)
                    GROUP BY slave_sn
                ) m2 ON m1.slave_sn = m2.slave_sn AND m1.recorded_at = m2.max_date
            ";
            if ($hasMeasurementsRelayOnline) {
                $sqlOffline = str_replace(
                    "SELECT m1.slave_sn, m1.recorded_at, m1.pressure, m1.temperature",
                    "SELECT m1.slave_sn, m1.recorded_at, m1.pressure, m1.temperature, m1.relay_online",
                    $sqlOffline
                );
            }
            if ($hasDeviceSerials && $hasDeviceSerialsStatus) {
                $sqlOffline .= " LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn ";
            }
            $sqlOffline .= " WHERE m1.master_id = ? ";
            if ($hasDeviceSerials && $hasDeviceSerialsStatus) {
                $sqlOffline .= " AND (ds.serial_number IS NULL OR ds.status NOT IN ('retired','voided')) ";
            }
            $stmtOffline = $pdo->prepare($sqlOffline);
            $stmtOffline->execute([$masterId, $masterId]);
            foreach ($stmtOffline->fetchAll() as $rr) {
                $ts = strtotime((string)($rr['recorded_at'] ?? ''));
                $fresh = ($ts !== false && $ts > ($nowTs - 75));
                if ($plantKind === 'standalone') {
                    if ($hasMeasurementsRelayOnline) {
                        $online485 = $fresh && ((int)($rr['relay_online'] ?? 0) === 1);
                    } else {
                        $online485 = $fresh;
                    }
                } else {
                    $online485 = $fresh && !is_null($rr['pressure']) && !is_null($rr['temperature']);
                }
                if (!$online485) {
                    $hasOfflineSlave = true;
                    break;
                }
            }
        } catch (Throwable $e) {
            $hasOfflineSlave = false;
        }
    }

    if ($masterId > 0 && $hasDeviceSerials && $hasDeviceSerialsAssignedMaster && $hasDeviceSerialsLockSource) {
        try {
            $manualExpected = [];
            $sqlManual = "
                SELECT serial_number
                FROM device_serials
                WHERE assigned_master_id = ?
                  AND lock_source IN ('manual_plant', 'manual')
                  AND product_type_code NOT IN ('01','02')
            ";
            if ($hasDeviceSerialsStatus) {
                $sqlManual .= " AND (status IS NULL OR status NOT IN ('retired','voided'))";
            }
            $stmtManual = $pdo->prepare($sqlManual);
            $stmtManual->execute([$masterId]);
            foreach ($stmtManual->fetchAll() as $mr) {
                $sn = trim((string)($mr['serial_number'] ?? ''));
                if ($sn !== '') {
                    $manualExpected[$sn] = true;
                }
            }

            if (!empty($manualExpected)) {
                $actualDetected = [];
                $sqlActual = "
                    SELECT DISTINCT m1.slave_sn
                    FROM measurements m1
                    LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn
                    WHERE m1.master_id = ?
                      AND m1.slave_sn IS NOT NULL
                      AND m1.slave_sn <> ''
                      AND m1.slave_sn <> '0'
                      AND m1.recorded_at >= (NOW() - INTERVAL 30 DAY)
                ";
                if ($hasDeviceSerialsStatus) {
                    $sqlActual .= " AND (ds.serial_number IS NULL OR ds.status NOT IN ('retired','voided'))";
                }
                $stmtActual = $pdo->prepare($sqlActual);
                $stmtActual->execute([$masterId]);
                foreach ($stmtActual->fetchAll() as $ar) {
                    $sn = trim((string)($ar['slave_sn'] ?? ''));
                    if ($sn !== '') {
                        $actualDetected[$sn] = true;
                    }
                }

                if (!empty($actualDetected)) {
                    foreach (array_keys($manualExpected) as $expectedSn) {
                        if (!isset($actualDetected[$expectedSn])) {
                            $hasManualPeripheralMismatch = true;
                            break;
                        }
                    }
                    if (!$hasManualPeripheralMismatch) {
                        foreach (array_keys($actualDetected) as $actualSn) {
                            if (!isset($manualExpected[$actualSn])) {
                                $hasManualPeripheralMismatch = true;
                                break;
                            }
                        }
                    }
                }
            }
        } catch (Throwable $e) {
            $hasManualPeripheralMismatch = false;
        }
    }

    $alarmRank = 0;
    if ($serialMismatch) $alarmRank += 2;
    if ($hasManualPeripheralMismatch) $alarmRank += 2;
    if ($hasOfflineSlave) $alarmRank += 1;

    $mx['_detected_serial'] = $detectedSerial;
    $mx['_serial_mismatch'] = $serialMismatch;
    $mx['_has_manual_peripheral_mismatch'] = $hasManualPeripheralMismatch;
    $mx['_has_offline_slave'] = $hasOfflineSlave;
    $mx['_alarm_rank'] = $alarmRank;
    $mx['_plant_kind'] = $plantKind;
    $mx['_plant_kind_label'] = $plantKindLabel;
    $mx['_permanently_offline'] = $isPermanentOffline;
}
unset($mx);

usort($masters, function (array $a, array $b): int {
    $aDeleted = empty($a['deleted_at']) ? 0 : 1;
    $bDeleted = empty($b['deleted_at']) ? 0 : 1;
    if ($aDeleted !== $bDeleted) {
        return $aDeleted <=> $bDeleted;
    }

    $ra = (int)($a['_alarm_rank'] ?? 0);
    $rb = (int)($b['_alarm_rank'] ?? 0);
    if ($ra !== $rb) {
        return $rb <=> $ra;
    }

    $ta = strtotime((string)($a['created_at'] ?? '1970-01-01 00:00:00')) ?: 0;
    $tb = strtotime((string)($b['created_at'] ?? '1970-01-01 00:00:00')) ?: 0;
    return $tb <=> $ta;
});

$dashboardOwnerFilterOptions = [];
$dashboardBuilderFilterOptions = [];
foreach ($masters as $plantRow) {
    $ownerId = (int)($plantRow['owner_id'] ?? 0);
    if ($ownerId > 0 && isset($dashboardAddressBookUsers[$ownerId])) {
        $dashboardOwnerFilterOptions[$ownerId] = trim($dashboardAddressBookUsers[$ownerId]) !== ''
            ? trim($dashboardAddressBookUsers[$ownerId])
            : ecCompanyLabel($plantRow['owner_company'] ?? null, $plantRow['owner_email'] ?? null);
    }

    $builderId = (int)($plantRow['builder_id'] ?? 0);
    if ($builderId > 0 && isset($dashboardAddressBookUsers[$builderId])) {
        $dashboardBuilderFilterOptions[$builderId] = trim($dashboardAddressBookUsers[$builderId]) !== ''
            ? trim($dashboardAddressBookUsers[$builderId])
            : ecCompanyLabel($plantRow['builder_company'] ?? null, $plantRow['builder_email'] ?? null);
    }
}
asort($dashboardOwnerFilterOptions);
asort($dashboardBuilderFilterOptions);

$showDeltaPColumn = false;
foreach ($masters as $plantRow) {
    if ((string)($plantRow['_plant_kind'] ?? '') === 'rewamping') {
        $showDeltaPColumn = true;
        break;
    }
}
$dashboardColumnsCount = $showDeltaPColumn ? 9 : 8;
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Dashboard - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <!-- FontAwesome per le icone -->
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
    <style>
        .status-dot { height: 12px; width: 12px; border-radius: 50%; display: inline-block; }
        .online { background-color: #28a745; }
        .offline { background-color: #dc3545; }
        .slave-details { background-color: #f8f9fa; font-size: 0.9rem; }
        .serial-mismatch-wrap { border: 1px dashed #dc3545; border-radius: 8px; padding: 6px; background: #fff6f6; }
        .serial-mismatch-row { line-height: 1.3; }
        .serial-mismatch-actions .btn { min-width: 30px; }
        .plant-filter-grid { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 12px; }
        .plant-filter-grid .full { grid-column: 1 / -1; }
        .plant-hidden { display: none !important; }
        .filter-toggle-btn[aria-expanded="true"] .filter-toggle-icon { transform: rotate(180deg); }
        .filter-toggle-icon { transition: transform 0.2s ease; }
        .dashboard-filter-card { background: #f8f9fa; border-radius: 12px; }
        .manual-peripherals-card { border: 1px solid #f4d03f; background: #fff9db; border-radius: 8px; }
        .manual-peripherals-card .table { margin-bottom: 0; }
        @media (max-width: 1200px) {
            .plant-filter-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }
        }
        @media (max-width: 768px) {
            .plant-filter-grid { grid-template-columns: repeat(1, minmax(0, 1fr)); }
        }
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="card shadow-sm">
        <div class="card-header bg-white d-flex justify-content-between align-items-center gap-2 flex-wrap">
            <h5 class="mb-0"><i class="fas fa-server"></i> <?php echo $lang['dash_plant_list']; ?></h5>
            <button
                type="button"
                class="btn btn-sm btn-outline-secondary filter-toggle-btn"
                id="dashboardFilterToggle"
                data-bs-toggle="collapse"
                data-bs-target="#dashboardFiltersCollapse"
                aria-expanded="false"
                aria-controls="dashboardFiltersCollapse"
            >
                <i class="fas fa-filter me-1"></i> Filtri
                <i class="fas fa-chevron-down ms-1 filter-toggle-icon"></i>
            </button>
        </div>
        <div class="card-body border-bottom">
            <div class="collapse" id="dashboardFiltersCollapse">
                <div class="dashboard-filter-card p-3">
                    <div class="plant-filter-grid">
                <?php if (!empty($dashboardEnabledFilters['status'])): ?>
                <div data-filter-key="status">
                    <label class="form-label form-label-sm">Stato</label>
                    <select class="form-select form-select-sm" id="filterStatus">
                        <option value="">Tutti</option>
                        <option value="online">Online</option>
                        <option value="offline">Offline</option>
                    </select>
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['owner'])): ?>
                <div data-filter-key="owner">
                    <label class="form-label form-label-sm">Proprietario</label>
                    <select class="form-select form-select-sm" id="filterOwner">
                        <option value="">Tutti</option>
                        <?php foreach ($dashboardOwnerFilterOptions as $ownerId => $ownerLabel): ?>
                            <option value="<?php echo (int)$ownerId; ?>"><?php echo htmlspecialchars($ownerLabel); ?></option>
                        <?php endforeach; ?>
                    </select>
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['plant_name'])): ?>
                <div data-filter-key="plant_name">
                    <label class="form-label form-label-sm">Nome impianto</label>
                    <input type="text" class="form-control form-control-sm" id="filterPlantName" placeholder="Cerca nome impianto">
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['address'])): ?>
                <div data-filter-key="address">
                    <label class="form-label form-label-sm">Indirizzo</label>
                    <input type="text" class="form-control form-control-sm" id="filterAddress" placeholder="Cerca indirizzo">
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['builder'])): ?>
                <div data-filter-key="builder">
                    <label class="form-label form-label-sm">Costruttore</label>
                    <select class="form-select form-select-sm" id="filterBuilder">
                        <option value="">Tutti</option>
                        <?php foreach ($dashboardBuilderFilterOptions as $builderId => $builderLabel): ?>
                            <option value="<?php echo (int)$builderId; ?>"><?php echo htmlspecialchars($builderLabel); ?></option>
                        <?php endforeach; ?>
                    </select>
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['maintainer'])): ?>
                <div data-filter-key="maintainer">
                    <label class="form-label form-label-sm">Manutentore</label>
                    <input type="text" class="form-control form-control-sm" id="filterMaintainer" placeholder="Cerca manutentore">
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['firmware'])): ?>
                <div data-filter-key="firmware">
                    <label class="form-label form-label-sm">Firmware master</label>
                    <input type="text" class="form-control form-control-sm" id="filterFirmware" placeholder="Es. 1.2.3">
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['plant_kind'])): ?>
                <div data-filter-key="plant_kind">
                    <label class="form-label form-label-sm">Tipo impianto</label>
                    <select class="form-select form-select-sm" id="filterPlantKind">
                        <option value="">Tutti</option>
                        <option value="rewamping">Rewamping</option>
                        <option value="standalone">Standalone</option>
                        <option value="display">Display</option>
                    </select>
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['last_seen_date'])): ?>
                <div data-filter-key="last_seen_date">
                    <label class="form-label form-label-sm">Connessione da</label>
                    <input type="date" class="form-control form-control-sm" id="filterLastSeenFrom">
                </div>
                <div data-filter-key="last_seen_date">
                    <label class="form-label form-label-sm">Connessione a</label>
                    <input type="date" class="form-control form-control-sm" id="filterLastSeenTo">
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['created_date'])): ?>
                <div data-filter-key="created_date">
                    <label class="form-label form-label-sm">Creato da</label>
                    <input type="date" class="form-control form-control-sm" id="filterCreatedFrom">
                </div>
                <div data-filter-key="created_date">
                    <label class="form-label form-label-sm">Creato a</label>
                    <input type="date" class="form-control form-control-sm" id="filterCreatedTo">
                </div>
                <?php endif; ?>
                <?php if (!empty($dashboardEnabledFilters['serial'])): ?>
                <div data-filter-key="serial">
                    <label class="form-label form-label-sm">Seriale master</label>
                    <input type="text" class="form-control form-control-sm" id="filterMasterSerial" placeholder="Cerca seriale">
                </div>
                <?php endif; ?>
                <div class="full d-flex justify-content-between align-items-center gap-2 flex-wrap">
                    <div class="small text-muted">
                        Visualizzati: <strong id="filterVisibleCount"><?php echo count($masters); ?></strong> / <?php echo count($masters); ?>
                    </div>
                    <div class="d-flex gap-2">
                        <button type="button" class="btn btn-sm btn-outline-secondary" id="resetPlantFilters">Reset filtri</button>
                    </div>
                </div>
                    </div>
                </div>
            </div>
        </div>
        <div class="card-body p-0 table-responsive">
            <table class="table table-hover mb-0">
                <thead class="table-light">
                    <tr>
                        <th width="5%"><?php echo $lang['dash_status']; ?></th>
                        <th width="20%"><?php echo $lang['dash_plant']; ?></th>
                        <th width="15%"><?php echo $lang['dash_address']; ?></th>
                        <th width="15%"><?php echo $lang['dash_serial']; ?></th>
                        <?php if ($showDeltaPColumn): ?>
                        <th width="10%"><?php echo $lang['dash_deltap']; ?></th>
                        <?php endif; ?>
                        <th width="10%"><?php echo $lang['dash_fw_ver']; ?></th>
                        <th width="5%"><?php echo $lang['dash_signal']; ?></th>
                        <th width="10%"><?php echo $lang['dash_last_seen']; ?></th>
                        <th width="10%"><?php echo $lang['dash_details']; ?></th>
                    </tr>
                </thead>
                <tbody>
                    <?php foreach ($masters as $m): 
                        // Calcolo stato online (se visto negli ultimi 2 minuti)
                        $isOnline = ($m['last_seen'] && strtotime($m['last_seen']) > time() - 120);
                        $statusClass = $isOnline ? 'online' : 'offline';
                        $mapUrl = "https://www.google.com/maps/search/?api=1&query=" . urlencode($m['address']);
                        $plantKind = (string)($m['_plant_kind'] ?? '');
                        $plantKindLabel = (string)($m['_plant_kind_label'] ?? '');
                        $isPermanentOffline = (bool)($m['_permanently_offline'] ?? false);
                        
                        // Gestione visualizzazione cancellati (solo per admin)
                        $isDeleted = !empty($m['deleted_at']);
                        $rowStyle = $isDeleted ? "background-color: #ffe6e6; color: #999;" : "";
                        $hasPlantAlarm = ((int)($m['_alarm_rank'] ?? 0) > 0);
                        if ($isDeleted) {
                            $statusDot = "<span class='badge bg-danger'>ELIMINATO</span>";
                        } else {
                            $statusDot = "<span class='status-dot $statusClass'></span>";
                            if ($hasPlantAlarm) {
                                $statusDot .= " <i class='fas fa-triangle-exclamation text-warning' title='Allarme impianto'></i>";
                            }
                        }
                        
                        // Calcolo qualità segnale per UI
                        $rssi = $m['rssi'] ?? -100;
                        $signalIcon = "fa-signal";
                        $signalColor = "text-muted";
                        if ($isOnline) {
                            if ($rssi > -60) { $signalColor = "text-success"; }
                            elseif ($rssi > -75) { $signalColor = "text-warning"; }
                            elseif ($rssi > -100) { $signalColor = "text-danger"; }
                        } else {
                            $signalColor = "text-danger";
                        }
                        $signalHtml = $isOnline
                            ? '<i class="fas ' . $signalIcon . ' ' . $signalColor . '" title="' . htmlspecialchars((string)$rssi, ENT_QUOTES) . ' dBm"></i>'
                            : '<span class="fa-stack" title="Offline"><i class="fas fa-signal fa-stack-1x text-danger"></i><i class="fas fa-slash fa-stack-1x text-danger"></i></span>';
                        
                        // Recupera le ultime misurazioni per questo master (Spostato qui per calcolare Delta P)
                        $stmtS = $pdo->prepare("SELECT * FROM measurements WHERE master_id = ? ORDER BY recorded_at DESC LIMIT 15");
                        $stmtS->execute([$m['id']]);
                        $measures = $stmtS->fetchAll();
                        $lastMeasure = $measures[0] ?? null; // Prendi la più recente

                        // Delta P visibile solo per impianti Rewamping.
                        $deltaPValue = '';
                        if ($showDeltaPColumn && $plantKind === 'rewamping') {
                            $deltaPValue = '-';
                            foreach ($measures as $ms) {
                                if (empty($ms['slave_sn']) && isset($ms['delta_p'])) {
                                    $deltaPValue = $ms['delta_p'] . ' Pa';
                                    break;
                                }
                            }
                        }
                        
                        // --- RILEVAMENTO CAMBIO SERIALE ---
                        // Se il seriale nell'ultimo pacchetto dati è diverso da quello registrato nel DB
                        $detectedSerial = (string)($m['_detected_serial'] ?? ($lastMeasure['master_sn'] ?? $m['serial_number']));
                        $serialMismatch = (bool)($m['_serial_mismatch'] ?? ($detectedSerial !== $m['serial_number']));
                        $manualPeripheralMismatch = (bool)($m['_has_manual_peripheral_mismatch'] ?? false);
                        // ----------------------------------

                        $dbSerialOnline = $isOnline;
                        $detectedSerialOnline = $isOnline;
                        if ($serialMismatch && $hasMeasurementsMasterSn) {
                            $serialSeenMap = [];
                            try {
                                $stmtSeen = $pdo->prepare("
                                    SELECT master_sn, MAX(recorded_at) AS last_seen
                                    FROM measurements
                                    WHERE master_id = ?
                                      AND master_sn IS NOT NULL
                                      AND master_sn <> ''
                                    GROUP BY master_sn
                                ");
                                $stmtSeen->execute([$m['id']]);
                                foreach ($stmtSeen->fetchAll() as $ss) {
                                    $sn = trim((string)($ss['master_sn'] ?? ''));
                                    if ($sn !== '') {
                                        $serialSeenMap[$sn] = (string)($ss['last_seen'] ?? '');
                                    }
                                }
                            } catch (Throwable $e) {
                                $serialSeenMap = [];
                            }

                            if (isset($serialSeenMap[(string)$m['serial_number']])) {
                                $tsDb = strtotime($serialSeenMap[(string)$m['serial_number']]);
                                $dbSerialOnline = ($tsDb !== false && $tsDb > time() - 120);
                            } else {
                                $dbSerialOnline = false;
                            }
                            if (isset($serialSeenMap[$detectedSerial])) {
                                $tsLive = strtotime($serialSeenMap[$detectedSerial]);
                                $detectedSerialOnline = ($tsLive !== false && $tsLive > time() - 120);
                            } else {
                                $detectedSerialOnline = false;
                            }
                        }

                        // Recupera la LISTA delle periferiche connesse (Ultimo dato per ognuna)
                        // Questa query complessa prende l'ultima misurazione per ogni slave_sn distinto
                        $sqlSlaves = "SELECT m1.*";
                        if ($hasDeviceSerials && $hasDeviceSerialsStatus) {
                            $sqlSlaves .= ", ds.status AS serial_status";
                        }
                        $sqlSlaves .= " FROM measurements m1
                                      INNER JOIN (
                                          SELECT slave_sn, MAX(recorded_at) as max_date
                                          FROM measurements
                                          WHERE master_id = ? AND slave_sn IS NOT NULL AND slave_sn != '' AND slave_sn != '0'
                                            AND recorded_at >= (NOW() - INTERVAL 15 MINUTE)
                                          GROUP BY slave_sn
                                      ) m2 ON m1.slave_sn = m2.slave_sn AND m1.recorded_at = m2.max_date";
                        if ($hasDeviceSerials && $hasDeviceSerialsStatus) {
                            $sqlSlaves .= " LEFT JOIN device_serials ds ON ds.serial_number = m1.slave_sn";
                        }
                        $sqlSlaves .= " WHERE m1.master_id = ?";
                        if ($hasDeviceSerials && $hasDeviceSerialsStatus) {
                            // In dashboard mostriamo solo seriali attivi (o non ancora censiti in device_serials).
                            $sqlSlaves .= " AND (ds.serial_number IS NULL OR ds.status NOT IN ('retired','voided'))";
                        }
                        $stmtSlaves = $pdo->prepare($sqlSlaves);
                        $stmtSlaves->execute([$m['id'], $m['id']]);
                        $slavesList = $stmtSlaves->fetchAll();

                        $manualAssignedList = [];
                        if ($hasDeviceSerials && $hasDeviceSerialsAssignedMaster && $hasDeviceSerialsLockSource) {
                            try {
                                $sqlManualAssigned = "
                                    SELECT serial_number, product_type_code, device_mode, firmware_version, lock_source
                                    FROM device_serials
                                    WHERE assigned_master_id = ?
                                      AND lock_source IN ('manual_plant', 'manual')
                                      AND product_type_code NOT IN ('01','02')
                                ";
                                if ($hasDeviceSerialsStatus) {
                                    $sqlManualAssigned .= " AND (status IS NULL OR status NOT IN ('retired','voided'))";
                                }
                                $sqlManualAssigned .= " ORDER BY serial_number ASC";
                                $stmtManualAssigned = $pdo->prepare($sqlManualAssigned);
                                $stmtManualAssigned->execute([$m['id']]);
                                $manualAssignedList = $stmtManualAssigned->fetchAll();
                            } catch (Throwable $e) {
                                $manualAssignedList = [];
                            }
                        }
                    ?>
                    <tr class="plant-row"
                        data-master-id="<?php echo (int)$m['id']; ?>"
                        data-online="<?php echo $isOnline ? '1' : '0'; ?>"
                        data-owner-id="<?php echo (int)($m['owner_id'] ?? 0); ?>"
                        data-builder-id="<?php echo (int)($m['builder_id'] ?? 0); ?>"
                        data-plant-name="<?php echo htmlspecialchars(strtolower((string)($m['nickname'] ?? ''))); ?>"
                        data-address="<?php echo htmlspecialchars(strtolower((string)($m['address'] ?? ''))); ?>"
                        data-maintainer-name="<?php echo htmlspecialchars(strtolower(ecCompanyLabel($m['maintainer_company'] ?? null, $m['maintainer_email'] ?? null))); ?>"
                        data-firmware="<?php echo htmlspecialchars(strtolower((string)($m['fw_version'] ?? ''))); ?>"
                        data-plant-kind="<?php echo htmlspecialchars($plantKind); ?>"
                        data-last-seen="<?php echo htmlspecialchars((string)($m['last_seen'] ?? '')); ?>"
                        data-created-at="<?php echo htmlspecialchars((string)($m['created_at'] ?? '')); ?>"
                        data-serial="<?php echo htmlspecialchars((string)($m['serial_number'] ?? '')); ?>"
                        data-permanent-offline="<?php echo $isPermanentOffline ? '1' : '0'; ?>"
                        style="<?php echo $rowStyle; ?>">
                        <td class="text-center align-middle"><?php echo $statusDot; ?></td>
                        <td class="align-middle">
                            <strong><a href="plant_detail.php?plant_id=<?php echo (int)$m['id']; ?>" class="text-decoration-none"><?php echo htmlspecialchars($m['nickname']); ?></a></strong>
                            <div class="small text-muted">ID impianto: <?php echo (int)$m['id']; ?></div>
                            <?php if ($plantKindLabel !== ''): ?>
                                <div class="mt-1"><span class="badge bg-secondary"><?php echo htmlspecialchars($plantKindLabel); ?></span></div>
                            <?php endif; ?>
                            <?php if ($isPermanentOffline): ?>
                                <div class="mt-1"><span class="badge bg-dark">Offline permanente</span></div>
                            <?php endif; ?>
                            <?php if ($manualPeripheralMismatch): ?>
                                <div class="mt-1"><span class="badge bg-danger">Periferiche da verificare</span></div>
                            <?php endif; ?>
                        </td>
                        <td class="align-middle">
                            <a href="<?php echo $mapUrl; ?>" target="_blank" class="text-decoration-none text-secondary">
                                <i class="fas fa-map-marker-alt text-danger"></i> <?php echo htmlspecialchars($m['address']); ?>
                            </a>
                        </td>
                        <td class="align-middle">
                            <?php if ($serialMismatch): ?>
                                <?php
                                    $dbRowClass = $dbSerialOnline ? 'text-success' : 'text-danger';
                                    $liveRowClass = $detectedSerialOnline ? 'text-success' : 'text-danger';
                                    $offlineSerial = '';
                                    $onlineSerial = '';
                                    if ($dbSerialOnline && !$detectedSerialOnline) {
                                        $offlineSerial = (string)$detectedSerial;
                                        $onlineSerial = (string)$m['serial_number'];
                                    } elseif (!$dbSerialOnline && $detectedSerialOnline) {
                                        $offlineSerial = (string)$m['serial_number'];
                                        $onlineSerial = (string)$detectedSerial;
                                    }
                                ?>
                                <div class="serial-mismatch-wrap">
                                    <div class="serial-mismatch-row <?php echo $dbRowClass; ?> fw-bold">
                                        <i class="fas fa-circle"></i> DB:
                                        <a href="serial_detail.php?serial=<?php echo urlencode((string)$m['serial_number']); ?>" class="<?php echo $dbRowClass; ?> text-decoration-none fw-bold">
                                            <?php echo htmlspecialchars($m['serial_number']); ?>
                                        </a>
                                        <span class="badge <?php echo $dbSerialOnline ? 'bg-success' : 'bg-danger'; ?> ms-1"><?php echo $dbSerialOnline ? 'Online' : 'Offline'; ?></span>
                                    </div>
                                    <div class="serial-mismatch-row <?php echo $liveRowClass; ?> fw-bold">
                                        <i class="fas fa-circle"></i> LIVE:
                                        <a href="serial_detail.php?serial=<?php echo urlencode((string)$detectedSerial); ?>" class="<?php echo $liveRowClass; ?> text-decoration-none fw-bold">
                                            <?php echo htmlspecialchars($detectedSerial); ?>
                                        </a>
                                        <span class="badge <?php echo $detectedSerialOnline ? 'bg-success' : 'bg-danger'; ?> ms-1"><?php echo $detectedSerialOnline ? 'Online' : 'Offline'; ?></span>
                                    </div>
                                    <div class="serial-mismatch-actions btn-group btn-group-sm mt-1" role="group">
                                        <?php if ($canSerialLifecycle): ?>
                                            <button class="btn btn-outline-primary"
                                                    title="Allinea seriale master"
                                                    onclick="replaceSerial(
                                                        <?php echo (int)$m['id']; ?>,
                                                        '<?php echo htmlspecialchars((string)$m['serial_number'], ENT_QUOTES); ?>',
                                                        '<?php echo htmlspecialchars((string)$detectedSerial, ENT_QUOTES); ?>'
                                                    )">
                                                <i class="fas fa-right-left"></i>
                                            </button>
                                        <?php else: ?>
                                            <button class="btn btn-outline-secondary" disabled title="Operazione non abilitata per la tua utenza">
                                                <i class="fas fa-right-left"></i>
                                            </button>
                                        <?php endif; ?>
                                        <?php if ($offlineSerial !== ''): ?>
                                            <?php if ($canSerialLifecycle): ?>
                                                <button class="btn btn-outline-danger"
                                                        title="Dismetti seriale offline"
                                                        onclick="openRetireMasterModal(
                                                            <?php echo (int)$m['id']; ?>,
                                                            '<?php echo htmlspecialchars($offlineSerial, ENT_QUOTES); ?>',
                                                            '<?php echo htmlspecialchars($onlineSerial, ENT_QUOTES); ?>'
                                                        )">
                                                    <i class="fas fa-trash"></i>
                                                </button>
                                            <?php else: ?>
                                                <button class="btn btn-outline-secondary" disabled title="Dismissione seriali non abilitata per la tua utenza">
                                                    <i class="fas fa-trash"></i>
                                                </button>
                                            <?php endif; ?>
                                        <?php else: ?>
                                            <button class="btn btn-outline-secondary" disabled title="Nessun seriale offline da dismettere">
                                                <i class="fas fa-trash"></i>
                                            </button>
                                        <?php endif; ?>
                                    </div>
                                </div>
                            <?php else: ?>
                                <a href="serial_detail.php?serial=<?php echo urlencode((string)$m['serial_number']); ?>" class="text-decoration-none">
                                    <?php echo htmlspecialchars($m['serial_number']); ?>
                                </a>
                            <?php endif; ?>
                        </td>
                        <?php if ($showDeltaPColumn): ?>
                        <td class="align-middle"><strong><?php echo $deltaPValue; ?></strong></td>
                        <?php endif; ?>
                        <td class="align-middle"><span class="badge bg-info text-dark"><?php echo htmlspecialchars($m['fw_version']); ?></span></td>
                        <td class="align-middle text-center"><?php echo $signalHtml; ?></td>
                        <td class="align-middle small text-muted"><?php echo $m['last_seen'] ? date('d/m H:i', strtotime($m['last_seen'])) : $lang['dash_never']; ?></td>
                        <td class="align-middle">
                            <button class="btn btn-sm btn-outline-primary" type="button" data-bs-toggle="collapse" data-bs-target="#details-<?php echo $m['id']; ?>">
                                <i class="fas fa-plus"></i>
                            </button>
                        </td>
                    </tr>
                    <!-- Riga Dettagli MASTER (Contiene lista periferiche) -->
                    <tr class="plant-detail-row" data-detail-for="<?php echo (int)$m['id']; ?>">
                        <td colspan="<?php echo (int)$dashboardColumnsCount; ?>" class="p-0 border-0">
                            <div class="collapse slave-details p-3" id="details-<?php echo $m['id']; ?>">
                                
                                <!-- Intestazione Dettagli Master -->
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h6 class="mb-0 text-primary"><i class="fas fa-network-wired"></i> <?php echo $lang['dash_connected_devices']; ?></h6>
                                    <div>
                                        <a href="plant_detail.php?plant_id=<?php echo (int)$m['id']; ?>" class="btn btn-sm btn-outline-primary me-2"><i class="fas fa-industry"></i> Scheda impianto</a>
                                        <a href="download_csv.php?master_id=<?php echo $m['id']; ?>" class="btn btn-sm btn-success me-2"><i class="fas fa-file-csv"></i> <?php echo $lang['dash_log_plant']; ?></a>
                                        
                                        <?php 
                                        // Pulsante Storico Modifiche (Audit) - Visibile solo a Admin, Builder, Maintainer
                                        if ($isAdmin || $_SESSION['user_role'] === 'builder' || $_SESSION['user_role'] === 'maintainer'): ?>
                                            <a href="download_audit.php?master_id=<?php echo $m['id']; ?>" class="btn btn-sm btn-info me-2 text-white"><i class="fas fa-clipboard-list"></i> <?php echo $lang['dash_audit']; ?></a>
                                        <?php endif; ?>
                                        
                                        <!-- PULSANTE AGGIORNAMENTO MASTER -->
                                        <?php 
                                            $cmpMasterVsLatest = $latestFw ? ecVersionCompare((string)$m['fw_version'], (string)$latestFw) : 0;
                                            $updateAvailable = ($latestFw && $cmpMasterVsLatest < 0);
                                            $firmwareAhead = ($latestFw && $cmpMasterVsLatest > 0);
                                            $canUpdate = $canFirmwareUpdate && ($rssi >= -75) && $isOnline;
                                            $btnClass = ($canUpdate && $updateAvailable && !$firmwareAhead) ? "btn-warning" : "btn-outline-secondary";
                                            $btnAttr = ($canUpdate && $updateAvailable && !$firmwareAhead) ? "" : "disabled";
                                            if ($firmwareAhead) {
                                                $btnText = "FW locale piu recente";
                                            } elseif (!$canFirmwareUpdate) {
                                                $btnText = "Aggiornamenti non abilitati";
                                            } elseif ($updateAvailable) {
                                                $btnText = $lang['dash_update_master'] . " v$latestFw";
                                            } else {
                                                $btnText = $lang['dash_master_updated'];
                                            }
                                            
                                            if ($m['update_requested'] == 1) {
                                                echo '<span class="badge bg-warning text-dark me-2"><i class="fas fa-sync fa-spin"></i> ' . $lang['dash_master_updating'] . '</span>';
                                            } elseif ($m['ota_status'] === 'Failed' && !$firmwareAhead && $updateAvailable && $canUpdate) {
                                                echo '<button type="button" class="btn btn-sm btn-danger me-2" onclick="startUpdate('.$m['id'].', \''.$latestFw.'\')"><i class="fas fa-redo"></i> ' . $lang['dash_retry_master'] . '</button>';
                                            } else {
                                                echo '<button type="button" class="btn btn-sm '.$btnClass.' me-2" '.$btnAttr.' onclick="startUpdate('.$m['id'].', \''.$latestFw.'\')"><i class="fas fa-microchip"></i> '.$btnText.'</button>';
                                            }
                                        ?>
                                    </div>
                                </div>

                                <div class="manual-peripherals-card mb-3">
                                    <div class="px-3 py-2 border-bottom d-flex justify-content-between align-items-center">
                                        <strong class="text-dark"><i class="fas fa-link"></i> Periferiche assegnate</strong>
                                        <span class="badge bg-warning text-dark"><?php echo count($manualAssignedList); ?></span>
                                    </div>
                                    <div class="table-responsive">
                                        <table class="table table-sm table-hover bg-transparent">
                                            <thead class="table-warning">
                                                <tr>
                                                    <th>Tipo</th>
                                                    <th><?php echo $lang['dash_serial']; ?></th>
                                                    <th>Modalita</th>
                                                    <th><?php echo $lang['dash_fw_ver']; ?></th>
                                                    <th>Fonte</th>
                                                </tr>
                                            </thead>
                                            <tbody>
                                                <?php if (empty($manualAssignedList)): ?>
                                                    <tr><td colspan="5" class="text-center text-muted">Nessuna periferica assegnata manualmente.</td></tr>
                                                <?php else: ?>
                                                    <?php foreach ($manualAssignedList as $manualPeripheral): ?>
                                                        <tr>
                                                            <td><span class="badge bg-warning text-dark"><?php echo htmlspecialchars(ecDeviceTypeLabel((string)($manualPeripheral['product_type_code'] ?? ''))); ?></span></td>
                                                            <td>
                                                                <a href="serial_detail.php?serial=<?php echo urlencode((string)($manualPeripheral['serial_number'] ?? '')); ?>" class="text-decoration-none">
                                                                    <?php echo htmlspecialchars((string)($manualPeripheral['serial_number'] ?? '')); ?>
                                                                </a>
                                                            </td>
                                                            <td><?php echo htmlspecialchars(ecDeviceModeLabel((string)($manualPeripheral['product_type_code'] ?? ''), $manualPeripheral['device_mode'] ?? null)); ?></td>
                                                            <td><?php echo htmlspecialchars((string)($manualPeripheral['firmware_version'] ?? 'N/D')); ?></td>
                                                            <td><span class="badge bg-dark"><?php echo htmlspecialchars((string)($manualPeripheral['lock_source'] ?? 'manual')); ?></span></td>
                                                        </tr>
                                                    <?php endforeach; ?>
                                                <?php endif; ?>
                                            </tbody>
                                        </table>
                                    </div>
                                </div>

                                <!-- Tabella Periferiche -->
                                <div class="table-responsive">
                                    <?php if ($plantKind === 'standalone'): ?>
                                        <table class="table table-sm table-hover bg-white border rounded">
                                            <thead class="table-light">
                                                <tr>
                                                    <th>Type</th>
                                                    <th><?php echo $lang['dash_serial']; ?></th>
                                                    <th>Grp</th>
                                                    <th>Stato</th>
                                                    <th>Relay</th>
                                                    <th>Sicurezza</th>
                                                    <th>Feedback</th>
                                                    <th>Ore rimanenti</th>
                                                    <th>Accensioni</th>
                                                    <th><?php echo $lang['dash_fw_ver']; ?></th>
                                                    <th><?php echo $lang['dash_last_seen']; ?></th>
                                                    <th><?php echo $lang['table_actions']; ?></th>
                                                </tr>
                                            </thead>
                                            <tbody>
                                                <?php foreach($slavesList as $slave):
                                                    $slaveTs = strtotime((string)($slave['recorded_at'] ?? ''));
                                                    $slaveFresh = ($slaveTs !== false && $slaveTs > time() - 75);
                                                    $relayOnline485 = $hasMeasurementsRelayOnline
                                                        ? ($slaveFresh && ((int)($slave['relay_online'] ?? 0) === 1))
                                                        : $slaveFresh;
                                                    $relayModeRaw = $hasMeasurementsRelayMode && isset($slave['relay_mode']) && $slave['relay_mode'] !== null ? (int)$slave['relay_mode'] : null;
                                                    $relayModeLabel = $relayModeRaw === null ? '-' : (string)$relayModeRaw;
                                                    if ($relayModeRaw === 2) {
                                                        $relayModeLabel = '2 - UVC';
                                                    } elseif ($relayModeRaw === 1) {
                                                        $relayModeLabel = '1 - LUCE';
                                                    } elseif ($relayModeRaw === 3) {
                                                        $relayModeLabel = '3 - ELETTROSTATICO';
                                                    }
                                                    $relayOn = $hasMeasurementsRelayOn && isset($slave['relay_on']) && $slave['relay_on'] !== null ? ((int)$slave['relay_on'] === 1) : null;
                                                    $safetyClosed = $hasMeasurementsRelaySafetyClosed && isset($slave['relay_safety_closed']) && $slave['relay_safety_closed'] !== null ? ((int)$slave['relay_safety_closed'] === 1) : null;
                                                    $feedbackOk = $hasMeasurementsRelayFeedbackOk && isset($slave['relay_feedback_ok']) && $slave['relay_feedback_ok'] !== null ? ((int)$slave['relay_feedback_ok'] === 1) : null;
                                                    $feedbackFault = $hasMeasurementsRelayFeedbackFault && isset($slave['relay_feedback_fault']) && $slave['relay_feedback_fault'] !== null ? ((int)$slave['relay_feedback_fault'] === 1) : null;
                                                    $lifetimeAlarm = $hasMeasurementsRelayLifetimeAlarm && isset($slave['relay_lifetime_alarm']) && $slave['relay_lifetime_alarm'] !== null ? ((int)$slave['relay_lifetime_alarm'] === 1) : null;
                                                    $lampFault = $hasMeasurementsRelayLampFault && isset($slave['relay_lamp_fault']) && $slave['relay_lamp_fault'] !== null ? ((int)$slave['relay_lamp_fault'] === 1) : null;
                                                    $relayState = $hasMeasurementsRelayState ? trim((string)($slave['relay_state'] ?? '')) : '';
                                                    if ($relayState === '') {
                                                        $relayState = '-';
                                                    }
                                                    $hoursRemaining = $hasMeasurementsRelayHoursRemaining && isset($slave['relay_hours_remaining']) && $slave['relay_hours_remaining'] !== null
                                                        ? (float)$slave['relay_hours_remaining']
                                                        : null;
                                                    $hoursOn = (isset($slave['relay_hours_on']) && $slave['relay_hours_on'] !== null) ? (float)$slave['relay_hours_on'] : null;
                                                    $startsCount = $hasMeasurementsRelayStarts && isset($slave['relay_starts']) && $slave['relay_starts'] !== null ? (int)$slave['relay_starts'] : null;

                                                    $issues = [];
                                                    if (!$relayOnline485) $issues[] = 'Offline RS485';
                                                    if ($relayModeRaw !== null && $relayModeRaw !== 2) $issues[] = 'Modalita non UVC';
                                                    if ($safetyClosed === false) $issues[] = 'Sicurezza aperta';
                                                    if ($feedbackFault === true) $issues[] = 'Feedback fault';
                                                    if ($lampFault === true) $issues[] = 'Guasto lampada';
                                                    if ($lifetimeAlarm === true) $issues[] = 'Fine vita lampada';
                                                    $relayOk = $relayOnline485 && empty($issues);
                                                    $relayStatusBadge = $relayOk
                                                        ? '<span class="badge bg-success">Operativa</span>'
                                                        : '<span class="badge bg-danger">Anomalia</span>';

                                                    $cmpSlaveVsLatestRelay = $latestFwSlaveR ? ecVersionCompare((string)($slave['fw_version'] ?? '0.0.0'), (string)$latestFwSlaveR) : 0;
                                                    $relayUpdateAvailable = ($latestFwSlaveR && $cmpSlaveVsLatestRelay < 0);
                                                    $relayFwAhead = ($latestFwSlaveR && $cmpSlaveVsLatestRelay > 0);
                                                    $relayUpdateTitle = $relayFwAhead
                                                        ? 'Firmware relay locale piu recente di quello attivo a portale'
                                                        : ($relayUpdateAvailable ? ('Aggiornamento disponibile: v' . $latestFwSlaveR) : 'Firmware relay aggiornato');
                                                    $relayUpdateBtnClass = $relayUpdateAvailable ? 'btn-warning' : 'btn-outline-secondary';

                                                    $trashBtnClass = $relayOnline485 ? "btn-outline-secondary" : "btn-outline-danger";
                                                    $trashTitle = $relayOnline485
                                                        ? "Periferica online: la dismissione puo creare problemi al sistema"
                                                        : "Dismetti periferica";
                                                    $historyId = 'relay-history-' . (int)$m['id'] . '-' . preg_replace('/[^A-Za-z0-9]/', '', (string)($slave['slave_sn'] ?? ''));

                                                    $startEvents = [];
                                                    if ($hasMeasurementsRelayStarts) {
                                                        $stmtHistRelay = $pdo->prepare("
                                                            SELECT recorded_at, relay_starts
                                                            FROM measurements
                                                            WHERE master_id = ? AND slave_sn = ?
                                                            ORDER BY recorded_at DESC
                                                            LIMIT 120
                                                        ");
                                                        $stmtHistRelay->execute([$m['id'], $slave['slave_sn']]);
                                                        $histRowsRelay = $stmtHistRelay->fetchAll();
                                                        $histRowsRelay = array_reverse($histRowsRelay);
                                                        $prevStarts = null;
                                                        foreach ($histRowsRelay as $hRow) {
                                                            if (!isset($hRow['relay_starts']) || $hRow['relay_starts'] === null) {
                                                                continue;
                                                            }
                                                            $currentStarts = (int)$hRow['relay_starts'];
                                                            if ($prevStarts !== null && $currentStarts > $prevStarts) {
                                                                $startEvents[] = [
                                                                    'recorded_at' => (string)$hRow['recorded_at'],
                                                                    'delta' => ($currentStarts - $prevStarts),
                                                                    'total' => $currentStarts
                                                                ];
                                                            }
                                                            $prevStarts = $currentStarts;
                                                        }
                                                        if (count($startEvents) > 10) {
                                                            $startEvents = array_slice($startEvents, -10);
                                                        }
                                                        $startEvents = array_reverse($startEvents);
                                                    }
                                                ?>
                                                <tr>
                                                    <td><span class="badge bg-info text-dark">Relay UVC</span></td>
                                                    <td>
                                                        <strong>
                                                            <a href="serial_detail.php?serial=<?php echo urlencode((string)$slave['slave_sn']); ?>" class="text-decoration-none">
                                                                <?php echo htmlspecialchars((string)$slave['slave_sn']); ?>
                                                            </a>
                                                        </strong>
                                                        <div class="mt-1"><?php echo $relayStatusBadge; ?></div>
                                                        <div class="small text-muted mt-1">Mode: <?php echo htmlspecialchars($relayModeLabel); ?></div>
                                                    </td>
                                                    <td><?php echo htmlspecialchars((string)($slave['slave_grp'] ?? '-')); ?></td>
                                                    <td>
                                                        <?php echo htmlspecialchars($relayState); ?>
                                                        <?php if (!empty($issues)): ?>
                                                            <div class="small text-danger mt-1"><?php echo htmlspecialchars(implode(' | ', $issues)); ?></div>
                                                        <?php endif; ?>
                                                    </td>
                                                    <td><?php echo $relayOn === null ? '-' : ($relayOn ? 'ON' : 'OFF'); ?></td>
                                                    <td><?php echo $safetyClosed === null ? '-' : ($safetyClosed ? 'Chiusa' : 'Aperta'); ?></td>
                                                    <td><?php echo $feedbackOk === null ? '-' : ($feedbackOk ? 'OK' : 'NO'); ?></td>
                                                    <td>
                                                        <?php
                                                            if ($hoursRemaining === null) {
                                                                echo '--';
                                                            } else {
                                                                echo htmlspecialchars(number_format($hoursRemaining, 1, '.', '')) . ' h';
                                                            }
                                                        ?>
                                                        <div class="small text-muted mt-1">
                                                            ON: <?php echo $hoursOn === null ? '--' : htmlspecialchars(number_format($hoursOn, 1, '.', '')); ?> h
                                                        </div>
                                                    </td>
                                                    <td><?php echo $startsCount === null ? '--' : (int)$startsCount; ?></td>
                                                    <td>
                                                        <?php echo htmlspecialchars((string)($slave['fw_version'] ?? 'N/D')); ?>
                                                        <?php if ($relayUpdateAvailable): ?>
                                                            <div class="small text-warning fw-bold mt-1">Update disponibile</div>
                                                        <?php endif; ?>
                                                    </td>
                                                    <td><?php echo date('H:i:s', strtotime((string)$slave['recorded_at'])); ?></td>
                                                    <td>
                                                        <button class="btn btn-xs btn-outline-dark" type="button" data-bs-toggle="collapse" data-bs-target="#<?php echo htmlspecialchars($historyId); ?>">
                                                            <i class="fas fa-history" title="Log accensioni"></i>
                                                        </button>
                                                        <a href="download_csv.php?master_id=<?php echo $m['id']; ?>&slave_sn=<?php echo $slave['slave_sn']; ?>" class="btn btn-xs btn-outline-success" title="<?php echo $lang['slave_download_csv_tooltip']; ?>"><i class="fas fa-download"></i></a>
                                                        <?php if ($canSerialLifecycle): ?>
                                                            <button class="btn btn-xs <?php echo $trashBtnClass; ?>"
                                                                    title="<?php echo htmlspecialchars($trashTitle, ENT_QUOTES); ?>"
                                                                    onclick="openRetireSlaveModal(
                                                                        <?php echo (int)$m['id']; ?>,
                                                                        '<?php echo htmlspecialchars((string)$slave['slave_sn'], ENT_QUOTES); ?>',
                                                                        <?php echo $relayOnline485 ? 'true' : 'false'; ?>
                                                                    )">
                                                                <i class="fas fa-trash"></i>
                                                            </button>
                                                        <?php else: ?>
                                                            <button class="btn btn-xs btn-outline-secondary" disabled title="Dismissione seriali non abilitata per la tua utenza">
                                                                <i class="fas fa-trash"></i>
                                                            </button>
                                                        <?php endif; ?>
                                                        <button class="btn btn-xs <?php echo $relayUpdateBtnClass; ?>"
                                                                title="<?php echo htmlspecialchars($relayUpdateTitle, ENT_QUOTES); ?>"
                                                                disabled>
                                                            <i class="fas fa-sync"></i>
                                                        </button>
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td colspan="12" class="p-0 border-0">
                                                        <div class="collapse bg-light p-2 ps-4" id="<?php echo htmlspecialchars($historyId); ?>">
                                                            <small class="text-muted fw-bold">Ultime accensioni rilevate per <?php echo htmlspecialchars((string)$slave['slave_sn']); ?>:</small>
                                                            <table class="table table-xs table-borderless mb-0 text-muted">
                                                                <?php if (empty($startEvents)): ?>
                                                                    <tr><td>Nessuna accensione registrata nel periodo storico disponibile.</td></tr>
                                                                <?php else: ?>
                                                                    <?php foreach ($startEvents as $ev): ?>
                                                                        <tr>
                                                                            <td width="30%"><?php echo date('d/m H:i:s', strtotime((string)$ev['recorded_at'])); ?></td>
                                                                            <td width="20%">+<?php echo (int)$ev['delta']; ?></td>
                                                                            <td width="30%">Totale: <?php echo (int)$ev['total']; ?></td>
                                                                            <td></td>
                                                                        </tr>
                                                                    <?php endforeach; ?>
                                                                <?php endif; ?>
                                                            </table>
                                                        </div>
                                                    </td>
                                                </tr>
                                                <?php endforeach; ?>
                                                <?php if(empty($slavesList)) echo "<tr><td colspan='12' class='text-center text-muted'>Nessuna relay rilevata.</td></tr>"; ?>
                                            </tbody>
                                        </table>
                                    <?php else: ?>
                                    <table class="table table-sm table-hover bg-white border rounded">
                                        <thead class="table-light">
                                            <tr>
                                                <th>Type</th>
                                                <th><?php echo $lang['dash_serial']; ?></th>
                                                <th>Grp</th>
                                                <th>Press.</th>
                                                <th>Temp</th>
                                                <th><?php echo $lang['dash_fw_ver']; ?></th>
                                                <th><?php echo $lang['dash_last_seen']; ?></th>
                                                <th><?php echo $lang['table_actions']; ?></th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <?php foreach($slavesList as $slave): 
                                                $cmpSlaveVsLatest = $latestFwSlaveP ? ecVersionCompare((string)($slave['fw_version'] ?? '0.0.0'), (string)$latestFwSlaveP) : 0;
                                                $slaveUpdAvail = ($latestFwSlaveP && $cmpSlaveVsLatest < 0);
                                                $slaveFwAhead = ($latestFwSlaveP && $cmpSlaveVsLatest > 0);
                                                // Nota: Aggiornamento slave non ancora implementato nel backend, ma UI pronta
                                                $slaveTs = strtotime((string)($slave['recorded_at'] ?? ''));
                                                $slaveFresh = ($slaveTs !== false && $slaveTs > time() - 75);
                                                $slaveOnline485 = $slaveFresh && !is_null($slave['pressure']) && !is_null($slave['temperature']);
                                                $canSlaveUpdate = $canFirmwareUpdate && $slaveUpdAvail && $slaveOnline485 && !$slaveFwAhead;
                                                $slaveBtnClass = $canSlaveUpdate ? "btn-warning" : "btn-outline-secondary";
                                                $trashBtnClass = $slaveOnline485 ? "btn-outline-secondary" : "btn-outline-danger";
                                                $trashTitle = $slaveOnline485
                                                    ? "Periferica online: la dismissione puo creare problemi al sistema"
                                                    : "Dismetti periferica";
                                                $slaveStatusBadge = $slaveOnline485
                                                    ? '<span class="badge bg-success">Online</span>'
                                                    : '<span class="badge bg-danger">Offline 485</span>';
                                                $pressureText = $slaveOnline485 ? ($slave['pressure'] . ' Pa') : '--';
                                                $temperatureText = $slaveOnline485 ? ($slave['temperature'] . ' °C') : '--';
                                            ?>
                                            <tr>
                                                <td><span class="badge bg-info text-dark">Pressione</span></td>
                                                <td>
                                                    <strong>
                                                        <a href="serial_detail.php?serial=<?php echo urlencode((string)$slave['slave_sn']); ?>" class="text-decoration-none">
                                                            <?php echo htmlspecialchars($slave['slave_sn']); ?>
                                                        </a>
                                                    </strong>
                                                    <div class="mt-1"><?php echo $slaveStatusBadge; ?></div>
                                                </td>
                                                <td><?php echo $slave['slave_grp']; ?></td>
                                                <td><?php echo $pressureText; ?></td>
                                                <td><?php echo $temperatureText; ?></td>
                                                <td><?php echo htmlspecialchars($slave['fw_version'] ?? 'N/D'); ?></td>
                                                <td><?php echo date('H:i:s', strtotime($slave['recorded_at'])); ?></td>
                                                <td>
                                                    <button class="btn btn-xs btn-outline-dark" type="button" data-bs-toggle="collapse" data-bs-target="#history-<?php echo $slave['slave_sn']; ?>">
                                                        <i class="fas fa-history" title="<?php echo $lang['slave_history_tooltip']; ?>"></i>
                                                    </button>
                                                    <a href="download_csv.php?master_id=<?php echo $m['id']; ?>&slave_sn=<?php echo $slave['slave_sn']; ?>" class="btn btn-xs btn-outline-success" title="<?php echo $lang['slave_download_csv_tooltip']; ?>"><i class="fas fa-download"></i></a>
                                                    <?php if ($canSerialLifecycle): ?>
                                                        <button class="btn btn-xs <?php echo $trashBtnClass; ?>"
                                                                title="<?php echo htmlspecialchars($trashTitle, ENT_QUOTES); ?>"
                                                                onclick="openRetireSlaveModal(
                                                                    <?php echo (int)$m['id']; ?>,
                                                                    '<?php echo htmlspecialchars((string)$slave['slave_sn'], ENT_QUOTES); ?>',
                                                                    <?php echo $slaveOnline485 ? 'true' : 'false'; ?>
                                                                )">
                                                            <i class="fas fa-trash"></i>
                                                        </button>
                                                    <?php else: ?>
                                                        <button class="btn btn-xs btn-outline-secondary" disabled title="Dismissione seriali non abilitata per la tua utenza">
                                                            <i class="fas fa-trash"></i>
                                                        </button>
                                                    <?php endif; ?>
                                                    <button class="btn btn-xs <?php echo $slaveBtnClass; ?>"
                                                            title="<?php echo !$canFirmwareUpdate ? 'Aggiornamenti firmware non abilitati' : ($slaveFwAhead ? 'Firmware slave locale piu recente di quello attivo a portale' : ($slaveOnline485 ? sprintf($lang['slave_update_tooltip'], $latestFwSlaveP) : 'Slave offline: aggiornamento non disponibile')); ?>"
                                                            onclick="startSlaveUpdate(<?php echo $m['id']; ?>, '<?php echo $slave['slave_sn']; ?>', '<?php echo $latestFwSlaveP; ?>')"
                                                            <?php if(!$canSlaveUpdate) echo 'disabled'; ?>>
                                                        <i class="fas fa-sync"></i>
                                                    </button>
                                                </td>
                                            </tr>
                                            <!-- Riga Storico Slave (Nascosta) -->
                                            <tr>
                                                <td colspan="8" class="p-0 border-0">
                                                    <div class="collapse bg-light p-2 ps-4" id="history-<?php echo $slave['slave_sn']; ?>">
                                                        <small class="text-muted fw-bold">Ultimi 10 rilevamenti per <?php echo $slave['slave_sn']; ?>:</small>
                                                        <table class="table table-xs table-borderless mb-0 text-muted">
                                                            <?php 
                                                                // Query leggera per lo storico specifico
                                                                $stmtHist = $pdo->prepare("SELECT recorded_at, pressure, temperature FROM measurements WHERE master_id = ? AND slave_sn = ? ORDER BY recorded_at DESC LIMIT 10");
                                                                $stmtHist->execute([$m['id'], $slave['slave_sn']]);
                                                                foreach($stmtHist->fetchAll() as $h):
                                                            ?>
                                                            <tr>
                                                                <td width="20%"><?php echo date('d/m H:i:s', strtotime($h['recorded_at'])); ?></td>
                                                                <td width="20%">P: <?php echo $h['pressure']; ?> Pa</td>
                                                                <td width="20%">T: <?php echo $h['temperature']; ?> °C</td>
                                                                <td></td>
                                                            </tr>
                                                            <?php endforeach; ?>
                                                        </table>
                                                    </div>
                                                </td>
                                            </tr>
                                            <?php endforeach; ?>
                                            <?php if(empty($slavesList)) echo "<tr><td colspan='8' class='text-center text-muted'>Nessuna periferica rilevata.</td></tr>"; ?>
                                        </tbody>
                                    </table>
                                    <?php endif; ?>
                                </div>
                            </div>
                        </td>
                    </tr>
                    <?php endforeach; ?>
                </tbody>
            </table>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>

<!-- MODAL AGGIORNAMENTO FIRMWARE -->
<div class="modal fade" id="updateModal" data-bs-backdrop="static" data-bs-keyboard="false" tabindex="-1">
  <div class="modal-dialog modal-dialog-centered">
    <div class="modal-content">
      <div class="modal-header bg-primary text-white">
        <h5 class="modal-title"><i class="fas fa-sync"></i> Aggiornamento Firmware</h5>
      </div>
      <div class="modal-body text-center py-4">
        <h4 id="updateStatusTitle" class="mb-3">Richiesta in corso...</h4>
        
        <!-- Progress Steps -->
        <div class="d-flex justify-content-between mb-4 px-4 position-relative">
            <div class="position-absolute top-50 start-0 w-100 translate-middle-y bg-secondary" style="height: 2px; z-index: 0;"></div>
            
            <div class="bg-white position-relative z-1 p-1">
                <i class="fas fa-hourglass-start fa-2x text-primary" id="iconStep1"></i>
                <div class="small mt-1">Attesa</div>
            </div>
            <div class="bg-white position-relative z-1 p-1">
                <i class="fas fa-cloud-download-alt fa-2x text-muted" id="iconStep2"></i>
                <div class="small mt-1">Download</div>
            </div>
            <div class="bg-white position-relative z-1 p-1">
                <i class="fas fa-check-circle fa-2x text-muted" id="iconStep3"></i>
                <div class="small mt-1">Finito</div>
            </div>
        </div>

        <div class="progress mb-3" style="height: 20px;">
            <div id="updateProgressBar" class="progress-bar progress-bar-striped progress-bar-animated" role="progressbar" style="width: 0%"></div>
        </div>
        
        <p id="updateStatusText" class="text-muted">Contatto il dispositivo...</p>
        <div id="updateErrorMsg" class="alert alert-danger d-none"></div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-secondary disabled" id="btnCloseModal" onclick="location.reload()">Chiudi</button>
      </div>
    </div>
  </div>
</div>

<!-- MODAL CAMBIO SERIALE -->
<div class="modal fade" id="serialModal" tabindex="-1">
  <div class="modal-dialog modal-dialog-centered">
    <div class="modal-content">
      <div class="modal-header bg-danger text-white">
        <h5 class="modal-title"><i class="fas fa-exchange-alt"></i> Sostituzione Master</h5>
      </div>
      <div class="modal-body">
        <p>E' stato rilevato un nuovo seriale per questo impianto:</p>
        <div class="border rounded p-2 mb-3">
            <div class="text-danger fw-bold">DB: <span id="oldSerialDisplay">-</span></div>
            <div class="text-success fw-bold">LIVE: <span id="newSerialDisplay">-</span></div>
        </div>
        <p class="text-muted small">Confermando, il vecchio seriale verra' dismesso e l'impianto sara' associato alla nuova scheda.</p>
        <div class="mb-3">
            <label for="replaceReasonCode" class="form-label">Motivazione dismissione vecchia master</label>
            <select class="form-select" id="replaceReasonCode"></select>
        </div>
        <div class="mb-0">
            <label for="replaceReasonDetails" class="form-label">Dettagli (opzionale)</label>
            <input type="text" class="form-control" id="replaceReasonDetails" placeholder="Nota tecnica / ticket">
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Annulla</button>
        <button type="button" class="btn btn-danger" onclick="confirmSerialChange()">Conferma sostituzione</button>
      </div>
    </div>
  </div>
</div>
<!-- MODAL DISMISSIONE SERIALI -->
<div class="modal fade" id="retireSerialModal" tabindex="-1">
  <div class="modal-dialog modal-dialog-centered">
    <div class="modal-content">
      <div class="modal-header bg-danger text-white">
        <h5 class="modal-title"><i class="fas fa-trash"></i> Dismissione seriale</h5>
      </div>
      <div class="modal-body">
        <div class="small text-muted">Target:</div>
        <div class="fw-bold mb-2" id="retireTargetLabel">-</div>
        <div class="alert alert-warning py-2 d-none" id="retireOnlineWarn">
            La periferica risulta online e funzionante. Procedi solo se sei certo della dismissione.
        </div>
        <div class="mb-3">
            <label for="retireReasonCode" class="form-label">Motivazione</label>
            <select class="form-select" id="retireReasonCode"></select>
        </div>
        <div class="mb-0">
            <label for="retireReasonDetails" class="form-label">Dettagli (opzionale)</label>
            <input type="text" class="form-control" id="retireReasonDetails" placeholder="Nota tecnica / ticket">
        </div>
        <div class="mb-0 mt-3">
            <label for="retireReplacedBySerial" class="form-label">Replaced by (opzionale)</label>
            <input type="text" class="form-control" id="retireReplacedBySerial" placeholder="Es. 202602050004">
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Annulla</button>
        <button type="button" class="btn btn-danger" onclick="confirmRetireSerial()" <?php echo $canSerialLifecycle ? '' : 'disabled'; ?>>Conferma dismissione</button>
      </div>
    </div>
  </div>
</div>
<script>
const USER_CAN_FIRMWARE_UPDATE = <?php echo $canFirmwareUpdate ? 'true' : 'false'; ?>;
const USER_CAN_SERIAL_LIFECYCLE = <?php echo $canSerialLifecycle ? 'true' : 'false'; ?>;
let pollInterval;
let currentMasterId;
let targetVersion;
let updateType = ''; // 'master' o 'slave'
let currentSlaveSn = '';
let pollErrors = 0;

function resetUpdateModalUi(title, text) {
    document.getElementById('updateStatusTitle').innerText = title;
    document.getElementById('updateStatusText').innerText = text;
    document.getElementById('updateProgressBar').style.width = "10%";
    document.getElementById('updateProgressBar').className = "progress-bar progress-bar-striped progress-bar-animated";
    document.getElementById('updateErrorMsg').classList.add('d-none');
    document.getElementById('updateErrorMsg').innerText = '';
    document.getElementById('iconStep1').className = "fas fa-hourglass-start fa-2x text-primary";
    document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-muted";
    document.getElementById('iconStep3').className = "fas fa-check-circle fa-2x text-muted";
    document.getElementById('btnCloseModal').className = "btn btn-secondary disabled";
}

function startUpdate(masterId, version) {
    if (!USER_CAN_FIRMWARE_UPDATE) {
        alert("La tua utenza non e abilitata agli aggiornamenti firmware.");
        return;
    }
    if(!confirm("Avviare l'aggiornamento firmware alla versione " + version + "?\nIl dispositivo si riavviera'.")) return;

    clearInterval(pollInterval);
    currentMasterId = masterId;
    targetVersion = version;
    updateType = 'master';
    currentSlaveSn = '';
    pollErrors = 0;

    const modal = new bootstrap.Modal(document.getElementById('updateModal'));
    resetUpdateModalUi("Richiesta aggiornamento Master", "Invio comando al dispositivo...");
    modal.show();

    fetch('api_command.php', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ action: 'request_update', master_id: masterId })
    })
    .then(res => res.json())
    .then(data => {
        if(data.status === 'ok') {
            pollInterval = setInterval(checkStatus, 2000);
        } else {
            showError("Errore invio comando: " + data.message);
        }
    })
    .catch(() => showError("Errore di rete durante invio comando."));
}

function startSlaveUpdate(masterId, slaveSn, version) {
    if (!USER_CAN_FIRMWARE_UPDATE) {
        alert("La tua utenza non e abilitata agli aggiornamenti firmware.");
        return;
    }
    if(!confirm("Avviare l'aggiornamento per lo SLAVE " + slaveSn + " alla versione " + version + "?\nL'operazione verra' gestita dal Master associato.")) return;

    clearInterval(pollInterval);
    currentMasterId = masterId;
    targetVersion = version;
    updateType = 'slave';
    currentSlaveSn = slaveSn;
    pollErrors = 0;

    const modal = new bootstrap.Modal(document.getElementById('updateModal'));
    resetUpdateModalUi("Aggiornamento Slave " + slaveSn, "Invio comando al master...");
    modal.show();

    fetch('api_command.php', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ action: 'request_slave_update', master_id: masterId, slave_sn: slaveSn })
    })
    .then(res => res.json())
    .then(data => {
        if (data.status === 'ok') {
            pollInterval = setInterval(checkStatus, 3000);
        } else {
            showError("Errore invio comando slave: " + (data.message || "Errore sconosciuto"));
        }
    })
    .catch(() => showError("Errore di rete durante invio comando slave."));
}

function checkStatus() {
    fetch('api_check_status.php?master_id=' + currentMasterId)
    .then(res => res.json())
    .then(data => {
        pollErrors = 0;
        if (updateType === 'slave') {
            const slaveStatus = data.slave_ota_status;
            const slaveMsg = data.slave_ota_message || '';
            const slaveSnForTitle = currentSlaveSn || data.slave_update_request_sn || 'N/D';

            document.getElementById('updateStatusTitle').innerText = "Aggiornamento Slave " + slaveSnForTitle;
            let progress = 10;
            let statusText = "Richiesta inviata...";

            if (slaveStatus === 'Pending') {
                progress = 25;
                statusText = "In attesa che il Master riceva il comando...";
                document.getElementById('iconStep1').className = "fas fa-hourglass-start fa-2x text-primary";
            } else if (slaveStatus === 'InProgress') {
                progress = 30;
                statusText = "Richiesta presa in carico dal Master...";
            } else if (slaveStatus === 'Downloading') {
                progress = 35;
                statusText = slaveMsg || "Download firmware sulla Master in corso...";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
            } else if (slaveStatus === 'Downloaded') {
                progress = 40;
                statusText = slaveMsg || "Download completato. Avvio trasferimento RS485...";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
            } else if (slaveStatus === 'Handshake') {
                progress = 45;
                statusText = "Il Master sta contattando lo Slave...";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
            } else if (slaveStatus === 'Sending data') {
                progress = 50;
                statusText = "Trasferimento del firmware in corso...";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
            } else if (slaveStatus === 'Uploading') {
                let pct = parseInt(slaveMsg);
                if (isNaN(pct)) pct = 50;
                progress = 50 + (pct / 2);
                statusText = "Trasferimento dati: " + pct + "%";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
            } else if (slaveStatus === 'Finalizing') {
                progress = 95;
                statusText = "Finalizzazione e riavvio dello Slave...";
            } else if (slaveStatus === 'Success') {
                clearInterval(pollInterval);
                document.getElementById('updateStatusTitle').innerText = "Aggiornamento Slave Completato!";
                document.getElementById('updateStatusText').innerText = slaveMsg || ("Lo slave " + slaveSnForTitle + " e' stato aggiornato.");
                document.getElementById('updateProgressBar').style.width = "100%";
                document.getElementById('updateProgressBar').className = "progress-bar bg-success";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-success";
                document.getElementById('iconStep3').className = "fas fa-check-circle fa-2x text-success";
                document.getElementById('btnCloseModal').classList.remove('disabled');
                document.getElementById('btnCloseModal').className = "btn btn-success";
                return;
            } else if (slaveStatus === 'Failed') {
                showError("Aggiornamento Slave Fallito: " + slaveMsg);
                return;
            } else if (!slaveStatus) {
                statusText = "In attesa dei primi aggiornamenti di stato...";
            }
            document.getElementById('updateStatusText').innerText = statusText;
            document.getElementById('updateProgressBar').style.width = progress + "%";

        } else if (updateType === 'master') {
            const masterStatus = data.ota_status;
            const masterMsg = data.ota_message;
            const masterVer = data.fw_version;

            if (masterStatus === 'Pending') {
                document.getElementById('updateStatusTitle').innerText = "In Attesa del Dispositivo";
                document.getElementById('updateStatusText').innerText = "Il dispositivo ricevera' il comando al prossimo controllo (max 2 min)...";
                document.getElementById('updateProgressBar').style.width = "30%";
            } else if (masterStatus === 'InProgress') {
                document.getElementById('updateStatusTitle').innerText = "Aggiornamento in Corso";
                document.getElementById('updateStatusText').innerText = "Il dispositivo sta scaricando e installando il firmware...";
                document.getElementById('updateProgressBar').style.width = "70%";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
            } else if (masterStatus === 'Success' || masterVer === targetVersion) {
                clearInterval(pollInterval);
                document.getElementById('updateStatusTitle').innerText = "Aggiornamento Completato!";
                if (masterVer === targetVersion) {
                    document.getElementById('updateStatusText').innerText = "Il dispositivo e' ora aggiornato alla v" + masterVer;
                } else {
                    document.getElementById('updateStatusText').innerText = masterMsg || "Firmware aggiornato correttamente. In attesa riallineamento versione in dashboard.";
                }
                document.getElementById('updateProgressBar').style.width = "100%";
                document.getElementById('updateProgressBar').className = "progress-bar bg-success";
                document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-success";
                document.getElementById('iconStep3').className = "fas fa-check-circle fa-2x text-success";
                document.getElementById('btnCloseModal').classList.remove('disabled');
                document.getElementById('btnCloseModal').className = "btn btn-success";
            } else if (masterStatus === 'Failed') {
                showError("Aggiornamento Fallito: " + (masterMsg || "Errore non specificato."));
            }
        }
    })
    .catch(() => {
        pollErrors++;
        if (pollErrors >= 4) {
            showError("Errore di comunicazione durante il controllo stato.");
        } else {
            document.getElementById('updateStatusText').innerText = "Connessione instabile, ritento... (" + pollErrors + "/4)";
        }
    });
}

function showError(msg) {
    clearInterval(pollInterval);
    document.getElementById('updateStatusTitle').innerText = "Errore";
    document.getElementById('updateProgressBar').className = "progress-bar bg-danger";
    document.getElementById('updateErrorMsg').innerText = msg;
    document.getElementById('updateErrorMsg').classList.remove('d-none');
    document.getElementById('btnCloseModal').classList.remove('disabled');
}

// --- GESTIONE CAMBIO SERIALE ---
const RETIRE_REASONS = <?php echo json_encode($retireReasons, JSON_UNESCAPED_UNICODE); ?>;
let serialModalInstance;
let retireModalInstance;
let pendingSerialChange = { id: 0, oldSerial: '', newSerial: '' };
let pendingRetire = { type: '', masterId: 0, serial: '', replacedBy: '', online: false };

function reasonLabel(item) {
    const lang = document.documentElement.lang || 'it';
    if (lang === 'it' && item.label_it) return item.label_it;
    if (lang !== 'it' && item.label_en) return item.label_en;
    return item.reason_code || '';
}

function fillReasonSelect(selectEl, defaultCode) {
    if (!selectEl) return;
    selectEl.innerHTML = '';
    RETIRE_REASONS.forEach((r) => {
        const opt = document.createElement('option');
        opt.value = r.reason_code;
        opt.textContent = `${r.reason_code} - ${reasonLabel(r)}`;
        selectEl.appendChild(opt);
    });
    if (defaultCode) {
        selectEl.value = defaultCode;
    }
    if (!selectEl.value && selectEl.options.length > 0) {
        selectEl.selectedIndex = 0;
    }
}

function replaceSerial(masterId, oldSerial, newSerial) {
    if (!USER_CAN_SERIAL_LIFECYCLE) {
        alert("La tua utenza non e abilitata alla gestione lifecycle seriali.");
        return;
    }
    pendingSerialChange = { id: masterId, oldSerial: oldSerial || '', newSerial: newSerial || '' };
    document.getElementById('oldSerialDisplay').innerText = pendingSerialChange.oldSerial || '-';
    document.getElementById('newSerialDisplay').innerText = pendingSerialChange.newSerial || '-';
    document.getElementById('replaceReasonDetails').value = '';
    fillReasonSelect(document.getElementById('replaceReasonCode'), 'master_replaced');
    serialModalInstance.show();
}

function openRetireSlaveModal(masterId, slaveSn, isOnline) {
    if (!USER_CAN_SERIAL_LIFECYCLE) {
        alert("La tua utenza non e abilitata alla gestione lifecycle seriali.");
        return;
    }
    pendingRetire = { type: 'slave', masterId: masterId, serial: slaveSn || '', replacedBy: '', online: !!isOnline };
    document.getElementById('retireTargetLabel').innerText = `Slave ${pendingRetire.serial}`;
    const warn = document.getElementById('retireOnlineWarn');
    if (warn) {
        if (pendingRetire.online) warn.classList.remove('d-none');
        else warn.classList.add('d-none');
    }
    document.getElementById('retireReasonDetails').value = '';
    document.getElementById('retireReplacedBySerial').value = '';
    fillReasonSelect(document.getElementById('retireReasonCode'), 'field_replaced');
    retireModalInstance.show();
}

function openRetireMasterModal(masterId, oldSerial, detectedSerial) {
    if (!USER_CAN_SERIAL_LIFECYCLE) {
        alert("La tua utenza non e abilitata alla gestione lifecycle seriali.");
        return;
    }
    pendingRetire = { type: 'master', masterId: masterId, serial: oldSerial || '', replacedBy: detectedSerial || '', online: false };
    document.getElementById('retireTargetLabel').innerText = `Master ${pendingRetire.serial}`;
    const warn = document.getElementById('retireOnlineWarn');
    if (warn) warn.classList.add('d-none');
    document.getElementById('retireReasonDetails').value = '';
    document.getElementById('retireReplacedBySerial').value = pendingRetire.replacedBy || '';
    fillReasonSelect(document.getElementById('retireReasonCode'), 'damaged');
    retireModalInstance.show();
}

function confirmSerialChange() {
    const reasonCode = (document.getElementById('replaceReasonCode').value || '').trim();
    const reasonDetails = (document.getElementById('replaceReasonDetails').value || '').trim();
    if (!pendingSerialChange.id || !pendingSerialChange.newSerial) {
        alert('Dati sostituzione non validi.');
        return;
    }
    if (!reasonCode) {
        alert('Seleziona una motivazione.');
        return;
    }

    fetch('api_command.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            action: 'update_serial',
            master_id: pendingSerialChange.id,
            old_serial: pendingSerialChange.oldSerial,
            new_serial: pendingSerialChange.newSerial,
            reason_code: reasonCode,
            reason_details: reasonDetails
        })
    })
    .then(res => res.json())
    .then(data => {
        if (data.status === 'ok') {
            alert('Seriale master aggiornato con successo.');
            location.reload();
        } else {
            alert('Errore: ' + (data.message || 'Errore sconosciuto'));
        }
    })
    .catch(() => alert('Errore di rete durante sostituzione seriale.'));
}

function confirmRetireSerial() {
    if (!USER_CAN_SERIAL_LIFECYCLE) {
        alert('Gestione seriali non abilitata per la tua utenza.');
        return;
    }
    const reasonCode = (document.getElementById('retireReasonCode').value || '').trim();
    const reasonDetails = (document.getElementById('retireReasonDetails').value || '').trim();
    const replacedBySerial = (document.getElementById('retireReplacedBySerial').value || '').trim();
    if (pendingRetire.online) {
        const proceed = confirm('La periferica risulta online e funzionante. Confermi comunque la dismissione?');
        if (!proceed) return;
    }
    if (!pendingRetire.masterId || !pendingRetire.serial) {
        alert('Dati dismissione non validi.');
        return;
    }
    if (!reasonCode) {
        alert('Seleziona una motivazione.');
        return;
    }

    const payload = {
        master_id: pendingRetire.masterId,
        reason_code: reasonCode,
        reason_details: reasonDetails,
        replaced_by_serial: replacedBySerial
    };

    if (pendingRetire.type === 'slave') {
        payload.action = 'retire_slave_serial';
        payload.slave_sn = pendingRetire.serial;
    } else {
        payload.action = 'retire_master_serial';
        payload.serial_number = pendingRetire.serial;
        if (!payload.replaced_by_serial) {
            payload.replaced_by_serial = pendingRetire.replacedBy || '';
        }
    }

    fetch('api_command.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    })
    .then(res => res.json())
    .then(data => {
        if (data.status === 'ok') {
            alert('Dismissione completata.');
            location.reload();
        } else {
            alert('Errore: ' + (data.message || 'Errore sconosciuto'));
        }
    })
    .catch(() => alert('Errore di rete durante dismissione.'));
}

const DASHBOARD_FILTER_IDS = [
    'filterStatus',
    'filterOwner',
    'filterPlantName',
    'filterAddress',
    'filterBuilder',
    'filterMaintainer',
    'filterFirmware',
    'filterPlantKind',
    'filterLastSeenFrom',
    'filterLastSeenTo',
    'filterCreatedFrom',
    'filterCreatedTo',
    'filterMasterSerial'
];

function normalizeFilterText(value) {
    return String(value || '').trim().toLowerCase();
}

function parseFilterDate(value, endOfDay = false) {
    const raw = String(value || '').trim();
    if (!raw) return null;
    const ts = Date.parse(raw + (endOfDay ? 'T23:59:59' : 'T00:00:00'));
    return Number.isNaN(ts) ? null : ts;
}

function rowDateMatches(rawValue, fromValue, toValue) {
    if (!fromValue && !toValue) {
        return true;
    }
    const raw = String(rawValue || '').trim();
    if (!raw) {
        return false;
    }
    const rowTs = Date.parse(raw.replace(' ', 'T'));
    if (Number.isNaN(rowTs)) {
        return false;
    }
    const fromTs = parseFilterDate(fromValue, false);
    const toTs = parseFilterDate(toValue, true);
    if (fromTs !== null && rowTs < fromTs) {
        return false;
    }
    if (toTs !== null && rowTs > toTs) {
        return false;
    }
    return true;
}

function currentPlantFilters() {
    const filters = {};
    DASHBOARD_FILTER_IDS.forEach((id) => {
        const el = document.getElementById(id);
        filters[id] = el ? String(el.value || '') : '';
    });
    return filters;
}

function hasActivePlantFilters(filters) {
    return Object.values(filters || {}).some((value) => String(value || '').trim() !== '');
}

function applyPlantFilters() {
    const filters = currentPlantFilters();
    const rows = Array.from(document.querySelectorAll('tr.plant-row'));
    let visibleCount = 0;

    rows.forEach((row) => {
        const detailRow = document.querySelector(`tr.plant-detail-row[data-detail-for="${row.dataset.masterId}"]`);
        const matches =
            (!filters.filterStatus || (filters.filterStatus === 'online' ? row.dataset.online === '1' : row.dataset.online !== '1')) &&
            (!filters.filterOwner || row.dataset.ownerId === filters.filterOwner) &&
            (!filters.filterBuilder || row.dataset.builderId === filters.filterBuilder) &&
            (!filters.filterPlantKind || row.dataset.plantKind === filters.filterPlantKind) &&
            (!normalizeFilterText(filters.filterPlantName) || normalizeFilterText(row.dataset.plantName).includes(normalizeFilterText(filters.filterPlantName))) &&
            (!normalizeFilterText(filters.filterAddress) || normalizeFilterText(row.dataset.address).includes(normalizeFilterText(filters.filterAddress))) &&
            (!normalizeFilterText(filters.filterMaintainer) || normalizeFilterText(row.dataset.maintainerName).includes(normalizeFilterText(filters.filterMaintainer))) &&
            (!normalizeFilterText(filters.filterFirmware) || normalizeFilterText(row.dataset.firmware).includes(normalizeFilterText(filters.filterFirmware))) &&
            (!normalizeFilterText(filters.filterMasterSerial) || normalizeFilterText(row.dataset.serial).includes(normalizeFilterText(filters.filterMasterSerial))) &&
            rowDateMatches(row.dataset.lastSeen, filters.filterLastSeenFrom, filters.filterLastSeenTo) &&
            rowDateMatches(row.dataset.createdAt, filters.filterCreatedFrom, filters.filterCreatedTo);

        row.classList.toggle('plant-hidden', !matches);
        if (detailRow) {
            detailRow.classList.toggle('plant-hidden', !matches);
        }
        if (matches) {
            visibleCount++;
        }
    });

    const counter = document.getElementById('filterVisibleCount');
    if (counter) {
        counter.textContent = String(visibleCount);
    }
}

document.addEventListener('DOMContentLoaded', function () {
    const DASHBOARD_UI_STATE_KEY = 'antralux_dashboard_ui_state_v1';
    const DASHBOARD_FILTER_STATE_KEY = 'antralux_dashboard_filter_state_v1';
    const filterCollapseEl = document.getElementById('dashboardFiltersCollapse');
    const filterCollapse = filterCollapseEl ? bootstrap.Collapse.getOrCreateInstance(filterCollapseEl, { toggle: false }) : null;

    function saveDashboardUiState() {
        try {
            const openCollapseIds = Array.from(document.querySelectorAll('.collapse.show[id]'))
                .map(el => el.id)
                .filter(Boolean);
            const state = {
                openCollapseIds: openCollapseIds,
                scrollY: Math.max(0, Math.floor(window.scrollY || 0)),
                savedAt: Date.now()
            };
            sessionStorage.setItem(DASHBOARD_UI_STATE_KEY, JSON.stringify(state));
        } catch (e) {
            // No-op: se sessionStorage non e disponibile, fallback a refresh classico.
        }
    }

    function restoreDashboardUiState() {
        let rawState = null;
        try {
            rawState = sessionStorage.getItem(DASHBOARD_UI_STATE_KEY);
            if (!rawState) return;
        } catch (e) {
            return;
        }

        let state = null;
        try {
            state = JSON.parse(rawState);
        } catch (e) {
            state = null;
        }

        try {
            sessionStorage.removeItem(DASHBOARD_UI_STATE_KEY);
        } catch (e) {
            // ignore
        }

        if (!state || typeof state !== 'object') return;
        if ((Date.now() - (Number(state.savedAt) || 0)) > 5 * 60 * 1000) return;

        const openIds = Array.isArray(state.openCollapseIds) ? state.openCollapseIds : [];
        openIds.forEach(id => {
            const el = document.getElementById(id);
            if (!el) return;
            try {
                bootstrap.Collapse.getOrCreateInstance(el, { toggle: false }).show();
            } catch (e) {
                // ignore single collapse errors
            }
        });

        const scrollY = Number(state.scrollY);
        if (Number.isFinite(scrollY) && scrollY > 0) {
            setTimeout(() => window.scrollTo(0, scrollY), 120);
        }
    }

    function saveDashboardFilterState() {
        try {
            sessionStorage.setItem(DASHBOARD_FILTER_STATE_KEY, JSON.stringify(currentPlantFilters()));
        } catch (e) {
            // ignore filter persistence errors
        }
    }

    function restoreDashboardFilterState() {
        let rawState = null;
        try {
            rawState = sessionStorage.getItem(DASHBOARD_FILTER_STATE_KEY);
        } catch (e) {
            rawState = null;
        }
        if (!rawState) {
            return;
        }
        try {
            const state = JSON.parse(rawState);
            DASHBOARD_FILTER_IDS.forEach((id) => {
                const el = document.getElementById(id);
                if (el && Object.prototype.hasOwnProperty.call(state, id)) {
                    el.value = String(state[id] || '');
                }
            });
        } catch (e) {
            // ignore invalid state
        }
    }

    serialModalInstance = new bootstrap.Modal(document.getElementById('serialModal'));
    retireModalInstance = new bootstrap.Modal(document.getElementById('retireSerialModal'));
    restoreDashboardUiState();
    restoreDashboardFilterState();
    if (filterCollapse && hasActivePlantFilters(currentPlantFilters())) {
        filterCollapse.show();
    }
    applyPlantFilters();

    DASHBOARD_FILTER_IDS.forEach((id) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.addEventListener('input', function () {
            applyPlantFilters();
            saveDashboardFilterState();
        });
        el.addEventListener('change', function () {
            applyPlantFilters();
            saveDashboardFilterState();
        });
    });

    const resetFiltersBtn = document.getElementById('resetPlantFilters');
    if (resetFiltersBtn) {
        resetFiltersBtn.addEventListener('click', function () {
            DASHBOARD_FILTER_IDS.forEach((id) => {
                const el = document.getElementById(id);
                if (el) {
                    el.value = '';
                }
            });
            applyPlantFilters();
            saveDashboardFilterState();
        });
    }

    // Auto-refresh dashboard per mostrare nuove schede/stati senza refresh manuale.
    const AUTO_REFRESH_MS = 15000;
    setInterval(function () {
        if (document.hidden) return;
        const updateModalEl = document.getElementById('updateModal');
        if (updateModalEl && updateModalEl.classList.contains('show')) return;
        if (document.querySelector('.modal.show')) return; // Non interrompere modali aperti.
        if (document.querySelector('#dashboardFiltersCollapse.show')) return;
        if (document.querySelector('div[id^="details-"].collapse.show')) return;
        if (document.querySelector('div[id^="history-"].collapse.show')) return;
        saveDashboardUiState();
        window.location.reload();
    }, AUTO_REFRESH_MS);
});
</script>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>

