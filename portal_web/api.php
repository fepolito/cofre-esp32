<?php
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');

$dataFile = __DIR__ . '/storage.json';

// Estrutura inicial do banco de dados simulado
if (!file_exists($dataFile)) {
    $defaultData = [
        'isConfigured' => true,
        'isLocked' => true,
        'solenoidPulseMs' => 800,
        'batteryV' => 8.85,
        'sleepTimeout' => 180,
        'recoveryToken' => 'CF-' . strtoupper(substr(md5(uniqid(mt_rand(), true)), 0, 6)),
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
                'action' => 'Inicialização do Cofre',
                'success' => true
            ],
            [
                'id' => 2,
                'time' => date('d/m H:i:s', time() - 3600),
                'user' => 'Fernando (Mestre)',
                'method' => 'Teclado',
                'action' => 'Abertura bem-sucedida',
                'success' => true
            ]
        ]
    ];
    file_put_contents($dataFile, json_encode($defaultData, JSON_PRETTY_PRINT));
}

$state = json_decode(file_get_contents($dataFile), true);
$action = $_GET['action'] ?? '';

// Função auxiliar para sanitizar lista de usuários (não vaza os PINs na consulta pública)
function getSanitizedUsers($users) {
    return array_map(function($u) {
        return [
            'id' => $u['id'],
            'name' => $u['name'],
            'role' => $u['role'],
            'created_at' => $u['created_at']
        ];
    }, $users);
}

// Salva alterações
function saveState($state, $file) {
    file_put_contents($file, json_encode($state, JSON_PRETTY_PRINT));
}

switch ($action) {
    case 'status':
        echo json_encode([
            'status' => 'ok',
            'isConfigured' => $state['isConfigured'] ?? true,
            'isLocked' => $state['isLocked'],
            'batteryV' => $state['batteryV'],
            'pulseMs' => $state['solenoidPulseMs'],
            'sleepTimeout' => $state['sleepTimeout'],
            'users' => getSanitizedUsers($state['users']),
            'logs' => array_slice(array_reverse($state['logs']), 0, 15)
        ]);
        break;

    case 'unlock':
        $pin = $_POST['pin'] ?? $_GET['pin'] ?? '';
        
        if (empty($pin)) {
            echo json_encode(['status' => 'error', 'message' => 'Senha não informada!']);
            exit;
        }

        // Procura usuário com este PIN
        $matchedUser = null;
        foreach ($state['users'] as $user) {
            if ($user['pin'] === $pin) {
                $matchedUser = $user;
                break;
            }
        }

        if ($matchedUser) {
            // Sucesso na autenticação
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
                'message' => 'Autenticado com sucesso! Destravando cofre...',
                'userName' => $matchedUser['name'],
                'duration' => $state['solenoidPulseMs']
            ]);
        } else {
            // Falha na autenticação (Tentativa não autorizada)
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

    case 'add_user':
        $adminPin = $_POST['adminPin'] ?? $_GET['adminPin'] ?? '';
        $name = trim($_POST['name'] ?? $_GET['name'] ?? '');
        $pin = trim($_POST['pin'] ?? $_GET['pin'] ?? '');

        // Valida se o PIN informado pertence ao admin
        $isAdmin = false;
        foreach ($state['users'] as $u) {
            if ($u['role'] === 'admin' && $u['pin'] === $adminPin) {
                $isAdmin = true;
                break;
            }
        }

        if (!$isAdmin) {
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Senha de Administrador incorreta!']);
            exit;
        }

        if (empty($name) || strlen($pin) < 4 || strlen($pin) > 8) {
            echo json_encode(['status' => 'error', 'message' => 'Nome obrigatório e senha de 4 a 8 dígitos!']);
            exit;
        }

        // Verifica duplicidade de PIN
        foreach ($state['users'] as $u) {
            if ($u['pin'] === $pin) {
                echo json_encode(['status' => 'error', 'message' => 'Esta senha já está em uso por outro usuário!']);
                exit;
            }
        }

        $newId = count($state['users']) + 1;
        $state['users'][] = [
            'id' => $newId,
            'name' => $name,
            'pin' => $pin,
            'role' => 'user',
            'created_at' => date('Y-m-d')
        ];

        $state['logs'][] = [
            'id' => count($state['logs']) + 1,
            'time' => date('d/m H:i:s'),
            'user' => 'Admin',
            'method' => 'Web Portal',
            'action' => "Novo usuário cadastrado: {$name}",
            'success' => true
        ];

        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => "Usuário {$name} cadastrado com sucesso!"]);
        break;

    case 'delete_user':
        $adminPin = $_POST['adminPin'] ?? $_GET['adminPin'] ?? '';
        $userId = intval($_POST['userId'] ?? $_GET['userId'] ?? 0);

        // Valida admin
        $isAdmin = false;
        foreach ($state['users'] as $u) {
            if ($u['role'] === 'admin' && $u['pin'] === $adminPin) {
                $isAdmin = true;
                break;
            }
        }

        if (!$isAdmin) {
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Senha de Administrador incorreta!']);
            exit;
        }

        $filtered = [];
        $deletedName = '';
        foreach ($state['users'] as $u) {
            if ($u['id'] === $userId) {
                if ($u['role'] === 'admin') {
                    echo json_encode(['status' => 'error', 'message' => 'Não é permitido excluir o usuário Mestre!']);
                    exit;
                }
                $deletedName = $u['name'];
            } else {
                $filtered[] = $u;
            }
        }

        $state['users'] = $filtered;
        $state['logs'][] = [
            'id' => count($state['logs']) + 1,
            'time' => date('d/m H:i:s'),
            'user' => 'Admin',
            'method' => 'Web Portal',
            'action' => "Usuário removido: {$deletedName}",
            'success' => true
        ];

        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => 'Usuário removido com sucesso!']);
        break;

    case 'change_admin_pin':
        $currPin = $_POST['currPin'] ?? $_GET['currPin'] ?? '';
        $newPin = $_POST['newPin'] ?? $_GET['newPin'] ?? '';

        if (strlen($newPin) < 4 || strlen($newPin) > 8) {
            echo json_encode(['status' => 'error', 'message' => 'A nova senha deve ter entre 4 e 8 dígitos!']);
            exit;
        }

        $found = false;
        foreach ($state['users'] as &$u) {
            if ($u['role'] === 'admin' && $u['pin'] === $currPin) {
                $u['pin'] = $newPin;
                $found = true;
                break;
            }
        }

        if (!$found) {
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Senha Mestre atual incorreta!']);
            exit;
        }

        $state['logs'][] = [
            'id' => count($state['logs']) + 1,
            'time' => date('d/m H:i:s'),
            'user' => 'Admin',
            'method' => 'Web Portal',
            'action' => 'Senha Mestre alterada',
            'success' => true
        ];

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
        $state['logs'][] = [
            'id' => count($state['logs']) + 1,
            'time' => date('d/m H:i:s'),
            'user' => 'Web Portal',
            'method' => 'Web Portal',
            'action' => 'Wi-Fi desligado (Deep Sleep acionado)',
            'success' => true
        ];
        saveState($state, $dataFile);
        echo json_encode(['status' => 'ok', 'message' => 'Entrando em Deep Sleep...']);
        break;

    default:
        echo json_encode(['status' => 'ok', 'message' => 'API de Controle Cofre ESP32']);
        break;
}
