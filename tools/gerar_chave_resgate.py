#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gerador Offline Privado de Chave Mestre de Resgate por Hardware (ESP32-C3)
Autor: Fernando Polito

Este script roda EXCLUSIVAMENTE no seu computador pessoal (offline).
Ele calcula o PIN numérico de emergência de 6 dígitos baseado no MAC físico do seu ESP32.
Guarde o código gerado em local seguro (carteira, gerenciador de senhas, documento físico).
"""

import hashlib
import sys

# Segredo criptográfico privado do projeto (deve ser idêntico ao do firmware main.cpp)
SECRET_SALT = "COFRE_POLITO_SECURE_2026"

def calculate_rescue_pin(mac_address: str, salt: str = SECRET_SALT) -> str:
    clean_mac = mac_address.replace(":", "").replace("-", "").strip().upper()
    data = (clean_mac + salt).encode('utf-8')
    digest = hashlib.sha256(data).hexdigest()
    # Pega os primeiros 8 caracteres hexadecimais e converte para número
    num = int(digest[:8], 16)
    # Extrai 6 dígitos numéricos
    pin = f"{num % 1000000:06d}"
    return pin

def main():
    print("=" * 60)
    print("[CHAVE DE RESGATE] GERADOR OFFLINE DE CHAVE MESTRE (ESP32)")
    print("=" * 60)
    
    if len(sys.argv) > 1:
        mac = sys.argv[1]
    else:
        mac = input("Digite o endereco MAC do seu ESP32 (ex: 7C:DF:A1:34:B8:2E): ").strip()
        if not mac:
            mac = "7C:DF:A1:34:B8:2E"
            print(f"-> Usando MAC de teste: {mac}")

    rescue_pin = calculate_rescue_pin(mac)
    
    print("\n" + "-" * 60)
    print(f"Endereco MAC:               {mac.upper()}")
    print(f"Chave Mestre de Resgate:    {rescue_pin}")
    print("-" * 60)
    print("INSTRUCOES DE SEGURANCA:")
    print("1. Anote esta Chave de Resgate de 6 digitos em local seguro fora do cofre.")
    print("2. O Captive Portal do cofre NUNCA exibe este codigo.")
    print("3. Se voce esquecer a senha mestre com o cofre trancado:")
    print(f"   - Digite '{rescue_pin}#' diretamente no teclado fisico da porta.")
    print(f"   - OU conecte no Wi-Fi (*000#) e digite '{rescue_pin}' na aba de Resgate.")
    print("=" * 60 + "\n")

if __name__ == "__main__":
    main()
