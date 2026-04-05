# Protocolo IOTP

## 1. Proposito y alcance

IOTP (IoT Text Protocol) es el protocolo de capa de aplicacion del proyecto de monitoreo IoT. Define como se comunican sensores, operadores y servidor central para:

- autenticar y registrar una sesion;
- reportar mediciones;
- consultar el estado del sistema;
- distribuir alertas de anomalia;
- cerrar sesiones de manera limpia.

Este documento fija la especificacion formal del Modulo 1 y debe ser respetado por todos los clientes y por el servidor central.

## 2. Resumen del protocolo

### 2.1 Proposito

IOTP es un protocolo textual, simple y stateful. Su objetivo es ofrecer un contrato unico entre:

- sensores simulados que envian mediciones;
- operadores que consultan informacion y reciben alertas;
- el servidor central que autentica, procesa y enruta mensajes.

### 2.2 Modelo de comunicacion

- Modelo principal: cliente-servidor.
- Modelo de sesion: stateful.
- Regla obligatoria: todo cliente DEBE completar `REGISTER` con exito antes de enviar cualquier otro comando.
- Regla de respuesta: todo request sobre TCP DEBE recibir exactamente una response.
- Excepcion permitida: `DATA` puede operar en modo fire-and-forget solo si existe una extension explicita sobre UDP. La especificacion base de este documento asume TCP.

### 2.3 Transporte usado

- Transporte normativo: TCP.
- Transporte opcional: UDP solo para `DATA`, si una implementacion decide justificarlo. En ese caso, `REGISTER`, `QUERY`, `STATUS` y `DISCONNECT` siguen siendo TCP.
- Codificacion: texto ASCII plano.
- Delimitador de campos: `|`.
- Terminador de mensaje: `\r\n`.

## 3. Invariantes obligatorias del Modulo 1

- I1: Todo mensaje es texto plano ASCII. No se permite contenido binario.
- I2: Todo mensaje usa exactamente la estructura `OPCODE|CAMPO1|CAMPO2|...\r\n`, con `|` como delimitador y `\r\n` como terminador.
- I3: Los opcodes forman un conjunto cerrado. Todo opcode desconocido genera `ERROR|UNKNOWN_OP|<opcode_recibido>`.
- I4: Todo request sobre TCP recibe exactamente una response. Solo `DATA` sobre UDP puede operar sin response si se implementa como extension.
- I5: Todo valor numerico usa `.` como separador decimal.
- I6: El protocolo es stateful. Ningun cliente puede usar comandos distintos de `REGISTER` antes de quedar autenticado.

## 4. Roles y estados de sesion

### 4.1 Roles validos

- `sensor`
- `operator`

### 4.2 Maquina de estados

| Estado | Descripcion | Mensajes permitidos |
|---|---|---|
| `CONNECTED` | Conexion TCP abierta pero aun no autenticada | `REGISTER` |
| `AUTHENTICATED_SENSOR` | Sensor autenticado | `DATA`, `DISCONNECT` |
| `AUTHENTICATED_OPERATOR` | Operador autenticado | `QUERY`, `STATUS`, `DISCONNECT` |
| `CLOSED` | Conexion cerrada | Ninguno |

### 4.3 Reglas de transicion

- `CONNECTED -> AUTHENTICATED_SENSOR` mediante `REGISTER` exitoso con rol `sensor`.
- `CONNECTED -> AUTHENTICATED_OPERATOR` mediante `REGISTER` exitoso con rol `operator`.
- Todo intento de usar un opcode no permitido para el rol autenticado DEBE responder con `ERROR|FORBIDDEN_OP|<detalle>`.
- `DISCONNECT` exitoso cierra la sesion y mueve la conexion a `CLOSED`.

## 5. Formato de mensaje

### 5.1 Regla general

Todo frame IOTP sigue la forma:

```text
OPCODE|CAMPO1|CAMPO2|...\r\n
```

Reglas adicionales:

- Ningun campo puede contener `|`, `\r` o `\n`.
- Todos los campos DEBEN ser ASCII imprimible.
- Los mensajes son sensibles a mayusculas y minusculas en los valores de negocio. Los opcodes y enums definidos en este documento DEBEN enviarse exactamente como aparecen aqui.

### 5.2 ABNF simplificada

```abnf
message        = register / ack / error / data / alert /
                 query / result / status / statusr / disconnect

register       = "REGISTER" "|" role "|" username "|" password CR LF
ack            = "ACK" "|" text CR LF
error          = "ERROR" "|" error-code "|" text CR LF
data           = "DATA" "|" sensor-id "|" sensor-type "|" decimal "|" timestamp CR LF
alert          = "ALERT" "|" sensor-id "|" sensor-type "|" decimal "|" decimal "|" timestamp CR LF
query          = "QUERY" "|" query-target CR LF
result         = "RESULT" "|" query-target "|" payload CR LF
status         = "STATUS" CR LF
statusr        = "STATUSR" "|" uint "|" uint "|" uint CR LF
disconnect     = "DISCONNECT" "|" client-id CR LF

role           = "sensor" / "operator"
query-target   = "SENSORS" / "MEASUREMENTS" / "ALERTS"
sensor-type    = "temperatura" / "vibracion" / "energia"
error-code     = 1*(ALPHA / DIGIT / "_")

username       = 1*id-char
password       = 1*field-char
sensor-id      = username
client-id      = username
text           = 1*field-char
payload        = 1*field-char
uint           = 1*DIGIT
decimal        = ["-"] 1*DIGIT ["." 1*DIGIT]
timestamp      = 4DIGIT "-" 2DIGIT "-" 2DIGIT "T"
                 2DIGIT ":" 2DIGIT ":" 2DIGIT
                 ("Z" / (("+" / "-") 2DIGIT ":" 2DIGIT))

id-char        = ALPHA / DIGIT / "_" / "-"
field-char     = SP / %x21-7E
                 ; excepto "|", CR y LF
```

### 5.3 Tipos de dato

| Tipo | Regla |
|---|---|
| `username`, `sensor_id`, `client_id` | ASCII, 1 o mas caracteres, recomendado `[A-Za-z0-9_-]+` |
| `password` | ASCII imprimible, sin `|`, `CR`, `LF` |
| `decimal` | Decimal en base 10 con `.` como separador |
| `uint` | Entero decimal sin signo |
| `timestamp` | ISO-8601 / RFC3339 ASCII, por ejemplo `2026-04-02T10:15:30Z` |
| `text` | Texto ASCII libre, sin `|`, `CR`, `LF` |
| `payload` | Campo ASCII unico, sin `|`, `CR`, `LF` |

## 6. Tabla completa de opcodes

| Opcode | Direccion | Emisor valido | Campos | Tipos de dato | Respuesta/efecto |
|---|---|---|---|---|---|
| `REGISTER` | Cliente -> Servidor | Sensor u operador aun no autenticado | `rol`, `username`, `password` | `role`, `username`, `password` | Responde `ACK` si autentica; `ERROR` si falla |
| `ACK` | Servidor -> Cliente | Servidor | `mensaje` | `text` | Confirmacion exitosa. Se usa como respuesta a `REGISTER`, `DATA` y `DISCONNECT` |
| `ERROR` | Servidor -> Cliente | Servidor | `codigo`, `descripcion` | `error-code`, `text` | Error semantico, de autenticacion o de formato |
| `DATA` | Sensor -> Servidor | Cliente autenticado como sensor | `sensor_id`, `tipo`, `valor`, `timestamp` | `username`, `sensor-type`, `decimal`, `timestamp` | Responde `ACK` o `ERROR`. Si hay anomalia, ademas dispara `ALERT` a operadores |
| `ALERT` | Servidor -> Operador | Servidor | `sensor_id`, `tipo`, `valor`, `umbral`, `timestamp` | `username`, `sensor-type`, `decimal`, `decimal`, `timestamp` | Notificacion asincronica de anomalia a cada operador autenticado |
| `QUERY` | Operador -> Servidor | Cliente autenticado como operador | `target` | `query-target` | Responde `RESULT` o `ERROR` |
| `RESULT` | Servidor -> Operador | Servidor | `tipo_query`, `payload` | `query-target`, `payload` | Respuesta a `QUERY` |
| `STATUS` | Operador -> Servidor | Cliente autenticado como operador | sin campos | sin campos | Responde `STATUSR` o `ERROR` |
| `STATUSR` | Servidor -> Operador | Servidor | `sensores_activos`, `operadores`, `uptime_segundos` | `uint`, `uint`, `uint` | Respuesta a `STATUS` |
| `DISCONNECT` | Cliente -> Servidor | Sensor u operador autenticado | `id` | `client-id` | Responde `ACK` y luego cierra la conexion |

### 6.1 Semantica por opcode

#### REGISTER

Formato:

```text
REGISTER|rol|username|password\r\n
```

Reglas:

- Solo es valido en estado `CONNECTED`.
- El servidor DEBE consultar al servicio de autenticacion antes de aceptar la sesion.
- El servidor DEBE comparar el rol solicitado con el rol devuelto por autenticacion.
- Respuesta exitosa recomendada:

```text
ACK|Registered as <rol>\r\n
```

Errores comunes:

- `ERROR|INVALID_ROLE|...`
- `ERROR|INVALID_CREDENTIALS|...`
- `ERROR|ROLE_MISMATCH|...`
- `ERROR|AUTH_UNAVAILABLE|...`

#### DATA

Formato:

```text
DATA|sensor_id|tipo|valor|timestamp\r\n
```

Reglas:

- Solo es valido para sesiones autenticadas como `sensor`.
- `sensor_id` DEBE coincidir con el `username` autenticado en `REGISTER`.
- `tipo` DEBE ser `temperatura`, `vibracion` o `energia`.
- `valor` DEBE usar `.` como separador decimal.
- `timestamp` DEBE ser ASCII ISO-8601.
- Respuesta exitosa recomendada:

```text
ACK|Data received\r\n
```

- Si la medicion cruza un umbral de anomalia, el servidor responde igual con `ACK` al sensor y, adicionalmente, emite `ALERT` a todos los operadores conectados.

#### ALERT

Formato:

```text
ALERT|sensor_id|tipo|valor|umbral|timestamp\r\n
```

Reglas:

- Es un mensaje asincronico del servidor hacia operadores autenticados.
- `umbral` DEBE contener exactamente el limite violado.
- Para `temperatura`, si `valor < 15.0`, el campo `umbral` DEBE ser `15.0`; si `valor > 80.0`, DEBE ser `80.0`.

#### QUERY

Formato:

```text
QUERY|target\r\n
```

Targets validos:

- `SENSORS`
- `MEASUREMENTS`
- `ALERTS`

Solo es valido para sesiones autenticadas como `operator`.

#### RESULT

Formato:

```text
RESULT|tipo_query|payload\r\n
```

Reglas:

- `tipo_query` DEBE repetir exactamente el target pedido en `QUERY`.
- `payload` ocupa un unico campo, por lo tanto NO puede contener `|`, `\r` ni `\n`.
- Formato recomendado del `payload`: JSON compacto ASCII en una sola linea.
- Formato alternativo permitido: CSV de una sola linea, siempre que siga sin usar `|`.

Payload recomendado por target:

| `tipo_query` | Contenido minimo esperado en `payload` |
|---|---|
| `SENSORS` | lista de sensores con `sensor_id`, `tipo`, `ultimo_valor`, `ultimo_timestamp`, `estado` |
| `MEASUREMENTS` | lista de mediciones recientes con `sensor_id`, `tipo`, `valor`, `timestamp` |
| `ALERTS` | lista de alertas recientes con `sensor_id`, `tipo`, `valor`, `umbral`, `timestamp` |

#### STATUS

Formato:

```text
STATUS\r\n
```

Solo es valido para sesiones autenticadas como `operator`.

#### STATUSR

Formato:

```text
STATUSR|sensores_activos|operadores|uptime_segundos\r\n
```

Reglas:

- `sensores_activos`: cantidad actual de sensores autenticados.
- `operadores`: cantidad actual de operadores autenticados.
- `uptime_segundos`: segundos transcurridos desde el arranque del servidor.

#### DISCONNECT

Formato:

```text
DISCONNECT|id\r\n
```

Reglas:

- `id` DEBE coincidir con la identidad autenticada de la sesion.
- El servidor DEBE responder con `ACK` antes de cerrar la conexion TCP.
- Respuesta exitosa recomendada:

```text
ACK|Disconnected\r\n
```

## 7. Diagramas de secuencia principales

### 7.1 Registro de sensor

```text
Sensor                      Servidor                    Auth Service
  |                            |                             |
  | REGISTER|sensor|temp01|pw  |                             |
  |--------------------------->|                             |
  |                            | AUTH|temp01|pw             |
  |                            |---------------------------->|
  |                            | OK|sensor                  |
  |                            |<----------------------------|
  | ACK|Registered as sensor   |                             |
  |<---------------------------|                             |
  |                            |                             |
```

### 7.2 Registro de operador

```text
Operador                    Servidor                    Auth Service
  |                            |                             |
  | REGISTER|operator|op01|pw  |                             |
  |--------------------------->|                             |
  |                            | AUTH|op01|pw               |
  |                            |---------------------------->|
  |                            | OK|operator                |
  |                            |<----------------------------|
  | ACK|Registered as operator |                             |
  |<---------------------------|                             |
  |                            |                             |
```

### 7.3 Envio de medicion normal

```text
Sensor                      Servidor
  |                            |
  | DATA|temp01|temperatura|72.5|2026-04-02T10:15:30Z
  |--------------------------->|
  | ACK|Data received
  |<---------------------------|
  |                            |
```

### 7.4 Envio de medicion que dispara alerta

```text
Sensor                      Servidor                     Operador
  |                            |                            |
  | DATA|temp01|temperatura|92.3|2026-04-02T10:16:00Z      |
  |--------------------------->|                            |
  | ACK|Data received          |                            |
  |<---------------------------|                            |
  |                            | ALERT|temp01|temperatura|92.3|80.0|2026-04-02T10:16:00Z
  |                            |--------------------------->|
  |                            |                            |
```

### 7.5 Consulta QUERY y STATUS

```text
Operador                    Servidor
  |                            |
  | QUERY|SENSORS              |
  |--------------------------->|
  | RESULT|SENSORS|[{"sensor_id":"temp01","tipo":"temperatura","estado":"online"}]
  |<---------------------------|
  |                            |
  | STATUS                     |
  |--------------------------->|
  | STATUSR|5|2|3600           |
  |<---------------------------|
  |                            |
```

### 7.6 Desconexion limpia

```text
Cliente                     Servidor
  |                            |
  | DISCONNECT|temp01          |
  |--------------------------->|
  | ACK|Disconnected           |
  |<---------------------------|
  | TCP FIN / close            |
  |<==========================>|
```

### 7.7 Fallo de autenticacion

```text
Cliente                     Servidor                    Auth Service
  |                            |                             |
  | REGISTER|sensor|temp01|bad |                             |
  |--------------------------->|                             |
  |                            | AUTH|temp01|bad            |
  |                            |---------------------------->|
  |                            | FAIL|invalid_credentials   |
  |                            |<----------------------------|
  | ERROR|INVALID_CREDENTIALS|authentication failed         |
  |<---------------------------|                             |
```

### 7.8 Auth service no disponible

```text
Cliente                     Servidor                    Auth Service
  |                            |                             |
  | REGISTER|operator|op01|pw  |                             |
  |--------------------------->|                             |
  |                            | AUTH|op01|pw               |
  |                            |---------------------------->|
  |                            |   timeout / connection error
  | ERROR|AUTH_UNAVAILABLE|auth service unavailable         |
  |<---------------------------|                             |
```

## 8. Codigos de error

Todos los errores IOTP usan el formato:

```text
ERROR|codigo|descripcion\r\n
```

| Codigo | Cuando se usa | Descripcion esperada |
|---|---|---|
| `UNKNOWN_OP` | Opcode no definido por IOTP | El servidor recibio un opcode fuera del conjunto cerrado |
| `NOT_AUTHENTICATED` | Se intenta usar un comando antes de `REGISTER` exitoso | La sesion aun no esta autenticada |
| `INVALID_ROLE` | El campo `rol` no es `sensor` ni `operator` | Rol no soportado |
| `INVALID_CREDENTIALS` | El servicio de autenticacion rechazo las credenciales | Usuario o password invalidos |
| `ROLE_MISMATCH` | El rol pedido en `REGISTER` no coincide con el rol devuelto por auth | El cliente solicito un rol distinto al autorizado |
| `AUTH_UNAVAILABLE` | El servidor no pudo contactar al servicio de autenticacion o este agoto tiempo | Fallo fail-closed de autenticacion |
| `FORBIDDEN_OP` | El opcode no esta permitido para el rol autenticado | Por ejemplo, un sensor intenta `QUERY` |
| `INVALID_TARGET` | `QUERY` usa un target distinto de `SENSORS`, `MEASUREMENTS` o `ALERTS` | Target no reconocido |
| `INVALID_SENSOR_TYPE` | `DATA` usa un tipo distinto de `temperatura`, `vibracion` o `energia` | Tipo de sensor no reconocido |
| `INVALID_VALUE` | Un campo numerico no cumple el formato decimal esperado | Numero mal formado |
| `INVALID_TIMESTAMP` | El timestamp no cumple la forma ISO-8601 esperada | Timestamp invalido |
| `INVALID_FIELD_COUNT` | El mensaje tiene menos o mas campos que los requeridos para su opcode | Aridad incorrecta |
| `ID_MISMATCH` | `sensor_id` o `id` no coincide con la identidad autenticada de la sesion | Integridad de sesion violada |
| `MALFORMED_MESSAGE` | El frame contiene caracteres no ASCII, delimitadores invalidos o no puede parsearse | Mensaje malformado |
| `INTERNAL_ERROR` | Falla interna no clasificable del servidor | Error interno del servidor |

## 9. Umbrales de anomalia por tipo de sensor

| Tipo de sensor | Rango operativo nominal | Regla de alerta | Valor que debe ir en `ALERT.umbral` |
|---|---|---|---|
| `temperatura` | `15.0` a `90.0` | alerta si `valor < 15.0` o `valor > 80.0` | `15.0` si cae por debajo; `80.0` si supera el maximo de alerta |
| `vibracion` | `0.0` a `50.0` | alerta si `valor > 40.0` | `40.0` |
| `energia` | `0.0` a `500.0` | alerta si `valor > 450.0` | `450.0` |

Notas:

- El rango operativo nominal describe el dominio esperado de generacion de datos.
- La regla de alerta define el punto a partir del cual el servidor DEBE emitir `ALERT`.
- Un valor fuera de rango tambien sigue siendo una `DATA` valida desde el punto de vista sintactico; la consecuencia es semantica: registrar la medicion y generar alerta.

## 10. Manejo de errores y fallos

### 10.1 Timeout

- Los clientes del proyecto DEBEN aplicar timeout de lectura de 30 segundos sobre la conexion TCP.
- Si un cliente envia un request y no recibe response dentro del timeout, DEBE cerrar la conexion limpiamente y decidir si reintenta segun su propia politica.
- Si el servidor no logra obtener respuesta del servicio de autenticacion en el tiempo de espera configurado, DEBE responder `ERROR|AUTH_UNAVAILABLE|...` y NO autenticar al cliente.

### 10.2 Mensaje malformado

Un mensaje se considera malformado cuando ocurre al menos una de estas condiciones:

- no termina en `\r\n`;
- contiene bytes no ASCII;
- contiene `|` dentro de un campo;
- tiene un numero de campos incorrecto para su opcode;
- un campo obligatorio esta vacio;
- un decimal o timestamp no cumple el formato esperado.

Comportamiento requerido:

- Si el opcode puede identificarse pero el resto del frame es invalido, el servidor DEBE responder `ERROR|MALFORMED_MESSAGE|...` o un error mas especifico como `INVALID_FIELD_COUNT`, `INVALID_VALUE` o `INVALID_TIMESTAMP`.
- Si el frame es imposible de recuperar de forma segura, la implementacion PUEDE cerrar la conexion despues de reportar el error si ello es tecnicamente posible.

### 10.3 Opcode desconocido

Comportamiento requerido:

```text
ERROR|UNKNOWN_OP|<opcode_recibido>\r\n
```

Reglas:

- El servidor NO debe reinterpretar ni aproximar opcodes.
- El opcode recibido se reporta tal cual llego, siempre que sea representable en ASCII seguro.

### 10.4 Violaciones de estado o rol

- Antes de autenticarse, cualquier comando distinto de `REGISTER` DEBE producir `ERROR|NOT_AUTHENTICATED|REGISTER required`.
- Si un sensor autenticado intenta `QUERY` o `STATUS`, el servidor DEBE responder `ERROR|FORBIDDEN_OP|operator only`.
- Si un operador autenticado intenta `DATA`, el servidor DEBE responder `ERROR|FORBIDDEN_OP|sensor only`.

### 10.5 Fallos internos

- Si una operacion del servidor falla despues de recibir un request valido y no existe un codigo mas preciso, la respuesta DEBE ser `ERROR|INTERNAL_ERROR|...`.
- El servidor no debe terminar su ejecucion por un error aislado de una sesion.

## 11. Reglas de interoperabilidad

- Todos los participantes DEBEN usar DNS para localizar el servidor y, cuando aplique, el auth service; no se permiten IPs literales en el codigo del proyecto.
- El servidor central NO debe persistir passwords ni escribirlas en logs.
- La respuesta de autenticacion externa `OK|rol` o `FAIL|reason` pertenece al protocolo interno del auth service; no reemplaza la respuesta IOTP hacia el cliente.
- Las respuestas `RESULT` y `STATUSR` cuentan como la unica response de sus requests correspondientes.
- Los mensajes `ALERT` son asincronicos y no invalidan la regla de "una response por request", porque no son respuesta a un request del operador sino eventos push del servidor.
