<?php
session_start();
require 'config.php';

// Includi il gestore della lingua
require 'lang.php';
$error_key = '';
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
        $error_key = 'login_error';
    }
}
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo $lang['login_title']; ?></title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <!-- Usiamo Bootstrap 5 via CDN per uno stile moderno immediato -->
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
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
        <h4><?php echo $lang['login_header']; ?></h4>
    </div>
    
    <?php if($error_key): ?>
        <div class="alert alert-danger"><?php echo $lang[$error_key]; ?></div>
    <?php endif; ?>

    <form method="POST">
        <div class="mb-3">
            <label><?php echo $lang['login_email_label']; ?></label>
            <input type="email" name="email" class="form-control" required placeholder="admin@antralux.com">
        </div>
        <div class="mb-3">
            <label><?php echo $lang['login_password_label']; ?></label>
            <div class="input-group">
                <input type="password" name="password" id="password" class="form-control" required>
                <button class="btn btn-outline-secondary" type="button" id="togglePassword">
                    <i class="fas fa-eye" id="toggleIcon"></i>
                </button>
            </div>
        </div>
        <button type="submit" class="btn btn-primary w-100"><?php echo $lang['login_button']; ?></button>
        <div class="text-center mt-3">
            <a href="forgot_password.php" class="text-muted small"><?php echo $lang['login_forgot_password']; ?></a>
        </div>
    </form>
</div>

<script>
    const togglePassword = document.querySelector('#togglePassword');
    const password = document.querySelector('#password');
    const icon = document.querySelector('#toggleIcon');

    togglePassword.addEventListener('click', function () {
        // cambia l'attributo type
        const type = password.getAttribute('type') === 'password' ? 'text' : 'password';
        password.setAttribute('type', type);
        // cambia l'icona
        icon.classList.toggle('fa-eye-slash');
    });
</script>
</body>
</html>