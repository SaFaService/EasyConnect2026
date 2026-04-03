<?php
session_start();
require 'config.php';
require 'lang.php';
require_once 'auth_common.php';

if (!isset($_SESSION['user_id'])) {
    header("Location: login.php");
    exit;
}

$currentUserId = (int)$_SESSION['user_id'];
$currentRole = (string)($_SESSION['user_role'] ?? '');
$isAdmin = ($currentRole === 'admin');
$isIt = (string)($_SESSION['lang'] ?? 'it') === 'it';

if ($currentRole === 'client') {
    header("Location: index.php");
    exit;
}

function cudTxt(string $it, string $en): string {
    global $isIt;
    return $isIt ? $it : $en;
}

function cudColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
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

function cudRoleLabel(string $role, array $lang): string {
    if ($role === 'builder') {
        return (string)($lang['users_role_builder'] ?? 'Builder');
    }
    if ($role === 'maintainer') {
        return (string)($lang['users_role_maintainer'] ?? 'Maintainer');
    }
    if ($role === 'client') {
        return (string)($lang['users_role_client'] ?? 'Client');
    }
    return $role;
}

function cudRoleBadge(string $role): string {
    if ($role === 'builder') {
        return 'bg-warning text-dark';
    }
    if ($role === 'maintainer') {
        return 'bg-success';
    }
    if ($role === 'client') {
        return 'bg-primary';
    }
    return 'bg-secondary';
}

function cudNormalizeExtraContacts(array $names, array $roles, array $emails, array $phones): array {
    $rows = [];
    $count = max(count($names), count($roles), count($emails), count($phones));
    for ($i = 0; $i < $count; $i++) {
        $name = trim((string)($names[$i] ?? ''));
        $role = trim((string)($roles[$i] ?? ''));
        $email = trim((string)($emails[$i] ?? ''));
        $phone = trim((string)($phones[$i] ?? ''));
        if ($name === '' && $role === '' && $email === '' && $phone === '') {
            continue;
        }
        $rows[] = [
            'name' => $name,
            'role' => $role,
            'email' => $email,
            'phone' => $phone,
        ];
    }
    return $rows;
}

function cudBoolFromInput($value): int {
    if (is_bool($value)) {
        return $value ? 1 : 0;
    }
    if (is_numeric($value)) {
        return ((int)$value) === 1 ? 1 : 0;
    }
    $normalized = strtolower(trim((string)$value));
    return in_array($normalized, ['1', 'true', 'on', 'yes', 'si'], true) ? 1 : 0;
}

$targetUserId = (int)($_GET['user_id'] ?? 0);
if ($targetUserId <= 0) {
    http_response_code(400);
    echo cudTxt('Utente non valido.', 'Invalid user.');
    exit;
}

$hasUsersCompany = false;
$hasUsersPhone = false;
$hasContactsCompany = false;
$hasUsersAddress = false;
$hasUsersVatNumber = false;
$hasPortalAccessLevel = false;
$hasGoogleAuthSecret = false;
$hasExtraContactsJson = false;
$hasCanFirmwareUpdate = false;
$hasCanCreatePlants = false;
$hasCanManageSerialLifecycle = false;
$hasCanReserveSerials = false;
$hasCanAssignManualPeripherals = false;
try {
    $hasUsersCompany = cudColumnExists($pdo, 'users', 'company');
    $hasUsersPhone = cudColumnExists($pdo, 'users', 'phone');
    $hasContactsCompany = cudColumnExists($pdo, 'contacts', 'company');
    $hasUsersAddress = cudColumnExists($pdo, 'users', 'address');
    $hasUsersVatNumber = cudColumnExists($pdo, 'users', 'vat_number');
    $hasPortalAccessLevel = cudColumnExists($pdo, 'users', 'portal_access_level');
    $hasGoogleAuthSecret = cudColumnExists($pdo, 'users', 'google_auth_secret');
    $hasExtraContactsJson = cudColumnExists($pdo, 'users', 'extra_contacts_json');
    $hasCanFirmwareUpdate = cudColumnExists($pdo, 'users', 'can_firmware_update');
    $hasCanCreatePlants = cudColumnExists($pdo, 'users', 'can_create_plants');
    $hasCanManageSerialLifecycle = cudColumnExists($pdo, 'users', 'can_manage_serial_lifecycle');
    $hasCanReserveSerials = cudColumnExists($pdo, 'users', 'can_reserve_serials');
    $hasCanAssignManualPeripherals = cudColumnExists($pdo, 'users', 'can_assign_manual_peripherals');
} catch (Throwable $e) {
    $hasUsersCompany = false;
    $hasUsersPhone = false;
    $hasContactsCompany = false;
    $hasUsersAddress = false;
    $hasUsersVatNumber = false;
    $hasPortalAccessLevel = false;
    $hasGoogleAuthSecret = false;
    $hasExtraContactsJson = false;
    $hasCanFirmwareUpdate = false;
    $hasCanCreatePlants = false;
    $hasCanManageSerialLifecycle = false;
    $hasCanReserveSerials = false;
    $hasCanAssignManualPeripherals = false;
}

$targetUser = null;
$contactRow = null;
$message = '';
$messageType = 'info';
$activationFallbackUrl = '';

try {
    $stmtUser = $pdo->prepare("
        SELECT
            id, email, role, created_at,
            " . ($hasUsersCompany ? "company" : "NULL") . " AS company,
            " . ($hasUsersPhone ? "phone" : "NULL") . " AS phone,
            " . ($hasUsersAddress ? "address" : "NULL") . " AS address,
            " . ($hasUsersVatNumber ? "vat_number" : "NULL") . " AS vat_number,
            " . ($hasPortalAccessLevel ? "portal_access_level" : "'active'") . " AS portal_access_level,
            " . ($hasExtraContactsJson ? "extra_contacts_json" : "NULL") . " AS extra_contacts_json,
            " . ($hasCanFirmwareUpdate ? "can_firmware_update" : "0") . " AS can_firmware_update,
            " . ($hasCanCreatePlants ? "can_create_plants" : "0") . " AS can_create_plants,
            " . ($hasCanManageSerialLifecycle ? "can_manage_serial_lifecycle" : "0") . " AS can_manage_serial_lifecycle,
            " . ($hasCanReserveSerials ? "can_reserve_serials" : "0") . " AS can_reserve_serials,
            " . ($hasCanAssignManualPeripherals ? "can_assign_manual_peripherals" : "0") . " AS can_assign_manual_peripherals
        FROM users
        WHERE id = ?
        LIMIT 1
    ");
    $stmtUser->execute([$targetUserId]);
    $targetUser = $stmtUser->fetch(PDO::FETCH_ASSOC);
} catch (Throwable $e) {
    $targetUser = null;
}

if (!$targetUser) {
    http_response_code(404);
    echo cudTxt('Utente non trovato.', 'User not found.');
    exit;
}

$targetRole = (string)($targetUser['role'] ?? '');

$canAccess = false;
if ($isAdmin) {
    $canAccess = in_array($targetRole, ['builder', 'maintainer', 'client'], true);
} elseif ($currentRole === 'builder' && in_array($targetRole, ['maintainer', 'client'], true)) {
    $stmtLink = $pdo->prepare("SELECT * FROM contacts WHERE managed_by_user_id = ? AND linked_user_id = ? ORDER BY id DESC LIMIT 1");
    $stmtLink->execute([$currentUserId, $targetUserId]);
    $contactRow = $stmtLink->fetch(PDO::FETCH_ASSOC) ?: null;
    $canAccess = (bool)$contactRow;
} elseif ($currentRole === 'maintainer' && $targetRole === 'client') {
    $stmtLink = $pdo->prepare("SELECT * FROM contacts WHERE managed_by_user_id = ? AND linked_user_id = ? ORDER BY id DESC LIMIT 1");
    $stmtLink->execute([$currentUserId, $targetUserId]);
    $contactRow = $stmtLink->fetch(PDO::FETCH_ASSOC) ?: null;
    $canAccess = (bool)$contactRow;
}

if ($isAdmin) {
    $stmtContactAny = $pdo->prepare("SELECT * FROM contacts WHERE linked_user_id = ? ORDER BY id DESC LIMIT 1");
    $stmtContactAny->execute([$targetUserId]);
    $contactRow = $stmtContactAny->fetch(PDO::FETCH_ASSOC) ?: null;
}

if (!$canAccess) {
    http_response_code(403);
    echo cudTxt('Permessi insufficienti.', 'Insufficient permissions.');
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = (string)($_POST['action'] ?? '');
    if ($action === 'save_user_detail') {
        $name = trim((string)($_POST['name'] ?? ''));
        $company = trim((string)($_POST['company'] ?? ''));
        $phone = trim((string)($_POST['phone'] ?? ''));
        $address = trim((string)($_POST['address'] ?? ''));
        $vatNumber = trim((string)($_POST['vat_number'] ?? ''));
        $newAccessLevel = ecAuthPortalAccessLevel((string)($_POST['portal_access_level'] ?? 'active'));
        $oldAccessLevel = ecAuthPortalAccessLevel((string)($targetUser['portal_access_level'] ?? 'active'));
        $newCanFirmwareUpdate = cudBoolFromInput($_POST['can_firmware_update'] ?? 0);
        $newCanCreatePlants = cudBoolFromInput($_POST['can_create_plants'] ?? 0);
        $newCanManageSerialLifecycle = cudBoolFromInput($_POST['can_manage_serial_lifecycle'] ?? 0);
        $newCanReserveSerials = cudBoolFromInput($_POST['can_reserve_serials'] ?? 0);
        $newCanAssignManualPeripherals = cudBoolFromInput($_POST['can_assign_manual_peripherals'] ?? 0);
        $extraContacts = cudNormalizeExtraContacts(
            (array)($_POST['extra_contact_name'] ?? []),
            (array)($_POST['extra_contact_role'] ?? []),
            (array)($_POST['extra_contact_email'] ?? []),
            (array)($_POST['extra_contact_phone'] ?? [])
        );
        $extraContactsJson = json_encode($extraContacts, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);

        if ($name === '' || $company === '') {
            $message = cudTxt('Nome e azienda sono obbligatori.', 'Name and company are required.');
            $messageType = 'danger';
        } else {
            try {
                $pdo->beginTransaction();

                $activationToken = null;
                $shouldSendActivation = $hasPortalAccessLevel && $oldAccessLevel !== 'active' && $newAccessLevel === 'active';

                $updUserSet = [];
                $updUserParams = [];
                if ($hasUsersCompany) {
                    $updUserSet[] = "company = ?";
                    $updUserParams[] = $company;
                }
                if ($hasUsersPhone) {
                    $updUserSet[] = "phone = ?";
                    $updUserParams[] = ($phone !== '' ? $phone : null);
                }
                if ($hasUsersAddress) {
                    $updUserSet[] = "address = ?";
                    $updUserParams[] = ($address !== '' ? $address : null);
                }
                if ($hasUsersVatNumber) {
                    $updUserSet[] = "vat_number = ?";
                    $updUserParams[] = ($vatNumber !== '' ? $vatNumber : null);
                }
                if ($hasPortalAccessLevel) {
                    $updUserSet[] = "portal_access_level = ?";
                    $updUserParams[] = $newAccessLevel;
                }
                if ($isAdmin && $hasCanFirmwareUpdate) {
                    $updUserSet[] = "can_firmware_update = ?";
                    $updUserParams[] = $newCanFirmwareUpdate;
                }
                if ($isAdmin && $hasCanCreatePlants) {
                    $updUserSet[] = "can_create_plants = ?";
                    $updUserParams[] = $newCanCreatePlants;
                }
                if ($isAdmin && $hasCanManageSerialLifecycle) {
                    $updUserSet[] = "can_manage_serial_lifecycle = ?";
                    $updUserParams[] = $newCanManageSerialLifecycle;
                }
                if ($isAdmin && $hasCanReserveSerials) {
                    $updUserSet[] = "can_reserve_serials = ?";
                    $updUserParams[] = $newCanReserveSerials;
                }
                if ($isAdmin && $hasCanAssignManualPeripherals) {
                    $updUserSet[] = "can_assign_manual_peripherals = ?";
                    $updUserParams[] = $newCanAssignManualPeripherals;
                }
                if ($hasExtraContactsJson) {
                    $updUserSet[] = "extra_contacts_json = ?";
                    $updUserParams[] = !empty($extraContacts) ? $extraContactsJson : null;
                }
                if ($shouldSendActivation) {
                    $updUserSet[] = "password_hash = ?";
                    $updUserParams[] = ecAuthMakeUnusablePasswordHash();
                    if ($hasGoogleAuthSecret) {
                        $updUserSet[] = "google_auth_secret = NULL";
                    }
                }

                if (!empty($updUserSet)) {
                    $updUserParams[] = $targetUserId;
                    $stmtUpdUser = $pdo->prepare("UPDATE users SET " . implode(', ', $updUserSet) . " WHERE id = ?");
                    $stmtUpdUser->execute($updUserParams);
                }

                if ($shouldSendActivation) {
                    $activationToken = ecAuthIssueActivationToken($pdo, $targetUserId, $currentUserId);
                    if ($activationToken === null) {
                        throw new RuntimeException('Tabella attivazione account non disponibile.');
                    }
                }

                if ($contactRow) {
                    $updContactSet = ["name = ?", "phone = ?"];
                    $updContactParams = [$name, ($phone !== '' ? $phone : null)];
                    if ($hasContactsCompany) {
                        $updContactSet[] = "company = ?";
                        $updContactParams[] = $company;
                    }
                    $updContactParams[] = (int)$contactRow['id'];
                    $stmtUpdContact = $pdo->prepare("UPDATE contacts SET " . implode(', ', $updContactSet) . " WHERE id = ?");
                    $stmtUpdContact->execute($updContactParams);
                } else {
                    if ($hasContactsCompany) {
                        $stmtInsContact = $pdo->prepare("
                            INSERT INTO contacts (managed_by_user_id, linked_user_id, name, email, phone, company)
                            VALUES (?, ?, ?, ?, ?, ?)
                        ");
                        $stmtInsContact->execute([$currentUserId, $targetUserId, $name, (string)$targetUser['email'], ($phone !== '' ? $phone : null), $company]);
                    } else {
                        $stmtInsContact = $pdo->prepare("
                            INSERT INTO contacts (managed_by_user_id, linked_user_id, name, email, phone)
                            VALUES (?, ?, ?, ?, ?)
                        ");
                        $stmtInsContact->execute([$currentUserId, $targetUserId, $name, (string)$targetUser['email'], ($phone !== '' ? $phone : null)]);
                    }
                }

                $pdo->commit();

                if ($shouldSendActivation && $activationToken !== null) {
                    $activationUrl = ecAuthActivationUrl($activationToken);
                    if (ecAuthSendActivationEmail((string)$targetUser['email'], $activationUrl)) {
                        $message = cudTxt('Dettaglio utente aggiornato. Email di attivazione inviata.', 'User details updated. Activation email sent.');
                        $messageType = 'success';
                    } else {
                        $activationFallbackUrl = $activationUrl;
                        $message = cudTxt('Dettaglio utente aggiornato, ma la mail non e stata inviata. Usa il link manuale qui sotto.', 'User details updated, but email delivery failed. Use the manual link below.');
                        $messageType = 'warning';
                    }
                } else {
                    $message = cudTxt('Dettaglio utente aggiornato.', 'User details updated.');
                    $messageType = 'success';
                }

                $stmtUser->execute([$targetUserId]);
                $targetUser = $stmtUser->fetch(PDO::FETCH_ASSOC);
                $stmtContactAny = $pdo->prepare("SELECT * FROM contacts WHERE linked_user_id = ? ORDER BY id DESC LIMIT 1");
                $stmtContactAny->execute([$targetUserId]);
                $contactRow = $stmtContactAny->fetch(PDO::FETCH_ASSOC) ?: null;
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $message = cudTxt('Errore aggiornamento: ', 'Update error: ') . $e->getMessage();
                $messageType = 'danger';
            }
        }
    }
}

$formName = trim((string)($contactRow['name'] ?? ''));
if ($formName === '') {
    $formName = trim((string)($targetUser['email'] ?? ''));
}
$formCompany = trim((string)($targetUser['company'] ?? ''));
if ($formCompany === '') {
    $formCompany = trim((string)($contactRow['company'] ?? ''));
}
$formPhone = trim((string)($targetUser['phone'] ?? ''));
if ($formPhone === '') {
    $formPhone = trim((string)($contactRow['phone'] ?? ''));
}
$formAddress = trim((string)($targetUser['address'] ?? ''));
$formVatNumber = trim((string)($targetUser['vat_number'] ?? ''));
$currentAccessLevel = ecAuthPortalAccessLevel((string)($targetUser['portal_access_level'] ?? 'active'));
$currentCanFirmwareUpdate = ecAuthPermissionFlag($targetUser['can_firmware_update'] ?? 0);
$currentCanCreatePlants = ecAuthPermissionFlag($targetUser['can_create_plants'] ?? 0);
$currentCanManageSerialLifecycle = ecAuthPermissionFlag($targetUser['can_manage_serial_lifecycle'] ?? 0);
$currentCanReserveSerials = ecAuthPermissionFlag($targetUser['can_reserve_serials'] ?? 0);
$currentCanAssignManualPeripherals = ecAuthPermissionFlag($targetUser['can_assign_manual_peripherals'] ?? 0);
$extraContacts = [];
if ($hasExtraContactsJson) {
    $decodedExtraContacts = json_decode((string)($targetUser['extra_contacts_json'] ?? ''), true);
    if (is_array($decodedExtraContacts)) {
        foreach ($decodedExtraContacts as $row) {
            if (!is_array($row)) {
                continue;
            }
            $extraContacts[] = [
                'name' => trim((string)($row['name'] ?? '')),
                'role' => trim((string)($row['role'] ?? '')),
                'email' => trim((string)($row['email'] ?? '')),
                'phone' => trim((string)($row['phone'] ?? '')),
            ];
        }
    }
}
if (empty($extraContacts)) {
    $extraContacts[] = ['name' => '', 'role' => '', 'email' => '', 'phone' => ''];
}
$assignedPlants = [];
$hasBuilderColumn = cudColumnExists($pdo, 'masters', 'builder_id');
try {
    $plantSql = "
        SELECT id, nickname, serial_number, address, last_seen, created_at,
               CASE
                   WHEN owner_id = :uid_owner THEN 'owner'
                   WHEN maintainer_id = :uid_maintainer THEN 'maintainer'"
                   . ($hasBuilderColumn ? " WHEN builder_id = :uid_builder THEN 'builder'" : "") . "
                   WHEN creator_id = :uid_creator THEN 'creator'
                   ELSE ''
               END AS assignment_role
        FROM masters
        WHERE deleted_at IS NULL
          AND (owner_id = :uid_owner_q OR maintainer_id = :uid_maintainer_q"
          . ($hasBuilderColumn ? " OR builder_id = :uid_builder_q" : "") . "
               OR creator_id = :uid_creator_q)
        ORDER BY created_at DESC
    ";
    $plantParams = [
        'uid_owner' => $targetUserId,
        'uid_maintainer' => $targetUserId,
        'uid_creator' => $targetUserId,
        'uid_owner_q' => $targetUserId,
        'uid_maintainer_q' => $targetUserId,
        'uid_creator_q' => $targetUserId,
    ];
    if ($hasBuilderColumn) {
        $plantParams['uid_builder'] = $targetUserId;
        $plantParams['uid_builder_q'] = $targetUserId;
    }
    $stmtPlants = $pdo->prepare($plantSql);
    $stmtPlants->execute($plantParams);
    $assignedPlants = $stmtPlants->fetchAll(PDO::FETCH_ASSOC);
} catch (Throwable $e) {
    $assignedPlants = [];
}
$accessLevelOptions = [
    'active' => cudTxt('Attivo', 'Active'),
    'existing_only' => cudTxt('Solo impianti esistenti', 'Existing plants only'),
    'blocked' => cudTxt('Bloccato', 'Blocked'),
];
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo htmlspecialchars(cudTxt('Dettaglio Utente', 'User Detail')); ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <div class="d-flex justify-content-between align-items-center mb-3">
        <h5 class="mb-0"><i class="fas fa-id-card"></i> <?php echo htmlspecialchars(cudTxt('Dettaglio Utente', 'User Detail')); ?></h5>
        <a href="contacts.php" class="btn btn-outline-secondary btn-sm"><i class="fas fa-arrow-left"></i> <?php echo htmlspecialchars(cudTxt('Indietro', 'Back')); ?></a>
    </div>

    <?php if ($message !== ''): ?>
        <div class="alert alert-<?php echo htmlspecialchars($messageType); ?>"><?php echo htmlspecialchars($message); ?></div>
    <?php endif; ?>
    <?php if ($activationFallbackUrl !== ''): ?>
        <div class="alert alert-secondary">
            <div class="fw-semibold mb-1">Link attivazione manuale</div>
            <a href="<?php echo htmlspecialchars($activationFallbackUrl); ?>" target="_blank" rel="noopener"><?php echo htmlspecialchars($activationFallbackUrl); ?></a>
        </div>
    <?php endif; ?>

    <div class="card shadow-sm mb-4">
        <div class="card-header d-flex justify-content-between align-items-center flex-wrap gap-2">
            <div>
                <strong><?php echo htmlspecialchars((string)$targetUser['email']); ?></strong>
                <span class="badge <?php echo htmlspecialchars(cudRoleBadge((string)$targetUser['role'])); ?> ms-2"><?php echo htmlspecialchars(cudRoleLabel((string)$targetUser['role'], $lang)); ?></span>
                <span class="badge <?php echo htmlspecialchars(ecAuthPortalAccessBadgeClass($currentAccessLevel)); ?> ms-2"><?php echo htmlspecialchars(ecAuthPortalAccessLabel($currentAccessLevel)); ?></span>
            </div>
            <div class="small text-muted"><?php echo htmlspecialchars(cudTxt('Creato il', 'Created on')); ?> <?php echo htmlspecialchars((string)($targetUser['created_at'] ?? '-')); ?></div>
        </div>
        <div class="card-body">
            <form method="POST">
                <input type="hidden" name="action" value="save_user_detail">
                <div class="row g-3">
                    <div class="col-md-4">
                        <label class="form-label"><?php echo htmlspecialchars($lang['contacts_name'] ?? cudTxt('Nome', 'Name')); ?>*</label>
                        <input type="text" name="name" class="form-control" value="<?php echo htmlspecialchars($formName); ?>" required>
                    </div>
                    <div class="col-md-4">
                        <label class="form-label"><?php echo htmlspecialchars($lang['contacts_company'] ?? cudTxt('Azienda', 'Company')); ?>*</label>
                        <input type="text" name="company" class="form-control" value="<?php echo htmlspecialchars($formCompany); ?>" required>
                    </div>
                    <div class="col-md-4">
                        <label class="form-label"><?php echo htmlspecialchars($lang['contacts_phone'] ?? cudTxt('Telefono', 'Phone')); ?></label>
                        <input type="text" name="phone" class="form-control" value="<?php echo htmlspecialchars($formPhone); ?>">
                    </div>
                    <?php if ($hasUsersAddress): ?>
                        <div class="col-md-8">
                            <label class="form-label">Indirizzo</label>
                            <input type="text" name="address" class="form-control" value="<?php echo htmlspecialchars($formAddress); ?>">
                        </div>
                    <?php endif; ?>
                    <?php if ($hasUsersVatNumber): ?>
                        <div class="col-md-4">
                            <label class="form-label">Partita IVA</label>
                            <input type="text" name="vat_number" class="form-control" value="<?php echo htmlspecialchars($formVatNumber); ?>">
                        </div>
                    <?php endif; ?>
                    <?php if ($hasPortalAccessLevel): ?>
                        <div class="col-md-4">
                            <label class="form-label">Stato accesso</label>
                            <select name="portal_access_level" class="form-select">
                                <?php foreach ($accessLevelOptions as $value => $label): ?>
                                    <option value="<?php echo htmlspecialchars($value); ?>" <?php echo $currentAccessLevel === $value ? 'selected' : ''; ?>><?php echo htmlspecialchars($label); ?></option>
                                <?php endforeach; ?>
                            </select>
                        </div>
                        <div class="col-md-8">
                            <div class="alert alert-light border h-100 mb-0">
                                <div><strong>Attivo</strong>: accesso completo al portale.</div>
                                <div><strong>Solo impianti esistenti</strong>: puo vedere gli impianti gia assegnati ma non ricevere nuovi impianti.</div>
                                <div><strong>Bloccato</strong>: accesso al portale disabilitato.</div>
                            </div>
                        </div>
                    <?php endif; ?>
                    <?php if (
                        $isAdmin
                        && in_array((string)($targetUser['role'] ?? ''), ['builder', 'maintainer'], true)
                        && ($hasCanFirmwareUpdate || $hasCanCreatePlants || $hasCanManageSerialLifecycle || $hasCanReserveSerials || $hasCanAssignManualPeripherals)
                    ): ?>
                        <div class="col-12">
                            <div class="border rounded p-3 bg-light">
                                <div class="fw-semibold mb-2">Abilitazioni operative</div>
                                <div class="row g-2">
                                    <?php if ($hasCanFirmwareUpdate): ?>
                                    <div class="col-md-6">
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="permFirmwareUpdate" name="can_firmware_update" value="1" <?php echo $currentCanFirmwareUpdate ? 'checked' : ''; ?>>
                                            <label class="form-check-label" for="permFirmwareUpdate">Abilita aggiornamenti firmware (OTA)</label>
                                        </div>
                                    </div>
                                    <?php endif; ?>
                                    <?php if ($hasCanCreatePlants): ?>
                                    <div class="col-md-6">
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="permCreatePlants" name="can_create_plants" value="1" <?php echo $currentCanCreatePlants ? 'checked' : ''; ?>>
                                            <label class="form-check-label" for="permCreatePlants">Abilita creazione impianti</label>
                                        </div>
                                    </div>
                                    <?php endif; ?>
                                    <?php if ($hasCanManageSerialLifecycle): ?>
                                    <div class="col-md-6">
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="permSerialLifecycle" name="can_manage_serial_lifecycle" value="1" <?php echo $currentCanManageSerialLifecycle ? 'checked' : ''; ?>>
                                            <label class="form-check-label" for="permSerialLifecycle">Abilita gestione seriali (assegnazione master + dismissione/annullamento)</label>
                                        </div>
                                    </div>
                                    <?php endif; ?>
                                    <?php if ($hasCanReserveSerials): ?>
                                    <div class="col-md-6">
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="permSerialReserve" name="can_reserve_serials" value="1" <?php echo $currentCanReserveSerials ? 'checked' : ''; ?>>
                                            <label class="form-check-label" for="permSerialReserve">Abilita riserva seriali</label>
                                        </div>
                                    </div>
                                    <?php endif; ?>
                                    <?php if ($hasCanAssignManualPeripherals): ?>
                                    <div class="col-md-6">
                                        <div class="form-check">
                                            <input class="form-check-input" type="checkbox" id="permManualPeripheral" name="can_assign_manual_peripherals" value="1" <?php echo $currentCanAssignManualPeripherals ? 'checked' : ''; ?>>
                                            <label class="form-check-label" for="permManualPeripheral">Abilita assegnazione manuale periferiche</label>
                                        </div>
                                    </div>
                                    <?php endif; ?>
                                </div>
                                <div class="small text-muted mt-2">Di default queste opzioni sono disattivate.</div>
                            </div>
                        </div>
                    <?php endif; ?>
                    <?php if ($hasExtraContactsJson): ?>
                        <div class="col-12">
                            <hr>
                            <div class="d-flex justify-content-between align-items-center mb-2">
                                <label class="form-label mb-0">Referenti aggiuntivi facoltativi</label>
                                <button type="button" class="btn btn-sm btn-outline-secondary" id="addExtraContactRow">
                                    <i class="fas fa-plus"></i> Aggiungi referente
                                </button>
                            </div>
                            <div id="extraContactsWrap">
                                <?php foreach ($extraContacts as $extraIndex => $extraRow): ?>
                                    <div class="row g-2 border rounded p-2 mb-2 extra-contact-row">
                                        <div class="col-md-3">
                                            <input type="text" name="extra_contact_name[]" class="form-control" placeholder="Nome referente" value="<?php echo htmlspecialchars((string)($extraRow['name'] ?? '')); ?>">
                                        </div>
                                        <div class="col-md-3">
                                            <input type="text" name="extra_contact_role[]" class="form-control" placeholder="Ruolo / ufficio" value="<?php echo htmlspecialchars((string)($extraRow['role'] ?? '')); ?>">
                                        </div>
                                        <div class="col-md-3">
                                            <input type="email" name="extra_contact_email[]" class="form-control" placeholder="Email" value="<?php echo htmlspecialchars((string)($extraRow['email'] ?? '')); ?>">
                                        </div>
                                        <div class="col-md-2">
                                            <input type="text" name="extra_contact_phone[]" class="form-control" placeholder="Telefono" value="<?php echo htmlspecialchars((string)($extraRow['phone'] ?? '')); ?>">
                                        </div>
                                        <div class="col-md-1 d-grid">
                                            <button type="button" class="btn btn-outline-danger remove-extra-contact"><i class="fas fa-trash"></i></button>
                                        </div>
                                    </div>
                                <?php endforeach; ?>
                            </div>
                            <template id="extraContactTemplate">
                                <div class="row g-2 border rounded p-2 mb-2 extra-contact-row">
                                    <div class="col-md-3"><input type="text" name="extra_contact_name[]" class="form-control" placeholder="Nome referente"></div>
                                    <div class="col-md-3"><input type="text" name="extra_contact_role[]" class="form-control" placeholder="Ruolo / ufficio"></div>
                                    <div class="col-md-3"><input type="email" name="extra_contact_email[]" class="form-control" placeholder="Email"></div>
                                    <div class="col-md-2"><input type="text" name="extra_contact_phone[]" class="form-control" placeholder="Telefono"></div>
                                    <div class="col-md-1 d-grid"><button type="button" class="btn btn-outline-danger remove-extra-contact"><i class="fas fa-trash"></i></button></div>
                                </div>
                            </template>
                        </div>
                    <?php endif; ?>
                </div>
                <div class="mt-3">
                    <button type="submit" class="btn btn-primary"><?php echo htmlspecialchars(cudTxt('Salva modifiche', 'Save changes')); ?></button>
                </div>
            </form>
        </div>
    </div>

    <div class="card shadow-sm mb-4">
        <div class="card-header d-flex justify-content-between align-items-center">
            <strong><i class="fas fa-industry"></i> Impianti assegnati</strong>
            <span class="badge bg-secondary"><?php echo count($assignedPlants); ?></span>
        </div>
        <div class="card-body p-0">
            <div class="table-responsive">
                <table class="table table-sm table-hover mb-0">
                    <thead class="table-light">
                        <tr>
                            <th>ID</th>
                            <th>Nome impianto</th>
                            <th>Seriale master</th>
                            <th>Ruolo</th>
                            <th>Indirizzo</th>
                            <th>Ultima connessione</th>
                        </tr>
                    </thead>
                    <tbody>
                        <?php if (empty($assignedPlants)): ?>
                            <tr><td colspan="6" class="text-center text-muted p-3">Nessun impianto assegnato.</td></tr>
                        <?php else: ?>
                            <?php foreach ($assignedPlants as $plant): ?>
                                <tr>
                                    <td><a href="plant_detail.php?plant_id=<?php echo (int)$plant['id']; ?>" class="text-decoration-none fw-semibold"><?php echo (int)$plant['id']; ?></a></td>
                                    <td><a href="plant_detail.php?plant_id=<?php echo (int)$plant['id']; ?>" class="text-decoration-none"><?php echo htmlspecialchars((string)($plant['nickname'] ?? '')); ?></a></td>
                                    <td><?php echo htmlspecialchars((string)($plant['serial_number'] ?? '')); ?></td>
                                    <td><span class="badge bg-info text-dark"><?php echo htmlspecialchars((string)($plant['assignment_role'] ?? '')); ?></span></td>
                                    <td><?php echo htmlspecialchars((string)($plant['address'] ?? '-')); ?></td>
                                    <td><?php echo htmlspecialchars((string)($plant['last_seen'] ?? '-')); ?></td>
                                </tr>
                            <?php endforeach; ?>
                        <?php endif; ?>
                    </tbody>
                </table>
            </div>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
<script>
document.addEventListener('DOMContentLoaded', function () {
    const addBtn = document.getElementById('addExtraContactRow');
    const wrap = document.getElementById('extraContactsWrap');
    const tpl = document.getElementById('extraContactTemplate');

    function bindRemoveButtons(scope) {
        scope.querySelectorAll('.remove-extra-contact').forEach((btn) => {
            btn.onclick = function () {
                const row = this.closest('.extra-contact-row');
                if (row) row.remove();
            };
        });
    }

    if (wrap) {
        bindRemoveButtons(wrap);
    }
    if (addBtn && wrap && tpl) {
        addBtn.addEventListener('click', function () {
            wrap.insertAdjacentHTML('beforeend', tpl.innerHTML);
            bindRemoveButtons(wrap);
        });
    }
});
</script>
</body>
</html>
