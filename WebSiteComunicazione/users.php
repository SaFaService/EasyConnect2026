<?php
session_start();
require 'config.php';

// Includi il gestore della lingua
require 'lang.php';

// Protezione: Solo Admin può gestire gli utenti
if (!isset($_SESSION['user_id']) || $_SESSION['user_role'] !== 'admin') {
    header("Location: login.php");
    exit;
}

$message = '';
$message_type = '';
$currentUserId = $_SESSION['user_id'];

// Gestione Richieste POST
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';

    // Aggiungi Utente
    if ($action === 'add_user') {
        $email = trim($_POST['email']);
        $password = $_POST['password'];
        $role = $_POST['role'];

        if (!empty($email) && !empty($password) && !empty($role)) {
            // Verifica se l'email esiste già
            $check = $pdo->prepare("SELECT id FROM users WHERE email = ?");
            $check->execute([$email]);
            
            if ($check->rowCount() > 0) {
                $message = "Esiste già un utente con questa email.";
                $message_type = 'danger';
            } else {
                // Hash della password per sicurezza
                $passwordHash = password_hash($password, PASSWORD_DEFAULT);
                
                // Inserimento con Data di Creazione
                $stmt = $pdo->prepare("INSERT INTO users (email, password_hash, role) VALUES (?, ?, ?)");
                try {
                    if ($stmt->execute([$email, $passwordHash, $role])) {
                        $message = "Utente creato con successo.";
                        $message_type = 'success';
                    } else {
                        $message = "Errore durante la creazione dell'utente.";
                        $message_type = 'danger';
                    }
                } catch (PDOException $e) {
                    $message = "Errore Database: " . $e->getMessage();
                    $message_type = 'danger';
                }
            }
        } else {
            $message = "Tutti i campi sono obbligatori.";
            $message_type = 'danger';
        }
    }

    // Elimina Utente
    if ($action === 'delete_user') {
        $id_to_delete = $_POST['user_id'];

        if ($id_to_delete == $currentUserId) {
            $message = "Non puoi eliminare il tuo stesso account.";
            $message_type = 'danger';
        } else {
            // Nota: Se l'utente è collegato a dei master (come owner o creator), 
            // la foreign key potrebbe bloccare l'eliminazione o settare a NULL a seconda del DB.
            // Qui assumiamo una cancellazione logica o fisica diretta.
            $stmt = $pdo->prepare("DELETE FROM users WHERE id = ?");
            try {
                $stmt->execute([$id_to_delete]);
                $message = "Utente eliminato.";
                $message_type = 'warning';
            } catch (PDOException $e) {
                $message = "Impossibile eliminare: l'utente è associato a degli impianti.";
                $message_type = 'danger';
            }
        }
    }
}

// Recupera lista utenti
$stmtUsers = $pdo->query("SELECT * FROM users ORDER BY created_at DESC, email ASC");
$users = $stmtUsers->fetchAll();
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo $lang['users_title']; ?> - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/lipis/flag-icons@6/css/flag-icons.min.css"/>
</head>
<body class="d-flex flex-column min-vh-100 bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4 flex-grow-1">
    
    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?> alert-dismissible fade show" role="alert">
            <?php echo $message; ?>
            <button type="button" class="btn-close" data-bs-dismiss="alert" aria-label="Close"></button>
        </div>
    <?php endif; ?>

    <div class="row">
        <!-- Colonna Aggiunta Utente -->
        <div class="col-md-4 mb-4">
            <div class="card shadow-sm">
                <div class="card-header bg-primary text-white">
                    <h5 class="mb-0"><i class="fas fa-user-plus"></i> <?php echo $lang['users_new']; ?></h5>
                </div>
                <div class="card-body">
                    <form method="POST">
                        <input type="hidden" name="action" value="add_user">
                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['users_email']; ?></label>
                            <input type="email" name="email" class="form-control" required placeholder="user@example.com">
                        </div>
                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['users_password']; ?></label>
                            <input type="password" name="password" class="form-control" required placeholder="Password sicura">
                        </div>
                        <div class="mb-3">
                            <label class="form-label"><?php echo $lang['users_role']; ?></label>
                            <select name="role" class="form-select" required>
                                <option value="client"><?php echo $lang['users_role_client']; ?></option>
                                <option value="maintainer"><?php echo $lang['users_role_maintainer']; ?></option>
                                <option value="builder"><?php echo $lang['users_role_builder']; ?></option>
                                <option value="admin"><?php echo $lang['users_role_admin']; ?></option>
                            </select>
                        </div>
                        <button type="submit" class="btn btn-primary w-100"><?php echo $lang['users_btn_create']; ?></button>
                    </form>
                </div>
            </div>
        </div>

        <!-- Colonna Lista Utenti -->
        <div class="col-md-8">
            <div class="card shadow-sm">
                <div class="card-header">
                    <h5 class="mb-0"><i class="fas fa-users"></i> <?php echo $lang['users_list_title']; ?></h5>
                </div>
                <div class="card-body p-0">
                    <div class="table-responsive">
                        <table class="table table-hover mb-0 align-middle">
                            <thead class="table-light">
                                <tr>
                                    <th><?php echo $lang['users_col_email']; ?></th>
                                    <th><?php echo $lang['users_col_role']; ?></th>
                                    <th><?php echo $lang['users_col_created']; ?></th>
                                    <th class="text-end"><?php echo $lang['users_col_actions']; ?></th>
                                </tr>
                            </thead>
                            <tbody>
                                <?php foreach ($users as $u): ?>
                                <tr>
                                    <td>
                                        <?php echo htmlspecialchars($u['email']); ?>
                                        <?php if($u['id'] == $currentUserId) echo '<span class="badge bg-info ms-1">' . $lang['users_you'] . '</span>'; ?>
                                    </td>
                                    <td>
                                        <?php 
                                            $badgeColor = match($u['role']) {
                                                'admin' => 'bg-danger',
                                                'builder' => 'bg-warning text-dark',
                                                'maintainer' => 'bg-success',
                                                default => 'bg-secondary'
                                            };
                                        ?>
                                        <span class="badge <?php echo $badgeColor; ?>"><?php echo ucfirst($u['role']); ?></span>
                                    </td>
                                    <td><?php echo $u['created_at'] ? date('d/m/Y', strtotime($u['created_at'])) : '-'; ?></td>
                                    <td class="text-end">
                                        <?php if($u['id'] != $currentUserId): ?>
                                            <form method="POST" onsubmit="return confirm('<?php echo $lang['users_delete_confirm']; ?>');" style="display:inline;">
                                                <input type="hidden" name="action" value="delete_user">
                                                <input type="hidden" name="user_id" value="<?php echo $u['id']; ?>">
                                                <button type="submit" class="btn btn-sm btn-outline-danger"><i class="fas fa-trash-alt"></i></button>
                                            </form>
                                        <?php endif; ?>
                                    </td>
                                </tr>
                                <?php endforeach; ?>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>

<?php require 'footer.php'; ?>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>