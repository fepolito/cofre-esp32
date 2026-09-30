# RDC - Requisitos e Diretrizes de Concepção
## Projeto: Retrofit Inteligente de Cofre Eletrônico com ESP32-C3

---

| **Documento** | Requisitos e Diretrizes de Concepção (RDC) |
| :--- | :--- |
| **Projeto** | Cofre Eletrônico Inteligente (Retrofit) |
| **Autor** | Fernando Polito |
| **Microcontrolador** | ESP32-C3FH4 (Placa ESP32-C3 Super Mini / Pro Mini) |
| **Hardware Original** | Placa Cofre SC421630P (Motorola DIP-16) |
| **Status** | **Versão 2.0 - Engenharia Reversa, Protótipo e Firmware Validados em Bancada** |
| **Data** | Setembro de 2026 |

---

## 1. Contexto e Justificativa

O cofre eletrônico em questão possui mecanismo de travamento acionado por solenoide (bobina eletromecânica) acoplado a um teclado numérico físico e placa de controle alimentada por bateria de 9V. Devido à perda da senha mestre/usuário e à descontinuação do fabricante original, o cofre encontrava-se inoperante.

O objetivo do projeto foi realizar o **retrofit completo da eletrônica de controle**, preservando a integridade física do cofre (chassi, furação, solenoide original, LEDs, buzzer e teclado matricial), substituindo o microcontrolador original obsoleto por um **ESP32-C3FH4 Super Mini**.

O novo sistema entrega com sucesso comprovado em bancada:
1. **Operação Primária Local:** Abertura via teclado físico (12 teclas: 0-9, C, P) com resposta instantânea e bips sonoros.
2. **Interface de Gestão Sem App (Wi-Fi Captive Portal):** Painel web responsivo (modo escuro) servido diretamente pelo ESP32 via navegador (Android, iOS, PC).
3. **Segurança Reforçada & Zero-Knowledge:**
   - Senhas personalizadas salvas na memória Flash não-volátil (`Preferences`).
   - Eliminação automática da senha temporária de fábrica (`123456`) após cadastro da senha mestre definitiva.
   - **Chave Mestre de Resgate por MAC:** Token de 6 dígitos calculado offline exclusivamente para o chip do usuário, permitindo destravamento mesmo com perda total de senhas.
4. **Gerenciamento de Energia:** Otimização agressiva de RF (+5 dBm), clock reduzido (80 MHz) e compressão GZIP nativa da interface para operação segura com bateria.

---

## 2. Engenharia Reversa da Placa Original

A placa de circuito impresso original do cofre foi inteiramente mapeada e transcrita para o software **KiCad 10** (`hardware/cofre_esp32_00.kicad_sch`), permitindo a identificação precisa de cada nó elétrico e a substituição direta do microcontrolador original:

```
[ Bateria 9V ] ──────────┬─────────────────────────────┐
                         │                             │
                         ▼                             ▼
                [ Regulador S-812C ]          [ Bobina Solenoide ]
                  (Saída: 5.33V)                       │ (1N4934 Flyback)
                         │                             ▼
                         ├───► [ ESP32-C3 5V ]   [ Driver Darlington ]
                         │       (LDO 3.3V)         (Q1 / Q2 2N4401)
                         │                             ▲
                         ├───► [ LEDs D4/D5 Anodo ]    │ Pino 7
                         │                             │
                         └───► [ Buzzer BZ1 / Q3 ] ────┴── [ ESP32-C3 ]
```

### 2.1. O Microcontrolador Original
* **Identificação:** Motorola `SC421630P` em encapsulamento DIP-16 (microcontrolador dedicado de 8 bits com ROM mascarada).
* **Solução de Retrofit:** O CI obsoleto foi desolado da placa. Os pontos de solda (ilhas DIP-16) foram aproveitados como pontos de conexão direta com os pinos da placa ESP32-C3 Super Mini.

### 2.2. Driver de Potência da Bobina (Solenoide)
* **Topologia:** Par Darlington com 2 transistores NPN **2N4401** (`Q1` e `Q2`).
* **Acionamento:** Pino 10 do CI original através do resistor limitador de base `R2` (500 Ω).
* **Proteção Indutiva:** Diodo roda-livre (flyback) **1N4934** em antiparalelo com a bobina para absorver as tensões reversas de desenergização.
* **Alimentação:** A bobina é alimentada diretamente pelo barramento da bateria (9V).

### 2.3. Sinalização Sonora e Luminosa
* **Buzzer (`BZ1`):** Transistor PNP **2N4403** (`Q3`) ligado ao pino 2 do CI original. O acionamento é **Active-LOW** (puxar a base para GND através do resistor `R5` liga o buzzer). Compartilha o nó com a Linha 1 da matriz do teclado.
* **LEDs (`D4` Verde e `D5` Vermelho):** Anodos conectados ao barramento de 5.33V através de resistores limitadores de 1 kΩ (`R3` e `R4`). Catodos ligados aos pinos 11 e 12 do CI original (**Active-LOW**).

### 2.4. Teclado Físico
* **Matriz Digital de 12 Teclas (3 Linhas x 4 Colunas):**
  * **Linha 1:** Teclas `0`, `1`, `2`, `3`
  * **Linha 2:** Teclas `C` (Clear), `4`, `5`, `6`
  * **Linha 3:** Teclas `P` (Program/Enter), `7`, `8`, `9`
* Todas as chaves táteis conectam diretamente as linhas de varredura às colunas com pull-up interno.

### 2.5. Regulador de Tensão e Capacitor
* **Regulador:** Seiko Instruments **S-812C** (saída regulada de 5.33V, consumo em repouso de ~2 µA, corrente máxima de saída ~50 mA).
* **Capacitor de Filtro Original:** `C1` de apenas **3.3 µF**.
* *Impacto para o ESP32:* Suficiente para operação do teclado em repouso. Para tráfego de rádio Wi-Fi contínuo na bateria, foram implementadas reduções de potência no firmware (+5 dBm) e compressão GZIP, sendo recomendado um capacitor eletrolítico buffer (470 µF a 1000 µF) entre 5V e GND.

---

## 3. Mapeamento de Pinagem Final (ESP32-C3)

| Função | Pino ESP32-C3 | Conexão na Placa (DIP-16) | Nível Lógico / Operação |
| :--- | :--- | :--- | :--- |
| **Driver Solenoide** | `GPIO 7` | Pino 10 (Base Darlington Q1/Q2 via R2 500Ω) | HIGH = Aciona Bobina |
| **LED Verde** | `GPIO 8` | Pino 12 (Catodo D4 via R3 1kΩ) | LOW = Acende LED |
| **LED Vermelho** | `GPIO 10` | Pino 11 (Catodo D5 via R4 1kΩ) | LOW = Acende LED |
| **Linha 1 Matriz + Buzzer**| `GPIO 4` | Pino 2 (Linha 1 e Base PNP Q3) | Saída Varredura / Tom PWM Buzzer |
| **Linha 2 Matriz** | `GPIO 5` | Pino 3 (Linha 2: C, 4, 5, 6) | Saída Varredura |
| **Linha 3 Matriz** | `GPIO 6` | Pino 9 (Linha 3: P, 7, 8, 9) | Saída Varredura |
| **Coluna 1 Matriz** | `GPIO 0` | Pino 5 (Col 1: 0, C, P) | Entrada c/ Pull-Up & RTC Wakeup |
| **Coluna 2 Matriz** | `GPIO 1` | Pino 6 (Col 2: 1, 4, 7) | Entrada c/ Pull-Up & RTC Wakeup |
| **Coluna 3 Matriz** | `GPIO 2` | Pino 7 (Col 3: 2, 5, 8) | Entrada c/ Pull-Up & RTC Wakeup |
| **Coluna 4 Matriz** | `GPIO 3` | Pino 8 (Col 4: 3, 6, 9) | Entrada c/ Pull-Up & RTC Wakeup |
| **Alimentação VDD** | `5V (VIN)` | Pino 12 VDD (Saída 5.33V do S-812C) | Alimentação da Placa |
| **Terra (GND)** | `GND` | Pino 18 GND (Malha Comum) | Referência Comum |

---

## 4. Arquitetura de Software e Firmware

### 4.1. Stack Tecnológico
* **Plataforma:** PlatformIO Core 6.2+
* **Framework:** Arduino-ESP32 (Core RISC-V 32-bit)
* **Bibliotecas Principais:** `WiFi`, `WebServer`, `DNSServer`, `Preferences` (NVS Flash), `Keypad`.

### 4.2. Segurança e Gestão de Senhas
* **Usuário Mestre:** Criado na primeira execução com senha temporária `123456`.
* **Expurgo Automático de Fábrica:** Assim que o proprietário cadastra seu usuário definitivo (ex: `Fernando` / `38508104`), o firmware apaga a senha `123456` da Flash. Testes práticos confirmaram a rejeição imediata da senha antiga.
* **Chave Mestre de Resgate por MAC (Zero-Knowledge):**
  * MAC gravado no chip: `7C:4F:AD:F4:5A:F4`
  * Chave de Resgate derivada: **`735569`**
  * Permite destravar o cofre e restaurar credenciais a qualquer momento pelo teclado ou web portal.

### 4.3. Interface Web Captive Portal Otimizada
* **Tamanho Otimizado:** Redução de 30.5 KB para **7.2 KB** através de compressão **GZIP** embutida em Flash (`PROGMEM`).
* **Potência de Rádio:** Configurada em **`WIFI_POWER_5dBm`** (~3.1 mW), garantindo alcance de 5 a 10 metros e poupando a bateria de 9V contra brownouts.
* **Frequência da CPU:** Clock ajustado em **80 MHz** (-50% consumo digital).
* **Rotas REST Implementadas:**
  * `GET /` -> Interface Web completa (GZIP).
  * `GET /api/status` -> Diagnóstico, usuários, logs e status da senha mestre.
  * `POST /api/unlock` -> Autenticação e retração da bobina.
  * `POST /api/change_master_pin` -> Atualização instantânea da Senha Mestre.
  * `POST /api/add_user` -> Inclusão de usuários adicionais.
  * `POST /api/delete_user` -> Remoção de acessos secundários.
  * `POST /api/config` -> Calibração do tempo de pulso da bobina (200ms a 3000ms).
  * `POST /api/emergency_reset` -> Restauração de emergência com a Chave MAC.
  * `POST /api/sleep` -> Transição manual para modo repouso.

---

## 5. Validação Prática em Bancada (Resultados Reais)

Durante os ensaios realizados na bancada com a placa interligada ao cofre, os seguintes eventos foram auditados e validados no log serial:

```text
[TECLADO] Tecla: 3
[TECLADO] Tecla: 0
[TECLADO] Tecla: C
[TECLADO] Tecla: P
[SOLENOIDE] Disparado!
[AUTH] Senha Mestre alterada para: 38508104 (123456 eliminada!)
[AUTH WEB] Tentativa com PIN: 38508104 -> Solenoide ATIVADO!
[AUTH WEB] Tentativa com PIN: 123456 -> ACESSO RECUSADO (Senha eliminada!)
```

* **Mapeamento Mecânico:** Retração do solenoide 100% responsiva em pulsos de 800ms.
* **Teclado:** Mapeamento 3x4 íntegro, todas as 12 teclas operando.
* **Persistência NVS:** Gravação e leitura em Flash validadas após múltiplos ciclos de reinicialização.
* **Portal Captive:** Acesso validado por smartphone sem necessidade de aplicativo.

---

## 6. Próximos Passos e Recomendações Construtivas

1. **Capacitor Buffer na Linha de 5V:** Soldar um capacitor eletrolítico de **470 µF a 1000 µF (16V)** entre os pinos `5V` e `GND` do ESP32-C3 Super Mini para absorver com folga qualquer flutuação de rádio Wi-Fi na bateria de 9V.
2. **Isolamento e Fixação:** Proteger as soldas com fita Kapton / espaguete termorretrátil e fixar a placa Super Mini no compartimento plástico do cofre.
3. **Ponto de Alimentação de Emergência:** Manter os contatos frontais originais de 9V (ou porta USB) limpos para permitir alimentação externa de socorro caso a bateria interna se esgote após meses de uso.
