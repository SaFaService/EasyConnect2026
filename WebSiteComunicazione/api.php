<?php
// api.php - Endpoint per ESP32
header('Content-Type: application/json');
require 'config.php';

// 1. Verifica API Key
$headers = getallheaders();
$client_key = $headers['X-API-KEY'] ?? $_SERVER['HTTP_X_API_KEY'] ?? '';

if (empty($client_key)) {
    http_response_code(403);
    echo json_encode(['error' => 'Missing API Key']);
    exit;
}

// Cerca la master con questa chiave
$stmt = $pdo->prepare("SELECT id, log_retention_days FROM masters WHERE api_key = ?");
$stmt->execute([$client_key]);
$master = $stmt->fetch();

if (!$master) {
    http_response_code(403);
    echo json_encode(['error' => 'Invalid API Key']);
    exit;
}

// 2. Leggi i dati JSON
$json = file_get_contents('php://input');
$data = json_decode($json, true);

if (!$data) {
    echo json_encode(['status' => 'no_data']);
    exit;
}

// 3. Aggiorna stato Master
$updateStmt = $pdo->prepare("UPDATE masters SET last_seen = NOW(), fw_version = ?, rssi = ? WHERE id = ?");
$updateStmt->execute([$data['fw_ver'] ?? 'unknown', $data['rssi'] ?? 0, $master['id']]);

// 4. Salva i dati (Log)
// DeltaP Master
if (isset($data['delta_p'])) {
    $logStmt = $pdo->prepare("INSERT INTO measurements (master_id, delta_p) VALUES (?, ?)");
    $logStmt->execute([$master['id'], $data['delta_p']]);
}

// Dati Slaves
if (isset($data['slaves']) && is_array($data['slaves'])) {
    $slaveStmt = $pdo->prepare("INSERT INTO measurements (master_id, slave_sn, slave_grp, pressure, temperature, fw_version) VALUES (?, ?, ?, ?, ?, ?)");
    foreach ($data['slaves'] as $slave) {
        $slaveStmt->execute([
            $master['id'],
            $slave['sn'],
            $slave['grp'],
            $slave['p'],
            $slave['t'],
            $slave['ver'] ?? null
        ]);
    }
}

// 5. Pulizia Log Vecchi (Opzionale qui, meglio via CronJob, ma per ora va bene)
$cleanStmt = $pdo->prepare("DELETE FROM measurements WHERE master_id = ? AND recorded_at < NOW() - INTERVAL ? DAY");
$cleanStmt->execute([$master['id'], $master['log_retention_days'] ?? 30]);

echo json_encode(['status' => 'ok']);
?>