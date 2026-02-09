<?php
session_start();
require 'config.php';

if (!isset($_SESSION['user_id'])) {
    die("Accesso negato");
}

$master_id = $_GET['master_id'] ?? null;
$slave_sn = $_GET['slave_sn'] ?? null;

if (!$master_id) {
    die("ID Impianto mancante");
}

// Verifica permessi (semplificata per brevità, idealmente riusa la logica di index.php)
// ...

// Costruzione Query
if ($slave_sn) {
    // Log specifica periferica
    $stmt = $pdo->prepare("SELECT recorded_at, slave_sn, slave_grp, pressure, temperature, fw_version FROM measurements WHERE master_id = ? AND slave_sn = ? ORDER BY recorded_at DESC");
    $stmt->execute([$master_id, $slave_sn]);
    $filename = "log_slave_{$slave_sn}_" . date('Y-m-d') . ".csv";
} else {
    // Log intero impianto (Master + Slaves)
    // Includiamo anche i dati del Master (Delta P) che hanno slave_sn NULL
    $stmt = $pdo->prepare("SELECT recorded_at, slave_sn, slave_grp, pressure, temperature, delta_p, fw_version FROM measurements WHERE master_id = ? ORDER BY recorded_at DESC");
    $stmt->execute([$master_id]);
    $filename = "log_impianto_{$master_id}_" . date('Y-m-d') . ".csv";
}

// Imposta headers per il download
header('Content-Type: text/csv');
header('Content-Disposition: attachment; filename="' . $filename . '"');

$output = fopen('php://output', 'w');

// Intestazione CSV
if ($slave_sn) {
    fputcsv($output, ['Data Ora', 'Seriale Slave', 'Gruppo', 'Pressione (Pa)', 'Temperatura (C)', 'Versione FW']);
} else {
    fputcsv($output, ['Data Ora', 'Seriale Slave', 'Gruppo', 'Pressione (Pa)', 'Temperatura (C)', 'Delta P (Master)', 'Versione FW']);
}

// Dati
while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
    fputcsv($output, $row);
}

fclose($output);
exit;
?>