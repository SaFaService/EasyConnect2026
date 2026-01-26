<?php
session_start();
require 'config.php';

$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $email = $_POST['email'];
    $password = $_POST['password'];

    // Cerca l'utente nel DB
    $stmt = $pdo->prepare("SELECT id, password_hash, force_password_change, role, google_auth_secret FROM users WHERE email = ?");
    $stmt->execute([$email]);
    $user = $stmt->fetch();

    if ($user && password_verify($password, $user['password_hash'])) {
        // Se l'utente ha il 2FA attivo, andiamo alla verifica
        if (!empty($user['google_auth_secret'])) {
            $_SESSION['temp_user_id'] = $user['id'];
            header("Location: verify_2fa.php");
            exit;
        }

        // Login corretto, salva i dati in sessione
        $_SESSION['user_id'] = $user['id'];
        $_SESSION['user_email'] = $email;
        $_SESSION['user_role'] = $user['role'];

        // Controlla se l'utente deve cambiare la password
        if ($user['force_password_change']) {
            header("Location: change_password.php");
        } else {
            header("Location: index.php");
        }
        exit;
    } else {
        $error = "Email o Password non validi.";
    }
}
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Login - Antralux EasyConnect</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <!-- Usiamo Bootstrap 5 via CDN per uno stile moderno immediato -->
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body { background-color: #f0f2f5; display: flex; align-items: center; justify-content: center; height: 100vh; }
        .login-card { width: 100%; max-width: 400px; padding: 20px; background: white; border-radius: 10px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
        .logo { display: block; margin: 0 auto 20px; max-height: 60px; }
    </style>
</head>
<body>

<div class="login-card">
    <div class="text-center">
        <!-- Logo aggiornato -->
        <img src="assets/img/AntraluxLogo.png" alt="Antralux Logo" class="logo" onerror="this.style.display='none'">
        <h4>Accesso Sistema</h4>
    </div>
    
    <?php if($error): ?>
        <div class="alert alert-danger"><?php echo $error; ?></div>
    <?php endif; ?>

    <form method="POST">
        <div class="mb-3">
            <label>Email</label>
            <input type="email" name="email" class="form-control" required placeholder="admin@antralux.com">
        </div>
        <div class="mb-3">
            <label>Password</label>
            <input type="password" name="password" class="form-control" required>
        </div>
        <button type="submit" class="btn btn-primary w-100">Accedi</button>
        <div class="text-center mt-3">
            <a href="forgot_password.php" class="text-muted small">Password dimenticata?</a>
        </div>
    </form>
</div>

</body>
</html>