<?php
session_start();
require 'config.php';
require 'auth_common.php';
require_once 'GoogleAuthenticator.php';

header('Content-Type: application/json');

function outJson(array $payload, int $httpCode = 200): void {
    http_response_code($httpCode);
    echo json_encode($payload);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    outJson(['status' => 'error', 'message' => 'Metodo non consentito'], 405);
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    $input = $_POST;
}

$email = trim((string)($input['email'] ?? $input['username'] ?? ''));
$password = (string)($input['password'] ?? '');
$rawCode = (string)($input['code'] ?? $input['otp'] ?? $input['otp_code'] ?? $input['two_factor_code'] ?? $input['2fa_code'] ?? '');
$code = trim($rawCode);
$code = preg_replace('/\D+/', '', $code);
$source = strtolower(trim((string)($input['source'] ?? 'desktop_app')));
if ($source === '' || !in_array($source, ['desktop_app', 'web'], true)) {
    $source = 'desktop_app';
}

if ($email === '' || $password === '') {
    outJson(['status' => 'error', 'message' => 'Email e password sono obbligatorie.'], 400);
}

$user = ecAuthFindUserByEmail($pdo, $email);
if (!$user || !password_verify($password, (string)$user['password_hash'])) {
    ecAuthWriteAccessLog($pdo, null, $email, '', $source, 'failed', 'Credenziali non valide');
    outJson(['status' => 'error', 'message' => 'Credenziali non valide.'], 401);
}

if (!ecAuthPortalLoginAllowed((string)($user['portal_access_level'] ?? 'active'))) {
    ecAuthWriteAccessLog(
        $pdo,
        (int)$user['id'],
        (string)$user['email'],
        (string)$user['role'],
        $source,
        'failed',
        'Accesso portale/desktop bloccato per questo utente'
    );
    outJson(['status' => 'error', 'message' => 'Accesso non consentito per questo utente.'], 403);
}

$role = ecAuthNormalizeRole((string)$user['role']);
if (!ecAuthDesktopRoleAllowed($role)) {
    ecAuthWriteAccessLog(
        $pdo,
        (int)$user['id'],
        (string)$user['email'],
        $role,
        $source,
        'denied_role',
        'Ruolo non abilitato alla desktop app'
    );
    outJson(['status' => 'error', 'message' => 'Ruolo non autorizzato per la desktop app.'], 403);
}

$has2fa = !empty($user['google_auth_secret']);
if ($has2fa) {
    if ($code === '') {
        ecAuthWriteAccessLog(
            $pdo,
            (int)$user['id'],
            (string)$user['email'],
            $role,
            $source,
            'pending_2fa',
            'Password valida, codice 2FA richiesto'
        );
        outJson([
            'status' => '2fa_required',
            'message' => 'Inserire codice 2FA.',
            'requires_2fa' => true,
        ], 200);
    }

    $ga = new GoogleAuthenticator();
    $secretRaw = trim((string)$user['google_auth_secret']);
    $secretVariants = array_values(array_unique([
        $secretRaw,
        strtoupper(str_replace([' ', '-'], '', $secretRaw)),
    ]));

    $otpOk = false;
    foreach ($secretVariants as $secretCandidate) {
        if ($secretCandidate === '') {
            continue;
        }
        // Tolleranza extra: fino a +/-120s per gestire drift orario tra server e dispositivo.
        if ($ga->verifyCode($secretCandidate, $code, 4)) {
            $otpOk = true;
            break;
        }
    }

    if (!$otpOk) {
        ecAuthWriteAccessLog(
            $pdo,
            (int)$user['id'],
            (string)$user['email'],
            $role,
            $source,
            'failed',
            'Codice 2FA non valido (verificare ora dispositivo/server)'
        );
        outJson([
            'status' => 'error',
            'message' => 'Codice 2FA non valido. Verifica che data/ora del dispositivo siano corrette.',
        ], 401);
    }
}

session_regenerate_id(true);
$_SESSION['user_id'] = (int)$user['id'];
$_SESSION['user_email'] = (string)$user['email'];
$_SESSION['user_role'] = $role;

ecAuthWriteAccessLog(
    $pdo,
    (int)$user['id'],
    (string)$user['email'],
    $role,
    $source,
    'success',
    $has2fa ? 'Login desktop app completato con 2FA' : 'Login desktop app completato'
);

outJson([
    'status' => 'ok',
    'role' => $role,
    'session_token' => session_id(),
    'requires_password_change' => (bool)$user['force_password_change'],
    'user' => [
        'id' => (int)$user['id'],
        'email' => (string)$user['email'],
        'role' => $role,
    ],
]);
