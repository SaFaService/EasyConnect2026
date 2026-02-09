<?php
session_start();
require 'config.php';

// Includi il gestore della lingua
require 'lang.php';

// Protezione: Solo Admin e Costruttori possono accedere
if (!isset($_SESSION['user_id']) || ($_SESSION['user_role'] !== 'admin' && $_SESSION['user_role'] !== 'builder')) {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';
$currentUserId = $_SESSION['user_id'];

// Funzione helper per convertire link Google Drive in link diretti
function convertDriveLink($url) {
    // Cerca l'ID del file in vari formati di link di Google Drive (es. /d/FILE_ID/ o ?id=FILE_ID)
    if (preg_match('/\/d\/([a-zA-Z0-9_-]+)/', $url, $matches) || preg_match('/id=([a-zA-Z0-9_-]+)/', $url, $matches)) {
        if (isset($matches[1])) {
            return "https://drive.google.com/uc?export=download&id=" . $matches[1];
        }
    }
    return $url; // Se non corrisponde o è già un link diretto, lo ritorna così com'è
}

// --- GESTIONE POST ---
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';

    try {
        // 1. AGGIUNGI NUOVO FIRMWARE
        if ($action === 'add_firmware') {
            $device_type = $_POST['device_type'];
            $version = $_POST['version_number']; // Calcolato dal JS o inserito a mano
            $drive_link = $_POST['drive_link'];
            $description = $_POST['description'];
            
            $direct_link = convertDriveLink($drive_link);

            if (!empty($version) && !empty($direct_link)) {
                // Disattiva eventuali versioni attive precedenti per questo tipo (opzionale, se vogliamo auto-attivare la nuova)
                // $pdo->prepare("UPDATE firmware_releases SET is_active = 0 WHERE device_type = ?")->execute([$device_type]);

                $stmt = $pdo->prepare("INSERT INTO firmware_releases (device_type, version, download_url, description, creator_id, is_active) VALUES (?, ?, ?, ?, ?, 0)");
                $stmt->execute([$device_type, $version, $direct_link, $description, $currentUserId]);
                
                $message = "Firmware v{$version} aggiunto correttamente!";
                $message_type = 'success';
            } else {
                $message = "Versione e Link Google Drive sono obbligatori.";
                $message_type = 'danger';
            }
        }

        // 2. IMPOSTA COME ATTIVO (ROLLBACK/UPGRADE)
        if ($action === 'set_active') {
            $fw_id = $_POST['fw_id'];
            $device_type = $_POST['device_type'];

            // Prima disattiva tutti i firmware di quel tipo
            $pdo->prepare("UPDATE firmware_releases SET is_active = 0 WHERE device_type = ?")->execute([$device_type]);
            
            // Poi attiva quello scelto
            $stmt = $pdo->prepare("UPDATE firmware_releases SET is_active = 1 WHERE id = ?");
            $stmt->execute([$fw_id]);

            $message = "Versione attiva aggiornata. I dispositivi scaricheranno questa versione al prossimo controllo.";
            $message_type = 'success';
        }

        // 3. ELIMINA FIRMWARE
        if ($action === 'delete_firmware') {
            $fw_id = $_POST['fw_id'];
            $stmt = $pdo->prepare("DELETE FROM firmware_releases WHERE id = ?");
            $stmt->execute([$fw_id]);
            $message = "Release firmware eliminata.";
            $message_type = 'warning';
        }
    } catch (PDOException $e) {
        $message = "Errore Database: " . $e->getMessage();
        $message_type = 'danger';
    }
}

// Recupera l'ultima versione per ogni tipo (per il JS)
$latestVersions = [];
$firmwares = [];
$types = ['master', 'slave_pressure', 'slave_relay'];

try {
    foreach ($types as $t) {
        $stmt = $pdo->prepare("SELECT version FROM firmware_releases WHERE device_type = ? ORDER BY id DESC LIMIT 1");
        $stmt->execute([$t]);
        $row = $stmt->fetch();
        $latestVersions[$t] = $row ? $row['version'] : '0.0.0';
    }

    // Recupera lista completa
    $stmtList = $pdo->query("SELECT f.*, u.email as creator_email FROM firmware_releases f LEFT JOIN users u ON f.creator_id = u.id ORDER BY f.device_type, f.id DESC");
    $firmwares = $stmtList->fetchAll();
} catch (PDOException $e) {
    $message = "Errore critico: Impossibile leggere dal database. Assicurati di aver creato la tabella 'firmware_releases'. <br>Dettagli: " . $e->getMessage();
    $message_type = 'danger';
}

?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo $lang['fw_title']; ?> - Antralux</title>
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

    <div class="row">
        <!-- COLONNA SINISTRA: CARICAMENTO -->
        <div class="col-md-4">
            <div class="card shadow-sm mb-4">
                <div class="card-header bg-primary text-white">
                    <h5 class="mb-0"><i class="fas fa-cloud-upload-alt"></i> <?php echo $lang['fw_new_release']; ?></h5>
                </div>
                <div class="card-body">
                    <form method="POST">
                        <input type="hidden" name="action" value="add_firmware">
                        
                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['fw_target']; ?></label>
                            <select name="device_type" id="deviceType" class="form-select" onchange="updateVersionSuggestion()">
                                <option value="master"><?php echo $lang['fw_target_master']; ?></option>
                                <option value="slave_pressure"><?php echo $lang['fw_target_slave_p']; ?></option>
                                <option value="slave_relay"><?php echo $lang['fw_target_slave_r']; ?></option>
                            </select>
                            <small class="text-muted"><?php echo $lang['fw_last_ver']; ?> <span id="lastVerDisplay">...</span></small>
                        </div>

                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['fw_update_type']; ?></label>
                            <div class="btn-group w-100" role="group">
                                <input type="radio" class="btn-check" name="update_type" id="typePatch" value="patch" checked onchange="updateVersionSuggestion()">
                                <label class="btn btn-outline-secondary" for="typePatch">Patch (z)</label>

                                <input type="radio" class="btn-check" name="update_type" id="typeMinor" value="minor" onchange="updateVersionSuggestion()">
                                <label class="btn btn-outline-secondary" for="typeMinor">Minor (y)</label>

                                <input type="radio" class="btn-check" name="update_type" id="typeMajor" value="major" onchange="updateVersionSuggestion()">
                                <label class="btn btn-outline-secondary" for="typeMajor">Major (x)</label>
                            </div>
                        </div>

                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['fw_ver_input']; ?></label>
                            <input type="text" name="version_number" id="versionInput" class="form-control fw-bold" required>
                        </div>

                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['fw_drive_link']; ?></label>
                            <input type="url" name="drive_link" class="form-control" placeholder="https://drive.google.com/..." required>
                            <div class="form-text"><?php echo $lang['fw_drive_help']; ?></div>
                        </div>

                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['fw_notes']; ?></label>
                            <textarea name="description" class="form-control" rows="3" placeholder="<?php echo $lang['fw_notes_placeholder']; ?>"></textarea>
                        </div>

                        <button type="submit" class="btn btn-primary w-100"><?php echo $lang['fw_btn_register']; ?></button>
                    </form>
                </div>
            </div>
        </div>

        <!-- COLONNA DESTRA: LISTA E GESTIONE -->
        <div class="col-md-8">
            <div class="card shadow-sm">
                <div class="card-header">
                    <h5 class="mb-0"><i class="fas fa-history"></i> <?php echo $lang['fw_history_title']; ?></h5>
                </div>
                <div class="card-body p-0">
                    <div class="table-responsive">
                        <table class="table table-hover mb-0 align-middle">
                            <thead class="table-light">
                                <tr>
                                    <th><?php echo $lang['fw_col_ver']; ?></th>
                                    <th><?php echo $lang['fw_col_type']; ?></th>
                                    <th><?php echo $lang['fw_col_notes']; ?></th>
                                    <th><?php echo $lang['fw_col_date']; ?></th>
                                    <th><?php echo $lang['fw_col_status']; ?></th>
                                    <th><?php echo $lang['fw_col_actions']; ?></th>
                                </tr>
                            </thead>
                            <tbody>
                                <?php foreach ($firmwares as $fw): ?>
                                    <tr class="<?php echo $fw['is_active'] ? 'table-success' : ''; ?>">
                                        <td><strong><?php echo htmlspecialchars($fw['version']); ?></strong></td>
                                        <td>
                                            <?php 
                                            if($fw['device_type'] == 'master') echo '<span class="badge bg-dark">' . $lang['fw_target_master'] . '</span>';
                                            elseif($fw['device_type'] == 'slave_pressure') echo '<span class="badge bg-info text-dark">' . $lang['fw_target_slave_p'] . '</span>';
                                            else echo '<span class="badge bg-secondary">' . $lang['fw_target_slave_r'] . '</span>';
                                            ?>
                                        </td>
                                        <td><small><?php echo htmlspecialchars($fw['description']); ?></small></td>
                                        <td><small><?php echo date('d/m/Y H:i', strtotime($fw['created_at'])); ?></small></td>
                                        <td>
                                            <?php if ($fw['is_active']): ?>
                                                <span class="badge bg-success"><?php echo $lang['fw_active']; ?></span>
                                            <?php else: ?>
                                                <span class="text-muted">-</span>
                                            <?php endif; ?>
                                        </td>
                                        <td>
                                            <form method="POST" class="d-inline">
                                                <input type="hidden" name="fw_id" value="<?php echo $fw['id']; ?>">
                                                <input type="hidden" name="device_type" value="<?php echo $fw['device_type']; ?>">
                                                
                                                <?php if (!$fw['is_active']): ?>
                                                    <button type="submit" name="action" value="set_active" class="btn btn-sm btn-outline-success" title="<?php echo $lang['fw_btn_activate_tooltip']; ?>">
                                                        <i class="fas fa-check"></i> <?php echo $lang['fw_btn_activate']; ?>
                                                    </button>
                                                <?php endif; ?>
                                                
                                                <button type="submit" name="action" value="delete_firmware" class="btn btn-sm btn-outline-danger" onclick="return confirm('<?php echo $lang['fw_delete_confirm']; ?>');">
                                                    <i class="fas fa-trash"></i>
                                                </button>
                                            </form>
                                        </td>
                                    </tr>
                                <?php endforeach; ?>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>

<script>
// Dati passati da PHP a JS
const latestVersions = <?php echo json_encode($latestVersions); ?>;

function updateVersionSuggestion() {
    const type = document.getElementById('deviceType').value;
    const currentVer = latestVersions[type] || '0.0.0';
    document.getElementById('lastVerDisplay').innerText = currentVer;

    // Parsing SemVer
    let parts = currentVer.split('.').map(Number);
    if (parts.length < 3) parts = [0, 0, 0];

    // Logica incremento
    if (document.getElementById('typeMajor').checked) {
        parts[0]++; parts[1] = 0; parts[2] = 0;
    } else if (document.getElementById('typeMinor').checked) {
        parts[1]++; parts[2] = 0;
    } else { // Patch
        parts[2]++;
    }

    document.getElementById('versionInput').value = parts.join('.');
}

// Inizializza al caricamento
document.addEventListener('DOMContentLoaded', updateVersionSuggestion);
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
