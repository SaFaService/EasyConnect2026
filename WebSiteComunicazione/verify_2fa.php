<?php
session_start();
require 'config.php';
require 'GoogleAuthenticator.php';

if (!isset($_SESSION['temp_user_id'])) {
    header("Location: login.php");
    exit;
}

$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $code = $_POST['code'];
    
    // Recupera il segreto dal DB
    $stmt = $pdo->prepare("SELECT google_auth_secret, id, email, role FROM users WHERE id = ?");
    $stmt->execute([$_SESSION['temp_user_id']]);
    $user = $stmt->fetch();

    $ga = new GoogleAuthenticator();
    if ($ga->verifyCode($user['google_auth_secret'], $code)) {
        // Codice valido: Login completo
        $_SESSION['user_id'] = $user['id'];
        $_SESSION['user_email'] = $user['email'];
        $_SESSION['user_role'] = $user['role'];
        unset($_SESSION['temp_user_id']); // Rimuovi ID temporaneo
        
        header("Location: index.php");
        exit;
    } else {
        $error = "Codice non valido.";
    }
}
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Verifica 2FA - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>body { background-color: #f0f2f5; display: flex; align-items: center; justify-content: center; height: 100vh; }</style>
</head>
<body>
<div class="card shadow-sm p-4" style="max-width: 400px; width: 100%;">
    <div class="text-center mb-3">
        <img src="assets/img/AntraluxLogo.png" alt="Antralux" style="max-height: 60px;">
        <h4 class="mt-3">Verifica a due fattori</h4>
        <p class="text-muted">Inserisci il codice dal tuo Google Authenticator</p>
    </div>
    <?php if($error): ?><div class="alert alert-danger"><?php echo $error; ?></div><?php endif; ?>
    <form method="POST">
        <div class="mb-3">
            <input type="text" name="code" class="form-control text-center" placeholder="000000" maxlength="6" style="font-size: 1.5em; letter-spacing: 5px;" required autofocus>
        </div>
        <button type="submit" class="btn btn-primary w-100">Verifica</button>
    </form>
</div>
</body>
</html>