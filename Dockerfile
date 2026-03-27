# btcpuzzle.info Docker file

# -------- BUILD STAGE --------
FROM nvidia/cuda:12.0.0-devel-ubuntu20.04 AS build

WORKDIR /build
RUN apt-get update && apt-get install -y \
    build-essential libcurl4-openssl-dev libssl-dev

COPY . .
RUN make gpu=1 all


# -------- RUNTIME STAGE --------
FROM nvidia/cuda:12.0.0-runtime-ubuntu20.04

WORKDIR /app
RUN apt-get update && apt-get install -y \
    build-essential libcurl4-openssl-dev libssl-dev dos2unix

ENV PUZZLE=71
ENV USERTOKEN=""
ENV WORKERNAME=""
ENV APISHARE=""
ENV PUBKEY=""
ENV TELEGRAM_TOKEN=""
ENV TELEGRAM_CHATID=""
ENV CUSTOM_RANGE=""
ENV CLOUDSEARCH_MODE="false"

COPY --from=build /build/vanitysearch /app/btcpuzzle
COPY btcpuzzle.sh /app/btcpuzzle.sh
COPY pool.conf /app/pool.conf

RUN dos2unix /app/btcpuzzle.sh && \
    chmod +x /app/btcpuzzle.sh && \
    chmod +x /app/btcpuzzle

WORKDIR /app
ENTRYPOINT ["/app/btcpuzzle.sh"]