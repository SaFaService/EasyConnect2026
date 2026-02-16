<?php
session_start();
require 'config.php';

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
$currentUserId = $_SESSION['user_id'];

if ($isAdmin) {
    // Admin: vede tutto, anche i cancellati (ma marcati)
    $sql = "SELECT m.*, o.email as owner_email, mn.email as maintainer_email FROM masters m 
            LEFT JOIN users o ON m.owner_id = o.id
            LEFT JOIN users mn ON m.maintainer_id = mn.id 
            ORDER BY m.deleted_at ASC, m.created_at DESC";
    $stmt = $pdo->query($sql);
} else {
    // Gli altri utenti vedono solo gli impianti a cui sono associati (come creatori, proprietari o manutentori)
    // e che non sono stati cancellati.
    $sql = "SELECT m.*, o.email as owner_email, mn.email as maintainer_email FROM masters m 
            LEFT JOIN users o ON m.owner_id = o.id
            LEFT JOIN users mn ON m.maintainer_id = mn.id
            WHERE (m.creator_id = :userIdCreator OR m.owner_id = :userIdOwner OR m.maintainer_id = :userIdMaintainer) 
            AND m.deleted_at IS NULL 
            ORDER BY m.created_at DESC";
    $stmt = $pdo->prepare($sql);
    $stmt->execute([
        'userIdCreator' => $currentUserId,
        'userIdOwner' => $currentUserId,
        'userIdMaintainer' => $currentUserId,
    ]);
}
$masters = $stmt->fetchAll();

// Recupera l'ultima versione firmware disponibile per Master (Rewamping)
$stmtFw = $pdo->prepare("SELECT version FROM firmware_releases WHERE device_type = 'master' AND is_active = 1 ORDER BY id DESC LIMIT 1");
$stmtFw->execute();
$latestFw = $stmtFw->fetchColumn();

// Recupera l'ultima versione firmware disponibile per Slave Pressione
$stmtFwS = $pdo->prepare("SELECT version FROM firmware_releases WHERE device_type = 'slave_pressure' AND is_active = 1 ORDER BY id DESC LIMIT 1");
$stmtFwS->execute();
$latestFwSlaveP = $stmtFwS->fetchColumn();

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
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="card shadow-sm">
        <div class="card-header bg-white">
            <h5 class="mb-0"><i class="fas fa-server"></i> <?php echo $lang['dash_plant_list']; ?></h5>
        </div>
        <div class="card-body p-0">
            <table class="table table-hover mb-0">
                <thead class="table-light">
                    <tr>
                        <th width="5%"><?php echo $lang['dash_status']; ?></th>
                        <th width="20%"><?php echo $lang['dash_plant']; ?></th>
                        <th width="15%"><?php echo $lang['dash_address']; ?></th>
                        <th width="15%"><?php echo $lang['dash_serial']; ?></th>
                        <th width="10%"><?php echo $lang['dash_deltap']; ?></th>
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
                        
                        // Gestione visualizzazione cancellati (solo per admin)
                        $isDeleted = !empty($m['deleted_at']);
                        $rowStyle = $isDeleted ? "background-color: #ffe6e6; color: #999;" : "";
                        $statusDot = $isDeleted ? "<span class='badge bg-danger'>ELIMINATO</span>" : "<span class='status-dot $statusClass'></span>";
                        
                        // Calcolo qualità segnale per UI
                        $rssi = $m['rssi'] ?? -100;
                        $signalIcon = "fa-signal";
                        $signalColor = "text-muted";
                        if ($rssi > -60) { $signalColor = "text-success"; }
                        elseif ($rssi > -75) { $signalColor = "text-warning"; }
                        elseif ($rssi > -100) { $signalColor = "text-danger"; }
                        
                        // Recupera le ultime misurazioni per questo master (Spostato qui per calcolare Delta P)
                        $stmtS = $pdo->prepare("SELECT * FROM measurements WHERE master_id = ? ORDER BY recorded_at DESC LIMIT 15");
                        $stmtS->execute([$m['id']]);
                        $measures = $stmtS->fetchAll();
                        $lastMeasure = $measures[0] ?? null; // Prendi la più recente

                        // Cerca l'ultimo valore di Delta P (associato al Master, quindi slave_sn vuoto)
                        $deltaPValue = '-';
                        foreach ($measures as $ms) {
                            if (empty($ms['slave_sn']) && isset($ms['delta_p'])) {
                                $deltaPValue = $ms['delta_p'] . ' Pa';
                                break;
                            }
                        }
                        
                        // --- RILEVAMENTO CAMBIO SERIALE ---
                        // Se il seriale nell'ultimo pacchetto dati è diverso da quello registrato nel DB
                        $detectedSerial = $lastMeasure['master_sn'] ?? $m['serial_number'];
                        $serialMismatch = ($detectedSerial !== $m['serial_number']);
                        // ----------------------------------

                        // Recupera la LISTA delle periferiche connesse (Ultimo dato per ognuna)
                        // Questa query complessa prende l'ultima misurazione per ogni slave_sn distinto
                        $sqlSlaves = "SELECT m1.* 
                                      FROM measurements m1
                                      INNER JOIN (
                                          SELECT slave_sn, MAX(recorded_at) as max_date
                                          FROM measurements
                                          WHERE master_id = ? AND slave_sn IS NOT NULL AND slave_sn != '' AND slave_sn != '0'
                                          GROUP BY slave_sn
                                      ) m2 ON m1.slave_sn = m2.slave_sn AND m1.recorded_at = m2.max_date
                                      WHERE m1.master_id = ?";
                        $stmtSlaves = $pdo->prepare($sqlSlaves);
                        $stmtSlaves->execute([$m['id'], $m['id']]);
                        $slavesList = $stmtSlaves->fetchAll();
                    ?>
                    <tr style="<?php echo $rowStyle; ?>">
                        <td class="text-center align-middle"><?php echo $statusDot; ?></td>
                        <td class="align-middle">
                            <strong><?php echo htmlspecialchars($m['nickname']); ?></strong>
                            <?php if($isAdmin): ?>
                                <div class="small text-muted">
                                    <?php echo $lang['dash_prop']; ?>: <?php echo $m['owner_email'] ?? 'N/D'; ?><br>
                                    <?php echo $lang['dash_man']; ?>: <?php echo $m['maintainer_email'] ?? 'N/D'; ?>
                                </div>
                            <?php endif; ?>
                        </td>
                        <td class="align-middle">
                            <a href="<?php echo $mapUrl; ?>" target="_blank" class="text-decoration-none text-secondary">
                                <i class="fas fa-map-marker-alt text-danger"></i> <?php echo htmlspecialchars($m['address']); ?>
                            </a>
                        </td>
                        <td class="align-middle">
                            <?php if ($serialMismatch): ?>
                                <div class="serial-mismatch-wrap">
                                    <div class="serial-mismatch-row text-danger fw-bold">
                                        <i class="fas fa-circle"></i> DB: <?php echo htmlspecialchars($m['serial_number']); ?>
                                    </div>
                                    <div class="serial-mismatch-row text-success fw-bold">
                                        <i class="fas fa-circle"></i> LIVE: <?php echo htmlspecialchars($detectedSerial); ?>
                                    </div>
                                    <div class="serial-mismatch-actions btn-group btn-group-sm mt-1" role="group">
                                        <button class="btn btn-outline-primary"
                                                title="Allinea seriale master"
                                                onclick="replaceSerial(
                                                    <?php echo (int)$m['id']; ?>,
                                                    '<?php echo htmlspecialchars((string)$m['serial_number'], ENT_QUOTES); ?>',
                                                    '<?php echo htmlspecialchars((string)$detectedSerial, ENT_QUOTES); ?>'
                                                )">
                                            <i class="fas fa-right-left"></i>
                                        </button>
                                        <button class="btn btn-outline-danger"
                                                title="Dismetti seriale master precedente"
                                                onclick="openRetireMasterModal(
                                                    <?php echo (int)$m['id']; ?>,
                                                    '<?php echo htmlspecialchars((string)$m['serial_number'], ENT_QUOTES); ?>',
                                                    '<?php echo htmlspecialchars((string)$detectedSerial, ENT_QUOTES); ?>'
                                                )">
                                            <i class="fas fa-trash"></i>
                                        </button>
                                    </div>
                                </div>
                            <?php else: ?>
                                <?php echo htmlspecialchars($m['serial_number']); ?>
                            <?php endif; ?>
                        </td>
                        <td class="align-middle"><strong><?php echo $deltaPValue; ?></strong></td>
                        <td class="align-middle"><span class="badge bg-info text-dark"><?php echo htmlspecialchars($m['fw_version']); ?></span></td>
                        <td class="align-middle text-center"><i class="fas <?php echo $signalIcon; ?> <?php echo $signalColor; ?>" title="<?php echo $rssi; ?> dBm"></i></td>
                        <td class="align-middle small text-muted"><?php echo $m['last_seen'] ? date('d/m H:i', strtotime($m['last_seen'])) : $lang['dash_never']; ?></td>
                        <td class="align-middle">
                            <button class="btn btn-sm btn-outline-primary" type="button" data-bs-toggle="collapse" data-bs-target="#details-<?php echo $m['id']; ?>">
                                <i class="fas fa-plus"></i>
                            </button>
                        </td>
                    </tr>
                    <!-- Riga Dettagli MASTER (Contiene lista periferiche) -->
                    <tr>
                        <td colspan="8" class="p-0 border-0">
                            <div class="collapse slave-details p-3" id="details-<?php echo $m['id']; ?>">
                                
                                <!-- Intestazione Dettagli Master -->
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h6 class="mb-0 text-primary"><i class="fas fa-network-wired"></i> <?php echo $lang['dash_connected_devices']; ?></h6>
                                    <div>
                                        <a href="download_csv.php?master_id=<?php echo $m['id']; ?>" class="btn btn-sm btn-success me-2"><i class="fas fa-file-csv"></i> <?php echo $lang['dash_log_plant']; ?></a>
                                        
                                        <?php 
                                        // Pulsante Storico Modifiche (Audit) - Visibile solo a Admin, Builder, Maintainer
                                        if ($isAdmin || $_SESSION['user_role'] === 'builder' || $_SESSION['user_role'] === 'maintainer'): ?>
                                            <a href="download_audit.php?master_id=<?php echo $m['id']; ?>" class="btn btn-sm btn-info me-2 text-white"><i class="fas fa-clipboard-list"></i> <?php echo $lang['dash_audit']; ?></a>
                                        <?php endif; ?>
                                        
                                        <!-- PULSANTE AGGIORNAMENTO MASTER -->
                                        <?php 
                                            $updateAvailable = ($latestFw && $m['fw_version'] !== $latestFw);
                                            $canUpdate = ($rssi >= -75);
                                            $btnClass = ($canUpdate && $updateAvailable) ? "btn-warning" : "btn-outline-secondary";
                                            $btnAttr = $canUpdate ? "" : "disabled";
                                            $btnText = $updateAvailable ? $lang['dash_update_master'] . " v$latestFw" : $lang['dash_master_updated'];
                                            
                                            if ($m['update_requested'] == 1) {
                                                echo '<span class="badge bg-warning text-dark me-2"><i class="fas fa-sync fa-spin"></i> ' . $lang['dash_master_updating'] . '</span>';
                                            } elseif ($m['ota_status'] === 'Failed') {
                                                echo '<button type="button" class="btn btn-sm btn-danger me-2" onclick="startUpdate('.$m['id'].', \''.$latestFw.'\')"><i class="fas fa-redo"></i> ' . $lang['dash_retry_master'] . '</button>';
                                            } else {
                                                echo '<button type="button" class="btn btn-sm '.$btnClass.' me-2" '.$btnAttr.' onclick="startUpdate('.$m['id'].', \''.$latestFw.'\')"><i class="fas fa-microchip"></i> '.$btnText.'</button>';
                                            }
                                        ?>
                                    </div>
                                </div>

                                <!-- Tabella Periferiche -->
                                <div class="table-responsive">
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
                                                $slaveUpdAvail = ($latestFwSlaveP && ($slave['fw_version'] ?? '0.0.0') !== $latestFwSlaveP);
                                                $slaveBtnClass = $slaveUpdAvail ? "btn-warning" : "btn-outline-secondary";
                                                // Nota: Aggiornamento slave non ancora implementato nel backend, ma UI pronta
                                            ?>
                                            <tr>
                                                <td><span class="badge bg-info text-dark">Pressione</span></td>
                                                <td><strong><?php echo htmlspecialchars($slave['slave_sn']); ?></strong></td>
                                                <td><?php echo $slave['slave_grp']; ?></td>
                                                <td><?php echo $slave['pressure']; ?> Pa</td>
                                                <td><?php echo $slave['temperature']; ?> °C</td>
                                                <td><?php echo htmlspecialchars($slave['fw_version'] ?? 'N/D'); ?></td>
                                                <td><?php echo date('H:i:s', strtotime($slave['recorded_at'])); ?></td>
                                                <td>
                                                    <button class="btn btn-xs btn-outline-dark" type="button" data-bs-toggle="collapse" data-bs-target="#history-<?php echo $slave['slave_sn']; ?>">
                                                        <i class="fas fa-history" title="<?php echo $lang['slave_history_tooltip']; ?>"></i>
                                                    </button>
                                                    <a href="download_csv.php?master_id=<?php echo $m['id']; ?>&slave_sn=<?php echo $slave['slave_sn']; ?>" class="btn btn-xs btn-outline-success" title="<?php echo $lang['slave_download_csv_tooltip']; ?>"><i class="fas fa-download"></i></a>
                                                    <button class="btn btn-xs btn-outline-danger"
                                                            title="Dismetti periferica"
                                                            onclick="openRetireSlaveModal(
                                                                <?php echo (int)$m['id']; ?>,
                                                                '<?php echo htmlspecialchars((string)$slave['slave_sn'], ENT_QUOTES); ?>'
                                                            )">
                                                        <i class="fas fa-trash"></i>
                                                    </button>
                                                    <button class="btn btn-xs <?php echo $slaveBtnClass; ?>" title="<?php echo sprintf($lang['slave_update_tooltip'], $latestFwSlaveP); ?>" onclick="startSlaveUpdate(<?php echo $m['id']; ?>, '<?php echo $slave['slave_sn']; ?>', '<?php echo $latestFwSlaveP; ?>')" <?php if(!$slaveUpdAvail) echo 'disabled'; ?>><i class="fas fa-sync"></i></button>
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
        <button type="button" class="btn btn-danger" onclick="confirmRetireSerial()">Conferma dismissione</button>
      </div>
    </div>
  </div>
</div>
<script>
let pollInterval;
let currentMasterId;
let targetVersion;
let updateType = ''; // 'master' o 'slave'
let currentSlaveSn = '';

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
    if(!confirm("Avviare l'aggiornamento firmware alla versione " + version + "?\nIl dispositivo si riavviera'.")) return;

    clearInterval(pollInterval);
    currentMasterId = masterId;
    targetVersion = version;
    updateType = 'master';
    currentSlaveSn = '';

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
    if(!confirm("Avviare l'aggiornamento per lo SLAVE " + slaveSn + " alla versione " + version + "?\nL'operazione verra' gestita dal Master associato.")) return;

    clearInterval(pollInterval);
    currentMasterId = masterId;
    targetVersion = version;
    updateType = 'slave';
    currentSlaveSn = slaveSn;

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
            } else if (masterVer === targetVersion) {
                clearInterval(pollInterval);
                document.getElementById('updateStatusTitle').innerText = "Aggiornamento Completato!";
                document.getElementById('updateStatusText').innerText = "Il dispositivo e' ora aggiornato alla v" + masterVer;
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
        showError("Errore di comunicazione durante il controllo stato.");
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
let pendingRetire = { type: '', masterId: 0, serial: '', replacedBy: '' };

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
    pendingSerialChange = { id: masterId, oldSerial: oldSerial || '', newSerial: newSerial || '' };
    document.getElementById('oldSerialDisplay').innerText = pendingSerialChange.oldSerial || '-';
    document.getElementById('newSerialDisplay').innerText = pendingSerialChange.newSerial || '-';
    document.getElementById('replaceReasonDetails').value = '';
    fillReasonSelect(document.getElementById('replaceReasonCode'), 'master_replaced');
    serialModalInstance.show();
}

function openRetireSlaveModal(masterId, slaveSn) {
    pendingRetire = { type: 'slave', masterId: masterId, serial: slaveSn || '', replacedBy: '' };
    document.getElementById('retireTargetLabel').innerText = `Slave ${pendingRetire.serial}`;
    document.getElementById('retireReasonDetails').value = '';
    document.getElementById('retireReplacedBySerial').value = '';
    fillReasonSelect(document.getElementById('retireReasonCode'), 'field_replaced');
    retireModalInstance.show();
}

function openRetireMasterModal(masterId, oldSerial, detectedSerial) {
    pendingRetire = { type: 'master', masterId: masterId, serial: oldSerial || '', replacedBy: detectedSerial || '' };
    document.getElementById('retireTargetLabel').innerText = `Master ${pendingRetire.serial}`;
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
    const reasonCode = (document.getElementById('retireReasonCode').value || '').trim();
    const reasonDetails = (document.getElementById('retireReasonDetails').value || '').trim();
    const replacedBySerial = (document.getElementById('retireReplacedBySerial').value || '').trim();
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

document.addEventListener('DOMContentLoaded', function () {
    serialModalInstance = new bootstrap.Modal(document.getElementById('serialModal'));
    retireModalInstance = new bootstrap.Modal(document.getElementById('retireSerialModal'));
});
</script>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>

