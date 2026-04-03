<?php
require 'config.php';
header('Content-Type: application/json');

// 1. Ricezione Dati (JSON o POST)
$input = json_decode(file_get_contents('php://input'), true);
$apiKey = $input['api_key'] ?? $_POST['api_key'] ?? '';

// Funzione helper per convertire link Google Drive in link diretti
function convertDriveLink($url) {
    // Cerca l'ID del file in vari formati di link di Google Drive (es. /d/FILE_ID/ o ?id=FILE_ID)
    if (preg_match('/\/d\/([a-zA-Z0-9_-]+)/', $url, $matches) || preg_match('/id=([a-zA-Z0-9_-]+)/', $url, $matches)) {
        if (isset($matches[1])) {
            return "https://drive.google.com/uc?export=download&id=" . $matches[1];
        }
    }
    return $url; // Se non corrisponde o è già un link diretto, lo ritorna così com'è
}

$currentVer = $input['version'] ?? $_POST['version'] ?? '0.0.0';

if (empty($apiKey)) {
    echo json_encode(['status' => 'error', 'message' => 'API Key mancante']);
    exit;
}

try {
    // 2. Identifica il Master tramite API Key
    $stmt = $pdo->prepare("SELECT * FROM masters WHERE api_key = ? AND deleted_at IS NULL");
    $stmt->execute([$apiKey]);
    $master = $stmt->fetch();

    if (!$master) {
        echo json_encode(['status' => 'error', 'message' => 'Master non trovato']);
        exit;
    }

    // --- NUOVA LOGICA: PRIORITA' AGLI SLAVE ---
    // 3. Controlla se c'è una richiesta di aggiornamento per uno SLAVE
    if (!empty($master['slave_update_request_sn'])) {
        // STOP LOOP: se è in uno stato intermedio, non reinviare il comando.
        // Evita ri-trigger durante una procedura già partita.
        // 'Pending' NON va considerato busy: e' proprio lo stato che deve generare il primo comando slave_update_ready.
        $busySlaveStates = ['InProgress', 'Downloading', 'Downloaded', 'Handshake', 'Sending data', 'Uploading', 'Finalizing'];
        if (in_array($master['slave_ota_status'], $busySlaveStates, true)) {
            echo json_encode(['status' => 'no_update', 'message' => 'Aggiornamento slave già in corso. Stato: ' . $master['slave_ota_status']]);
            exit;
        }

        // Recupera l'ultimo firmware ATTIVO per 'slave_pressure'
        $stmtFw = $pdo->prepare("SELECT version, download_url FROM firmware_releases WHERE device_type = 'slave_pressure' AND is_active = 1 ORDER BY id DESC LIMIT 1");
        $stmtFw->execute();
        $fw = $stmtFw->fetch();

        if ($fw) {
            // Comunica al master che deve avviare un aggiornamento per uno slave
            // Aggiorna SUBITO lo stato a 'InProgress' per evitare che al prossimo check (es. dopo reboot) riparta da solo.
            $pdo->prepare("UPDATE masters SET slave_ota_status = 'InProgress' WHERE id = ?")->execute([$master['id']]);

            echo json_encode([
                'status' => 'slave_update_ready',
                'target_device_type' => 'slave_pressure',
                'target_slave_sn' => $master['slave_update_request_sn'],
                'new_version' => $fw['version'],
                'url' => convertDriveLink($fw['download_url'])
            ]);
        } else {
            echo json_encode(['status' => 'no_update', 'message' => 'Nessun firmware attivo trovato per lo slave.']);
        }
        exit;
    }

    // 3. Controlla se è stato richiesto un aggiornamento dall'utente
    if ($master['update_requested'] == 1) {
        // Recupera l'ultimo firmware ATTIVO per 'master'
        $stmtFw = $pdo->prepare("SELECT version, download_url FROM firmware_releases WHERE device_type = 'master' AND is_active = 1 ORDER BY id DESC LIMIT 1");
        $stmtFw->execute();
        $fw = $stmtFw->fetch();

        if ($fw) {
            // Imposta lo stato su InProgress e resetta la richiesta
            $pdo->prepare("UPDATE masters SET update_requested = 0, ota_status = 'InProgress' WHERE id = ?")->execute([$master['id']]);

            echo json_encode([
                'status' => 'update_ready',
                'target_device_type' => 'master',
                'new_version' => $fw['version'],
                'url' => convertDriveLink($fw['download_url'])
            ]);
            exit;
        } else {
            echo json_encode(['status' => 'no_update', 'message' => 'Nessun firmware attivo trovato nel sistema']);
            exit;
        }
    }

    // 4. Coda comandi remoti (es. configurazione scheda pressione)
    // Viene valutata solo quando non ci sono OTA pending.
    if ($master['update_requested'] != 1 && empty($master['slave_update_request_sn'])) {
        $tableCheck = $pdo->prepare("
            SELECT 1
            FROM information_schema.tables
            WHERE table_schema = DATABASE()
              AND table_name = 'device_commands'
            LIMIT 1
        ");
        $tableCheck->execute();
        $hasCmdTable = (bool)$tableCheck->fetchColumn();

        if ($hasCmdTable) {
            $pdo->beginTransaction();
            $cmdStmt = $pdo->prepare("
                SELECT *
                FROM device_commands
                WHERE master_id = ?
                  AND status = 'pending'
                ORDER BY id ASC
                LIMIT 1
                FOR UPDATE
            ");
            $cmdStmt->execute([$master['id']]);
            $cmd = $cmdStmt->fetch();

            if ($cmd) {
                $updCmd = $pdo->prepare("
                    UPDATE device_commands
                    SET status = 'sent',
                        sent_at = NOW(),
                        attempt_count = attempt_count + 1
                    WHERE id = ?
                ");
                $updCmd->execute([(int)$cmd['id']]);
                $pdo->commit();

                $payload = json_decode((string)($cmd['payload_json'] ?? '{}'), true);
                if (!is_array($payload)) {
                    $payload = [];
                }

                echo json_encode([
                    'status' => 'command_ready',
                    'command_id' => (int)$cmd['id'],
                    'command_type' => (string)$cmd['command_type'],
                    'target_slave_sn' => (string)$cmd['target_serial'],
                    'payload' => $payload
                ]);
                exit;
            }
            $pdo->commit();
        }
    }

    echo json_encode(['status' => 'no_update', 'message' => 'Nessun aggiornamento richiesto']);
    exit;

} catch (PDOException $e) {
    if ($pdo->inTransaction()) {
        $pdo->rollBack();
    }
    echo json_encode(['status' => 'error', 'message' => 'DB Error: ' . $e->getMessage()]);
}
?>
