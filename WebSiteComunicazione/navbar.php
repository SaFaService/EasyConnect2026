<?php
// navbar.php - Barra di navigazione riutilizzabile
?>
<nav class="navbar navbar-expand-lg navbar-dark bg-dark">
  <div class="container-fluid">
    <a class="navbar-brand" href="index.php">
        <img src="assets/img/AntraluxCloud.png" height="55" alt="Antralux Cloud" class="d-inline-block align-text-top">
    </a>
    <div class="d-flex align-items-center">
        <a href="index.php" class="btn btn-outline-light me-2"><i class="fas fa-server"></i> <?php echo $lang['navbar_dashboard']; ?></a>
        <?php if ($_SESSION['user_role'] !== 'client'): ?>
            <a href="contacts.php" class="btn btn-outline-light me-2"><i class="fas fa-address-book"></i> <?php echo $lang['navbar_address_book']; ?></a>
        <?php endif; ?>
        <a href="settings.php" class="btn btn-outline-light me-2"><i class="fas fa-cogs"></i> <?php echo $lang['navbar_plant_management']; ?></a>
        <?php if ($_SESSION['user_role'] === 'admin'): ?>
            <a href="users.php" class="btn btn-outline-light me-2"><i class="fas fa-users"></i> <?php echo $lang['navbar_user_management']; ?></a>
        <?php endif; ?>
        <?php if ($_SESSION['user_role'] === 'admin' || $_SESSION['user_role'] === 'builder'): ?>
            <a href="firmware.php" class="btn btn-outline-light me-2"><i class="fas fa-cloud-upload-alt"></i> <?php echo $lang['navbar_firmware_management']; ?></a>
        <?php endif; ?>
        
        <div class="dropdown">
            <a href="#" class="btn btn-outline-light ms-2 dropdown-toggle" role="button" id="dropdownLang" data-bs-toggle="dropdown" aria-expanded="false" title="<?php echo $lang['navbar_language']; ?>">
                <i class="fas fa-globe"></i>
            </a>
            <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="dropdownLang">
                <li><a class="dropdown-item <?php if($_SESSION['lang'] == 'it') echo 'active'; ?>" href="?lang=it"><span class="fi fi-it me-2"></span><?php echo $lang['navbar_italian']; ?></a></li>
                <li><a class="dropdown-item <?php if($_SESSION['lang'] == 'en') echo 'active'; ?>" href="?lang=en"><span class="fi fi-gb me-2"></span><?php echo $lang['navbar_english']; ?></a></li>
            </ul>
        </div>

        <div class="dropdown">
            <a href="#" class="btn btn-outline-primary ms-2 dropdown-toggle" role="button" id="dropdownUser" data-bs-toggle="dropdown" aria-expanded="false" title="<?php echo $lang['navbar_user_profile']; ?>">
                <i class="fas fa-user"></i>
            </a>
            <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="dropdownUser">
                <li><a class="dropdown-item" href="profile.php"><i class="fas fa-user-cog me-2"></i><?php echo $lang['navbar_my_profile']; ?></a></li>
                <li><hr class="dropdown-divider"></li>
                <li><a class="dropdown-item text-danger" href="logout.php"><i class="fas fa-sign-out-alt me-2"></i><?php echo $lang['navbar_logout']; ?></a></li>
            </ul>
        </div>
    </div>
  </div>
</nav>