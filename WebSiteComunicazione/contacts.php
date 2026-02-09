<?php
session_start();
require 'config.php';

// Includi il gestore della lingua
require 'lang.php';

if (!isset($_SESSION['user_id']) || $_SESSION['user_role'] === 'client') {
    header("Location: index.php"); // I clienti non possono accedere alla rubrica
    exit;
}

$message = '';
$message_type = '';
$currentUserId = $_SESSION['user_id'];

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';

    if ($action === 'add_contact') {
        $name = $_POST['name'];
        $email = $_POST['email'];
        $phone = $_POST['phone'];
        $company = $_POST['company'];

        if (!empty($name) && !empty($email)) {
            try {
                $stmt = $pdo->prepare("INSERT INTO contacts (managed_by_user_id, name, email, phone, company) VALUES (?, ?, ?, ?, ?)");
                $stmt->execute([$currentUserId, $name, $email, $phone, $company]);
                $message = "Contatto '{$name}' aggiunto con successo!";
                $message_type = 'success';
            } catch (PDOException $e) {
                if ($e->errorInfo[1] == 1062) { // Errore di duplicato
                    $message = "Un contatto con questa email esiste già nella tua rubrica.";
                } else {
                    $message = "Errore del database: " . $e->getMessage();
                }
                $message_type = 'danger';
            }
        } else {
            $message = "Nome e Email sono obbligatori.";
            $message_type = 'danger';
        }
    }

    if ($action === 'delete_contact') {
        $contact_id = $_POST['contact_id'];
        $stmt = $pdo->prepare("DELETE FROM contacts WHERE id = ? AND managed_by_user_id = ?");
        $stmt->execute([$contact_id, $currentUserId]);
        $message = "Contatto eliminato.";
        $message_type = 'warning';
    }
}

$stmt = $pdo->prepare("SELECT c.*, u.id as is_user FROM contacts c LEFT JOIN users u ON c.linked_user_id = u.id WHERE c.managed_by_user_id = ? ORDER BY c.name ASC");
$stmt->execute([$currentUserId]);
$contacts = $stmt->fetchAll();
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo $lang['contacts_title']; ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">

    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <div class="card shadow-sm mb-4">
        <div class="card-header d-flex justify-content-between align-items-center">
            <h5 class="mb-0"><i class="fas fa-address-book"></i> <?php echo $lang['contacts_my_book']; ?></h5>
            <div>
                <button class="btn btn-sm btn-outline-secondary" disabled title="<?php echo $lang['contacts_dev_tooltip']; ?>"><i class="fab fa-google"></i> <?php echo $lang['contacts_import_google']; ?></button>
                <button class="btn btn-sm btn-outline-success" disabled title="<?php echo $lang['contacts_dev_tooltip']; ?>"><i class="fas fa-file-excel"></i> <?php echo $lang['contacts_import_excel']; ?></button>
            </div>
        </div>
        <div class="card-body">
            <p class="text-muted"><?php echo $lang['contacts_desc']; ?></p>
            <form method="POST" class="mb-4 p-3 border rounded">
                <input type="hidden" name="action" value="add_contact">
                <div class="row align-items-end">
                    <div class="col-md-3"><label><?php echo $lang['contacts_name']; ?>*</label><input type="text" name="name" class="form-control" required></div>
                    <div class="col-md-3"><label><?php echo $lang['contacts_email']; ?>*</label><input type="email" name="email" class="form-control" required></div>
                    <div class="col-md-2"><label><?php echo $lang['contacts_phone']; ?></label><input type="text" name="phone" class="form-control"></div>
                    <div class="col-md-2"><label><?php echo $lang['contacts_company']; ?></label><input type="text" name="company" class="form-control"></div>
                    <div class="col-md-2"><button type="submit" class="btn btn-primary w-100"><?php echo $lang['contacts_btn_add']; ?></button></div>
                </div>
            </form>

            <div class="table-responsive">
                <table class="table table-hover">
                    <thead class="table-light">
                        <tr>
                            <th><?php echo $lang['contacts_name']; ?></th>
                            <th><?php echo $lang['contacts_email']; ?></th>
                            <th><?php echo $lang['contacts_phone']; ?></th>
                            <th><?php echo $lang['contacts_company']; ?></th>
                            <th><?php echo $lang['contacts_user_status']; ?></th>
                            <th><?php echo $lang['table_actions']; ?></th>
                        </tr>
                    </thead>
                    <tbody>
                        <?php foreach ($contacts as $contact): ?>
                        <tr>
                            <td><?php echo htmlspecialchars($contact['name']); ?></td>
                            <td><?php echo htmlspecialchars($contact['email']); ?></td>
                            <td><?php echo htmlspecialchars($contact['phone']); ?></td>
                            <td><?php echo htmlspecialchars($contact['company']); ?></td>
                            <td>
                                <?php if ($contact['is_user']): ?>
                                    <span class="badge bg-success"><?php echo $lang['contacts_active_user']; ?></span>
                                <?php else: ?>
                                    <span class="badge bg-secondary"><?php echo $lang['contacts_non_user']; ?></span>
                                <?php endif; ?>
                            </td>
                            <td>
                                <?php if (!$contact['is_user']): ?>
                                    <button class="btn btn-sm btn-outline-primary" disabled title="<?php echo $lang['contacts_dev_tooltip']; ?>"><?php echo $lang['contacts_create_account']; ?></button>
                                <?php endif; ?>
                                <form method="POST" style="display:inline;" onsubmit="return confirm('<?php echo $lang['contacts_delete_confirm']; ?>');">
                                    <input type="hidden" name="action" value="delete_contact">
                                    <input type="hidden" name="contact_id" value="<?php echo $contact['id']; ?>">
                                    <button type="submit" class="btn btn-sm btn-outline-danger" title="Elimina Contatto"><i class="fas fa-trash"></i></button>
                                </form>
                            </td>
                        </tr>
                        <?php endforeach; ?>
                    </tbody>
                </table>
            </div>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>