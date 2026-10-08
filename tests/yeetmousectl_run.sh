#!/usr/bin/env bash
set -u

ctl=${1:?usage: yeetmousectl_run.sh <path to yeetmousectl>}
unset DBUS_SESSION_BUS_ADDRESS
failures=0

expect() {
    local want=$1 got
    shift
    "$@" > /dev/null 2>&1
    got=$?
    if [ "$got" != "$want" ]; then
        echo "FAIL: $* exited $got, want $want"
        failures=$((failures + 1))
    fi
}

expect 3 "$ctl" run absent-profile -- sh -c 'exit 3'
expect 127 "$ctl" run absent-profile -- /nonexistent/game
expect 2 "$ctl" run absent-profile sh

"$ctl" run absent-profile -- sh -c 'trap "exit 7" TERM; sleep 30 & wait' > /dev/null 2>&1 &
wrapper=$!
sleep 1
kill -TERM "$wrapper"
wait "$wrapper"
got=$?
if [ "$got" != 7 ]; then
    echo "FAIL: a terminated run exited $got, want the game's own 7"
    failures=$((failures + 1))
fi

if [ "$failures" -eq 0 ]; then
    echo "yeetmousectl run: all checks passed"
fi
exit $((failures > 0))
