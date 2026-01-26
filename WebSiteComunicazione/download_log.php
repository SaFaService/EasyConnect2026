<?php
session_start();
require 'config.php';

// Sicurezza: Solo utenti loggati
if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$master_id = $_GET['id'] ?? 0;

// 1. Recupera info Master
$stmt = $pdo->prepare("SELECT nickname, serial_number FROM masters WHERE id = ?");
$stmt->execute([$master_id]);
$master = $stmt->fetch();

if (!$master) {
    die("Impianto non trovato o ID non valido.");
}

// 2. Imposta nome file (es. Log_ImpiantoPisa_2023-10-25.csv)
$cleanName = preg_replace('/[^a-zA-Z0-9]/', '', $master['nickname']);
$filename = "Log_" . $cleanName . "_" . date('Y-m-d') . ".csv";

// 3. Header per forzare il download
header('Content-Type: text/csv; charset=utf-8');
header('Content-Disposition: attachment; filename="' . $filename . '"');

// 4. Apre l'output stream
$output = fopen('php://output', 'w');

// Intestazione Colonne CSV (Excel le riconosce automaticamente se separate da virgola o punto e virgola)
fputcsv($output, ['Data e Ora', 'Sorgente', 'Gruppo', 'Pressione (Pa)', 'Temperatura (C)', 'Delta P (Pa)']);

// 5. Recupera i dati dal DB
$stmtLogs = $pdo->prepare("SELECT * FROM measurements WHERE master_id = ? ORDER BY recorded_at DESC");
$stmtLogs->execute([$master_id]);

while ($row = $stmtLogs->fetch()) {
    // Determina chi ha generato il dato
    $sorgente = $row['slave_sn'] ? "Slave " . $row['slave_sn'] : "Master Centralina";
    
    fputcsv($output, [
        $row['recorded_at'],
        $sorgente,
        $row['slave_grp'] ?? '-',
        $row['pressure'] !== null ? $row['pressure'] : '-',
        $row['temperature'] !== null ? $row['temperature'] : '-',
        $row['delta_p'] !== null ? $row['delta_p'] : '-'
    ]);
}

fclose($output);
exit;
?>