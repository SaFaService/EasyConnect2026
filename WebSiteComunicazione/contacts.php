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
$isIt = (string)($_SESSION['lang'] ?? 'it') === 'it';
$isAdmin = $currentRole === 'admin';

if ($currentRole === 'client') {
    header("Location: index.php");
    exit;
}

function abTxt(string $it, string $en): string {
    global $isIt;
    return $isIt ? $it : $en;
}

function abColumnExists(PDO $pdo, string $tableName, string $columnName): bool {
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

function abRoleLabel(string $role, array $lang): string {
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

function abRoleBadge(string $role): string {
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

function abAccessIconClass(string $accessLevel): string {
    if ($accessLevel === 'active') {
        return 'fa-circle-check text-success';
    }
    if ($accessLevel === 'existing_only') {
        return 'fa-eye text-warning';
    }
    return 'fa-ban text-danger';
}

function abNormalizeComparable(string $value): string {
    $v = strtolower(trim($value));
    if ($v === '') {
        return '';
    }
    if (function_exists('iconv')) {
        $iconv = @iconv('UTF-8', 'ASCII//TRANSLIT//IGNORE', $v);
        if ($iconv !== false) {
            $v = strtolower($iconv);
        }
    }
    $v = preg_replace('/[^a-z0-9]+/', ' ', $v) ?? $v;
    $v = preg_replace('/\s+/', ' ', $v) ?? $v;
    return trim($v);
}

function abSimilarityRatio(string $a, string $b): float {
    if ($a === '' || $b === '') {
        return 0.0;
    }
    if ($a === $b) {
        return 1.0;
    }
    if (strpos($a, $b) !== false || strpos($b, $a) !== false) {
        return 0.92;
    }
    $pct = 0.0;
    similar_text($a, $b, $pct);
    return max(0.0, min(1.0, $pct / 100.0));
}

function abFindPotentialDuplicateUsers(
    PDO $pdo,
    string $role,
    string $company,
    string $address,
    bool $hasUsersCompany,
    bool $hasUsersAddress,
    bool $hasContactsCompany
): array {
    $companyNorm = abNormalizeComparable($company);
    if ($companyNorm === '' || $role === '') {
        return [];
    }

    $companySel = $hasUsersCompany ? "u.company" : "NULL";
    $addressSel = $hasUsersAddress ? "u.address" : "NULL";
    $contactCompanySel = $hasContactsCompany ? "c.company" : "NULL";

    $sql = "
        SELECT
            u.id AS user_id,
            u.email AS user_email,
            u.role AS user_role,
            {$companySel} AS user_company,
            {$addressSel} AS user_address,
            c.name AS contact_name,
            {$contactCompanySel} AS contact_company
        FROM users u
        LEFT JOIN contacts c ON c.id = (
            SELECT c2.id
            FROM contacts c2
            WHERE c2.linked_user_id = u.id
            ORDER BY c2.id DESC
            LIMIT 1
        )
        WHERE u.role = ?
        ORDER BY u.id DESC
        LIMIT 500
    ";
    $stmt = $pdo->prepare($sql);
    $stmt->execute([$role]);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

    $addressNorm = abNormalizeComparable($address);
    $candidates = [];
    foreach ($rows as $row) {
        $candidateCompany = trim((string)($row['user_company'] ?? ''));
        if ($candidateCompany === '') {
            $candidateCompany = trim((string)($row['contact_company'] ?? ''));
        }
        $candidateAddress = trim((string)($row['user_address'] ?? ''));

        $candidateCompanyNorm = abNormalizeComparable($candidateCompany);
        if ($candidateCompanyNorm === '') {
            continue;
        }

        $companyScore = abSimilarityRatio($companyNorm, $candidateCompanyNorm);
        $addressScore = 0.0;
        if ($addressNorm !== '') {
            $addressScore = abSimilarityRatio($addressNorm, abNormalizeComparable($candidateAddress));
        }
        $totalScore = $addressNorm !== ''
            ? ($companyScore * 0.80 + $addressScore * 0.20)
            : $companyScore;

        $isStrong = $companyScore >= 0.94 || $totalScore >= 0.88 || ($companyScore >= 0.85 && $addressScore >= 0.70);
        if (!$isStrong) {
            continue;
        }

        $candidates[] = [
            'user_id' => (int)($row['user_id'] ?? 0),
            'email' => (string)($row['user_email'] ?? ''),
            'role' => (string)($row['user_role'] ?? ''),
            'name' => trim((string)($row['contact_name'] ?? '')),
            'company' => $candidateCompany,
            'address' => $candidateAddress,
            'score' => (int)round($totalScore * 100),
        ];
    }

    usort($candidates, static function ($a, $b) {
        return (int)($b['score'] ?? 0) <=> (int)($a['score'] ?? 0);
    });
    return array_slice($candidates, 0, 5);
}

function abCanDeleteRow(array $row, string $currentRole, int $currentUserId): bool {
    $role = (string)($row['user_role'] ?? '');
    $managerId = (int)($row['manager_id'] ?? 0);
    if ($currentRole === 'admin' && $role !== 'admin') {
        return true;
    }
    if ($currentRole === 'builder' && in_array($role, ['maintainer', 'client'], true)) {
        return $managerId === $currentUserId;
    }
    if ($currentRole === 'maintainer' && $role === 'client') {
        return $managerId === $currentUserId;
    }
    return false;
}

function abRenderDirectoryTable(array $rows, bool $isAdmin, string $currentRole, int $currentUserId, array $lang): void {
    $colspan = 6;
    ?>
    <div class="table-responsive">
        <table class="table table-hover mb-0 align-middle">
            <thead class="table-light">
                <tr>
                    <th><?php echo htmlspecialchars($lang['contacts_name'] ?? 'Nome'); ?></th>
                    <th><?php echo htmlspecialchars($lang['contacts_company'] ?? 'Azienda'); ?></th>
                    <th>Accesso</th>
                    <th><?php echo htmlspecialchars($lang['users_role'] ?? 'Ruolo'); ?></th>
                    <th><?php echo htmlspecialchars($lang['users_col_created'] ?? 'Creato'); ?></th>
                    <th class="text-end actions-col"><?php echo htmlspecialchars($lang['table_actions'] ?? 'Azioni'); ?></th>
                </tr>
            </thead>
            <tbody>
                <?php if (empty($rows)): ?>
                    <tr>
                        <td colspan="<?php echo $colspan; ?>" class="text-muted p-3"><?php echo htmlspecialchars(abTxt('Nessun utente in rubrica.', 'No users in address book.')); ?></td>
                    </tr>
                <?php else: ?>
                    <?php foreach ($rows as $row): ?>
                        <?php
                        $name = trim((string)($row['contact_name'] ?? ''));
                        if ($name === '') {
                            $name = trim((string)($row['user_email'] ?? ''));
                        }
                        $company = trim((string)($row['user_company'] ?? ''));
                        if ($company === '') {
                            $company = trim((string)($row['contact_company'] ?? ''));
                        }
                        $phone = trim((string)($row['user_phone'] ?? ''));
                        if ($phone === '') {
                            $phone = trim((string)($row['contact_phone'] ?? ''));
                        }
                        $address = trim((string)($row['user_address'] ?? ''));
                        $accessLevel = ecAuthPortalAccessLevel((string)($row['portal_access_level'] ?? 'active'));
                        $canDelete = abCanDeleteRow($row, $currentRole, $currentUserId);
                        ?>
                        <tr
                            data-name="<?php echo htmlspecialchars(strtolower($name)); ?>"
                            data-company="<?php echo htmlspecialchars(strtolower($company)); ?>"
                            data-email="<?php echo htmlspecialchars(strtolower((string)($row['user_email'] ?? ''))); ?>"
                            data-phone="<?php echo htmlspecialchars(strtolower($phone)); ?>"
                            data-address="<?php echo htmlspecialchars(strtolower($address)); ?>"
                            data-role="<?php echo htmlspecialchars(strtolower((string)($row['user_role'] ?? ''))); ?>"
                            data-access="<?php echo htmlspecialchars(strtolower($accessLevel)); ?>"
                        >
                            <td>
                                <a href="contact_detail.php?user_id=<?php echo (int)$row['user_id']; ?>" class="text-decoration-none">
                                    <span class="maskable" data-real="<?php echo htmlspecialchars($name); ?>"><?php echo htmlspecialchars($name); ?></span>
                                </a>
                            </td>
                            <td>
                                <a href="contact_detail.php?user_id=<?php echo (int)$row['user_id']; ?>" class="text-decoration-none">
                                    <span class="maskable" data-real="<?php echo htmlspecialchars($company !== '' ? $company : 'N/D'); ?>"><?php echo htmlspecialchars($company !== '' ? $company : 'N/D'); ?></span>
                                </a>
                            </td>
                            <td>
                                <i class="fas <?php echo htmlspecialchars(abAccessIconClass($accessLevel)); ?> me-1" title="<?php echo htmlspecialchars(ecAuthPortalAccessLabel($accessLevel)); ?>"></i>
                                <span class="badge <?php echo htmlspecialchars(ecAuthPortalAccessBadgeClass($accessLevel)); ?>"><?php echo htmlspecialchars(ecAuthPortalAccessLabel($accessLevel)); ?></span>
                            </td>
                            <td><span class="badge <?php echo htmlspecialchars(abRoleBadge((string)$row['user_role'])); ?>"><?php echo htmlspecialchars(abRoleLabel((string)$row['user_role'], $lang)); ?></span></td>
                            <td><?php echo htmlspecialchars((string)($row['user_created_at'] ?? '-')); ?></td>
                            <td class="text-end actions-col">
                                <a href="contact_detail.php?user_id=<?php echo (int)$row['user_id']; ?>" class="btn btn-sm btn-outline-primary" title="<?php echo htmlspecialchars(abTxt('Dettaglio utente', 'User details')); ?>">
                                    <i class="fas fa-pen-to-square"></i>
                                </a>
                                <?php if ($canDelete && (int)($row['user_id'] ?? 0) > 0): ?>
                                    <form method="POST" class="d-inline" onsubmit="return confirm('<?php echo htmlspecialchars(abTxt('Confermi eliminazione utente?', 'Confirm user deletion?'), ENT_QUOTES); ?>');">
                                        <input type="hidden" name="action" value="delete_user">
                                        <input type="hidden" name="target_user_id" value="<?php echo (int)$row['user_id']; ?>">
                                        <input type="hidden" name="target_manager_id" value="<?php echo (int)($row['manager_id'] ?? 0); ?>">
                                        <button type="submit" class="btn btn-sm btn-outline-danger"><i class="fas fa-trash"></i></button>
                                    </form>
                                <?php else: ?>
                                    <button class="btn btn-sm btn-outline-secondary" disabled><i class="fas fa-lock"></i></button>
                                <?php endif; ?>
                            </td>
                        </tr>
                    <?php endforeach; ?>
                <?php endif; ?>
            </tbody>
        </table>
    </div>
    <?php
}

$hasUsersCompany = false;
$hasContactsCompany = false;
$hasUsersPhone = false;
$hasUsersAddress = false;
$hasUsersVatNumber = false;
$hasPortalAccessLevel = false;
try {
    $hasUsersCompany = abColumnExists($pdo, 'users', 'company');
    $hasContactsCompany = abColumnExists($pdo, 'contacts', 'company');
    $hasUsersPhone = abColumnExists($pdo, 'users', 'phone');
    $hasUsersAddress = abColumnExists($pdo, 'users', 'address');
    $hasUsersVatNumber = abColumnExists($pdo, 'users', 'vat_number');
    $hasPortalAccessLevel = abColumnExists($pdo, 'users', 'portal_access_level');
} catch (Throwable $e) {
    $hasUsersCompany = false;
    $hasContactsCompany = false;
    $hasUsersPhone = false;
    $hasUsersAddress = false;
    $hasUsersVatNumber = false;
    $hasPortalAccessLevel = false;
}

$allowedRolesByCreator = [
    'admin' => ['builder', 'maintainer', 'client'],
    'builder' => ['maintainer', 'client'],
    'maintainer' => ['client'],
];
$allowedCreateRoles = $allowedRolesByCreator[$currentRole] ?? [];
$canCreateUsers = !empty($allowedCreateRoles);
$accessLevelOptions = [
    'active' => abTxt('Attivo', 'Active'),
    'existing_only' => abTxt('Solo impianti esistenti', 'Existing plants only'),
    'blocked' => abTxt('Bloccato', 'Blocked'),
];

$message = '';
$messageType = 'info';
$requestAction = (string)($_POST['action'] ?? '');
$duplicateCandidates = [];
$showDuplicatePrompt = false;
$createFormState = [
    'name' => '',
    'email' => '',
    'password' => '',
    'role' => (string)($allowedCreateRoles[0] ?? ''),
    'phone' => '',
    'company' => '',
    'address' => '',
    'vat_number' => '',
    'portal_access_level' => 'active',
    'force_create_if_similar' => false,
];

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $requestAction;

    if ($action === 'add_user') {
        if (!$canCreateUsers) {
            $message = abTxt('Permessi insufficienti per creare utenti.', 'Insufficient permissions to create users.');
            $messageType = 'danger';
        } else {
            $name = trim((string)($_POST['name'] ?? ''));
            $email = trim((string)($_POST['email'] ?? ''));
            $password = (string)($_POST['password'] ?? '');
            $role = trim((string)($_POST['role'] ?? ''));
            $phone = trim((string)($_POST['phone'] ?? ''));
            $company = trim((string)($_POST['company'] ?? ''));
            $address = trim((string)($_POST['address'] ?? ''));
            $vatNumber = trim((string)($_POST['vat_number'] ?? ''));
            $accessLevel = ecAuthPortalAccessLevel((string)($_POST['portal_access_level'] ?? 'active'));
            $forceCreateIfSimilar = !empty($_POST['force_create_if_similar']);

            $createFormState = [
                'name' => $name,
                'email' => $email,
                'password' => $password,
                'role' => $role,
                'phone' => $phone,
                'company' => $company,
                'address' => $address,
                'vat_number' => $vatNumber,
                'portal_access_level' => $accessLevel,
                'force_create_if_similar' => $forceCreateIfSimilar,
            ];

            if ($name === '' || $email === '' || $role === '' || $company === '') {
                $message = abTxt('Compila tutti i campi obbligatori.', 'Please fill all required fields.');
                $messageType = 'danger';
            } elseif (!in_array($role, $allowedCreateRoles, true)) {
                $message = abTxt('Ruolo non consentito per la tua utenza.', 'Selected role is not allowed for your account.');
                $messageType = 'danger';
            } else {
                try {
                    $check = $pdo->prepare("SELECT id, role FROM users WHERE email = ? LIMIT 1");
                    $check->execute([$email]);
                    $existingUser = $check->fetch(PDO::FETCH_ASSOC) ?: null;
                    if ($existingUser) {
                        $existingUserId = (int)($existingUser['id'] ?? 0);
                        $existingUserRole = (string)($existingUser['role'] ?? '');
                        if (!in_array($existingUserRole, $allowedCreateRoles, true)) {
                            $message = abTxt(
                                'Email gia presente ma con un ruolo non gestibile dalla tua utenza.',
                                'Email already exists with a role your account cannot manage.'
                            );
                            $messageType = 'danger';
                        } else {
                            $stmtCkContact = $pdo->prepare("
                                SELECT id
                                FROM contacts
                                WHERE managed_by_user_id = ?
                                  AND (linked_user_id = ? OR email = ?)
                                ORDER BY id DESC
                                LIMIT 1
                            ");
                            $stmtCkContact->execute([$currentUserId, $existingUserId, $email]);
                            $contactId = (int)($stmtCkContact->fetchColumn() ?: 0);

                            if ($contactId > 0) {
                                if ($hasContactsCompany) {
                                    $stmtUpdContact = $pdo->prepare("UPDATE contacts SET linked_user_id = ?, name = ?, email = ?, phone = ?, company = ? WHERE id = ?");
                                    $stmtUpdContact->execute([$existingUserId, $name, $email, $phone !== '' ? $phone : null, $company, $contactId]);
                                } else {
                                    $stmtUpdContact = $pdo->prepare("UPDATE contacts SET linked_user_id = ?, name = ?, email = ?, phone = ? WHERE id = ?");
                                    $stmtUpdContact->execute([$existingUserId, $name, $email, $phone !== '' ? $phone : null, $contactId]);
                                }
                                $message = abTxt(
                                    'Email gia presente: utente gia in rubrica, dati aggiornati.',
                                    'Email already exists: user already in your address book, details updated.'
                                );
                                $messageType = 'success';
                            } else {
                                if ($hasContactsCompany) {
                                    $stmtInsContact = $pdo->prepare("INSERT INTO contacts (managed_by_user_id, linked_user_id, name, email, phone, company) VALUES (?, ?, ?, ?, ?, ?)");
                                    $stmtInsContact->execute([$currentUserId, $existingUserId, $name, $email, $phone !== '' ? $phone : null, $company]);
                                } else {
                                    $stmtInsContact = $pdo->prepare("INSERT INTO contacts (managed_by_user_id, linked_user_id, name, email, phone) VALUES (?, ?, ?, ?, ?)");
                                    $stmtInsContact->execute([$currentUserId, $existingUserId, $name, $email, $phone !== '' ? $phone : null]);
                                }
                                $message = abTxt(
                                    'Email gia presente: utente esistente collegato alla tua rubrica.',
                                    'Email already exists: existing user linked to your address book.'
                                );
                                $messageType = 'success';
                            }

                            $createFormState = [
                                'name' => '',
                                'email' => '',
                                'password' => '',
                                'role' => (string)($allowedCreateRoles[0] ?? ''),
                                'phone' => '',
                                'company' => '',
                                'address' => '',
                                'vat_number' => '',
                                'portal_access_level' => 'active',
                                'force_create_if_similar' => false,
                            ];
                            $duplicateCandidates = [];
                            $showDuplicatePrompt = false;
                        }
                    } else {
                        if ($accessLevel === 'active' && trim($password) === '') {
                            $message = abTxt('Per un utente attivo devi indicare una password iniziale.', 'An active user requires an initial password.');
                            $messageType = 'danger';
                        } elseif (!$forceCreateIfSimilar) {
                            $duplicateCandidates = abFindPotentialDuplicateUsers(
                                $pdo,
                                $role,
                                $company,
                                $address,
                                $hasUsersCompany,
                                $hasUsersAddress,
                                $hasContactsCompany
                            );
                            if (!empty($duplicateCandidates)) {
                                $showDuplicatePrompt = true;
                                $message = abTxt(
                                    'Possibile duplicato rilevato. Controlla i suggerimenti prima di creare un nuovo record.',
                                    'Potential duplicate found. Review suggestions before creating a new record.'
                                );
                                $messageType = 'warning';
                            }
                        }

                        if ($message === '') {
                            $passwordHash = $accessLevel === 'active'
                                ? password_hash($password, PASSWORD_DEFAULT)
                                : ecAuthMakeUnusablePasswordHash();

                            $insCols = ['email', 'password_hash', 'role'];
                            $insVals = ['?', '?', '?'];
                            $insParams = [$email, $passwordHash, $role];

                            if ($hasUsersPhone) {
                                $insCols[] = 'phone';
                                $insVals[] = '?';
                                $insParams[] = $phone !== '' ? $phone : null;
                            }
                            if ($hasUsersCompany) {
                                $insCols[] = 'company';
                                $insVals[] = '?';
                                $insParams[] = $company;
                            }
                            if ($hasUsersAddress) {
                                $insCols[] = 'address';
                                $insVals[] = '?';
                                $insParams[] = $address !== '' ? $address : null;
                            }
                            if ($hasUsersVatNumber) {
                                $insCols[] = 'vat_number';
                                $insVals[] = '?';
                                $insParams[] = $vatNumber !== '' ? $vatNumber : null;
                            }
                            if ($hasPortalAccessLevel) {
                                $insCols[] = 'portal_access_level';
                                $insVals[] = '?';
                                $insParams[] = $accessLevel;
                            }

                            $stmtInsUser = $pdo->prepare("INSERT INTO users (" . implode(', ', $insCols) . ") VALUES (" . implode(', ', $insVals) . ")");
                            $stmtInsUser->execute($insParams);
                            $newUserId = (int)$pdo->lastInsertId();

                            $stmtCkContact = $pdo->prepare("
                                SELECT id
                                FROM contacts
                                WHERE managed_by_user_id = ?
                                  AND (linked_user_id = ? OR email = ?)
                                ORDER BY id DESC
                                LIMIT 1
                            ");
                            $stmtCkContact->execute([$currentUserId, $newUserId, $email]);
                            $contactId = (int)($stmtCkContact->fetchColumn() ?: 0);

                            if ($contactId > 0) {
                                if ($hasContactsCompany) {
                                    $stmtUpdContact = $pdo->prepare("UPDATE contacts SET linked_user_id = ?, name = ?, email = ?, phone = ?, company = ? WHERE id = ?");
                                    $stmtUpdContact->execute([$newUserId, $name, $email, $phone !== '' ? $phone : null, $company, $contactId]);
                                } else {
                                    $stmtUpdContact = $pdo->prepare("UPDATE contacts SET linked_user_id = ?, name = ?, email = ?, phone = ? WHERE id = ?");
                                    $stmtUpdContact->execute([$newUserId, $name, $email, $phone !== '' ? $phone : null, $contactId]);
                                }
                            } else {
                                if ($hasContactsCompany) {
                                    $stmtInsContact = $pdo->prepare("INSERT INTO contacts (managed_by_user_id, linked_user_id, name, email, phone, company) VALUES (?, ?, ?, ?, ?, ?)");
                                    $stmtInsContact->execute([$currentUserId, $newUserId, $name, $email, $phone !== '' ? $phone : null, $company]);
                                } else {
                                    $stmtInsContact = $pdo->prepare("INSERT INTO contacts (managed_by_user_id, linked_user_id, name, email, phone) VALUES (?, ?, ?, ?, ?)");
                                    $stmtInsContact->execute([$currentUserId, $newUserId, $name, $email, $phone !== '' ? $phone : null]);
                                }
                            }

                            $message = abTxt('Utente creato correttamente nella rubrica.', 'User created successfully in address book.');
                            $messageType = 'success';
                            $createFormState = [
                                'name' => '',
                                'email' => '',
                                'password' => '',
                                'role' => (string)($allowedCreateRoles[0] ?? ''),
                                'phone' => '',
                                'company' => '',
                                'address' => '',
                                'vat_number' => '',
                                'portal_access_level' => 'active',
                                'force_create_if_similar' => false,
                            ];
                            $duplicateCandidates = [];
                            $showDuplicatePrompt = false;
                        }
                    }
                } catch (Throwable $e) {
                    $message = abTxt('Errore durante la creazione utente: ', 'Error while creating user: ') . $e->getMessage();
                    $messageType = 'danger';
                }
            }
        }
    }

    if ($action === 'delete_user') {
        $targetUserId = (int)($_POST['target_user_id'] ?? 0);
        $targetManagerId = (int)($_POST['target_manager_id'] ?? 0);
        if ($targetUserId <= 0) {
            $message = abTxt('Utente non valido.', 'Invalid user.');
            $messageType = 'danger';
        } elseif ($targetUserId === $currentUserId) {
            $message = abTxt('Non puoi eliminare il tuo account.', 'You cannot delete your own account.');
            $messageType = 'danger';
        } else {
            try {
                $stmtRole = $pdo->prepare("SELECT role FROM users WHERE id = ? LIMIT 1");
                $stmtRole->execute([$targetUserId]);
                $targetRole = (string)($stmtRole->fetchColumn() ?: '');
                $canDelete = false;

                if ($currentRole === 'admin' && $targetRole !== 'admin') {
                    $canDelete = true;
                } elseif ($currentRole === 'builder' && in_array($targetRole, ['maintainer', 'client'], true)) {
                    $stmtLink = $pdo->prepare("SELECT id FROM contacts WHERE managed_by_user_id = ? AND linked_user_id = ? LIMIT 1");
                    $stmtLink->execute([$currentUserId, $targetUserId]);
                    $canDelete = (bool)$stmtLink->fetchColumn();
                } elseif ($currentRole === 'maintainer' && $targetRole === 'client') {
                    $stmtLink = $pdo->prepare("SELECT id FROM contacts WHERE managed_by_user_id = ? AND linked_user_id = ? LIMIT 1");
                    $stmtLink->execute([$currentUserId, $targetUserId]);
                    $canDelete = (bool)$stmtLink->fetchColumn();
                }

                if (!$canDelete) {
                    $message = abTxt('Non hai i permessi per eliminare questo utente.', 'You do not have permission to delete this user.');
                    $messageType = 'danger';
                } else {
                    $pdo->beginTransaction();

                    $deleteManagerId = $currentUserId;
                    if ($currentRole === 'admin' && $targetManagerId > 0) {
                        $deleteManagerId = $targetManagerId;
                    }

                    $stmtDelLink = $pdo->prepare("
                        DELETE FROM contacts
                        WHERE managed_by_user_id = ?
                          AND linked_user_id = ?
                    ");
                    $stmtDelLink->execute([$deleteManagerId, $targetUserId]);
                    $removedLinks = (int)$stmtDelLink->rowCount();

                    $stmtCountLinks = $pdo->prepare("SELECT COUNT(*) FROM contacts WHERE linked_user_id = ?");
                    $stmtCountLinks->execute([$targetUserId]);
                    $remainingLinks = (int)$stmtCountLinks->fetchColumn();

                    if ($removedLinks <= 0 && $remainingLinks > 0) {
                        $pdo->rollBack();
                        $message = abTxt(
                            'Impossibile rimuovere: il contatto non risulta associato alla tua rubrica.',
                            'Unable to remove: contact is not associated with your address book.'
                        );
                        $messageType = 'danger';
                    } else {
                        if ($remainingLinks <= 0) {
                            try {
                                $pdo->prepare("DELETE FROM users WHERE id = ?")->execute([$targetUserId]);
                                $pdo->commit();
                                $message = abTxt(
                                    'Contatto rimosso dalla rubrica e cancellato definitivamente (nessun altro collegamento).',
                                    'Contact removed from address book and permanently deleted (no other links).'
                                );
                                $messageType = 'warning';
                            } catch (Throwable $deleteUserError) {
                                $pdo->commit();
                                $message = abTxt(
                                    'Contatto rimosso dalla rubrica, ma non cancellabile definitivamente (probabile associazione a impianti).',
                                    'Contact removed from address book, but cannot be permanently deleted (likely linked to plants).'
                                );
                                $messageType = 'warning';
                            }
                        } else {
                            $pdo->commit();
                            $message = abTxt(
                                'Contatto rimosso solo dalla rubrica selezionata. Rimane disponibile nelle altre rubriche collegate.',
                                'Contact removed only from the selected address book. It remains available in other linked address books.'
                            );
                            $messageType = 'info';
                        }
                    }
                }
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $message = abTxt('Impossibile eliminare utente (probabilmente associato a impianti).', 'Unable to delete user (likely linked to plants).');
                $messageType = 'danger';
            }
        }
    }
}

$directoryRows = [];
try {
    $companySelect = $hasUsersCompany ? "u.company" : "NULL";
    $phoneSelect = $hasUsersPhone ? "u.phone" : "NULL";
    $addressSelect = $hasUsersAddress ? "u.address" : "NULL";
    $vatSelect = $hasUsersVatNumber ? "u.vat_number" : "NULL";
    $accessSelect = $hasPortalAccessLevel ? "u.portal_access_level" : "'active'";

    if ($currentRole === 'admin') {
        $sql = "
            SELECT
                u.id AS user_id,
                u.email AS user_email,
                u.role AS user_role,
                u.created_at AS user_created_at,
                {$companySelect} AS user_company,
                {$phoneSelect} AS user_phone,
                {$addressSelect} AS user_address,
                {$vatSelect} AS user_vat_number,
                {$accessSelect} AS portal_access_level,
                c.name AS contact_name,
                c.phone AS contact_phone,
                " . ($hasContactsCompany ? "c.company" : "NULL") . " AS contact_company,
                c.managed_by_user_id AS manager_id,
                mgr.email AS manager_email
            FROM users u
            LEFT JOIN contacts c ON c.id = (
                SELECT c2.id
                FROM contacts c2
                WHERE c2.linked_user_id = u.id
                ORDER BY c2.id DESC
                LIMIT 1
            )
            LEFT JOIN users mgr ON mgr.id = c.managed_by_user_id
            WHERE u.role IN ('builder','maintainer','client')
            ORDER BY u.created_at DESC, u.email ASC
        ";
        $directoryRows = $pdo->query($sql)->fetchAll();
    } else {
        $allowedListRoles = ($currentRole === 'builder') ? ['maintainer', 'client'] : ['client'];
        $ph = implode(',', array_fill(0, count($allowedListRoles), '?'));
        $sql = "
            SELECT
                u.id AS user_id,
                u.email AS user_email,
                u.role AS user_role,
                u.created_at AS user_created_at,
                {$companySelect} AS user_company,
                {$phoneSelect} AS user_phone,
                {$addressSelect} AS user_address,
                {$vatSelect} AS user_vat_number,
                {$accessSelect} AS portal_access_level,
                c.name AS contact_name,
                c.phone AS contact_phone,
                " . ($hasContactsCompany ? "c.company" : "NULL") . " AS contact_company,
                c.managed_by_user_id AS manager_id,
                NULL AS manager_email
            FROM contacts c
            INNER JOIN users u ON u.id = c.linked_user_id
            WHERE c.managed_by_user_id = ?
              AND u.role IN ($ph)
            ORDER BY u.created_at DESC, c.name ASC
        ";
        $params = array_merge([$currentUserId], $allowedListRoles);
        $stmt = $pdo->prepare($sql);
        $stmt->execute($params);
        $directoryRows = $stmt->fetchAll();
    }
} catch (Throwable $e) {
    $directoryRows = [];
    if ($message === '') {
        $message = abTxt('Errore caricamento rubrica: ', 'Address book loading error: ') . $e->getMessage();
        $messageType = 'danger';
    }
}

$activeRows = [];
$inactiveRows = [];
foreach ($directoryRows as $row) {
    $accessLevel = ecAuthPortalAccessLevel((string)($row['portal_access_level'] ?? 'active'));
    if ($accessLevel === 'active') {
        $activeRows[] = $row;
    } else {
        $inactiveRows[] = $row;
    }
}
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo htmlspecialchars($lang['contacts_title'] ?? abTxt('Rubrica', 'Address Book')); ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
    <style>
        .actions-col {
            position: sticky;
            right: 0;
            background: #fff;
            min-width: 130px;
            z-index: 2;
        }
        thead .actions-col {
            background: #f8f9fa;
            z-index: 3;
        }
        .toggle-btn {
            min-width: 34px;
        }
        .contact-filter-grid { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 12px; }
        .contact-filter-grid .full { grid-column: 1 / -1; }
        .contact-row-hidden { display: none !important; }
        @media (max-width: 1200px) {
            .contact-filter-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }
        }
        @media (max-width: 768px) {
            .contact-filter-grid { grid-template-columns: repeat(1, minmax(0, 1fr)); }
        }
    </style>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    <?php if ($message !== ''): ?>
        <div class="alert alert-<?php echo htmlspecialchars($messageType); ?>"><?php echo htmlspecialchars($message); ?></div>
    <?php endif; ?>

    <?php if ($canCreateUsers): ?>
        <div class="card shadow-sm mb-3">
            <div class="card-header d-flex justify-content-between align-items-center bg-primary text-white">
                <strong><i class="fas fa-user-plus"></i> <?php echo htmlspecialchars(abTxt('Crea utente in Rubrica', 'Create user in Address Book')); ?></strong>
                <button class="btn btn-sm btn-light toggle-btn" type="button" data-bs-toggle="collapse" data-bs-target="#createUserBody" aria-expanded="<?php echo ($showDuplicatePrompt || ($requestAction === 'add_user' && $messageType !== 'success')) ? 'true' : 'false'; ?>" aria-controls="createUserBody">
                    <i class="fas fa-chevron-down"></i>
                </button>
            </div>
            <div id="createUserBody" class="collapse<?php echo ($showDuplicatePrompt || ($requestAction === 'add_user' && $messageType !== 'success')) ? ' show' : ''; ?>">
                <div class="card-body">
                    <div class="small text-muted mb-2">
                        <?php
                        if ($currentRole === 'admin') {
                            echo htmlspecialchars(abTxt('Ruoli consentiti: Costruttore, Manutentore, Cliente.', 'Allowed roles: Builder, Maintainer, Client.'));
                        } elseif ($currentRole === 'builder') {
                            echo htmlspecialchars(abTxt('Ruoli consentiti: Manutentore, Cliente.', 'Allowed roles: Maintainer, Client.'));
                        } else {
                            echo htmlspecialchars(abTxt('Ruolo consentito: Cliente.', 'Allowed role: Client.'));
                        }
                        ?>
                    </div>
                    <form method="POST">
                        <input type="hidden" name="action" value="add_user">
                        <?php if ($showDuplicatePrompt && !empty($duplicateCandidates)): ?>
                            <div class="alert alert-warning">
                                <div class="fw-semibold mb-1"><?php echo htmlspecialchars(abTxt('Volevi inserire questo?', 'Did you mean one of these?')); ?></div>
                                <div class="small mb-2"><?php echo htmlspecialchars(abTxt('Abbiamo trovato record simili nel sistema. Controlla prima di creare un nuovo utente.', 'We found similar records in the system. Please review before creating a new user.')); ?></div>
                                <div class="table-responsive mb-2">
                                    <table class="table table-sm table-bordered align-middle bg-white mb-0">
                                        <thead class="table-light">
                                            <tr>
                                                <th><?php echo htmlspecialchars(abTxt('Somiglianza', 'Similarity')); ?></th>
                                                <th><?php echo htmlspecialchars($lang['contacts_company'] ?? abTxt('Azienda', 'Company')); ?></th>
                                                <th><?php echo htmlspecialchars($lang['contacts_name'] ?? abTxt('Nome', 'Name')); ?></th>
                                                <th><?php echo htmlspecialchars($lang['contacts_email'] ?? 'Email'); ?></th>
                                                <th><?php echo htmlspecialchars(abTxt('Indirizzo', 'Address')); ?></th>
                                                <th><?php echo htmlspecialchars(abTxt('Dettaglio', 'Details')); ?></th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <?php foreach ($duplicateCandidates as $candidate): ?>
                                                <tr>
                                                    <td><span class="badge bg-warning text-dark"><?php echo (int)($candidate['score'] ?? 0); ?>%</span></td>
                                                    <td><?php echo htmlspecialchars((string)($candidate['company'] ?? '-')); ?></td>
                                                    <td><?php echo htmlspecialchars((string)($candidate['name'] ?? '-')); ?></td>
                                                    <td><?php echo htmlspecialchars((string)($candidate['email'] ?? '-')); ?></td>
                                                    <td><?php echo htmlspecialchars((string)($candidate['address'] ?? '-')); ?></td>
                                                    <td>
                                                        <a class="btn btn-sm btn-outline-primary" href="contact_detail.php?user_id=<?php echo (int)($candidate['user_id'] ?? 0); ?>">
                                                            <i class="fas fa-eye"></i>
                                                        </a>
                                                    </td>
                                                </tr>
                                            <?php endforeach; ?>
                                        </tbody>
                                    </table>
                                </div>
                                <div class="form-check">
                                    <input class="form-check-input" type="checkbox" value="1" id="forceCreateIfSimilar" name="force_create_if_similar"<?php echo !empty($createFormState['force_create_if_similar']) ? ' checked' : ''; ?>>
                                    <label class="form-check-label" for="forceCreateIfSimilar">
                                        <?php echo htmlspecialchars(abTxt('Confermo la creazione di un nuovo record anche se simile a quelli trovati.', 'I confirm creating a new record even if similar records exist.')); ?>
                                    </label>
                                </div>
                            </div>
                        <?php endif; ?>
                        <div class="row g-2">
                            <div class="col-md-4">
                                <label class="form-label"><?php echo htmlspecialchars($lang['contacts_name'] ?? abTxt('Nome', 'Name')); ?>*</label>
                                <input type="text" name="name" class="form-control" value="<?php echo htmlspecialchars((string)($createFormState['name'] ?? '')); ?>" required>
                            </div>
                            <div class="col-md-4">
                                <label class="form-label"><?php echo htmlspecialchars($lang['contacts_email'] ?? 'Email'); ?>*</label>
                                <input type="email" name="email" class="form-control" value="<?php echo htmlspecialchars((string)($createFormState['email'] ?? '')); ?>" required>
                            </div>
                            <div class="col-md-4">
                                <label class="form-label"><?php echo htmlspecialchars($lang['users_password'] ?? abTxt('Password', 'Password')); ?></label>
                                <input type="password" name="password" id="createUserPassword" class="form-control">
                                <div class="form-text">Obbligatoria solo per utenti attivi.</div>
                            </div>
                            <div class="col-md-3">
                                <label class="form-label"><?php echo htmlspecialchars($lang['contacts_phone'] ?? abTxt('Telefono', 'Phone')); ?></label>
                                <input type="text" name="phone" class="form-control" value="<?php echo htmlspecialchars((string)($createFormState['phone'] ?? '')); ?>">
                            </div>
                            <div class="col-md-5">
                                <label class="form-label"><?php echo htmlspecialchars($lang['contacts_company'] ?? abTxt('Azienda', 'Company')); ?>*</label>
                                <input type="text" name="company" class="form-control" value="<?php echo htmlspecialchars((string)($createFormState['company'] ?? '')); ?>" required>
                            </div>
                            <div class="col-md-4">
                                <label class="form-label"><?php echo htmlspecialchars($lang['users_role'] ?? abTxt('Ruolo', 'Role')); ?>*</label>
                                <select name="role" class="form-select" required>
                                    <?php foreach ($allowedCreateRoles as $r): ?>
                                        <option value="<?php echo htmlspecialchars($r); ?>"<?php echo ((string)($createFormState['role'] ?? '') === $r) ? ' selected' : ''; ?>><?php echo htmlspecialchars(abRoleLabel($r, $lang)); ?></option>
                                    <?php endforeach; ?>
                                </select>
                            </div>
                            <div class="col-md-6">
                                <label class="form-label">Indirizzo</label>
                                <input type="text" name="address" class="form-control" placeholder="Via, numero civico, citta" value="<?php echo htmlspecialchars((string)($createFormState['address'] ?? '')); ?>">
                            </div>
                            <div class="col-md-3">
                                <label class="form-label">Partita IVA</label>
                                <input type="text" name="vat_number" class="form-control" placeholder="IT12345678901" value="<?php echo htmlspecialchars((string)($createFormState['vat_number'] ?? '')); ?>">
                            </div>
                            <div class="col-md-3">
                                <label class="form-label">Stato accesso</label>
                                <select name="portal_access_level" id="createUserAccessLevel" class="form-select">
                                    <?php foreach ($accessLevelOptions as $value => $label): ?>
                                        <option value="<?php echo htmlspecialchars($value); ?>"<?php echo ((string)($createFormState['portal_access_level'] ?? 'active') === $value) ? ' selected' : ''; ?>><?php echo htmlspecialchars($label); ?></option>
                                    <?php endforeach; ?>
                                </select>
                            </div>
                        </div>
                        <button type="submit" class="btn btn-primary mt-3"><?php echo htmlspecialchars($lang['users_btn_create'] ?? abTxt('Crea Utente', 'Create User')); ?></button>
                    </form>
                </div>
            </div>
        </div>
    <?php endif; ?>

    <div class="card shadow-sm">
        <div class="card-header d-flex justify-content-between align-items-center gap-2 flex-wrap">
            <div>
                <strong><i class="fas fa-address-book"></i> <?php echo htmlspecialchars(abTxt('Rubrica Utenti', 'User Address Book')); ?></strong>
                <span class="badge bg-secondary ms-2"><?php echo count($directoryRows); ?></span>
            </div>
            <div class="d-flex gap-2">
                <button class="btn btn-sm btn-outline-secondary" type="button" data-bs-toggle="collapse" data-bs-target="#contactFiltersCollapse" aria-expanded="false" aria-controls="contactFiltersCollapse">
                    <i class="fas fa-filter"></i> <?php echo htmlspecialchars(abTxt('Filtri', 'Filters')); ?>
                </button>
                <button class="btn btn-sm btn-outline-dark" type="button" id="toggleMaskContacts">
                    <i class="fas fa-user-secret"></i> <?php echo htmlspecialchars(abTxt('Oscura utenti', 'Hide users')); ?>: <span id="maskContactsState">OFF</span>
                </button>
            </div>
        </div>
        <div id="contactFiltersCollapse" class="collapse border-top">
            <div class="card-body bg-light">
                <div class="contact-filter-grid">
                    <div>
                        <label class="form-label form-label-sm">Nome</label>
                        <input type="text" class="form-control form-control-sm" id="contactFilterName" placeholder="Cerca nome">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Azienda</label>
                        <input type="text" class="form-control form-control-sm" id="contactFilterCompany" placeholder="Cerca azienda">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Email</label>
                        <input type="text" class="form-control form-control-sm" id="contactFilterEmail" placeholder="Cerca email">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Telefono</label>
                        <input type="text" class="form-control form-control-sm" id="contactFilterPhone" placeholder="Cerca telefono">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Indirizzo</label>
                        <input type="text" class="form-control form-control-sm" id="contactFilterAddress" placeholder="Cerca indirizzo">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Ruolo</label>
                        <input type="text" class="form-control form-control-sm" id="contactFilterRole" placeholder="builder / maintainer / client">
                    </div>
                    <div>
                        <label class="form-label form-label-sm">Accesso</label>
                        <select class="form-select form-select-sm" id="contactFilterAccess">
                            <option value="">Tutti</option>
                            <option value="active">Attivi</option>
                            <option value="existing_only">Solo impianti esistenti</option>
                            <option value="blocked">Bloccati</option>
                        </select>
                    </div>
                    <div class="full d-flex justify-content-between align-items-center">
                        <div class="small text-muted">Visualizzati: <strong id="contactVisibleCount"><?php echo count($directoryRows); ?></strong> / <?php echo count($directoryRows); ?></div>
                        <button class="btn btn-sm btn-outline-secondary" type="button" id="resetContactFilters">Reset filtri</button>
                    </div>
                </div>
            </div>
        </div>
        <div class="card-body p-0">
            <?php abRenderDirectoryTable($directoryRows, $isAdmin, $currentRole, $currentUserId, $lang); ?>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
<script>
document.addEventListener('DOMContentLoaded', function () {
    const accessEl = document.getElementById('createUserAccessLevel');
    const passwordEl = document.getElementById('createUserPassword');
    if (accessEl && passwordEl) {
        function syncPasswordRequirement() {
            const isActive = String(accessEl.value || '') === 'active';
            passwordEl.required = isActive;
            if (!isActive) {
                passwordEl.value = '';
            }
        }

        accessEl.addEventListener('change', syncPasswordRequirement);
        syncPasswordRequirement();
    }

    const filterEls = {
        name: document.getElementById('contactFilterName'),
        company: document.getElementById('contactFilterCompany'),
        email: document.getElementById('contactFilterEmail'),
        phone: document.getElementById('contactFilterPhone'),
        address: document.getElementById('contactFilterAddress'),
        role: document.getElementById('contactFilterRole'),
        access: document.getElementById('contactFilterAccess')
    };
    const rows = Array.from(document.querySelectorAll('tbody tr[data-name]'));
    const visibleCountEl = document.getElementById('contactVisibleCount');
    function applyContactFilters() {
        let visible = 0;
        rows.forEach((row) => {
            const ok =
                (!filterEls.name || !filterEls.name.value || row.dataset.name.includes(filterEls.name.value.toLowerCase())) &&
                (!filterEls.company || !filterEls.company.value || row.dataset.company.includes(filterEls.company.value.toLowerCase())) &&
                (!filterEls.email || !filterEls.email.value || row.dataset.email.includes(filterEls.email.value.toLowerCase())) &&
                (!filterEls.phone || !filterEls.phone.value || row.dataset.phone.includes(filterEls.phone.value.toLowerCase())) &&
                (!filterEls.address || !filterEls.address.value || row.dataset.address.includes(filterEls.address.value.toLowerCase())) &&
                (!filterEls.role || !filterEls.role.value || row.dataset.role.includes(filterEls.role.value.toLowerCase())) &&
                (!filterEls.access || !filterEls.access.value || row.dataset.access === filterEls.access.value.toLowerCase());
            row.classList.toggle('contact-row-hidden', !ok);
            if (ok) visible++;
        });
        if (visibleCountEl) {
            visibleCountEl.textContent = String(visible);
        }
    }
    Object.values(filterEls).forEach((el) => {
        if (!el) return;
        el.addEventListener('input', applyContactFilters);
        el.addEventListener('change', applyContactFilters);
    });
    const resetFiltersEl = document.getElementById('resetContactFilters');
    if (resetFiltersEl) {
        resetFiltersEl.addEventListener('click', function () {
            Object.values(filterEls).forEach((el) => {
                if (el) el.value = '';
            });
            applyContactFilters();
        });
    }

    const maskToggleEl = document.getElementById('toggleMaskContacts');
    const maskStateEl = document.getElementById('maskContactsState');
    const maskableEls = Array.from(document.querySelectorAll('.maskable'));
    const maskKey = 'antralux_contacts_mask_v1';
    function applyMaskState(masked) {
        maskableEls.forEach((el) => {
            const real = el.getAttribute('data-real') || '';
            el.textContent = masked ? 'xxxxx' : real;
        });
        if (maskStateEl) {
            maskStateEl.textContent = masked ? 'ON' : 'OFF';
        }
    }
    let masked = localStorage.getItem(maskKey) === '1';
    applyMaskState(masked);
    if (maskToggleEl) {
        maskToggleEl.addEventListener('click', function () {
            masked = !masked;
            localStorage.setItem(maskKey, masked ? '1' : '0');
            applyMaskState(masked);
        });
    }
});
</script>
</body>
</html>
