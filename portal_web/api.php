<?php
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');

$dataFile = __DIR__ . '/storage.json';

// Inicializa dados padrão
if (!file_exists($dataFile)) {
    $defaultData = [
        'isLocked' => true,
        'solenoidPulseMs' => 800,
        'batteryV' => 8.85,
        'sleepTimeout' => 180,
        'passcode' => '123456',
        'logs' => [
            ['time' => date('H:i:s'), 'text' => 'Servidor Laragon (Simulador ESP32-C3) iniciado', 'error' => false]
        ]
    ];
    file_put_contents($dataFile, json_encode($defaultData, JSON_PRETTY_PRINT));
}

$state = json_decode(file_get_contents($dataFile), true);
$action = $_GET['action'] ?? '';

switch ($action) {
    case 'status':
        echo json_encode([
            'status' => 'ok',
            'isLocked' => $state['isLocked'],
            'batteryV' => $state['batteryV'],
            'pulseMs' => $state['solenoidPulseMs'],
            'sleepTimeout' => $state['sleepTimeout']
        ]);
        break;

    case 'unlock':
        $state['logs'][] = [
            'time' => date('H:i:s'),
            'text' => 'Comando de destravamento recebido via Web',
            'error' => false
        ];
        file_put_contents($dataFile, json_encode($state, JSON_PRETTY_PRINT));
        echo json_encode([
            'status' => 'ok',
            'message' => 'Pulso de destravamento enviado ao solenoide!',
            'duration' => $state['solenoidPulseMs']
        ]);
        break;

    case 'password':
        $curr = $_GET['curr'] ?? '';
        $new = $_GET['new'] ?? '';
        
        if ($curr !== $state['passcode']) {
            echo json_encode(['status' => 'error', 'message' => 'Senha atual incorreta!']);
            exit;
        }

        $state['passcode'] = $new;
        $state['logs'][] = [
            'time' => date('H:i:s'),
            'text' => 'Senha mestre atualizada com sucesso',
            'error' => false
        ];
        file_put_contents($dataFile, json_encode($state, JSON_PRETTY_PRINT));
        echo json_encode(['status' => 'ok', 'message' => 'Senha atualizada!']);
        break;

    case 'config':
        if (isset($_GET['pulse'])) $state['solenoidPulseMs'] = intval($_GET['pulse']);
        if (isset($_GET['timeout'])) $state['sleepTimeout'] = intval($_GET['timeout']);
        
        $state['logs'][] = [
            'time' => date('H:i:s'),
            'text' => 'Parâmetros de configuração salvos',
            'error' => false
        ];
        file_put_contents($dataFile, json_encode($state, JSON_PRETTY_PRINT));
        echo json_encode(['status' => 'ok', 'message' => 'Configurações salvas!']);
        break;

    case 'sleep':
        $state['logs'][] = [
            'time' => date('H:i:s'),
            'text' => 'Comando Deep Sleep executado',
            'error' => false
        ];
        file_put_contents($dataFile, json_encode($state, JSON_PRETTY_PRINT));
        echo json_encode(['status' => 'ok', 'message' => 'Entrando em Deep Sleep...']);
        break;

    default:
        echo json_encode(['status' => 'ok', 'message' => 'Simulador de API ESP32 pronto']);
        break;
}
