<?php
require 'config.php';
header('Content-Type: application/json');

$input = json_decode(file_get_contents('php://input'), true);
$apiKey = $input['api_key'] ?? '';
$status = trim((string)($input['status'] ?? 'Failed'));
$message = trim((string)($input['message'] ?? ($input['error_message'] ?? 'Errore non specificato dal dispositivo.')));

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

if ($status === 'Success') {
    $updateStmt = $pdo->prepare("UPDATE masters SET update_requested = 0, ota_status = 'Success', ota_message = ? WHERE id = ?");
    $updateStmt->execute([$message !== '' ? $message : 'Aggiornamento completato con successo.', $master['id']]);
} elseif ($status === 'InProgress') {
    $updateStmt = $pdo->prepare("UPDATE masters SET ota_status = 'InProgress', ota_message = ? WHERE id = ?");
    $updateStmt->execute([$message !== '' ? $message : 'Aggiornamento in corso...', $master['id']]);
} else {
    // Default: fallimento
    $updateStmt = $pdo->prepare("UPDATE masters SET update_requested = 0, ota_status = 'Failed', ota_message = ? WHERE id = ?");
    $updateStmt->execute([$message !== '' ? $message : 'Errore non specificato dal dispositivo.', $master['id']]);
}

echo json_encode(['status' => 'ok', 'message' => 'Report OTA ricevuto.']);
?>
