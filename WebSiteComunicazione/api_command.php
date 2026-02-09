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

// Prima di ogni nuovo comando, è buona norma resettare tutti gli stati OTA per evitare dati sporchi da operazioni precedenti.
$resetStmt = $pdo->prepare("UPDATE masters SET 
    update_requested = 0, 
    ota_status = NULL, 
    ota_message = NULL, 
    slave_update_request_sn = NULL, 
    slave_ota_status = NULL, 
    slave_ota_message = NULL 
    WHERE id = ?");

if ($action === 'request_update') {
    $mid = $input['master_id'];
    $resetStmt->execute([$mid]); // Esegui il reset
    
    // Imposta la nuova richiesta per il master
    $stmtUpd = $pdo->prepare("UPDATE masters SET update_requested = 1, ota_status = 'Pending' WHERE id = ?");
    
    if ($stmtUpd->execute([$mid])) {
        // --- AUDIT LOG ---
        $logDetails = "Richiesto aggiornamento firmware Master. Utente ID: " . $_SESSION['user_id'];
        $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, 'OTA_REQ_MASTER', ?)")->execute([$mid, $logDetails]);

        echo json_encode(['status' => 'ok', 'message' => 'Richiesta inviata']);
    } else {
        echo json_encode(['status' => 'error', 'message' => 'Errore DB']);
    }
    exit;
}

if ($action === 'request_slave_update') {
    $mid = $input['master_id'];
    $slave_sn = $input['slave_sn'];
    $resetStmt->execute([$mid]); // Esegui il reset

    // Imposta la nuova richiesta per lo slave
    $stmtUpd = $pdo->prepare("UPDATE masters SET slave_update_request_sn = ?, slave_ota_status = 'Pending' WHERE id = ?");

    if ($stmtUpd->execute([$slave_sn, $mid])) {
        // --- AUDIT LOG ---
        $logDetails = "Richiesto aggiornamento firmware Slave $slave_sn. Utente ID: " . $_SESSION['user_id'];
        $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, 'OTA_REQ_SLAVE', ?)")->execute([$mid, $logDetails]);

        echo json_encode(['status' => 'ok', 'message' => 'Richiesta per slave inviata']);
    } else {
        echo json_encode(['status' => 'error', 'message' => 'Errore DB']);
    }
    exit;
}

if ($action === 'update_serial') {
    $mid = $input['master_id'];
    $newSerial = $input['new_serial'];
    $pin = $input['pin'];

    // PIN di sicurezza (puoi cambiarlo o metterlo nel DB)
    $validPin = "1234"; 

    if ($pin !== $validPin) {
        echo json_encode(['status' => 'error', 'message' => 'PIN non valido']);
        exit;
    }

    // Recupera il vecchio seriale per il log
    $stmtOld = $pdo->prepare("SELECT serial_number FROM masters WHERE id = ?");
    $stmtOld->execute([$mid]);
    $oldSerial = $stmtOld->fetchColumn();

    $stmt = $pdo->prepare("UPDATE masters SET serial_number = ? WHERE id = ?");
    if ($stmt->execute([$newSerial, $mid])) {
        // --- AUDIT LOG ---
        $logDetails = "Sostituzione Master. Vecchio SN: $oldSerial -> Nuovo SN: $newSerial. Utente ID: " . $_SESSION['user_id'];
        $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, 'CHANGE_SERIAL', ?)")->execute([$mid, $logDetails]);
        
        echo json_encode(['status' => 'ok', 'message' => 'Seriale aggiornato']);
    } else {
        echo json_encode(['status' => 'error', 'message' => 'Errore DB']);
    }
    exit;
}

echo json_encode(['status' => 'error', 'message' => 'Azione non valida']);
?>