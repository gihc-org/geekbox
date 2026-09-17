#!/bin/bash
# kill_bidi.sh - stop den kørende BiDi-klient (så BiDi-sessionen frigøres).
#
# Baggrund: Firefox tillader kun ÉN BiDi-session ad gangen. Skal man køre en
# ny klient (fx for at tage et screenshot), skal den gamle lukkes først.
# Mønsteret er delt i to strenge, så scriptet ikke matcher sin egen
# kommandolinje (en `pkill -f bidi_cyan.py` ville dræbe ssh-sessionen selv).
for p in $(pgrep -f "bidi_cy""an"); do
    kill "$p" 2>/dev/null && echo "stoppede BiDi-klient pid=$p"
done
exit 0
