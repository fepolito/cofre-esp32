# RDC - Requisitos e Diretrizes de Concepção
## Projeto: Retrofit Inteligente de Cofre Eletrônico com ESP32-C3

---

| **Documento** | Requisitos e Diretrizes de Concepção (RDC) |
| :--- | :--- |
| **Projeto** | Cofre Eletrônico Inteligente (Retrofit) |
| **Autor** | Fernando Polito |
| **Microcontrolador** | ESP32-C3FH4 (Placa ESP32-C3 Super Mini / Pro Mini) |
| **Hardware Original** | Placa Cofre SC421630P (Motorola DIP-16) |
| **Status** | **Versão 2.1 - Validação Completa em Bateria, Buffer de 2200µF, Correções de Teclado e Auditoria Persistente** |
| **Data** | Outubro de 2026 |

---

## 1. Contexto e Justificativa

O cofre eletrônico em questão possui mecanismo de travamento acionado por solenoide (bobina eletromecânica) acoplado a um teclado numérico físico e placa de controle alimentada por bateria de 9V. Devido à perda da senha mestre/usuário e à descontinuação do fabricante original, o cofre encontrava-se inoperante.

O objetivo do projeto foi realizar o **retrofit completo da eletrônica de controle**, preservando a integridade física do cofre (chassi, furação, solenoide original, LEDs, buzzer e teclado matricial), substituindo o microcontrolador original obsoleto por um **ESP32-C3FH4 Super Mini**.

O novo sistema entrega com sucesso comprovado em bancada e na bateria:
1. **Operação Primária Local:** Abertura via teclado físico (12 teclas: 0-9, C, P) com resposta instantânea e bips sonoros sem diafonia (anti-crosstalk).
2. **Interface de Gestão Sem App (Wi-Fi Captive Portal):** Painel web responsivo (modo escuro) servido diretamente pelo ESP32 via navegador (Android, iOS, PC).
3. **Segurança Reforçada & Zero-Knowledge:**
   - Senhas personalizadas salvas na memória Flash não-volátil (`Preferences`).
   - Eliminação automática da senha temporária de fábrica (`123456`) após cadastro da senha mestre definitiva.
   - **Chave Mestre de Resgate por MAC:** Token de 6 dígitos calculado offline exclusivamente para o chip do usuário (`735569`), permitindo destravamento mesmo com perda total de senhas.
4. **Gerenciamento de Energia e Estabilidade Elétrica:**
   - Inclusão do **capacitor eletrolítico de 2200 µF 10V** em paralelo com o barramento de alimentação lógica, sanando integralmente a queda de tensão durante as rajadas de Wi-Fi e o disparo da bobina na bateria de 9V.
   - Otimização de RF (+5 dBm), clock reduzido (80 MHz) e compressão GZIP nativa da interface para operação segura.

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
                         │       │  (LDO 3.3V)      (Q1 / Q2 2N4401)
                         │       ▼                     ▲
                         ├───► [ 2200µF Buffer ]       │ Pino 7
                         │                             │
                         ├───► [ LEDs D4/D5 Anodo ]    │
                         │                             │
                         └───► [ Buzzer BZ1 / Q3 ] ────┴── [ ESP32-C3 ]
```

### 2.1. O Microcontrolador Original
* **Identificação:** Motorola `SC421630P` em encapsulamento DIP-16 (microcontrolador dedicado de 8 bits com ROM mascarada).
* **Solução de Retrofit:** O CI obsoleto foi dessoldado da placa. Os pontos de solda (ilhas DIP-16) foram aproveitados como pontos de conexão direta com os pinos da placa ESP32-C3 Super Mini.

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

### 2.5. O Gargalo do Regulador S-812C e a Solução do Capacitor Buffer
* **Regulador Original:** Seiko Instruments **S-812C** (saída regulada de 5.33V, corrente máxima de saída ~50 mA).
* **Problema Identificado na Bateria:** O ESP32 em transmissão Wi-Fi e acionamento de periféricos consome picos de corrente que superavam a capacidade de 50 mA do regulador original com seu capacitor de fábrica de apenas 3.3 µF (`C1`), causando brownout.
* **Solução Comprovada em Bancada:** A adição de um **capacitor eletrolítico de 2200 µF 10V** em paralelo com a linha de alimentação de 5V forneceu a capacitância de reservatório necessária para sustentar a transmissão do captive portal e o pulso do solenoide diretamente pela bateria de 9V.

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
| **Alimentação VDD** | `5V (VIN)` | Pino 12 VDD (Saída 5.33V do S-812C c/ 2200µF) | Alimentação da Placa |
| **Terra (GND)** | `GND` | Pino 18 GND (Malha Comum) | Referência Comum |

---

## 4. Arquitetura de Software e Firmware

### 4.1. Stack Tecnológico
* **Plataforma:** PlatformIO Core 6.2+
* **Framework:** Arduino-ESP32 (Core RISC-V 32-bit)
* **Bibliotecas Principais:** `WiFi`, `WebServer`, `DNSServer`, `Preferences` (NVS Flash), `Keypad`.

### 4.2. Correção de Diafonia do Teclado (Anti-Crosstalk)
* **Problema:** Como a Linha 1 da matriz compartilha a linha com a base do transistor do buzzer (GPIO 4), tocar o buzzer enquanto o usuário mantinha a tecla pressionada injetava o tom de 2,7 kHz diretamente nas colunas, corrompendo a leitura e gerando senhas erradas no buffer.
* **Solução:** O firmware separa as leituras em `PRESSED` e `RELEASED`. A captura do dígito é feita no `PRESSED` com o circuito em silêncio. O bip de confirmação sonora é emitido apenas no `RELEASED` (quando o contato mecânico já abriu), isolando completamente o áudio da matriz. O tempo de debounce foi ajustado em 40 ms para imunidade a ruídos de RF.

### 4.3. Histórico de Auditoria Persistente em Flash (`Preferences`)
* Os eventos de abertura, tentativas e alterações administrativas são gravados em namespace não-volátil (`cofre_logs`).
* O histórico é recarregado automaticamente na inicialização ou ao despertar do sono profundo, eliminando o problema de perda de logs.

### 4.4. Gestão Dinâmica do Administrador Mestre
* Possibilidade de renomear e redefinir a senha do Administrador Mestre diretamente no portal Web via `/api/change_master`.
* Eliminação automática do PIN padrão `123456`.

---

## 5. Diretrizes de Autonomia Energética (Deep Sleep e o LED de Power)

### 5.1. Análise do Consumo do LED de Power On-Board
* A placa ESP32-C3 Super Mini possui um LED vermelho de **Power (PWR)** soldado diretamente entre a linha de 3.3V e o terra (GND) através de um resistor limitador.
* **Consumo medido:** $\approx 1.2\text{ mA a } 1.5\text{ mA}$ ($1500\ \mu\text{A}$).
* **Consumo do ESP32-C3 em Deep Sleep:** $\approx 15\ \mu\text{A}$.
* **Conclusão:** O LED de Power consome **100 vezes mais corrente** que todo o microcontrolador em repouso!
  * **Com o LED ativo:** Bateria de 9V (500 mAh) dura cerca de **14 dias**.
  * **Com o LED removido:** Bateria de 9V dura **1 a 2 anos**.

> **Recomendação Construtiva:** Na montagem final dentro da caixa do cofre, remover ou raspar o pequeno LED vermelho de PWR (ou seu resistor SMD associado) com a ponta do ferro de solda para atingir a autonomia máxima de bateria.

---

## 6. Histórico de Versões

| Versão | Data | Principais Entregas |
| :--- | :--- | :--- |
| **1.0** | 29/09/2026 | Concepção inicial, levantamento de requisitos e diretrizes de projeto. |
| **2.0** | 30/09/2026 | Engenharia reversa no KiCad 10, pinagem validada em bancada, firmware REST e expurgo da senha `123456`. |
| **2.1** | 01/10/2026 | Validação da bateria com capacitor de 2200µF, correção de anti-crosstalk no teclado, logs persistentes na Flash e auditoria de consumo do LED. |
