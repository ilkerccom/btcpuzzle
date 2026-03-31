#!/usr/bin/env bash

echo "nameserver 8.8.8.8" > /etc/resolv.conf
echo "nameserver 8.8.4.4" >> /etc/resolv.conf

GPUCOUNTS=$(nvidia-smi -L | wc -l)

if [ -z "$WORKERNAME" ]; then
    RANDOM_ID=$(shuf -i 100000-999999 -n 1)
    
    if [ "$CLOUDSEARCH_MODE" = "true" ]; then
        BASE_WORKER="cloud${RANDOM_ID}"
    else
        BASE_WORKER="worker${RANDOM_ID}"
    fi
else
    BASE_WORKER="$WORKERNAME"
fi

i=0
while [ $i -lt $GPUCOUNTS ]
do
  echo "[--] Starting btcpuzzle.info client on GPU #$i"

  CMD=("./btcpuzzle" "-gpu" "-gpuId" "$i")

  [ -n "$PUZZLE" ] && CMD+=("-puzzle" "$PUZZLE")
  [ -n "$USERTOKEN" ] && CMD+=("-token" "$USERTOKEN")
  [ -n "$APISHARE" ] && CMD+=("-apishare" "$APISHARE")
  [ -n "$PUBKEY" ] && CMD+=("-pubkey" "$PUBKEY")
  [ -n "$TELEGRAM_TOKEN" ] && CMD+=("-telegramtoken" "$TELEGRAM_TOKEN")
  [ -n "$TELEGRAM_CHATID" ] && CMD+=("-telegramchatid" "$TELEGRAM_CHATID")
  [ -n "$CUSTOM_RANGE" ] && CMD+=("-customrange" "$CUSTOM_RANGE")
  [ -n "$SAVE_KEY" ] && CMD+=("-savekey" "$SAVE_KEY")

  CURRENT_WORKER="${BASE_WORKER}_${i}"
  CMD+=("-worker" "$CURRENT_WORKER")

  "${CMD[@]}" &

  i=$((i+1))
  sleep 2
done

wait