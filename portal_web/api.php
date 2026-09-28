<?php
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');

$dataFile = __DIR__ . '/storage.json';

// Algoritmo de Derivação de Chave de Resgate a partir do MAC
// Combina o MAC físico do ESP32 com um Salt secreto e gera um PIN numérico de 6 dígitos
define('SECRET_SALT', 'COFRE_POLITO_SECURE_2026');

function calculateEmergencyCode($mac) {
    $cleanMac = strtoupper(str_replace([':', '-'], '', $mac));
    $hash = hash('sha256', $cleanMac . SECRET_SALT);
    $num = hexdec(substr($hash, 0, 8));
    return str_pad($num % 1000000, 6, '0', STR_PAD_LEFT);
}

// MAC Simulado do ESP32-C3
$simulatedMac = '7C:DF:A1:34:B8:2E';
$calculatedRescuePin = calculateEmergencyCode($simulatedMac);

// Inicializa dados padrão
if (!file_exists($dataFile)) {
    $defaultData = [
        'isConfigured' => true,
        'isLocked' => true,
        'solenoidPulseMs' => 800,
        'batteryV' => 8.85,
        'sleepTimeout' => 180,
        'deviceMac' => $simulatedMac,
        'users' => [
            [
                'id' => 1,
                'name' => 'Fernando (Mestre)',
                'pin' => '123456',
                'role' => 'admin',
                'created_at' => date('Y-m-d')
            ],
            [
                'id' => 2,
                'name' => 'Família / Reserva',
                'pin' => '2580',
                'role' => 'user',
                'created_at' => date('Y-m-d')
            ]
        ],
        'logs' => [
            [
                'id' => 1,
                'time' => date('d/m H:i:s'),
                'user' => 'Sistema',
                'method' => 'Sistema',
                'action' => 'Inicialização do Cofre (Chave MAC Segura)',
                'success' => true
            ]
        ]
    ];
    file_put_contents($dataFile, json_encode($defaultData, JSON_PRETTY_PRINT));
}

$state = json_decode(file_get_contents($dataFile), true);
$action = $_GET['action'] ?? '';

function saveState($state, $file) {
    file_put_contents($file, json_encode($state, JSON_PRETTY_PRINT));
}

switch ($action) {
    case 'status':
        $sanitizedUsers = array_map(function($u) {
            return [
                'id' => $u['id'],
                'name' => $u['name'],
                'role' => $u['role'],
                'created_at' => $u['created_at']
            ];
        }, $state['users']);

        // IMPORTANTE: Por segurança, NUNCA expor a chave de resgate na API!
        echo json_encode([
            'status' => 'ok',
            'isConfigured' => true,
            'isLocked' => $state['isLocked'],
            'batteryV' => $state['batteryV'],
            'pulseMs' => $state['solenoidPulseMs'],
            'sleepTimeout' => $state['sleepTimeout'],
            'deviceMac' => $state['deviceMac'] ?? $simulatedMac,
            'users' => $sanitizedUsers,
            'logs' => array_slice(array_reverse($state['logs']), 0, 15)
        ]);
        break;

    case 'unlock':
        $pin = $_POST['pin'] ?? $_GET['pin'] ?? '';
        
        if (empty($pin)) {
            echo json_encode(['status' => 'error', 'message' => 'Senha não informada!']);
            exit;
        }

        // Validação da Chave Mestre de Resgate (MAC Token)
        if ($pin === $calculatedRescuePin) {
            $state['logs'][] = [
                'id' => count($state['logs']) + 1,
                'time' => date('d/m H:i:s'),
                'user' => 'RESCUE (Chave MAC)',
                'method' => 'Web Portal',
                'action' => '🚨 DESTRAVAMENTO DE EMERGÊNCIA POR CHAVE MAC',
                'success' => true
            ];
            saveState($state, $dataFile);

            echo json_encode([
                'status' => 'ok',
                'isRescue' => true,
                'message' => 'CHAVE MESTRE DE RESGATE VÁLIDA! Destravando cofre...',
                'userName' => 'Chave de Emergência MAC',
                'duration' => $state['solenoidPulseMs']
            ]);
            exit;
        }

        // Validação de usuários comuns
        $matchedUser = null;
        foreach ($state['users'] as $user) {
            if ($user['pin'] === $pin) {
                $matchedUser = $user;
                break;
            }
        }

        if ($matchedUser) {
            $state['logs'][] = [
                'id' => count($state['logs']) + 1,
                'time' => date('d/m H:i:s'),
                'user' => $matchedUser['name'],
                'method' => 'Web Portal',
                'action' => 'Abertura autorizada',
                'success' => true
            ];
            saveState($state, $dataFile);

            echo json_encode([
                'status' => 'ok',
                'isRescue' => false,
                'message' => 'Autenticado com sucesso! Destravando cofre...',
                'userName' => $matchedUser['name'],
                'duration' => $state['solenoidPulseMs']
            ]);
        } else {
            $state['logs'][] = [
                'id' => count($state['logs']) + 1,
                'time' => date('d/m H:i:s'),
                'user' => 'Desconhecido',
                'method' => 'Web Portal',
                'action' => 'Tentativa de senha inválida',
                'success' => false
            ];
            saveState($state, $dataFile);

            http_response_code(401);
            echo json_encode(['status' => 'error', 'message' => 'Senha incorreta! Acesso negado.']);
        }
        break;

    case 'emergency_reset':
        $rescuePin = $_POST['rescuePin'] ?? $_GET['rescuePin'] ?? '';
        if ($rescuePin !== $calculatedRescuePin) {
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Chave de Resgate MAC incorreta! Acesso negado.']);
            exit;
        }

        foreach ($state['users'] as &$u) {
            if ($u['role'] === 'admin') {
                $u['pin'] = '123456';
                break;
            }
        }

        $state['logs'][] = [
            'id' => count($state['logs']) + 1,
            'time' => date('d/m H:i:s'),
            'user' => 'RESCUE (Chave MAC)',
            'method' => 'Web Portal',
            'action' => 'Senha Mestre restaurada para 123456 via Chave MAC',
            'success' => true
        ];
        saveState($state, $dataFile);

        echo json_encode([
            'status' => 'ok',
            'message' => 'Senha Mestre restaurada para 123456 com sucesso!'
        ]);
        break;

    case 'add_user':
        $adminPin = $_POST['adminPin'] ?? $_GET['adminPin'] ?? '';
        $name = trim($_POST['name'] ?? $_GET['name'] ?? '');
        $pin = trim($_POST['pin'] ?? $_GET['pin'] ?? '');

        $isAdmin = false;
        foreach ($state['users'] as $u) {
            if ($u['role'] === 'admin' && ($u['pin'] === $adminPin || $adminPin === $calculatedRescuePin)) {
                $isAdmin = true;
                break;
            }
        }

        if (!$isAdmin) {
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Senha Mestre ou Chave MAC incorreta!']);
            exit;
        }

        if (empty($name) || strlen($pin) < 4 || strlen($pin) > 8) {
            echo json_encode(['status' => 'error', 'message' => 'Nome obrigatório e senha de 4 a 8 dígitos!']);
            exit;
        }

        $newId = count($state['users']) + 1;
        $state['users'][] = [
            'id' => $newId,
            'name' => $name,
            'pin' => $pin,
            'role' => 'user',
            'created_at' => date('Y-m-d')
        ];

        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => "Usuário {$name} cadastrado com sucesso!"]);
        break;

    case 'change_admin_pin':
        $currPin = $_POST['currPin'] ?? $_GET['currPin'] ?? '';
        $newPin = $_POST['newPin'] ?? $_GET['newPin'] ?? '';

        $found = false;
        foreach ($state['users'] as &$u) {
            if ($u['role'] === 'admin' && ($u['pin'] === $currPin || $currPin === $calculatedRescuePin)) {
                $u['pin'] = $newPin;
                $found = true;
                break;
            }
        }

        if (!$found) {
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Senha atual ou Chave MAC inválida!']);
            exit;
        }

        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => 'Senha Mestre atualizada com sucesso!']);
        break;

    case 'config':
        if (isset($_GET['pulse'])) $state['solenoidPulseMs'] = intval($_GET['pulse']);
        if (isset($_GET['timeout'])) $state['sleepTimeout'] = intval($_GET['timeout']);
        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => 'Configurações salvas!']);
        break;

    case 'sleep':
        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => 'Entrando em Deep Sleep...']);
        break;

    default:
        echo json_encode(['status' => 'ok', 'message' => 'API de Controle Cofre ESP32']);
        break;
}
