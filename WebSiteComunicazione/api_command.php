<?php
session_start();
require 'config.php';
header('Content-Type: application/json');

if (!isset($_SESSION['user_id'])) {
    echo json_encode(['status' => 'error', 'message' => 'Non autorizzato']);
    exit;
}

$input = json_decode(file_get_contents('php://input'), true);
$action = $input['action'] ?? '';

if ($action === 'request_update') {
    $mid = $input['master_id'];
    
    // Imposta la richiesta e lo stato iniziale a "Pending"
    $stmtUpd = $pdo->prepare("UPDATE masters SET update_requested = 1, ota_status = 'Pending', ota_message = NULL WHERE id = ?");
    
    if ($stmtUpd->execute([$mid])) {
        echo json_encode(['status' => 'ok', 'message' => 'Richiesta inviata']);
    } else {
        echo json_encode(['status' => 'error', 'message' => 'Errore DB']);
    }
    exit;
}

echo json_encode(['status' => 'error', 'message' => 'Azione non valida']);
?>