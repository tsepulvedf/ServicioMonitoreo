#!/usr/bin/env bash

set -euo pipefail

echo "[1/6] Actualizando indice de paquetes..."
sudo apt-get update

echo "[2/6] Instalando dependencias base..."
sudo apt-get install -y ca-certificates curl

echo "[3/6] Configurando repositorio oficial de Docker..."
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

sudo tee /etc/apt/sources.list.d/docker.sources > /dev/null <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Signed-By: /etc/apt/keyrings/docker.asc
EOF

echo "[4/6] Actualizando indice con el repositorio de Docker..."
sudo apt-get update

echo "[5/6] Instalando Docker Engine y Docker Compose Plugin..."
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

echo "[6/6] Habilitando servicio Docker y permisos para el usuario actual..."
sudo systemctl enable docker
sudo systemctl start docker
sudo usermod -aG docker "$USER"

echo "Instalacion completada."
echo "Puedes verificar la instalacion con: docker --version && docker compose version"
echo "Cierra la sesion SSH y vuelve a entrar para que el grupo docker aplique al usuario actual."
