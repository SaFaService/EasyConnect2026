<?php
session_start();
require 'config.php';

// Se l'utente non è loggato, non può essere qui.
if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$error = '';
$success = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $new_pass = $_POST['new_password'];
    $confirm_pass = $_POST['confirm_password'];

    if ($new_pass !== $confirm_pass) {
        $error = "Le password non coincidono.";
    } elseif (strlen($new_pass) < 8) {
        $error = "La password deve essere di almeno 8 caratteri.";
    } else {
        // Le password sono valide, procediamo con l'aggiornamento
        $new_password_hash = password_hash($new_pass, PASSWORD_DEFAULT);

        // Aggiorna la password e disattiva il flag 'force_password_change'
        $stmt = $pdo->prepare("UPDATE users SET password_hash = ?, force_password_change = FALSE WHERE id = ?");
        $stmt->execute([$new_password_hash, $_SESSION['user_id']]);

        $success = "Password aggiornata con successo! Verrai reindirizzato alla dashboard...";
        // Reindirizza alla dashboard dopo 3 secondi
        header("refresh:3;url=index.php");
    }
}
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cambio Password</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body { background-color: #f0f2f5; display: flex; align-items: center; justify-content: center; height: 100vh; }
        .card { width: 100%; max-width: 450px; padding: 20px; }
    </style>
</head>
<body>

<div class="card shadow-sm">
    <h4 class="text-center">Cambio Password Obbligatorio</h4>
    <p class="text-muted text-center">Per motivi di sicurezza, imposta una nuova password personale.</p>

    <?php if($error) echo "<div class='alert alert-danger'>$error</div>"; ?>
    <?php if($success) echo "<div class='alert alert-success'>$success</div>"; ?>

    <?php if(!$success): // Nasconde il form se la password è stata cambiata con successo ?>
    <form method="POST">
        <div class="mb-3">
            <label>Nuova Password</label>
            <input type="password" name="new_password" class="form-control" required>
        </div>
        <div class="mb-3">
            <label>Conferma Nuova Password</label>
            <input type="password" name="confirm_password" class="form-control" required>
        </div>
        <button type="submit" class="btn btn-primary w-100">Imposta Nuova Password</button>
    </form>
    <?php endif; ?>
</div>

</body>
</html>