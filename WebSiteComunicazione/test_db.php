<?php
// Abilita la visualizzazione di tutti gli errori
ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);

echo "<h1>Test Connessione Database</h1>";

// Includiamo il file di configurazione
require 'config.php';

// Se siamo qui, il require non ha fallito (altrimenti vedresti l'errore del die in config.php)
if (isset($pdo)) {
    echo "<h2 style='color:green'>CONNESSO CON SUCCESSO!</h2>";
    echo "<p>Database: <b>$db</b></p>";
    echo "<p>Utente: <b>$user</b></p>";
    echo "<p>Host: <b>$host</b></p>";
} else {
    echo "<h2 style='color:red'>ERRORE: Variabile \$pdo non definita.</h2>";
}
?>