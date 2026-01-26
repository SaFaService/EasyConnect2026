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
                        <th width="25%">Impianto</th>
                        <th width="20%">Indirizzo</th>
                        <th width="15%">Seriale Master</th>
                        <th width="10%">Ver. FW</th>
                        <th width="15%">Ultimo Contatto</th>
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
                        <td class="align-middle"><span class="badge bg-info text-dark"><?php echo htmlspecialchars($m['fw_version']); ?></span></td>
                        <td class="align-middle small text-muted"><?php echo $m['last_seen'] ? date('d/m H:i', strtotime($m['last_seen'])) : 'Mai'; ?></td>
                        <td class="align-middle">
                            <button class="btn btn-sm btn-primary" type="button" data-bs-toggle="collapse" data-bs-target="#details-<?php echo $m['id']; ?>">
                                <i class="fas fa-plus"></i>
                            </button>
                        </td>
                    </tr>
                    <!-- Riga Dettagli Slaves (Nascosta) -->
                    <tr>
                        <td colspan="7" class="p-0 border-0">
                            <div class="collapse slave-details p-3" id="details-<?php echo $m['id']; ?>">
                                <h6><i class="fas fa-microchip"></i> Periferiche Connesse</h6>
                                <?php 
                                    // Recupera le ultime misurazioni per questo master
                                    // NOTA: Questa è una query semplificata. In produzione useremo una query più complessa per avere solo l'ultimo dato per ogni slave.
                                    $stmtS = $pdo->prepare("SELECT * FROM measurements WHERE master_id = ? ORDER BY recorded_at DESC LIMIT 10");
                                    $stmtS->execute([$m['id']]);
                                    $measures = $stmtS->fetchAll();
                                ?>
                                <div class="table-responsive">
                                    <table class="table table-sm table-bordered bg-white">
                                        <thead><th>Data</th><th>Slave SN</th><th>Gruppo</th><th>Pressione</th><th>Temp</th></thead>
                                        <tbody>
                                            <?php foreach($measures as $row): ?>
                                            <tr>
                                                <td><?php echo date('H:i:s', strtotime($row['recorded_at'])); ?></td>
                                                <td><?php echo $row['slave_sn'] ? htmlspecialchars($row['slave_sn']) : 'MASTER (DeltaP)'; ?></td>
                                                <td><?php echo $row['slave_grp']; ?></td>
                                                <td><?php echo $row['pressure'] !== null ? $row['pressure'] . ' Pa' : ($row['delta_p'] !== null ? 'Delta: '.$row['delta_p'] : '-'); ?></td>
                                                <td><?php echo $row['temperature'] !== null ? $row['temperature'] . ' °C' : '-'; ?></td>
                                            </tr>
                                            <?php endforeach; ?>
                                            <?php if(empty($measures)) echo "<tr><td colspan='5' class='text-center'>Nessun dato recente</td></tr>"; ?>
                                        </tbody>
                                    </table>
                                </div>
                                <div class="mt-2">
                                    <a href="download_log.php?id=<?php echo $m['id']; ?>" class="btn btn-sm btn-success"><i class="fas fa-download"></i> Scarica Log CSV</a>
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

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>