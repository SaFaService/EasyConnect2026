<?php
session_start();
require 'config.php';
require_once 'GoogleAuthenticator.php';

if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';
$currentUserId = $_SESSION['user_id'];

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';

    if ($action === 'update_profile') {
        $phone = $_POST['phone'];
        $whatsapp = $_POST['whatsapp'];
        $telegram = $_POST['telegram'];
        
        $stmt = $pdo->prepare("UPDATE users SET phone = ?, whatsapp = ?, telegram = ? WHERE id = ?");
        $stmt->execute([$phone, $whatsapp, $telegram, $currentUserId]);
        $message = "Profilo aggiornato!";
        $message_type = 'success';
    }

    if ($action === 'enable_2fa') {
        $secret = $_POST['secret'];
        $code = $_POST['code'];
        $ga = new GoogleAuthenticator();
        if ($ga->verifyCode($secret, $code)) {
            $stmt = $pdo->prepare("UPDATE users SET google_auth_secret = ? WHERE id = ?");
            $stmt->execute([$secret, $currentUserId]);
            $message = "Autenticazione a due fattori ATTIVATA!";
            $message_type = 'success';
        } else {
            $message = "Codice non valido. Riprova.";
            $message_type = 'danger';
        }
    }

    if ($action === 'disable_2fa') {
        $stmt = $pdo->prepare("UPDATE users SET google_auth_secret = NULL WHERE id = ?");
        $stmt->execute([$currentUserId]);
        $message = "Autenticazione a due fattori DISATTIVATA.";
        $message_type = 'warning';
    }
}

$stmtUser = $pdo->prepare("SELECT * FROM users WHERE id = ?");
$stmtUser->execute([$currentUserId]);
$currentUser = $stmtUser->fetch();

$ga = new GoogleAuthenticator();
$newSecret = $ga->createSecret();
$otpAuthUrl = 'otpauth://totp/Antralux%20(' . urlencode($currentUser['email']) . ')?secret=' . $newSecret . '&issuer=Antralux';
?>
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Mio Profilo - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <script src="https://cdn.jsdelivr.net/npm/qrcodejs@1.0.0/qrcode.min.js"></script>
</head>
<body class="bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4">

    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <div class="row justify-content-center">
        <div class="col-lg-8">
            <!-- Card Profilo -->
            <div class="card shadow-sm mb-4">
                <div class="card-header"><h5 class="mb-0"><i class="fas fa-user-edit"></i> Il Mio Profilo</h5></div>
                <div class="card-body">
                    <form method="POST">
                        <input type="hidden" name="action" value="update_profile">
                        <div class="mb-3"><label>Email</label><input type="text" class="form-control" value="<?php echo htmlspecialchars($currentUser['email']); ?>" readonly></div>
                        <div class="mb-3"><label>Telefono</label><input type="text" name="phone" class="form-control" value="<?php echo htmlspecialchars($currentUser['phone']); ?>"></div>
                        <div class="mb-3"><label>WhatsApp</label><input type="text" name="whatsapp" class="form-control" value="<?php echo htmlspecialchars($currentUser['whatsapp']); ?>" placeholder="Numero o username"></div>
                        <div class="mb-3"><label>Telegram</label><input type="text" name="telegram" class="form-control" value="<?php echo htmlspecialchars($currentUser['telegram']); ?>" placeholder="@username"></div>
                        <button type="submit" class="btn btn-primary">Salva Contatti</button>
                    </form>
                </div>
            </div>

            <!-- Card Sicurezza 2FA -->
            <div class="card shadow-sm mb-4">
                <div class="card-header"><h5 class="mb-0"><i class="fas fa-shield-alt"></i> Sicurezza 2FA (Google Authenticator)</h5></div>
                <div class="card-body text-center">
                    <?php if (empty($currentUser['google_auth_secret'])): ?>
                        <p class="text-muted small">Scansiona il QR Code con la tua app di autenticazione, poi inserisci il codice a 6 cifre per attivare.</p>
                        <div id="qrcode" class="d-flex justify-content-center mb-3"></div>
                        <p class="small">In alternativa, inserisci manualmente la chiave: <code><?php echo $newSecret; ?></code></p>
                        
                        <form method="POST" class="mt-2">
                            <input type="hidden" name="action" value="enable_2fa">
                            <input type="hidden" name="secret" value="<?php echo $newSecret; ?>">
                            <input type="text" name="code" class="form-control mb-2 text-center" placeholder="Codice a 6 cifre" required>
                            <button type="submit" class="btn btn-success">Attiva 2FA</button>
                        </form>

                        <script>
                            new QRCode(document.getElementById("qrcode"), {
                                text: "<?php echo $otpAuthUrl; ?>",
                                width: 180,
                                height: 180,
                            });
                        </script>

                    <?php else: ?>
                        <div class="alert alert-success"><i class="fas fa-check-circle"></i> L'autenticazione a due fattori è attiva.</div>
                        <form method="POST">
                            <input type="hidden" name="action" value="disable_2fa">
                            <button type="submit" class="btn btn-danger">Disattiva 2FA</button>
                        </form>
                    <?php endif; ?>
                </div>
            </div>
        </div>
    </div>
</div>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>