<?php
require 'config.php';
header('Content-Type: application/json');

$input = json_decode(file_get_contents('php://input'), true);
$apiKey = $input['api_key'] ?? '';
$slaveSn = $input['slave_sn'] ?? '';
$status = $input['status'] ?? 'Failed';
$message = $input['message'] ?? 'Nessun messaggio.';

if (empty($apiKey) || empty($slaveSn)) {
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'API Key o Seriale Slave mancante.']);
    exit;
}

// Trova il master corrispondente che ha una richiesta attiva per questo slave
$stmt = $pdo->prepare("SELECT id FROM masters WHERE api_key = ? AND slave_update_request_sn = ?");
$stmt->execute([$apiKey, $slaveSn]);
$master = $stmt->fetch();

if (!$master) {
    // Potrebbe essere un report tardivo, non lo trattiamo come un errore fatale
    // ma semplicemente non facciamo nulla.
    echo json_encode(['status' => 'ok', 'message' => 'Nessuna richiesta attiva trovata. Report ignorato.']);
    exit;
}

if ($status == 'Success' || $status == 'Failed') {
    // Stato finale: aggiorna lo stato e cancella la richiesta per evitare loop infiniti
    $updateStmt = $pdo->prepare("UPDATE masters SET slave_update_request_sn = NULL, slave_ota_status = ?, slave_ota_message = ? WHERE id = ?");
    $updateStmt->execute([$status, $message, $master['id']]);
} else {
    // Stato intermedio: aggiorna solo lo stato e il messaggio
    $updateStmt = $pdo->prepare("UPDATE masters SET slave_ota_status = ?, slave_ota_message = ? WHERE id = ?");
    $updateStmt->execute([$status, $message, $master['id']]);
}

echo json_encode(['status' => 'ok', 'message' => 'Report ricevuto.']);
?>