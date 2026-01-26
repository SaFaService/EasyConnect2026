<?php
// Abilita visualizzazione errori per debug immediato
ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);

require 'config.php';

$message = '';
$message_type = 'info';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $email = $_POST['email'];

    try {
        // 1. Cerca se l'utente esiste
        $stmt = $pdo->prepare("SELECT id FROM users WHERE email = ?");
        $stmt->execute([$email]);
        $user = $stmt->fetch();

        if ($user) {
            // 2. Genera una nuova password casuale e sicura (10 caratteri)
            // Fallback per versioni PHP vecchie che non hanno random_bytes
            if (function_exists('random_bytes')) {
                $new_password = substr(bin2hex(random_bytes(32)), 0, 10);
            } else {
                $new_password = substr(md5(uniqid(mt_rand(), true)), 0, 10);
            }

            // 3. Cripta la nuova password
            $new_password_hash = password_hash($new_password, PASSWORD_DEFAULT);

            // 4. Aggiorna il database con la nuova password e imposta il flag per forzare il cambio
            $updateStmt = $pdo->prepare("UPDATE users SET password_hash = ?, force_password_change = TRUE WHERE id = ?");
            $updateStmt->execute([$new_password_hash, $user['id']]);

            // 5. Invia l'email (assicurati che il tuo server hosting possa inviare email)
            $to = $email;
            $subject = 'Recupero Password - Antralux EasyConnect';
            $body = "Ciao,\n\nLa tua nuova password temporanea e': " . $new_password . "\n\nTi verra' richiesto di cambiarla al primo accesso.\n\nSaluti,\nIl team Antralux";
            $headers = 'From: no-reply@antralux.com' . "\r\n" .
                       'Reply-To: no-reply@antralux.com' . "\r\n" .
                       'X-Mailer: PHP/' . phpversion();

            // Usa @ per sopprimere errori mail() che causano warning, gestiamo noi il risultato
            if(@mail($to, $subject, $body, $headers)) {
                $message = "Abbiamo inviato le istruzioni per il recupero alla tua email.";
                $message_type = 'success';
            } else {
                // FALLBACK TEMPORANEO: Se l'email non parte, mostriamo la password a video per permetterti di entrare
                $message = "Errore invio email (Server SMTP non configurato).<br><strong>PASSWORD TEMPORANEA (Copia e usa subito): " . $new_password . "</strong>";
                $message_type = 'warning';
            }
        } else {
            $message = "Questa mail non esiste nei nostri sistemi, per avere accesso a questo sistema, contatta l'amministratore.";
            $message_type = 'danger';
        }
    } catch (Exception $e) {
        // Cattura l'errore (es. colonna mancante) e lo mostra invece di dare HTTP 500
        $message = "Errore di Sistema: " . $e->getMessage();
        $message_type = 'danger';
    }
}
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Recupero Password</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body { background-color: #f0f2f5; display: flex; align-items: center; justify-content: center; height: 100vh; }
        .card { width: 100%; max-width: 450px; padding: 20px; }
    </style>
</head>
<body>

<div class="card shadow-sm">
    <h4 class="text-center">Recupero Password</h4>
    <p class="text-muted text-center">Inserisci la tua email per ricevere una nuova password temporanea.</p>

    <?php if($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <form method="POST">
        <div class="mb-3">
            <label>Email</label>
            <input type="email" name="email" class="form-control" required>
        </div>
        <button type="submit" class="btn btn-primary w-100">Invia Istruzioni</button>
        <div class="text-center mt-3">
            <a href="login.php">Torna al Login</a>
        </div>
    </form>
</div>

</body>
</html>