<?php
session_start();
require 'config.php';

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

    // Azione: Eliminare un master
    if ($action === 'delete_master') {
        $id = $_POST['master_id'];
        
        // Admin può cancellare (logicamente) qualsiasi impianto
        if ($isAdmin) {
            $stmt = $pdo->prepare("UPDATE masters SET deleted_at = NOW() WHERE id = ?");
            $stmt->execute([$id]);
        } else {
            // Costruttori e Clienti possono cancellare solo i propri
            $stmt = $pdo->prepare("UPDATE masters SET deleted_at = NOW() WHERE id = ? AND (creator_id = ? OR owner_id = ?)");
            $stmt->execute([$id, $currentUserId, $currentUserId]);
        }
        $message = "Impianto rimosso dalla dashboard.";
        $message_type = 'warning';
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
        // La foreign key con ON DELETE CASCADE si occuperà di eliminare i record collegati
        // in 'measurements' e 'maintainer_requests'.
        $stmt = $pdo->prepare("DELETE FROM masters WHERE id = ?");
        $stmt->execute([$id]);
        $message = "Impianto eliminato definitivamente dal sistema.";
        $message_type = 'danger';
    }

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
                   WHERE (m.creator_id = :userId OR m.owner_id = :userId OR m.maintainer_id = :userId) AND m.deleted_at IS NULL 
                   ORDER BY m.nickname ASC";
    $stmtM = $pdo->prepare($sqlMasters);
    $stmtM->execute(['userId' => $currentUserId]);
}
$masters = $stmtM->fetchAll();
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Configurazione - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body class="bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4">

    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <!-- Card per Aggiungere un Nuovo Impianto -->
    <?php if ($isAdmin || $isBuilder || $isClient): // Solo questi ruoli possono creare impianti ?>
    <div class="card shadow-sm mb-4">
        <div class="card-header">
            <h5 class="mb-0"><i class="fas fa-plus-circle"></i> Aggiungi Nuovo Impianto</h5>
        </div>
        <div class="card-body">
            <form method="POST">
                <input type="hidden" name="action" value="add_master">
                <div class="row">
                    <div class="col-md-4 mb-3">
                        <label class="form-label">Nickname Impianto</label>
                        <input type="text" name="nickname" class="form-control" placeholder="Es. Impianto Pisa" required>
                    </div>
                    <div class="col-md-4 mb-3">
                        <label class="form-label">Numero Seriale Master</label>
                        <input type="text" name="serial_number" class="form-control" placeholder="Es. 2023110001" required>
                    </div>
                    <div class="col-md-4 mb-3">
                        <label class="form-label">Indirizzo</label>
                        <input type="text" name="address" class="form-control" placeholder="Via, Città, CAP">
                    </div>
                </div>
                <button type="submit" class="btn btn-primary">Aggiungi Impianto</button>
            </form>
        </div>
    </div>
    <?php endif; ?>

    <!-- Card per Gestire gli Impianti Esistenti -->
    <div class="card shadow-sm">
        <div class="card-header">
            <h5 class="mb-0"><i class="fas fa-sliders-h"></i> Gestisci Impianti Esistenti</h5>
        </div>
        <div class="card-body">
            <?php foreach ($masters as $master): ?>
                <?php
                    $isDeleted = !empty($master['deleted_at']);
                    $bgStyle = $isDeleted ? "background-color: #ffe6e6;" : "";
                    // Determina se l'utente corrente può modificare/assegnare questo impianto
                    $canManage = $isAdmin || ($isBuilder && $master['creator_id'] == $currentUserId);
                ?>
                <form method="POST" class="border rounded p-3 mb-3" style="<?php echo $bgStyle; ?>">
                    <input type="hidden" name="master_id" value="<?php echo $master['id']; ?>">
                    
                    <h5>
                        <?php echo htmlspecialchars($master['nickname']); ?>
                        <?php if($isDeleted) echo " <span class='badge bg-danger'>ELIMINATO</span>"; ?>
                        <?php if($isAdmin) echo " <small class='text-muted'>(Creatore: " . ($master['creator_email'] ?? 'N/D') . ")</small>"; ?>
                    </h5>
                    
                    <div class="mb-2 small text-muted">
                        Proprietario: <?php echo $master['owner_email'] ?? 'Non assegnato'; ?> | 
                        Manutentore: <?php echo $master['maintainer_email'] ?? 'Non assegnato'; ?>
                    </div>

                    <div class="input-group mb-3">
                        <span class="input-group-text">API Key</span>
                        <input type="text" class="form-control" value="<?php echo $master['api_key']; ?>" id="apiKey-<?php echo $master['id']; ?>" readonly>
                        <button class="btn btn-outline-secondary" type="button" onclick="copyToClipboard('apiKey-<?php echo $master['id']; ?>')"><i class="fas fa-copy"></i> Copia</button>
                    </div>

                    <div class="row">
                        <div class="col-md-4"><label>Nickname</label><input type="text" name="nickname" class="form-control" value="<?php echo htmlspecialchars($master['nickname']); ?>" <?php if(!$canManage) echo 'readonly'; ?>></div>
                        <div class="col-md-5"><label>Indirizzo</label><input type="text" name="address" class="form-control" value="<?php echo htmlspecialchars($master['address']); ?>" <?php if(!$canManage) echo 'readonly'; ?>></div>
                        <div class="col-md-3"><label>Conserva Log (giorni)</label><input type="number" name="log_retention_days" class="form-control" value="<?php echo $master['log_retention_days']; ?>" min="1" max="365" <?php if(!$canManage) echo 'readonly'; ?>></div>
                    </div>

                    <?php if ($canManage): // Mostra i campi di assegnazione solo a chi può gestire ?>
                        <div class="row mt-3">
                            <div class="col-md-6">
                                <label>Assegna a Proprietario (Cliente)</label>
                                <select name="owner_id" class="form-select">
                                    <option value="">Nessuno</option>
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
                                <label>Assegna a Manutentore</label>
                                <select name="maintainer_id" class="form-select">
                                    <option value="">Nessuno</option>
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
                            <button type="submit" name="action" value="update_master" class="btn btn-info btn-sm">Salva Dati</button>
                            <button type="submit" name="action" value="assign_master" class="btn btn-success btn-sm">Salva Assegnazioni</button>
                        <?php endif; ?>

                        <?php if(!$isDeleted && ($isAdmin || $isBuilder || $isClient)): ?>
                            <button type="submit" name="action" value="delete_master" class="btn btn-danger btn-sm" onclick="return confirm('Sei sicuro di voler rimuovere questo impianto?');">Elimina</button>
                        <?php endif; ?>

                        <?php if($isDeleted && $isAdmin): ?>
                            <button type="submit" name="action" value="restore_master" class="btn btn-warning btn-sm">Ripristina</button>
                            <button type="submit" name="action" value="hard_delete_master" class="btn btn-dark btn-sm" onclick="return confirm('Sei sicuro di voler eliminare definitivamente questo impianto? Questa operazione non potrà più essere ripristinata.');">Elimina Definitivamente</button>
                        <?php endif; ?>
                    </div>
                </form>
            <?php endforeach; ?>
        </div>
    </div>
</div>

<script>
function copyToClipboard(elementId) {
    var copyText = document.getElementById(elementId);
    copyText.select();
    document.execCommand("copy");
    alert("Chiave API copiata negli appunti!");
}
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>