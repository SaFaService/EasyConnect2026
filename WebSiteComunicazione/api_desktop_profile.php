<?php
session_start();
require 'config.php';

header('Content-Type: application/json');

function outJson(array $payload, int $httpCode = 200): void {
    http_response_code($httpCode);
    echo json_encode($payload);
    exit;
}

function columnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = DATABASE()
          AND table_name = ?
          AND column_name = ?
        LIMIT 1
    ");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

if (!isset($_SESSION['user_id'])) {
    outJson(['status' => 'error', 'message' => 'Non autorizzato'], 401);
}

$userId = (int)$_SESSION['user_id'];
$hasCompany = columnExists($pdo, 'users', 'company');
$hasName = columnExists($pdo, 'users', 'name');
$hasPhone = columnExists($pdo, 'users', 'phone');
$hasWhatsapp = columnExists($pdo, 'users', 'whatsapp');
$hasTelegram = columnExists($pdo, 'users', 'telegram');

try {
    $method = strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET'));
    $raw = json_decode(file_get_contents('php://input'), true);
    if (!is_array($raw)) {
        $raw = $_POST;
    }

    if ($method === 'POST' && (($raw['action'] ?? '') === 'update_profile')) {
        $set = [];
        $params = [];

        if ($hasName) {
            $set[] = 'name = ?';
            $params[] = trim((string)($raw['name'] ?? ''));
        }
        if ($hasCompany) {
            $set[] = 'company = ?';
            $params[] = trim((string)($raw['company'] ?? ''));
        }
        if ($hasPhone) {
            $set[] = 'phone = ?';
            $params[] = trim((string)($raw['phone'] ?? ''));
        }
        if ($hasWhatsapp) {
            $set[] = 'whatsapp = ?';
            $params[] = trim((string)($raw['whatsapp'] ?? ''));
        }
        if ($hasTelegram) {
            $set[] = 'telegram = ?';
            $params[] = trim((string)($raw['telegram'] ?? ''));
        }

        if (!empty($set)) {
            $params[] = $userId;
            $sqlUpd = "UPDATE users SET " . implode(', ', $set) . " WHERE id = ?";
            $stmtUpd = $pdo->prepare($sqlUpd);
            $stmtUpd->execute($params);
        }
    }

    $nameSel = $hasName ? 'name' : 'NULL AS name';
    $companySel = $hasCompany ? 'company' : 'NULL AS company';
    $phoneSel = $hasPhone ? 'phone' : 'NULL AS phone';
    $waSel = $hasWhatsapp ? 'whatsapp' : 'NULL AS whatsapp';
    $tgSel = $hasTelegram ? 'telegram' : 'NULL AS telegram';

    $stmt = $pdo->prepare("
        SELECT
            email,
            role,
            {$nameSel},
            {$companySel},
            {$phoneSel},
            {$waSel},
            {$tgSel},
            google_auth_secret
        FROM users
        WHERE id = ?
        LIMIT 1
    ");
    $stmt->execute([$userId]);
    $user = $stmt->fetch(PDO::FETCH_ASSOC);
    if (!$user) {
        outJson(['status' => 'error', 'message' => 'Utente non trovato'], 404);
    }

    outJson([
        'status' => 'ok',
        'profile' => [
            'email' => (string)($user['email'] ?? ''),
            'role' => (string)($user['role'] ?? ''),
            'name' => (string)($user['name'] ?? ''),
            'company' => (string)($user['company'] ?? ''),
            'phone' => (string)($user['phone'] ?? ''),
            'whatsapp' => (string)($user['whatsapp'] ?? ''),
            'telegram' => (string)($user['telegram'] ?? ''),
            'has_2fa' => !empty($user['google_auth_secret']),
        ],
    ]);
} catch (Throwable $e) {
    outJson(['status' => 'error', 'message' => 'Errore DB: ' . $e->getMessage()], 500);
}

