<?php
require 'config.php';
header('Content-Type: application/json');

function tableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("
        SELECT 1
        FROM information_schema.tables
        WHERE table_schema = DATABASE()
          AND table_name = ?
        LIMIT 1
    ");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

$input = json_decode(file_get_contents('php://input'), true);
$apiKey = trim((string)($input['api_key'] ?? ''));
$commandId = (int)($input['command_id'] ?? 0);
$statusIn = strtolower(trim((string)($input['status'] ?? 'failed')));
$message = trim((string)($input['message'] ?? 'Nessun dettaglio'));
$result = $input['result'] ?? null;

if ($apiKey === '' || $commandId <= 0) {
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'Parametri mancanti']);
    exit;
}
if (!preg_match('/^[a-f0-9]{64}$/i', $apiKey)) {
    http_response_code(403);
    echo json_encode(['status' => 'error', 'message' => 'API Key non valida']);
    exit;
}
if (!tableExists($pdo, 'device_commands')) {
    http_response_code(500);
    echo json_encode(['status' => 'error', 'message' => 'Tabella device_commands non disponibile']);
    exit;
}

try {
    $stmtMaster = $pdo->prepare("SELECT id FROM masters WHERE api_key = ? AND deleted_at IS NULL LIMIT 1");
    $stmtMaster->execute([$apiKey]);
    $masterId = (int)($stmtMaster->fetchColumn() ?: 0);
    if ($masterId <= 0) {
        http_response_code(403);
        echo json_encode(['status' => 'error', 'message' => 'Master non trovato']);
        exit;
    }

    $status = ($statusIn === 'success') ? 'success' : 'failed';
    $resultJson = null;
    if (is_array($result)) {
        $resultJson = json_encode($result, JSON_UNESCAPED_UNICODE);
    }

    $stmtCmd = $pdo->prepare("SELECT id FROM device_commands WHERE id = ? AND master_id = ? LIMIT 1");
    $stmtCmd->execute([$commandId, $masterId]);
    if (!$stmtCmd->fetchColumn()) {
        echo json_encode(['status' => 'ok', 'message' => 'Comando non trovato o non associato a questa master']);
        exit;
    }

    $upd = $pdo->prepare("
        UPDATE device_commands
        SET status = ?,
            completed_at = NOW(),
            result_message = ?,
            result_json = ?
        WHERE id = ?
          AND master_id = ?
    ");
    $upd->execute([$status, $message, $resultJson, $commandId, $masterId]);

    echo json_encode(['status' => 'ok', 'message' => 'Report comando ricevuto']);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['status' => 'error', 'message' => 'DB Error: ' . $e->getMessage()]);
}
?>
