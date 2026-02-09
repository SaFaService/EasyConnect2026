<?php
// lang.php - Gestore della lingua

// Se la lingua non è impostata in sessione, usa l'italiano di default
if (!isset($_SESSION['lang'])) {
    $_SESSION['lang'] = 'it';
}

// Se l'utente ha cliccato su un link per cambiare lingua (es. ?lang=en)
if (isset($_GET['lang'])) {
    $new_lang = $_GET['lang'];
    // Controlla che la lingua sia tra quelle supportate
    if ($new_lang == 'it' || $new_lang == 'en') {
        $_SESSION['lang'] = $new_lang;
    }
    // Rimuovi il parametro 'lang' dall'URL per pulizia e ricarica la pagina
    $redirect_url = strtok($_SERVER["REQUEST_URI"], '?');
    header("Location: " . $redirect_url);
    exit;
}

// Carica il file di lingua corretto
// MODIFICA: Cerca sia nella cartella 'lang/' che nella root per evitare errori 500
$lang_file_subdir = __DIR__ . '/lang/' . $_SESSION['lang'] . '.php';
$lang_file_root = __DIR__ . '/' . $_SESSION['lang'] . '.php';

if (file_exists($lang_file_subdir)) {
    require_once($lang_file_subdir);
} elseif (file_exists($lang_file_root)) {
    require_once($lang_file_root);
} else {
    // Fallback: prova a caricare italiano dalla root se non trova altro
    if (file_exists(__DIR__ . '/it.php')) require_once(__DIR__ . '/it.php');
}