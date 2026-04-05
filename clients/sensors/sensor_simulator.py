#!/usr/bin/env python3
"""Simulador de sensores IoT para el proyecto de monitoreo."""

from __future__ import annotations

import argparse
import ipaddress
import json
import logging
import random
import signal
import socket
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_COUNT = 5
DEFAULT_INTERVAL = 3.0
DEFAULT_TIMEOUT = 30.0
DEFAULT_PASSWORD = "sensor123"
MAX_RETRIES = 5
CONFIG_PATH = Path(__file__).with_name("config.json")

SENSOR_TYPES = (
    {"protocol_type": "temperatura", "prefix": "temp"},
    {"protocol_type": "vibracion", "prefix": "vib"},
    {"protocol_type": "energia", "prefix": "energy"},
)


class SensorError(Exception):
    """Error base del simulador."""


class AuthenticationRejected(SensorError):
    """El servidor rechazo el registro del sensor."""


class ConnectionLost(SensorError):
    """La conexion con el servidor se perdio."""


class ProtocolError(SensorError):
    """La respuesta del servidor no cumple el protocolo esperado."""


@dataclass(frozen=True)
class SensorIdentity:
    """Representa el identificador y el tipo de un sensor simulado."""

    sensor_id: str
    sensor_type: str


def configure_logging() -> None:
    """Configura logs hacia stdout."""
    logging.basicConfig(
        level=logging.INFO,
        format="[%(asctime)s] [%(levelname)s] [%(threadName)s] %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%SZ",
    )


def load_config(path: Path) -> dict[str, dict[str, float]]:
    """Carga el archivo de configuracion con rangos por tipo."""
    with path.open("r", encoding="ascii") as file:
        data = json.load(file)

    if not isinstance(data, dict):
        raise ValueError("config.json debe contener un objeto JSON")

    required_keys = {
        "temperatura": ("normal_min", "normal_max", "alert_low", "alert_high"),
        "vibracion": ("normal_min", "normal_max", "alert_high"),
        "energia": ("normal_min", "normal_max", "alert_high"),
    }

    for sensor_type, keys in required_keys.items():
        if sensor_type not in data or not isinstance(data[sensor_type], dict):
            raise ValueError(f"Falta la configuracion del tipo {sensor_type}")
        for key in keys:
            if key not in data[sensor_type]:
                raise ValueError(f"Falta la clave {key} para el tipo {sensor_type}")

    return data


def validate_hostname(host: str) -> None:
    """Verifica que el argumento sea un hostname y no una IP literal."""
    try:
        ipaddress.ip_address(host)
    except ValueError:
        return
    raise ValueError("El parametro --host debe ser un hostname, no una IP literal")


def parse_args() -> argparse.Namespace:
    """Parsea argumentos de linea de comandos."""
    parser = argparse.ArgumentParser(description="Simulador de sensores IoT")
    parser.add_argument("--host", required=True, help="Hostname del servidor IOTP")
    parser.add_argument("--port", required=True, type=int, help="Puerto TCP del servidor IOTP")
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT, help="Cantidad de sensores a lanzar")
    parser.add_argument(
        "--interval",
        type=float,
        default=DEFAULT_INTERVAL,
        help="Segundos entre envios de medicion",
    )
    parser.add_argument("--chaos", action="store_true", help="Activa inyeccion periodica de valores anomalos")
    return parser.parse_args()


def build_sensor_identity(index: int) -> SensorIdentity:
    """Genera IDs unicos con asignacion round-robin por tipo."""
    sensor_info = SENSOR_TYPES[(index - 1) % len(SENSOR_TYPES)]
    return SensorIdentity(
        sensor_id=f"{sensor_info['prefix']}_{index:03d}",
        sensor_type=sensor_info["protocol_type"],
    )


def resolve_addresses(host: str, port: int) -> list[tuple[Any, ...]]:
    """Resuelve el hostname usando getaddrinfo antes de conectar."""
    return socket.getaddrinfo(
        host,
        port,
        family=socket.AF_UNSPEC,
        type=socket.SOCK_STREAM,
        proto=socket.IPPROTO_TCP,
    )


def connect_with_resolution(host: str, port: int, timeout: float) -> socket.socket:
    """Intenta conectar recorriendo todas las direcciones resueltas."""
    last_error: OSError | None = None

    for family, socktype, proto, _, sockaddr in resolve_addresses(host, port):
        client_socket: socket.socket | None = None
        try:
            client_socket = socket.socket(family, socktype, proto)
            client_socket.settimeout(timeout)
            client_socket.connect(sockaddr)
            return client_socket
        except OSError as exc:
            last_error = exc
            if client_socket is not None:
                try:
                    client_socket.close()
                except OSError:
                    pass

    if last_error is None:
        raise OSError("No fue posible resolver o conectar al servidor")
    raise last_error


def recv_line(client_socket: socket.socket) -> str:
    """Lee una linea ASCII terminada en CRLF."""
    buffer = bytearray()

    while len(buffer) < 1024:
        try:
            chunk = client_socket.recv(1)
        except socket.timeout as exc:
            raise ConnectionLost("Timeout esperando respuesta del servidor") from exc
        except OSError as exc:
            raise ConnectionLost(f"Fallo de recepcion: {exc}") from exc

        if not chunk:
            raise ConnectionLost("Conexion cerrada por el servidor")

        buffer.extend(chunk)
        if len(buffer) >= 2 and buffer[-2:] == b"\r\n":
            try:
                return bytes(buffer[:-2]).decode("ascii")
            except UnicodeDecodeError as exc:
                raise ProtocolError("Respuesta no ASCII del servidor") from exc

    raise ProtocolError("Respuesta demasiado larga")


def send_line(client_socket: socket.socket, message: str) -> None:
    """Envia una linea completa del protocolo IOTP."""
    try:
        client_socket.sendall(message.encode("ascii"))
    except OSError as exc:
        raise ConnectionLost(f"Fallo de envio: {exc}") from exc


def iso_timestamp() -> str:
    """Genera un timestamp UTC en formato ISO-8601."""
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def generate_measurement(
    sensor_type: str,
    config: dict[str, dict[str, float]],
    chaos_enabled: bool,
    send_count: int,
) -> float:
    """Genera una medicion normal o anomala segun el modo caos."""
    sensor_config = config[sensor_type]
    normal_min = float(sensor_config["normal_min"])
    normal_max = float(sensor_config["normal_max"])

    if chaos_enabled and send_count > 0 and send_count % 10 == 0:
        if sensor_type == "temperatura":
            if random.choice((True, False)):
                return round(random.uniform(float(sensor_config["alert_high"]) + 1.0, normal_max + 15.0), 2)
            return round(random.uniform(normal_min - 10.0, float(sensor_config["alert_low"]) - 1.0), 2)

        if sensor_type == "vibracion":
            return round(random.uniform(float(sensor_config["alert_high"]) + 1.0, normal_max + 10.0), 2)

        if sensor_type == "energia":
            return round(random.uniform(float(sensor_config["alert_high"]) + 1.0, normal_max + 80.0), 2)

    return round(random.uniform(normal_min, normal_max), 2)


def expect_ack(client_socket: socket.socket, stage: str) -> str:
    """Espera una respuesta del servidor y valida que sea ACK."""
    response = recv_line(client_socket)
    if response.startswith("ACK|"):
        return response
    if response.startswith("ERROR|"):
        if stage == "register":
            raise AuthenticationRejected(response)
        raise ProtocolError(f"El servidor devolvio error durante {stage}: {response}")
    raise ProtocolError(f"Respuesta inesperada durante {stage}: {response}")


def send_disconnect(client_socket: socket.socket, sensor_id: str) -> None:
    """Intenta cerrar la sesion de forma limpia."""
    try:
        send_line(client_socket, f"DISCONNECT|{sensor_id}\r\n")
        response = recv_line(client_socket)
        if not response.startswith("ACK|"):
            logging.warning("sensor=%s respuesta inesperada en disconnect: %s", sensor_id, response)
    except SensorError as exc:
        logging.warning("sensor=%s no se pudo cerrar limpiamente: %s", sensor_id, str(exc))


class SensorWorker(threading.Thread):
    """Hilo que representa un sensor independiente."""

    def __init__(
        self,
        identity: SensorIdentity,
        host: str,
        port: int,
        interval: float,
        chaos_enabled: bool,
        config: dict[str, dict[str, float]],
        stop_event: threading.Event,
    ) -> None:
        super().__init__(name=identity.sensor_id)
        self.identity = identity
        self.host = host
        self.port = port
        self.interval = interval
        self.chaos_enabled = chaos_enabled
        self.config = config
        self.stop_event = stop_event
        self.send_count = 0

    def run(self) -> None:
        """Mantiene la sesion del sensor con reintentos ante perdida de conexion."""
        attempt = 0

        while not self.stop_event.is_set():
            try:
                self.run_session()
                return
            except AuthenticationRejected as exc:
                logging.error("sensor=%s autenticacion rechazada: %s", self.identity.sensor_id, str(exc))
                return
            except (ConnectionLost, OSError, socket.gaierror, ProtocolError) as exc:
                attempt += 1
                if attempt > MAX_RETRIES or self.stop_event.is_set():
                    logging.error(
                        "sensor=%s agotado maximo de reintentos (%s): %s",
                        self.identity.sensor_id,
                        MAX_RETRIES,
                        str(exc),
                    )
                    return

                backoff = min(2 ** (attempt - 1), 16)
                logging.warning(
                    "sensor=%s reintentando conexion intento=%s espera=%ss detalle=%s",
                    self.identity.sensor_id,
                    attempt,
                    backoff,
                    str(exc),
                )
                self.stop_event.wait(backoff)

    def run_session(self) -> None:
        """Ejecuta una sesion completa de REGISTER, DATA y DISCONNECT."""
        registered = False
        client_socket: socket.socket | None = None

        try:
            logging.info("sensor=%s resolviendo host=%s puerto=%s", self.identity.sensor_id, self.host, self.port)
            client_socket = connect_with_resolution(self.host, self.port, DEFAULT_TIMEOUT)
            logging.info("sensor=%s conexion establecida", self.identity.sensor_id)

            register_message = f"REGISTER|sensor|{self.identity.sensor_id}|{DEFAULT_PASSWORD}\r\n"
            logging.info("sensor=%s enviando REGISTER", self.identity.sensor_id)
            send_line(client_socket, register_message)
            register_response = expect_ack(client_socket, "register")
            registered = True
            logging.info("sensor=%s registro exitoso respuesta=%s", self.identity.sensor_id, register_response)

            while not self.stop_event.is_set():
                self.send_count += 1
                value = generate_measurement(
                    self.identity.sensor_type,
                    self.config,
                    self.chaos_enabled,
                    self.send_count,
                )
                timestamp = iso_timestamp()
                data_message = (
                    f"DATA|{self.identity.sensor_id}|{self.identity.sensor_type}|{value:.2f}|{timestamp}\r\n"
                )

                logging.info(
                    "sensor=%s enviando DATA tipo=%s valor=%.2f",
                    self.identity.sensor_id,
                    self.identity.sensor_type,
                    value,
                )
                send_line(client_socket, data_message)
                data_response = expect_ack(client_socket, "data")
                logging.info("sensor=%s ACK recibido=%s", self.identity.sensor_id, data_response)

                if self.stop_event.wait(self.interval):
                    break
        finally:
            if client_socket is not None:
                if registered:
                    send_disconnect(client_socket, self.identity.sensor_id)
                try:
                    client_socket.close()
                except OSError:
                    pass
                logging.info("sensor=%s socket cerrado", self.identity.sensor_id)


def install_signal_handlers(stop_event: threading.Event) -> None:
    """Instala manejadores para detener todos los hilos limpiamente."""

    def handle_signal(signum: int, _frame: Any) -> None:
        logging.info("senal recibida=%s iniciando apagado limpio", signum)
        stop_event.set()

    signal.signal(signal.SIGINT, handle_signal)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, handle_signal)


def main() -> None:
    """Punto de entrada del simulador."""
    configure_logging()
    try:
        args = parse_args()
        validate_hostname(args.host)

        if args.count <= 0:
            raise ValueError("--count debe ser mayor que 0")
        if args.interval <= 0:
            raise ValueError("--interval debe ser mayor que 0")

        config = load_config(CONFIG_PATH)
        stop_event = threading.Event()
        install_signal_handlers(stop_event)

        workers: list[SensorWorker] = []
        for index in range(1, args.count + 1):
            identity = build_sensor_identity(index)
            worker = SensorWorker(
                identity=identity,
                host=args.host,
                port=args.port,
                interval=args.interval,
                chaos_enabled=args.chaos,
                config=config,
                stop_event=stop_event,
            )
            workers.append(worker)
            worker.start()

        try:
            for worker in workers:
                while worker.is_alive():
                    worker.join(timeout=0.5)
        except KeyboardInterrupt:
            logging.info("interrupcion manual recibida, deteniendo sensores")
            stop_event.set()
        finally:
            stop_event.set()
            for worker in workers:
                worker.join()
    except ValueError as exc:
        logging.error("Configuracion invalida: %s", str(exc))
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
