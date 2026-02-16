<?php
session_start();
require 'config.php';

// Includi il gestore della lingua
require 'lang.php';

// Protezione: se l'utente non è loggato, lo rimanda alla pagina di login.
if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';

// Definiamo i ruoli per una lettura più chiara del codice
$isAdmin = ($_SESSION['user_role'] === 'admin');
$isBuilder = ($_SESSION['user_role'] === 'builder');
$isMaintainer = ($_SESSION['user_role'] === 'maintainer');
$isClient = ($_SESSION['user_role'] === 'client');
$currentUserId = $_SESSION['user_id'];

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

$allLifecycleReasons = ecSettingsLifecycleReasons($pdo, false);
$retiredReasons = array_values(array_filter($allLifecycleReasons, function ($r) {
    $applies = (string)($r['applies_to_status'] ?? 'any');
    return $applies === 'retired' || $applies === 'any';
}));
if (empty($retiredReasons)) {
    $retiredReasons = $allLifecycleReasons;
}

// Gestione delle richieste POST (quando un modulo viene inviato)
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';

    // Azione: Aggiungere un nuovo master
    if ($action === 'add_master') {
        $serial = $_POST['serial_number'];
        $nickname = $_POST['nickname'];
        $address = $_POST['address'];

        if (!empty($serial) && !empty($nickname)) {
            // I manutentori non possono creare impianti
            if ($isMaintainer) {
                $message = "I manutentori non possono creare nuovi impianti.";
                $message_type = 'danger';
            } else {
                // Controllo unicità nickname per l'utente che sta creando (ignorando i cancellati)
                $check = $pdo->prepare("SELECT id FROM masters WHERE creator_id = ? AND nickname = ? AND deleted_at IS NULL");
                $check->execute([$currentUserId, $nickname]);
            
                if ($check->rowCount() > 0) {
                    $message = "Hai già un impianto attivo con questo nome.";
                    $message_type = 'danger';
                } else {
                    $api_key = bin2hex(random_bytes(32));
                    
                    // Se è un cliente a creare, è sia creatore che proprietario
                    $owner_id = $isClient ? $currentUserId : null;

                    $stmt = $pdo->prepare("INSERT INTO masters (creator_id, owner_id, serial_number, api_key, nickname, address) VALUES (?, ?, ?, ?, ?, ?)");
                    $stmt->execute([$currentUserId, $owner_id, $serial, $api_key, $nickname, $address]);
                    $message = "Nuovo impianto '{$nickname}' aggiunto con successo!";
                    $message_type = 'success';
                }
            }
        } else {
            $message = "Numero di serie e Nickname sono obbligatori.";
            $message_type = 'danger';
        }
    }

    // Azione: Aggiornare un master esistente
    if ($action === 'update_master') {
        $id = $_POST['master_id'];
        $nickname = $_POST['nickname'];
        $address = $_POST['address'];
        $log_days = $_POST['log_retention_days'];

        // Solo Admin e Costruttore (del proprio impianto) possono modificare questi dati
        $sql = "UPDATE masters SET nickname = ?, address = ?, log_retention_days = ? WHERE id = ?";
        if (!$isAdmin) {
            $sql .= " AND creator_id = ?"; // Il costruttore può modificare solo i suoi
        }

        $stmt = $pdo->prepare($sql);
        
        if ($isAdmin) {
            $stmt->execute([$nickname, $address, $log_days, $id]);
        } else {
            // Per il costruttore, ci assicuriamo che stia modificando un suo impianto
            $checkOwner = $pdo->prepare("SELECT id FROM masters WHERE id = ? AND creator_id = ?");
            $checkOwner->execute([$id, $currentUserId]);
            if($checkOwner->fetch()){
                $stmt->execute([$nickname, $address, $log_days, $id, $currentUserId]);
            }
        }
        $message = "Impianto aggiornato!";
        $message_type = 'success';
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
        if ($retireMode !== 'none' && $retireReason === '') {
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
        $master_id = $_POST['master_id'];
        $owner_id = $_POST['owner_id'] ?: null; // Se vuoto, imposta a NULL
        $maintainer_id = $_POST['maintainer_id'] ?: null;

        // Solo Admin e Costruttore (del proprio impianto) possono assegnare
        $sql = "UPDATE masters SET owner_id = ?, maintainer_id = ? WHERE id = ?";
        if (!$isAdmin) $sql .= " AND creator_id = ?";
        $stmt = $pdo->prepare($sql);
        $params = $isAdmin ? [$owner_id, $maintainer_id, $master_id] : [$owner_id, $maintainer_id, $master_id, $currentUserId];
        $stmt->execute($params);
        $message = "Assegnazioni impianto aggiornate.";
        $message_type = 'success';
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
$stmtUser = $pdo->prepare("SELECT * FROM users WHERE id = ?");
$stmtUser->execute([$currentUserId]);
$currentUser = $stmtUser->fetch();

// Recupera lista Impianti in base al ruolo
$sqlMasters = "";
if ($isAdmin) {
    // Admin vede tutto
    $sqlMasters = "SELECT m.*, c.email as creator_email, o.email as owner_email, mn.email as maintainer_email FROM masters m 
                   LEFT JOIN users c ON m.creator_id = c.id
                   LEFT JOIN users o ON m.owner_id = o.id
                   LEFT JOIN users mn ON m.maintainer_id = mn.id
                   ORDER BY m.deleted_at ASC, m.nickname ASC";
    $stmtM = $pdo->query($sqlMasters);
} else {
    // Altri utenti vedono solo gli impianti a cui sono associati (come creatori, proprietari o manutentori)
    $sqlMasters = "SELECT m.*, c.email as creator_email, o.email as owner_email, mn.email as maintainer_email FROM masters m 
                   LEFT JOIN users c ON m.creator_id = c.id
                   LEFT JOIN users o ON m.owner_id = o.id
                   LEFT JOIN users mn ON m.maintainer_id = mn.id
                   WHERE (m.creator_id = :userIdCreator OR m.owner_id = :userIdOwner OR m.maintainer_id = :userIdMaintainer) AND m.deleted_at IS NULL 
                   ORDER BY m.nickname ASC";
    $stmtM = $pdo->prepare($sqlMasters);
    $stmtM->execute([
        'userIdCreator' => $currentUserId,
        'userIdOwner' => $currentUserId,
        'userIdMaintainer' => $currentUserId,
    ]);
}
$masters = $stmtM->fetchAll();
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
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">

    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <!-- Card per Aggiungere un Nuovo Impianto -->
    <?php if ($isAdmin || $isBuilder || $isClient): // Solo questi ruoli possono creare impianti ?>
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
                        <input type="text" name="serial_number" class="form-control" placeholder="Es. 2023110001" required>
                    </div>
                    <div class="col-md-4 mb-3">
                        <label class="form-label"><?php echo $lang['settings_address']; ?></label>
                        <input type="text" name="address" class="form-control" placeholder="Via, Città, CAP">
                    </div>
                </div>
                <button type="submit" class="btn btn-primary"><?php echo $lang['settings_btn_add']; ?></button>
            </form>
        </div>
    </div>
    <?php endif; ?>

    <!-- Card per Gestire gli Impianti Esistenti -->
    <div class="card shadow-sm">
        <div class="card-header">
            <h5 class="mb-0"><i class="fas fa-sliders-h"></i> <?php echo $lang['settings_manage_existing']; ?></h5>
        </div>
        <div class="card-body">
            <?php foreach ($masters as $master): ?>
                <?php
                    $isDeleted = !empty($master['deleted_at']);
                    $bgStyle = $isDeleted ? "background-color: #ffe6e6;" : "";
                    // Determina se l'utente corrente può modificare/assegnare questo impianto
                    $canManage = $isAdmin || ($isBuilder && $master['creator_id'] == $currentUserId);
                    // Eliminazione impianto consentita ad admin/creator/owner/maintainer.
                    $canDeletePlant = $isAdmin
                        || ((int)$master['creator_id'] === (int)$currentUserId)
                        || ((int)$master['owner_id'] === (int)$currentUserId)
                        || ((int)$master['maintainer_id'] === (int)$currentUserId);
                ?>
                <form method="POST" class="border rounded p-3 mb-3" style="<?php echo $bgStyle; ?>">
                    <input type="hidden" name="master_id" value="<?php echo $master['id']; ?>">
                    
                    <h5>
                        <?php echo htmlspecialchars($master['nickname']); ?>
                        <?php if($isDeleted) echo " <span class='badge bg-danger'>{$lang['settings_deleted']}</span>"; ?>
                        <?php if($isAdmin) echo " <small class='text-muted'>({$lang['settings_creator']}: " . ($master['creator_email'] ?? 'N/D') . ")</small>"; ?>
                    </h5>
                    
                    <div class="mb-2 small text-muted">
                        <?php echo $lang['settings_owner']; ?>: <?php echo $master['owner_email'] ?? $lang['settings_unassigned']; ?> | 
                        <?php echo $lang['settings_maintainer']; ?>: <?php echo $master['maintainer_email'] ?? $lang['settings_unassigned']; ?>
                    </div>

                    <div class="input-group mb-3">
                        <span class="input-group-text"><?php echo $lang['settings_api_key']; ?></span>
                        <input type="text" class="form-control" value="<?php echo $master['api_key']; ?>" id="apiKey-<?php echo $master['id']; ?>" readonly>
                        <button class="btn btn-outline-secondary" type="button" onclick="copyToClipboard('apiKey-<?php echo $master['id']; ?>')"><i class="fas fa-copy"></i> <?php echo $lang['settings_copy']; ?></button>
                    </div>

                    <div class="row">
                        <div class="col-md-4"><label><?php echo $lang['settings_nickname']; ?></label><input type="text" name="nickname" class="form-control" value="<?php echo htmlspecialchars($master['nickname']); ?>" <?php if(!$canManage) echo 'readonly'; ?>></div>
                        <div class="col-md-5"><label><?php echo $lang['settings_address']; ?></label><input type="text" name="address" class="form-control" value="<?php echo htmlspecialchars($master['address']); ?>" <?php if(!$canManage) echo 'readonly'; ?>></div>
                        <div class="col-md-3"><label><?php echo $lang['settings_log_retention']; ?></label><input type="number" name="log_retention_days" class="form-control" value="<?php echo $master['log_retention_days']; ?>" min="1" max="365" <?php if(!$canManage) echo 'readonly'; ?>></div>
                    </div>

                    <?php if ($canManage): // Mostra i campi di assegnazione solo a chi può gestire ?>
                        <div class="row mt-3">
                            <div class="col-md-6">
                                <label><?php echo $lang['settings_assign_owner']; ?></label>
                                <select name="owner_id" class="form-select">
                                    <option value=""><?php echo $lang['settings_none']; ?></option>
                                    <?php
                                    // Carica i contatti dell'utente che sono anche utenti del sistema
                                    $contacts_stmt = $pdo->prepare("SELECT u.id, c.name, u.email FROM contacts c JOIN users u ON c.linked_user_id = u.id WHERE c.managed_by_user_id = ? ORDER BY c.name");
                                    $contacts_stmt->execute([$currentUserId]);
                                    foreach($contacts_stmt->fetchAll() as $c) {
                                        $selected = ($c['id'] == $master['owner_id']) ? 'selected' : '';
                                        echo "<option value='{$c['id']}' {$selected}>{$c['email']}</option>";
                                    }
                                    ?>
                                </select>
                            </div>
                            <div class="col-md-6">
                                <label><?php echo $lang['settings_assign_maintainer']; ?></label>
                                <select name="maintainer_id" class="form-select">
                                    <option value=""><?php echo $lang['settings_none']; ?></option>
                                    <?php 
                                    $maintainers = $pdo->query("SELECT id, email FROM users WHERE role = 'maintainer'")->fetchAll();
                                    foreach($maintainers as $m) {
                                        $selected = ($m['id'] == $master['maintainer_id']) ? 'selected' : '';
                                        echo "<option value='{$m['id']}' {$selected}>{$m['email']}</option>";
                                    }
                                    ?>
                                </select>
                            </div>
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
            <option value="master_only">Dismetti solo la master</option>
            <option value="all_devices">Dismetti master + tutte le periferiche rilevate</option>
          </select>
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
    deletePlantModal.show();
}

document.addEventListener('DOMContentLoaded', function () {
    deletePlantModal = new bootstrap.Modal(document.getElementById('deletePlantModal'));
    const retireModeEl = document.getElementById('deleteRetireMode');
    const reasonWrapEl = document.getElementById('retireReasonWrap');
    const reasonEl = document.getElementById('deleteRetireReason');
    if (retireModeEl && reasonWrapEl && reasonEl) {
        retireModeEl.addEventListener('change', function () {
            const needReason = this.value !== 'none';
            reasonWrapEl.classList.toggle('d-none', !needReason);
            reasonEl.required = needReason;
            if (!needReason) {
                reasonEl.value = '';
            }
        });
    }
});
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>



