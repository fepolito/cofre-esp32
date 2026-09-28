#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Cofre Inteligente ESP32</title>
  <style>
    :root {
      --bg-primary: #0b1120;
      --bg-card: #1e293b;
      --bg-card-hover: #334155;
      --accent-primary: #38bdf8;
      --accent-success: #10b981;
      --accent-danger: #ef4444;
      --accent-warning: #f59e0b;
      --text-main: #f8fafc;
      --text-muted: #94a3b8;
      --border-color: #334155;
      --radius-lg: 16px;
      --radius-md: 10px;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; -webkit-tap-highlight-color: transparent; }
    body { background-color: var(--bg-primary); color: var(--text-main); min-height: 100vh; display: flex; flex-direction: column; align-items: center; padding: 16px; }
    .container { width: 100%; max-width: 480px; display: flex; flex-direction: column; gap: 16px; }
    header { display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; background: var(--bg-card); border-radius: var(--radius-lg); border: 1px solid var(--border-color); }
    .logo-area { display: flex; align-items: center; gap: 10px; }
    .logo-icon { width: 38px; height: 38px; background: linear-gradient(135deg, #0284c7, #38bdf8); border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 20px; box-shadow: 0 4px 10px rgba(56, 189, 248, 0.3); }
    .logo-text h1 { font-size: 1.05rem; font-weight: 700; color: #fff; }
    .logo-text span { font-size: 0.72rem; color: var(--text-muted); }
    .status-badge { display: flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px; font-size: 0.75rem; font-weight: 600; background: rgba(239, 68, 68, 0.15); color: var(--accent-danger); border: 1px solid rgba(239, 68, 68, 0.3); }
    .status-badge.unlocked { background: rgba(16, 185, 129, 0.15); color: var(--accent-success); border-color: rgba(16, 185, 129, 0.3); }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background-color: currentColor; }
    .card { background: var(--bg-card); border-radius: var(--radius-lg); padding: 18px; border: 1px solid var(--border-color); box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2); }
    .hero-card { text-align: center; display: flex; flex-direction: column; align-items: center; gap: 16px; padding: 24px 18px; }
    .lock-indicator { width: 82px; height: 82px; border-radius: 50%; background: rgba(56, 189, 248, 0.1); border: 2px solid var(--accent-primary); display: flex; align-items: center; justify-content: center; font-size: 38px; transition: all 0.3s ease; }
    .lock-indicator.unlocked { background: rgba(16, 185, 129, 0.15); border-color: var(--accent-success); }
    .security-note { display: flex; align-items: center; gap: 6px; font-size: 0.75rem; color: #38bdf8; background: rgba(56, 189, 248, 0.1); padding: 6px 12px; border-radius: 20px; }
    .btn-unlock { width: 100%; padding: 16px; font-size: 1.05rem; font-weight: 700; color: white; background: linear-gradient(135deg, #0284c7, #2563eb); border: none; border-radius: var(--radius-md); cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 10px; box-shadow: 0 10px 15px -3px rgba(2, 132, 199, 0.4); }
    .btn-unlock:active { transform: scale(0.98); }
    .btn-unlock:disabled { background: #475569; box-shadow: none; cursor: not-allowed; }
    .metrics-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; width: 100%; }
    .metric-card { background: rgba(11, 17, 32, 0.7); border-radius: var(--radius-md); padding: 12px; border: 1px solid var(--border-color); display: flex; flex-direction: column; gap: 4px; text-align: left; }
    .metric-label { font-size: 0.72rem; color: var(--text-muted); text-transform: uppercase; font-weight: 600; }
    .metric-value { font-size: 1.25rem; font-weight: 700; color: var(--text-main); }
    .tabs-header { display: flex; gap: 6px; border-bottom: 1px solid var(--border-color); padding-bottom: 8px; margin-bottom: 14px; overflow-x: auto; }
    .tab-btn { background: transparent; border: none; color: var(--text-muted); font-size: 0.85rem; font-weight: 600; padding: 8px 12px; border-radius: 6px; cursor: pointer; white-space: nowrap; }
    .tab-btn.active { background: var(--bg-card-hover); color: var(--accent-primary); }
    .tab-content { display: none; }
    .tab-content.active { display: block; }
    .form-group { display: flex; flex-direction: column; gap: 6px; margin-bottom: 12px; }
    .form-group label { font-size: 0.8rem; color: var(--text-muted); }
    .form-input { background: #0b1120; border: 1px solid var(--border-color); border-radius: 8px; padding: 12px; color: white; font-size: 0.95rem; outline: none; }
    .form-input:focus { border-color: var(--accent-primary); }
    .btn-secondary { width: 100%; padding: 12px; background: var(--bg-card-hover); border: 1px solid var(--border-color); color: white; border-radius: 8px; font-weight: 600; cursor: pointer; }
    .btn-primary-action { background: #0284c7; border: none; }
    .btn-warning-action { background: rgba(245, 158, 11, 0.15); border: 1px solid rgba(245, 158, 11, 0.4); color: #fcd34d; }
    .btn-danger { background: rgba(239, 68, 68, 0.15); border-color: rgba(239, 68, 68, 0.4); color: #fca5a5; }
    .user-list { display: flex; flex-direction: column; gap: 10px; margin-bottom: 16px; }
    .user-item { display: flex; align-items: center; justify-content: space-between; padding: 12px; background: #0b1120; border-radius: var(--radius-md); border: 1px solid var(--border-color); }
    .user-info { display: flex; align-items: center; gap: 10px; }
    .user-avatar { width: 36px; height: 36px; border-radius: 50%; background: #1e293b; display: flex; align-items: center; justify-content: center; font-size: 16px; }
    .user-details h4 { font-size: 0.9rem; font-weight: 600; color: white; }
    .user-details span { font-size: 0.72rem; color: var(--text-muted); }
    .role-badge { font-size: 0.65rem; padding: 2px 8px; border-radius: 12px; font-weight: 700; text-transform: uppercase; }
    .role-admin { background: rgba(56, 189, 248, 0.2); color: #38bdf8; border: 1px solid rgba(56, 189, 248, 0.4); }
    .role-user { background: rgba(148, 163, 184, 0.2); color: #cbd5e1; }
    .log-list { display: flex; flex-direction: column; gap: 8px; max-height: 280px; overflow-y: auto; }
    .log-item { display: flex; justify-content: space-between; align-items: center; padding: 10px; background: #0b1120; border-radius: 8px; font-size: 0.8rem; border-left: 3px solid var(--accent-success); }
    .log-item.error { border-left-color: var(--accent-danger); }
    .log-item.rescue { border-left-color: var(--accent-warning); background: rgba(245, 158, 11, 0.08); }
    .log-left { display: flex; flex-direction: column; gap: 2px; }
    .log-user { font-weight: 600; color: white; display: flex; align-items: center; gap: 6px; }
    .log-desc { font-size: 0.72rem; color: var(--text-muted); }
    .log-time { color: var(--text-muted); font-size: 0.7rem; }
    .modal-overlay { position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(11, 17, 32, 0.85); backdrop-filter: blur(5px); display: none; align-items: center; justify-content: center; z-index: 1000; padding: 16px; }
    .modal-card { background: var(--bg-card); border-radius: var(--radius-lg); padding: 24px; width: 100%; max-width: 380px; border: 1px solid var(--border-color); text-align: center; }
    .pin-display { font-size: 2rem; letter-spacing: 8px; color: var(--accent-primary); height: 48px; display: flex; align-items: center; justify-content: center; margin: 12px 0 14px; background: #0b1120; border-radius: 8px; border: 1px solid var(--border-color); font-family: monospace; }
    .keypad-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 14px; }
    .key-btn { background: #0b1120; border: 1px solid var(--border-color); color: white; font-size: 1.3rem; font-weight: 600; padding: 14px 0; border-radius: 10px; cursor: pointer; }
    .key-btn:active { background: var(--accent-primary); color: black; }
    .key-btn.action-btn { font-size: 0.95rem; background: #1e293b; color: var(--text-muted); }
    .rescue-box { background: #0b1120; border: 1px solid rgba(245, 158, 11, 0.4); border-radius: var(--radius-md); padding: 14px; margin-top: 14px; text-align: left; }
    .toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #1e293b; color: white; padding: 12px 20px; border-radius: 30px; font-size: 0.9rem; box-shadow: 0 10px 25px rgba(0, 0, 0, 0.6); z-index: 2000; display: none; border: 1px solid #475569; }
    footer { text-align: center; color: var(--text-muted); font-size: 0.75rem; margin-top: 8px; padding: 10px; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div class="logo-area">
        <div class="logo-icon">🔒</div>
        <div class="logo-text">
          <h1>Cofre Inteligente</h1>
          <span>ESP32-C3 • Zero-Knowledge Rescue Token</span>
        </div>
      </div>
      <div class="status-badge" id="lockBadge">
        <span class="status-dot"></span>
        <span id="lockBadgeText">TRANCADO</span>
      </div>
    </header>

    <div class="card hero-card">
      <div class="security-note">🛡️ Proteção por Senha + Chave de Resgate Offline</div>
      <div class="lock-indicator" id="lockIcon">🔒</div>
      <div>
        <h2 id="heroTitle" style="font-size: 1.3rem; margin-bottom: 4px;">Cofre Trancado</h2>
        <p style="color: var(--text-muted); font-size: 0.85rem;" id="heroSubtitle">Clique para autenticar e destravar a bobina</p>
      </div>

      <button class="btn-unlock" id="btnUnlock" onclick="openAuthModal()">
        <span>⚡ DESTRAVAR AGORA</span>
      </button>

      <div class="metrics-grid">
        <div class="metric-card">
          <span class="metric-label">Bateria</span>
          <div class="metric-value" id="batVoltage">-- <span style="font-size: 0.8rem; color: var(--text-muted);">V</span></div>
        </div>
        <div class="metric-card">
          <span class="metric-label">Pulso Bobina</span>
          <div class="metric-value" id="pulseDisplay">800 <span style="font-size: 0.8rem; color: var(--text-muted);">ms</span></div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="tabs-header">
        <button class="tab-btn active" onclick="switchTab('usuarios')">👥 Usuários</button>
        <button class="tab-btn" onclick="switchTab('historico')">📋 Histórico Auditado</button>
        <button class="tab-btn" onclick="switchTab('resgate')">🆘 Resgate por MAC</button>
        <button class="tab-btn" onclick="switchTab('ajustes')">⚙️ Ajustes</button>
      </div>

      <div class="tab-content active" id="tab-usuarios">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
          <h3 style="font-size: 0.95rem; font-weight: 600;">Usuários com Acesso</h3>
          <span style="font-size: 0.75rem; color: var(--text-muted);" id="userCount">Carregando...</span>
        </div>
        <div class="user-list" id="usersContainer"></div>

        <div style="background: #0b1120; padding: 14px; border-radius: var(--radius-md); border: 1px solid var(--border-color);">
          <h4 style="font-size: 0.85rem; margin-bottom: 10px; color: var(--accent-primary);">➕ Cadastrar Novo Usuário</h4>
          <form onsubmit="event.preventDefault(); handleAddUser();">
            <div class="form-group">
              <label>Nome do Usuário</label>
              <input type="text" id="newUserName" class="form-input" placeholder="Ex: Esposa, Sócio" required>
            </div>
            <div class="form-group">
              <label>PIN Numérico (4 a 8 dígitos)</label>
              <input type="password" id="newUserPin" class="form-input" placeholder="Nova senha" maxlength="8" pattern="[0-9]*" inputmode="numeric" required>
            </div>
            <div class="form-group">
              <label>Senha do Mestre (Autorização)</label>
              <input type="password" id="adminAuthPin" class="form-input" placeholder="Senha Mestre" maxlength="8" required>
            </div>
            <button type="submit" class="btn-secondary btn-primary-action">Cadastrar Acesso</button>
          </form>
        </div>
      </div>

      <div class="tab-content" id="tab-historico">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
          <span style="font-size: 0.8rem; color: var(--text-muted);">Últimas Aberturas e Tentativas</span>
          <button onclick="loadStatus()" style="background: none; border: none; color: var(--accent-primary); font-size: 0.75rem; cursor: pointer;">🔄 Atualizar</button>
        </div>
        <div class="log-list" id="logContainer"></div>
      </div>

      <div class="tab-content" id="tab-resgate">
        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 10px;">
          <span style="font-size: 24px;">🆘</span>
          <div>
            <h3 style="font-size: 0.95rem; color: #fbbf24;">Centro de Resgate de Emergência</h3>
            <span style="font-size: 0.72rem; color: var(--text-muted);">Recuperação de Perda Total de Senhas</span>
          </div>
        </div>

        <div class="rescue-box">
          <div style="display: flex; justify-content: space-between; align-items: center;">
            <span style="font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; font-weight: 600;">ID de Hardware (MAC)</span>
            <span style="font-family: monospace; font-size: 0.85rem; color: white; background: #1e293b; padding: 2px 8px; border-radius: 4px;" id="macDisplay">--:--:--:--:--:--</span>
          </div>
          <p style="font-size: 0.78rem; color: #cbd5e1; line-height: 1.4; margin-top: 10px;">
            🔒 <b>Política de Segurança Rigorosa:</b><br>
            A Chave Mestre de Resgate <u>NUNCA</u> é exibida neste portal para impedir que pessoas não autorizadas descubram o código ao conectar no Wi-Fi.
          </p>
          <p style="font-size: 0.75rem; color: var(--text-muted); line-height: 1.4; margin-top: 6px;">
            A Chave foi gerada pela sua ferramenta offline (<code>tools/gerar_chave_resgate.py</code>) a partir do MAC do ESP32 e guardada com você fora do cofre.
          </p>
        </div>

        <div style="margin-top: 14px; background: #0b1120; padding: 14px; border-radius: var(--radius-md); border: 1px solid var(--border-color);">
          <h4 style="font-size: 0.85rem; margin-bottom: 8px; color: white;">Executar Resgate com a Chave</h4>
          <div class="form-group">
            <label>Sua Chave Mestre de Resgate (6 dígitos guardada por você)</label>
            <input type="password" id="inputRescueActionPin" class="form-input" placeholder="Digite a chave de 6 dígitos" maxlength="6" pattern="[0-9]*" inputmode="numeric">
          </div>
          <div style="display: flex; flex-direction: column; gap: 8px;">
            <button class="btn-secondary btn-warning-action" onclick="handleEmergencyUnlockAction()">⚡ Destravar Cofre em Emergência</button>
            <button class="btn-secondary" style="border-color: #64748b; font-size: 0.8rem;" onclick="handleEmergencyResetAction()">🔄 Redefinir Senha Mestre para 123456</button>
          </div>
        </div>
      </div>

      <div class="tab-content" id="tab-ajustes">
        <div class="form-group">
          <label>Tempo de Retração da Bobina (ms)</label>
          <input type="number" id="pulseTimeInput" class="form-input" value="800" min="300" max="2000" step="100">
        </div>
        <button class="btn-secondary" onclick="handleSaveConfig()" style="margin-bottom: 14px;">Salvar Parâmetros</button>
        <hr style="border: none; border-top: 1px solid var(--border-color); margin: 16px 0;">
        <button class="btn-secondary btn-danger" onclick="handleSleepNow()">💤 Desligar Wi-Fi e Entrar em Deep Sleep</button>
      </div>
    </div>
  </div>

  <div class="modal-overlay" id="authModal">
    <div class="modal-card">
      <div style="font-size: 32px; margin-bottom: 8px;">🔐</div>
      <h3 style="font-size: 1.15rem; color: white;">Autenticação Obrigatória</h3>
      <p style="font-size: 0.8rem; color: var(--text-muted); margin-top: 4px;">Digite seu PIN de usuário ou Chave de Resgate</p>
      <div class="pin-display" id="modalPinDisplay">••••</div>
      <div class="keypad-grid">
        <button class="key-btn" onclick="appendPin('1')">1</button>
        <button class="key-btn" onclick="appendPin('2')">2</button>
        <button class="key-btn" onclick="appendPin('3')">3</button>
        <button class="key-btn" onclick="appendPin('4')">4</button>
        <button class="key-btn" onclick="appendPin('5')">5</button>
        <button class="key-btn" onclick="appendPin('6')">6</button>
        <button class="key-btn" onclick="appendPin('7')">7</button>
        <button class="key-btn" onclick="appendPin('8')">8</button>
        <button class="key-btn" onclick="appendPin('9')">9</button>
        <button class="key-btn action-btn" onclick="clearPin()">C</button>
        <button class="key-btn" onclick="appendPin('0')">0</button>
        <button class="key-btn action-btn" onclick="backspacePin()">⌫</button>
      </div>
      <div style="display: flex; gap: 8px;">
        <button class="btn-secondary" style="flex: 1;" onclick="closeAuthModal()">Cancelar</button>
        <button class="btn-secondary btn-primary-action" style="flex: 1.5;" onclick="submitUnlockPin()">Confirmar</button>
      </div>
    </div>
  </div>

  <div class="toast" id="toast"></div>

  <script>
    const state = {
      isLocked: true,
      solenoidPulseMs: 800,
      batteryV: 8.85,
      deviceMac: '--:--:--:--:--:--',
      users: [],
      logs: [],
      modalPin: ''
    };

    function showToast(msg, isError = false) {
      const toast = document.getElementById('toast');
      toast.innerText = msg;
      toast.style.borderColor = isError ? 'var(--accent-danger)' : 'var(--accent-success)';
      toast.style.display = 'block';
      setTimeout(() => { toast.style.display = 'none'; }, 3400);
    }

    function switchTab(t) {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      event.target.classList.add('active');
      document.getElementById('tab-' + t).classList.add('active');
    }

    function openAuthModal() { state.modalPin = ''; updatePinDisplay(); document.getElementById('authModal').style.display = 'flex'; }
    function closeAuthModal() { document.getElementById('authModal').style.display = 'none'; state.modalPin = ''; }
    function appendPin(d) { if (state.modalPin.length < 8) { state.modalPin += d; updatePinDisplay(); } }
    function clearPin() { state.modalPin = ''; updatePinDisplay(); }
    function backspacePin() { state.modalPin = state.modalPin.slice(0, -1); updatePinDisplay(); }
    function updatePinDisplay() { document.getElementById('modalPinDisplay').innerText = state.modalPin ? '•'.repeat(state.modalPin.length) : '••••'; }

    async function submitUnlockPin(customPin = null) {
      const pinToSend = customPin || state.modalPin;
      if (!pinToSend || pinToSend.length < 4) { showToast('Digite pelo menos 4 dígitos!', true); return; }
      if (!customPin) closeAuthModal();

      const btn = document.getElementById('btnUnlock');
      btn.disabled = true;
      btn.innerHTML = '<span>⏳ VERIFICANDO...</span>';

      try {
        const res = await fetch(`/api/unlock?pin=${encodeURIComponent(pinToSend)}`, { method: 'POST' });
        const data = await res.json();

        if (res.ok && data.status === 'ok') {
          document.getElementById('lockIcon').innerText = '🔓';
          document.getElementById('lockBadge').classList.add('unlocked');
          document.getElementById('lockBadgeText').innerText = 'DESTRAVADO';
          document.getElementById('heroTitle').innerText = data.isRescue ? '🚨 Destravado por Chave MAC!' : `Destravado por ${data.userName}!`;
          showToast(data.message || `Acesso liberado!`);
          loadStatus();

          setTimeout(() => {
            document.getElementById('lockIcon').innerText = '🔒';
            document.getElementById('lockBadge').classList.remove('unlocked');
            document.getElementById('lockBadgeText').innerText = 'TRANCADO';
            document.getElementById('heroTitle').innerText = 'Cofre Trancado';
            btn.disabled = false;
            btn.innerHTML = '<span>⚡ DESTRAVAR AGORA</span>';
          }, state.solenoidPulseMs + 1500);
        } else {
          showToast(data.message || 'Senha incorreta! Acesso negado.', true);
          btn.disabled = false;
          btn.innerHTML = '<span>⚡ DESTRAVAR AGORA</span>';
          loadStatus();
        }
      } catch (e) {
        showToast('Falha na comunicação', true);
        btn.disabled = false;
        btn.innerHTML = '<span>⚡ DESTRAVAR AGORA</span>';
      }
    }

    function renderUsers() {
      const c = document.getElementById('usersContainer');
      document.getElementById('userCount').innerText = `${state.users.length} cadastrados`;
      c.innerHTML = state.users.map(u => `
        <div class="user-item">
          <div class="user-info">
            <div class="user-avatar">${u.role === 'admin' ? '👑' : '👤'}</div>
            <div class="user-details">
              <h4>${u.name}</h4>
              <span>${u.role === 'admin' ? 'Administrador Mestre' : 'Usuário Padrão'}</span>
            </div>
          </div>
          <span class="role-badge ${u.role === 'admin' ? 'role-admin' : 'role-user'}">${u.role}</span>
        </div>
      `).join('');
    }

    function renderLogs() {
      const c = document.getElementById('logContainer');
      c.innerHTML = state.logs.map(l => {
        const isRescue = l.action.includes('MAC') || l.user.includes('RESCUE');
        return `
          <div class="log-item ${!l.success ? 'error' : (isRescue ? 'rescue' : '')}">
            <div class="log-left">
              <span class="log-user">${isRescue ? '🚨' : (l.method === 'Teclado' ? '🔢' : '🌐')} ${l.user}</span>
              <span class="log-desc">${l.action} • Canal: ${l.method}</span>
            </div>
            <span class="log-time">${l.time}</span>
          </div>
        `;
      }).join('');
    }

    async function handleAddUser() {
      const name = document.getElementById('newUserName').value.trim();
      const pin = document.getElementById('newUserPin').value.trim();
      const adminPin = document.getElementById('adminAuthPin').value.trim();
      const res = await fetch(`/api/add_user?name=${encodeURIComponent(name)}&pin=${encodeURIComponent(pin)}&adminPin=${encodeURIComponent(adminPin)}`, { method: 'POST' });
      const data = await res.json();
      if (res.ok && data.status === 'ok') {
        showToast(data.message);
        document.getElementById('newUserName').value = '';
        document.getElementById('newUserPin').value = '';
        document.getElementById('adminAuthPin').value = '';
        loadStatus();
      } else {
        showToast(data.message || 'Erro ao cadastrar', true);
      }
    }

    function handleEmergencyUnlockAction() {
      const pin = document.getElementById('inputRescueActionPin').value.trim();
      if (!pin) { showToast('Digite a Chave Mestre de Resgate!', true); return; }
      submitUnlockPin(pin);
    }

    async function handleEmergencyResetAction() {
      const pin = document.getElementById('inputRescueActionPin').value.trim();
      if (!pin) { showToast('Digite a Chave Mestre de Resgate!', true); return; }
      if (!confirm('Deseja restaurar a Senha Mestre para 123456?')) return;

      const res = await fetch(`/api/emergency_reset?rescuePin=${encodeURIComponent(pin)}`, { method: 'POST' });
      const data = await res.json();
      if (res.ok && data.status === 'ok') {
        showToast(data.message);
        loadStatus();
      } else {
        showToast(data.message || 'Chave de Resgate incorreta!', true);
      }
    }

    function handleSaveConfig() {
      const p = parseInt(document.getElementById('pulseTimeInput').value);
      state.solenoidPulseMs = p;
      fetch(`/api/config?pulse=${p}`, { method: 'POST' });
      document.getElementById('pulseDisplay').innerText = p + ' ms';
      showToast('Configurações salvas!');
    }

    function handleSleepNow() {
      if (confirm('Deseja colocar o cofre em repouso agora?')) {
        fetch('/api/sleep', { method: 'POST' });
        document.body.innerHTML = '<div style="text-align:center;margin-top:80px;color:#94a3b8;"><h2>💤 Cofre em Deep Sleep</h2><p>Wi-Fi desligado. Digite *000# no teclado para religar.</p></div>';
      }
    }

    async function loadStatus() {
      try {
        const res = await fetch('/api/status').then(r => r.json());
        if (res && res.status === 'ok') {
          state.batteryV = res.batteryV;
          state.solenoidPulseMs = res.pulseMs;
          state.deviceMac = res.deviceMac || '--:--:--:--:--:--';
          state.users = res.users || [];
          state.logs = res.logs || [];
          document.getElementById('batVoltage').innerText = res.batteryV.toFixed(1) + ' V';
          document.getElementById('pulseDisplay').innerText = res.pulseMs + ' ms';
          document.getElementById('pulseTimeInput').value = res.pulseMs;
          document.getElementById('macDisplay').innerText = state.deviceMac;
          renderUsers();
          renderLogs();
        }
      } catch(e) {}
    }
    loadStatus();
  </script>
</body>
</html>
)rawliteral";

#endif
