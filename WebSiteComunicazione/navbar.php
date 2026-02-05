<?php
// navbar.php - Barra di navigazione riutilizzabile
?>
<nav class="navbar navbar-expand-lg navbar-dark bg-dark">
  <div class="container-fluid">
    <a class="navbar-brand" href="index.php">
        <img src="assets/img/AntraluxLogo.png" height="30" alt="Antralux" onerror="this.style.display='none'"> Sistema di Controllo
    </a>
    <div class="d-flex align-items-center">
        <a href="index.php" class="btn btn-outline-light me-2"><i class="fas fa-server"></i> Dashboard</a>
        <?php if ($_SESSION['user_role'] !== 'client'): ?>
            <a href="contacts.php" class="btn btn-outline-light me-2"><i class="fas fa-address-book"></i> Rubrica</a>
        <?php endif; ?>
        <a href="settings.php" class="btn btn-outline-light me-2"><i class="fas fa-cogs"></i> Gestione Impianti</a>
        <?php if ($_SESSION['user_role'] === 'admin' || $_SESSION['user_role'] === 'builder'): ?>
            <a href="firmware.php" class="btn btn-outline-warning me-2"><i class="fas fa-cloud-upload-alt"></i> Gestione Firmware</a>
        <?php endif; ?>
        
        <div class="dropdown">
            <a href="#" class="btn btn-outline-primary ms-2 dropdown-toggle" role="button" id="dropdownUser" data-bs-toggle="dropdown" aria-expanded="false" title="Profilo Utente">
                <i class="fas fa-user"></i>
            </a>
            <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="dropdownUser">
                <li><a class="dropdown-item" href="profile.php"><i class="fas fa-user-cog me-2"></i>Mio Profilo</a></li>
                <li><hr class="dropdown-divider"></li>
                <li><a class="dropdown-item text-danger" href="logout.php"><i class="fas fa-sign-out-alt me-2"></i>Esci</a></li>
            </ul>
        </div>
    </div>
  </div>
</nav>