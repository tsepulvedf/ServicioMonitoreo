#!/usr/bin/env python3
"""Servicio de autenticacion TCP para el proyecto IoT."""

from __future__ import annotations

import argparse
import json
import logging
import socket
import threading
from pathlib import Path
from typing import Dict, Tuple


BUFFER_SIZE = 1024
DEFAULT_PORT = 9001
USERS_FILE = Path(__file__).with_name("users.json")


UserRecord = Dict[str, str]
UserDatabase = Dict[str, UserRecord]


def configure_logging() -> None:
    """Configura logs hacia stdout sin exponer secretos."""
    logging.basicConfig(
        level=logging.INFO,
        format="[%(asctime)s] [auth-service] %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%SZ",
    )


def load_users(path: Path) -> UserDatabase:
    """Carga usuarios desde JSON y valida la estructura minima esperada."""
    with path.open("r", encoding="ascii") as file:
        data = json.load(file)

    if not isinstance(data, dict):
        raise ValueError("users.json debe contener un objeto JSON")

    for username, record in data.items():
        if not isinstance(username, str):
            raise ValueError("Cada username debe ser string")
        if not isinstance(record, dict):
            raise ValueError(f"El registro de {username} debe ser un objeto")
        if "password" not in record or "role" not in record:
            raise ValueError(f"El registro de {username} debe incluir password y role")
        if not isinstance(record["password"], str) or not isinstance(record["role"], str):
            raise ValueError(f"Los campos de {username} deben ser strings")

    return data


def create_server_socket(port: int) -> socket.socket:
    """Crea un socket TCP escuchando en todas las interfaces disponibles."""
    last_error: OSError | None = None
    addrinfo_list = socket.getaddrinfo(
        host=None,
        port=port,
        family=socket.AF_UNSPEC,
        type=socket.SOCK_STREAM,
        proto=socket.IPPROTO_TCP,
        flags=socket.AI_PASSIVE,
    )

    for family, socktype, proto, _, sockaddr in addrinfo_list:
        server_socket: socket.socket | None = None
        try:
            server_socket = socket.socket(family, socktype, proto)
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_socket.bind(sockaddr)
            server_socket.listen()
            return server_socket
        except OSError as exc:
            last_error = exc
            try:
                if server_socket is not None:
                    server_socket.close()
            except Exception:
                pass

    if last_error is None:
        raise OSError("No fue posible crear el socket servidor")
    raise last_error


def recv_line(connection: socket.socket) -> str:
    """Lee una sola linea terminada en CRLF."""
    chunks = bytearray()

    while len(chunks) < BUFFER_SIZE:
        data = connection.recv(1)
        if not data:
            raise ConnectionError("Conexion cerrada por el cliente")

        chunks.extend(data)
        if len(chunks) >= 2 and chunks[-2:] == b"\r\n":
            try:
                line = bytes(chunks[:-2]).decode("ascii")
            except UnicodeDecodeError as exc:
                raise ValueError("Mensaje no ASCII") from exc
            return line

    raise ValueError("Mensaje demasiado largo")


def send_response(connection: socket.socket, response: str) -> None:
    """Envia una respuesta completa controlando errores de escritura."""
    connection.sendall(response.encode("ascii"))


def parse_auth_request(message: str) -> Tuple[str, str]:
    """Parsea AUTH|username|password y devuelve usuario y password."""
    parts = message.split("|")
    if len(parts) != 3:
        raise ValueError("Cantidad de campos invalida")
    if parts[0] != "AUTH":
        raise ValueError("Opcode invalido")
    username, password = parts[1], parts[2]
    if not username or not password:
        raise ValueError("Username o password vacios")
    return username, password


def authenticate(users: UserDatabase, username: str, password: str) -> str:
    """Valida credenciales y devuelve la respuesta del protocolo de auth."""
    record = users.get(username)
    if record is None:
        return "FAIL|invalid_credentials\r\n"
    if record["password"] != password:
        return "FAIL|invalid_credentials\r\n"
    return f"OK|{record['role']}\r\n"


def handle_client(connection: socket.socket, address: Tuple[str, int], users: UserDatabase) -> None:
    """Procesa una conexion de cliente sin exponer la password en logs."""
    client_ip, client_port = address[0], address[1]

    try:
        message = recv_line(connection)
        username = "<desconocido>"

        try:
            username, password = parse_auth_request(message)
            response = authenticate(users, username, password)
            result = "OK" if response.startswith("OK|") else "FAIL"
            logging.info("ip=%s puerto=%s usuario=%s resultado=%s", client_ip, client_port, username, result)
            send_response(connection, response)
        except ValueError as exc:
            logging.warning(
                "ip=%s puerto=%s usuario=%s resultado=MALFORMED detalle=%s",
                client_ip,
                client_port,
                username,
                str(exc),
            )
            send_response(connection, "FAIL|malformed_request\r\n")
    except ConnectionError as exc:
        logging.warning("ip=%s puerto=%s resultado=CONNECTION_CLOSED detalle=%s", client_ip, client_port, str(exc))
    except OSError as exc:
        logging.warning("ip=%s puerto=%s resultado=IO_ERROR detalle=%s", client_ip, client_port, str(exc))
    finally:
        try:
            connection.close()
        except OSError:
            pass


def serve_forever(server_socket: socket.socket, users: UserDatabase) -> None:
    """Acepta conexiones concurrentes y continua ante errores de red."""
    while True:
        try:
            connection, address = server_socket.accept()
        except OSError as exc:
            logging.warning("resultado=ACCEPT_ERROR detalle=%s", str(exc))
            continue

        worker = threading.Thread(
            target=handle_client,
            args=(connection, address, users),
            daemon=True,
        )
        worker.start()


def parse_args() -> argparse.Namespace:
    """Parsea argumentos de linea de comandos."""
    parser = argparse.ArgumentParser(description="Servicio de autenticacion TCP para IoT")
    parser.add_argument("port", nargs="?", type=int, default=DEFAULT_PORT, help="Puerto TCP de escucha")
    return parser.parse_args()


def main() -> None:
    """Punto de entrada del servicio."""
    configure_logging()
    args = parse_args()
    users = load_users(USERS_FILE)
    server_socket = create_server_socket(args.port)
    logging.info("servicio iniciado puerto=%s usuarios=%s", args.port, len(users))

    try:
        serve_forever(server_socket, users)
    finally:
        try:
            server_socket.close()
        except OSError:
            pass


if __name__ == "__main__":
    main()
