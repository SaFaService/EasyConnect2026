<?php
session_start();
require 'config.php';
require_once 'auth_common.php';

// Includi il gestore della lingua
require 'lang.php';

// Protezione: se l'utente non Ã¨ loggato, lo rimanda alla pagina di login.
if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';

// Definiamo i ruoli per una lettura piÃ¹ chiara del codice
$isAdmin = ($_SESSION['user_role'] === 'admin');
$isBuilder = ($_SESSION['user_role'] === 'builder');
$isMaintainer = ($_SESSION['user_role'] === 'maintainer');
$isClient = ($_SESSION['user_role'] === 'client');
$currentUserId = $_SESSION['user_id'];
$currentUserAccessLevel = ecAuthCurrentUserAccessLevel($pdo, (int)$currentUserId);
$currentUserPermissions = ecAuthCurrentUserPermissions($pdo, (int)$currentUserId);
$canCreatePlants = !empty($currentUserPermissions['plant_create']);
$canSerialLifecycle = !empty($currentUserPermissions['serial_lifecycle']);

/**
 * Verifica se una tabella esiste nello schema corrente.
 */
function ecSettingsTableExists(PDO $pdo, string $tableName): bool {
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

/**
 * Verifica se una colonna esiste in una tabella.
 */
function ecSettingsColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
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

/**
 * Ritorna le motivazioni disponibili per dismissione/annullamento.
 */
function ecSettingsLifecycleReasons(PDO $pdo, bool $onlyActive = true): array {
    $fallback = [
        ['id' => 0, 'reason_code' => 'field_replaced', 'label_it' => 'Sostituzione in campo', 'label_en' => 'Field replacement', 'applies_to_status' => 'retired', 'sort_order' => 40, 'is_active' => 1],
        ['id' => 0, 'reason_code' => 'damaged', 'label_it' => 'Dismesso per guasto', 'label_en' => 'Dismissed due to fault', 'applies_to_status' => 'retired', 'sort_order' => 50, 'is_active' => 1],
        ['id' => 0, 'reason_code' => 'plant_dismission', 'label_it' => 'Impianto dismesso', 'label_en' => 'Plant decommissioned', 'applies_to_status' => 'retired', 'sort_order' => 60, 'is_active' => 1],
        ['id' => 0, 'reason_code' => 'master_replaced', 'label_it' => 'Sostituito da altro seriale', 'label_en' => 'Replaced by another serial', 'applies_to_status' => 'retired', 'sort_order' => 70, 'is_active' => 1],
        ['id' => 0, 'reason_code' => 'wrong_product_type', 'label_it' => 'Tipo prodotto errato', 'label_en' => 'Wrong product type', 'applies_to_status' => 'voided', 'sort_order' => 10, 'is_active' => 1],
        ['id' => 0, 'reason_code' => 'wrong_flashing', 'label_it' => 'Programmazione errata', 'label_en' => 'Wrong flashing', 'applies_to_status' => 'voided', 'sort_order' => 20, 'is_active' => 1],
    ];

    if (!ecSettingsTableExists($pdo, 'serial_status_reasons')) {
        return $fallback;
    }

    try {
        $sql = "
            SELECT id, reason_code, label_it, label_en, applies_to_status, sort_order, is_active
            FROM serial_status_reasons
        ";
        if ($onlyActive) {
            $sql .= " WHERE is_active = 1 ";
        }
        $sql .= " ORDER BY is_active DESC, sort_order ASC, reason_code ASC";
        $rows = $pdo->query($sql)->fetchAll();
        return !empty($rows) ? $rows : $fallback;
    } catch (Throwable $e) {
        return $fallback;
    }
}

/**
 * Disattiva un seriale impostandolo a retired.
 * Se la tabella device_serials non esiste, la funzione non genera errore bloccante.
 */
function ecSettingsRetireSerial(
    PDO $pdo,
    string $serial,
    string $reasonCode,
    string $reasonDetails,
    int $actorUserId,
    ?string $replacedBySerial = null,
    ?int $masterId = null
): array {
    if ($serial === '') {
        return ['updated' => false, 'reason' => 'empty'];
    }
    if (!ecSettingsTableExists($pdo, 'device_serials')) {
        return ['updated' => false, 'reason' => 'device_serials_missing'];
    }

    $stmt = $pdo->prepare("SELECT * FROM device_serials WHERE serial_number = ? FOR UPDATE");
    $stmt->execute([$serial]);
    $row = $stmt->fetch();
    if (!$row) {
        return ['updated' => false, 'reason' => 'not_found'];
    }

    $cols = [
        'status_reason_code' => ecSettingsColumnExists($pdo, 'device_serials', 'status_reason_code'),
        'status_notes' => ecSettingsColumnExists($pdo, 'device_serials', 'status_notes'),
        'replaced_by_serial' => ecSettingsColumnExists($pdo, 'device_serials', 'replaced_by_serial'),
        'status_changed_at' => ecSettingsColumnExists($pdo, 'device_serials', 'status_changed_at'),
        'status_changed_by_user_id' => ecSettingsColumnExists($pdo, 'device_serials', 'status_changed_by_user_id'),
        'deactivated_at' => ecSettingsColumnExists($pdo, 'device_serials', 'deactivated_at'),
    ];

    $setParts = ["status = 'retired'"];
    $params = [];
    if ($cols['status_reason_code']) {
        $setParts[] = "status_reason_code = ?";
        $params[] = $reasonCode;
    }
    if ($cols['status_notes']) {
        $setParts[] = "status_notes = ?";
        $params[] = $reasonDetails !== '' ? $reasonDetails : 'Dismissione da gestione impianti';
    }
    if ($cols['replaced_by_serial']) {
        $setParts[] = "replaced_by_serial = ?";
        $params[] = $replacedBySerial;
    }
    if ($cols['status_changed_at']) {
        $setParts[] = "status_changed_at = NOW()";
    }
    if ($cols['status_changed_by_user_id']) {
        $setParts[] = "status_changed_by_user_id = ?";
        $params[] = $actorUserId;
    }
    if ($cols['deactivated_at']) {
        $setParts[] = "deactivated_at = COALESCE(deactivated_at, NOW())";
    }
    $params[] = $row['id'];

    $sql = "UPDATE device_serials SET " . implode(', ', $setParts) . " WHERE id = ?";
    $upd = $pdo->prepare($sql);
    $upd->execute($params);

    if (ecSettingsTableExists($pdo, 'serial_lifecycle_events')) {
        try {
            $insEvent = $pdo->prepare("
                INSERT INTO serial_lifecycle_events (
                    serial_number,
                    from_status,
                    to_status,
                    reason_code,
                    reason_details,
                    replaced_by_serial,
                    actor_user_id,
                    master_id
                ) VALUES (?, ?, 'retired', ?, ?, ?, ?, ?)
            ");
            $insEvent->execute([
                $serial,
                $row['status'] ?? null,
                $reasonCode,
                $reasonDetails !== '' ? $reasonDetails : null,
                $replacedBySerial,
                $actorUserId,
                $masterId
            ]);
        } catch (Throwable $e) {
            // Log evento non bloccante.
        }
    }

    return ['updated' => true, 'reason' => 'ok'];
}

function ecSettingsBoolFromInput($value): int {
    if (is_bool($value)) {
        return $value ? 1 : 0;
    }
    if (is_numeric($value)) {
        return ((int)$value) === 1 ? 1 : 0;
    }
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['1', 'true', 'on', 'yes', 'si'], true) ? 1 : 0;
}

function ecSettingsGeneratePendingApiKey(PDO $pdo): string {
    for ($i = 0; $i < 10; $i++) {
        try {
            $candidate = 'pending_' . bin2hex(random_bytes(28));
        } catch (Throwable $e) {
            $candidate = 'pending_' . substr(hash('sha256', uniqid('pending_api_key_', true)), 0, 56);
        }
        $stmt = $pdo->prepare("SELECT 1 FROM masters WHERE api_key = ? LIMIT 1");
        $stmt->execute([$candidate]);
        if (!$stmt->fetchColumn()) {
            return $candidate;
        }
    }
    return 'pending_' . substr(hash('sha256', uniqid('pending_api_key_fallback_', true)), 0, 56);
}

function ecSettingsIsUsableApiKey($value): bool {
    return (bool)preg_match('/^[a-f0-9]{64}$/i', trim((string)$value));
}

function ecSettingsNormalizePlantKind($value): string {
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['display', 'standalone', 'rewamping'], true) ? $normalized : '';
}

function ecSettingsSerialType(string $serial): string {
    if (preg_match('/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/', $serial, $m)) {
        return (string)$m[1];
    }
    return '';
}

function ecSettingsPlantKindLabel(string $kind): string {
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

function ecSettingsResolvedPlantKind(array $master, bool $hasMasterPlantKindColumn): string {
    $kind = $hasMasterPlantKindColumn ? ecSettingsNormalizePlantKind($master['plant_kind'] ?? '') : '';
    if ($kind !== '') {
        return $kind;
    }

    $serialType = ecSettingsSerialType((string)($master['serial_number'] ?? ''));
    if ($serialType === '01') {
        return 'display';
    }
    return '';
}

function ecSettingsFetchUserAddress(PDO $pdo, ?int $userId, bool $hasUsersAddressColumn): string {
    if (!$hasUsersAddressColumn || $userId === null || $userId <= 0) {
        return '';
    }
    $stmt = $pdo->prepare("SELECT address FROM users WHERE id = ? LIMIT 1");
    $stmt->execute([$userId]);
    return trim((string)($stmt->fetchColumn() ?: ''));
}

function ecSettingsResolvedPlantAddress(PDO $pdo, ?int $ownerUserId, ?int $maintainerUserId, ?int $builderUserId, bool $hasUsersAddressColumn): string {
    $candidateIds = [$ownerUserId, $maintainerUserId, $builderUserId];
    foreach ($candidateIds as $candidateId) {
        $address = ecSettingsFetchUserAddress($pdo, $candidateId, $hasUsersAddressColumn);
        if ($address !== '') {
            return $address;
        }
    }
    return '';
}

function ecSettingsUserCanReceiveAssignments(PDO $pdo, ?int $userId, bool $hasUsersPortalAccessLevelColumn): bool {
    if ($userId === null || $userId <= 0) {
        return true;
    }
    if (!$hasUsersPortalAccessLevelColumn) {
        return true;
    }
    $stmt = $pdo->prepare("SELECT portal_access_level FROM users WHERE id = ? LIMIT 1");
    $stmt->execute([$userId]);
    return ecAuthCanReceiveNewAssignments((string)($stmt->fetchColumn() ?: 'active'));
}

function ecSettingsUserOptionLabel(array $row, bool $hasUsersPortalAccessLevelColumn): string {
    $name = trim((string)($row['name'] ?? ''));
    $email = trim((string)($row['email'] ?? ''));
    $base = $name !== '' ? ($name . ($email !== '' ? ' - ' . $email : '')) : $email;
    if ($base === '') {
        $base = 'Utente #' . (int)($row['id'] ?? 0);
    }
    if ($hasUsersPortalAccessLevelColumn) {
        $base .= ' [' . ecAuthPortalAccessLabel((string)($row['portal_access_level'] ?? 'active')) . ']';
    }
    return $base;
}

function ecSettingsUserInAddressBook(PDO $pdo, int $managerUserId, ?int $userId, ?array $allowedRoles = null): bool {
    if ($userId === null || $userId <= 0) {
        return true;
    }

    $sql = "
        SELECT 1
        FROM contacts c
        JOIN users u ON u.id = c.linked_user_id
        WHERE c.managed_by_user_id = ?
          AND c.linked_user_id = ?
    ";
    $params = [$managerUserId, $userId];

    if (is_array($allowedRoles) && !empty($allowedRoles)) {
        $placeholders = implode(', ', array_fill(0, count($allowedRoles), '?'));
        $sql .= " AND u.role IN ($placeholders)";
        foreach ($allowedRoles as $role) {
            $params[] = (string)$role;
        }
    }

    $sql .= " LIMIT 1";
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    return (bool)$stmt->fetchColumn();
}

$allLifecycleReasons = ecSettingsLifecycleReasons($pdo, false);
$retiredReasons = array_values(array_filter($allLifecycleReasons, function ($r) {
    $applies = (string)($r['applies_to_status'] ?? 'any');
    return $applies === 'retired' || $applies === 'any';
}));
if (empty($retiredReasons)) {
    $retiredReasons = $allLifecycleReasons;
}

// Colonna opzionale per assegnazione costruttore su tabella masters.
$hasMasterBuilderColumn = ecSettingsColumnExists($pdo, 'masters', 'builder_id');
$hasMasterPermanentOfflineColumn = ecSettingsColumnExists($pdo, 'masters', 'permanently_offline');
$hasMasterPlantKindColumn = ecSettingsColumnExists($pdo, 'masters', 'plant_kind');
$hasMasterNotesColumn = ecSettingsColumnExists($pdo, 'masters', 'notes');
$hasMasterDeliveryDateColumn = ecSettingsColumnExists($pdo, 'masters', 'delivery_date');
$hasUsersAddressColumn = ecSettingsColumnExists($pdo, 'users', 'address');
$hasUsersPortalAccessLevelColumn = ecSettingsColumnExists($pdo, 'users', 'portal_access_level');

// Gestione delle richieste POST (quando un modulo viene inviato)
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    try {
        $action = $_POST['action'] ?? '';

    // Azione: Aggiungere un nuovo master
    if ($action === 'add_master') {
        $serial = trim((string)($_POST['serial_number'] ?? ''));
        $nickname = trim((string)($_POST['nickname'] ?? ''));
        $address = trim((string)($_POST['address'] ?? ''));
        $ownerIdInput = isset($_POST['owner_id']) && $_POST['owner_id'] !== '' ? (int)$_POST['owner_id'] : null;
        $maintainerIdInput = isset($_POST['maintainer_id']) && $_POST['maintainer_id'] !== '' ? (int)$_POST['maintainer_id'] : null;
        $builderIdInput = isset($_POST['builder_id']) && $_POST['builder_id'] !== '' ? (int)$_POST['builder_id'] : null;
        $notes = trim((string)($_POST['notes'] ?? ''));
        $deliveryDate = trim((string)($_POST['delivery_date'] ?? ''));
        $permanentlyOffline = ecSettingsBoolFromInput($_POST['permanently_offline'] ?? 0);
        $plantKind = ecSettingsNormalizePlantKind($_POST['plant_kind'] ?? '');
        $serialType = ecSettingsSerialType($serial);

        if (!empty($serial) && !empty($nickname)) {
            if (!$canCreatePlants) {
                $message = "La tua utenza non e abilitata alla creazione impianti.";
                $message_type = 'danger';
            } elseif (!$isAdmin && !ecAuthCanReceiveNewAssignments($currentUserAccessLevel)) {
                $message = "La tua utenza non puo creare nuovi impianti.";
                $message_type = 'danger';
            } elseif ($plantKind !== '' && $serialType === '01' && $plantKind !== 'display') {
                $message = "Per un seriale master di tipo 01 il tipo impianto deve essere Display.";
                $message_type = 'danger';
            } elseif ($plantKind !== '' && $serialType === '02' && !in_array($plantKind, ['standalone', 'rewamping'], true)) {
                $message = "Per un seriale master di tipo 02 il tipo impianto deve essere Standalone o Rewamping.";
                $message_type = 'danger';
            } elseif (!ecSettingsUserInAddressBook($pdo, (int)$currentUserId, $ownerIdInput, ['client', 'builder', 'maintainer'])) {
                $message = "Il proprietario selezionato non e presente nella tua rubrica.";
                $message_type = 'danger';
            } elseif (!ecSettingsUserInAddressBook($pdo, (int)$currentUserId, $maintainerIdInput, ['maintainer'])) {
                $message = "Il manutentore selezionato non e presente nella tua rubrica.";
                $message_type = 'danger';
            } elseif ($hasMasterBuilderColumn && !ecSettingsUserInAddressBook($pdo, (int)$currentUserId, $builderIdInput, ['builder'])) {
                $message = "Il costruttore selezionato non e presente nella tua rubrica.";
                $message_type = 'danger';
            } else {
                // Verifica seriale master gia associato ad altro impianto attivo.
                $checkSerial = $pdo->prepare("SELECT id FROM masters WHERE serial_number = ? AND deleted_at IS NULL LIMIT 1");
                $checkSerial->execute([$serial]);
                if ($checkSerial->fetch()) {
                    $message = "L'impianto non si puo creare perche la scheda master e gia associata ad un altro impianto.";
                    $message_type = 'danger';
                } else {
                    $api_key = ecSettingsGeneratePendingApiKey($pdo);

                    // Se e un cliente a creare, e sia creatore che proprietario
                    $owner_id = $isClient ? $currentUserId : $ownerIdInput;
                    $builder_id = $builderIdInput !== null ? $builderIdInput : (($isBuilder && $hasMasterBuilderColumn) ? $currentUserId : null);
                    $maintainer_id = $maintainerIdInput;
                    if ($address === '') {
                        $address = ecSettingsResolvedPlantAddress($pdo, $owner_id, $maintainer_id, $builder_id, $hasUsersAddressColumn);
                    }

                    try {
                        $insertCols = ['creator_id', 'owner_id', 'serial_number', 'api_key', 'nickname', 'address'];
                        $insertVals = ['?', '?', '?', '?', '?', '?'];
                        $insertParams = [$currentUserId, $owner_id, $serial, $api_key, $nickname, $address];

                        $insertCols[] = 'maintainer_id';
                        $insertVals[] = '?';
                        $insertParams[] = $maintainer_id;
                        if ($hasMasterBuilderColumn) {
                            $insertCols[] = 'builder_id';
                            $insertVals[] = '?';
                            $insertParams[] = $builder_id;
                        }
                        if ($hasMasterPermanentOfflineColumn) {
                            $insertCols[] = 'permanently_offline';
                            $insertVals[] = '?';
                            $insertParams[] = $permanentlyOffline;
                        }
                        if ($hasMasterPlantKindColumn) {
                            $insertCols[] = 'plant_kind';
                            $insertVals[] = '?';
                            $insertParams[] = $plantKind !== '' ? $plantKind : null;
                        }
                        if ($hasMasterNotesColumn) {
                            $insertCols[] = 'notes';
                            $insertVals[] = '?';
                            $insertParams[] = $notes !== '' ? $notes : null;
                        }
                        if ($hasMasterDeliveryDateColumn) {
                            $insertCols[] = 'delivery_date';
                            $insertVals[] = '?';
                            $insertParams[] = $deliveryDate !== '' ? $deliveryDate : null;
                        }

                        $stmt = $pdo->prepare("
                            INSERT INTO masters (" . implode(', ', $insertCols) . ")
                            VALUES (" . implode(', ', $insertVals) . ")
                        ");
                        $stmt->execute($insertParams);
                        $message = "Nuovo impianto '{$nickname}' aggiunto con successo. L'API Key va generata dalla pagina API Key Impianti con utenza amministratore.";
                        $message_type = 'success';
                    } catch (PDOException $e) {
                        if ((int)($e->errorInfo[1] ?? 0) === 1062) {
                            $message = "L'impianto non si puo creare perche la scheda master e gia associata ad un altro impianto.";
                        } else {
                            $message = "Errore durante la creazione impianto: " . $e->getMessage();
                        }
                        $message_type = 'danger';
                    }
                }
            }
        } else {
            $message = "Numero di serie e Nickname sono obbligatori.";
            $message_type = 'danger';
        }
    }

    // Azione: Aggiornare un master esistente
    if ($action === 'update_master') {
        $id = (int)($_POST['master_id'] ?? 0);
        $nickname = trim((string)($_POST['nickname'] ?? ''));
        $address = trim((string)($_POST['address'] ?? ''));
        $notes = trim((string)($_POST['notes'] ?? ''));
        $deliveryDate = trim((string)($_POST['delivery_date'] ?? ''));
        $log_days = $_POST['log_retention_days'];
        $permanentlyOffline = ecSettingsBoolFromInput($_POST['permanently_offline'] ?? 0);
        $plantKind = ecSettingsNormalizePlantKind($_POST['plant_kind'] ?? '');

        $stmtPlantType = $pdo->prepare("SELECT serial_number FROM masters WHERE id = ? LIMIT 1");
        $stmtPlantType->execute([$id]);
        $plantSerialForValidation = (string)($stmtPlantType->fetchColumn() ?: '');
        $serialType = ecSettingsSerialType($plantSerialForValidation);
        if ($plantKind !== '' && $serialType === '01' && $plantKind !== 'display') {
            $message = "Per un seriale master di tipo 01 il tipo impianto deve essere Display.";
            $message_type = 'danger';
        } elseif ($plantKind !== '' && $serialType === '02' && !in_array($plantKind, ['standalone', 'rewamping'], true)) {
            $message = "Per un seriale master di tipo 02 il tipo impianto deve essere Standalone o Rewamping.";
            $message_type = 'danger';
        } else {
            if ($address === '') {
                $stmtCurrentAssignments = $pdo->prepare("SELECT owner_id, maintainer_id" . ($hasMasterBuilderColumn ? ", builder_id" : "") . " FROM masters WHERE id = ? LIMIT 1");
                $stmtCurrentAssignments->execute([$id]);
                $currentAssignments = $stmtCurrentAssignments->fetch();
                if ($currentAssignments) {
                    $address = ecSettingsResolvedPlantAddress(
                        $pdo,
                        isset($currentAssignments['owner_id']) ? (int)$currentAssignments['owner_id'] : null,
                        isset($currentAssignments['maintainer_id']) ? (int)$currentAssignments['maintainer_id'] : null,
                        $hasMasterBuilderColumn && isset($currentAssignments['builder_id']) ? (int)$currentAssignments['builder_id'] : null,
                        $hasUsersAddressColumn
                    );
                }
            }

            // Solo Admin e Costruttore (del proprio impianto) possono modificare questi dati
            $setParts = [
                "nickname = ?",
                "address = ?",
                "log_retention_days = ?",
            ];
            $paramsUpdate = [$nickname, $address, $log_days];
            if ($hasMasterPermanentOfflineColumn) {
                $setParts[] = "permanently_offline = ?";
                $paramsUpdate[] = $permanentlyOffline;
            }
            if ($hasMasterPlantKindColumn) {
                $setParts[] = "plant_kind = ?";
                $paramsUpdate[] = $plantKind !== '' ? $plantKind : null;
            }
            if ($hasMasterNotesColumn) {
                $setParts[] = "notes = ?";
                $paramsUpdate[] = $notes !== '' ? $notes : null;
            }
            if ($hasMasterDeliveryDateColumn) {
                $setParts[] = "delivery_date = ?";
                $paramsUpdate[] = $deliveryDate !== '' ? $deliveryDate : null;
            }

            $sql = "UPDATE masters SET " . implode(', ', $setParts) . " WHERE id = ?";
            $paramsUpdate[] = $id;
            if (!$isAdmin) {
                $sql .= " AND creator_id = ?"; // Il costruttore puÃ² modificare solo i suoi
            }

            $stmt = $pdo->prepare($sql);
            
            if ($isAdmin) {
                $stmt->execute($paramsUpdate);
            } else {
                // Per il costruttore, ci assicuriamo che stia modificando un suo impianto
                $checkOwner = $pdo->prepare("SELECT id FROM masters WHERE id = ? AND creator_id = ?");
                $checkOwner->execute([$id, $currentUserId]);
                if($checkOwner->fetch()){
                    $paramsUpdate[] = $currentUserId;
                    $stmt->execute($paramsUpdate);
                }
            }
            $message = "Impianto aggiornato!";
            $message_type = 'success';
        }
    }
    // Azione: Eliminare un master (con opzione dismissione seriali)
    if ($action === 'delete_master') {
        $id = (int)($_POST['master_id'] ?? 0);
        $retireMode = (string)($_POST['retire_mode'] ?? 'none'); // none|master_only|all_devices
        $retireReason = trim((string)($_POST['retire_reason_code'] ?? ''));
        $retireNotes = trim((string)($_POST['retire_reason_details'] ?? ''));
        if (!in_array($retireMode, ['none', 'master_only', 'all_devices'], true)) {
            $retireMode = 'none';
        }
        if ($retireMode !== 'none' && !$canSerialLifecycle) {
            $message = "Non hai i permessi per dismettere/annullare seriali.";
            $message_type = 'danger';
        } elseif ($retireMode !== 'none' && $retireReason === '') {
            $message = "Per dismettere le schede devi selezionare una motivazione.";
            $message_type = 'danger';
        } else {
            try {
                $pdo->beginTransaction();
                if ($isAdmin) {
                    $stmtMaster = $pdo->prepare("SELECT * FROM masters WHERE id = ? FOR UPDATE");
                    $stmtMaster->execute([$id]);
                } else {
                    $stmtMaster = $pdo->prepare("
                        SELECT *
                        FROM masters
                        WHERE id = ?
                          AND (creator_id = ? OR owner_id = ? OR maintainer_id = ?)
                        FOR UPDATE
                    ");
                    $stmtMaster->execute([$id, $currentUserId, $currentUserId, $currentUserId]);
                }
                $masterRow = $stmtMaster->fetch();
                if (!$masterRow) {
                    $pdo->rollBack();
                    $message = "Impianto non trovato o permessi insufficienti.";
                    $message_type = 'danger';
                } else {
                    $stmtDelete = $pdo->prepare("UPDATE masters SET deleted_at = NOW() WHERE id = ?");
                    $stmtDelete->execute([$id]);
                    $retiredOk = 0;
                    $retiredNotFound = 0;
                    $retiredSkipped = 0;
                    if ($retireMode !== 'none') {
                        $serialsToRetire = [];
                        $masterSerial = trim((string)($masterRow['serial_number'] ?? ''));
                        if ($masterSerial !== '') {
                            $serialsToRetire[] = $masterSerial;
                        }
                        if ($retireMode === 'all_devices') {
                            $stmtSlaves = $pdo->prepare("
                                SELECT DISTINCT slave_sn
                                FROM measurements
                                WHERE master_id = ?
                                  AND slave_sn IS NOT NULL
                                  AND slave_sn <> ''
                                  AND slave_sn <> '0'
                            ");
                            $stmtSlaves->execute([$id]);
                            foreach ($stmtSlaves->fetchAll() as $sr) {
                                $sn = trim((string)($sr['slave_sn'] ?? ''));
                                if ($sn !== '') {
                                    $serialsToRetire[] = $sn;
                                }
                            }
                        }
                        $serialsToRetire = array_values(array_unique($serialsToRetire));
                        foreach ($serialsToRetire as $sn) {
                            $ret = ecSettingsRetireSerial(
                                $pdo,
                                $sn,
                                $retireReason,
                                $retireNotes !== '' ? $retireNotes : 'Dismissione da eliminazione impianto',
                                (int)$currentUserId,
                                null,
                                (int)$id
                            );
                            if (($ret['updated'] ?? false) === true) {
                                $retiredOk++;
                            } else {
                                $reason = (string)($ret['reason'] ?? '');
                                if ($reason === 'not_found') {
                                    $retiredNotFound++;
                                } else {
                                    $retiredSkipped++;
                                }
                            }
                        }
                    }
                    try {
                        $auditDetail = "Delete plant by user {$currentUserId} | retire_mode={$retireMode}";
                        if ($retireMode !== 'none') {
                            $auditDetail .= " | retired_ok={$retiredOk} | not_found={$retiredNotFound} | skipped={$retiredSkipped} | reason={$retireReason}";
                        }
                        $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, 'PLANT_DELETE', ?)")
                            ->execute([$id, $auditDetail]);
                    } catch (Throwable $e) {
                        // audit non bloccante
                    }
                    $pdo->commit();
                    if ($retireMode === 'none') {
                        $message = "Impianto rimosso dalla dashboard.";
                    } else {
                        $message = "Impianto rimosso. Dismissione seriali completata: {$retiredOk} aggiornati";
                        if ($retiredNotFound > 0) {
                            $message .= ", {$retiredNotFound} non presenti in device_serials";
                        }
                        if ($retiredSkipped > 0) {
                            $message .= ", {$retiredSkipped} non aggiornati";
                        }
                        $message .= ".";
                    }
                    $message_type = 'warning';
                }
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $message = "Errore durante eliminazione impianto: " . $e->getMessage();
                $message_type = 'danger';
            }
        }
    }
    // Azione: Assegnare un impianto
    if ($action === 'assign_master') {
        $master_id = (int)($_POST['master_id'] ?? 0);
        $owner_id = $_POST['owner_id'] ?: null; // Se vuoto, imposta a NULL
        $maintainer_id = $_POST['maintainer_id'] ?: null;
        $builder_id = ($hasMasterBuilderColumn && isset($_POST['builder_id']) && $_POST['builder_id'] !== '') ? $_POST['builder_id'] : null;
        $ownerIdInt = $owner_id !== null ? (int)$owner_id : null;
        $maintainerIdInt = $maintainer_id !== null ? (int)$maintainer_id : null;
        $builderIdInt = $builder_id !== null ? (int)$builder_id : null;

        if (!ecSettingsUserInAddressBook($pdo, (int)$currentUserId, $ownerIdInt, ['client', 'builder', 'maintainer'])) {
            $message = "Il proprietario selezionato non e presente nella tua rubrica.";
            $message_type = 'danger';
        } elseif (!ecSettingsUserInAddressBook($pdo, (int)$currentUserId, $maintainerIdInt, ['maintainer'])) {
            $message = "Il manutentore selezionato non e presente nella tua rubrica.";
            $message_type = 'danger';
        } elseif ($hasMasterBuilderColumn && !ecSettingsUserInAddressBook($pdo, (int)$currentUserId, $builderIdInt, ['builder'])) {
            $message = "Il costruttore selezionato non e presente nella tua rubrica.";
            $message_type = 'danger';
        } else {
            $currentAddress = '';
            $stmtCurrentMaster = $pdo->prepare("SELECT address FROM masters WHERE id = ? LIMIT 1");
            $stmtCurrentMaster->execute([$master_id]);
            $currentAddress = trim((string)($stmtCurrentMaster->fetchColumn() ?: ''));
            $resolvedAddress = $currentAddress !== ''
                ? $currentAddress
                : ecSettingsResolvedPlantAddress(
                    $pdo,
                    $ownerIdInt,
                    $maintainerIdInt,
                    $builderIdInt,
                    $hasUsersAddressColumn
                );

            // Solo Admin e Costruttore (del proprio impianto) possono assegnare
            $sql = "UPDATE masters SET owner_id = ?, maintainer_id = ?, address = ?";
            $params = [$owner_id, $maintainer_id, $resolvedAddress];
            if ($hasMasterBuilderColumn) {
                $sql .= ", builder_id = ?";
                $params[] = $builder_id;
            }
            $sql .= " WHERE id = ?";
            $params[] = $master_id;
            if (!$isAdmin) $sql .= " AND creator_id = ?";
            if (!$isAdmin) $params[] = $currentUserId;
            $stmt = $pdo->prepare($sql);
            $stmt->execute($params);
            $message = "Assegnazioni impianto aggiornate.";
            $message_type = 'success';
        }
    }

    // Azione: Ripristinare un impianto (solo Admin)
    if ($action === 'restore_master' && $isAdmin) {
        $id = $_POST['master_id'];
        $stmt = $pdo->prepare("UPDATE masters SET deleted_at = NULL WHERE id = ?");
        $stmt->execute([$id]);
        $message = "Impianto ripristinato con successo.";
        $message_type = 'success';
    }

    // Azione: Eliminazione definitiva (solo Admin)
    if ($action === 'hard_delete_master' && $isAdmin) {
        $id = $_POST['master_id'];
        // La foreign key con ON DELETE CASCADE si occupera di eliminare i record collegati.
        $stmt = $pdo->prepare("DELETE FROM masters WHERE id = ?");
        $stmt->execute([$id]);
        $message = "Impianto eliminato definitivamente dal sistema.";
        $message_type = 'danger';
    }

    // Azione: Aggiungi motivazione lifecycle (solo Admin)
    if ($action === 'add_status_reason' && $isAdmin) {
        $reasonCode = strtolower(trim((string)($_POST['reason_code'] ?? '')));
        $labelIt = trim((string)($_POST['label_it'] ?? ''));
        $labelEn = trim((string)($_POST['label_en'] ?? ''));
        $appliesTo = trim((string)($_POST['applies_to_status'] ?? 'retired'));
        $sortOrder = (int)($_POST['sort_order'] ?? 100);
        if (!preg_match('/^[a-z0-9_]{3,64}$/', $reasonCode)) {
            $message = "reason_code non valido. Usa solo lettere minuscole, numeri e underscore.";
            $message_type = 'danger';
        } elseif ($labelIt === '' || $labelEn === '') {
            $message = "Compila sia etichetta IT che EN.";
            $message_type = 'danger';
        } elseif (!in_array($appliesTo, ['any', 'active', 'retired', 'voided'], true)) {
            $message = "Stato target motivazione non valido.";
            $message_type = 'danger';
        } elseif (!ecSettingsTableExists($pdo, 'serial_status_reasons')) {
            $message = "Tabella serial_status_reasons non presente: esegui la migration Step 2.6.";
            $message_type = 'danger';
        } else {
            try {
                $stmt = $pdo->prepare("
                    INSERT INTO serial_status_reasons (
                        reason_code,
                        label_it,
                        label_en,
                        applies_to_status,
                        sort_order,
                        is_active
                    ) VALUES (?, ?, ?, ?, ?, 1)
                    ON DUPLICATE KEY UPDATE
                        label_it = VALUES(label_it),
                        label_en = VALUES(label_en),
                        applies_to_status = VALUES(applies_to_status),
                        sort_order = VALUES(sort_order),
                        is_active = 1
                ");
                $stmt->execute([$reasonCode, $labelIt, $labelEn, $appliesTo, $sortOrder]);
                $message = "Motivazione salvata correttamente.";
                $message_type = 'success';
            } catch (Throwable $e) {
                $message = "Errore salvataggio motivazione: " . $e->getMessage();
                $message_type = 'danger';
            }
        }
    }
    // Azione: Disattiva motivazione lifecycle (solo Admin)
    if ($action === 'deactivate_status_reason' && $isAdmin) {
        $reasonId = (int)($_POST['reason_id'] ?? 0);
        if ($reasonId <= 0) {
            $message = "ID motivazione non valido.";
            $message_type = 'danger';
        } elseif (!ecSettingsTableExists($pdo, 'serial_status_reasons')) {
            $message = "Tabella serial_status_reasons non presente.";
            $message_type = 'danger';
        } else {
            try {
                $stmt = $pdo->prepare("UPDATE serial_status_reasons SET is_active = 0 WHERE id = ?");
                $stmt->execute([$reasonId]);
                $message = "Motivazione disattivata.";
                $message_type = 'warning';
            } catch (Throwable $e) {
                $message = "Errore disattivazione motivazione: " . $e->getMessage();
                $message_type = 'danger';
            }
        }
    }
        // Azione: Riattiva motivazione lifecycle (solo Admin)
        if ($action === 'reactivate_status_reason' && $isAdmin) {
        $reasonId = (int)($_POST['reason_id'] ?? 0);
        if ($reasonId <= 0) {
            $message = "ID motivazione non valido.";
            $message_type = 'danger';
        } elseif (!ecSettingsTableExists($pdo, 'serial_status_reasons')) {
            $message = "Tabella serial_status_reasons non presente.";
            $message_type = 'danger';
        } else {
            try {
                $stmt = $pdo->prepare("UPDATE serial_status_reasons SET is_active = 1 WHERE id = ?");
                $stmt->execute([$reasonId]);
                $message = "Motivazione riattivata.";
                $message_type = 'success';
            } catch (Throwable $e) {
                $message = "Errore riattivazione motivazione: " . $e->getMessage();
                $message_type = 'danger';
            }
        }
        }
    } catch (Throwable $e) {
        $message = "Errore operazione impianti: " . $e->getMessage();
        $message_type = 'danger';
    }

}

// Ricarica motivazioni dopo eventuali modifiche POST.
$allLifecycleReasons = ecSettingsLifecycleReasons($pdo, false);
$retiredReasons = array_values(array_filter($allLifecycleReasons, function ($r) {
    $applies = (string)($r['applies_to_status'] ?? 'any');
    return $applies === 'retired' || $applies === 'any';
}));
if (empty($retiredReasons)) {
    $retiredReasons = $allLifecycleReasons;
}

// Recupera dati utente
$currentUser = null;
try {
    $stmtUser = $pdo->prepare("SELECT * FROM users WHERE id = ?");
    $stmtUser->execute([$currentUserId]);
    $currentUser = $stmtUser->fetch();
} catch (Throwable $e) {
    // Fallback minimale: evita HTTP500 anche con schema parziale.
    $currentUser = [
        'id' => $currentUserId,
        'role' => $_SESSION['user_role'] ?? '',
        'email' => $_SESSION['user_email'] ?? '',
    ];
}

// Recupera lista Impianti in base al ruolo
$sqlMasters = "";
$builderSelect = $hasMasterBuilderColumn ? ", b.email as builder_email" : ", NULL as builder_email";
$builderJoin = $hasMasterBuilderColumn ? " LEFT JOIN users b ON m.builder_id = b.id " : "";
$masters = [];
try {
    if ($isAdmin) {
        // Admin vede tutto
        $sqlMasters = "SELECT m.*, c.email as creator_email, o.email as owner_email, mn.email as maintainer_email {$builderSelect} FROM masters m 
                       LEFT JOIN users c ON m.creator_id = c.id
                       LEFT JOIN users o ON m.owner_id = o.id
                       LEFT JOIN users mn ON m.maintainer_id = mn.id
                       {$builderJoin}
                       ORDER BY m.deleted_at ASC, m.nickname ASC";
        $stmtM = $pdo->query($sqlMasters);
    } else {
        // Altri utenti vedono solo gli impianti a cui sono associati.
        $sqlMasters = "SELECT m.*, c.email as creator_email, o.email as owner_email, mn.email as maintainer_email {$builderSelect} FROM masters m 
                       LEFT JOIN users c ON m.creator_id = c.id
                       LEFT JOIN users o ON m.owner_id = o.id
                       LEFT JOIN users mn ON m.maintainer_id = mn.id
                       {$builderJoin}
                       WHERE (m.creator_id = :userIdCreator OR m.owner_id = :userIdOwner OR m.maintainer_id = :userIdMaintainer" . ($hasMasterBuilderColumn ? " OR m.builder_id = :userIdBuilder" : "") . ") AND m.deleted_at IS NULL 
                       ORDER BY m.nickname ASC";
        $stmtM = $pdo->prepare($sqlMasters);
        $paramsM = [
            'userIdCreator' => $currentUserId,
            'userIdOwner' => $currentUserId,
            'userIdMaintainer' => $currentUserId,
        ];
        if ($hasMasterBuilderColumn) {
            $paramsM['userIdBuilder'] = $currentUserId;
        }
        $stmtM->execute($paramsM);
    }
    $masters = $stmtM->fetchAll();
} catch (Throwable $e) {
    // Fallback robusto per evitare HTTP500 con schemi parziali o query non compatibili.
    try {
        if ($isAdmin) {
            $stmtM = $pdo->query("SELECT * FROM masters ORDER BY deleted_at ASC, nickname ASC");
        } else {
            $sqlFb = "SELECT * FROM masters WHERE (creator_id = :u1 OR owner_id = :u2 OR maintainer_id = :u3" . ($hasMasterBuilderColumn ? " OR builder_id = :u4" : "") . ") AND deleted_at IS NULL ORDER BY nickname ASC";
            $stmtM = $pdo->prepare($sqlFb);
            $paramsFb = ['u1' => $currentUserId, 'u2' => $currentUserId, 'u3' => $currentUserId];
            if ($hasMasterBuilderColumn) {
                $paramsFb['u4'] = $currentUserId;
            }
            $stmtM->execute($paramsFb);
        }
        $masters = $stmtM->fetchAll();
        $message = "Caricamento impianti in modalità compatibile (schema DB parziale rilevato).";
        $message_type = 'warning';
    } catch (Throwable $e2) {
        $masters = [];
        $message = "Errore caricamento impianti: " . $e2->getMessage();
        $message_type = 'danger';
    }
}

// Seriali master disponibili per creazione impianto (combobox filtrabile).
// Regola:
// - Admin: vede tutti i seriali master (type 01/02) non dismessi/annullati.
// - Altri ruoli: vede solo seriali collegati alla propria utenza.
$availableMasterSerials = [];
try {
    if (ecSettingsTableExists($pdo, 'device_serials')) {
        $hasProductType = ecSettingsColumnExists($pdo, 'device_serials', 'product_type_code');
        $hasStatus = ecSettingsColumnExists($pdo, 'device_serials', 'status');
        $hasOwnerUser = ecSettingsColumnExists($pdo, 'device_serials', 'owner_user_id');
        $hasAssignedUser = ecSettingsColumnExists($pdo, 'device_serials', 'assigned_user_id');
        $hasAssignedMaster = ecSettingsColumnExists($pdo, 'device_serials', 'assigned_master_id');

        $sqlSerials = "
            SELECT DISTINCT
                ds.serial_number,
                " . ($hasProductType ? "ds.product_type_code" : "'' AS product_type_code") . ",
                " . ($hasStatus ? "ds.status" : "'' AS status") . "
            FROM device_serials ds
        ";
        if ($hasAssignedMaster) {
            $sqlSerials .= " LEFT JOIN masters m ON m.id = ds.assigned_master_id ";
        }
        $sqlSerials .= " WHERE ds.serial_number IS NOT NULL AND ds.serial_number <> '' ";
        if ($hasProductType) {
            $sqlSerials .= " AND ds.product_type_code IN ('01','02') ";
        }
        if ($hasStatus) {
            $sqlSerials .= " AND ds.status NOT IN ('retired','voided') ";
        }

        $paramsSerials = [];
        if (!$isAdmin) {
            $visibility = [];
            if ($hasOwnerUser) {
                $visibility[] = "ds.owner_user_id = :uidOwner";
                $paramsSerials['uidOwner'] = $currentUserId;
            }
            if ($hasAssignedUser) {
                $visibility[] = "ds.assigned_user_id = :uidAssigned";
                $paramsSerials['uidAssigned'] = $currentUserId;
            }
            if ($hasAssignedMaster) {
                $visibility[] = "(m.creator_id = :uidCreator OR m.owner_id = :uidPlantOwner OR m.maintainer_id = :uidMaintainer" . ($hasMasterBuilderColumn ? " OR m.builder_id = :uidBuilder" : "") . ")";
                $paramsSerials['uidCreator'] = $currentUserId;
                $paramsSerials['uidPlantOwner'] = $currentUserId;
                $paramsSerials['uidMaintainer'] = $currentUserId;
                if ($hasMasterBuilderColumn) {
                    $paramsSerials['uidBuilder'] = $currentUserId;
                }
            }

            if (!empty($visibility)) {
                $sqlSerials .= " AND (" . implode(' OR ', $visibility) . ") ";
            } else {
                $sqlSerials .= " AND 1 = 0 ";
            }
        }

        $sqlSerials .= " ORDER BY ds.serial_number DESC ";
        $stmtSerials = $pdo->prepare($sqlSerials);
        $stmtSerials->execute($paramsSerials);
        $availableMasterSerials = $stmtSerials->fetchAll();
    }

    // Fallback: se device_serials non disponibile/usabile, usa seriali dalla tabella masters.
    if (empty($availableMasterSerials)) {
        if ($isAdmin) {
            $stmtFallback = $pdo->query("
                SELECT DISTINCT serial_number, '' AS product_type_code, '' AS status
                FROM masters
                WHERE serial_number IS NOT NULL AND serial_number <> ''
                ORDER BY serial_number DESC
            ");
            $availableMasterSerials = $stmtFallback->fetchAll();
        } else {
            $stmtFallback = $pdo->prepare("
                SELECT DISTINCT serial_number, '' AS product_type_code, '' AS status
                FROM masters
                WHERE serial_number IS NOT NULL
                  AND serial_number <> ''
                  AND deleted_at IS NULL
                  AND (
                      creator_id = :uidCreator
                      OR owner_id = :uidOwner
                      OR maintainer_id = :uidMaintainer
                      " . ($hasMasterBuilderColumn ? " OR builder_id = :uidBuilder " : "") . "
                  )
                ORDER BY serial_number DESC
            ");
            $paramsFallback = [
                'uidCreator' => $currentUserId,
                'uidOwner' => $currentUserId,
                'uidMaintainer' => $currentUserId,
            ];
            if ($hasMasterBuilderColumn) {
                $paramsFallback['uidBuilder'] = $currentUserId;
            }
            $stmtFallback->execute($paramsFallback);
            $availableMasterSerials = $stmtFallback->fetchAll();
        }
    }
} catch (Throwable $e) {
    $availableMasterSerials = [];
}

$assignOwnerOptions = [];
$assignMaintainerOptions = [];
$assignBuilderOptions = [];
try {
    $contacts_stmt = $pdo->prepare("
        SELECT u.id, c.name, u.email, u.role,
               " . ($hasUsersAddressColumn ? "u.address" : "NULL") . " AS address,
               " . ($hasUsersPortalAccessLevelColumn ? "u.portal_access_level" : "'active'") . " AS portal_access_level
        FROM contacts c
        JOIN users u ON c.linked_user_id = u.id
        WHERE c.managed_by_user_id = ?
          AND c.linked_user_id IS NOT NULL
        ORDER BY c.name ASC, u.email ASC
    ");
    $contacts_stmt->execute([$currentUserId]);
    $contactRows = $contacts_stmt->fetchAll();

    $contactsByUserId = [];
    foreach ($contactRows as $contactRow) {
        $linkedId = (int)($contactRow['id'] ?? 0);
        if ($linkedId <= 0 || isset($contactsByUserId[$linkedId])) {
            continue;
        }
        $contactsByUserId[$linkedId] = $contactRow;
    }

    $assignOwnerOptions = array_values(array_filter($contactsByUserId, static function ($row) {
        return in_array((string)($row['role'] ?? ''), ['client', 'builder', 'maintainer'], true);
    }));
    $assignMaintainerOptions = array_values(array_filter($contactsByUserId, static function ($row) {
        return (string)($row['role'] ?? '') === 'maintainer';
    }));
    if ($hasMasterBuilderColumn) {
        $assignBuilderOptions = array_values(array_filter($contactsByUserId, static function ($row) {
            return (string)($row['role'] ?? '') === 'builder';
        }));
    }
} catch (Throwable $e) {
    $assignOwnerOptions = [];
    $assignMaintainerOptions = [];
    $assignBuilderOptions = [];
}
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo $lang['settings_title']; ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
    <style>
        .existing-plant-hidden { display: none !important; }
        .existing-plant-filter-grid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
        .existing-plant-filter-grid .full { grid-column: 1 / -1; }
        @media (max-width: 992px) {
            .existing-plant-filter-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
        }
        @media (max-width: 768px) {
            .existing-plant-filter-grid { grid-template-columns: repeat(1, minmax(0, 1fr)); }
        }
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">

    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <!-- Card per Aggiungere un Nuovo Impianto -->
    <?php if ($canCreatePlants): ?>
    <div class="card shadow-sm mb-4">
        <div class="card-header">
            <h5 class="mb-0"><i class="fas fa-plus-circle"></i> <?php echo $lang['settings_add_new']; ?></h5>
        </div>
        <div class="card-body">
            <form method="POST">
                <input type="hidden" name="action" value="add_master">
                <div class="row">
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_nickname']; ?></label>
                        <input type="text" name="nickname" class="form-control" placeholder="Es. Impianto Pisa" required>
                    </div>
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_serial']; ?></label>
                        <input type="text"
                               name="serial_number"
                               class="form-control"
                               list="masterSerialList"
                               id="newPlantSerial"
                               placeholder="Es. 202602020001"
                               autocomplete="off"
                               required>
                        <div class="form-text">
                            <?php if ($isAdmin): ?>
                                Elenco completo seriali Master (01/02). Digita per filtrare (es. 2026...).
                            <?php else: ?>
                                Elenco seriali Master assegnati alla tua utenza. Digita per filtrare.
                            <?php endif; ?>
                        </div>
                        <datalist id="masterSerialList">
                            <?php foreach ($availableMasterSerials as $ser): ?>
                                <?php
                                    $sn = (string)($ser['serial_number'] ?? '');
                                    if ($sn === '') continue;
                                    $type = trim((string)($ser['product_type_code'] ?? ''));
                                    $st = trim((string)($ser['status'] ?? ''));
                                    $labelParts = [];
                                    if ($type !== '') $labelParts[] = 'T' . $type;
                                    if ($st !== '') $labelParts[] = $st;
                                    $lbl = implode(' | ', $labelParts);
                                ?>
                                <option value="<?php echo htmlspecialchars($sn); ?>"<?php if ($lbl !== ''): ?> label="<?php echo htmlspecialchars($lbl); ?>"<?php endif; ?>></option>
                            <?php endforeach; ?>
                        </datalist>
                    </div>
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_address']; ?></label>
                        <input type="text"
                               name="address"
                               id="newPlantAddress"
                               class="form-control"
                               list="addressSuggestions"
                               placeholder="Via, CittÃ , CAP"
                               autocomplete="off">
                        <div class="form-text">Suggerimenti automatici indirizzo (opzionali): puoi sempre inserire un testo libero.</div>
                        <datalist id="addressSuggestions"></datalist>
                    </div>
                    <?php if ($isAdmin || $isBuilder): ?>
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_assign_owner']; ?></label>
                        <select name="owner_id" id="newPlantOwnerId" class="form-select">
                            <option value=""><?php echo $lang['settings_none']; ?></option>
                            <?php foreach ($assignOwnerOptions as $ownerOption): ?>
                                <option value="<?php echo (int)$ownerOption['id']; ?>" data-address="<?php echo htmlspecialchars((string)($ownerOption['address'] ?? '')); ?>">
                                    <?php echo htmlspecialchars(ecSettingsUserOptionLabel($ownerOption, $hasUsersPortalAccessLevelColumn)); ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                        <div class="form-text">Se l'indirizzo e vuoto, verra usato l'indirizzo dell'utente selezionato.</div>
                    </div>
                    <?php endif; ?>
                </div>
                <div class="row">
                    <div class="col-md-4 mb-3">
                        <label class="form-label">Tipo impianto</label>
                        <select name="plant_kind" id="newPlantKind" class="form-select">
                            <option value="">Non specificato</option>
                            <option value="display">Display</option>
                            <option value="standalone">Standalone</option>
                            <option value="rewamping">Rewamping</option>
                        </select>
                        <div class="form-text">Usato per classificare l'impianto in Dashboard e nei filtri.</div>
                    </div>
                    <div class="col-md-4 mb-3 d-flex align-items-end">
                        <div class="form-check form-switch">
                            <input class="form-check-input" type="checkbox" role="switch" id="newPlantPermanentOffline" name="permanently_offline" value="1">
                            <label class="form-check-label" for="newPlantPermanentOffline">Impianto permanentemente offline</label>
                            <div class="form-text">Attivalo per impianti che non andranno mai online.</div>
                        </div>
                    </div>
                    <?php if ($isAdmin || $isBuilder): ?>
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_assign_maintainer']; ?></label>
                        <select name="maintainer_id" class="form-select">
                            <option value=""><?php echo $lang['settings_none']; ?></option>
                            <?php foreach ($assignMaintainerOptions as $maintainerOption): ?>
                                <option value="<?php echo (int)$maintainerOption['id']; ?>" data-address="<?php echo htmlspecialchars((string)($maintainerOption['address'] ?? '')); ?>">
                                    <?php echo htmlspecialchars(ecSettingsUserOptionLabel($maintainerOption, $hasUsersPortalAccessLevelColumn)); ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <?php endif; ?>
                </div>
                <div class="row">
                    <?php if (($isAdmin || $isBuilder) && $hasMasterBuilderColumn): ?>
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_builder'] ?? 'Costruttore'; ?></label>
                        <select name="builder_id" class="form-select">
                            <option value=""><?php echo $lang['settings_none']; ?></option>
                            <?php foreach ($assignBuilderOptions as $builderOption): ?>
                                <option value="<?php echo (int)$builderOption['id']; ?>" data-address="<?php echo htmlspecialchars((string)($builderOption['address'] ?? '')); ?>" <?php echo ($isBuilder && (int)$builderOption['id'] === (int)$currentUserId) ? 'selected' : ''; ?>>
                                    <?php echo htmlspecialchars(ecSettingsUserOptionLabel($builderOption, $hasUsersPortalAccessLevelColumn)); ?>
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <?php endif; ?>
                    <?php if ($hasMasterDeliveryDateColumn): ?>
                    <div class="col-md-4 mb-3">
                        <label class="form-label">Data consegna</label>
                        <input type="date" name="delivery_date" class="form-control">
                    </div>
                    <?php endif; ?>
                    <div class="col-md-4 mb-3 d-flex align-items-end">
                        <div class="form-text">La data di creazione verra impostata automaticamente dal sistema.</div>
                    </div>
                </div>
                <?php if ($hasMasterNotesColumn): ?>
                <div class="row">
                    <div class="col-md-12 mb-3">
                        <label class="form-label">Note impianto</label>
                        <textarea name="notes" class="form-control" rows="2" placeholder="Es. numero ordine interno, riferimenti commessa, note operative"></textarea>
                    </div>
                </div>
                <?php endif; ?>
                <button type="submit" class="btn btn-primary"><?php echo $lang['settings_btn_add']; ?></button>
            </form>
        </div>
    </div>
    <?php endif; ?>

    <!-- Card per Gestire gli Impianti Esistenti -->
    <div class="card shadow-sm">
        <div class="card-header d-flex justify-content-between align-items-center gap-2 flex-wrap">
            <div>
                <h5 class="mb-0"><i class="fas fa-sliders-h"></i> <?php echo $lang['settings_manage_existing']; ?></h5>
                <div class="small text-muted">Visualizzati: <strong id="existingPlantVisibleCount"><?php echo count($masters); ?></strong> / <?php echo count($masters); ?></div>
            </div>
            <button class="btn btn-sm btn-outline-secondary" type="button" data-bs-toggle="collapse" data-bs-target="#existingPlantFiltersCollapse" aria-expanded="false" aria-controls="existingPlantFiltersCollapse">
                <i class="fas fa-filter"></i> Filtri
            </button>
        </div>
        <div id="existingPlantFiltersCollapse" class="collapse border-top">
            <div class="card-body bg-light">
                <div class="existing-plant-filter-grid">
                    <div>
                        <label class="form-label form-label-sm">Nome impianto</label>
                        <input type="text" class="form-control form-control-sm" id="existingPlantFilterName" placeholder="Cerca nome">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Seriale master</label>
                        <input type="text" class="form-control form-control-sm" id="existingPlantFilterSerial" placeholder="Cerca seriale">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Indirizzo</label>
                        <input type="text" class="form-control form-control-sm" id="existingPlantFilterAddress" placeholder="Cerca indirizzo">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Stato</label>
                        <select class="form-select form-select-sm" id="existingPlantFilterStatus">
                            <option value="">Tutti</option>
                            <option value="active">Attivi</option>
                            <option value="deleted">Eliminati</option>
                        </select>
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Proprietario</label>
                        <input type="text" class="form-control form-control-sm" id="existingPlantFilterOwner" placeholder="Cerca proprietario">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Manutentore</label>
                        <input type="text" class="form-control form-control-sm" id="existingPlantFilterMaintainer" placeholder="Cerca manutentore">
                    </div>
                    <?php if ($hasMasterBuilderColumn): ?>
                    <div>
                        <label class="form-label form-label-sm"><?php echo $lang['settings_builder'] ?? 'Costruttore'; ?></label>
                        <input type="text" class="form-control form-control-sm" id="existingPlantFilterBuilder" placeholder="Cerca costruttore">
                    </div>
                    <?php endif; ?>
                    <div class="full d-flex justify-content-end">
                        <button class="btn btn-sm btn-outline-secondary" type="button" id="resetExistingPlantFilters">Reset filtri</button>
                    </div>
                </div>
            </div>
        </div>
        <div class="card-body">
            <?php foreach ($masters as $master): ?>
                <?php
                    $isDeleted = !empty($master['deleted_at']);
                    $bgStyle = $isDeleted ? "background-color: #ffe6e6;" : "";
                    $resolvedPlantKind = ecSettingsResolvedPlantKind($master, $hasMasterPlantKindColumn);
                    $plantKindLabel = ecSettingsPlantKindLabel($resolvedPlantKind);
                    $isPermanentOffline = $hasMasterPermanentOfflineColumn && ((int)($master['permanently_offline'] ?? 0) === 1);
                    // Determina se l'utente corrente puÃ² modificare/assegnare questo impianto
                    $canManage = $isAdmin
                        || ($isBuilder && (
                            (int)$master['creator_id'] === (int)$currentUserId
                            || ($hasMasterBuilderColumn && (int)($master['builder_id'] ?? 0) === (int)$currentUserId)
                        ));
                    // Eliminazione impianto consentita ad admin/creator/owner/maintainer.
                    $canDeletePlant = $isAdmin
                        || ((int)$master['creator_id'] === (int)$currentUserId)
                        || ((int)$master['owner_id'] === (int)$currentUserId)
                        || ((int)$master['maintainer_id'] === (int)$currentUserId)
                        || ($hasMasterBuilderColumn && ((int)($master['builder_id'] ?? 0) === (int)$currentUserId));
                ?>
                <form method="POST"
                      class="border rounded p-3 mb-3 existing-plant-card"
                      data-plant-name="<?php echo htmlspecialchars(strtolower((string)($master['nickname'] ?? ''))); ?>"
                      data-serial="<?php echo htmlspecialchars(strtolower((string)($master['serial_number'] ?? ''))); ?>"
                      data-address="<?php echo htmlspecialchars(strtolower((string)($master['address'] ?? ''))); ?>"
                      data-owner="<?php echo htmlspecialchars(strtolower((string)($master['owner_email'] ?? ''))); ?>"
                      data-maintainer="<?php echo htmlspecialchars(strtolower((string)($master['maintainer_email'] ?? ''))); ?>"
                      data-builder="<?php echo htmlspecialchars(strtolower((string)($master['builder_email'] ?? ''))); ?>"
                      data-status="<?php echo $isDeleted ? 'deleted' : 'active'; ?>"
                      style="<?php echo $bgStyle; ?>">
                    <input type="hidden" name="master_id" value="<?php echo $master['id']; ?>">
                    
                    <h5>
                        <a href="plant_detail.php?plant_id=<?php echo (int)$master['id']; ?>" class="text-decoration-none">
                            <?php echo htmlspecialchars($master['nickname']); ?>
                        </a>
                        <?php if($isDeleted) echo " <span class='badge bg-danger'>{$lang['settings_deleted']}</span>"; ?>
                        <?php if($plantKindLabel !== '-'): ?> <span class="badge bg-secondary"><?php echo htmlspecialchars($plantKindLabel); ?></span><?php endif; ?>
                        <?php if($isPermanentOffline): ?> <span class="badge bg-dark">Offline permanente</span><?php endif; ?>
                        <?php if($isAdmin) echo " <small class='text-muted'>({$lang['settings_creator']}: " . ($master['creator_email'] ?? 'N/D') . ")</small>"; ?>
                    </h5>

                    <div class="d-flex justify-content-end mb-2">
                        <button class="btn btn-sm btn-outline-primary" type="button" data-bs-toggle="collapse" data-bs-target="#plantSettingsCollapse-<?php echo (int)$master['id']; ?>" aria-expanded="false" aria-controls="plantSettingsCollapse-<?php echo (int)$master['id']; ?>">
                            <i class="fas fa-chevron-down"></i> Dettagli impianto
                        </button>
                    </div>

                    <div class="collapse" id="plantSettingsCollapse-<?php echo (int)$master['id']; ?>">
                    <div class="mb-2 small text-muted">
                        <?php echo $lang['settings_owner']; ?>: <?php echo $master['owner_email'] ?? $lang['settings_unassigned']; ?> | 
                        <?php echo $lang['settings_maintainer']; ?>: <?php echo $master['maintainer_email'] ?? $lang['settings_unassigned']; ?>
                        <?php if ($hasMasterBuilderColumn): ?>
                            | <?php echo $lang['settings_builder'] ?? 'Costruttore'; ?>: <?php echo $master['builder_email'] ?? $lang['settings_unassigned']; ?>
                        <?php endif; ?>
                    </div>

                    <?php
                        $apiKeyValue = (string)($master['api_key'] ?? '');
                        $apiKeyUsable = ecSettingsIsUsableApiKey($apiKeyValue);
                    ?>
                    <div class="input-group mb-3">
                        <span class="input-group-text"><?php echo $lang['settings_api_key']; ?></span>
                        <input type="text" class="form-control" value="<?php echo $apiKeyUsable ? htmlspecialchars($apiKeyValue) : 'Da generare'; ?>" id="apiKey-<?php echo (int)$master['id']; ?>" readonly>
                        <?php if ($apiKeyUsable): ?>
                            <button class="btn btn-outline-secondary" type="button" onclick="copyToClipboard('apiKey-<?php echo (int)$master['id']; ?>')"><i class="fas fa-copy"></i> <?php echo $lang['settings_copy']; ?></button>
                        <?php endif; ?>
                        <?php if ($isAdmin): ?>
                            <a class="btn btn-outline-primary" href="api_keys.php?plant_id=<?php echo (int)$master['id']; ?>"><i class="fas fa-key"></i> Gestisci</a>
                        <?php endif; ?>
                    </div>

                    <div class="row">
                        <div class="col-md-4"><label><?php echo $lang['settings_nickname']; ?></label><input type="text" name="nickname" class="form-control" value="<?php echo htmlspecialchars($master['nickname']); ?>" <?php if(!$canManage) echo 'readonly'; ?>></div>
                        <div class="col-md-5"><label><?php echo $lang['settings_address']; ?></label><input type="text" name="address" class="form-control" value="<?php echo htmlspecialchars($master['address']); ?>" <?php if(!$canManage) echo 'readonly'; ?>></div>
                        <div class="col-md-3"><label><?php echo $lang['settings_log_retention']; ?></label><input type="number" name="log_retention_days" class="form-control" value="<?php echo $master['log_retention_days']; ?>" min="1" max="365" <?php if(!$canManage) echo 'readonly'; ?>></div>
                    </div>
                    <div class="row mt-3">
                        <div class="col-md-4">
                            <label>Data creazione</label>
                            <input type="text" class="form-control" value="<?php echo htmlspecialchars((string)($master['created_at'] ?? '')); ?>" readonly>
                        </div>
                        <?php if ($hasMasterDeliveryDateColumn): ?>
                        <div class="col-md-4">
                            <label>Data consegna</label>
                            <input type="date" name="delivery_date" class="form-control" value="<?php echo htmlspecialchars((string)($master['delivery_date'] ?? '')); ?>" <?php if(!$canManage) echo 'readonly'; ?>>
                        </div>
                        <?php endif; ?>
                    </div>
                    <?php if ($hasMasterNotesColumn): ?>
                    <div class="row mt-3">
                        <div class="col-md-12">
                            <label>Note impianto</label>
                            <textarea name="notes" class="form-control" rows="2" <?php if(!$canManage) echo 'readonly'; ?>><?php echo htmlspecialchars((string)($master['notes'] ?? '')); ?></textarea>
                        </div>
                    </div>
                    <?php endif; ?>

                    <?php if ($canManage): // Mostra i campi di assegnazione solo a chi puÃ² gestire ?>
                        <div class="row mt-3">
                            <div class="col-md-4">
                                <label>Tipo impianto</label>
                                <select name="plant_kind" class="form-select">
                                    <option value="" <?php echo $resolvedPlantKind === '' ? 'selected' : ''; ?>>Non specificato</option>
                                    <option value="display" <?php echo $resolvedPlantKind === 'display' ? 'selected' : ''; ?>>Display</option>
                                    <option value="standalone" <?php echo $resolvedPlantKind === 'standalone' ? 'selected' : ''; ?>>Standalone</option>
                                    <option value="rewamping" <?php echo $resolvedPlantKind === 'rewamping' ? 'selected' : ''; ?>>Rewamping</option>
                                </select>
                            </div>
                            <div class="col-md-4 d-flex align-items-end">
                                <div class="form-check form-switch">
                                    <input class="form-check-input" type="checkbox" role="switch" id="permanentOffline-<?php echo (int)$master['id']; ?>" name="permanently_offline" value="1" <?php echo $isPermanentOffline ? 'checked' : ''; ?>>
                                    <label class="form-check-label" for="permanentOffline-<?php echo (int)$master['id']; ?>">Impianto permanentemente offline</label>
                                </div>
                            </div>
                        </div>
                        <div class="row mt-3">
                            <div class="col-md-4">
                                <label><?php echo $lang['settings_assign_owner']; ?></label>
                                <input type="text" class="form-control form-control-sm mb-1 select-filter-input" data-target="ownerSelect-<?php echo (int)$master['id']; ?>" placeholder="Filtra...">
                                <select name="owner_id" id="ownerSelect-<?php echo (int)$master['id']; ?>" class="form-select">
                                    <option value=""><?php echo $lang['settings_none']; ?></option>
                                    <?php
                                    $ownerOptionIds = array_map(static fn($row) => (int)$row['id'], $assignOwnerOptions);
                                    if (!empty($master['owner_id']) && !in_array((int)$master['owner_id'], $ownerOptionIds, true) && !empty($master['owner_email'])) {
                                        echo "<option value='" . (int)$master['owner_id'] . "' selected>" . htmlspecialchars((string)$master['owner_email'] . ' [non attivo]') . "</option>";
                                    }
                                    ?>
                                    <?php
                                    foreach($assignOwnerOptions as $c) {
                                        $selected = ((int)$c['id'] === (int)$master['owner_id']) ? 'selected' : '';
                                        echo "<option value='{$c['id']}' {$selected}>" . htmlspecialchars(ecSettingsUserOptionLabel($c, $hasUsersPortalAccessLevelColumn)) . "</option>";
                                    }
                                    ?>
                                </select>
                            </div>
                            <div class="col-md-4">
                                <label><?php echo $lang['settings_assign_maintainer']; ?></label>
                                <input type="text" class="form-control form-control-sm mb-1 select-filter-input" data-target="maintainerSelect-<?php echo (int)$master['id']; ?>" placeholder="Filtra...">
                                <select name="maintainer_id" id="maintainerSelect-<?php echo (int)$master['id']; ?>" class="form-select">
                                    <option value=""><?php echo $lang['settings_none']; ?></option>
                                    <?php
                                    $maintainerOptionIds = array_map(static fn($row) => (int)$row['id'], $assignMaintainerOptions);
                                    if (!empty($master['maintainer_id']) && !in_array((int)$master['maintainer_id'], $maintainerOptionIds, true) && !empty($master['maintainer_email'])) {
                                        echo "<option value='" . (int)$master['maintainer_id'] . "' selected>" . htmlspecialchars((string)$master['maintainer_email'] . ' [non attivo]') . "</option>";
                                    }
                                    ?>
                                    <?php
                                    foreach($assignMaintainerOptions as $m) {
                                        $selected = ((int)$m['id'] === (int)$master['maintainer_id']) ? 'selected' : '';
                                        echo "<option value='{$m['id']}' {$selected}>" . htmlspecialchars(ecSettingsUserOptionLabel($m, $hasUsersPortalAccessLevelColumn)) . "</option>";
                                    }
                                    ?>
                                </select>
                            </div>
                            <?php if ($hasMasterBuilderColumn): ?>
                            <div class="col-md-4">
                                <label><?php echo $lang['settings_assign_builder'] ?? 'Assegna a Costruttore'; ?></label>
                                <input type="text" class="form-control form-control-sm mb-1 select-filter-input" data-target="builderSelect-<?php echo (int)$master['id']; ?>" placeholder="Filtra...">
                                <select name="builder_id" id="builderSelect-<?php echo (int)$master['id']; ?>" class="form-select">
                                    <option value=""><?php echo $lang['settings_none']; ?></option>
                                    <?php
                                    $builderOptionIds = array_map(static fn($row) => (int)$row['id'], $assignBuilderOptions);
                                    if (!empty($master['builder_id']) && !in_array((int)$master['builder_id'], $builderOptionIds, true) && !empty($master['builder_email'])) {
                                        echo "<option value='" . (int)$master['builder_id'] . "' selected>" . htmlspecialchars((string)$master['builder_email'] . ' [non attivo]') . "</option>";
                                    }
                                    ?>
                                    <?php
                                    foreach($assignBuilderOptions as $b) {
                                        $selected = ((int)$b['id'] === (int)($master['builder_id'] ?? 0)) ? 'selected' : '';
                                        echo "<option value='{$b['id']}' {$selected}>" . htmlspecialchars(ecSettingsUserOptionLabel($b, $hasUsersPortalAccessLevelColumn)) . "</option>";
                                    }
                                    ?>
                                </select>
                            </div>
                            <?php endif; ?>
                        </div>
                    <?php endif; ?>

                    <div class="mt-3">
                        <?php if ($canManage): ?>
                            <button type="submit" name="action" value="update_master" class="btn btn-info btn-sm"><?php echo $lang['settings_save_data']; ?></button>
                            <button type="submit" name="action" value="assign_master" class="btn btn-success btn-sm"><?php echo $lang['settings_save_assign']; ?></button>
                        <?php endif; ?>

                        <?php if(!$isDeleted && $canDeletePlant): ?>
                            <button type="button"
                                    class="btn btn-danger btn-sm"
                                    onclick="openDeletePlantModal(
                                        <?php echo (int)$master['id']; ?>,
                                        '<?php echo htmlspecialchars((string)$master['nickname'], ENT_QUOTES); ?>',
                                        '<?php echo htmlspecialchars((string)$master['serial_number'], ENT_QUOTES); ?>'
                                    )">
                                <?php echo $lang['settings_btn_delete']; ?>
                            </button>
                        <?php endif; ?>

                        <?php if($isDeleted && $isAdmin): ?>
                            <button type="submit" name="action" value="restore_master" class="btn btn-warning btn-sm"><?php echo $lang['settings_restore']; ?></button>
                            <button type="submit" name="action" value="hard_delete_master" class="btn btn-dark btn-sm" onclick="return confirm('<?php echo $lang['settings_hard_delete_confirm']; ?>');"><?php echo $lang['settings_hard_delete']; ?></button>
                        <?php endif; ?>
                    </div>
                    </div>
                </form>
            <?php endforeach; ?>
        </div>
    </div>

    <?php if ($isAdmin): ?>
    <div class="card shadow-sm mt-4">
        <div class="card-header d-flex justify-content-between align-items-center">
            <h5 class="mb-0"><i class="fas fa-list-check"></i> Gestione motivazioni dismissione/annullamento</h5>
            <button class="btn btn-sm btn-outline-secondary" type="button" data-bs-toggle="collapse" data-bs-target="#statusReasonPanel" aria-expanded="false" aria-controls="statusReasonPanel">
                <i class="fas fa-chevron-down"></i>
            </button>
        </div>
        <div id="statusReasonPanel" class="collapse">
            <div class="card-body">
                <div class="alert alert-warning py-2">
                    Se modifichi questa lista, i nuovi valori saranno disponibili nella dashboard e nella gestione seriali.
                </div>
                <form method="POST" class="row g-2 align-items-end mb-4">
                    <input type="hidden" name="action" value="add_status_reason">
                    <div class="col-md-2">
                        <label class="form-label">Code</label>
                        <input type="text" name="reason_code" class="form-control" required placeholder="es. plant_dismission">
                    </div>
                    <div class="col-md-3">
                        <label class="form-label">Label IT</label>
                        <input type="text" name="label_it" class="form-control" required>
                    </div>
                    <div class="col-md-3">
                        <label class="form-label">Label EN</label>
                        <input type="text" name="label_en" class="form-control" required>
                    </div>
                    <div class="col-md-2">
                        <label class="form-label">Stato</label>
                        <select name="applies_to_status" class="form-select" required>
                            <option value="retired">retired</option>
                            <option value="voided">voided</option>
                            <option value="any">any</option>
                            <option value="active">active</option>
                        </select>
                    </div>
                    <div class="col-md-1">
                        <label class="form-label">Ordine</label>
                        <input type="number" name="sort_order" class="form-control" value="100" min="1" max="9999" required>
                    </div>
                    <div class="col-md-1 d-grid">
                        <button type="submit" class="btn btn-primary"><i class="fas fa-plus"></i></button>
                    </div>
                </form>

                <div class="table-responsive">
                    <table class="table table-sm table-striped align-middle">
                        <thead class="table-light">
                            <tr>
                                <th>ID</th>
                                <th>Code</th>
                                <th>IT</th>
                                <th>EN</th>
                                <th>Status</th>
                                <th>Order</th>
                                <th>State</th>
                                <th>Azioni</th>
                            </tr>
                        </thead>
                        <tbody>
                            <?php foreach ($allLifecycleReasons as $r): ?>
                                <?php $isActive = (int)($r['is_active'] ?? 0) === 1; ?>
                                <tr>
                                    <td><?php echo (int)($r['id'] ?? 0); ?></td>
                                    <td><code><?php echo htmlspecialchars((string)$r['reason_code']); ?></code></td>
                                    <td><?php echo htmlspecialchars((string)$r['label_it']); ?></td>
                                    <td><?php echo htmlspecialchars((string)$r['label_en']); ?></td>
                                    <td><?php echo htmlspecialchars((string)$r['applies_to_status']); ?></td>
                                    <td><?php echo (int)($r['sort_order'] ?? 100); ?></td>
                                    <td>
                                        <?php if ($isActive): ?>
                                            <span class="badge bg-success">active</span>
                                        <?php else: ?>
                                            <span class="badge bg-secondary">inactive</span>
                                        <?php endif; ?>
                                    </td>
                                    <td>
                                        <?php if (!empty($r['id'])): ?>
                                            <?php if ($isActive): ?>
                                                <form method="POST" class="d-inline">
                                                    <input type="hidden" name="action" value="deactivate_status_reason">
                                                    <input type="hidden" name="reason_id" value="<?php echo (int)$r['id']; ?>">
                                                    <button type="submit" class="btn btn-sm btn-outline-danger" onclick="return confirm('Disattivare questa motivazione?');">
                                                        <i class="fas fa-trash"></i>
                                                    </button>
                                                </form>
                                            <?php else: ?>
                                                <form method="POST" class="d-inline">
                                                    <input type="hidden" name="action" value="reactivate_status_reason">
                                                    <input type="hidden" name="reason_id" value="<?php echo (int)$r['id']; ?>">
                                                    <button type="submit" class="btn btn-sm btn-outline-success">
                                                        <i class="fas fa-rotate-left"></i>
                                                    </button>
                                                </form>
                                            <?php endif; ?>
                                        <?php else: ?>
                                            <span class="text-muted small">fallback</span>
                                        <?php endif; ?>
                                    </td>
                                </tr>
                            <?php endforeach; ?>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    </div>
    <?php endif; ?>
</div>

<!-- MODAL ELIMINAZIONE IMPIANTO -->
<div class="modal fade" id="deletePlantModal" tabindex="-1" aria-hidden="true">
  <div class="modal-dialog">
    <form method="POST" class="modal-content">
      <input type="hidden" name="action" value="delete_master">
      <input type="hidden" name="master_id" id="deletePlantMasterId" value="">
      <div class="modal-header bg-danger text-white">
        <h5 class="modal-title"><i class="fas fa-triangle-exclamation"></i> Eliminazione impianto</h5>
        <button type="button" class="btn-close btn-close-white" data-bs-dismiss="modal" aria-label="Close"></button>
      </div>
      <div class="modal-body">
        <p class="mb-1"><strong id="deletePlantName">Impianto</strong></p>
        <p class="text-muted small">Master seriale: <code id="deletePlantSerial">-</code></p>

        <div class="alert alert-warning py-2">
          Operazione sensibile: verifica attentamente prima di confermare.
        </div>

        <div class="mb-3">
          <label class="form-label">Dismissione seriali durante eliminazione</label>
          <select name="retire_mode" id="deleteRetireMode" class="form-select">
            <option value="none">Nessuna dismissione automatica</option>
            <?php if ($canSerialLifecycle): ?>
            <option value="master_only">Dismetti solo la master</option>
            <option value="all_devices">Dismetti master + tutte le periferiche rilevate</option>
            <?php endif; ?>
          </select>
          <?php if (!$canSerialLifecycle): ?>
          <div class="form-text text-muted">Dismissione/annullamento seriali non abilitati per la tua utenza.</div>
          <?php endif; ?>
        </div>

        <div id="retireReasonWrap" class="d-none">
          <div class="mb-3">
            <label class="form-label">Motivazione</label>
            <select name="retire_reason_code" id="deleteRetireReason" class="form-select">
              <option value="">Seleziona motivazione...</option>
              <?php foreach ($retiredReasons as $rr): ?>
                <option value="<?php echo htmlspecialchars((string)$rr['reason_code']); ?>">
                  <?php echo htmlspecialchars((string)$rr['reason_code'] . ' - ' . (($_SESSION['lang'] === 'it') ? (string)$rr['label_it'] : (string)$rr['label_en'])); ?>
                </option>
              <?php endforeach; ?>
            </select>
          </div>
          <div class="mb-2">
            <label class="form-label">Dettagli (opzionale)</label>
            <input type="text" class="form-control" name="retire_reason_details" placeholder="Nota intervento / ticket">
          </div>
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Annulla</button>
        <button type="submit" class="btn btn-danger">Conferma eliminazione</button>
      </div>
    </form>
  </div>
</div>

<?php require 'footer.php'; ?>

<script>
let deletePlantModal;
let addressSuggestTimer = null;
const USER_CAN_SERIAL_LIFECYCLE_SETTINGS = <?php echo $canSerialLifecycle ? 'true' : 'false'; ?>;

function copyToClipboard(elementId) {
    var copyText = document.getElementById(elementId);
    copyText.select();
    document.execCommand("copy");
    alert("<?php echo $lang['settings_copy_alert']; ?>");
}

function openDeletePlantModal(masterId, nickname, serialNumber) {
    document.getElementById('deletePlantMasterId').value = String(masterId || '');
    document.getElementById('deletePlantName').textContent = nickname || 'Impianto';
    document.getElementById('deletePlantSerial').textContent = serialNumber || '-';
    document.getElementById('deleteRetireMode').value = 'none';
    document.getElementById('deleteRetireReason').value = '';
    document.getElementById('retireReasonWrap').classList.add('d-none');
    if (!USER_CAN_SERIAL_LIFECYCLE_SETTINGS) {
        document.getElementById('deleteRetireMode').value = 'none';
    }
    deletePlantModal.show();
}

document.addEventListener('DOMContentLoaded', function () {
    deletePlantModal = new bootstrap.Modal(document.getElementById('deletePlantModal'));
    const retireModeEl = document.getElementById('deleteRetireMode');
    const reasonWrapEl = document.getElementById('retireReasonWrap');
    const reasonEl = document.getElementById('deleteRetireReason');
    if (retireModeEl && reasonWrapEl && reasonEl) {
        retireModeEl.addEventListener('change', function () {
            const needReason = USER_CAN_SERIAL_LIFECYCLE_SETTINGS && this.value !== 'none';
            reasonWrapEl.classList.toggle('d-none', !needReason);
            reasonEl.required = needReason;
            if (!needReason) {
                reasonEl.value = '';
            }
        });
    }

    // Filtro client-side dei select assegnazione (owner/maintainer/builder).
    document.querySelectorAll('.select-filter-input').forEach((inputEl) => {
        inputEl.addEventListener('input', function () {
            const targetId = this.getAttribute('data-target');
            if (!targetId) return;
            const selectEl = document.getElementById(targetId);
            if (!selectEl) return;
            const q = (this.value || '').toLowerCase().trim();
            Array.from(selectEl.options).forEach((opt, idx) => {
                if (idx === 0) {
                    opt.hidden = false; // "Nessuno" sempre visibile
                    return;
                }
                const txt = (opt.textContent || '').toLowerCase();
                opt.hidden = q !== '' && txt.indexOf(q) === -1;
            });
        });
    });

    const existingPlantFilterEls = {
        name: document.getElementById('existingPlantFilterName'),
        serial: document.getElementById('existingPlantFilterSerial'),
        address: document.getElementById('existingPlantFilterAddress'),
        owner: document.getElementById('existingPlantFilterOwner'),
        maintainer: document.getElementById('existingPlantFilterMaintainer'),
        builder: document.getElementById('existingPlantFilterBuilder'),
        status: document.getElementById('existingPlantFilterStatus')
    };
    const existingPlantCards = Array.from(document.querySelectorAll('.existing-plant-card'));
    const existingPlantVisibleCountEl = document.getElementById('existingPlantVisibleCount');
    function applyExistingPlantFilters() {
        let visible = 0;
        existingPlantCards.forEach((card) => {
            const matches =
                (!existingPlantFilterEls.name || !existingPlantFilterEls.name.value || (card.dataset.plantName || '').includes(existingPlantFilterEls.name.value.toLowerCase())) &&
                (!existingPlantFilterEls.serial || !existingPlantFilterEls.serial.value || (card.dataset.serial || '').includes(existingPlantFilterEls.serial.value.toLowerCase())) &&
                (!existingPlantFilterEls.address || !existingPlantFilterEls.address.value || (card.dataset.address || '').includes(existingPlantFilterEls.address.value.toLowerCase())) &&
                (!existingPlantFilterEls.owner || !existingPlantFilterEls.owner.value || (card.dataset.owner || '').includes(existingPlantFilterEls.owner.value.toLowerCase())) &&
                (!existingPlantFilterEls.maintainer || !existingPlantFilterEls.maintainer.value || (card.dataset.maintainer || '').includes(existingPlantFilterEls.maintainer.value.toLowerCase())) &&
                (!existingPlantFilterEls.builder || !existingPlantFilterEls.builder.value || (card.dataset.builder || '').includes(existingPlantFilterEls.builder.value.toLowerCase())) &&
                (!existingPlantFilterEls.status || !existingPlantFilterEls.status.value || (card.dataset.status || '') === existingPlantFilterEls.status.value.toLowerCase());
            card.classList.toggle('existing-plant-hidden', !matches);
            if (matches) {
                visible++;
            }
        });
        if (existingPlantVisibleCountEl) {
            existingPlantVisibleCountEl.textContent = String(visible);
        }
    }
    Object.values(existingPlantFilterEls).forEach((el) => {
        if (!el) return;
        el.addEventListener('input', applyExistingPlantFilters);
        el.addEventListener('change', applyExistingPlantFilters);
    });
    const resetExistingPlantFiltersEl = document.getElementById('resetExistingPlantFilters');
    if (resetExistingPlantFiltersEl) {
        resetExistingPlantFiltersEl.addEventListener('click', function () {
            Object.values(existingPlantFilterEls).forEach((el) => {
                if (el) el.value = '';
            });
            applyExistingPlantFilters();
        });
    }
    applyExistingPlantFilters();

    // Suggerimenti indirizzo (OpenStreetMap Nominatim) per la creazione nuovo impianto.
    // Non e' vincolante: il campo resta liberamente modificabile.
    const addressInputEl = document.getElementById('newPlantAddress');
    const addressListEl = document.getElementById('addressSuggestions');
    const ownerSelectEl = document.getElementById('newPlantOwnerId');
    const maintainerSelectEl = document.querySelector('select[name="maintainer_id"]');
    const builderSelectEl = document.querySelector('select[name="builder_id"]');
    if (addressInputEl && addressListEl) {
        addressInputEl.addEventListener('input', function () {
            const query = (this.value || '').trim();
            if (query.length < 4) {
                addressListEl.innerHTML = '';
                return;
            }

            if (addressSuggestTimer) {
                clearTimeout(addressSuggestTimer);
            }

            addressSuggestTimer = setTimeout(async () => {
                try {
                    const url = 'https://nominatim.openstreetmap.org/search?format=jsonv2&addressdetails=0&limit=8&countrycodes=it&q=' + encodeURIComponent(query);
                    const res = await fetch(url, { method: 'GET', headers: { 'Accept': 'application/json' } });
                    if (!res.ok) return;
                    const rows = await res.json();
                    if (!Array.isArray(rows)) return;

                    addressListEl.innerHTML = '';
                    const seen = new Set();
                    rows.forEach((item) => {
                        const display = String(item.display_name || '').trim();
                        if (!display || seen.has(display)) return;
                        seen.add(display);
                        const opt = document.createElement('option');
                        opt.value = display;
                        addressListEl.appendChild(opt);
                    });
                } catch (e) {
                    // In caso di errore rete non blocchiamo il form.
                }
            }, 350);
        });
    }
    function syncAddressFromSelect(selectEl) {
        if (!addressInputEl || !selectEl) return;
        if (String(addressInputEl.value || '').trim() !== '' || String(selectEl.value || '').trim() === '') {
            return;
        }
        const selectedOpt = selectEl.options[selectEl.selectedIndex];
        const candidateAddress = selectedOpt ? String(selectedOpt.getAttribute('data-address') || '').trim() : '';
        if (candidateAddress !== '') {
            addressInputEl.value = candidateAddress;
        }
    }
    if (ownerSelectEl) {
        ownerSelectEl.addEventListener('change', function () {
            syncAddressFromSelect(ownerSelectEl);
        });
    }
    if (maintainerSelectEl) {
        maintainerSelectEl.addEventListener('change', function () {
            if (ownerSelectEl && String(ownerSelectEl.value || '').trim() !== '') return;
            syncAddressFromSelect(maintainerSelectEl);
        });
    }
    if (builderSelectEl) {
        builderSelectEl.addEventListener('change', function () {
            if (ownerSelectEl && String(ownerSelectEl.value || '').trim() !== '') return;
            if (maintainerSelectEl && String(maintainerSelectEl.value || '').trim() !== '') return;
            syncAddressFromSelect(builderSelectEl);
        });
    }

    const newPlantSerialEl = document.getElementById('newPlantSerial');
    const newPlantKindEl = document.getElementById('newPlantKind');
    if (newPlantSerialEl && newPlantKindEl) {
        newPlantSerialEl.addEventListener('input', function () {
            const serial = String(this.value || '').trim();
            const match = serial.match(/^[0-9]{6}(01|02|03|04|05)[0-9]{4}$/);
            if (!match) return;
            const typeCode = match[1];
            if (typeCode === '01') {
                newPlantKindEl.value = 'display';
            } else if (typeCode === '02' && newPlantKindEl.value === 'display') {
                newPlantKindEl.value = '';
            }
        });
    }
});
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>





