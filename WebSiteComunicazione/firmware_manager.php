<?php
session_start();
require 'config.php';

// Solo l'admin può accedere a questa pagina
if (!isset($_SESSION['user_id']) || $_SESSION['user_role'] !== 'admin') {
    header("Location: index.php");
    exit;
}

$message = '';
$message_type = '';
$firmware_dir = __DIR__ . '/firmware/';
$target_file = $firmware_dir . 'firmware_latest.bin';

// Assicurati che la cartella esista, altrimenti la crea
if (!is_dir($firmware_dir)) {
    mkdir($firmware_dir, 0755, true);
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_FILES['firmware_file'])) {
    if ($_FILES['firmware_file']['error'] === UPLOAD_ERR_OK) {
        $file_tmp_path = $_FILES['firmware_file']['tmp_name'];
        $file_name = $_FILES['firmware_file']['name'];
        $file_ext = strtolower(pathinfo($file_name, PATHINFO_EXTENSION));

        if ($file_ext === 'bin') {
            if (move_uploaded_file($file_tmp_path, $target_file)) {
                $message = "Firmware caricato con successo come 'firmware_latest.bin'.";
                $message_type = 'success';
            } else {
                $message = "Errore durante lo spostamento del file. Controlla i permessi della cartella 'firmware'.";
                $message_type = 'danger';
            }
        } else {
            $message = "Errore: Caricare solo file con estensione .bin";
            $message_type = 'danger';
        }
    } else {
        $message = "Errore durante il caricamento del file. Codice: " . $_FILES['firmware_file']['error'];
        $message_type = 'danger';
    }
}

$current_firmware_info = '';
if (file_exists($target_file)) {
    $current_firmware_info = "<li>Firmware attuale: <strong>firmware_latest.bin</strong></li>";
    $current_firmware_info .= "<li>Data caricamento: " . date("d/m/Y H:i:s", filemtime($target_file)) . "</li>";
    $current_firmware_info .= "<li>Dimensione: " . round(filesize($target_file) / 1024, 2) . " KB</li>";
    $current_firmware_info .= "<li>MD5: <code>" . md5_file($target_file) . "</code></li>";
} else {
    $current_firmware_info = "<li>Nessun firmware presente sul server.</li>";
}
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Gestione Firmware - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body class="bg-light">
<?php require 'navbar.php'; ?>
<div class="container mt-4">
    <?php if ($message) echo "<div class='alert alert-{$message_type}'>{$message}</div>"; ?>
    <div class="card shadow-sm">
        <div class="card-header"><h5 class="mb-0"><i class="fas fa-cloud-upload-alt"></i> Gestione Firmware OTA</h5></div>
        <div class="card-body">
            <div class="mb-4"><h6>Stato Attuale</h6><ul><?php echo $current_firmware_info; ?></ul></div>
            <hr>
            <h6>Carica Nuovo Firmware</h6>
            <p class="text-muted small">Carica il file <code>firmware.bin</code> generato da PlatformIO. Verrà rinominato in <code>firmware_latest.bin</code> e sostituirà la versione precedente.</p>
            <form method="POST" enctype="multipart/form-data">
                <div class="input-group"><input type="file" name="firmware_file" class="form-control" required accept=".bin"><button type="submit" class="btn btn-primary">Carica e Sostituisci</button></div>
            </form>
        </div>
    </div>
</div>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>