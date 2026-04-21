<?php
session_start();
require 'config.php';
require 'lang.php';
require_once 'auth_common.php';

if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$currentRole = ecAuthNormalizeRole((string)($_SESSION['user_role'] ?? ''));
if ($currentRole !== 'admin') {
    header("Location: index.php");
    exit;
}

function akTableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare("
        SELECT 1
        FROM information_schema.tables
        WHERE table_schema = DATABASE()
          AND table_name = ?
        LIMIT 1
    ");
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function akColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare("
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = DATABASE()
          AND table_name = ?
          AND column_name = ?
        LIMIT 1
    ");
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

function akIsUsableApiKey($value): bool {
    return (bool)preg_match('/^[a-f0-9]{64}$/i', trim((string)$value));
}

function akGenerateApiKey(PDO $pdo): string {
    for ($i = 0; $i < 20; $i++) {
        try {
            $candidate = bin2hex(random_bytes(32));
        } catch (Throwable $e) {
            $candidate = hash('sha256', uniqid('api_key_', true));
        }
        $stmt = $pdo->prepare("SELECT 1 FROM masters WHERE api_key = ? LIMIT 1");
        $stmt->execute([$candidate]);
        if (!$stmt->fetchColumn()) {
            return $candidate;
        }
    }
    throw new RuntimeException('Impossibile generare una API Key univoca.');
}

function akInsertAudit(PDO $pdo, int $plantId, string $action, string $details): void {
    if (!akTableExists($pdo, 'audit_logs')) {
        return;
    }
    try {
        $stmt = $pdo->prepare("INSERT INTO audit_logs (master_id, action, details) VALUES (?, ?, ?)");
        $stmt->execute([$plantId, $action, $details]);
    } catch (Throwable $e) {
        // Audit non bloccante.
    }
}

function akPlantLabel(array $plant): string {
    $name = trim((string)($plant['nickname'] ?? ''));
    $serial = trim((string)($plant['serial_number'] ?? ''));
    $parts = ['#' . (int)($plant['id'] ?? 0)];
    if ($name !== '') {
        $parts[] = $name;
    }
    if ($serial !== '') {
        $parts[] = $serial;
    }
    return implode(' - ', $parts);
}

$message = '';
$messageType = '';
$generatedKey = '';
$selectedPlantId = (int)($_POST['plant_id'] ?? ($_GET['plant_id'] ?? 0));
$schemaOk = akTableExists($pdo, 'masters') && akColumnExists($pdo, 'masters', 'api_key');

if (!$schemaOk) {
    $message = 'Schema database non compatibile: tabella masters o colonna api_key non disponibile.';
    $messageType = 'danger';
}

if ($schemaOk && $_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = (string)($_POST['action'] ?? '');
    $confirmReplace = (string)($_POST['confirm_replace'] ?? '') === '1';

    if ($action !== 'generate_api_key' || $selectedPlantId <= 0) {
        $message = 'Seleziona un impianto valido.';
        $messageType = 'danger';
    } else {
        try {
            $pdo->beginTransaction();

            $stmt = $pdo->prepare("
                SELECT id, nickname, serial_number, api_key
                FROM masters
                WHERE id = ? AND deleted_at IS NULL
                LIMIT 1
                FOR UPDATE
            ");
            $stmt->execute([$selectedPlantId]);
            $plant = $stmt->fetch();

            if (!$plant) {
                $pdo->rollBack();
                $message = 'Impianto non trovato o eliminato.';
                $messageType = 'danger';
            } else {
                $oldKey = (string)($plant['api_key'] ?? '');
                $hadUsableKey = akIsUsableApiKey($oldKey);

                if ($hadUsableKey && !$confirmReplace) {
                    $pdo->rollBack();
                    $message = "L'impianto ha gia una API Key. Per modificarla serve una conferma esplicita.";
                    $messageType = 'warning';
                } else {
                    $newKey = akGenerateApiKey($pdo);
                    $upd = $pdo->prepare("UPDATE masters SET api_key = ? WHERE id = ?");
                    $upd->execute([$newKey, $selectedPlantId]);

                    $auditAction = $hadUsableKey ? 'API_KEY_REGENERATE' : 'API_KEY_CREATE';
                    $auditDetails = ($hadUsableKey ? 'API Key rigenerata' : 'API Key generata')
                        . '. Utente admin ID: ' . (int)$_SESSION['user_id'];
                    akInsertAudit($pdo, $selectedPlantId, $auditAction, $auditDetails);

                    $pdo->commit();
                    $generatedKey = $newKey;
                    $message = ($hadUsableKey ? 'API Key rigenerata' : 'API Key generata') . ' per ' . akPlantLabel($plant) . '.';
                    $messageType = 'success';
                }
            }
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            $message = 'Errore durante la generazione API Key: ' . $e->getMessage();
            $messageType = 'danger';
        }
    }
}

$plants = [];
if ($schemaOk) {
    try {
        $stmtPlants = $pdo->query("
            SELECT id, nickname, serial_number, api_key, last_seen, created_at
            FROM masters
            WHERE deleted_at IS NULL
            ORDER BY nickname ASC, id ASC
        ");
        $plants = $stmtPlants->fetchAll();
    } catch (Throwable $e) {
        $message = 'Errore caricamento impianti: ' . $e->getMessage();
        $messageType = 'danger';
    }
}

$selectedPlant = null;
$plantsWithKey = 0;
$plantsWithoutKey = 0;
foreach ($plants as $plant) {
    if (akIsUsableApiKey($plant['api_key'] ?? '')) {
        $plantsWithKey++;
    } else {
        $plantsWithoutKey++;
    }
    if ((int)$plant['id'] === $selectedPlantId) {
        $selectedPlant = $plant;
    }
}
?>
<!DOCTYPE html>
<html lang="<?php echo htmlspecialchars((string)($_SESSION['lang'] ?? 'it')); ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>API Key Impianti - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
    <style>
        .api-key-value { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace; font-size: 0.9rem; }
        .plant-row-muted { color: #6c757d; }
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="d-flex flex-wrap justify-content-between align-items-start gap-3 mb-3">
        <div>
            <h4 class="mb-1"><i class="fas fa-key"></i> API Key Impianti</h4>
            <div class="text-muted">Generazione riservata agli amministratori per centraline Display, Rewamping e Standalone.</div>
        </div>
        <a href="settings.php" class="btn btn-outline-primary btn-sm"><i class="fas fa-cogs"></i> Gestione impianti</a>
    </div>

    <?php if ($message !== ''): ?>
        <div class="alert alert-<?php echo htmlspecialchars($messageType); ?>"><?php echo htmlspecialchars($message); ?></div>
    <?php endif; ?>

    <?php if ($generatedKey !== ''): ?>
        <div class="alert alert-success">
            <div class="fw-semibold mb-2">Nuova API Key da inserire nella centralina</div>
            <div class="input-group">
                <input type="text" class="form-control api-key-value" id="generatedApiKey" value="<?php echo htmlspecialchars($generatedKey); ?>" readonly>
                <button class="btn btn-outline-secondary" type="button" onclick="copyToClipboard('generatedApiKey')"><i class="fas fa-copy"></i> Copia</button>
            </div>
        </div>
    <?php endif; ?>

    <div class="row g-3 mb-4">
        <div class="col-md-4">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <div class="text-muted small">Impianti attivi</div>
                    <div class="fs-3 fw-semibold"><?php echo count($plants); ?></div>
                </div>
            </div>
        </div>
        <div class="col-md-4">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <div class="text-muted small">Con API Key</div>
                    <div class="fs-3 fw-semibold text-success"><?php echo $plantsWithKey; ?></div>
                </div>
            </div>
        </div>
        <div class="col-md-4">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <div class="text-muted small">Da generare</div>
                    <div class="fs-3 fw-semibold text-warning"><?php echo $plantsWithoutKey; ?></div>
                </div>
            </div>
        </div>
    </div>

    <div class="card shadow-sm mb-4">
        <div class="card-header">
            <strong>Genera o rigenera API Key</strong>
        </div>
        <div class="card-body">
            <form method="POST" id="apiKeyForm">
                <input type="hidden" name="action" value="generate_api_key">
                <div class="mb-3">
                    <label class="form-label">Impianto</label>
                    <select class="form-select" name="plant_id" id="plantSelect" required>
                        <option value="">Seleziona impianto...</option>
                        <?php foreach ($plants as $plant): ?>
                            <?php $hasKey = akIsUsableApiKey($plant['api_key'] ?? ''); ?>
                            <option value="<?php echo (int)$plant['id']; ?>"
                                    data-has-key="<?php echo $hasKey ? '1' : '0'; ?>"
                                    <?php echo ((int)$plant['id'] === $selectedPlantId) ? 'selected' : ''; ?>>
                                <?php echo htmlspecialchars(akPlantLabel($plant)); ?> - <?php echo $hasKey ? 'API Key presente' : 'da generare'; ?>
                            </option>
                        <?php endforeach; ?>
                    </select>
                </div>

                <?php if ($selectedPlant): ?>
                    <?php $selectedHasKey = akIsUsableApiKey($selectedPlant['api_key'] ?? ''); ?>
                    <div class="alert <?php echo $selectedHasKey ? 'alert-warning' : 'alert-info'; ?>">
                        <div><strong>Impianto selezionato:</strong> <?php echo htmlspecialchars(akPlantLabel($selectedPlant)); ?></div>
                        <div><strong>Stato API Key:</strong> <?php echo $selectedHasKey ? 'presente' : 'da generare'; ?></div>
                    </div>
                <?php endif; ?>

                <div class="form-check mb-3">
                    <input class="form-check-input" type="checkbox" value="1" id="confirmReplace" name="confirm_replace">
                    <label class="form-check-label" for="confirmReplace">
                        Confermo la modifica anche se l'impianto ha gia una API Key.
                    </label>
                    <div class="form-text">
                        Se non puoi modificare l'API Key sulla centralina, il rischio &egrave; che il collegamento remoto non funzioni pi&ugrave;.
                    </div>
                </div>

                <button type="submit" class="btn btn-primary" <?php echo empty($plants) ? 'disabled' : ''; ?>>
                    <i class="fas fa-key"></i> Genera API Key
                </button>
            </form>
        </div>
    </div>

    <div class="card shadow-sm">
        <div class="card-header">
            <strong>Elenco impianti</strong>
        </div>
        <div class="table-responsive">
            <table class="table table-sm align-middle mb-0">
                <thead>
                    <tr>
                        <th>ID</th>
                        <th>Impianto</th>
                        <th>Seriale</th>
                        <th>API Key</th>
                        <th>Ultimo collegamento</th>
                    </tr>
                </thead>
                <tbody>
                    <?php if (empty($plants)): ?>
                        <tr><td colspan="5" class="text-muted">Nessun impianto attivo disponibile.</td></tr>
                    <?php endif; ?>
                    <?php foreach ($plants as $plant): ?>
                        <?php
                            $keyValue = (string)($plant['api_key'] ?? '');
                            $hasKey = akIsUsableApiKey($keyValue);
                            $keyInputId = 'apiKeyList-' . (int)$plant['id'];
                        ?>
                        <tr class="<?php echo $hasKey ? '' : 'plant-row-muted'; ?>">
                            <td>#<?php echo (int)$plant['id']; ?></td>
                            <td><?php echo htmlspecialchars((string)($plant['nickname'] ?? '')); ?></td>
                            <td><code><?php echo htmlspecialchars((string)($plant['serial_number'] ?? '')); ?></code></td>
                            <td>
                                <?php if ($hasKey): ?>
                                    <div class="input-group input-group-sm">
                                        <input type="text" class="form-control api-key-value" id="<?php echo $keyInputId; ?>" value="<?php echo htmlspecialchars($keyValue); ?>" readonly>
                                        <button class="btn btn-outline-secondary" type="button" onclick="copyToClipboard('<?php echo $keyInputId; ?>')"><i class="fas fa-copy"></i></button>
                                    </div>
                                <?php else: ?>
                                    <span class="badge bg-warning text-dark">Da generare</span>
                                <?php endif; ?>
                            </td>
                            <td><?php echo htmlspecialchars((string)($plant['last_seen'] ?? 'Mai')); ?></td>
                        </tr>
                    <?php endforeach; ?>
                </tbody>
            </table>
        </div>
    </div>
</div>

<script>
function copyToClipboard(elementId) {
    const el = document.getElementById(elementId);
    if (!el) return;
    el.select();
    el.setSelectionRange(0, 99999);
    document.execCommand('copy');
    alert('API Key copiata negli appunti');
}

document.addEventListener('DOMContentLoaded', function () {
    const form = document.getElementById('apiKeyForm');
    const plantSelect = document.getElementById('plantSelect');
    const confirmReplace = document.getElementById('confirmReplace');
    if (!form || !plantSelect || !confirmReplace) return;

    form.addEventListener('submit', function (event) {
        const selected = plantSelect.options[plantSelect.selectedIndex];
        const hasKey = selected && selected.getAttribute('data-has-key') === '1';
        if (!hasKey) return;

        const warning = "Attenzione: se non puoi modificare l'API Key sulla centralina, il rischio \u00e8 che il collegamento remoto non funzioni pi\u00f9.";
        if (!confirmReplace.checked) {
            alert(warning + " Conferma la casella prima di procedere.");
            event.preventDefault();
            return;
        }
        if (!confirm(warning + " Vuoi rigenerare comunque la API Key?")) {
            event.preventDefault();
        }
    });
});
</script>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
