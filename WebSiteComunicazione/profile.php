<?php
session_start();
require 'config.php';
require_once 'GoogleAuthenticator.php';

// Includi il gestore della lingua
require 'lang.php';

if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';
$currentUserId = $_SESSION['user_id'];
$hasUsersCompanyColumn = false;
$hasUsersAddressColumn = false;
$hasUsersVatColumn = false;
$hasUsersDashboardFilterPrefsColumn = false;
try {
    $chkCompany = $pdo->query("SHOW COLUMNS FROM users LIKE 'company'");
    $hasUsersCompanyColumn = (bool)$chkCompany->fetch();
    $chkAddress = $pdo->query("SHOW COLUMNS FROM users LIKE 'address'");
    $hasUsersAddressColumn = (bool)$chkAddress->fetch();
    $chkVat = $pdo->query("SHOW COLUMNS FROM users LIKE 'vat_number'");
    $hasUsersVatColumn = (bool)$chkVat->fetch();
    $chkDashboardFilterPrefs = $pdo->query("SHOW COLUMNS FROM users LIKE 'dashboard_filter_prefs'");
    $hasUsersDashboardFilterPrefsColumn = (bool)$chkDashboardFilterPrefs->fetch();
} catch (Throwable $e) {
    $hasUsersCompanyColumn = false;
    $hasUsersAddressColumn = false;
    $hasUsersVatColumn = false;
    $hasUsersDashboardFilterPrefsColumn = false;
}

$dashboardFilterOptions = [
    'status' => 'Stato online/offline',
    'owner' => 'Proprietario',
    'plant_name' => 'Nome impianto',
    'address' => 'Indirizzo',
    'builder' => 'Costruttore',
    'maintainer' => 'Manutentore',
    'firmware' => 'Versione firmware',
    'plant_kind' => 'Tipo impianto',
    'last_seen_date' => 'Data connessione',
    'created_date' => 'Data creazione impianto',
    'serial' => 'Seriale master',
];

function profileNormalizedDashboardFilterPrefs(array $selectedKeys, array $availableOptions): array {
    $normalized = [];
    foreach ($selectedKeys as $key) {
        $key = trim((string)$key);
        if ($key !== '' && array_key_exists($key, $availableOptions)) {
            $normalized[$key] = true;
        }
    }
    if (empty($normalized)) {
        foreach (array_keys($availableOptions) as $key) {
            $normalized[$key] = true;
        }
    }
    return $normalized;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';

    if ($action === 'update_profile') {
        $phone = trim((string)($_POST['phone'] ?? ''));
        $whatsapp = trim((string)($_POST['whatsapp'] ?? ''));
        $telegram = trim((string)($_POST['telegram'] ?? ''));
        $company = trim((string)($_POST['company'] ?? ''));
        $address = trim((string)($_POST['address'] ?? ''));
        $vatNumber = trim((string)($_POST['vat_number'] ?? ''));
        $selectedFilterPrefs = profileNormalizedDashboardFilterPrefs((array)($_POST['dashboard_filters'] ?? []), $dashboardFilterOptions);
        $dashboardFilterPrefsJson = json_encode(array_keys($selectedFilterPrefs), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);

        if ($hasUsersCompanyColumn && $company === '') {
            $message = "Il campo Azienda e obbligatorio.";
            $message_type = 'danger';
        } else {
            $setParts = ['phone = ?', 'whatsapp = ?', 'telegram = ?'];
            $params = [$phone, $whatsapp, $telegram];

            if ($hasUsersCompanyColumn) {
                $setParts[] = 'company = ?';
                $params[] = $company;
            }
            if ($hasUsersAddressColumn) {
                $setParts[] = 'address = ?';
                $params[] = $address;
            }
            if ($hasUsersVatColumn) {
                $setParts[] = 'vat_number = ?';
                $params[] = $vatNumber;
            }
            if ($hasUsersDashboardFilterPrefsColumn) {
                $setParts[] = 'dashboard_filter_prefs = ?';
                $params[] = $dashboardFilterPrefsJson;
            }

            $params[] = $currentUserId;
            $stmt = $pdo->prepare("UPDATE users SET " . implode(', ', $setParts) . " WHERE id = ?");
            $stmt->execute($params);
            $message = "Profilo aggiornato!";
            $message_type = 'success';
        }
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

$dashboardFilterPrefs = [];
if ($hasUsersDashboardFilterPrefsColumn) {
    $rawDashboardFilterPrefs = trim((string)($currentUser['dashboard_filter_prefs'] ?? ''));
    if ($rawDashboardFilterPrefs !== '') {
        $decodedPrefs = json_decode($rawDashboardFilterPrefs, true);
        if (is_array($decodedPrefs)) {
            $dashboardFilterPrefs = profileNormalizedDashboardFilterPrefs($decodedPrefs, $dashboardFilterOptions);
        }
    }
    if (empty($dashboardFilterPrefs)) {
        $dashboardFilterPrefs = profileNormalizedDashboardFilterPrefs([], $dashboardFilterOptions);
    }
}

// Mappa ruolo utente in etichetta leggibile (lingua corrente).
$roleCode = (string)($currentUser['role'] ?? '');
$roleLabelMap = [
    'admin' => $lang['users_role_admin'] ?? 'Administrator',
    'builder' => $lang['users_role_builder'] ?? 'Builder',
    'maintainer' => $lang['users_role_maintainer'] ?? 'Maintainer',
    'client' => $lang['users_role_client'] ?? 'Client',
];
$roleLabel = $roleLabelMap[$roleCode] ?? $roleCode;

$ga = new GoogleAuthenticator();
$newSecret = $ga->createSecret();
$otpAuthUrl = 'otpauth://totp/Antralux%20(' . urlencode((string)$currentUser['email']) . ')?secret=' . $newSecret . '&issuer=Antralux';
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Mio Profilo - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <script src="https://cdn.jsdelivr.net/npm/qrcodejs@1.0.0/qrcode.min.js"></script>
    <style>
        .dashboard-filter-choice.form-check {
            padding: 0.75rem 0.75rem 0.75rem 2.25rem;
        }
        .dashboard-filter-choice .form-check-input {
            float: none;
            margin-left: 0;
            margin-top: 0.2rem;
            position: absolute;
            left: 0.75rem;
        }
    </style>
</head>
<body class="bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4">
    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo htmlspecialchars($message); ?></div>
    <?php endif; ?>

    <div class="row justify-content-center">
        <div class="col-lg-8">
            <div class="card shadow-sm mb-4">
                <div class="card-header"><h5 class="mb-0"><i class="fas fa-user-edit"></i> Il Mio Profilo</h5></div>
                <div class="card-body">
                    <form method="POST">
                        <input type="hidden" name="action" value="update_profile">
                        <div class="mb-3"><label>Email</label><input type="text" class="form-control" value="<?php echo htmlspecialchars((string)$currentUser['email']); ?>" readonly></div>
                        <div class="mb-3"><label><?php echo htmlspecialchars($lang['users_role'] ?? 'Role'); ?></label><input type="text" class="form-control" value="<?php echo htmlspecialchars($roleLabel); ?>" readonly></div>
                        <?php if ($hasUsersCompanyColumn): ?>
                            <div class="mb-3"><label><?php echo htmlspecialchars($lang['contacts_company'] ?? 'Azienda'); ?></label><input type="text" name="company" class="form-control" value="<?php echo htmlspecialchars((string)($currentUser['company'] ?? '')); ?>" required></div>
                        <?php endif; ?>
                        <?php if ($hasUsersAddressColumn): ?>
                            <div class="mb-3"><label>Indirizzo</label><textarea name="address" class="form-control" rows="2" placeholder="Via, numero civico, citta"><?php echo htmlspecialchars((string)($currentUser['address'] ?? '')); ?></textarea></div>
                        <?php endif; ?>
                        <?php if ($hasUsersVatColumn): ?>
                            <div class="mb-3"><label>Partita IVA</label><input type="text" name="vat_number" class="form-control" value="<?php echo htmlspecialchars((string)($currentUser['vat_number'] ?? '')); ?>" placeholder="IT12345678901"></div>
                        <?php endif; ?>
                        <div class="mb-3"><label>Telefono</label><input type="text" name="phone" class="form-control" value="<?php echo htmlspecialchars((string)($currentUser['phone'] ?? '')); ?>"></div>
                        <div class="mb-3"><label>WhatsApp</label><input type="text" name="whatsapp" class="form-control" value="<?php echo htmlspecialchars((string)($currentUser['whatsapp'] ?? '')); ?>" placeholder="Numero o username"></div>
                        <div class="mb-3"><label>Telegram</label><input type="text" name="telegram" class="form-control" value="<?php echo htmlspecialchars((string)($currentUser['telegram'] ?? '')); ?>" placeholder="@username"></div>
                        <?php if ($hasUsersDashboardFilterPrefsColumn): ?>
                            <div class="mb-3">
                                <label class="form-label">Filtri Dashboard visibili</label>
                                <div class="row g-2">
                                    <?php foreach ($dashboardFilterOptions as $filterKey => $filterLabel): ?>
                                        <div class="col-md-6">
                                            <div class="form-check dashboard-filter-choice border rounded h-100 bg-light position-relative">
                                                <input
                                                    class="form-check-input"
                                                    type="checkbox"
                                                    name="dashboard_filters[]"
                                                    value="<?php echo htmlspecialchars($filterKey); ?>"
                                                    id="dashboardFilter_<?php echo htmlspecialchars($filterKey); ?>"
                                                    <?php echo !empty($dashboardFilterPrefs[$filterKey]) ? 'checked' : ''; ?>
                                                >
                                                <label class="form-check-label" for="dashboardFilter_<?php echo htmlspecialchars($filterKey); ?>">
                                                    <?php echo htmlspecialchars($filterLabel); ?>
                                                </label>
                                            </div>
                                        </div>
                                    <?php endforeach; ?>
                                </div>
                                <div class="form-text">Se non selezioni nulla, il portale mantiene disponibili tutti i filtri.</div>
                            </div>
                        <?php endif; ?>
                        <button type="submit" class="btn btn-primary">Salva Impostazioni</button>
                    </form>
                </div>
            </div>

            <div class="card shadow-sm mb-4">
                <div class="card-header"><h5 class="mb-0"><i class="fas fa-shield-alt"></i> Sicurezza 2FA (Google Authenticator)</h5></div>
                <div class="card-body text-center">
                    <?php if (empty($currentUser['google_auth_secret'])): ?>
                        <p class="text-muted small">Scansiona il QR Code con la tua app di autenticazione, poi inserisci il codice a 6 cifre per attivare.</p>
                        <div id="qrcode" class="d-flex justify-content-center mb-3"></div>
                        <p class="small">In alternativa, inserisci manualmente la chiave: <code><?php echo htmlspecialchars($newSecret); ?></code></p>

                        <form method="POST" class="mt-2">
                            <input type="hidden" name="action" value="enable_2fa">
                            <input type="hidden" name="secret" value="<?php echo htmlspecialchars($newSecret); ?>">
                            <input type="text" name="code" class="form-control mb-2 text-center" placeholder="Codice a 6 cifre" required>
                            <button type="submit" class="btn btn-success">Attiva 2FA</button>
                        </form>

                        <script>
                            new QRCode(document.getElementById("qrcode"), {
                                text: <?php echo json_encode($otpAuthUrl, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE); ?>,
                                width: 180,
                                height: 180
                            });
                        </script>
                    <?php else: ?>
                        <div class="alert alert-success"><i class="fas fa-check-circle"></i> L'autenticazione a due fattori e attiva.</div>
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
