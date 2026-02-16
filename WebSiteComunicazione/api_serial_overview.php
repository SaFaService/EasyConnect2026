<?php
session_start();
require 'config.php';
header('Content-Type: application/json');

// API overview seriali: pensata per polling UI (serials.php).
// Risponde a utenti autenticati con ruolo admin/builder/maintainer.
if (!isset($_SESSION['user_id'])) {
    http_response_code(401);
    echo json_encode(['status' => 'error', 'message' => 'Non autorizzato']);
    exit;
}
if (!isset($_SESSION['user_role']) || !in_array($_SESSION['user_role'], ['admin', 'builder', 'maintainer'], true)) {
    http_response_code(403);
    echo json_encode(['status' => 'error', 'message' => 'Permessi insufficienti']);
    exit;
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    $input = [];
}

if (($input['action'] ?? '') !== 'overview') {
    echo json_encode(['status' => 'error', 'message' => 'Azione non valida']);
    exit;
}

/**
 * Verifica sicura esistenza tabella nello schema attivo.
 */
function tableExists(PDO $pdo, string $tableName): bool {
    $stmt = $pdo->prepare(
        "SELECT 1
         FROM information_schema.tables
         WHERE table_schema = DATABASE()
           AND table_name = ?
         LIMIT 1"
    );
    $stmt->execute([$tableName]);
    return (bool)$stmt->fetchColumn();
}

function columnExists(PDO $pdo, string $tableName, string $columnName): bool {
    $stmt = $pdo->prepare(
        "SELECT 1
         FROM information_schema.columns
         WHERE table_schema = DATABASE()
           AND table_name = ?
           AND column_name = ?
         LIMIT 1"
    );
    $stmt->execute([$tableName, $columnName]);
    return (bool)$stmt->fetchColumn();
}

try {
    $warnings = [];
    $serials = [];
    $audit = [];

    if (tableExists($pdo, 'device_serials')) {
        $hasReason = columnExists($pdo, 'device_serials', 'status_reason_code');
        $hasReplaced = columnExists($pdo, 'device_serials', 'replaced_by_serial');

        $reasonSelect = $hasReason ? "ds.status_reason_code AS status_reason_code" : "'' AS status_reason_code";
        $replacedSelect = $hasReplaced ? "ds.replaced_by_serial AS replaced_by_serial" : "'' AS replaced_by_serial";

        // Lista sintetica ultimi seriali con riferimento master, se presente.
        $sql = "
            SELECT
                ds.serial_number,
                ds.product_type_code,
                ds.status,
                $reasonSelect,
                $replacedSelect,
                ds.serial_locked,
                ds.assigned_master_id,
                ds.created_at,
                m.nickname AS master_nickname,
                m.serial_number AS master_serial
            FROM device_serials ds
            LEFT JOIN masters m ON m.id = ds.assigned_master_id
            ORDER BY ds.id DESC
            LIMIT 20
        ";
        $stmt = $pdo->query($sql);

        foreach ($stmt->fetchAll() as $row) {
            $masterLabel = '';
            if (!empty($row['assigned_master_id'])) {
                $masterLabel = '#' . (int)$row['assigned_master_id'];
                if (!empty($row['master_nickname'])) {
                    $masterLabel .= ' - ' . $row['master_nickname'];
                }
                if (!empty($row['master_serial'])) {
                    $masterLabel .= ' (' . $row['master_serial'] . ')';
                }
            }

            $serials[] = [
                'serial_number' => (string)$row['serial_number'],
                'product_type_code' => (string)$row['product_type_code'],
                'status' => (string)$row['status'],
                'status_reason_code' => isset($row['status_reason_code']) ? (string)$row['status_reason_code'] : '',
                'replaced_by_serial' => isset($row['replaced_by_serial']) ? (string)$row['replaced_by_serial'] : '',
                'serial_locked' => (int)$row['serial_locked'],
                'assigned_master_id' => $row['assigned_master_id'] !== null ? (int)$row['assigned_master_id'] : null,
                'master_label' => $masterLabel,
                'created_at' => (string)$row['created_at'],
            ];
        }
    } else {
        $warnings[] = 'Tabella device_serials non disponibile';
    }

    if (tableExists($pdo, 'serial_audit_logs')) {
        // Ultime azioni audit con utente che ha effettuato l'operazione.
        $stmt = $pdo->query(
            "SELECT
                sal.created_at,
                sal.action,
                sal.serial_number,
                sal.master_id,
                sal.details,
                u.email AS actor_email
             FROM serial_audit_logs sal
             LEFT JOIN users u ON u.id = sal.actor_user_id
             ORDER BY sal.id DESC
             LIMIT 20"
        );

        foreach ($stmt->fetchAll() as $row) {
            $details = (string)($row['details'] ?? '');
            $detailsShort = substr($details, 0, 80);
            if (strlen($details) > 80) {
                $detailsShort .= '...';
            }

            $audit[] = [
                'created_at' => (string)$row['created_at'],
                'action' => (string)$row['action'],
                'serial_number' => (string)$row['serial_number'],
                'master_id' => $row['master_id'] !== null ? (int)$row['master_id'] : null,
                'actor_email' => (string)($row['actor_email'] ?? ''),
                'details_short' => $detailsShort,
                'details' => $details,
            ];
        }
    } else {
        $warnings[] = 'Tabella serial_audit_logs non disponibile';
    }

    echo json_encode([
        'status' => 'ok',
        'serials' => $serials,
        'audit' => $audit,
        'warnings' => $warnings,
        'generated_at' => date('Y-m-d H:i:s')
    ]);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['status' => 'error', 'message' => 'DB Error: ' . $e->getMessage()]);
}
?>
