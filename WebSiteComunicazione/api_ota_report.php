<?php
require 'config.php';
header('Content-Type: application/json');

$input = json_decode(file_get_contents('php://input'), true);
$apiKey = $input['api_key'] ?? '';
$errorMessage = $input['error_message'] ?? 'Errore non specificato dal dispositivo.';

if (empty($apiKey)) {
    http_response_code(403);
    echo json_encode(['status' => 'error', 'message' => 'API Key mancante']);
    exit;
}

// Trova il master corrispondente
$stmt = $pdo->prepare("SELECT id FROM masters WHERE api_key = ?");
$stmt->execute([$apiKey]);
$master = $stmt->fetch();

if (!$master) {
    http_response_code(404);
    echo json_encode(['status' => 'error', 'message' => 'Master non trovato']);
    exit;
}

// Aggiorna lo stato OTA a "Failed" e resetta la richiesta
$updateStmt = $pdo->prepare("UPDATE masters SET update_requested = 0, ota_status = 'Failed', ota_message = ? WHERE id = ?");
$updateStmt->execute([$errorMessage, $master['id']]);

echo json_encode(['status' => 'ok', 'message' => 'Report di fallimento ricevuto.']);
?>