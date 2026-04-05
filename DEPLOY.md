# Despliegue en AWS

Este documento describe como desplegar el sistema IoT en una instancia EC2 usando Docker y `docker-compose`.

## Requisitos previos

- Cuenta de AWS con permisos para EC2 y Route 53.
- Un dominio administrado en Route 53 o acceso al panel DNS del dominio.
- Una key pair descargada localmente para conectarte por SSH.

## 1. Crear la instancia EC2

1. Entra a la consola de AWS y abre el servicio EC2.
2. Haz clic en `Launch instance`.
3. Nombre sugerido: `iot-monitoring-server`.
4. AMI recomendada: `Ubuntu Server 22.04 LTS`.
5. Tipo de instancia: `t2.micro`.
6. Crea o selecciona una `key pair`.
7. Guarda la llave `.pem` en tu equipo.

## 2. Configurar el Security Group

Configura solo los puertos necesarios:

- `22/tcp` para SSH.
- `8080/tcp` para la interfaz HTTP.
- `9000/tcp` para IOTP.
- `9001/tcp` para el auth service.

Recomendaciones:

- Restringe `22/tcp` a tu IP publica.
- Si el auth service solo se usara dentro de la instancia, puedes dejar `9001/tcp` abierto solo si necesitas probarlo desde fuera; de lo contrario, documentalo pero no lo expongas publicamente.

## 3. Asignar una IP elastica

1. En EC2, abre `Elastic IPs`.
2. Reserva una nueva IP elastica.
3. Asociarla a la instancia recien creada.
4. Toma nota de la IP publica resultante.

## 4. Conectarte por SSH

Desde tu maquina local:

```bash
chmod 400 tu-llave.pem
ssh -i tu-llave.pem ubuntu@TU_IP_ELASTICA
```

## 5. Instalar Docker y Docker Compose

Puedes hacerlo manualmente o ejecutando el script del repo:

```bash
git clone <URL_DEL_REPOSITORIO>.git
cd <NOMBRE_DEL_REPOSITORIO>
chmod +x infra/setup_ec2.sh
./infra/setup_ec2.sh
```

El script instala:

- Docker Engine
- Docker CLI
- containerd
- Docker Compose Plugin

## 6. Levantar el sistema con docker-compose

Dentro del repositorio:

```bash
mkdir -p logs
docker compose up -d --build
```

Esto levantara:

- `auth` en el puerto `9001`
- `server` en `9000` para IOTP y `8080` para HTTP

La resolucion entre contenedores se hace por nombre de servicio Docker, por eso el servidor usa `AUTH_HOST=auth`.

## 7. Configurar Route 53

1. Abre `Route 53`.
2. Entra a tu hosted zone.
3. Crea un registro `A`.
4. Nombre sugerido: `iot` o el subdominio que prefieras.
5. Valor: la IP elastica de tu instancia.
6. TTL sugerido durante desarrollo: `300`.

Ejemplo:

- `iot.tudominio.com -> TU_IP_ELASTICA`

## 8. Verificar el despliegue

### Desde navegador

Abre:

- `http://TU_DOMINIO_O_IP:8080/`
- `http://TU_DOMINIO_O_IP:8080/dashboard`
- `http://TU_DOMINIO_O_IP:8080/status`

### Desde clientes

Sensores:

```bash
python clients/sensors/sensor_simulator.py --host TU_DOMINIO --port 9000 --count 5 --interval 3 --chaos
```

Operador GUI:

- Configura el operador para conectarse al hostname publicado y al puerto `9000`.

## 9. Comandos utiles

Ver contenedores:

```bash
docker compose ps
```

Ver logs del servidor:

```bash
docker compose logs -f server
```

Ver logs del auth service:

```bash
docker compose logs -f auth
```

Reiniciar servicios:

```bash
docker compose restart
```

Reconstruir y levantar de nuevo:

```bash
docker compose up -d --build
```

Detener todo:

```bash
docker compose down
```

Eliminar contenedores e imagenes no usadas:

```bash
docker system prune -f
```

## 10. Notas operativas

- El volumen `./logs:/app/logs` conserva los logs del servidor fuera del contenedor.
- La imagen del servidor usa multi-stage build para mantener el runtime pequeno.
- Los unicos puertos documentados para este proyecto son `22`, `8080`, `9000` y `9001`.
