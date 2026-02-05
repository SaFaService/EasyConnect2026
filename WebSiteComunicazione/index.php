<?php
session_start();
require 'config.php';

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
            WHERE (m.creator_id = :userId OR m.owner_id = :userId OR m.maintainer_id = :userId) 
            AND m.deleted_at IS NULL 
            ORDER BY m.created_at DESC";
    $stmt = $pdo->prepare($sql);
    $stmt->execute(['userId' => $currentUserId]);
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
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Dashboard - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <!-- FontAwesome per le icone -->
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <style>
        .status-dot { height: 12px; width: 12px; border-radius: 50%; display: inline-block; }
        .online { background-color: #28a745; }
        .offline { background-color: #dc3545; }
        .slave-details { background-color: #f8f9fa; font-size: 0.9rem; }
    </style>
</head>
<body class="bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4">
    <div class="card shadow-sm">
        <div class="card-header bg-white">
            <h5 class="mb-0"><i class="fas fa-server"></i> Elenco Impianti</h5>
        </div>
        <div class="card-body p-0">
            <table class="table table-hover mb-0">
                <thead class="table-light">
                    <tr>
                        <th width="5%">Stato</th>
                        <th width="20%">Impianto</th>
                        <th width="15%">Indirizzo</th>
                        <th width="15%">Seriale</th>
                        <th width="10%">Delta P</th>
                        <th width="10%">Ver. FW</th>
                        <th width="5%">Segnale</th>
                        <th width="10%">Ultimo</th>
                        <th width="10%">Dettagli</th>
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

                        // Cerca l'ultimo valore di Delta P (associato al Master, quindi slave_sn vuoto)
                        $deltaPValue = '-';
                        foreach ($measures as $ms) {
                            if (empty($ms['slave_sn']) && isset($ms['delta_p'])) {
                                $deltaPValue = $ms['delta_p'] . ' Pa';
                                break;
                            }
                        }

                        // Recupera la LISTA delle periferiche connesse (Ultimo dato per ognuna)
                        // Questa query complessa prende l'ultima misurazione per ogni slave_sn distinto
                        $sqlSlaves = "SELECT m1.* 
                                      FROM measurements m1
                                      INNER JOIN (
                                          SELECT slave_sn, MAX(recorded_at) as max_date
                                          FROM measurements
                                          WHERE master_id = ? AND slave_sn IS NOT NULL
                                          GROUP BY slave_sn
                                      ) m2 ON m1.slave_sn = m2.slave_sn AND m1.recorded_at = m2.max_date
                                      WHERE m1.master_id = ?";
                        $stmtSlaves = $pdo->prepare($sqlSlaves);
                        $stmtSlaves->execute([$m['id']]);
                        $slavesList = $stmtSlaves->fetchAll();
                    ?>
                    <tr style="<?php echo $rowStyle; ?>">
                        <td class="text-center align-middle"><?php echo $statusDot; ?></td>
                        <td class="align-middle">
                            <strong><?php echo htmlspecialchars($m['nickname']); ?></strong>
                            <?php if($isAdmin): ?>
                                <div class="small text-muted">
                                    Prop: <?php echo $m['owner_email'] ?? 'N/D'; ?><br>
                                    Man: <?php echo $m['maintainer_email'] ?? 'N/D'; ?>
                                </div>
                            <?php endif; ?>
                        </td>
                        <td class="align-middle">
                            <a href="<?php echo $mapUrl; ?>" target="_blank" class="text-decoration-none text-secondary">
                                <i class="fas fa-map-marker-alt text-danger"></i> <?php echo htmlspecialchars($m['address']); ?>
                            </a>
                        </td>
                        <td class="align-middle"><?php echo htmlspecialchars($m['serial_number']); ?></td>
                        <td class="align-middle"><strong><?php echo $deltaPValue; ?></strong></td>
                        <td class="align-middle"><span class="badge bg-info text-dark"><?php echo htmlspecialchars($m['fw_version']); ?></span></td>
                        <td class="align-middle text-center"><i class="fas <?php echo $signalIcon; ?> <?php echo $signalColor; ?>" title="<?php echo $rssi; ?> dBm"></i></td>
                        <td class="align-middle small text-muted"><?php echo $m['last_seen'] ? date('d/m H:i', strtotime($m['last_seen'])) : 'Mai'; ?></td>
                        <td class="align-middle">
                            <button class="btn btn-sm btn-primary" type="button" data-bs-toggle="collapse" data-bs-target="#details-<?php echo $m['id']; ?>">
                            <button class="btn btn-sm btn-outline-primary" type="button" data-bs-toggle="collapse" data-bs-target="#details-<?php echo $m['id']; ?>">
                                <i class="fas fa-plus"></i>
                            </button>
                        </td>
                    </tr>
                    <!-- Riga Dettagli Slaves (Nascosta) -->
                    <!-- Riga Dettagli MASTER (Contiene lista periferiche) -->
                    <tr>
                        <td colspan="8" class="p-0 border-0">
                            <div class="collapse slave-details p-3" id="details-<?php echo $m['id']; ?>">
                                <h6><i class="fas fa-microchip"></i> Periferiche Connesse</h6>
                                
                                <!-- Intestazione Dettagli Master -->
                                <div class="d-flex justify-content-between align-items-center mb-3">
                                    <h6 class="mb-0 text-primary"><i class="fas fa-network-wired"></i> Periferiche Connesse</h6>
                                    <div>
                                        <a href="download_csv.php?master_id=<?php echo $m['id']; ?>" class="btn btn-sm btn-success me-2"><i class="fas fa-file-csv"></i> Log Impianto</a>
                                        
                                        <!-- PULSANTE AGGIORNAMENTO MASTER -->
                                        <?php 
                                            $updateAvailable = ($latestFw && $m['fw_version'] !== $latestFw);
                                            $canUpdate = ($rssi >= -75);
                                            $btnClass = ($canUpdate && $updateAvailable) ? "btn-warning" : "btn-outline-secondary";
                                            $btnAttr = $canUpdate ? "" : "disabled";
                                            $btnText = $updateAvailable ? "Aggiorna Master v$latestFw" : "Master Aggiornato";
                                            
                                            if ($m['update_requested'] == 1) {
                                                echo '<span class="badge bg-warning text-dark me-2"><i class="fas fa-sync fa-spin"></i> Master in Aggiornamento...</span>';
                                            } elseif ($m['ota_status'] === 'Failed') {
                                                echo '<button type="button" class="btn btn-sm btn-danger me-2" onclick="startUpdate('.$m['id'].', \''.$latestFw.'\')"><i class="fas fa-redo"></i> Riprova Master</button>';
                                            } else {
                                                echo '<button type="button" class="btn btn-sm '.$btnClass.' me-2" '.$btnAttr.' onclick="startUpdate('.$m['id'].', \''.$latestFw.'\')"><i class="fas fa-microchip"></i> '.$btnText.'</button>';
                                            }
                                        ?>
                                    </div>
                                </div>

                                <!-- Tabella Periferiche -->
                                <div class="table-responsive">
                                    <table class="table table-sm table-bordered bg-white">
                                        <thead><th>Data</th><th>Slave SN</th><th>Gruppo</th><th>Pressione</th><th>Temp</th></thead>
                                    <table class="table table-sm table-hover bg-white border rounded">
                                        <thead class="table-light">
                                            <tr>
                                                <th>Tipo</th>
                                                <th>Seriale</th>
                                                <th>Gruppo</th>
                                                <th>Pressione</th>
                                                <th>Temp</th>
                                                <th>Ver. FW</th>
                                                <th>Ultimo Dato</th>
                                                <th>Azioni</th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <?php foreach($measures as $row): 
                                                // Salta la riga del Master (DeltaP) perché ora è visualizzata nella tabella principale
                                                if (empty($row['slave_sn'])) continue; 
                                            <?php foreach($slavesList as $slave): 
                                                $slaveUpdAvail = ($latestFwSlaveP && ($slave['fw_version'] ?? '0.0.0') !== $latestFwSlaveP);
                                                $slaveBtnClass = $slaveUpdAvail ? "btn-warning" : "btn-outline-secondary";
                                                // Nota: Aggiornamento slave non ancora implementato nel backend, ma UI pronta
                                            ?>
                                            <tr>
                                                <td><?php echo date('H:i:s', strtotime($row['recorded_at'])); ?></td>
                                                <td><?php echo $row['slave_sn'] ? htmlspecialchars($row['slave_sn']) : 'MASTER (DeltaP)'; ?></td>
                                                <td><?php echo $row['slave_grp']; ?></td>
                                                <td><?php echo $row['pressure'] !== null ? $row['pressure'] . ' Pa' : ($row['delta_p'] !== null ? 'Delta: '.$row['delta_p'] : '-'); ?></td>
                                                <td><?php echo $row['temperature'] !== null ? $row['temperature'] . ' °C' : '-'; ?></td>
                                                <td><span class="badge bg-info text-dark">Pressione</span></td>
                                                <td><strong><?php echo htmlspecialchars($slave['slave_sn']); ?></strong></td>
                                                <td><?php echo $slave['slave_grp']; ?></td>
                                                <td><?php echo $slave['pressure']; ?> Pa</td>
                                                <td><?php echo $slave['temperature']; ?> °C</td>
                                                <td><?php echo htmlspecialchars($slave['fw_version'] ?? 'N/D'); ?></td>
                                                <td><?php echo date('H:i:s', strtotime($slave['recorded_at'])); ?></td>
                                                <td>
                                                    <button class="btn btn-xs btn-outline-dark" type="button" data-bs-toggle="collapse" data-bs-target="#history-<?php echo $slave['slave_sn']; ?>">
                                                        <i class="fas fa-history"></i>
                                                    </button>
                                                    <a href="download_csv.php?master_id=<?php echo $m['id']; ?>&slave_sn=<?php echo $slave['slave_sn']; ?>" class="btn btn-xs btn-outline-success" title="Scarica CSV"><i class="fas fa-download"></i></a>
                                                    <button class="btn btn-xs <?php echo $slaveBtnClass; ?>" title="Aggiorna Slave" onclick="alert('Aggiornamento Slave non ancora disponibile.')"><i class="fas fa-sync"></i></button>
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
                                            <?php if(empty($measures)) echo "<tr><td colspan='5' class='text-center'>Nessun dato recente</td></tr>"; ?>
                                            <?php if(empty($slavesList)) echo "<tr><td colspan='8' class='text-center text-muted'>Nessuna periferica rilevata.</td></tr>"; ?>
                                        </tbody>
                                    </table>
                                </div>
                                <div class="mt-2 d-flex justify-content-between align-items-center">
                                    <a href="download_log.php?id=<?php echo $m['id']; ?>" class="btn btn-sm btn-success"><i class="fas fa-download"></i> Scarica Log CSV</a>
                                    
                                    <!-- PULSANTE AGGIORNAMENTO FIRMWARE -->
                                    <?php 
                                        // Verifica disponibilità aggiornamento
                                        $updateAvailable = ($latestFw && $m['fw_version'] !== $latestFw);
                                        $canUpdate = ($rssi >= -75); // Soglia di sicurezza
                                        
                                        $btnClass = ($canUpdate && $updateAvailable) ? "btn-primary" : "btn-outline-secondary";
                                        $btnAttr = $canUpdate ? "" : "disabled";
                                        $tooltip = $canUpdate ? "Avvia aggiornamento remoto" : "Segnale WiFi Debole (" . $rssi . "dBm), impossibile avviare l'aggiornamento in sicurezza.";
                                        $btnText = $updateAvailable ? "Aggiorna a v$latestFw" : "Reinstalla Firmware";

                                        // Se è già stato richiesto
                                        if ($m['update_requested'] == 1) {
                                            if ($m['ota_status'] === 'Pending') {
                                                echo '<span class="badge bg-info text-dark"><i class="fas fa-sync fa-spin"></i> In attesa del dispositivo...</span>';
                                            } else {
                                                echo '<span class="badge bg-warning text-dark"><i class="fas fa-sync fa-spin"></i> Aggiornamento Richiesto...</span>';
                                            }
                                        } elseif ($m['ota_status'] === 'Failed') {
                                            echo '<div class="text-danger small mb-1"><i class="fas fa-exclamation-triangle"></i> <strong>Fallito:</strong> ' . htmlspecialchars($m['ota_message']) . '</div>';
                                            // Mostra di nuovo il pulsante per riprovare
                                            ?>
                                            <button type="button" class="btn btn-sm btn-danger" onclick="startUpdate(<?php echo $m['id']; ?>, '<?php echo $latestFw; ?>')">
                                                <i class="fas fa-redo"></i> Riprova
                                            </button>
                                            <?php
                                        } else {
                                    ?>
                                    <button type="button" class="btn btn-sm <?php echo $btnClass; ?>" <?php echo $btnAttr; ?> title="<?php echo $tooltip; ?>" 
                                            onclick="startUpdate(<?php echo $m['id']; ?>, '<?php echo $latestFw; ?>')">
                                        <i class="fas fa-cloud-upload-alt"></i> <?php echo $btnText; ?>
                                    </button>
                                    <?php if($updateAvailable && !$m['update_requested'] && $m['ota_status'] !== 'Failed') echo "<small class='text-success ms-2'><i class='fas fa-arrow-up'></i> v$latestFw disponibile</small>"; ?>
                                    <?php } ?>
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

<script>
let pollInterval;
let currentMasterId;
let targetVersion;

function startUpdate(masterId, version) {
    if(!confirm("Avviare l'aggiornamento firmware alla versione " + version + "?\nIl dispositivo si riavvierà.")) return;

    currentMasterId = masterId;
    targetVersion = version;
    
    // Mostra Modal
    const modal = new bootstrap.Modal(document.getElementById('updateModal'));
    modal.show();

    // Invia comando
    fetch('api_command.php', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ action: 'request_update', master_id: masterId })
    })
    .then(res => res.json())
    .then(data => {
        if(data.status === 'ok') {
            document.getElementById('updateProgressBar').style.width = "10%";
            pollInterval = setInterval(checkStatus, 2000); // Controlla ogni 2 secondi
        } else {
            showError("Errore invio comando: " + data.message);
        }
    });
}

function checkStatus() {
    fetch('api_check_status.php?master_id=' + currentMasterId)
    .then(res => res.json())
    .then(data => {
        const status = data.ota_status;
        const ver = data.fw_version;

        if (status === 'Pending') {
            document.getElementById('updateStatusTitle').innerText = "In Attesa del Dispositivo";
            document.getElementById('updateStatusText').innerText = "Il dispositivo riceverà il comando al prossimo controllo (max 2 min)...";
            document.getElementById('updateProgressBar').style.width = "30%";
        } else if (status === 'InProgress') {
            document.getElementById('updateStatusTitle').innerText = "Aggiornamento in Corso";
            document.getElementById('updateStatusText').innerText = "Il dispositivo sta scaricando e installando il firmware...";
            document.getElementById('updateProgressBar').style.width = "70%";
            document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-primary";
        } else if (status === 'Failed') {
            showError("Aggiornamento Fallito: " + data.ota_message);
        }

        // Controllo Successo (Versione aggiornata)
        if (ver === targetVersion) {
            clearInterval(pollInterval);
            document.getElementById('updateStatusTitle').innerText = "Aggiornamento Completato!";
            document.getElementById('updateStatusText').innerText = "Il dispositivo è ora aggiornato alla v" + ver;
            document.getElementById('updateProgressBar').style.width = "100%";
            document.getElementById('updateProgressBar').className = "progress-bar bg-success";
            document.getElementById('iconStep2').className = "fas fa-cloud-download-alt fa-2x text-success";
            document.getElementById('iconStep3').className = "fas fa-check-circle fa-2x text-success";
            document.getElementById('btnCloseModal').classList.remove('disabled');
            document.getElementById('btnCloseModal').className = "btn btn-success";
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
</script>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>