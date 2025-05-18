#!/bin/bash

KEY_NAME="mod_signing_key"
VALIDITY_IN_DAYS=7300

if [ -f $KEY_NAME.priv ]; then
    echo "Error: $KEY_NAME.priv already exists."
    exit 1
elif [ -f $KEY_NAME.der ]; then
    echo "Error: $KEY_NAME.der already exists."
    exit 1
fi

openssl req -config ./openssl.config -new -x509 -newkey rsa:2048 -nodes \
    -days $VALIDITY_IN_DAYS -outform DER -keyout "$KEY_NAME.priv" \
    -out "$KEY_NAME.der"
