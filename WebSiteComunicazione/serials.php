<?php
session_start();
require 'config.php';
require 'lang.php';

// Pagina seriali disponibile per admin/builder/maintainer.
if (!isset($_SESSION['user_id'])) {
    header("Location: index.php");
    exit;
}
$currentRole = (string)($_SESSION['user_role'] ?? '');
$canAdmin = ($currentRole === 'admin');
$canLifecycle = in_array($currentRole, ['admin', 'builder', 'maintainer'], true);
if (!$canLifecycle) {
    header("Location: index.php");
    exit;
}

/**
 * Helper lingua con fallback: evita warning se una chiave non e' ancora presente.
 */
function ecLang(string $key, string $fallback = ''): string {
    global $lang;
    return isset($lang[$key]) ? (string)$lang[$key] : $fallback;
}

/**
 * Verifica sicura esistenza tabella nello schema corrente.
 */
function ecTableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare(
        "SELECT 1
         FROM information_schema.tables
         WHERE table_schema = DATABASE()
           AND table_name = ?
         LIMIT 1"
    );
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

// Fallback statico tipi prodotto: usato se la tabella product_types non esiste ancora.
$productTypes = [
    ['code' => '01', 'label' => 'Centralina Display'],
    ['code' => '02', 'label' => 'Centralina Standalone/Rewamping'],
    ['code' => '03', 'label' => 'Scheda Relay'],
    ['code' => '04', 'label' => 'Scheda Pressione'],
    ['code' => '05', 'label' => 'Scheda Motore'],
];

if (ecTableExists($pdo, 'product_types')) {
    try {
        $rows = $pdo->query("SELECT code, label FROM product_types ORDER BY code")->fetchAll();
        if (!empty($rows)) {
            $productTypes = $rows;
        }
    } catch (Throwable $e) {
        // Manteniamo fallback statico.
    }
}

$masters = [];
if (ecTableExists($pdo, 'masters')) {
    try {
        $masters = $pdo->query("SELECT id, nickname, serial_number FROM masters ORDER BY nickname ASC, id ASC")->fetchAll();
    } catch (Throwable $e) {
        $masters = [];
    }
}

// Motivazioni lifecycle (retired/voided) per combobox operativa.
$statusReasons = [
    ['reason_code' => 'wrong_product_type', 'label_it' => 'Tipo prodotto errato', 'label_en' => 'Wrong product type', 'applies_to_status' => 'voided'],
    ['reason_code' => 'wrong_flashing', 'label_it' => 'Programmazione errata', 'label_en' => 'Wrong flashing', 'applies_to_status' => 'voided'],
    ['reason_code' => 'factory_test_discard', 'label_it' => 'Scarto collaudo fabbrica', 'label_en' => 'Factory test discard', 'applies_to_status' => 'voided'],
    ['reason_code' => 'field_replaced', 'label_it' => 'Sostituzione in campo', 'label_en' => 'Field replacement', 'applies_to_status' => 'retired'],
    ['reason_code' => 'damaged', 'label_it' => 'Dismesso per guasto', 'label_en' => 'Dismissed due to fault', 'applies_to_status' => 'retired'],
    ['reason_code' => 'plant_dismission', 'label_it' => 'Impianto dismesso', 'label_en' => 'Plant decommissioned', 'applies_to_status' => 'retired'],
    ['reason_code' => 'master_replaced', 'label_it' => 'Sostituito da altro seriale', 'label_en' => 'Replaced by another serial', 'applies_to_status' => 'retired'],
];

if (ecTableExists($pdo, 'serial_status_reasons')) {
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
        // fallback statico
    }
}
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo htmlspecialchars(ecLang('serials_page_title', 'Gestione Seriali')); ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
    <style>
        .help-btn {
            width: 28px;
            height: 28px;
            border-radius: 50%;
            padding: 0;
            line-height: 1;
        }
        .result-kv {
            font-family: Consolas, monospace;
            font-size: 0.92rem;
            background: #f8f9fa;
            border: 1px solid #e9ecef;
            border-radius: 8px;
            padding: 10px;
        }
        .table td code {
            font-size: 0.82rem;
        }
        .copyable-serial {
            cursor: pointer;
            text-decoration: underline dotted;
        }
        .copy-feedback {
            position: fixed;
            right: 16px;
            bottom: 16px;
            z-index: 2000;
            display: none;
        }
        .result-panel {
            border-left: 4px solid #0d6efd;
        }
        .card-toggle-btn {
            min-width: 34px;
        }
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">
<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="mb-2">
        <h5 class="mb-0"><i class="fas fa-barcode"></i> <?php echo htmlspecialchars(ecLang('serials_heading', 'Gestione Seriali')); ?></h5>
    </div>
    <?php if (!$canAdmin): ?>
    <div class="alert alert-info py-2">
        <?php echo htmlspecialchars(ecLang('serials_limited_mode_info', 'Modalita limitata: puoi verificare seriali e gestire dismissione/annullamento. Riserva, inserimento manuale e assegnazione master restano riservati all\'admin.')); ?>
    </div>
    <?php endif; ?>

    <div id="resultPanel" class="card shadow-sm mb-3 result-panel d-none">
        <div class="card-header d-flex justify-content-between align-items-center">
            <span><i class="fas fa-circle-info"></i> <?php echo htmlspecialchars(ecLang('serials_result_title', 'Esito ultima operazione')); ?></span>
            <button class="btn btn-sm btn-outline-secondary"
                    type="button"
                    data-bs-toggle="collapse"
                    data-bs-target="#resultCollapse"
                    aria-expanded="true"
                    aria-controls="resultCollapse">
                <?php echo htmlspecialchars(ecLang('serials_result_toggle', 'Mostra/Nascondi')); ?>
            </button>
        </div>
        <div id="resultCollapse" class="collapse show">
            <div class="card-body">
                <div class="row g-2 mb-2">
                    <div class="col-md-3"><strong><?php echo htmlspecialchars(ecLang('serials_result_status', 'Stato')); ?>:</strong> <span id="resultStatus" class="badge bg-secondary"><?php echo htmlspecialchars(ecLang('serials_result_waiting', 'In attesa di operazioni...')); ?></span></div>
                    <div class="col-md-3"><strong><?php echo htmlspecialchars(ecLang('serials_result_action', 'Azione')); ?>:</strong> <span id="resultAction">-</span></div>
                    <div class="col-md-6"><strong><?php echo htmlspecialchars(ecLang('serials_result_message', 'Messaggio')); ?>:</strong> <span id="resultMessage">-</span></div>
                </div>
                <div class="mb-2"><strong><?php echo htmlspecialchars(ecLang('serials_result_time', 'Orario')); ?>:</strong> <span id="resultTime">-</span></div>
                <div class="mb-2">
                    <strong><?php echo htmlspecialchars(ecLang('serials_result_key_data', 'Dati principali')); ?>:</strong>
                    <div id="resultKeyData" class="result-kv">-</div>
                </div>
                <details>
                    <summary><?php echo htmlspecialchars(ecLang('serials_result_details', 'Dettagli tecnici (JSON)')); ?></summary>
                    <pre id="resultJson" class="result-kv mt-2">{}</pre>
                </details>
            </div>
        </div>
    </div>

    <div class="row g-3">
        <?php if ($canAdmin): ?>
        <div class="col-lg-6">
            <div class="card shadow-sm h-100">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_card_reserve', '1) Riserva seriale automatico')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardReserveBody"
                                aria-expanded="false"
                                aria-controls="cardReserveBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_reserve_title', 'Riserva seriale automatico')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_reserve_body', 'Genera il prossimo seriale univoco per il tipo selezionato nel formato YYYYMMTTNNNN.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardReserveBody" class="card-body collapse">
                    <form id="formReserve">
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_product_type', 'Tipo prodotto')); ?></label>
                            <select class="form-select" name="product_type_code" required>
                                <?php foreach ($productTypes as $pt): ?>
                                    <option value="<?php echo htmlspecialchars((string)$pt['code']); ?>">
                                        <?php echo htmlspecialchars((string)$pt['code'] . ' - ' . (string)$pt['label']); ?>
                                    </option>
                                <?php endforeach; ?>
                            </select>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_notes_optional', 'Note (opzionale)')); ?></label>
                            <input type="text" class="form-control" name="notes" placeholder="<?php echo htmlspecialchars(ecLang('serials_placeholder_notes_reserve', 'Es. lotto test febbraio')); ?>">
                        </div>
                        <button type="submit" class="btn btn-primary btn-sm">
                            <i class="fas fa-plus-circle"></i> <?php echo htmlspecialchars(ecLang('serials_btn_reserve', 'Riserva seriale')); ?>
                        </button>
                    </form>
                </div>
            </div>
        </div>
        <?php endif; ?>

        <div class="col-lg-6">
            <div class="card shadow-sm h-100">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_card_check', '2) Verifica seriale')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardCheckBody"
                                aria-expanded="false"
                                aria-controls="cardCheckBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_check_title', 'Verifica seriale')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_check_body', 'Controlla se un seriale esiste e mostra stato, lock e collegamento master.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardCheckBody" class="card-body collapse show">
                    <form id="formCheck">
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_serial', 'Seriale')); ?></label>
                            <input type="text" class="form-control" name="serial_number" required placeholder="<?php echo htmlspecialchars(ecLang('serials_placeholder_format', 'YYYYMMTTNNNN')); ?>">
                        </div>
                        <button type="submit" class="btn btn-secondary btn-sm">
                            <i class="fas fa-search"></i> <?php echo htmlspecialchars(ecLang('serials_btn_check', 'Verifica seriale')); ?>
                        </button>
                    </form>
                </div>
            </div>
        </div>

        <?php if ($canAdmin): ?>
        <div class="col-lg-6">
            <div class="card shadow-sm h-100">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_card_manual', '3) Inserimento manuale seriale')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardManualBody"
                                aria-expanded="false"
                                aria-controls="cardManualBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_manual_title', 'Inserimento manuale seriale')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_manual_body', 'Registra un seriale manualmente per casi eccezionali, mantenendo tracciamento audit.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardManualBody" class="card-body collapse">
                    <form id="formManual">
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_serial', 'Seriale')); ?></label>
                            <input type="text" class="form-control" name="serial_number" required>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_product_type', 'Tipo prodotto')); ?></label>
                            <select class="form-select" name="product_type_code" required>
                                <?php foreach ($productTypes as $pt): ?>
                                    <option value="<?php echo htmlspecialchars((string)$pt['code']); ?>">
                                        <?php echo htmlspecialchars((string)$pt['code'] . ' - ' . (string)$pt['label']); ?>
                                    </option>
                                <?php endforeach; ?>
                            </select>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_notes', 'Note')); ?></label>
                            <input type="text" class="form-control" name="notes" placeholder="<?php echo htmlspecialchars(ecLang('serials_placeholder_notes_manual', 'Caso eccezionale')); ?>">
                        </div>
                        <button type="submit" class="btn btn-warning btn-sm">
                            <i class="fas fa-pen"></i> <?php echo htmlspecialchars(ecLang('serials_btn_manual', 'Registra manuale')); ?>
                        </button>
                    </form>
                </div>
            </div>
        </div>
        <?php endif; ?>

        <?php if ($canAdmin): ?>
        <div class="col-lg-6">
            <div class="card shadow-sm h-100">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_card_assign', '4) Assegna seriale a master')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardAssignBody"
                                aria-expanded="false"
                                aria-controls="cardAssignBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_assign_title', 'Assegna seriale a master')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_assign_body', 'Collega un seriale (tipo 01 o 02) a una master esistente e aggiorna il record in modo atomico.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardAssignBody" class="card-body collapse">
                    <div class="alert alert-light border small py-2">
                        <?php echo htmlspecialchars(ecLang('serials_assign_info', 'Usa questo comando solo per Master (tipo 01 o 02): collega un seriale gia\' riservato a un impianto esistente nel portale.')); ?>
                    </div>
                    <form id="formAssign">
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_serial', 'Seriale')); ?> (tipo 01/02)</label>
                            <input type="text" class="form-control" name="serial_number" required>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_master_target', 'Master target')); ?></label>
                            <select class="form-select" name="master_id" required>
                                <?php foreach ($masters as $m): ?>
                                    <option value="<?php echo (int)$m['id']; ?>">
                                        #<?php echo (int)$m['id']; ?> - <?php echo htmlspecialchars((string)$m['nickname']); ?>
                                        (<?php echo htmlspecialchars((string)$m['serial_number']); ?>)
                                    </option>
                                <?php endforeach; ?>
                            </select>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_notes', 'Note')); ?></label>
                            <input type="text" class="form-control" name="notes" placeholder="<?php echo htmlspecialchars(ecLang('serials_placeholder_notes_assign', 'Es. allineamento installazione impianto')); ?>">
                        </div>
                        <button type="submit" class="btn btn-success btn-sm">
                            <i class="fas fa-link"></i> <?php echo htmlspecialchars(ecLang('serials_btn_assign', 'Assegna a master')); ?>
                        </button>
                    </form>
                </div>
            </div>
        </div>
        <?php endif; ?>

        <div class="col-lg-6">
            <div class="card shadow-sm h-100 border-danger-subtle">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_card_lifecycle', '5) Dismissione / annullamento seriale')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardLifecycleBody"
                                aria-expanded="false"
                                aria-controls="cardLifecycleBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_lifecycle_title', 'Dismissione seriale')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_lifecycle_body', 'Usa questo comando per annullare (voided) o dismettere (retired) un seriale. Il sistema chiede conferma prima di applicare la modifica.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardLifecycleBody" class="card-body collapse">
                    <div class="alert alert-light border small py-2">
                        <?php echo htmlspecialchars(ecLang('serials_lifecycle_info', 'Operazione sensibile: scegli motivazione da lista e conferma prima di procedere.')); ?>
                    </div>
                    <form id="formLifecycle">
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_serial', 'Seriale')); ?></label>
                            <input type="text" class="form-control" name="serial_number" required>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_target_status', 'Nuovo stato')); ?></label>
                            <select class="form-select" name="target_status" id="targetStatusSelect" required>
                                <option value="retired"><?php echo htmlspecialchars(ecLang('serials_status_retired', 'retired - dismesso')); ?></option>
                                <option value="voided"><?php echo htmlspecialchars(ecLang('serials_status_voided', 'voided - annullato')); ?></option>
                            </select>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_reason_code', 'Motivazione')); ?></label>
                            <select class="form-select" name="reason_code" id="reasonCodeSelect" required></select>
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_replaced_by', 'Replaced by (opzionale)')); ?></label>
                            <input type="text" class="form-control" name="replaced_by_serial" placeholder="<?php echo htmlspecialchars(ecLang('serials_placeholder_replaced_by', 'Es. 202602050004')); ?>">
                        </div>
                        <div class="mb-2">
                            <label class="form-label"><?php echo htmlspecialchars(ecLang('serials_field_reason_details', 'Dettaglio motivazione (opzionale)')); ?></label>
                            <input type="text" class="form-control" name="reason_details" placeholder="<?php echo htmlspecialchars(ecLang('serials_placeholder_reason_details', 'Note operative / ticket')); ?>">
                        </div>
                        <button type="submit" class="btn btn-danger btn-sm">
                            <i class="fas fa-power-off"></i> <?php echo htmlspecialchars(ecLang('serials_btn_set_status', 'Aggiorna stato seriale')); ?>
                        </button>
                    </form>
                </div>
            </div>
        </div>
    </div>

    <div class="row g-3 mt-1">
        <div class="col-lg-6">
            <div class="card shadow-sm">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_table_recent_title', 'Ultimi seriali (max 20)')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardRecentBody"
                                aria-expanded="false"
                                aria-controls="cardRecentBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_recent_title', 'Ultimi seriali')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_recent_body', 'Mostra gli ultimi seriali registrati con stato e lock. Aggiornamento automatico.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardRecentBody" class="card-body table-responsive collapse show">
                    <table class="table table-sm table-striped align-middle mb-0">
                        <thead>
                            <tr>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_serial', 'Seriale')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_type', 'Tipo')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_status', 'Stato')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_lock', 'Lock')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_master', 'Master')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_created', 'Creato')); ?></th>
                            </tr>
                        </thead>
                        <tbody id="recentSerialsBody">
                            <tr><td colspan="6" class="text-muted"><?php echo htmlspecialchars(ecLang('serials_no_data', 'Nessun dato disponibile.')); ?></td></tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>

        <div class="col-lg-6">
            <div class="card shadow-sm">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <span><?php echo htmlspecialchars(ecLang('serials_table_audit_title', 'Audit seriali (max 20)')); ?></span>
                    <div class="d-flex gap-1">
                        <button class="btn btn-outline-secondary btn-sm card-toggle-btn"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#cardAuditBody"
                                aria-expanded="false"
                                aria-controls="cardAuditBody">
                            <i class="fas fa-chevron-down"></i>
                        </button>
                        <button type="button" class="btn btn-outline-secondary btn-sm help-btn"
                                data-help-title="<?php echo htmlspecialchars(ecLang('serials_help_audit_title', 'Audit seriali')); ?>"
                                data-help-body="<?php echo htmlspecialchars(ecLang('serials_help_audit_body', 'Mostra le ultime operazioni seriali effettuate dagli utenti amministratori.')); ?>"
                                onclick="openHelp(this)">?</button>
                    </div>
                </div>
                <div id="cardAuditBody" class="card-body table-responsive collapse show">
                    <table class="table table-sm table-striped align-middle mb-0">
                        <thead>
                            <tr>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_when', 'Quando')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_action', 'Azione')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_serial', 'Seriale')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_user', 'Utente')); ?></th>
                                <th><?php echo htmlspecialchars(ecLang('serials_col_details', 'Dettagli')); ?></th>
                            </tr>
                        </thead>
                        <tbody id="recentAuditBody">
                            <tr><td colspan="5" class="text-muted"><?php echo htmlspecialchars(ecLang('serials_no_data', 'Nessun dato disponibile.')); ?></td></tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    </div>
</div>

<div class="modal fade" id="helpModal" tabindex="-1" aria-hidden="true">
  <div class="modal-dialog modal-dialog-centered">
    <div class="modal-content">
      <div class="modal-header">
        <h5 class="modal-title" id="helpModalTitle"><?php echo htmlspecialchars(ecLang('serials_help_title', 'Aiuto')); ?></h5>
        <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
      </div>
      <div class="modal-body" id="helpModalBody"></div>
    </div>
  </div>
</div>

<div id="copyFeedback" class="alert alert-success shadow copy-feedback py-2 px-3 mb-0"></div>

<?php require 'footer.php'; ?>

<script>
const TXT = {
    statusSuccess: <?php echo json_encode(ecLang('serials_status_success', 'SUCCESSO')); ?>,
    statusError: <?php echo json_encode(ecLang('serials_status_error', 'ERRORE')); ?>,
    noData: <?php echo json_encode(ecLang('serials_no_data', 'Nessun dato disponibile.')); ?>,
    yes: <?php echo json_encode(ecLang('serials_yes', 'SI')); ?>,
    no: <?php echo json_encode(ecLang('serials_no', 'NO')); ?>,
    masterNone: <?php echo json_encode(ecLang('serials_master_none', 'Nessuno')); ?>,
    copyHint: <?php echo json_encode(ecLang('serials_copy_hint', 'Clicca per copiare seriale')); ?>,
    copyOk: <?php echo json_encode(ecLang('serials_copy_ok', 'Seriale copiato')); ?>,
    copyErr: <?php echo json_encode(ecLang('serials_copy_err', 'Copia non riuscita')); ?>,
    noReasons: <?php echo json_encode(ecLang('serials_no_reasons', 'Nessuna motivazione disponibile')); ?>,
    confirmLifecycle: <?php echo json_encode(ecLang('serials_confirm_lifecycle', 'Stai per dismettere/annullare una scheda. Verifica i dati prima di confermare.')); ?>
};
const SERIAL_REASON_ITEMS = <?php echo json_encode($statusReasons, JSON_UNESCAPED_UNICODE); ?>;

let helpModal;
let copyTimer;

function openHelp(btn) {
    const title = btn.getAttribute('data-help-title') || 'Help';
    const body = btn.getAttribute('data-help-body') || '';
    document.getElementById('helpModalTitle').textContent = title;
    document.getElementById('helpModalBody').textContent = body;
    helpModal.show();
}

async function callJsonApi(url, payload) {
    const res = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
        cache: 'no-store'
    });

    const raw = await res.text();
    let data;
    try {
        data = JSON.parse(raw);
    } catch (e) {
        throw new Error('Risposta non JSON: ' + raw);
    }

    if (!res.ok || data.status === 'error') {
        throw new Error(data.message || ('HTTP ' + res.status));
    }

    return data;
}

function renderResult(actionName, payload, data, isError) {
    const panel = document.getElementById('resultPanel');
    const collapseEl = document.getElementById('resultCollapse');
    const now = new Date().toLocaleString();
    const badge = document.getElementById('resultStatus');
    const action = document.getElementById('resultAction');
    const message = document.getElementById('resultMessage');
    const time = document.getElementById('resultTime');
    const keyData = document.getElementById('resultKeyData');
    const rawJson = document.getElementById('resultJson');

    badge.className = isError ? 'badge bg-danger' : 'badge bg-success';
    badge.textContent = isError ? TXT.statusError : TXT.statusSuccess;
    action.textContent = actionName;
    message.textContent = data.message || (isError ? 'Errore non specificato' : 'Operazione completata');
    time.textContent = now;

    const keyParts = [];
    if (data.serial) keyParts.push('serial=' + data.serial);
    if (data.serial_number) keyParts.push('serial_number=' + data.serial_number);
    if (data.master_id) keyParts.push('master_id=' + data.master_id);
    if (typeof data.exists !== 'undefined') keyParts.push('exists=' + String(data.exists));
    if (typeof data.assignable !== 'undefined') keyParts.push('assignable=' + String(data.assignable));
    if (data.reason) keyParts.push('reason=' + data.reason);
    if (data.conflict_serial) keyParts.push('conflict_serial=' + data.conflict_serial);
    if (data.expected_next_seq) keyParts.push('expected_next_seq=' + data.expected_next_seq);
    if (data.expected_next_serial_for_type) keyParts.push('expected_next_serial=' + data.expected_next_serial_for_type);
    if (data.record && data.record.status) keyParts.push('record.status=' + data.record.status);
    if (data.record && data.record.product_type_code) keyParts.push('record.type=' + data.record.product_type_code);
    keyData.textContent = keyParts.length ? keyParts.join(' | ') : '-';

    rawJson.textContent = JSON.stringify({ payload, response: data }, null, 2);

    if (panel.classList.contains('d-none')) {
        panel.classList.remove('d-none');
    }
    // A ogni nuova richiesta mostriamo aperto il dettaglio.
    if (!collapseEl.classList.contains('show')) {
        const bsCollapse = new bootstrap.Collapse(collapseEl, { toggle: false });
        bsCollapse.show();
    }
}

function escHtml(value) {
    return String(value)
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

function reasonLabel(item) {
    const isIt = document.documentElement.lang === 'it';
    if (isIt && item.label_it) return item.label_it;
    if (!isIt && item.label_en) return item.label_en;
    return item.reason_code || '';
}

function populateReasonOptions() {
    const target = document.getElementById('targetStatusSelect');
    const reasonSelect = document.getElementById('reasonCodeSelect');
    if (!target || !reasonSelect) return;

    const targetStatus = target.value;
    const valid = SERIAL_REASON_ITEMS.filter((r) => {
        const applies = String(r.applies_to_status || 'any');
        return applies === 'any' || applies === targetStatus;
    });

    reasonSelect.innerHTML = '';
    if (!valid.length) {
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = TXT.noReasons;
        reasonSelect.appendChild(opt);
        reasonSelect.value = '';
        return;
    }

    valid.forEach((item) => {
        const opt = document.createElement('option');
        opt.value = item.reason_code;
        opt.textContent = `${item.reason_code} - ${reasonLabel(item)}`;
        reasonSelect.appendChild(opt);
    });
    reasonSelect.value = valid[0].reason_code;
}

function showCopyFeedback(text, isError = false) {
    const box = document.getElementById('copyFeedback');
    box.className = isError
        ? 'alert alert-danger shadow copy-feedback py-2 px-3 mb-0'
        : 'alert alert-success shadow copy-feedback py-2 px-3 mb-0';
    box.textContent = text;
    box.style.display = 'block';
    if (copyTimer) {
        clearTimeout(copyTimer);
    }
    copyTimer = setTimeout(() => {
        box.style.display = 'none';
    }, 1400);
}

async function copyText(value) {
    if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(value);
        return;
    }
    // Fallback per contesti non sicuri (HTTP): usa textarea temporanea.
    const ta = document.createElement('textarea');
    ta.value = value;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    const ok = document.execCommand('copy');
    document.body.removeChild(ta);
    if (!ok) {
        throw new Error('copy_failed');
    }
}

function buildRecentSerialRow(item) {
    const lockText = Number(item.serial_locked) === 1 ? TXT.yes : TXT.no;
    const masterText = item.master_label ? item.master_label : TXT.masterNone;
    const serial = String(item.serial_number || '');
    const reason = String(item.status_reason_code || '');
    const replaced = String(item.replaced_by_serial || '');
    const statusText = reason ? `${String(item.status || '')} (${reason})` : String(item.status || '');
    const statusHtml = replaced ? `${escHtml(statusText)}<br><small class="text-muted">replaced_by: ${escHtml(replaced)}</small>` : escHtml(statusText);
    return `<tr>
        <td><code class="copyable-serial copy-serial" title="${escHtml(TXT.copyHint)}" data-serial="${escHtml(serial)}">${escHtml(serial)}</code></td>
        <td>${escHtml(item.product_type_code || '')}</td>
        <td>${statusHtml}</td>
        <td>${escHtml(lockText)}</td>
        <td>${escHtml(masterText)}</td>
        <td>${escHtml(item.created_at || '')}</td>
    </tr>`;
}

function buildAuditRow(item) {
    const serial = String(item.serial_number || '');
    return `<tr>
        <td>${escHtml(item.created_at || '')}</td>
        <td>${escHtml(item.action || '')}</td>
        <td><code class="copyable-serial copy-serial" title="${escHtml(TXT.copyHint)}" data-serial="${escHtml(serial)}">${escHtml(serial)}</code></td>
        <td>${escHtml(item.actor_email || '')}</td>
        <td title="${escHtml(item.details || '')}">${escHtml(item.details_short || '')}</td>
    </tr>`;
}

async function refreshOverview() {
    try {
        const data = await callJsonApi('api_serial_overview.php', { action: 'overview' });

        const serialsBody = document.getElementById('recentSerialsBody');
        const auditBody = document.getElementById('recentAuditBody');

        if (!data.serials || !data.serials.length) {
            serialsBody.innerHTML = `<tr><td colspan="6" class="text-muted">${escHtml(TXT.noData)}</td></tr>`;
        } else {
            serialsBody.innerHTML = data.serials.map(buildRecentSerialRow).join('');
        }

        if (!data.audit || !data.audit.length) {
            auditBody.innerHTML = `<tr><td colspan="5" class="text-muted">${escHtml(TXT.noData)}</td></tr>`;
        } else {
            auditBody.innerHTML = data.audit.map(buildAuditRow).join('');
        }
    } catch (err) {
        // Se la API overview fallisce, non blocchiamo la pagina: lasciamo l'ultima vista disponibile.
        console.warn('refreshOverview error:', err.message);
    }
}

async function submitAction(actionName, payload) {
    try {
        const data = await callJsonApi('api_serial.php', payload);
        renderResult(actionName, payload, data, false);
        await refreshOverview();
    } catch (err) {
        renderResult(actionName, payload, { status: 'error', message: err.message }, true);
    }
}

const formReserve = document.getElementById('formReserve');
if (formReserve) {
    formReserve.addEventListener('submit', async (e) => {
        e.preventDefault();
        await submitAction('reserve_next_serial', {
            action: 'reserve_next_serial',
            product_type_code: e.target.product_type_code.value,
            notes: e.target.notes.value
        });
    });
}

const formCheck = document.getElementById('formCheck');
if (formCheck) {
    formCheck.addEventListener('submit', async (e) => {
        e.preventDefault();
        await submitAction('check_serial', {
            action: 'check_serial',
            serial_number: e.target.serial_number.value.trim()
        });
    });
}

const formManual = document.getElementById('formManual');
if (formManual) {
    formManual.addEventListener('submit', async (e) => {
        e.preventDefault();
        await submitAction('register_manual_serial', {
            action: 'register_manual_serial',
            serial_number: e.target.serial_number.value.trim(),
            product_type_code: e.target.product_type_code.value,
            notes: e.target.notes.value
        });
    });
}

const formAssign = document.getElementById('formAssign');
if (formAssign) {
    formAssign.addEventListener('submit', async (e) => {
        e.preventDefault();
        await submitAction('assign_serial_to_master', {
            action: 'assign_serial_to_master',
            serial_number: e.target.serial_number.value.trim(),
            master_id: Number(e.target.master_id.value),
            notes: e.target.notes.value
        });
    });
}

const formLifecycle = document.getElementById('formLifecycle');
if (formLifecycle) {
    formLifecycle.addEventListener('submit', async (e) => {
        e.preventDefault();
        if (!confirm(TXT.confirmLifecycle)) {
            return;
        }
        await submitAction('set_serial_status', {
            action: 'set_serial_status',
            serial_number: e.target.serial_number.value.trim(),
            target_status: e.target.target_status.value,
            reason_code: e.target.reason_code.value,
            replaced_by_serial: e.target.replaced_by_serial.value.trim(),
            reason_details: e.target.reason_details.value.trim()
        });
    });
}

document.addEventListener('click', async (e) => {
    const el = e.target.closest('.copy-serial');
    if (!el) return;
    const serial = (el.getAttribute('data-serial') || '').trim();
    if (!serial) return;
    try {
        await copyText(serial);
        showCopyFeedback(`${TXT.copyOk}: ${serial}`, false);
    } catch (err) {
        showCopyFeedback(TXT.copyErr, true);
    }
});

document.addEventListener('DOMContentLoaded', () => {
    helpModal = new bootstrap.Modal(document.getElementById('helpModal'));
    populateReasonOptions();
    const targetStatusSelect = document.getElementById('targetStatusSelect');
    if (targetStatusSelect) {
        targetStatusSelect.addEventListener('change', populateReasonOptions);
    }
    refreshOverview();
    setInterval(refreshOverview, 7000);
});
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
