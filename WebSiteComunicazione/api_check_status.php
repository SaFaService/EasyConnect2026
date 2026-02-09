<?php
session_start();
require 'config.php';
header('Content-Type: application/json');

if (!isset($_SESSION['user_id']) || empty($_GET['master_id'])) {
    echo json_encode(['status' => 'error']);
    exit;
}

$mid = $_GET['master_id'];

$stmt = $pdo->prepare("SELECT * FROM masters WHERE id = ?");
$stmt->execute([$mid]);
$m = $stmt->fetch();

echo json_encode([
    'ota_status' => $m['ota_status'],
    'ota_message' => $m['ota_message'],
    'slave_update_request_sn' => $m['slave_update_request_sn'],
    'slave_ota_status' => $m['slave_ota_status'],
    'slave_ota_message' => $m['slave_ota_message'],
    'fw_version' => $m['fw_version'],
    'last_seen_seconds_ago' => (time() - strtotime($m['last_seen']))
]);
?>