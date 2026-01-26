<?php
session_start();
require 'config.php';

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
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Rubrica Contatti - Antralux</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body class="bg-light">

<?php require 'navbar.php'; ?>

<div class="container mt-4">

    <?php if ($message): ?>
        <div class="alert alert-<?php echo $message_type; ?>"><?php echo $message; ?></div>
    <?php endif; ?>

    <div class="card shadow-sm mb-4">
        <div class="card-header d-flex justify-content-between align-items-center">
            <h5 class="mb-0"><i class="fas fa-address-book"></i> La Mia Rubrica</h5>
            <div>
                <button class="btn btn-sm btn-outline-secondary" disabled title="Funzione in sviluppo"><i class="fab fa-google"></i> Importa da Google</button>
                <button class="btn btn-sm btn-outline-success" disabled title="Funzione in sviluppo"><i class="fas fa-file-excel"></i> Importa da Excel</button>
            </div>
        </div>
        <div class="card-body">
            <p class="text-muted">Aggiungi qui i tuoi contatti (clienti, manutentori) per poterli poi assegnare agli impianti.</p>
            <form method="POST" class="mb-4 p-3 border rounded">
                <input type="hidden" name="action" value="add_contact">
                <div class="row align-items-end">
                    <div class="col-md-3"><label>Nome*</label><input type="text" name="name" class="form-control" required></div>
                    <div class="col-md-3"><label>Email*</label><input type="email" name="email" class="form-control" required></div>
                    <div class="col-md-2"><label>Telefono</label><input type="text" name="phone" class="form-control"></div>
                    <div class="col-md-2"><label>Azienda</label><input type="text" name="company" class="form-control"></div>
                    <div class="col-md-2"><button type="submit" class="btn btn-primary w-100">Aggiungi</button></div>
                </div>
            </form>

            <div class="table-responsive">
                <table class="table table-hover">
                    <thead class="table-light">
                        <tr>
                            <th>Nome</th>
                            <th>Email</th>
                            <th>Telefono</th>
                            <th>Azienda</th>
                            <th>Stato Utente</th>
                            <th>Azioni</th>
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
                                    <span class="badge bg-success">Utente Attivo</span>
                                <?php else: ?>
                                    <span class="badge bg-secondary">Non Utente</span>
                                <?php endif; ?>
                            </td>
                            <td>
                                <?php if (!$contact['is_user']): ?>
                                    <button class="btn btn-sm btn-outline-primary" disabled title="Funzione in sviluppo">Crea Account</button>
                                <?php endif; ?>
                                <form method="POST" style="display:inline;" onsubmit="return confirm('Eliminare questo contatto?');">
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

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>