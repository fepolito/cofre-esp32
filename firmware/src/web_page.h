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
      --bg-primary: #0f172a;
      --bg-card: #1e293b;
      --bg-card-hover: #334155;
      --accent-primary: #38bdf8;
      --accent-success: #10b981;
      --accent-danger: #ef4444;
      --text-main: #f8fafc;
      --text-muted: #94a3b8;
      --border-color: #334155;
      --radius-lg: 16px;
      --radius-md: 10px;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background-color: var(--bg-primary); color: var(--text-main); min-height: 100vh; display: flex; flex-direction: column; align-items: center; padding: 16px; }
    .container { width: 100%; max-width: 480px; display: flex; flex-direction: column; gap: 16px; }
    header { display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; background: var(--bg-card); border-radius: var(--radius-lg); border: 1px solid var(--border-color); }
    .logo-area { display: flex; align-items: center; gap: 10px; }
    .logo-icon { width: 36px; height: 36px; background: linear-gradient(135deg, #0284c7, #38bdf8); border-radius: 8px; display: flex; align-items: center; justify-content: center; font-size: 20px; }
    .logo-text h1 { font-size: 1.1rem; font-weight: 700; color: #fff; }
    .logo-text span { font-size: 0.75rem; color: var(--text-muted); }
    .status-badge { display: flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px; font-size: 0.75rem; font-weight: 600; background: rgba(239, 68, 68, 0.15); color: var(--accent-danger); border: 1px solid rgba(239, 68, 68, 0.3); }
    .status-badge.unlocked { background: rgba(16, 185, 129, 0.15); color: var(--accent-success); border-color: rgba(16, 185, 129, 0.3); }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background-color: currentColor; }
    .card { background: var(--bg-card); border-radius: var(--radius-lg); padding: 18px; border: 1px solid var(--border-color); }
    .hero-card { text-align: center; display: flex; flex-direction: column; align-items: center; gap: 16px; padding: 24px 18px; }
    .lock-indicator { width: 80px; height: 80px; border-radius: 50%; background: rgba(56, 189, 248, 0.1); border: 2px solid var(--accent-primary); display: flex; align-items: center; justify-content: center; font-size: 38px; transition: all 0.3s ease; }
    .lock-indicator.unlocked { background: rgba(16, 185, 129, 0.15); border-color: var(--accent-success); }
    .btn-unlock { width: 100%; padding: 16px; font-size: 1.1rem; font-weight: 700; color: white; background: linear-gradient(135deg, #0284c7, #2563eb); border: none; border-radius: var(--radius-md); cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 10px; box-shadow: 0 10px 15px -3px rgba(2, 132, 199, 0.4); }
    .btn-unlock:active { transform: scale(0.98); }
    .btn-unlock:disabled { background: #475569; box-shadow: none; cursor: not-allowed; }
    .metrics-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; width: 100%; }
    .metric-card { background: rgba(15, 23, 42, 0.6); border-radius: var(--radius-md); padding: 12px; border: 1px solid var(--border-color); display: flex; flex-direction: column; gap: 6px; }
    .metric-label { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; font-weight: 600; }
    .metric-value { font-size: 1.25rem; font-weight: 700; color: var(--text-main); }
    .tabs-header { display: flex; gap: 8px; border-bottom: 1px solid var(--border-color); padding-bottom: 8px; margin-bottom: 14px; }
    .tab-btn { background: transparent; border: none; color: var(--text-muted); font-size: 0.9rem; font-weight: 600; padding: 8px 12px; border-radius: 6px; cursor: pointer; }
    .tab-btn.active { background: var(--bg-card-hover); color: var(--accent-primary); }
    .tab-content { display: none; }
    .tab-content.active { display: block; }
    .form-group { display: flex; flex-direction: column; gap: 6px; margin-bottom: 12px; }
    .form-group label { font-size: 0.8rem; color: var(--text-muted); }
    .form-input { background: #0f172a; border: 1px solid var(--border-color); border-radius: 8px; padding: 12px; color: white; font-size: 1rem; outline: none; }
    .form-input:focus { border-color: var(--accent-primary); }
    .btn-secondary { width: 100%; padding: 12px; background: var(--bg-card-hover); border: 1px solid var(--border-color); color: white; border-radius: 8px; font-weight: 600; cursor: pointer; }
    .btn-danger { background: rgba(239, 68, 68, 0.2); border-color: rgba(239, 68, 68, 0.4); color: #fca5a5; }
    .toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #334155; color: white; padding: 12px 20px; border-radius: 30px; font-size: 0.9rem; box-shadow: 0 10px 25px rgba(0, 0, 0, 0.5); z-index: 100; display: none; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div class="logo-area">
        <div class="logo-icon">🔒</div>
        <div class="logo-text">
          <h1>Cofre ESP32</h1>
          <span>Firmware v1.0 • ESP32-C3</span>
        </div>
      </div>
      <div class="status-badge" id="lockBadge">
        <span class="status-dot"></span>
        <span id="lockBadgeText">TRANCADO</span>
      </div>
    </header>

    <div class="card hero-card">
      <div class="lock-indicator" id="lockIcon">🔒</div>
      <div>
        <h2 id="heroTitle" style="font-size: 1.3rem;">Cofre Trancado</h2>
        <p style="color: var(--text-muted); font-size: 0.85rem;" id="heroSubtitle">Clique para acionar a bobina de abertura</p>
      </div>

      <button class="btn-unlock" id="btnUnlock" onclick="handleUnlock()">
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
        <button class="tab-btn active" onclick="switchTab('senhas')">🔑 Senhas</button>
        <button class="tab-btn" onclick="switchTab('ajustes')">⚙️ Ajustes</button>
      </div>

      <div class="tab-content active" id="tab-senhas">
        <form onsubmit="event.preventDefault(); handleChangePassword();">
          <div class="form-group">
            <label>Senha Atual (Mestre)</label>
            <input type="password" id="currentPin" class="form-input" maxlength="8" required>
          </div>
          <div class="form-group">
            <label>Nova Senha Teclado (4 a 8 dígitos)</label>
            <input type="password" id="newPin" class="form-input" maxlength="8" pattern="[0-9]*" inputmode="numeric" required>
          </div>
          <div class="form-group">
            <label>Confirmar Nova Senha</label>
            <input type="password" id="confirmPin" class="form-input" maxlength="8" pattern="[0-9]*" inputmode="numeric" required>
          </div>
          <button type="submit" class="btn-secondary" style="background: #0284c7; border: none;">Salvar Nova Senha</button>
        </form>
      </div>

      <div class="tab-content" id="tab-ajustes">
        <div class="form-group">
          <label>Tempo de Retração da Bobina (ms)</label>
          <input type="number" id="pulseTimeInput" class="form-input" value="800" min="300" max="2000" step="100">
        </div>
        <button class="btn-secondary" onclick="handleSaveConfig()" style="margin-bottom: 12px;">Salvar Parâmetros</button>
        <hr style="border: none; border-top: 1px solid var(--border-color); margin: 16px 0;">
        <button class="btn-secondary btn-danger" onclick="handleSleepNow()">💤 Desligar Wi-Fi e Dormir</button>
      </div>
    </div>
  </div>

  <div class="toast" id="toast"></div>

  <script>
    let pulseMs = 800;

    function showToast(msg) {
      const t = document.getElementById('toast');
      t.innerText = msg;
      t.style.display = 'block';
      setTimeout(() => { t.style.display = 'none'; }, 3000);
    }

    function switchTab(t) {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
      event.target.classList.add('active');
      document.getElementById('tab-' + t).classList.add('active');
    }

    async function handleUnlock() {
      const btn = document.getElementById('btnUnlock');
      btn.disabled = true;
      btn.innerHTML = '<span>⏳ DESTRAVANDO...</span>';

      try {
        await fetch('/api/unlock', { method: 'POST' });
        document.getElementById('lockIcon').innerText = '🔓';
        document.getElementById('lockBadge').classList.add('unlocked');
        document.getElementById('lockBadgeText').innerText = 'DESTRAVADO';
        document.getElementById('heroTitle').innerText = 'Cofre Destravado!';
        showToast('Bobina ativada!');

        setTimeout(() => {
          document.getElementById('lockIcon').innerText = '🔒';
          document.getElementById('lockBadge').classList.remove('unlocked');
          document.getElementById('lockBadgeText').innerText = 'TRANCADO';
          document.getElementById('heroTitle').innerText = 'Cofre Trancado';
          btn.disabled = false;
          btn.innerHTML = '<span>⚡ DESTRAVAR AGORA</span>';
        }, pulseMs + 1000);
      } catch (e) {
        showToast('Erro ao comunicar com o cofre');
        btn.disabled = false;
        btn.innerHTML = '<span>⚡ DESTRAVAR AGORA</span>';
      }
    }

    async function handleChangePassword() {
      const curr = document.getElementById('currentPin').value;
      const n1 = document.getElementById('newPin').value;
      const n2 = document.getElementById('confirmPin').value;
      if (n1 !== n2) { showToast('Senhas não conferem!'); return; }
      const res = await fetch(`/api/password?curr=${encodeURIComponent(curr)}&new=${encodeURIComponent(n1)}`, { method: 'POST' });
      if (res.ok) {
        showToast('Senha atualizada com sucesso!');
        document.getElementById('currentPin').value = '';
        document.getElementById('newPin').value = '';
        document.getElementById('confirmPin').value = '';
      } else {
        showToast('Erro: Senha atual incorreta');
      }
    }

    function handleSaveConfig() {
      const p = parseInt(document.getElementById('pulseTimeInput').value);
      pulseMs = p;
      fetch(`/api/config?pulse=${p}`, { method: 'POST' });
      document.getElementById('pulseDisplay').innerText = p + ' ms';
      showToast('Configurações salvas!');
    }

    function handleSleepNow() {
      if (confirm('Desligar Wi-Fi e colocar o cofre em repouso profundo agora?')) {
        fetch('/api/sleep', { method: 'POST' });
        document.body.innerHTML = '<div style="text-align:center;margin-top:80px;color:#94a3b8;"><h2>💤 Cofre em Deep Sleep</h2><p>Wi-Fi desligado. Digite *000# no teclado para religar.</p></div>';
      }
    }

    async function loadStatus() {
      try {
        const res = await fetch('/api/status').then(r => r.json());
        if (res) {
          if (res.pulseMs) {
            pulseMs = res.pulseMs;
            document.getElementById('pulseDisplay').innerText = pulseMs + ' ms';
            document.getElementById('pulseTimeInput').value = pulseMs;
          }
          if (res.batteryV) {
            document.getElementById('batVoltage').innerText = res.batteryV.toFixed(1) + ' V';
          }
        }
      } catch(e) {}
    }
    loadStatus();
  </script>
</body>
</html>
)rawliteral";

#endif
