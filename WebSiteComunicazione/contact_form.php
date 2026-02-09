<?php
session_start();
require 'config.php';

// Includi il gestore della lingua
require 'lang.php';

// Protezione: se l'utente non è loggato, lo rimanda alla pagina di login.
if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $senderEmail = trim($_POST['sender_email']);
    $subject = $_POST['subject'];
    $body = $_POST['body'];
    
    // Info Tecniche (Browser e Risoluzione)
    $userAgent = $_SERVER['HTTP_USER_AGENT'] ?? 'Sconosciuto';
    $screenW = $_POST['screen_width'] ?? 'N/A';
    $screenH = $_POST['screen_height'] ?? 'N/A';
    
    if (!empty($senderEmail) && filter_var($senderEmail, FILTER_VALIDATE_EMAIL) && !empty($subject) && !empty($body)) {
        // --- LOGICA INVIO EMAIL ---
        $to = "info@safaservice.com";
        
        // Prefisso Oggetto
        $finalSubject = "[Antralux Cloud] " . $subject;
        
        // Aggiunta Info Tecniche al corpo del messaggio
        $finalBody = $body . "\n\n--------------------------------------------------\n";
        $finalBody .= "Info Tecniche Utente:\n";
        $finalBody .= "Browser: " . $userAgent . "\n";
        $finalBody .= "Risoluzione Schermo: " . $screenW . "x" . $screenH . "\n";
        $finalBody .= "--------------------------------------------------";

        $headers = "From: " . $senderEmail . "\r\n" .
                   "Reply-To: " . $senderEmail . "\r\n" .
                   "X-Mailer: PHP/" . phpversion();
        // NOTA: La funzione mail() di PHP richiede un server di posta (sendmail, Postfix) configurato sul server web.
        // Se non funziona, potrebbe essere necessario usare una libreria come PHPMailer con un account SMTP.
        if (mail($to, $finalSubject, $finalBody, $headers)) {
            $message = "Messaggio inviato con successo! Sarai ricontattato all'indirizzo: " . htmlspecialchars($senderEmail);
            $message_type = 'success';
        } else {
            $message = "Errore del server durante l'invio del messaggio. Riprova più tardi.";
            $message_type = 'danger';
        }
    } else {
        $message = "Tutti i campi sono obbligatori e l'email del mittente deve essere valida.";
        $message_type = 'danger';
    }
}

// Recupero l'email dell'utente loggato per pre-compilare il campo
$stmtUser = $pdo->prepare("SELECT email FROM users WHERE id = ?");
$stmtUser->execute([$_SESSION['user_id']]);
$user = $stmtUser->fetch();
$userEmail = $user ? $user['email'] : '';
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Contattaci - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-5 flex-grow-1">
    <div class="row justify-content-center">
        <div class="col-md-8">
            <div class="card shadow-sm">
                <div class="card-header"><h5 class="mb-0"><i class="fas fa-envelope me-2"></i><?php echo $lang['contact_title']; ?></h5></div>
                <div class="card-body">
                    <?php if ($message): ?>
                        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
                    <?php endif; ?>
                    <form method="POST">
                        <!-- Campi nascosti per risoluzione schermo -->
                        <input type="hidden" name="screen_width" id="screen_width">
                        <input type="hidden" name="screen_height" id="screen_height">

                        <div class="mb-3"><label class="form-label"><?php echo $lang['contact_your_email']; ?></label><input type="email" name="sender_email" class="form-control" value="<?php echo htmlspecialchars($userEmail); ?>" required></div>
                        <div class="mb-3"><label class="form-label"><?php echo $lang['contact_subject']; ?></label><input type="text" name="subject" class="form-control" required></div>
                        <div class="mb-3"><label class="form-label"><?php echo $lang['contact_message']; ?></label><textarea name="body" class="form-control" rows="6" required></textarea></div>
                        
                        <div class="mb-3 form-check">
                            <input type="checkbox" class="form-check-input" id="privacyCheck" required>
                            <label class="form-check-label small" for="privacyCheck">
                                <?php echo $lang['privacy_agreement_1']; ?> 
                                <a href="privacy.php" target="_blank"><?php echo $lang['privacy_link']; ?></a> 
                                <?php echo $lang['privacy_agreement_2']; ?>
                            </label>
                        </div>

                        <button type="submit" class="btn btn-primary"><?php echo $lang['contact_btn_send']; ?></button>
                    </form>
                </div>
            </div>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
<script>
    // Cattura risoluzione schermo
    document.getElementById('screen_width').value = window.screen.width;
    document.getElementById('screen_height').value = window.screen.height;
</script>
</body>
</html>