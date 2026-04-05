FROM gcc:13 AS builder

WORKDIR /build

COPY Makefile .
COPY src ./src
COPY web ./web

RUN make

FROM debian:bookworm-slim

WORKDIR /app

RUN mkdir -p /app/logs /app/web

COPY --from=builder /build/server /app/server
COPY --from=builder /build/web /app/web

EXPOSE 9000 8080

ENTRYPOINT ["./server"]
CMD ["9000", "/app/logs/server.log"]
