<?php
session_start();
require 'config.php';

if (!isset($_SESSION['user_id'])) {
    die("Accesso negato");
}

$master_id = $_GET['master_id'] ?? null;

if (!$master_id) {
    die("ID Impianto mancante");
}

$currentUserId = $_SESSION['user_id'];
$userRole = $_SESSION['user_role'];

// Recupera info impianto per verificare i permessi
$stmt = $pdo->prepare("SELECT * FROM masters WHERE id = ?");
$stmt->execute([$master_id]);
$master = $stmt->fetch();

if (!$master) {
    die("Impianto non trovato");
}

// Verifica Permessi: Admin, Costruttore (del proprio impianto) o Manutentore (assegnato)
$canAccess = false;
if ($userRole === 'admin') {
    $canAccess = true;
} elseif ($userRole === 'builder' && $master['creator_id'] == $currentUserId) {
    $canAccess = true;
} elseif ($userRole === 'maintainer' && $master['maintainer_id'] == $currentUserId) {
    $canAccess = true;
}

if (!$canAccess) {
    die("Non hai i permessi per scaricare lo storico modifiche di questo impianto.");
}

// Query Audit Logs
$stmtLogs = $pdo->prepare("SELECT created_at, action, details FROM audit_logs WHERE master_id = ? ORDER BY created_at DESC");
$stmtLogs->execute([$master_id]);

$filename = "storico_modifiche_impianto_{$master_id}_" . date('Y-m-d') . ".csv";

header('Content-Type: text/csv');
header('Content-Disposition: attachment; filename="' . $filename . '"');

$output = fopen('php://output', 'w');
fputcsv($output, ['Data Ora', 'Azione', 'Dettagli']);

while ($row = $stmtLogs->fetch(PDO::FETCH_ASSOC)) {
    fputcsv($output, $row);
}

fclose($output);
exit;
?>