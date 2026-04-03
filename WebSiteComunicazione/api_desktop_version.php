<?php
header('Content-Type: application/json');
header('Cache-Control: no-store, no-cache, must-revalidate');
header('Pragma: no-cache');

function outJson(array $payload, int $httpCode = 200): void {
    http_response_code($httpCode);
    echo json_encode($payload);
    exit;
}

$currentVersion = trim((string)($_GET['current_version'] ?? ''));
$versionFile = __DIR__ . DIRECTORY_SEPARATOR . 'desktop_app_version.json';

$payload = [
    'latest_version' => $currentVersion !== '' ? $currentVersion : '0.2.1',
    'download_url' => '',
    'notes' => '',
    'mandatory' => false,
];

if (is_file($versionFile)) {
    try {
        $raw = file_get_contents($versionFile);
        $data = json_decode((string)$raw, true, 512, JSON_THROW_ON_ERROR);
        if (is_array($data)) {
            $latest = trim((string)($data['latest_version'] ?? $data['version'] ?? ''));
            if ($latest !== '') {
                $payload['latest_version'] = $latest;
            }
            $payload['download_url'] = trim((string)($data['download_url'] ?? ''));
            $payload['notes'] = trim((string)($data['notes'] ?? ''));
            $payload['mandatory'] = !empty($data['mandatory']);
        }
    } catch (Throwable $e) {
        // Fallback silenzioso sul payload base.
    }
}

outJson(array_merge(['status' => 'ok'], $payload));
