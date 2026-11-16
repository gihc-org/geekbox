#!/bin/bash
# Start den lokale spilkæde i en terminal, så bring-up-fejl er synlige.
exec lxterminal --no-remote --title="GeekBox - Subway Surfers" \
    --geometry=90x24 \
    --command="/usr/local/bin/subway-surfers-terminal"
