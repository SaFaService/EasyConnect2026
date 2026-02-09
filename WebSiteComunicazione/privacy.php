<?php
session_start();
require 'lang.php';
?>
<!DOCTYPE html>
<html lang="<?php echo $_SESSION['lang']; ?>">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title><?php echo $lang['privacy_title']; ?> - Antralux Cloud</title>
    <link rel="icon" type="image/x-icon" href="assets/img/Icona.ico">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body class="bg-light">

<div class="container mt-5 mb-5">
    <div class="card shadow-sm">
        <div class="card-header bg-dark text-white">
            <h4 class="mb-0"><?php echo $lang['privacy_title']; ?></h4>
        </div>
        <div class="card-body">
            <h5><?php echo $lang['privacy_controller_title']; ?></h5>
            <p><?php echo $lang['privacy_controller_text']; ?></p>

            <hr>

            <h5><?php echo $lang['privacy_data_title']; ?></h5>
            <p><?php echo $lang['privacy_data_text']; ?></p>
            <ul>
                <li><strong><?php echo $lang['privacy_data_nav']; ?>:</strong> <?php echo $lang['privacy_data_nav_desc']; ?></li>
                <li><strong><?php echo $lang['privacy_data_user']; ?>:</strong> <?php echo $lang['privacy_data_user_desc']; ?></li>
                <li><strong><?php echo $lang['privacy_data_plant']; ?>:</strong> <?php echo $lang['privacy_data_plant_desc']; ?></li>
            </ul>

            <hr>

            <h5><?php echo $lang['privacy_cookie_title']; ?></h5>
            <p><?php echo $lang['privacy_cookie_text']; ?></p>
            <ul>
                <li><?php echo $lang['privacy_cookie_list_1']; ?></li>
                <li><?php echo $lang['privacy_cookie_list_2']; ?></li>
            </ul>
            <p><?php echo $lang['privacy_cookie_marketing']; ?></p>

            <hr>

            <h5><?php echo $lang['privacy_purpose_title']; ?></h5>
            <p><?php echo $lang['privacy_purpose_text']; ?></p>
        </div>
        <div class="card-footer text-center">
            <button onclick="window.close()" class="btn btn-secondary"><?php echo $lang['btn_close']; ?></button>
        </div>
    </div>
</div>

</body>
</html>