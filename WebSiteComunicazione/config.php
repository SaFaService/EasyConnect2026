<?php
// config.php - Connessione al Database

$host = 'localhost'; // Su SupportHost solitamente è localhost
$db   = 'antralux_iot'; // O il nome esatto del DB che hai creato
$user = 'antralux_easyconnect'; // L'utente che hai creato
$pass = 'Tommaso2013@!';   // La password dell'utente DB
$charset = 'utf8mb4';

$dsn = "mysql:host=$host;dbname=$db;charset=$charset";
$options = [
    PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    PDO::ATTR_EMULATE_PREPARES   => false,
];

try {
    $pdo = new PDO($dsn, $user, $pass, $options);
} catch (\PDOException $e) {
    // In produzione non mostrare l'errore completo per sicurezza
    // PER DEBUG: Mostriamo l'errore reale per capire il problema
    die("Errore Connessione DB: " . $e->getMessage());
}
?>