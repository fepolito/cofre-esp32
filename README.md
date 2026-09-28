# 🔐 Retrofit de Cofre Eletrônico com ESP32-C3

Projeto de substituição da placa controladora de um cofre eletrônico residencial/comercial por um microcontrolador **ESP32-C3 Pro Mini / Super Mini (ESP32-C3FH4)**, mantendo o mecanismo mecânico, o solenoide original e o teclado da porta frontal, adicionando acesso de emergência e gestão via **Wi-Fi Captive Portal**.

---

## 🎯 Objetivos Principais

1. **Abertura Local Primária:** Operação por teclado numérico existente com senhas customizáveis (4 a 8 dígitos) e feedback por buzzer e LEDs.
2. **Interface Sem App (Captive Portal):** Ao acionar uma combinação no teclado (ex: `*000#`), o ESP32 sobe uma rede Wi-Fi que abre automaticamente uma página Web no smartphone para configuração e destravamento de emergência.
3. **Eficiência Energética Estrita:** Operação em *Deep Sleep* contínuo com consumo na faixa de microamperes ($\sim 15\ \mu\text{A}$), acordando apenas ao pressionar as teclas (*GPIO Wakeup*).
4. **Segurança Reforçada:** Bloqueio progressivo anti-força bruta (após 3 e 5 tentativas erradas) e persistência de credenciais com hash criptográfico.

---

## 📂 Estrutura do Repositório

```text
Cofre/
├── .gitignore
├── README.md
├── RDC_Cofre_Eletronico_ESP32.md  # Requisitos e Diretrizes de Concepção
├── docs/
│   └── RDC_Cofre_Eletronico_ESP32.md
├── firmware/                       # Código-fonte (Arduino / ESP-IDF / PlatformIO)
└── hardware/                       # Diagramas de circuito, pinout e fotos de engenharia reversa
```

---

## 📋 Documentação

Consulte o documento completo de especificações:
* 📄 [RDC - Requisitos e Diretrizes de Concepção](RDC_Cofre_Eletronico_ESP32.md)

---

## 🛠️ Tecnologias e Componentes

* **MCU:** ESP32-C3FH4 (32-bit RISC-V 160MHz, Wi-Fi 2.4GHz, BLE 5, 4MB Flash embutida)
* **Atuador:** Solenoide eletromecânico original (9V / 5V DC)
* **Driver:** MOSFET N-Channel de nível lógico (ou transistor original aproveitado) + Diodo Flyback (1N4007 / SS34)
* **Teclado:** Teclado numérico original integrado na porta
* **Alimentação:** Bateria 9V com LDO eficiente / pack de baterias com Wakeup por interrupção
