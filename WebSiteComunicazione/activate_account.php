<?php
require 'config.php';
require_once 'auth_common.php';

$message = '';
$messageType = 'info';
$token = trim((string)($_GET['token'] ?? $_POST['token'] ?? ''));
$tokenHash = $token !== '' ? hash('sha256', $token) : '';
$tokenRow = null;

if ($tokenHash !== '' && ecAuthTableExists($pdo, 'user_activation_tokens')) {
    $stmtToken = $pdo->prepare("
        SELECT t.id, t.user_id, t.expires_at, t.used_at, u.email
        FROM user_activation_tokens t
        INNER JOIN users u ON u.id = t.user_id
        WHERE t.token_hash = ?
        LIMIT 1
    ");
    $stmtToken->execute([$tokenHash]);
    $tokenRow = $stmtToken->fetch(PDO::FETCH_ASSOC) ?: null;
}

$tokenValid = false;
if ($tokenRow) {
    $isUsed = !empty($tokenRow['used_at']);
    $expiresAtTs = strtotime((string)$tokenRow['expires_at'] ?? '');
    $tokenValid = !$isUsed && $expiresAtTs !== false && $expiresAtTs >= time();
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $password = (string)($_POST['password'] ?? '');
    $passwordConfirm = (string)($_POST['password_confirm'] ?? '');

    if (!$tokenValid) {
        $message = 'Link non valido o scaduto.';
        $messageType = 'danger';
    } elseif (strlen($password) < 8) {
        $message = 'La password deve contenere almeno 8 caratteri.';
        $messageType = 'danger';
    } elseif ($password !== $passwordConfirm) {
        $message = 'Le password non coincidono.';
        $messageType = 'danger';
    } else {
        try {
            $pdo->beginTransaction();

            $passwordHash = password_hash($password, PASSWORD_DEFAULT);
            $hasPortalAccessLevel = ecAuthColumnExists($pdo, 'users', 'portal_access_level');
            $hasForcePasswordChange = ecAuthColumnExists($pdo, 'users', 'force_password_change');

            $setParts = ['password_hash = ?'];
            $params = [$passwordHash];
            if ($hasPortalAccessLevel) {
                $setParts[] = "portal_access_level = 'active'";
            }
            if ($hasForcePasswordChange) {
                $setParts[] = 'force_password_change = 0';
            }
            $params[] = (int)$tokenRow['user_id'];
            $stmtUser = $pdo->prepare("UPDATE users SET " . implode(', ', $setParts) . " WHERE id = ?");
            $stmtUser->execute($params);

            $stmtUse = $pdo->prepare("UPDATE user_activation_tokens SET used_at = NOW() WHERE id = ?");
            $stmtUse->execute([(int)$tokenRow['id']]);

            $pdo->commit();
            $message = 'Accesso attivato. Ora puoi entrare dal login con la password appena impostata.';
            $messageType = 'success';
            $tokenValid = false;
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            $message = 'Errore durante l attivazione: ' . $e->getMessage();
            $messageType = 'danger';
        }
    }
}
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Attivazione account</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body { background-color: #f0f2f5; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
        .card { width: 100%; max-width: 480px; }
    </style>
</head>
<body>
<div class="card shadow-sm">
    <div class="card-body p-4">
        <h4 class="text-center mb-2">Attivazione account</h4>
        <?php if ($tokenRow && !empty($tokenRow['email'])): ?>
            <p class="text-center text-muted small mb-4"><?php echo htmlspecialchars((string)$tokenRow['email']); ?></p>
        <?php endif; ?>

        <?php if ($message !== ''): ?>
            <div class="alert alert-<?php echo htmlspecialchars($messageType); ?>"><?php echo htmlspecialchars($message); ?></div>
        <?php endif; ?>

        <?php if ($tokenValid): ?>
            <form method="POST">
                <input type="hidden" name="token" value="<?php echo htmlspecialchars($token); ?>">
                <div class="mb-3">
                    <label class="form-label">Nuova password</label>
                    <input type="password" name="password" class="form-control" minlength="8" required>
                </div>
                <div class="mb-3">
                    <label class="form-label">Conferma password</label>
                    <input type="password" name="password_confirm" class="form-control" minlength="8" required>
                </div>
                <button type="submit" class="btn btn-primary w-100">Attiva accesso</button>
            </form>
        <?php elseif ($messageType !== 'success'): ?>
            <div class="alert alert-warning mb-0">Il link non e disponibile. Richiedi una nuova attivazione all amministratore.</div>
        <?php else: ?>
            <a href="login.php" class="btn btn-outline-primary w-100">Vai al login</a>
        <?php endif; ?>
    </div>
</div>
</body>
</html>
