FROM python:3.11-slim

WORKDIR /app

COPY auth_server.py .
COPY users.json .
COPY requirements.txt .

ENTRYPOINT ["python", "auth_server.py"]
