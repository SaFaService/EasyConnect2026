<?php
// footer.php - Footer riutilizzabile
require_once __DIR__ . '/portal_meta.php';
$portalVersion = defined('ANTRALUX_PORTAL_VERSION') ? ANTRALUX_PORTAL_VERSION : 'n/a';
?>
<footer class="bg-dark text-white text-center py-3 mt-auto">
    <div class="container">
        <p class="mb-0 small">
            <?php echo $lang['footer_text']; ?> 
            <a href="contact_form.php" class="text-white">info@safaservice.com</a>
            <span class="badge bg-secondary ms-2">
                <?php echo ($lang['footer_portal_version'] ?? 'Portal Version'); ?>: <?php echo htmlspecialchars($portalVersion); ?>
            </span>
        </p>
    </div>
</footer>
